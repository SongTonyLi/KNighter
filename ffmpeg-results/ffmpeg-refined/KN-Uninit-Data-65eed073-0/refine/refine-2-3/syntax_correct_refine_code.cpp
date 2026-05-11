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
REGISTER_SET_WITH_PROGRAMSTATE(CheckedShortReadRegions, const MemRegion *)

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
  bool isNamedCall(const CallEvent &Call, StringRef ExactName) const;
  bool isNamedCall(const CallExpr *CE, CheckerContext &C, StringRef ExactName) const;
  bool isAvioReadCall(const CallExpr *CE, CheckerContext &C) const;
  bool isAvioReadCall(const CallEvent &Call, CheckerContext &C) const;
  bool isMemcmpCall(const CallEvent &Call, CheckerContext &C) const;
  bool isSafeExactReadCall(const CallEvent &Call, CheckerContext &C) const;

  const MemRegion *getBaseRegion(const MemRegion *MR) const;
  const MemRegion *getTrackedRegionFromExpr(const Expr *E,
                                            CheckerContext &C) const;
  const MemRegion *getTrackedRegionFromAvioReadCall(const CallExpr *CE,
                                                    CheckerContext &C) const;
  const CallExpr *getAvioReadCallFromRetVar(const Expr *CondE,
                                            CheckerContext &C) const;

  bool isSubRegionOf(const MemRegion *Inner, const MemRegion *Outer) const;
  bool regionsOverlapOrSubregion(const MemRegion *A, const MemRegion *B) const;
  bool isTrackedUse(const MemRegion *UseMR, const MemRegion *TrackedMR) const;
  bool isFalsePositive(const MemRegion *UseMR, const MemRegion *TrackedMR,
                       const Stmt *S, CheckerContext &C) const;
  bool isBufferConsumingStmt(const Stmt *S, const MemRegion *UseMR,
                             const MemRegion *TrackedMR, CheckerContext &C) const;
  bool isCheckedRegion(const MemRegion *MR, CheckerContext &C) const;

  void clearTrackedRegion(const MemRegion *MR, CheckerContext &C) const;
  void markCheckedRegion(const MemRegion *MR, CheckerContext &C) const;
  void reportBug(const MemRegion *MR, const Stmt *S, CheckerContext &C,
                 StringRef Msg) const;
};

bool SAGenTestChecker::isNamedCall(const CallEvent &Call,
                                   StringRef ExactName) const {
  if (const IdentifierInfo *ID = Call.getCalleeIdentifier())
    return ID->getName() == ExactName;
  return false;
}

bool SAGenTestChecker::isNamedCall(const CallExpr *CE,
                                   CheckerContext &C,
                                   StringRef ExactName) const {
  if (!CE)
    return false;

  const Expr *Callee = CE->getCallee();
  if (!Callee)
    return false;

  Callee = Callee->IgnoreParenImpCasts();
  if (const auto *DRE = dyn_cast<DeclRefExpr>(Callee)) {
    if (const auto *FD = dyn_cast<FunctionDecl>(DRE->getDecl()))
      return FD->getIdentifier() && FD->getName() == ExactName;
  }

  return false;
}

bool SAGenTestChecker::isAvioReadCall(const CallExpr *CE,
                                      CheckerContext &C) const {
  return isNamedCall(CE, C, "avio_read");
}

bool SAGenTestChecker::isAvioReadCall(const CallEvent &Call,
                                      CheckerContext &C) const {
  return isNamedCall(Call, "avio_read");
}

bool SAGenTestChecker::isMemcmpCall(const CallEvent &Call,
                                    CheckerContext &C) const {
  return isNamedCall(Call, "memcmp");
}

bool SAGenTestChecker::isSafeExactReadCall(const CallEvent &Call,
                                           CheckerContext &C) const {
  return isNamedCall(Call, "ffio_read_size");
}

const MemRegion *SAGenTestChecker::getBaseRegion(const MemRegion *MR) const {
  if (!MR)
    return nullptr;

  const MemRegion *Cur = MR;
  while (const auto *SR = dyn_cast<SubRegion>(Cur)) {
    const MemRegion *Super = SR->getSuperRegion();
    if (!Super)
      break;
    Cur = Super;
  }
  return Cur ? Cur : MR;
}

