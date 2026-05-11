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

using namespace clang;
using namespace ento;
using namespace taint;

namespace {

enum AggregateKind : unsigned {
  AK_None = 0,
  AK_ZeroInitCandidate = 1, // float local initialized to 0
  AK_AggregateAccum = 2     // observed additive self-accumulation
};

} // end anonymous namespace

REGISTER_MAP_WITH_PROGRAMSTATE(AggregateDenomMap, const MemRegion *, unsigned)
REGISTER_MAP_WITH_PROGRAMSTATE(GuardedNonZeroMap, const MemRegion *, bool)

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

static bool exprReferencesRegion(const Expr *E, const MemRegion *TargetMR,
                                 CheckerContext &C) {
  if (!E || !TargetMR)
    return false;

  E = E->IgnoreParenImpCasts();
  if (!E)
    return false;

  if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
      const MemRegion *MR = getVarRegionFromDecl(VD, C);
      return MR && MR == TargetMR;
    }
  }

  for (const Stmt *Child : E->children()) {
    if (const auto *CE = dyn_cast_or_null<Expr>(Child)) {
      if (exprReferencesRegion(CE, TargetMR, C))
        return true;
    }
  }

  return false;
}

static bool isAdditiveSelfAccumulation(const BinaryOperator *BO,
                                       const MemRegion *LHSReg,
                                       CheckerContext &C) {
  if (!BO || !LHSReg)
    return false;

  if (BO->getOpcode() == BO_AddAssign)
    return true;

  if (BO->getOpcode() != BO_Assign)
    return false;

  const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
  const auto *RBO = dyn_cast<BinaryOperator>(RHS);
  if (!RBO || RBO->getOpcode() != BO_Add)
    return false;

  return exprReferencesRegion(RBO->getLHS(), LHSReg, C) ||
         exprReferencesRegion(RBO->getRHS(), LHSReg, C);
}

static const MemRegion *getGuardedRegionFromCondExpr(const Expr *Cond,
                                                     CheckerContext &C) {
  if (!Cond)
    return nullptr;

  Cond = Cond->IgnoreParenImpCasts();
  if (!Cond)
    return nullptr;

  if (const auto *DRE = dyn_cast<DeclRefExpr>(Cond)) {
    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
      if (isRealFloatingType(VD->getType()))
        return getVarRegionFromDecl(VD, C);
    }
  }

  if (const auto *BO = dyn_cast<BinaryOperator>(Cond)) {
    if (BO->getOpcode() == BO_EQ || BO->getOpcode() == BO_NE) {
      const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
      const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();

      if (isZeroLiteralExpr(LHS, C)) {
        if (const auto *DRE = dyn_cast<DeclRefExpr>(RHS)) {
          if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
            if (isRealFloatingType(VD->getType()))
              return getVarRegionFromDecl(VD, C);
          }
        }
      }

      if (isZeroLiteralExpr(RHS, C)) {
        if (const auto *DRE = dyn_cast<DeclRefExpr>(LHS)) {
          if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
            if (isRealFloatingType(VD->getType()))
              return getVarRegionFromDecl(VD, C);
          }
        }
      }
    }
  }

  return nullptr;
}

static bool branchTrueMeansNonZero(const Expr *Cond, CheckerContext &C) {
  if (!Cond)
    return false;

  Cond = Cond->IgnoreParenImpCasts();
  if (!Cond)
    return false;

  if (isa<DeclRefExpr>(Cond))
    return true;

  if (const auto *BO = dyn_cast<BinaryOperator>(Cond)) {
    if (BO->getOpcode() == BO_NE) {
      return isZeroLiteralExpr(BO->getLHS(), C) || isZeroLiteralExpr(BO->getRHS(), C);
    }
    if (BO->getOpcode() == BO_EQ) {
      return false;
    }
  }

  return false;
}

