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

REGISTER_MAP_WITH_PROGRAMSTATE(ShortReadDstMap, const MemRegion *, const CallExpr *)
REGISTER_MAP_WITH_PROGRAMSTATE(ShortReadRetValMap, const MemRegion *, const MemRegion *)
REGISTER_MAP_WITH_PROGRAMSTATE(InitializedAfterReadMap, const MemRegion *, bool)

namespace {

class SAGenTestChecker
    : public Checker<check::PostCall,
                     check::BranchCondition,
                     check::PreCall,
                     check::Location> {
  mutable std::unique_ptr<BugType> BT;

public:
  SAGenTestChecker()
      : BT(new BugType(this, "Unchecked avio_read result", "API")) {}

  void checkPostCall(const CallEvent &Call, CheckerContext &C) const;
  void checkBranchCondition(const Stmt *Condition, CheckerContext &C) const;
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
  void checkLocation(SVal Loc, bool IsLoad, const Stmt *S,
                     CheckerContext &C) const;

private:
  bool isAvioReadCall(const CallExpr *CE, CheckerContext &C) const;
  bool isAvioReadCall(const CallEvent &Call, CheckerContext &C) const;
  bool isMemcmpCall(const CallEvent &Call, CheckerContext &C) const;
  bool isMemsetCall(const CallEvent &Call, CheckerContext &C) const;

  const MemRegion *getTrackedRegionFromExpr(const Expr *E,
                                            CheckerContext &C) const;
  const MemRegion *getTrackedRegionFromAvioReadCall(const CallExpr *CE,
                                                    CheckerContext &C) const;
  const CallExpr *getAvioReadCallFromRetVar(const Expr *CondE,
                                            CheckerContext &C) const;
  const MemRegion *getRetVarRegionFromAvioReadCall(const CallExpr *CE,
                                                   CheckerContext &C) const;

  bool isSubRegionOf(const MemRegion *Inner, const MemRegion *Outer) const;
  bool regionsOverlapOrSubregion(const MemRegion *A, const MemRegion *B) const;
  bool isTrackedUse(const MemRegion *UseMR, const MemRegion *TrackedMR) const;

  bool exprReferencesRegion(const Expr *E, const MemRegion *MR,
                            CheckerContext &C) const;
  bool exprReferencesRetRegion(const Expr *E, const MemRegion *RetMR,
                               CheckerContext &C) const;

  bool callUsesTrackedBufferAndRetVal(const CallEvent &Call,
                                      const MemRegion *TrackedMR,
                                      const MemRegion *RetMR,
                                      CheckerContext &C) const;

  bool handleMemsetInitialization(const CallEvent &Call,
                                  CheckerContext &C) const;

  bool isFalsePositive(const MemRegion *UseMR, const MemRegion *TrackedMR,
                       const Stmt *S, CheckerContext &C) const;

  void clearTrackedRegion(const MemRegion *MR, CheckerContext &C) const;
  void markInitializedAfterRead(const MemRegion *MR, CheckerContext &C) const;
  bool isInitializedAfterRead(const MemRegion *MR, CheckerContext &C) const;

  void reportBug(const MemRegion *MR, const Stmt *S, CheckerContext &C,
                 StringRef Msg) const;
};

bool SAGenTestChecker::isAvioReadCall(const CallExpr *CE,
                                      CheckerContext &C) const {
  if (!CE)
    return false;
  return ExprHasName(CE, "avio_read", C) && !ExprHasName(CE, "ffio_read_size", C);
}

bool SAGenTestChecker::isAvioReadCall(const CallEvent &Call,
                                      CheckerContext &C) const {
  const Expr *OriginExpr = Call.getOriginExpr();
  if (!OriginExpr)
    return false;
  return ExprHasName(OriginExpr, "avio_read", C) &&
         !ExprHasName(OriginExpr, "ffio_read_size", C);
}

bool SAGenTestChecker::isMemcmpCall(const CallEvent &Call,
                                    CheckerContext &C) const {
  const Expr *OriginExpr = Call.getOriginExpr();
  if (!OriginExpr)
    return false;
  return ExprHasName(OriginExpr, "memcmp", C);
}

bool SAGenTestChecker::isMemsetCall(const CallEvent &Call,
                                    CheckerContext &C) const {
  const Expr *OriginExpr = Call.getOriginExpr();
  if (!OriginExpr)
    return false;
  return ExprHasName(OriginExpr, "memset", C);
}

const MemRegion *
SAGenTestChecker::getTrackedRegionFromExpr(const Expr *E,
                                           CheckerContext &C) const {
  if (!E)
    return nullptr;

  const MemRegion *MR = getMemRegionFromExpr(E, C);
  if (MR)
    return MR;

  return nullptr;
}

const MemRegion *
SAGenTestChecker::getTrackedRegionFromAvioReadCall(const CallExpr *CE,
                                                   CheckerContext &C) const {
  if (!CE)
    return nullptr;
  if (!isAvioReadCall(CE, C))
    return nullptr;
  if (CE->getNumArgs() != 3)
    return nullptr;

  return getTrackedRegionFromExpr(CE->getArg(1), C);
}

const MemRegion *
SAGenTestChecker::getRetVarRegionFromAvioReadCall(const CallExpr *CE,
                                                  CheckerContext &C) const {
  if (!CE)
    return nullptr;

  const Stmt *Parent = findSpecificTypeInParents<Stmt>(CE, C);
  if (!Parent)
    return nullptr;

  if (const auto *BO = dyn_cast<BinaryOperator>(Parent)) {
    if (BO->isAssignmentOp()) {
      const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
      return getTrackedRegionFromExpr(LHS, C);
    }
  }

  if (const auto *DS = dyn_cast<DeclStmt>(Parent)) {
    for (const Decl *D : DS->decls()) {
      if (const auto *VD = dyn_cast<VarDecl>(D)) {
        if (VD->getInit() &&
            VD->getInit()->IgnoreParenImpCasts() == CE->IgnoreParenImpCasts()) {
          return C.getSValBuilder().getRegionManager().getVarRegion(
              VD, C.getLocationContext());
        }
      }
    }
  }

  return nullptr;
}

const CallExpr *
SAGenTestChecker::getAvioReadCallFromRetVar(const Expr *CondE,
                                            CheckerContext &C) const {
  if (!CondE)
    return nullptr;

  const DeclRefExpr *DRE = findSpecificTypeInChildren<DeclRefExpr>(CondE);
  if (!DRE)
    return nullptr;

  const VarDecl *VD = dyn_cast<VarDecl>(DRE->getDecl());
  if (!VD)
    return nullptr;

  if (const Expr *Init = VD->getInit()) {
    if (const CallExpr *CE = findSpecificTypeInChildren<CallExpr>(Init)) {
      if (isAvioReadCall(CE, C))
        return CE;
    }
    if (const CallExpr *CE = dyn_cast<CallExpr>(Init->IgnoreParenImpCasts())) {
      if (isAvioReadCall(CE, C))
        return CE;
    }
  }

  ProgramStateRef State = C.getState();
  const MemRegion *RetMR =
      C.getSValBuilder().getRegionManager().getVarRegion(VD, C.getLocationContext());

  for (const auto &Entry : State->get<ShortReadRetValMap>()) {
    if (Entry.second == RetMR) {
      if (const CallExpr *const *CE = State->get<ShortReadDstMap>(Entry.first))
        return *CE;
    }
  }

  return nullptr;
}

bool SAGenTestChecker::isSubRegionOf(const MemRegion *Inner,
                                     const MemRegion *Outer) const {
  if (!Inner || !Outer)
    return false;

  const MemRegion *Cur = Inner;
  while (Cur) {
    if (Cur == Outer)
      return true;
    const SubRegion *SR = dyn_cast<SubRegion>(Cur);
    if (!SR)
      break;
    Cur = SR->getSuperRegion();
  }
  return false;
}

bool SAGenTestChecker::regionsOverlapOrSubregion(const MemRegion *A,
                                                 const MemRegion *B) const {
  if (!A || !B)
    return false;

  if (A == B)
    return true;

  return isSubRegionOf(A, B) || isSubRegionOf(B, A);
}

bool SAGenTestChecker::isTrackedUse(const MemRegion *UseMR,
                                    const MemRegion *TrackedMR) const {
  return regionsOverlapOrSubregion(UseMR, TrackedMR);
}

bool SAGenTestChecker::exprReferencesRegion(const Expr *E, const MemRegion *MR,
                                            CheckerContext &C) const {
  if (!E || !MR)
    return false;

  if (const MemRegion *EMR = getTrackedRegionFromExpr(E, C))
    return regionsOverlapOrSubregion(EMR, MR);

  if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(E->IgnoreParenImpCasts())) {
    if (const MemRegion *BaseMR = getTrackedRegionFromExpr(ASE->getBase(), C))
      return regionsOverlapOrSubregion(BaseMR, MR);
  }