const MemRegion *
SAGenTestChecker::getTrackedRegionFromExpr(const Expr *E,
                                           CheckerContext &C) const {
  if (!E)
    return nullptr;

  const MemRegion *MR = getMemRegionFromExpr(E, C);
  if (!MR)
    return nullptr;

  return MR;
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

bool SAGenTestChecker::isFalsePositive(const MemRegion *UseMR,
                                       const MemRegion *TrackedMR,
                                       const Stmt *S,
                                       CheckerContext &C) const {
  if (!UseMR || !TrackedMR || !S)
    return true;

  if (!regionsOverlapOrSubregion(UseMR, TrackedMR))
    return true;

  // Do not warn on mere pointer propagation / declaration initialization such as:
  //   cursor = bp.str;
  // This is not a semantic consumption of the buffer contents.
  if (const auto *DS = dyn_cast<DeclStmt>(S)) {
    for (const Decl *D : DS->decls()) {
      if (const auto *VD = dyn_cast<VarDecl>(D)) {
        if (const Expr *Init = VD->getInit()) {
          const MemRegion *InitMR = getTrackedRegionFromExpr(Init, C);
          if (InitMR && regionsOverlapOrSubregion(InitMR, TrackedMR))
            return true;
        }
      }
    }
  }

  if (const auto *BO = dyn_cast<BinaryOperator>(S)) {
    if (BO->isAssignmentOp()) {
      const Expr *RHS = BO->getRHS();
      const MemRegion *RHSMR = getTrackedRegionFromExpr(RHS, C);
      if (RHSMR && regionsOverlapOrSubregion(RHSMR, TrackedMR))
        return true;
    }
  }

  return false;
}

bool SAGenTestChecker::isBufferConsumingStmt(const Stmt *S,
                                             const MemRegion *UseMR,
                                             const MemRegion *TrackedMR,
                                             CheckerContext &C) const {
  if (!S || !UseMR || !TrackedMR)
    return false;

  if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(S)) {
    const MemRegion *BaseMR = getTrackedRegionFromExpr(ASE->getBase(), C);
    if (BaseMR && isTrackedUse(BaseMR, TrackedMR))
      return true;
  }

  if (isa<MemberExpr>(S))
    return isTrackedUse(UseMR, TrackedMR);

  if (isa<UnaryOperator>(S) || isa<ImplicitCastExpr>(S) || isa<CStyleCastExpr>(S))
    return isTrackedUse(UseMR, TrackedMR);

  // For plain loads, be conservative and require the location to be a subregion
  // of the tracked buffer rather than the buffer pointer object itself.
  if (UseMR != TrackedMR && isTrackedUse(UseMR, TrackedMR))
    return true;

  return false;
}

bool SAGenTestChecker::isCheckedRegion(const MemRegion *MR,
                                       CheckerContext &C) const {
  if (!MR)
    return false;

  ProgramStateRef State = C.getState();
  return State->contains<CheckedShortReadRegions>(MR);
}

void SAGenTestChecker::clearTrackedRegion(const MemRegion *MR,
                                          CheckerContext &C) const {
  if (!MR)
    return;

  ProgramStateRef State = C.getState();
  State = State->remove<ShortReadDstMap>(MR);
  State = State->remove<CheckedShortReadRegions>(MR);
  C.addTransition(State);
}

void SAGenTestChecker::markCheckedRegion(const MemRegion *MR,
                                         CheckerContext &C) const {
  if (!MR)
    return;

  ProgramStateRef State = C.getState();
  if (State->get<ShortReadDstMap>(MR))
    State = State->add<CheckedShortReadRegions>(MR);
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
  // Exact-size helper is safe: if it targets a tracked region, clear it.
  if (isSafeExactReadCall(Call, C)) {
    if (Call.getNumArgs() >= 2) {
      if (const Expr *DstE = Call.getArgExpr(1)) {
        if (const MemRegion *MR = getTrackedRegionFromExpr(DstE, C))
          clearTrackedRegion(MR, C);
      }
    }
    return;
  }

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
  State = State->remove<CheckedShortReadRegions>(DstMR);
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
      markCheckedRegion(DstMR, C);
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

  markCheckedRegion(DstMR, C);
}

void SAGenTestChecker::checkPreCall(const CallEvent &Call,
                                    CheckerContext &C) const {
  ProgramStateRef State = C.getState();

  auto HandleConsumerArg = [&](unsigned Idx) -> bool {
    if (Idx >= Call.getNumArgs())
      return false;

    const Expr *ArgE = Call.getArgExpr(Idx);
    if (!ArgE)
      return false;

    const MemRegion *UseMR = getTrackedRegionFromExpr(ArgE, C);
    if (!UseMR)
      return false;

    for (const auto &Entry : State->get<ShortReadDstMap>()) {
      const MemRegion *TrackedMR = Entry.first;
      if (!TrackedMR)
        continue;

      if (isCheckedRegion(TrackedMR, C))
        continue;

      if (!isTrackedUse(UseMR, TrackedMR))
        continue;

      if (isFalsePositive(UseMR, TrackedMR, Call.getOriginExpr(), C))
        continue;

      reportBug(TrackedMR, Call.getOriginExpr(),
                C, "buffer read by avio_read may be partially uninitialized");
      return true;
    }

    return false;
  };

  if (isMemcmpCall(Call, C)) {
    if (HandleConsumerArg(0))
      return;
    if (HandleConsumerArg(1))
      return;
    return;
  }

  llvm::SmallVector<unsigned, 4> DerefParams;
  if (functionKnownToDeref(Call, DerefParams)) {
    for (unsigned ParamIdx : DerefParams) {
      if (HandleConsumerArg(ParamIdx))
        return;
    }
  }
}

void SAGenTestChecker::checkLocation(SVal Loc, bool IsLoad, const Stmt *S,
                                     CheckerContext &C) const {
  if (!IsLoad || !S)
    return;

  ProgramStateRef State = C.getState();

  const MemRegion *UseMR = Loc.getAsRegion();
  if (!UseMR)
    return;

  for (const auto &Entry : State->get<ShortReadDstMap>()) {
    const MemRegion *TrackedMR = Entry.first;
    if (!TrackedMR)
      continue;

    if (isCheckedRegion(TrackedMR, C))
      continue;

    if (!isTrackedUse(UseMR, TrackedMR))
      continue;

    if (isFalsePositive(UseMR, TrackedMR, S, C))
      continue;

    if (!isBufferConsumingStmt(S, UseMR, TrackedMR, C))
      continue;

    reportBug(TrackedMR, S, C,
              "buffer read by avio_read may be partially uninitialized");
    return;
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
