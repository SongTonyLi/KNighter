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

enum class TrackedKind : unsigned char {
  UnknownZeroInit = 0,
  AggregateCandidate = 1,
  Guarded = 2,
  InvertedAggregate = 3
};

} // end anonymous namespace

REGISTER_MAP_WITH_PROGRAMSTATE(AggregateDenomMap, const MemRegion *, TrackedKind)

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
  if (EvaluateExprToInt(Res, E, C))
    return Res == 0;

  return false;
}

static bool isNonZeroLiteralExpr(const Expr *E, CheckerContext &C) {
  if (!E)
    return false;

  E = E->IgnoreParenImpCasts();
  if (!E)
    return false;

  if (const auto *FL = dyn_cast<FloatingLiteral>(E))
    return !FL->getValue().isZero();

  if (const auto *IL = dyn_cast<IntegerLiteral>(E))
    return !IL->getValue().isZero();

  llvm::APSInt Res;
  if (EvaluateExprToInt(Res, E, C))
    return Res != 0;

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

static const VarDecl *getReferencedVarDecl(const Expr *E) {
  if (!E)
    return nullptr;

  E = E->IgnoreParenImpCasts();
  if (!E)
    return nullptr;

  if (const auto *DRE = dyn_cast<DeclRefExpr>(E))
    return dyn_cast<VarDecl>(DRE->getDecl());

  return nullptr;
}

static bool exprReferencesRegion(const Expr *E, const MemRegion *TargetMR,
                                 CheckerContext &C) {
  if (!E || !TargetMR)
    return false;

  E = E->IgnoreParenImpCasts();
  if (!E)
    return false;

  if (const MemRegion *MR = getRegionFromTrackedExpr(E, C))
    return MR == TargetMR;

  if (const auto *BO = dyn_cast<BinaryOperator>(E))
    return exprReferencesRegion(BO->getLHS(), TargetMR, C) ||
           exprReferencesRegion(BO->getRHS(), TargetMR, C);

  if (const auto *UO = dyn_cast<UnaryOperator>(E))
    return exprReferencesRegion(UO->getSubExpr(), TargetMR, C);

  if (const auto *CO = dyn_cast<ConditionalOperator>(E))
    return exprReferencesRegion(CO->getCond(), TargetMR, C) ||
           exprReferencesRegion(CO->getTrueExpr(), TargetMR, C) ||
           exprReferencesRegion(CO->getFalseExpr(), TargetMR, C);

  return false;
}

static bool isSelfAccumulationExpr(const Expr *E, const MemRegion *LHSReg,
                                   CheckerContext &C) {
  if (!E || !LHSReg)
    return false;

  E = E->IgnoreParenImpCasts();
  if (!E)
    return false;

  const auto *BO = dyn_cast<BinaryOperator>(E);
  if (!BO)
    return false;

  if (BO->getOpcode() != BO_Add)
    return false;

  return exprReferencesRegion(BO->getLHS(), LHSReg, C) ||
         exprReferencesRegion(BO->getRHS(), LHSReg, C);
}

static bool isSelfDivisionExpr(const Expr *E, const MemRegion *LHSReg,
                               CheckerContext &C) {
  if (!E || !LHSReg)
    return false;

  E = E->IgnoreParenImpCasts();
  if (!E)
    return false;

  const auto *BO = dyn_cast<BinaryOperator>(E);
  if (!BO)
    return false;

  if (BO->getOpcode() != BO_Div)
    return false;

  const Expr *Denom = BO->getRHS()->IgnoreParenImpCasts();
  const Expr *Numer = BO->getLHS()->IgnoreParenImpCasts();

  if (!exprReferencesRegion(Denom, LHSReg, C))
    return false;

  // Target pattern is strongest for inversion by a constant, especially 1/x.
  // Accept any non-zero literal numerator to remain robust.
  if (isNonZeroLiteralExpr(Numer, C))
    return true;

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

  if (const auto *UO = dyn_cast<UnaryOperator>(Cond)) {
    if (UO->getOpcode() == UO_LNot) {
      if (const auto *DRE = dyn_cast<DeclRefExpr>(UO->getSubExpr()->IgnoreParenImpCasts())) {
        if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
          if (isRealFloatingType(VD->getType()))
            return getVarRegionFromDecl(VD, C);
        }
      }
    }
  }

  if (const auto *BO = dyn_cast<BinaryOperator>(Cond)) {
    if (BO->getOpcode() == BO_EQ || BO->getOpcode() == BO_NE) {
      const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
      const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();

      if (isZeroLiteralExpr(LHS, C)) {
        if (const auto *VD = getReferencedVarDecl(RHS)) {
          if (isRealFloatingType(VD->getType()))
            return getVarRegionFromDecl(VD, C);
        }
      }

      if (isZeroLiteralExpr(RHS, C)) {
        if (const auto *VD = getReferencedVarDecl(LHS)) {
          if (isRealFloatingType(VD->getType()))
            return getVarRegionFromDecl(VD, C);
        }
      }
    }
  }

  return nullptr;
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

    State = State->set<AggregateDenomMap>(MR, TrackedKind::UnknownZeroInit);
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

  const TrackedKind *KindPtr = State->get<AggregateDenomMap>(LHSReg);
  if (!KindPtr)
    return;

  const Expr *BindExpr = dyn_cast_or_null<Expr>(S);
  if (!BindExpr)
    return;

  BindExpr = BindExpr->IgnoreParenImpCasts();
  if (!BindExpr)
    return;

  TrackedKind Kind = *KindPtr;

  // Respect guards: once guarded, don't resurrect the warning state.
  if (Kind == TrackedKind::Guarded) {
    C.addTransition(State);
    return;
  }

  // Still zero after assignment.
  if (isZeroLiteralExpr(BindExpr, C)) {
    State = State->set<AggregateDenomMap>(LHSReg, TrackedKind::UnknownZeroInit);
    C.addTransition(State);
    return;
  }

  // Detect aggregate formation patterns.
  if (const auto *BO = dyn_cast<BinaryOperator>(BindExpr)) {
    if (BO->isCompoundAssignmentOp()) {
      if (BO->getOpcode() == BO_AddAssign) {
        State = State->set<AggregateDenomMap>(LHSReg, TrackedKind::AggregateCandidate);
        C.addTransition(State);
        return;
      }
    }

    if (isSelfAccumulationExpr(BindExpr, LHSReg, C)) {
      State = State->set<AggregateDenomMap>(LHSReg, TrackedKind::AggregateCandidate);
      C.addTransition(State);
      return;
    }

    if (Kind == TrackedKind::AggregateCandidate &&
        isSelfDivisionExpr(BindExpr, LHSReg, C)) {
      State = State->set<AggregateDenomMap>(LHSReg, TrackedKind::InvertedAggregate);
      C.addTransition(State);
      return;
    }
  }

  // If assigned a provably non-zero float/int constant and it is not part of
  // the target pattern anymore, stop tracking to reduce noise.
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

  const TrackedKind *KindPtr = State->get<AggregateDenomMap>(MR);
  if (!KindPtr)
    return;

  // Any explicit truthiness/zero comparison is treated as a guard for this
  // checker, because the bug pattern specifically requires lack of such guard.
  State = State->set<AggregateDenomMap>(MR, TrackedKind::Guarded);
  C.addTransition(State);
}

bool SAGenTestChecker::isFalsePositive(const BinaryOperator *BO,
                                       CheckerContext &C,
                                       const MemRegion *MR) const {
  const TrackedKind *KindPtr = C.getState()->get<AggregateDenomMap>(MR);
  if (!KindPtr)
    return true;

  // Only report the target pattern:
  // aggregate candidate later inverted and then used as divisor.
  if (*KindPtr != TrackedKind::InvertedAggregate)
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

  const TrackedKind *KindPtr = C.getState()->get<AggregateDenomMap>(MR);
  if (!KindPtr)
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
