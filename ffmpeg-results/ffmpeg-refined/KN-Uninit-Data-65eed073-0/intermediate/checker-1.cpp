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
REGISTER_MAP_WITH_PROGRAMSTATE(ReadRetVarToDstMap, const VarDecl *, const MemRegion *)
REGISTER_MAP_WITH_PROGRAMSTATE(DstToReadRetVarMap, const MemRegion *, const VarDecl *)
REGISTER_MAP_WITH_PROGRAMSTATE(SafeDstMap, const MemRegion *, bool)

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
  bool isAvShrinkPacketCall(const CallEvent &Call, CheckerContext &C) const;

  const MemRegion *getTrackedRegionFromExpr(const Expr *E,
                                            CheckerContext &C) const;
  const MemRegion *getTrackedRegionFromAvioReadCall(const CallExpr *CE,
                                                    CheckerContext &C) const;
  const CallExpr *getAvioReadCallFromRetVar(const Expr *CondE,
                                            CheckerContext &C) const;
  const VarDecl *getAssignedVarFromStmt(const Stmt *S) const;
  const VarDecl *getRetVarForAvioReadResult(const CallEvent &Call,
                                            CheckerContext &C) const;

  bool isSubRegionOf(const MemRegion *Inner, const MemRegion *Outer) const;
  bool regionsOverlapOrSubregion(const MemRegion *A, const MemRegion *B) const;
  bool isTrackedUse(const MemRegion *UseMR, const MemRegion *TrackedMR) const;
  bool isFalsePositive(const MemRegion *UseMR, const MemRegion *TrackedMR,
                       const Stmt *S, CheckerContext &C) const;

  bool conditionMentionsTrackedRetVar(const Stmt *Condition,
                                      const VarDecl *VD) const;
  bool isSanitizingMemsetForTrackedDst(const CallEvent &Call,
                                       const MemRegion *TrackedMR,
                                       CheckerContext &C) const;
  bool isSanitizingShrinkForTrackedDst(const CallEvent &Call,
                                       const MemRegion *TrackedMR,
                                       CheckerContext &C) const;
  const MemRegion *findTrackedRegionForUse(const MemRegion *UseMR,
                                           ProgramStateRef State) const;

  void clearTrackedRegion(const MemRegion *MR, CheckerContext &C) const;
  void markTrackedRegionSafe(const MemRegion *MR, CheckerContext &C) const;
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

