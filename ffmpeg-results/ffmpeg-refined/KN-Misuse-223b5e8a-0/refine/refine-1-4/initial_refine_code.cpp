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

REGISTER_MAP_WITH_PROGRAMSTATE(AggregateDenomMap, const MemRegion *, bool)
REGISTER_MAP_WITH_PROGRAMSTATE(NonZeroGuardMap, const MemRegion *, bool)

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

static const VarDecl *getFloatVarDeclFromExpr(const Expr *E) {
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

  return VD;
}

static bool exprIsTrackedFloatVar(const Expr *E, CheckerContext &C,
                                  const MemRegion *&MR) {
  MR = nullptr;
  const VarDecl *VD = getFloatVarDeclFromExpr(E);
  if (!VD)
    return false;

  MR = getVarRegionFromDecl(VD, C);
  return MR != nullptr;
}

/// Returns true iff the current path state plus Cond imply that TrackedExpr
/// is non-zero on the path where Cond evaluates to true.
static bool conditionImpliesNonZeroOnTrue(const Expr *Cond,
                                          const Expr *TrackedExpr,
                                          CheckerContext &C) {
  if (!Cond || !TrackedExpr)
    return false;

  SVal CondV = C.getState()->getSVal(Cond, C.getLocationContext());
  Optional<DefinedSVal> DCond = CondV.getAs<DefinedSVal>();
  if (!DCond)
    return false;

  ProgramStateRef TrueState = C.getState()->assume(*DCond, true);
  if (!TrueState)
    return false;

  SVal TV = TrueState->getSVal(TrackedExpr, C.getLocationContext());
  Optional<DefinedSVal> DTV = TV.getAs<DefinedSVal>();
  if (!DTV)
    return false;

  ProgramStateRef ZeroState, NonZeroState;
  std::tie(ZeroState, NonZeroState) =
      TrueState->assumeInclusiveRangeDual(*DTV, llvm::APSInt::get(0), llvm::APSInt::get(0));

  // If zero is infeasible but some non-zero value is feasible under Cond==true,
  // then Cond proves the tracked expr is non-zero on the true branch.
  return ZeroState == nullptr && NonZeroState != nullptr;
}

static bool isFalsePositive(const BinaryOperator *BO, CheckerContext &C,
                            const MemRegion *MR) {
  if (!BO || !MR)
    return false;

  const bool *KnownNonZero = C.getState()->get<NonZeroGuardMap>(MR);
  if (KnownNonZero && *KnownNonZero)
    return true;

  return false;
}

class SAGenTestChecker
    : public Checker<check::PostStmt<DeclStmt>,
                     check::Bind,
                     check::BranchCondition,
                     check::PreStmt<BinaryOperator>> {
  mutable std::unique_ptr<BugType> BT;

public:
  SAGenTestChecker()
      : BT(new BugType(this, "Possible division by zero",
                       "Possible division by zero")) {}

  void checkPostStmt(const DeclStmt *DS, CheckerContext &C) const;
  void checkBind(SVal Loc, SVal Val, const Stmt *S, CheckerContext &C) const;
  void checkBranchCondition(const Stmt *Condition, CheckerContext &C) const;
  void checkPreStmt(const BinaryOperator *BO, CheckerContext &C) const;

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

    State = State->set<AggregateDenomMap>(MR, true);
    State = State->remove<NonZeroGuardMap>(MR);
    Changed = true;
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

  // Any write invalidates previously remembered "known non-zero because of guard"
  // information for that region.
  if (State->get<NonZeroGuardMap>(LHSReg))
    State = State->remove<NonZeroGuardMap>(LHSReg);

  const bool *Tracked = State->get<AggregateDenomMap>(LHSReg);
  if (!Tracked || !*Tracked) {
    C.addTransition(State);
    return;
  }

  const Expr *BindExpr = dyn_cast_or_null<Expr>(S);
  if (!BindExpr) {
    C.addTransition(State);
    return;
  }

  BindExpr = BindExpr->IgnoreParenImpCasts();
  if (!BindExpr) {
    C.addTransition(State);
    return;
  }

  if (isZeroLiteralExpr(BindExpr, C)) {
    C.addTransition(State);
    return;
  }

  llvm::APFloat F(0.0);
  if (BindExpr->EvaluateAsFloat(F, C.getASTContext())) {
    if (!F.isZero()) {
      State = State->remove<AggregateDenomMap>(LHSReg);
      C.addTransition(State);
      return;
    }
  }

  C.addTransition(State);
}

void SAGenTestChecker::checkBranchCondition(const Stmt *Condition,
                                            CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  const Expr *CondE = dyn_cast_or_null<Expr>(Condition);
  if (!CondE)
    return;

  CondE = CondE->IgnoreParenImpCasts();
  if (!CondE)
    return;

  bool Changed = false;

  // Simple direct case: if (x), if (x != 0), if (x > 0), if (x < 0), ...
  if (const VarDecl *VD = getFloatVarDeclFromExpr(CondE)) {
    const MemRegion *MR = getVarRegionFromDecl(VD, C);
    if (MR) {
      const bool *Tracked = State->get<AggregateDenomMap>(MR);
      if (Tracked && *Tracked &&
          conditionImpliesNonZeroOnTrue(CondE, CondE, C)) {
        State = State->set<NonZeroGuardMap>(MR, true);
        Changed = true;
      }
    }
  }

  if (const auto *BO = dyn_cast<BinaryOperator>(CondE)) {
    const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
    const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();

    const MemRegion *MR = nullptr;

    if (exprIsTrackedFloatVar(LHS, C, MR)) {
      const bool *Tracked = State->get<AggregateDenomMap>(MR);
      if (Tracked && *Tracked &&
          conditionImpliesNonZeroOnTrue(CondE, LHS, C)) {
        State = State->set<NonZeroGuardMap>(MR, true);
        Changed = true;
      }
    }

    MR = nullptr;
    if (exprIsTrackedFloatVar(RHS, C, MR)) {
      const bool *Tracked = State->get<AggregateDenomMap>(MR);
      if (Tracked && *Tracked &&
          conditionImpliesNonZeroOnTrue(CondE, RHS, C)) {
        State = State->set<NonZeroGuardMap>(MR, true);
        Changed = true;
      }
    }
  }

  if (Changed)
    C.addTransition(State);
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

  const bool *Tracked = C.getState()->get<AggregateDenomMap>(MR);
  if (!Tracked || !*Tracked)
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
