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

using namespace clang;
using namespace ento;
using namespace taint;

namespace {

enum AggregateKind : unsigned {
  AK_None = 0,
  AK_ZeroInit = 1,     // float x = 0;
  AK_Aggregated = 2,   // x is built by accumulation from prior value.
  AK_Guarded = 3       // x has a recognized non-zero guard on current path.
};

} // end anonymous namespace

REGISTER_MAP_WITH_PROGRAMSTATE(AggregateDenomMap, const MemRegion *, unsigned)

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

static bool exprRefersToRegion(const Expr *E, const MemRegion *Target,
                               CheckerContext &C) {
  if (!E || !Target)
    return false;

  E = E->IgnoreParenImpCasts();
  if (!E)
    return false;

  if (const MemRegion *MR = getRegionFromTrackedExpr(E, C))
    return MR == Target;

  if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
    return exprRefersToRegion(BO->getLHS(), Target, C) ||
           exprRefersToRegion(BO->getRHS(), Target, C);
  }

  if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
    return exprRefersToRegion(UO->getSubExpr(), Target, C);
  }

  if (const auto *CO = dyn_cast<ConditionalOperator>(E)) {
    return exprRefersToRegion(CO->getCond(), Target, C) ||
           exprRefersToRegion(CO->getTrueExpr(), Target, C) ||
           exprRefersToRegion(CO->getFalseExpr(), Target, C);
  }

  return false;
}

static const MemRegion *getTrackedVarFromExpr(const Expr *E, CheckerContext &C) {
  E = E ? E->IgnoreParenImpCasts() : nullptr;
  if (!E)
    return nullptr;

  if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
      if (isRealFloatingType(VD->getType()))
        return getVarRegionFromDecl(VD, C);
    }
  }

  return nullptr;
}

static bool isAggregateUpdateExpr(const Expr *RHS, const MemRegion *LHSReg,
                                  CheckerContext &C) {
  RHS = RHS ? RHS->IgnoreParenImpCasts() : nullptr;
  if (!RHS || !LHSReg)
    return false;

  const auto *BO = dyn_cast<BinaryOperator>(RHS);
  if (!BO)
    return false;

  BinaryOperatorKind Op = BO->getOpcode();
  if (Op != BO_Add && Op != BO_Sub)
    return false;

  return exprRefersToRegion(BO->getLHS(), LHSReg, C) ||
         exprRefersToRegion(BO->getRHS(), LHSReg, C);
}

static bool isRecognizedNonZeroGuardForRegion(const Expr *Cond,
                                              const MemRegion *MR,
                                              bool Assumption,
                                              CheckerContext &C) {
  if (!Cond || !MR)
    return false;

  Cond = Cond->IgnoreParenImpCasts();
  if (!Cond)
    return false;

  // if (x)  --> true branch means x != 0
  if (const MemRegion *CondMR = getTrackedVarFromExpr(Cond, C)) {
    return CondMR == MR && Assumption;
  }

  // if (!x) --> false branch means x != 0
  if (const auto *UO = dyn_cast<UnaryOperator>(Cond)) {
    if (UO->getOpcode() == UO_LNot) {
      if (const MemRegion *SubMR = getTrackedVarFromExpr(UO->getSubExpr(), C))
        return SubMR == MR && !Assumption;
    }
  }

  // if (x != 0), if (x == 0), and flipped forms.
  if (const auto *BO = dyn_cast<BinaryOperator>(Cond)) {
    if (BO->getOpcode() == BO_EQ || BO->getOpcode() == BO_NE) {
      const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
      const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();

      const MemRegion *VarMR = nullptr;
      bool HasZero = false;

      if (isZeroLiteralExpr(LHS, C)) {
        HasZero = true;
        VarMR = getTrackedVarFromExpr(RHS, C);
      } else if (isZeroLiteralExpr(RHS, C)) {
        HasZero = true;
        VarMR = getTrackedVarFromExpr(LHS, C);
      }

      if (!HasZero || VarMR != MR)
        return false;

      if (BO->getOpcode() == BO_NE)
        return Assumption;   // true branch of (x != 0)
      return !Assumption;    // false branch of (x == 0)
    }
  }

  return false;
}