  if (const auto *UO = dyn_cast<UnaryOperator>(E->IgnoreParenImpCasts())) {
    if (UO->getOpcode() == UO_AddrOf || UO->getOpcode() == UO_Deref)
      return exprReferencesRegion(UO->getSubExpr(), MR, C);
  }

  if (const auto *BO = dyn_cast<BinaryOperator>(E->IgnoreParenImpCasts())) {
    if (BO->isAdditiveOp())
      return exprReferencesRegion(BO->getLHS(), MR, C) ||
             exprReferencesRegion(BO->getRHS(), MR, C);
  }

  return false;
}

bool SAGenTestChecker::exprReferencesRetRegion(const Expr *E,
                                               const MemRegion *RetMR,
                                               CheckerContext &C) const {
  if (!E || !RetMR)
    return false;

  if (const MemRegion *EMR = getTrackedRegionFromExpr(E, C))
    return regionsOverlapOrSubregion(EMR, RetMR);

  if (const auto *BO = dyn_cast<BinaryOperator>(E->IgnoreParenImpCasts()))
    return exprReferencesRetRegion(BO->getLHS(), RetMR, C) ||
           exprReferencesRetRegion(BO->getRHS(), RetMR, C);

  if (const auto *UO = dyn_cast<UnaryOperator>(E->IgnoreParenImpCasts()))
    return exprReferencesRetRegion(UO->getSubExpr(), RetMR, C);

  return false;
}