static bool branchFalseMeansNonZero(const Expr *Cond, CheckerContext &C) {
  if (!Cond)
    return false;

  Cond = Cond->IgnoreParenImpCasts();
  if (!Cond)
    return false;

  if (isa<DeclRefExpr>(Cond))
    return false;

  if (const auto *BO = dyn_cast<BinaryOperator>(Cond)) {
    if (BO->getOpcode() == BO_EQ) {
      return isZeroLiteralExpr(BO->getLHS(), C) || isZeroLiteralExpr(BO->getRHS(), C);
    }
    if (BO->getOpcode() == BO_NE) {
      return false;
    }
  }

  return false;
}

static bool isGuardedByConditionalOperator(const BinaryOperator *DivBO,
                                           const MemRegion *MR,
                                           CheckerContext &C) {
  if (!DivBO || !MR)
    return false;

  const auto *ACO = findSpecificTypeInParents<AbstractConditionalOperator>(DivBO, C);
  if (!ACO)
    return false;

  const Expr *Cond = ACO->getCond();
  const MemRegion *GuardMR = getGuardedRegionFromCondExpr(Cond, C);
  if (!GuardMR || GuardMR != MR)
    return false;

  const Expr *TrueE = ACO->getTrueExpr();
  const Expr *FalseE = ACO->getFalseExpr();

  if (!TrueE || !FalseE)
    return false;

  bool DivInTrue = exprReferencesRegion(TrueE, MR, C) && true;
  bool DivInFalse = exprReferencesRegion(FalseE, MR, C) && true;

  // The common safe pattern is: x ? 1/x : 0  or  x != 0 ? 1/x : ...
  // If the condition's true branch implies non-zero and division is in true expr,
  // suppress. Also handle x == 0 ? ... : 1/x via false branch implication.
  if (branchTrueMeansNonZero(Cond, C) && DivInTrue)
    return true;
  if (branchFalseMeansNonZero(Cond, C) && DivInFalse)
    return true;

  return false;
}

static bool isFalsePositive(const BinaryOperator *BO, const MemRegion *MR,
                            CheckerContext &C) {
  if (!BO || !MR)
    return true;

  if (isGuardedByConditionalOperator(BO, MR, C))
    return true;

  return false;
}

class SAGenTestChecker
    : public Checker<check::PostStmt<DeclStmt>,
                     check::PreStmt<BinaryOperator>,
                     check::Bind,
                     check::BranchCondition,
                     eval::Assume> {
  mutable std::unique_ptr<BugType> BT;

public:
  SAGenTestChecker()
      : BT(new BugType(this, "Possible division by zero",
                       "Possible division by zero")) {}

  void checkPostStmt(const DeclStmt *DS, CheckerContext &C) const;
  void checkPreStmt(const BinaryOperator *BO, CheckerContext &C) const;
  void checkBind(SVal Loc, SVal Val, const Stmt *S, CheckerContext &C) const;
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

    State = State->set<AggregateDenomMap>(MR, (unsigned)AK_ZeroInitCandidate);
    State = State->remove<GuardedNonZeroMap>(MR);
    Changed = true;
  }

  if (Changed)
    C.addTransition(State);
}

void SAGenTestChecker::checkPreStmt(const BinaryOperator *BO,
                                    CheckerContext &C) const {
  if (!BO)
    return;

  ProgramStateRef State = C.getState();

  // 1) Learn additive self-accumulation patterns that match the target bug shape.
  if (BO->isAssignmentOp()) {
    const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
    const MemRegion *LHSReg = getRegionFromTrackedExpr(LHS, C);
    if (LHSReg) {
      const unsigned *Kind = State->get<AggregateDenomMap>(LHSReg);
      if (Kind && *Kind >= (unsigned)AK_ZeroInitCandidate) {
        if (isAdditiveSelfAccumulation(BO, LHSReg, C)) {
          State = State->set<AggregateDenomMap>(LHSReg, (unsigned)AK_AggregateAccum);
          State = State->remove<GuardedNonZeroMap>(LHSReg);
          C.addTransition(State);
          return;
        }
      }
    }
  }

  // 2) Check divisions.
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
  const auto *DRE = dyn_cast<DeclRefExpr>(Denom);
  if (!DRE)
    return;

  const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
  if (!VD || !isRealFloatingType(VD->getType()))
    return;

  const MemRegion *MR = getVarRegionFromDecl(VD, C);
  if (!MR)
    return;

  const unsigned *Kind = State->get<AggregateDenomMap>(MR);
  if (!Kind || *Kind != (unsigned)AK_AggregateAccum)
    return;

  const bool *Guarded = State->get<GuardedNonZeroMap>(MR);
  if (Guarded && *Guarded)
    return;

  if (isFalsePositive(BO, MR, C))
    return;

  reportBug(BO, C, MR);
}

