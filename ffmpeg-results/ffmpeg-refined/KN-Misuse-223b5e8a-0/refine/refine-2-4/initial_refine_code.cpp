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

enum AggregateStateKind : unsigned {
  ASK_None = 0,
  // Variable was initialized to zero and is being tracked as a candidate
  // aggregate denominator.
  ASK_TrackedZero = 1,
  // Variable participates in accumulation, e.g. x += y or x = x + y.
  ASK_Accumulating = 2,
  // We have seen at least one definitely positive contribution added into it,
  // so if the accumulation executed, the sum is > 0.
  ASK_HasPositiveContribution = 3
};

} // end anonymous namespace

REGISTER_MAP_WITH_PROGRAMSTATE(AggregateDenomMap, const MemRegion *, unsigned)
REGISTER_SET_WITH_PROGRAMSTATE(ZeroGuardedNonZeroSet, const MemRegion *)

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

static bool isNonZeroConstantExpr(const Expr *E, CheckerContext &C) {
  if (!E)
    return false;

  E = E->IgnoreParenImpCasts();
  if (!E)
    return false;

  if (const auto *FL = dyn_cast<FloatingLiteral>(E))
    return !FL->getValue().isZero();

  if (const auto *IL = dyn_cast<IntegerLiteral>(E))
    return !IL->getValue().isZero();

  llvm::APSInt IntRes;
  if (EvaluateExprToInt(IntRes, E, C))
    return IntRes != 0;

  Expr::EvalResult ER;
  if (E->EvaluateAsRValue(ER, C.getASTContext()) && ER.Val.isFloat())
    return !ER.Val.getFloat().isZero();

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

static const MemRegion *getTrackedRegionFromDeclRefExpr(const Expr *E,
                                                        CheckerContext &C) {
  if (!E)
    return nullptr;

  E = E->IgnoreParenImpCasts();
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

static bool exprReferencesRegion(const Expr *E, const MemRegion *Target,
                                 CheckerContext &C) {
  if (!E || !Target)
    return false;

  E = E->IgnoreParenImpCasts();

  if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
      const MemRegion *MR = getVarRegionFromDecl(VD, C);
      return MR && MR == Target;
    }
  }

  for (const Stmt *Child : E->children()) {
    if (const auto *CE = dyn_cast_or_null<Expr>(Child)) {
      if (exprReferencesRegion(CE, Target, C))
        return true;
    }
  }
  return false;
}

/// Returns true when the expression is definitely > 0 based on syntax / constant
/// folding / known math forms, but stays conservative for unknown runtime data.
static bool isDefinitelyPositiveExpr(const Expr *E, CheckerContext &C) {
  if (!E)
    return false;

  E = E->IgnoreParenImpCasts();
  if (!E)
    return false;

  Expr::EvalResult ER;
  if (E->EvaluateAsRValue(ER, C.getASTContext())) {
    if (ER.Val.isFloat())
      return ER.Val.getFloat().isPos() && !ER.Val.getFloat().isZero();
    if (ER.Val.isInt())
      return ER.Val.getInt().isStrictlyPositive();
  }

  if (const auto *FL = dyn_cast<FloatingLiteral>(E))
    return FL->getValue().isPos() && !FL->getValue().isZero();

  if (const auto *IL = dyn_cast<IntegerLiteral>(E))
    return IL->getValue().isStrictlyPositive();

  if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
    const Expr *L = BO->getLHS()->IgnoreParenImpCasts();
    const Expr *R = BO->getRHS()->IgnoreParenImpCasts();

    switch (BO->getOpcode()) {
    case BO_Add:
      return isDefinitelyPositiveExpr(L, C) && isDefinitelyPositiveExpr(R, C);
    case BO_Mul:
      return isDefinitelyPositiveExpr(L, C) && isDefinitelyPositiveExpr(R, C);
    case BO_Div:
      return isDefinitelyPositiveExpr(L, C) && isDefinitelyPositiveExpr(R, C);
    default:
      break;
    }
  }

  if (const auto *CE = dyn_cast<CallExpr>(E)) {
    if (const FunctionDecl *FD = CE->getDirectCallee()) {
      StringRef Name = FD->getIdentifier() ? FD->getName() : "";
      // exp(x) and exp2(x) are strictly positive for finite arguments.
      // We use this as a heuristic to suppress the Gaussian-weight false positive.
      if (Name == "exp" || Name == "expf" || Name == "expl" ||
          Name == "exp2" || Name == "exp2f" || Name == "exp2l")
        return true;
      // sqrt(c) is positive if c is definitely positive.
      if ((Name == "sqrt" || Name == "sqrtf" || Name == "sqrtl") &&
          CE->getNumArgs() == 1)
        return isDefinitelyPositiveExpr(CE->getArg(0), C);
    }
  }

  return false;
}