bool SAGenTestChecker::isAvShrinkPacketCall(const CallEvent &Call,
                                            CheckerContext &C) const {
  const Expr *OriginExpr = Call.getOriginExpr();
  if (!OriginExpr)
    return false;
  return ExprHasName(OriginExpr, "av_shrink_packet", C);
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

const VarDecl *SAGenTestChecker::getAssignedVarFromStmt(const Stmt *S) const {
  if (!S)
    return nullptr;

  if (const auto *DS = dyn_cast<DeclStmt>(S)) {
    for (const Decl *D : DS->decls()) {
      if (const auto *VD = dyn_cast<VarDecl>(D))
        return VD;
    }
  }

  if (const auto *BO = dyn_cast<BinaryOperator>(S)) {
    if (BO->isAssignmentOp()) {
      if (const auto *DRE =
              dyn_cast<DeclRefExpr>(BO->getLHS()->IgnoreParenImpCasts()))
        return dyn_cast<VarDecl>(DRE->getDecl());
    }
  }

  return nullptr;
}

const VarDecl *
SAGenTestChecker::getRetVarForAvioReadResult(const CallEvent &Call,
                                             CheckerContext &C) const {
  const Expr *OriginExpr = Call.getOriginExpr();
  if (!OriginExpr)
    return nullptr;

  const Stmt *ParentBO = findSpecificTypeInParents<BinaryOperator>(OriginExpr, C);
  if (ParentBO) {
    if (const auto *BO = dyn_cast<BinaryOperator>(ParentBO)) {
      if (BO->isAssignmentOp()) {
        if (const auto *DRE =
                dyn_cast<DeclRefExpr>(BO->getLHS()->IgnoreParenImpCasts()))
          return dyn_cast<VarDecl>(DRE->getDecl());
      }
    }
  }

  const Stmt *ParentDS = findSpecificTypeInParents<DeclStmt>(OriginExpr, C);
  return getAssignedVarFromStmt(ParentDS);
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

  ProgramStateRef State = C.getState();
  if (State->get<SafeDstMap>(TrackedMR))
    return true;

  return false;
}

bool SAGenTestChecker::conditionMentionsTrackedRetVar(const Stmt *Condition,
                                                      const VarDecl *VD) const {
  if (!Condition || !VD)
    return false;

  const DeclRefExpr *DRE = findSpecificTypeInChildren<DeclRefExpr>(Condition);
  if (!DRE)
    return false;

  return DRE->getDecl() == VD;
}

bool SAGenTestChecker::isSanitizingMemsetForTrackedDst(const CallEvent &Call,
                                                       const MemRegion *TrackedMR,
                                                       CheckerContext &C) const {
  if (!isMemsetCall(Call, C))
    return false;
  if (Call.getNumArgs() < 1)
    return false;

  const Expr *DstArg = Call.getArgExpr(0);
  if (!DstArg)
    return false;

  const MemRegion *MemsetMR = getTrackedRegionFromExpr(DstArg, C);
  if (!MemsetMR)
    return false;

  return regionsOverlapOrSubregion(MemsetMR, TrackedMR);
}

bool SAGenTestChecker::isSanitizingShrinkForTrackedDst(const CallEvent &Call,
                                                       const MemRegion *TrackedMR,
                                                       CheckerContext &C) const {
  if (!isAvShrinkPacketCall(Call, C))
    return false;
  if (Call.getNumArgs() < 1)
    return false;

  const Expr *PktArg = Call.getArgExpr(0);
  if (!PktArg)
    return false;

  const MemRegion *PktMR = getTrackedRegionFromExpr(PktArg, C);
  if (!PktMR)
    return false;

  return regionsOverlapOrSubregion(PktMR, TrackedMR) ||
         regionsOverlapOrSubregion(TrackedMR, PktMR);
}

const MemRegion *SAGenTestChecker::findTrackedRegionForUse(const MemRegion *UseMR,
                                                           ProgramStateRef State) const {
  if (!UseMR)
    return nullptr;

  for (const auto &Entry : State->get<ShortReadDstMap>()) {
    const MemRegion *TrackedMR = Entry.first;
    if (!TrackedMR)
      continue;
    if (regionsOverlapOrSubregion(UseMR, TrackedMR))
      return TrackedMR;
  }
  return nullptr;
}

void SAGenTestChecker::clearTrackedRegion(const MemRegion *MR,
                                          CheckerContext &C) const {
  if (!MR)
    return;

  ProgramStateRef State = C.getState();
  State = State->remove<ShortReadDstMap>(MR);
  State = State->remove<SafeDstMap>(MR);

  if (const VarDecl *const *VD = State->get<DstToReadRetVarMap>(MR)) {
    State = State->remove<ReadRetVarToDstMap>(*VD);
    State = State->remove<DstToReadRetVarMap>(MR);
  }

  C.addTransition(State);
}

void SAGenTestChecker::markTrackedRegionSafe(const MemRegion *MR,
                                             CheckerContext &C) const {
  if (!MR)
    return;

  ProgramStateRef State = C.getState();
  if (!State->get<ShortReadDstMap>(MR))
    return;

  State = State->set<SafeDstMap>(MR, true);
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
  State = State->remove<SafeDstMap>(DstMR);

  if (const VarDecl *RetVD = getRetVarForAvioReadResult(Call, C)) {
    State = State->set<ReadRetVarToDstMap>(RetVD, DstMR);
    State = State->set<DstToReadRetVarMap>(DstMR, RetVD);
  }

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

  ProgramStateRef State = C.getState();
  for (const auto &Entry : State->get<ReadRetVarToDstMap>()) {
    const VarDecl *RetVD = Entry.first;
    const MemRegion *DstMR = Entry.second;
    if (!RetVD || !DstMR)
      continue;

    if (conditionMentionsTrackedRetVar(Condition, RetVD)) {
      clearTrackedRegion(DstMR, C);
      return;
    }
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
  ProgramStateRef State = C.getState();

  for (const auto &Entry : State->get<ShortReadDstMap>()) {
    const MemRegion *TrackedMR = Entry.first;
    if (!TrackedMR)
      continue;

    if (isSanitizingMemsetForTrackedDst(Call, TrackedMR, C) ||
        isSanitizingShrinkForTrackedDst(Call, TrackedMR, C)) {
      markTrackedRegionSafe(TrackedMR, C);
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
