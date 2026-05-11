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

// Tracks whether a floating local started from zero and is still a candidate
// for the "aggregate may stay zero" pattern.
REGISTER_MAP_WITH_PROGRAMSTATE(AggregateDenomMap, const MemRegion *, bool)

// Tracks whether we have actually seen an aggregate-building update for the
// variable, such as:
//   x += y;
//   x = x + y;
//   x = y + x;
REGISTER_MAP_WITH_PROGRAMSTATE(AggregateBuiltMap, const MemRegion *, bool)

// Tracks whether the candidate has been guarded against zero in a branch-like
// condition, e.g.
//   if (x) ...
//   if (x != 0) ...
//   if (0 != x) ...
REGISTER_MAP_WITH_PROGRAMSTATE(AggregateGuardedMap, const MemRegion *, bool)

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

  if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
      return getVarRegionFromDecl(VD, C);
    }
  }

  const MemRegion *MR = getMemRegionFromExpr(E, C);
  if (!MR)
    return nullptr;

  return MR->getBaseRegion();
}

static bool exprRefersToRegion(const Expr *E, const MemRegion *MR, CheckerContext &C) {
  if (!E || !MR)
    return false;

  const MemRegion *ExprMR = getRegionFromTrackedExpr(E, C);
  if (!ExprMR)
    return false;

  return ExprMR == MR;
}

static const MemRegion *getTrackedRegionFromZeroGuardCond(const Expr *Cond,
                                                          CheckerContext &C) {
  if (!Cond)
    return nullptr;

  Cond = Cond->IgnoreParenImpCasts();
  if (!Cond)
    return nullptr;

  // if (x) or if (!x) is a zero/non-zero style guard for scalar values.
  if (const auto *DRE = dyn_cast<DeclRefExpr>(Cond)) {
    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
      if (isRealFloatingType(VD->getType()))
        return getVarRegionFromDecl(VD, C);
    }
  }

  if (const auto *UO = dyn_cast<UnaryOperator>(Cond)) {
    if (UO->getOpcode() == UO_LNot) {
      const Expr *Sub = UO->getSubExpr()->IgnoreParenImpCasts();
      if (const auto *DRE = dyn_cast<DeclRefExpr>(Sub)) {
        if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
          if (isRealFloatingType(VD->getType()))
            return getVarRegionFromDecl(VD, C);
        }
      }
    }
  }

  if (const auto *BO = dyn_cast<BinaryOperator>(Cond)) {
    if (BO->isComparisonOp()) {
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

static bool isAggregateUpdateForRegion(const BinaryOperator *BO,
                                       const MemRegion *LHSReg,
                                       CheckerContext &C) {
  if (!BO || !LHSReg)
    return false;

  // x += y
  if (BO->getOpcode() == BO_AddAssign) {
    return exprRefersToRegion(BO->getLHS(), LHSReg, C);
  }

  // x = x + y  or  x = y + x
  if (BO->getOpcode() != BO_Assign)
    return false;

  if (!exprRefersToRegion(BO->getLHS(), LHSReg, C))
    return false;

  const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
  const auto *RHSBO = dyn_cast<BinaryOperator>(RHS);
  if (!RHSBO)
    return false;

  if (RHSBO->getOpcode() != BO_Add)
    return false;

  return exprRefersToRegion(RHSBO->getLHS(), LHSReg, C) ||
         exprRefersToRegion(RHSBO->getRHS(), LHSReg, C);
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
  bool isTrackedAggregateCandidate(const MemRegion *MR,
                                   ProgramStateRef State) const;
  bool isFalsePositive(const BinaryOperator *BO,
                       const MemRegion *MR,
                       CheckerContext &C) const;
  void reportBug(const BinaryOperator *BO, CheckerContext &C,
                 const MemRegion *MR) const;
};

bool SAGenTestChecker::isTrackedAggregateCandidate(const MemRegion *MR,
                                                   ProgramStateRef State) const {
  if (!MR || !State)
    return false;

  const bool *Tracked = State->get<AggregateDenomMap>(MR);
  if (!Tracked || !*Tracked)
    return false;

  const bool *Built = State->get<AggregateBuiltMap>(MR);
  if (!Built || !*Built)
    return false;

  const bool *Guarded = State->get<AggregateGuardedMap>(MR);
  if (Guarded && *Guarded)
    return false;

  return true;
}

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
    State = State->remove<AggregateBuiltMap>(MR);
    State = State->remove<AggregateGuardedMap>(MR);
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

  const bool *Tracked = State->get<AggregateDenomMap>(LHSReg);
  if (!Tracked || !*Tracked)
    return;

  const Expr *BindExpr = dyn_cast_or_null<Expr>(S);
  if (!BindExpr)
    return;

  BindExpr = BindExpr->IgnoreParenImpCasts();
  if (!BindExpr)
    return;

  bool Changed = false;

  if (const auto *BO = dyn_cast<BinaryOperator>(BindExpr)) {
    if (isAggregateUpdateForRegion(BO, LHSReg, C)) {
      State = State->set<AggregateBuiltMap>(LHSReg, true);
      Changed = true;
    }
  }

  // Preserve candidate tracking for zero re-assignment.
  if (isZeroLiteralExpr(BindExpr, C)) {
    if (Changed)
      C.addTransition(State);
    else
      C.addTransition(State);
    return;
  }

  // If the variable is assigned a compile-time non-zero constant directly,
  // this is no longer the target "aggregate starts at zero and may stay zero"
  // pattern.
  llvm::APFloat F(0.0);
  if (BindExpr->EvaluateAsFloat(F, C.getASTContext())) {
    if (!F.isZero()) {
      State = State->remove<AggregateDenomMap>(LHSReg);
      State = State->remove<AggregateBuiltMap>(LHSReg);
      State = State->remove<AggregateGuardedMap>(LHSReg);
      C.addTransition(State);
      return;
    }
  }

  if (Changed)
    C.addTransition(State);
  else
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

  const bool *Tracked = State->get<AggregateDenomMap>(MR);
  if (!Tracked || !*Tracked)
    return;

  State = State->set<AggregateGuardedMap>(MR, true);
  C.addTransition(State);
}

bool SAGenTestChecker::isFalsePositive(const BinaryOperator *BO,
                                       const MemRegion *MR,
                                       CheckerContext &C) const {
  if (!BO || !MR)
    return true;

  // The checker is intentionally narrow: only warn when the denominator of the
  // floating-point division is the tracked aggregate variable itself.
  const Expr *Denom = BO->getRHS();
  if (!Denom)
    return true;

  Denom = Denom->IgnoreParenImpCasts();
  if (!exprRefersToRegion(Denom, MR, C))
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

  ProgramStateRef State = C.getState();
  if (!isTrackedAggregateCandidate(MR, State))
    return;

  if (isFalsePositive(BO, MR, C))
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