static const MemRegion *getTrackedRegionFromZeroGuardCond(const Expr *Cond,
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
        if (const MemRegion *MR = getTrackedRegionFromDeclRefExpr(RHS, C))
          return MR;
      }

      if (isZeroLiteralExpr(RHS, C)) {
        if (const MemRegion *MR = getTrackedRegionFromDeclRefExpr(LHS, C))
          return MR;
      }
    }
  }

  return nullptr;
}

class SAGenTestChecker
    : public Checker<check::PostStmt<DeclStmt>,
                     check::PostStmt<BinaryOperator>,
                     check::Bind,
                     check::BranchCondition,
                     check::PreStmt<BinaryOperator>> {
  mutable std::unique_ptr<BugType> BT;

public:
  SAGenTestChecker()
      : BT(new BugType(this, "Possible division by zero",
                       "Possible division by zero")) {}

  void checkPostStmt(const DeclStmt *DS, CheckerContext &C) const;
  void checkPostStmt(const BinaryOperator *BO, CheckerContext &C) const;
  void checkBind(SVal Loc, SVal Val, const Stmt *S, CheckerContext &C) const;
  void checkBranchCondition(const Stmt *Condition, CheckerContext &C) const;
  void checkPreStmt(const BinaryOperator *BO, CheckerContext &C) const;

private:
  bool isFalsePositive(const BinaryOperator *BO, CheckerContext &C,
                       const MemRegion *MR) const;
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

    State = State->set<AggregateDenomMap>(MR, (unsigned)ASK_TrackedZero);
    Changed = true;
  }

  if (Changed)
    C.addTransition(State);
}

void SAGenTestChecker::checkPostStmt(const BinaryOperator *BO,
                                     CheckerContext &C) const {
  if (!BO)
    return;

  ProgramStateRef State = C.getState();
  bool Changed = false;

  auto updateTrackedRegion = [&](const MemRegion *LHSReg, const Expr *RHS,
                                 bool IsAccumulation) {
    if (!LHSReg || !RHS)
      return;

    const unsigned *Tracked = State->get<AggregateDenomMap>(LHSReg);
    if (!Tracked)
      return;

    RHS = RHS->IgnoreParenImpCasts();

    if (isZeroLiteralExpr(RHS, C)) {
      State = State->set<AggregateDenomMap>(LHSReg, (unsigned)ASK_TrackedZero);
      Changed = true;
      return;
    }

    if (IsAccumulation) {
      if (isDefinitelyPositiveExpr(RHS, C))
        State = State->set<AggregateDenomMap>(LHSReg,
                                              (unsigned)ASK_HasPositiveContribution);
      else
        State = State->set<AggregateDenomMap>(LHSReg, (unsigned)ASK_Accumulating);
      Changed = true;
      return;
    }

    if (isDefinitelyPositiveExpr(RHS, C)) {
      State = State->set<AggregateDenomMap>(LHSReg,
                                            (unsigned)ASK_HasPositiveContribution);
      Changed = true;
      return;
    }

    if (isNonZeroConstantExpr(RHS, C)) {
      State = State->remove<AggregateDenomMap>(LHSReg);
      State = State->remove<ZeroGuardedNonZeroSet>(LHSReg);
      Changed = true;
      return;
    }

    // Unknown overwrite: keep tracking conservatively as an aggregate candidate.
    State = State->set<AggregateDenomMap>(LHSReg, (unsigned)ASK_TrackedZero);
    Changed = true;
  };

  switch (BO->getOpcode()) {
  case BO_AddAssign: {
    const MemRegion *LHSReg = getRegionFromTrackedExpr(BO->getLHS(), C);
    updateTrackedRegion(LHSReg, BO->getRHS(), true);
    break;
  }
  case BO_Assign: {
    const MemRegion *LHSReg = getRegionFromTrackedExpr(BO->getLHS(), C);
    if (!LHSReg)
      break;

    const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
    bool IsAccumulation = exprReferencesRegion(RHS, LHSReg, C);
    updateTrackedRegion(LHSReg, RHS, IsAccumulation);
    break;
  }
  default:
    break;
  }

  if (Changed)
    C.addTransition(State);
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

  const unsigned *Tracked = State->get<AggregateDenomMap>(LHSReg);
  if (!Tracked)
    return;

  const Expr *BindExpr = dyn_cast_or_null<Expr>(S);
  if (!BindExpr)
    return;

  BindExpr = BindExpr->IgnoreParenImpCasts();
  if (!BindExpr)
    return;

  if (isZeroLiteralExpr(BindExpr, C)) {
    State = State->set<AggregateDenomMap>(LHSReg, (unsigned)ASK_TrackedZero);
    C.addTransition(State);
    return;
  }

  if (isDefinitelyPositiveExpr(BindExpr, C)) {
    State = State->set<AggregateDenomMap>(LHSReg,
                                          (unsigned)ASK_HasPositiveContribution);
    C.addTransition(State);
    return;
  }

  if (isNonZeroConstantExpr(BindExpr, C)) {
    State = State->remove<AggregateDenomMap>(LHSReg);
    State = State->remove<ZeroGuardedNonZeroSet>(LHSReg);
    C.addTransition(State);
    return;
  }

  C.addTransition(State);
}

