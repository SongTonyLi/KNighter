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
REGISTER_MAP_WITH_PROGRAMSTATE(AvioReadRetVarMap, const VarDecl *, const MemRegion *)
REGISTER_SET_WITH_PROGRAMSTATE(CheckedRetVars, const VarDecl *)
REGISTER_SET_WITH_PROGRAMSTATE(StreamHandledRegions, const MemRegion *)

namespace {

class SAGenTestChecker
    : public Checker<check::PostCall,
                     check::PostStmt<DeclStmt>,
                     check::PreStmt<BinaryOperator>,
                     check::BranchCondition,
                     check::PreCall,
                     check::Location> {
  mutable std::unique_ptr<BugType> BT;

public:
  SAGenTestChecker()
      : BT(new BugType(this, "Unchecked avio_read result", "API")) {}

  void checkPostCall(const CallEvent &Call, CheckerContext &C) const;
  void checkPostStmt(const DeclStmt *DS, CheckerContext &C) const;
  void checkPreStmt(const BinaryOperator *BO, CheckerContext &C) const;
  void checkBranchCondition(const Stmt *Condition, CheckerContext &C) const;
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
  void checkLocation(SVal Loc, bool IsLoad, const Stmt *S,
                     CheckerContext &C) const;

private:
  bool isAvioReadCall(const CallExpr *CE, CheckerContext &C) const;
  bool isAvioReadCall(const CallEvent &Call, CheckerContext &C) const;
  bool isMemcmpCall(const CallEvent &Call, CheckerContext &C) const;

  const MemRegion *getTrackedBaseRegionFromExpr(const Expr *E,
                                                CheckerContext &C) const;
  const MemRegion *getTrackedBaseRegionFromAvioReadCall(const CallExpr *CE,
                                                        CheckerContext &C) const;

  const VarDecl *getVarDeclFromExpr(const Expr *E) const;
  const CallExpr *getDirectAvioReadCall(const Expr *E,
                                        CheckerContext &C) const;
  const CallExpr *getAvioReadCallFromRetVar(const Expr *CondE,
                                            CheckerContext &C) const;

  bool isExprRefToVar(const Expr *E, const VarDecl *VD) const;
  bool isAccumulationWithVar(const BinaryOperator *BO, const VarDecl *VD) const;
  bool isHandledStreamingRegion(const MemRegion *MR, ProgramStateRef State) const;
  bool isFalsePositive(const MemRegion *MR, const Stmt *S,
                       CheckerContext &C) const;

  void markRetVarForCallResult(const VarDecl *VD, const MemRegion *MR,
                               CheckerContext &C) const;
  void markCheckedRetVar(const VarDecl *VD, CheckerContext &C) const;
  void markStreamHandledRegion(const MemRegion *MR, CheckerContext &C) const;

  void clearTrackedRegion(const MemRegion *MR, CheckerContext &C) const;
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

const MemRegion *
SAGenTestChecker::getTrackedBaseRegionFromExpr(const Expr *E,
                                               CheckerContext &C) const {
  if (!E)
    return nullptr;

  const MemRegion *MR = getMemRegionFromExpr(E, C);
  if (!MR)
    return nullptr;

  MR = MR->getBaseRegion();
  if (!MR)
    return nullptr;

  return MR;
}

const MemRegion *
SAGenTestChecker::getTrackedBaseRegionFromAvioReadCall(const CallExpr *CE,
                                                       CheckerContext &C) const {
  if (!CE)
    return nullptr;
  if (!isAvioReadCall(CE, C))
    return nullptr;
  if (CE->getNumArgs() != 3)
    return nullptr;

  return getTrackedBaseRegionFromExpr(CE->getArg(1), C);
}

const VarDecl *SAGenTestChecker::getVarDeclFromExpr(const Expr *E) const {
  if (!E)
    return nullptr;

  E = E->IgnoreParenImpCasts();
  if (const auto *DRE = dyn_cast<DeclRefExpr>(E))
    return dyn_cast<VarDecl>(DRE->getDecl());

  return nullptr;
}

const CallExpr *SAGenTestChecker::getDirectAvioReadCall(const Expr *E,
                                                        CheckerContext &C) const {
  if (!E)
    return nullptr;

  E = E->IgnoreParenImpCasts();
  if (const auto *CE = dyn_cast<CallExpr>(E)) {
    if (isAvioReadCall(CE, C))
      return CE;
  }

  if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
    if (BO->isAssignmentOp()) {
      const Expr *RHS = BO->getRHS();
      RHS = RHS ? RHS->IgnoreParenImpCasts() : nullptr;
      if (const auto *CE = dyn_cast_or_null<CallExpr>(RHS)) {
        if (isAvioReadCall(CE, C))
          return CE;
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

  return nullptr;
}

bool SAGenTestChecker::isExprRefToVar(const Expr *E, const VarDecl *VD) const {
  if (!E || !VD)
    return false;

  E = E->IgnoreParenImpCasts();
  if (const auto *DRE = dyn_cast<DeclRefExpr>(E))
    return DRE->getDecl() == VD;

  return false;
}

bool SAGenTestChecker::isAccumulationWithVar(const BinaryOperator *BO,
                                             const VarDecl *VD) const {
  if (!BO || !VD)
    return false;

  if (BO->getOpcode() == BO_AddAssign)
    return isExprRefToVar(BO->getRHS(), VD);

  if (!BO->isAssignmentOp())
    return false;

  const Expr *RHS = BO->getRHS();
  if (!RHS)
    return false;

  RHS = RHS->IgnoreParenImpCasts();
  if (const auto *InnerBO = dyn_cast<BinaryOperator>(RHS)) {
    if (InnerBO->getOpcode() == BO_Add) {
      return isExprRefToVar(InnerBO->getLHS(), VD) ||
             isExprRefToVar(InnerBO->getRHS(), VD);
    }
  }

  return false;
}

bool SAGenTestChecker::isHandledStreamingRegion(const MemRegion *MR,
                                                ProgramStateRef State) const {
  if (!MR || !State)
    return false;
  return State->contains<StreamHandledRegions>(MR->getBaseRegion());
}

bool SAGenTestChecker::isFalsePositive(const MemRegion *MR, const Stmt *S,
                                       CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  if (!MR || !State)
    return false;

  MR = MR->getBaseRegion();
  if (!MR)
    return false;

  if (isHandledStreamingRegion(MR, State))
    return true;

  return false;
}

void SAGenTestChecker::markRetVarForCallResult(const VarDecl *VD,
                                               const MemRegion *MR,
                                               CheckerContext &C) const {
  if (!VD || !MR)
    return;

  ProgramStateRef State = C.getState();
  State = State->set<AvioReadRetVarMap>(VD, MR->getBaseRegion());
  C.addTransition(State);
}

void SAGenTestChecker::markCheckedRetVar(const VarDecl *VD,
                                         CheckerContext &C) const {
  if (!VD)
    return;

  ProgramStateRef State = C.getState();
  State = State->add<CheckedRetVars>(VD);
  C.addTransition(State);
}

void SAGenTestChecker::markStreamHandledRegion(const MemRegion *MR,
                                               CheckerContext &C) const {
  if (!MR)
    return;

  ProgramStateRef State = C.getState();
  State = State->add<StreamHandledRegions>(MR->getBaseRegion());
  C.addTransition(State);
}

void SAGenTestChecker::clearTrackedRegion(const MemRegion *MR,
                                          CheckerContext &C) const {
  if (!MR)
    return;

  ProgramStateRef State = C.getState();
  const MemRegion *Base = MR->getBaseRegion();
  if (!Base)
    return;

  if (!State->get<ShortReadDstMap>(Base))
    return;

  State = State->remove<ShortReadDstMap>(Base);
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

  const MemRegion *DstMR = getTrackedBaseRegionFromExpr(CE->getArg(1), C);
  if (!DstMR)
    return;

  ProgramStateRef State = C.getState();
  State = State->set<ShortReadDstMap>(DstMR, CE);
  C.addTransition(State);
}

void SAGenTestChecker::checkPostStmt(const DeclStmt *DS,
                                     CheckerContext &C) const {
  if (!DS)
    return;

  for (const Decl *D : DS->decls()) {
    const auto *VD = dyn_cast<VarDecl>(D);
    if (!VD || !VD->hasInit())
      continue;

    const Expr *Init = VD->getInit();
    const CallExpr *CE = getDirectAvioReadCall(Init, C);
    if (!CE)
      continue;

    const MemRegion *DstMR = getTrackedBaseRegionFromAvioReadCall(CE, C);
    if (!DstMR)
      continue;

    markRetVarForCallResult(VD, DstMR, C);
  }
}

void SAGenTestChecker::checkPreStmt(const BinaryOperator *BO,
                                    CheckerContext &C) const {
  if (!BO)
    return;

  ProgramStateRef State = C.getState();

  if (BO->isAssignmentOp()) {
    const VarDecl *LHSVD = getVarDeclFromExpr(BO->getLHS());
    const Expr *RHS = BO->getRHS();
    if (LHSVD && RHS) {
      const CallExpr *CE = getDirectAvioReadCall(RHS, C);
      if (CE) {
        const MemRegion *DstMR = getTrackedBaseRegionFromAvioReadCall(CE, C);
        if (DstMR)
          markRetVarForCallResult(LHSVD, DstMR, C);
        return;
      }
    }
  }

  for (auto I = State->get<AvioReadRetVarMap>().begin(),
            E = State->get<AvioReadRetVarMap>().end();
       I != E; ++I) {
    const VarDecl *RetVD = I->first;
    const MemRegion *DstMR = I->second;
    if (!RetVD || !DstMR)
      continue;

    if (!State->contains<CheckedRetVars>(RetVD))
      continue;

    if (isAccumulationWithVar(BO, RetVD)) {
      markStreamHandledRegion(DstMR, C);
      return;
    }
  }
}

void SAGenTestChecker::checkBranchCondition(const Stmt *Condition,
                                            CheckerContext &C) const {
  if (!Condition)
    return;

  const CallExpr *DirectCall = findSpecificTypeInChildren<CallExpr>(Condition);
  if (DirectCall && isAvioReadCall(DirectCall, C)) {
    const MemRegion *DstMR = getTrackedBaseRegionFromAvioReadCall(DirectCall, C);
    if (DstMR)
      clearTrackedRegion(DstMR, C);
    return;
  }

  const Expr *CondE = dyn_cast<Expr>(Condition);
  if (!CondE)
    return;

  ProgramStateRef State = C.getState();

  const DeclRefExpr *DRE = findSpecificTypeInChildren<DeclRefExpr>(CondE);
  if (DRE) {
    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
      if (State->get<AvioReadRetVarMap>(VD))
        markCheckedRetVar(VD, C);
    }
  }

  const CallExpr *TrackedCall = getAvioReadCallFromRetVar(CondE, C);
  if (!TrackedCall)
    return;

  const MemRegion *DstMR = getTrackedBaseRegionFromAvioReadCall(TrackedCall, C);
  if (!DstMR)
    return;

  clearTrackedRegion(DstMR, C);
}

void SAGenTestChecker::checkPreCall(const CallEvent &Call,
                                    CheckerContext &C) const {
  if (!isMemcmpCall(Call, C))
    return;

  if (Call.getNumArgs() < 2)
    return;

  ProgramStateRef State = C.getState();

  for (unsigned Idx = 0; Idx < 2; ++Idx) {
    const Expr *ArgE = Call.getArgExpr(Idx);
    if (!ArgE)
      continue;

    const MemRegion *MR = getTrackedBaseRegionFromExpr(ArgE, C);
    if (!MR)
      continue;

    const CallExpr *const *TrackedBy = State->get<ShortReadDstMap>(MR);
    if (!TrackedBy)
      continue;

    if (isFalsePositive(MR, Call.getOriginExpr(), C))
      continue;

    reportBug(MR, Call.getOriginExpr(),
              C, "buffer read by avio_read may be partially uninitialized");
    return;
  }
}

void SAGenTestChecker::checkLocation(SVal Loc, bool IsLoad, const Stmt *S,
                                     CheckerContext &C) const {
  ProgramStateRef State = C.getState();

  const MemRegion *MR = Loc.getAsRegion();
  if (!MR)
    return;

  MR = MR->getBaseRegion();
  if (!MR)
    return;

  const CallExpr *const *TrackedBy = State->get<ShortReadDstMap>(MR);
  if (!TrackedBy)
    return;

  if (isFalsePositive(MR, S, C))
    return;

  if (IsLoad) {
    reportBug(MR, S, C,
              "buffer read by avio_read may be partially uninitialized");
    return;
  }

  if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(S)) {
    if (!ASE->getBase())
      return;

    const MemRegion *BaseMR = getTrackedBaseRegionFromExpr(ASE->getBase(), C);
    if (!BaseMR)
      return;

    if (State->get<ShortReadDstMap>(BaseMR)) {
      if (isFalsePositive(BaseMR, S, C))
        return;

      reportBug(BaseMR, S, C,
                "buffer read by avio_read may be partially uninitialized");
      return;
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