bool SAGenTestChecker::callUsesTrackedBufferAndRetVal(const CallEvent &Call,
                                                      const MemRegion *TrackedMR,
                                                      const MemRegion *RetMR,
                                                      CheckerContext &C) const {
  bool SawBuffer = false;
  bool SawRetVal = false;

  for (unsigned I = 0; I < Call.getNumArgs(); ++I) {
    const Expr *ArgE = Call.getArgExpr(I);
    if (!ArgE)
      continue;

    if (!SawBuffer && exprReferencesRegion(ArgE, TrackedMR, C))
      SawBuffer = true;

    if (!SawRetVal && exprReferencesRetRegion(ArgE, RetMR, C))
      SawRetVal = true;
  }

  return SawBuffer && SawRetVal;
}

void SAGenTestChecker::markInitializedAfterRead(const MemRegion *MR,
                                                CheckerContext &C) const {
  if (!MR)
    return;

  ProgramStateRef State = C.getState();
  State = State->set<InitializedAfterReadMap>(MR, true);
  C.addTransition(State);
}

bool SAGenTestChecker::isInitializedAfterRead(const MemRegion *MR,
                                              CheckerContext &C) const {
  if (!MR)
    return false;

  ProgramStateRef State = C.getState();
  if (const bool *B = State->get<InitializedAfterReadMap>(MR))
    return *B;

  return false;
}

