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

struct AggregateInfo {
  bool SawZeroInit = false;
  bool SawAggregation = false;

  bool operator==(const AggregateInfo &Other) const {
    return SawZeroInit == Other.SawZeroInit &&
           SawAggregation == Other.SawAggregation;
  }

  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.AddBoolean(SawZeroInit);
    ID.AddBoolean(SawAggregation);
  }
};

} // end anonymous namespace

REGISTER_MAP_WITH_PROGRAMSTATE(AggregateDenomMap, const MemRegion *, AggregateInfo)

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

static bool exprReferencesRegion(const Expr *E, const MemRegion *Target,
                                 CheckerContext &C) {
  if (!E || !Target)
    return false;

  E = E->IgnoreParenImpCasts();
  if (!E)
    return false;

  const MemRegion *MR = getRegionFromTrackedExpr(E, C);
  if (MR && MR == Target)
    return true;

  for (const Stmt *Child : E->children()) {
    if (const auto *CE = dyn_cast_or_null<Expr>(Child)) {
      if (exprReferencesRegion(CE, Target, C))
        return true;
    }
  }

  return false;
}

static const MemRegion *getTrackedRegionFromExpr(const Expr *E, CheckerContext &C) {
  E = E ? E->IgnoreParenImpCasts() : nullptr;
  if (!E)
    return nullptr;

  if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
      if (isRealFloatingType(VD->getType()))
        return getVarRegionFromDecl(VD, C);
    }
  }

  return getRegionFromTrackedExpr(E, C);
}

static bool isSelfAccumulationAssignment(const BinaryOperator *BO,
                                         const MemRegion *LHSReg,
                                         CheckerContext &C) {
  if (!BO || !LHSReg)
    return false;

  BinaryOperatorKind Op = BO->getOpcode();

  if (Op == BO_AddAssign) {
    const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
    const MemRegion *L = getTrackedRegionFromExpr(LHS, C);
    return L && L == LHSReg;
  }

  if (Op != BO_Assign)
    return false;

  const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
  const auto *RHSBO = dyn_cast<BinaryOperator>(RHS);
  if (!RHSBO || RHSBO->getOpcode() != BO_Add)
    return false;

  const Expr *AddLHS = RHSBO->getLHS()->IgnoreParenImpCasts();
  const Expr *AddRHS = RHSBO->getRHS()->IgnoreParenImpCasts();

  const MemRegion *AL = getTrackedRegionFromExpr(AddLHS, C);
  const MemRegion *AR = getTrackedRegionFromExpr(AddRHS, C);

  return (AL && AL == LHSReg) || (AR && AR == LHSReg);
}

static bool isKnownNonZeroSVal(SVal V, CheckerContext &C) {
  std::optional<DefinedSVal> DV = V.getAs<DefinedSVal>();
  if (!DV)
    return false;

  ProgramStateRef State = C.getState();
  ProgramStateRef ZeroState, NonZeroState;
  std::tie(ZeroState, NonZeroState) = State->assume(*DV);

  return ZeroState == nullptr && NonZeroState != nullptr;
}

static bool isRegionConstrainedNonZero(const MemRegion *MR, CheckerContext &C) {
  if (!MR)
    return false;

  SVal V = C.getState()->getSVal(MR);
  return isKnownNonZeroSVal(V, C);
}

static bool isGuardedByConditionalOperator(const BinaryOperator *BO,
                                           const MemRegion *DenomMR,
                                           CheckerContext &C) {
  if (!BO || !DenomMR)
    return false;

  const auto *CO = findSpecificTypeInParents<ConditionalOperator>(BO, C);
  if (!CO)
    return false;

  const Expr *Cond = CO->getCond();
  const Expr *TrueExpr = CO->getTrueExpr();
  const Expr *FalseExpr = CO->getFalseExpr();

  if (!exprReferencesRegion(Cond, DenomMR, C))
    return false;

  bool InTrueArm = TrueExpr && exprReferencesRegion(TrueExpr, DenomMR, C) &&
                   exprReferencesRegion(BO, DenomMR, C);
  bool InFalseArm = FalseExpr && exprReferencesRegion(FalseExpr, DenomMR, C) &&
                    exprReferencesRegion(BO, DenomMR, C);

  return InTrueArm || InFalseArm;
}

static bool isFalsePositive(const BinaryOperator *BO, const MemRegion *MR,
                            CheckerContext &C) {
  if (!BO || !MR)
    return true;

  if (isRegionConstrainedNonZero(MR, C))
    return true;

  if (isGuardedByConditionalOperator(BO, MR, C))
    return true;

  return false;
}

class SAGenTestChecker
    : public Checker<check::PostStmt<DeclStmt>,
                     check::PreStmt<BinaryOperator>> {
  mutable std::unique_ptr<BugType> BT;

public:
  SAGenTestChecker()
      : BT(new BugType(this, "Possible division by zero",
                       "Possible division by zero")) {}

  void checkPostStmt(const DeclStmt *DS, CheckerContext &C) const;
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

    AggregateInfo Info;
    Info.SawZeroInit = true;
    Info.SawAggregation = false;
    State = State->set<AggregateDenomMap>(MR, Info);
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

  // Step 1: learn aggregate-denominator candidates from accumulation patterns.
  if (BO->isAssignmentOp()) {
    const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
    const MemRegion *LHSReg = getTrackedRegionFromExpr(LHS, C);
    if (LHSReg) {
      if (const AggregateInfo *Info = State->get<AggregateDenomMap>(LHSReg)) {
        if (Info->SawZeroInit && isSelfAccumulationAssignment(BO, LHSReg, C)) {
          AggregateInfo NewInfo = *Info;
          NewInfo.SawAggregation = true;
          if (!(NewInfo == *Info)) {
            State = State->set<AggregateDenomMap>(LHSReg, NewInfo);
            C.addTransition(State);
          }
        }
      }
    }
    return;
  }

  // Step 2: report only floating-point divisions whose denominator is a
  // zero-initialized variable that has subsequently participated in
  // self-aggregation and is not known nonzero on this path.
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

  const MemRegion *MR = getTrackedRegionFromExpr(Denom, C);
  if (!MR)
    return;

  const AggregateInfo *Info = State->get<AggregateDenomMap>(MR);
  if (!Info)
    return;
  if (!Info->SawZeroInit || !Info->SawAggregation)
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
```