class SAGenTestChecker
    : public Checker<check::PostStmt<DeclStmt>,
                     check::BranchCondition,
                     check::PreStmt<BinaryOperator>,
                     eval::Assume> {
  mutable std::unique_ptr<BugType> BT;

public:
  SAGenTestChecker()
      : BT(new BugType(this, "Possible division by zero",
                       "Possible division by zero")) {}

  void checkPostStmt(const DeclStmt *DS, CheckerContext &C) const;
  void checkBranchCondition(const Stmt *Condition, CheckerContext &C) const;
  void checkPreStmt(const BinaryOperator *BO, CheckerContext &C) const;
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

    State = State->set<AggregateDenomMap>(MR, (unsigned)AK_ZeroInit);
    Changed = true;
  }

  if (Changed)
    C.addTransition(State);
}

void SAGenTestChecker::checkBranchCondition(const Stmt *Condition,
                                            CheckerContext &C) const {
  // Intentionally left lightweight.
  // Actual path-sensitive guard refinement is done in evalAssume().
  (void)Condition;
  (void)C;
}

ProgramStateRef SAGenTestChecker::evalAssume(ProgramStateRef State, SVal Cond,
                                             bool Assumption) const {
  const nonloc::SymbolVal *StateExpr = Cond.getAs<nonloc::SymbolVal>();
  if (!StateExpr)
    return State;

  SymbolRef Sym = StateExpr->getSymbol();
  if (!Sym)
    return State;

  // Recover the origin expression if possible.
  const auto *RegionSym = dyn_cast<SymbolRegionValue>(Sym);
  if (!RegionSym)
    return State;

  const MemRegion *MR = RegionSym->getRegion();
  if (!MR)
    return State;

  MR = MR->getBaseRegion();
  const unsigned *Kind = State->get<AggregateDenomMap>(MR);
  if (!Kind)
    return State;

  // We cannot inspect the original branch expression directly from evalAssume().
  // However, analyzer assumptions on region symbols correspond to truthiness checks
  // such as `if (x)` / `if (!x)`. Mark the true branch as guarded for tracked vars.
  if (*Kind == (unsigned)AK_Aggregated && Assumption)
    return State->set<AggregateDenomMap>(MR, (unsigned)AK_Guarded);

  return State;
}

void SAGenTestChecker::checkPreStmt(const BinaryOperator *BO,
                                    CheckerContext &C) const {
  if (!BO)
    return;

  ProgramStateRef State = C.getState();

  // Track accumulation patterns:
  //   x += y;
  if (BO->getOpcode() == BO_AddAssign || BO->getOpcode() == BO_SubAssign) {
    const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
    const auto *DRE = dyn_cast<DeclRefExpr>(LHS);
    if (!DRE)
      return;

    const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
    if (!VD || !isRealFloatingType(VD->getType()))
      return;

    const MemRegion *MR = getVarRegionFromDecl(VD, C);
    if (!MR)
      return;

    const unsigned *Kind = State->get<AggregateDenomMap>(MR);
    if (!Kind)
      return;

    // A zero-initialized tracked variable now becomes an aggregated denominator.
    if (*Kind == (unsigned)AK_ZeroInit || *Kind == (unsigned)AK_Aggregated) {
      State = State->set<AggregateDenomMap>(MR, (unsigned)AK_Aggregated);
      C.addTransition(State);
    }
    return;
  }

  // Track assignment forms and division uses.
  if (BO->getOpcode() == BO_Assign) {
    const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
    const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();

    const auto *DRE = dyn_cast<DeclRefExpr>(LHS);
    if (!DRE)
      return;

    const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
    if (!VD || !isRealFloatingType(VD->getType()))
      return;

    const MemRegion *MR = getVarRegionFromDecl(VD, C);
    if (!MR)
      return;

    const unsigned *Kind = State->get<AggregateDenomMap>(MR);
    if (!Kind)
      return;

    // x = 0; keep as zero-init candidate.
    if (isZeroLiteralExpr(RHS, C)) {
      State = State->set<AggregateDenomMap>(MR, (unsigned)AK_ZeroInit);
      C.addTransition(State);
      return;
    }

    // x = x + y; or x = y + x; or x = x - y;  ==> aggregated
    if (isAggregateUpdateExpr(RHS, MR, C)) {
      State = State->set<AggregateDenomMap>(MR, (unsigned)AK_Aggregated);
      C.addTransition(State);
      return;
    }

    // Any other overwrite is not the target aggregate-denominator pattern.
    // This is the key fix for the false positive on `b0 = ...;`.
    State = State->remove<AggregateDenomMap>(MR);
    C.addTransition(State);
    return;
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

  const unsigned *Kind = State->get<AggregateDenomMap>(MR);
  if (!Kind)
    return;

  // Report only for actual accumulated aggregate denominators that are not
  // already known guarded on this path.
  if (*Kind == (unsigned)AK_Aggregated)
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
  if (MR)
    R->markInteresting(MR);
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