bool SAGenTestChecker::handleMemsetInitialization(const CallEvent &Call,
                                                  CheckerContext &C) const {
  if (!isMemsetCall(Call, C))
    return false;

  if (Call.getNumArgs() < 3)
    return false;

  const Expr *DstE = Call.getArgExpr(0);
  if (!DstE)
    return false;

  ProgramStateRef State = C.getState();
  for (const auto &Entry : State->get<ShortReadDstMap>()) {
    const MemRegion *TrackedMR = Entry.first;
    if (!TrackedMR)
      continue;

    if (exprReferencesRegion(DstE, TrackedMR, C)) {
      markInitializedAfterRead(TrackedMR, C);
      return true;
    }
  }

  return false;
}

bool SAGenTestChecker::isFalsePositive(const MemRegion *UseMR,
                                       const MemRegion *TrackedMR,
                                       const Stmt *S,
                                       CheckerContext &C) const {
  if (!UseMR || !TrackedMR || !S)
    return true;

  if (!regionsOverlapOrSubregion(UseMR, TrackedMR))
    return true;

  if (isInitializedAfterRead(TrackedMR, C))
    return true;

  return false;
}

void SAGenTestChecker::clearTrackedRegion(const MemRegion *MR,
                                          CheckerContext &C) const {
  if (!MR)
    return;

  ProgramStateRef State = C.getState();
  bool Changed = false;

  if (State->get<ShortReadDstMap>(MR)) {
    State = State->remove<ShortReadDstMap>(MR);
    Changed = true;
  }

  if (State->get<ShortReadRetValMap>(MR)) {
    State = State->remove<ShortReadRetValMap>(MR);
    Changed = true;
  }

  if (State->get<InitializedAfterReadMap>(MR)) {
    State = State->remove<InitializedAfterReadMap>(MR);
    Changed = true;
  }

  if (Changed)
    C.addTransition(State);
}

void SAGenTestChecker::reportBug(const MemRegion *MR, const Stmt *S,
                                 CheckerContext &C, StringRef Msg) const {
  if (!MR || !S)
    return;

  ExplodedNode *N = C.generateNonFatalErrorNode();
  if (!N)
    return;

  auto R = std::make_unique<PathSensitiveBugReport>(*BT, Msg, N);
  R->addRange(S->getSourceRange());
  C.emitReport(std::move(R));
}

void SAGenTestChecker::checkPostCall(const CallEvent &Call,
                                     CheckerContext &C) const {
  if (!isAvioReadCall(Call, C))
    return;

  const Expr *OriginExpr = Call.getOriginExpr();
  if (!OriginExpr)
    return;

  const CallExpr *CE = dyn_cast<CallExpr>(OriginExpr->IgnoreParenImpCasts());
  if (!CE)
    return;

  if (CE->getNumArgs() != 3)
    return;

  const MemRegion *DstMR = getTrackedRegionFromExpr(CE->getArg(1), C);
  if (!DstMR)
    return;

  ProgramStateRef State = C.getState();
  State = State->set<ShortReadDstMap>(DstMR, CE);

  if (const MemRegion *RetVarMR = getRetVarRegionFromAvioReadCall(CE, C))
    State = State->set<ShortReadRetValMap>(DstMR, RetVarMR);

  State = State->remove<InitializedAfterReadMap>(DstMR);
  C.addTransition(State);
}

void SAGenTestChecker::checkBranchCondition(const Stmt *Condition,
                                            CheckerContext &C) const {
  if (!Condition)
    return;

  const CallExpr *DirectCall = findSpecificTypeInChildren<CallExpr>(Condition);
  if (DirectCall && isAvioReadCall(DirectCall, C)) {
    const MemRegion *DstMR = getTrackedRegionFromAvioReadCall(DirectCall, C);
    if (DstMR)
      clearTrackedRegion(DstMR, C);
    return;
  }

  const Expr *CondE = dyn_cast<Expr>(Condition);
  if (!CondE)
    return;

  const CallExpr *TrackedCall = getAvioReadCallFromRetVar(CondE, C);
  if (!TrackedCall)
    return;

  const MemRegion *DstMR = getTrackedRegionFromAvioReadCall(TrackedCall, C);
  if (!DstMR)
    return;

  clearTrackedRegion(DstMR, C);
}

