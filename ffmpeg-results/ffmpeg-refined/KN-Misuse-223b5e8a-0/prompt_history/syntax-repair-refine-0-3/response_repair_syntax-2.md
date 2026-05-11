```cpp
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Checkers/Taint.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/Environment.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymExpr.h"
#include "clang/StaticAnalyzer/Frontend/CheckerRegistry.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtVisitor.h"
#include "llvm/Support/raw_ostream.h"
#include "clang/StaticAnalyzer/Checkers/utility.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Decl.h"
#include "llvm/ADT/APSInt.h"
#include <optional>

using namespace clang;
using namespace ento;
using namespace taint;

namespace {

struct AggregateDenomInfo {
  bool ZeroInit;
  bool AggregateUpdated;
  bool GuardedNonZero;
  bool KnownNonZero;

  AggregateDenomInfo(bool Z = false, bool A = false, bool G = false, bool K = false)
      : ZeroInit(Z), AggregateUpdated(A), GuardedNonZero(G), KnownNonZero(K) {}

  bool operator==(const AggregateDenomInfo &Other) const {
    return ZeroInit == Other.ZeroInit &&
           AggregateUpdated == Other.AggregateUpdated &&
           GuardedNonZero == Other.GuardedNonZero &&
           KnownNonZero == Other.KnownNonZero;
  }

  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.AddBoolean(ZeroInit);
    ID.AddBoolean(AggregateUpdated);
    ID.AddBoolean(GuardedNonZero);
    ID.AddBoolean(KnownNonZero);
  }
};

} // end anonymous namespace

REGISTER_MAP_WITH_PROGRAMSTATE(AggregateDenomMap, const MemRegion *, AggregateDenomInfo)

namespace {

static bool isRealFloatingType(QualType QT) {
  return !QT.isNull() && QT->isRealFloatingType();
}

static bool isZeroLiteralExpr(const Expr *E, CheckerContext &C) {
  if (!E)
    return false;

  E = E->IgnoreParenImpCasts();
  if (!E)
    return false;

  if (const auto *FL = dyn_cast<FloatingLiteral>(E))
    return FL->getValue().isZero();

  if (const auto *IL = dyn_cast<IntegerLiteral>(E))
    return IL->getValue().isZero();

  llvm::APSInt Res;
  if (EvaluateExprToInt(Res, E, C)) {
    return Res == 0;
  }

  return false;
}

static bool isPositiveConstantExpr(const Expr *E, CheckerContext &C) {
  if (!E)
    return false;

  E = E->IgnoreParenImpCasts();
  if (!E)
    return false;

  if (const auto *FL = dyn_cast<FloatingLiteral>(E))
    return !FL->getValue().isNegative() && !FL->getValue().isZero();

  if (const auto *IL = dyn_cast<IntegerLiteral>(E))
    return IL->getValue().sgt(0);

  llvm::APSInt IntRes;
  if (EvaluateExprToInt(IntRes, E, C))
    return IntRes > 0;

  Expr::EvalResult ER;
  if (E->EvaluateAsRValue(ER, C.getASTContext()) && ER.Val.isFloat()) {
    llvm::APFloat F = ER.Val.getFloat();
    return !F.isNegative() && !F.isZero();
  }

  return false;
}

static const MemRegion *getVarRegionFromDecl(const VarDecl *VD, CheckerContext &C) {
  if (!VD)
    return nullptr;

  const LocationContext *LCtx = C.getLocationContext();
  if (!LCtx)
    return nullptr;

  const MemRegion *MR = C.getState()->getRegion(VD, LCtx);
  if (!MR)
    return nullptr;

  return MR->getBaseRegion();
}

static const MemRegion *getRegionFromTrackedExpr(const Expr *E, CheckerContext &C) {
  if (!E)
    return nullptr;

  E = E->IgnoreParenImpCasts();
  if (!E)
    return nullptr;

  const MemRegion *MR = getMemRegionFromExpr(E, C);
  if (!MR)
    return nullptr;

  return MR->getBaseRegion();
}

static bool refersToRegion(const Expr *E, const MemRegion *Target, CheckerContext &C) {
  if (!E || !Target)
    return false;

  E = E->IgnoreParenImpCasts();

  if (const MemRegion *MR = getRegionFromTrackedExpr(E, C))
    return MR == Target;

  for (const Stmt *Child : E->children()) {
    if (const auto *CE = dyn_cast_or_null<Expr>(Child)) {
      if (refersToRegion(CE, Target, C))
        return true;
    }
  }
  return false;
}

static const MemRegion *getTrackedFloatingVar(const Expr *E, CheckerContext &C) {
  E = E ? E->IgnoreParenImpCasts() : nullptr;
  if (!E)
    return nullptr;

  const auto *DRE = dyn_cast<DeclRefExpr>(E);
  if (!DRE)
    return nullptr;

  const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
  if (!VD)
    return nullptr;

  if (!isRealFloatingType(VD->getType()))
    return nullptr;

  return getVarRegionFromDecl(VD, C);
}

static bool conditionProvesExprNonZero(const Expr *Cond, const MemRegion *MR,
                                       CheckerContext &C) {
  if (!Cond || !MR)
    return false;

  Cond = Cond->IgnoreParenImpCasts();
  if (!Cond)
    return false;

  if (const auto *DRE = dyn_cast<DeclRefExpr>(Cond)) {
    const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
    if (VD && isRealFloatingType(VD->getType())) {
      const MemRegion *CondMR = getVarRegionFromDecl(VD, C);
      return CondMR == MR;
    }
  }

  const auto *BO = dyn_cast<BinaryOperator>(Cond);
  if (!BO)
    return false;

  const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
  const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();

  const MemRegion *LMR = getTrackedFloatingVar(LHS, C);
  const MemRegion *RMR = getTrackedFloatingVar(RHS, C);

  auto ProvesWithPositiveBound = [&](BinaryOperatorKind Op,
                                     const MemRegion *SideMR,
                                     const Expr *OtherSide,
                                     bool SideIsLHS) -> bool {
    if (SideMR != MR)
      return false;

    switch (Op) {
    case BO_GT:
      return SideIsLHS ? isPositiveConstantExpr(OtherSide, C) : false;
    case BO_GE:
      return SideIsLHS ? isPositiveConstantExpr(OtherSide, C) : false;
    case BO_LT:
      return !SideIsLHS ? isPositiveConstantExpr(OtherSide, C) : false;
    case BO_LE:
      return !SideIsLHS ? isPositiveConstantExpr(OtherSide, C) : false;
    case BO_NE:
      return isZeroLiteralExpr(OtherSide, C);
    default:
      return false;
    }
  };

  if (ProvesWithPositiveBound(BO->getOpcode(), LMR, RHS, true))
    return true;
  if (ProvesWithPositiveBound(BO->getOpcode(), RMR, LHS, false))
    return true;

  if (BO->getOpcode() == BO_GT || BO->getOpcode() == BO_GE) {
    if (LMR == MR && isPositiveConstantExpr(RHS, C))
      return true;
  }
  if (BO->getOpcode() == BO_LT || BO->getOpcode() == BO_LE) {
    if (RMR == MR && isPositiveConstantExpr(LHS, C))
      return true;
  }

  return false;
}

static bool isAggregateUpdateOfRegion(const BinaryOperator *BO,
                                      const MemRegion *MR,
                                      CheckerContext &C) {
  if (!BO || !MR)
    return false;

  BinaryOperatorKind Op = BO->getOpcode();
  const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
  const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();

  const MemRegion *LHSMR = getRegionFromTrackedExpr(LHS, C);
  if (LHSMR != MR)
    return false;

  if (Op == BO_AddAssign || Op == BO_SubAssign)
    return true;

  if (Op == BO_Assign) {
    if (const auto *RHSBO = dyn_cast<BinaryOperator>(RHS)) {
      BinaryOperatorKind RHOp = RHSBO->getOpcode();
      if (RHOp == BO_Add || RHOp == BO_Sub) {
        return refersToRegion(RHSBO->getLHS(), MR, C) ||
               refersToRegion(RHSBO->getRHS(), MR, C);
      }
    }
  }

  return false;
}

static bool isKnownNonZeroExpr(const Expr *E, CheckerContext &C) {
  if (!E)
    return false;

  E = E->IgnoreParenImpCasts();
  if (!E)
    return false;

  Expr::EvalResult ER;
  if (E->EvaluateAsRValue(ER, C.getASTContext())) {
    if (ER.Val.isFloat()) {
      llvm::APFloat F = ER.Val.getFloat();
      return !F.isZero();
    }
    if (ER.Val.isInt()) {
      return ER.Val.getInt() != 0;
    }
  }

  return false;
}

static bool isFalsePositive(const BinaryOperator *BO, CheckerContext &C) {
  if (!BO || BO->getOpcode() != BO_Div)
    return true;

  const Expr *Denom = BO->getRHS()->IgnoreParenImpCasts();
  if (!Denom)
    return true;

  ProgramStateRef State = C.getState();
  SVal DenomV = State->getSVal(Denom, C.getLocationContext());
  std::optional<DefinedSVal> DV = DenomV.getAs<DefinedSVal>();
  if (DV) {
    ProgramStateRef ZeroState, NonZeroState;
    std::tie(ZeroState, NonZeroState) = State->assume(*DV);
    if (!ZeroState && NonZeroState)
      return true;
  }

  return false;
}

class SAGenTestChecker
    : public Checker<check::PostStmt<DeclStmt>,
                     check::PreStmt<BinaryOperator>,
                     check::BranchCondition,
                     eval::Assume> {
  mutable std::unique_ptr<BugType> BT;

public:
  SAGenTestChecker()
      : BT(new BugType(this, "Possible division by zero",
                       "Possible division by zero")) {}

  void checkPostStmt(const DeclStmt *DS, CheckerContext &C) const;
  void checkPreStmt(const BinaryOperator *BO, CheckerContext &C) const;
  void checkBranchCondition(const Stmt *Condition, CheckerContext &C) const;
  ProgramStateRef evalAssume(ProgramStateRef State, SVal Cond,
                             bool Assumption) const;

private:
  void reportBug(const BinaryOperator *BO, CheckerContext &C,
                 const MemRegion *MR) const;
};

void SAGenTestChecker::checkPostStmt(const DeclStmt *DS,
                                     CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  bool Changed = false;

  for (const Decl *D : DS->decls()) {
    const auto *VD = dyn_cast<VarDecl>(D);
    if (!VD)
      continue;
    if (!VD->hasLocalStorage())
      continue;
    if (!isRealFloatingType(VD->getType()))
      continue;

    const Expr *Init = VD->getInit();
    if (!Init)
      continue;

    if (!isZeroLiteralExpr(Init, C))
      continue;

    const MemRegion *MR = getVarRegionFromDecl(VD, C);
    if (!MR)
      continue;

    State = State->set<AggregateDenomMap>(
        MR, AggregateDenomInfo(true, false, false, false));
    Changed = true;
  }

  if (Changed)
    C.addTransition(State);
}

void SAGenTestChecker::checkBranchCondition(const Stmt *Condition,
                                            CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  const Expr *CondE = dyn_cast_or_null<Expr>(Condition);
  if (!CondE)
    return;

  bool Changed = false;
  auto Map = State->get<AggregateDenomMap>();

  for (auto I : Map) {
    const MemRegion *MR = I.first;
    AggregateDenomInfo Info = I.second;

    if (conditionProvesExprNonZero(CondE, MR, C)) {
      Info.GuardedNonZero = true;
      Info.KnownNonZero = true;
      State = State->set<AggregateDenomMap>(MR, Info);
      Changed = true;
    }
  }

  if (Changed)
    C.addTransition(State);
}

ProgramStateRef SAGenTestChecker::evalAssume(ProgramStateRef State, SVal Cond,
                                             bool Assumption) const {
  std::optional<DefinedSVal> DV = Cond.getAs<DefinedSVal>();
  if (!DV)
    return State;

  auto Map = State->get<AggregateDenomMap>();
  for (auto I : Map) {
    const MemRegion *MR = I.first;
    AggregateDenomInfo Info = I.second;

    if (Info.KnownNonZero)
      continue;

    SymbolRef Sym = nullptr;
    if (const auto *SR = dyn_cast<SymbolicRegion>(MR))
      Sym = SR->getSymbol();

    if (!Sym)
      continue;

    SValBuilder &SVB = State->getStateManager().getSValBuilder();
    DefinedOrUnknownSVal VarVal = SVB.makeSymbolVal(Sym);

    ProgramStateRef TrueSt, FalseSt;
    std::tie(FalseSt, TrueSt) = State->assume(VarVal.castAs<DefinedSVal>());

    if (Assumption && TrueSt && !FalseSt) {
      Info.KnownNonZero = true;
      State = State->set<AggregateDenomMap>(MR, Info);
    }
  }

  return State;
}

void SAGenTestChecker::checkPreStmt(const BinaryOperator *BO,
                                    CheckerContext &C) const {
  if (!BO)
    return;

  ProgramStateRef State = C.getState();

  if (BO->isAssignmentOp()) {
    const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
    const MemRegion *LHSMR = getRegionFromTrackedExpr(LHS, C);
    if (LHSMR) {
      const AggregateDenomInfo *InfoPtr = State->get<AggregateDenomMap>(LHSMR);
      if (InfoPtr) {
        AggregateDenomInfo Info = *InfoPtr;
        bool Changed = false;

        if (isAggregateUpdateOfRegion(BO, LHSMR, C)) {
          if (!Info.AggregateUpdated) {
            Info.AggregateUpdated = true;
            Changed = true;
          }
        } else if (BO->getOpcode() == BO_Assign) {
          const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
          if (isZeroLiteralExpr(RHS, C)) {
            if (Info.KnownNonZero || Info.GuardedNonZero) {
              Info.KnownNonZero = false;
              Info.GuardedNonZero = false;
              Changed = true;
            }
          } else if (isKnownNonZeroExpr(RHS, C)) {
            if (!Info.KnownNonZero) {
              Info.KnownNonZero = true;
              Changed = true;
            }
          } else {
            if (Info.KnownNonZero || Info.GuardedNonZero) {
              Info.KnownNonZero = false;
              Info.GuardedNonZero = false;
              Changed = true;
            }
          }
        }

        if (Changed) {
          State = State->set<AggregateDenomMap>(LHSMR, Info);
          C.addTransition(State);
          return;
        }
      }
    }
  }

  if (BO->getOpcode() != BO_Div)
    return;

  QualType QT = BO->getType();
  bool IsFPDiv = (!QT.isNull() && QT->isRealFloatingType()) ||
                 (!BO->getLHS()->getType().isNull() &&
                  BO->getLHS()->getType()->isRealFloatingType()) ||
                 (!BO->getRHS()->getType().isNull() &&
                  BO->getRHS()->getType()->isRealFloatingType());
  if (!IsFPDiv)
    return;

  const Expr *Denom = BO->getRHS()->IgnoreParenImpCasts();
  if (!Denom)
    return;

  const MemRegion *MR = getTrackedFloatingVar(Denom, C);
  if (!MR)
    return;

  const AggregateDenomInfo *InfoPtr = State->get<AggregateDenomMap>(MR);
  if (!InfoPtr)
    return;

  const AggregateDenomInfo &Info = *InfoPtr;

  if (!Info.ZeroInit)
    return;
  if (!Info.AggregateUpdated)
    return;
  if (Info.GuardedNonZero || Info.KnownNonZero)
    return;

  if (isFalsePositive(BO, C))
    return;

  reportBug(BO, C, MR);
}

void SAGenTestChecker::reportBug(const BinaryOperator *BO, CheckerContext &C,
                                 const MemRegion *MR) const {
  ExplodedNode *N = C.generateNonFatalErrorNode();
  if (!N)
    return;

  auto R = std::make_unique<PathSensitiveBugReport>(
      *BT, "division by possibly zero aggregate factor", N);
  R->addRange(BO->getSourceRange());
  C.emitReport(std::move(R));
}

} // end anonymous namespace

extern "C" void clang_registerCheckers(CheckerRegistry &registry) {
  registry.addChecker<SAGenTestChecker>(
      "custom.SAGenTestChecker",
      "Detects floating-point division by an aggregate factor that may remain zero",
      "");
}

extern "C" const char clang_analyzerAPIVersionString[] =
    CLANG_ANALYZER_API_VERSION_STRING;
```