void SAGenTestChecker::checkBranchCondition(const Stmt *Condition,
                                            CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  const Expr *CondE = dyn_cast_or_null<Expr>(Condition);
  if (!CondE)
    return;

  const MemRegion *MR = getTrackedRegionFromZeroGuardCond(CondE, C);
  if (!MR)
    return;

  const unsigned *Tracked = State->get<AggregateDenomMap>(MR);
  if (!Tracked)
    return;

  // Record that a zero-related guard exists syntactically. The actual path
  // feasibility/nonzero proof is still checked via the constraint manager at
  // report time.
  State = State->add<ZeroGuardedNonZeroSet>(MR);
  C.addTransition(State);
}

bool SAGenTestChecker::isFalsePositive(const BinaryOperator *BO,
                                       CheckerContext &C,
                                       const MemRegion *MR) const {
  if (!BO || !MR)
    return true;

  ProgramStateRef State = C.getState();

  const unsigned *Tracked = State->get<AggregateDenomMap>(MR);
  if (!Tracked)
    return true;

  // If we have evidence of a definitely positive contribution to the aggregate,
  // suppress. This specifically addresses cases such as Gaussian-weight sums:
  // total += positive_constant * exp(...)
  if (*Tracked == (unsigned)ASK_HasPositiveContribution)
    return true;

  // Only report on actual aggregate-like variables. Plain zero-initialized
  // float locals that were never accumulated into are out of scope.
  if (*Tracked == (unsigned)ASK_TrackedZero)
    return true;

  const Expr *Denom = BO->getRHS();
  if (!Denom)
    return true;

  SVal DenomVal = State->getSVal(Denom, C.getLocationContext());
  Optional<DefinedSVal> DSV = DenomVal.getAs<DefinedSVal>();
  if (!DSV)
    return false;

  ProgramStateRef NonZeroState, ZeroState;
  std::tie(NonZeroState, ZeroState) = State->assume(*DSV);

  // Current path proves nonzero.
  if (NonZeroState && !ZeroState)
    return true;

  // If a syntactic guard was seen and current path is consistent only with
  // nonzero, also suppress. This is mostly redundant with the solver check but
  // harmless and keeps the intent explicit.
  if (State->contains<ZeroGuardedNonZeroSet>(MR) && NonZeroState && !ZeroState)
    return true;

  return false;
}

void SAGenTestChecker::checkPreStmt(const BinaryOperator *BO,
                                    CheckerContext &C) const {
  if (!BO)
    return;
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

  const Expr *Denom = BO->getRHS();
  if (!Denom)
    return;
  Denom = Denom->IgnoreParenImpCasts();

  const auto *DRE = dyn_cast<DeclRefExpr>(Denom);
  if (!DRE)
    return;

  const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
  if (!VD)
    return;
  if (!isRealFloatingType(VD->getType()))
    return;

  const MemRegion *MR = getVarRegionFromDecl(VD, C);
  if (!MR)
    return;

  const unsigned *Tracked = C.getState()->get<AggregateDenomMap>(MR);
  if (!Tracked)
    return;

  if (isFalsePositive(BO, C, MR))
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
