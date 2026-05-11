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

REGISTER_MAP_WITH_PROGRAMSTATE(ZeroInitFloatMap, const MemRegion *, bool)
REGISTER_MAP_WITH_PROGRAMSTATE(AggregateDenomMap, const MemRegion *, bool)
REGISTER_MAP_WITH_PROGRAMSTATE(GuardedDenomMap, const MemRegion *, bool)

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

static const MemRegion *getRegionFromExpr(const Expr *E, CheckerContext &C) {
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

static bool exprReferencesRegion(const Expr *E, const MemRegion *Target,
                                 CheckerContext &C) {
  if (!E || !Target)
    return false;

  E = E->IgnoreParenImpCasts();
  if (!E)
    return false;

  if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
      const MemRegion *MR = getVarRegionFromDecl(VD, C);
      return MR && MR == Target;
    }
  }

  if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
    return exprReferencesRegion(BO->getLHS(), Target, C) ||
           exprReferencesRegion(BO->getRHS(), Target, C);
  }

  if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
    return exprReferencesRegion(UO->getSubExpr(), Target, C);
  }

  if (const auto *CE = dyn_cast<CastExpr>(E)) {
    return exprReferencesRegion(CE->getSubExpr(), Target, C);
  }

  if (const auto *PE = dyn_cast<ParenExpr>(E)) {
    return exprReferencesRegion(PE->getSubExpr(), Target, C);
  }

  if (const auto *ME = dyn_cast<MemberExpr>(E)) {
    return exprReferencesRegion(ME->getBase(), Target, C);
  }

  if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(E)) {
    return exprReferencesRegion(ASE->getBase(), Target, C) ||
           exprReferencesRegion(ASE->getIdx(), Target, C);
  }

  if (const auto *COCE = dyn_cast<ConditionalOperator>(E)) {
    return exprReferencesRegion(COCE->getCond(), Target, C) ||
           exprReferencesRegion(COCE->getTrueExpr(), Target, C) ||
           exprReferencesRegion(COCE->getFalseExpr(), Target, C);
  }

  return false;
}

static bool isSelfAdditiveAggregateUpdate(const BinaryOperator *BO,
                                          CheckerContext &C,
                                          const MemRegion *&TrackedMR) {
  TrackedMR = nullptr;
  if (!BO)
    return false;

  if (BO->getOpcode() == BO_AddAssign) {
    const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
    const auto *DRE = dyn_cast<DeclRefExpr>(LHS);
    if (!DRE)
      return false;

    const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
    if (!VD || !isRealFloatingType(VD->getType()))
      return false;

    const MemRegion *MR = getVarRegionFromDecl(VD, C);
    if (!MR)
      return false;

    TrackedMR = MR;
    return true;
  }

  if (BO->getOpcode() != BO_Assign)
    return false;

  const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
  const auto *DRE = dyn_cast<DeclRefExpr>(LHS);
  if (!DRE)
    return false;

  const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
  if (!VD || !isRealFloatingType(VD->getType()))
    return false;

  const MemRegion *MR = getVarRegionFromDecl(VD, C);
  if (!MR)
    return false;

  const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
  const auto *Add = dyn_cast<BinaryOperator>(RHS);
  if (!Add || Add->getOpcode() != BO_Add)
    return false;

  bool RefL = exprReferencesRegion(Add->getLHS(), MR, C);
  bool RefR = exprReferencesRegion(Add->getRHS(), MR, C);

  if (!(RefL || RefR))
    return false;

  TrackedMR = MR;
  return true;
}

enum GuardKind {
  GK_None,
  GK_NonZeroOnTrue,
  GK_NonZeroOnFalse
};