void SAGenTestChecker::checkPreCall(const CallEvent &Call,
                                    CheckerContext &C) const {
  if (handleMemsetInitialization(Call, C))
    return;

  ProgramStateRef State = C.getState();

  for (const auto &Entry : State->get<ShortReadDstMap>()) {
    const MemRegion *TrackedMR = Entry.first;
    if (!TrackedMR)
      continue;

    const MemRegion *RetMR = nullptr;
    if (const MemRegion *const *RetMRPtr =
            State->get<ShortReadRetValMap>(TrackedMR))
      RetMR = *RetMRPtr;

    if (RetMR && callUsesTrackedBufferAndRetVal(Call, TrackedMR, RetMR, C) &&
        !isMemcmpCall(Call, C)) {
      // This is the key FP suppression:
      // a helper/parser consumes the same buffer together with the actual
      // avio_read() result, so the use is explicitly bounded by bytes read.
      return;
    }
  }

  if (!isMemcmpCall(Call, C))
    return;

  if (Call.getNumArgs() < 2)
    return;

  for (unsigned Idx = 0; Idx < 2; ++Idx) {
    const Expr *ArgE = Call.getArgExpr(Idx);
    if (!ArgE)
      continue;

    const MemRegion *UseMR = getTrackedRegionFromExpr(ArgE, C);
    if (!UseMR)
      continue;

    for (const auto &Entry : State->get<ShortReadDstMap>()) {
      const MemRegion *TrackedMR = Entry.first;
      if (!TrackedMR)
        continue;

      if (!isTrackedUse(UseMR, TrackedMR))
        continue;

      if (isFalsePositive(UseMR, TrackedMR, Call.getOriginExpr(), C))
        continue;

      reportBug(TrackedMR, Call.getOriginExpr(),
                C, "buffer read by avio_read may be partially uninitialized");
      return;
    }
  }
}

void SAGenTestChecker::checkLocation(SVal Loc, bool IsLoad, const Stmt *S,
                                     CheckerContext &C) const {
  ProgramStateRef State = C.getState();

  const MemRegion *UseMR = Loc.getAsRegion();
  if (!UseMR)
    return;

  for (const auto &Entry : State->get<ShortReadDstMap>()) {
    const MemRegion *TrackedMR = Entry.first;
    if (!TrackedMR)
      continue;

    if (!isTrackedUse(UseMR, TrackedMR))
      continue;

    if (isFalsePositive(UseMR, TrackedMR, S, C))
      continue;

    if (IsLoad) {
      reportBug(TrackedMR, S, C,
                "buffer read by avio_read may be partially uninitialized");
      return;
    }

    if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(S)) {
      if (!ASE->getBase())
        return;

      const MemRegion *BaseMR = getTrackedRegionFromExpr(ASE->getBase(), C);
      if (!BaseMR)
        return;

      if (isTrackedUse(BaseMR, TrackedMR) &&
          !isFalsePositive(BaseMR, TrackedMR, S, C)) {
        reportBug(TrackedMR, S, C,
                  "buffer read by avio_read may be partially uninitialized");
        return;
      }
    }
  }
}

} // end anonymous namespace

extern "C" void clang_registerCheckers(CheckerRegistry &registry) {
  registry.addChecker<SAGenTestChecker>(
      "custom.SAGenTestChecker",
      "Detects uses of buffers filled by avio_read() without checking for short reads",
      "");
}

extern "C" const char clang_analyzerAPIVersionString[] =
    CLANG_ANALYZER_API_VERSION_STRING;
