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
enum AggregateDenomKind : unsigned {
  ADK_ZeroInitOnly = 1,
  ADK_Aggregated = 2,
  ADK_Guarded = 3
};
} // namespace

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

  if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
      if (isRealFloatingType(VD->getType()))
        return getVarRegionFromDecl(VD, C);
    }
  }

  const MemRegion *MR = getMemRegionFromExpr(E, C);
  if (!MR)
    return nullptr;

  return MR->getBaseRegion();
}

static bool refersToTrackedVar(const Expr *E, const MemRegion *TargetMR,
                               CheckerContext &C) {
  if (!E || !TargetMR)
    return false;

  E = E->IgnoreParenImpCasts();
  if (!E)
    return false;

  if (const MemRegion *MR = getRegionFromTrackedExpr(E, C))
    return MR == TargetMR;

  return false;
}

static bool isTrackedFloatingVarExpr(const Expr *E, CheckerContext &C,
                                     const MemRegion *&MR) {
  MR = nullptr;
  if (!E)
    return false;

  E = E->IgnoreParenImpCasts();
  if (!E)
    return false;

  const auto *DRE = dyn_cast<DeclRefExpr>(E);
  if (!DRE)
    return false;

  const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
  if (!VD)
    return false;

  if (!isRealFloatingType(VD->getType()))
    return false;

  MR = getVarRegionFromDecl(VD, C);
  return MR != nullptr;
}

static bool isAggregateUpdateExpr(const BinaryOperator *BO, CheckerContext &C,
                                  const MemRegion *&MR) {
  MR = nullptr;
  if (!BO)
    return false;

  // x += y
  if (BO->getOpcode() == BO_AddAssign) {
    const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
    if (!LHS)
      return false;

    if (!isTrackedFloatingVarExpr(LHS, C, MR))
      return false;

    return true;
  }

  // x = x + y  or  x = y + x
  if (BO->getOpcode() == BO_Assign) {
    const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
    const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
    if (!LHS || !RHS)
      return false;

    const MemRegion *LHSMR = nullptr;
    if (!isTrackedFloatingVarExpr(LHS, C, LHSMR))
      return false;

    const auto *RHSBO = dyn_cast<BinaryOperator>(RHS);
    if (!RHSBO || RHSBO->getOpcode() != BO_Add)
      return false;

    const Expr *AddL = RHSBO->getLHS()->IgnoreParenImpCasts();
    const Expr *AddR = RHSBO->getRHS()->IgnoreParenImpCasts();

    if (refersToTrackedVar(AddL, LHSMR, C) || refersToTrackedVar(AddR, LHSMR, C)) {
      MR = LHSMR;
      return true;
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

  // if (x)
  if (const auto *DRE = dyn_cast<DeclRefExpr>(Cond)) {
    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
      if (isRealFloatingType(VD->getType()))
        return getVarRegionFromDecl(VD, C);
    }
  }

  // if (!x)
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

  // if (x == 0), if (x != 0), if (0 == x), if (0 != x)
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

static bool isFalsePositive(const BinaryOperator *BO, CheckerContext &C) {
  if (!BO)
    return true;

  if (BO->getOpcode() != BO_Div)
    return true;

  const Expr *Denom = BO->getRHS();
  if (!Denom)
    return true;

  Denom = Denom->IgnoreParenImpCasts();
  const MemRegion *MR = getRegionFromTrackedExpr(Denom, C);
  if (!MR)
    return true;

  const unsigned *Kind = C.getState()->get<AggregateDenomMap>(MR);
  if (!Kind)
    return true;

  // Only report for actual observed aggregation patterns.
  if (*Kind != ADK_Aggregated)
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

    State = State->set<AggregateDenomMap>(MR, (unsigned)ADK_ZeroInitOnly);
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

  const unsigned *Tracked = State->get<AggregateDenomMap>(LHSReg);
  if (!Tracked)
    return;

  const Expr *BindExpr = dyn_cast_or_null<Expr>(S);
  if (!BindExpr)
    return;

  BindExpr = BindExpr->IgnoreParenImpCasts();
  if (!BindExpr)
    return;

  // Preserve tracking for explicit zero stores.
  if (isZeroLiteralExpr(BindExpr, C)) {
    State = State->set<AggregateDenomMap>(LHSReg, (unsigned)ADK_ZeroInitOnly);
    C.addTransition(State);
    return;
  }

  // If we already recognized it as an aggregate accumulator, keep that state
  // across symbolic/non-constant updates. This is important for patterns like:
  //   norm_fac += band->norm_fac;
  if (*Tracked == (unsigned)ADK_Aggregated || *Tracked == (unsigned)ADK_Guarded) {
    C.addTransition(State);
    return;
  }

  // Otherwise, a known non-zero constant assignment means this is no longer the
  // target "zero-initialized aggregate" pattern.
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

  const MemRegion *MR = getTrackedRegionFromZeroGuardCond(CondE, C);
  if (!MR)
    return;

  const unsigned *Tracked = State->get<AggregateDenomMap>(MR);
  if (!Tracked)
    return;

  // Treat any explicit branch on the denominator as a guard and suppress the
  // warning for this path.
  State = State->set<AggregateDenomMap>(MR, (unsigned)ADK_Guarded);
  C.addTransition(State);
}

void SAGenTestChecker::checkPreStmt(const BinaryOperator *BO,
                                    CheckerContext &C) const {
  if (!BO)
    return;

  ProgramStateRef State = C.getState();

  // First, recognize the target aggregate-construction pattern.
  const MemRegion *AggMR = nullptr;
  if (isAggregateUpdateExpr(BO, C, AggMR)) {
    const unsigned *Tracked = State->get<AggregateDenomMap>(AggMR);
    if (Tracked) {
      State = State->set<AggregateDenomMap>(AggMR, (unsigned)ADK_Aggregated);
      C.addTransition(State);
    }
  }

  // Then, diagnose division by a tracked aggregate denominator.
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
  if (*Tracked != (unsigned)ADK_Aggregated)
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