static GuardKind classifyZeroGuardCondition(const Stmt *Condition,
                                            const MemRegion *&TrackedMR,
                                            CheckerContext &C) {
  TrackedMR = nullptr;

  const Expr *Cond = dyn_cast_or_null<Expr>(Condition);
  if (!Cond)
    return GK_None;

  Cond = Cond->IgnoreParenImpCasts();
  if (!Cond)
    return GK_None;

  if (const auto *DRE = dyn_cast<DeclRefExpr>(Cond)) {
    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
      if (!isRealFloatingType(VD->getType()))
        return GK_None;
      TrackedMR = getVarRegionFromDecl(VD, C);
      return TrackedMR ? GK_NonZeroOnTrue : GK_None;
    }
  }

  if (const auto *BO = dyn_cast<BinaryOperator>(Cond)) {
    if (BO->getOpcode() == BO_NE || BO->getOpcode() == BO_EQ) {
      const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
      const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();

      auto extractTrackedMR = [&](const Expr *VarSide,
                                  const Expr *ZeroSide) -> const MemRegion * {
        if (!isZeroLiteralExpr(ZeroSide, C))
          return nullptr;
        const auto *DRE = dyn_cast<DeclRefExpr>(VarSide);
        if (!DRE)
          return nullptr;
        const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
        if (!VD || !isRealFloatingType(VD->getType()))
          return nullptr;
        return getVarRegionFromDecl(VD, C);
      };

      if (const MemRegion *MR = extractTrackedMR(LHS, RHS)) {
        TrackedMR = MR;
        return BO->getOpcode() == BO_NE ? GK_NonZeroOnTrue : GK_NonZeroOnFalse;
      }

      if (const MemRegion *MR = extractTrackedMR(RHS, LHS)) {
        TrackedMR = MR;
        return BO->getOpcode() == BO_NE ? GK_NonZeroOnTrue : GK_NonZeroOnFalse;
      }
    }
  }

  return GK_None;
}

class SAGenTestChecker
    : public Checker<check::PostStmt<DeclStmt>,
                     check::PreStmt<BinaryOperator>,
                     check::BranchCondition,
                     eval::Assume,
                     check::EndFunction> {
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
  void checkEndFunction(const ReturnStmt *RS, CheckerContext &C) const;

private:
  bool shouldTrackAsAggregateCandidate(const MemRegion *MR,
                                       CheckerContext &C) const;
  bool isFalsePositive(const BinaryOperator *BO, CheckerContext &C,
                       const MemRegion *MR) const;
  void reportBug(const BinaryOperator *BO, CheckerContext &C,
                 const MemRegion *MR) const;
};

bool SAGenTestChecker::shouldTrackAsAggregateCandidate(const MemRegion *MR,
                                                       CheckerContext &C) const {
  if (!MR)
    return false;
  const bool *B = C.getState()->get<ZeroInitFloatMap>(MR);
  return B && *B;
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

    State = State->set<ZeroInitFloatMap>(MR, true);
    Changed = true;
  }

  if (Changed)
    C.addTransition(State);
}

void SAGenTestChecker::checkBranchCondition(const Stmt *Condition,
                                            CheckerContext &C) const {
  const MemRegion *TrackedMR = nullptr;
  GuardKind GK = classifyZeroGuardCondition(Condition, TrackedMR, C);
  if (GK == GK_None || !TrackedMR)
    return;

  ProgramStateRef State = C.getState();
  State = State->set<GuardedDenomMap>(TrackedMR, true);
  C.addTransition(State);
}

ProgramStateRef SAGenTestChecker::evalAssume(ProgramStateRef State, SVal Cond,
                                             bool Assumption) const {
  (void)Cond;
  (void)Assumption;
  return State;
}

void SAGenTestChecker::checkEndFunction(const ReturnStmt *RS,
                                        CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  if (State->get<ZeroInitFloatMap>().isEmpty() &&
      State->get<AggregateDenomMap>().isEmpty() &&
      State->get<GuardedDenomMap>().isEmpty())
    return;

  State = State->remove<ZeroInitFloatMap>();
  State = State->remove<AggregateDenomMap>();
  State = State->remove<GuardedDenomMap>();
  C.addTransition(State);
  (void)RS;
}

bool SAGenTestChecker::isFalsePositive(const BinaryOperator *BO,
                                       CheckerContext &C,
                                       const MemRegion *MR) const {
  if (!BO || !MR)
    return true;

  const bool *Agg = C.getState()->get<AggregateDenomMap>(MR);
  if (!Agg || !*Agg)
    return true;

  const bool *Guarded = C.getState()->get<GuardedDenomMap>(MR);
  if (Guarded && *Guarded)
    return true;

  return false;
}

void SAGenTestChecker::checkPreStmt(const BinaryOperator *BO,
                                    CheckerContext &C) const {
  if (!BO)
    return;

  ProgramStateRef State = C.getState();

  const MemRegion *AggMR = nullptr;
  if (isSelfAdditiveAggregateUpdate(BO, C, AggMR)) {
    if (AggMR && shouldTrackAsAggregateCandidate(AggMR, C)) {
      State = State->set<AggregateDenomMap>(AggMR, true);
      State = State->remove<GuardedDenomMap>(AggMR);
      C.addTransition(State);
    }
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
  if (!VD || !isRealFloatingType(VD->getType()))
    return;

  const MemRegion *MR = getVarRegionFromDecl(VD, C);
  if (!MR)
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
  (void)MR;
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