void SAGenTestChecker::checkBind(SVal Loc, SVal Val, const Stmt *S,
                                 CheckerContext &C) const {
  ProgramStateRef State = C.getState();

  const MemRegion *LHSReg = Loc.getAsRegion();
  if (!LHSReg)
    return;
  LHSReg = LHSReg->getBaseRegion();
  if (!LHSReg)
    return;

  const unsigned *Kind = State->get<AggregateDenomMap>(LHSReg);
  if (!Kind)
    return;

  const Expr *BindExpr = dyn_cast_or_null<Expr>(S);
  if (!BindExpr)
    return;

  BindExpr = BindExpr->IgnoreParenImpCasts();
  if (!BindExpr)
    return;

  // Preserve tracking across zero assignments.
  if (isZeroLiteralExpr(BindExpr, C)) {
    State = State->remove<GuardedNonZeroMap>(LHSReg);
    C.addTransition(State);
    return;
  }

  // If this bind is a definite non-zero floating constant, the denominator is safe now.
  llvm::APFloat F(0.0);
  if (BindExpr->EvaluateAsFloat(F, C.getASTContext())) {
    if (!F.isZero()) {
      State = State->remove<AggregateDenomMap>(LHSReg);
      State = State->remove<GuardedNonZeroMap>(LHSReg);
      C.addTransition(State);
      return;
    }
  }

  // If assigned from an unrelated expression and not a self-accumulation shape,
  // stop tracking. This is key to avoiding broad false positives.
  bool KeepTracking = false;
  if (const auto *BO = dyn_cast<BinaryOperator>(BindExpr)) {
    if (BO->isAssignmentOp() && isAdditiveSelfAccumulation(BO, LHSReg, C))
      KeepTracking = true;
  }

  if (!KeepTracking && !exprReferencesRegion(BindExpr, LHSReg, C)) {
    State = State->remove<AggregateDenomMap>(LHSReg);
    State = State->remove<GuardedNonZeroMap>(LHSReg);
    C.addTransition(State);
    return;
  }

  // Value changed; a previous path-specific non-zero guard should not be assumed
  // to still hold after mutation.
  State = State->remove<GuardedNonZeroMap>(LHSReg);
  C.addTransition(State);
}

void SAGenTestChecker::checkBranchCondition(const Stmt *Condition,
                                            CheckerContext &C) const {
  // Intentionally do not mutate tracked aggregate state here.
  // Path-sensitive guarded-nonzero facts are handled in evalAssume().
  (void)Condition;
  (void)C;
}

ProgramStateRef SAGenTestChecker::evalAssume(ProgramStateRef State, SVal Cond,
                                             bool Assumption) const {
  std::optional<DefinedSVal> DSV = Cond.getAs<DefinedSVal>();
  if (!DSV)
    return State;

  const auto *SymVal = dyn_cast<nonloc::SymbolVal>(&DSV->castAs<NonLoc>());
  if (!SymVal)
    return State;

  SymbolRef Sym = SymVal->getSymbol();
  if (!Sym)
    return State;

  const auto *RegionSym = dyn_cast<SymbolRegionValue>(Sym);
  if (!RegionSym)
    return State;

  const MemRegion *MR = RegionSym->getRegion();
  if (!MR)
    return State;
  MR = MR->getBaseRegion();

  const unsigned *Kind = State->get<AggregateDenomMap>(MR);
  if (!Kind || *Kind != (unsigned)AK_AggregateAccum)
    return State;

  if (Assumption)
    State = State->set<GuardedNonZeroMap>(MR, true);
  else
    State = State->remove<GuardedNonZeroMap>(MR);

  return State;
}

void SAGenTestChecker::reportBug(const BinaryOperator *BO, CheckerContext &C,
                                 const MemRegion *MR) const {
  (void)MR;
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
