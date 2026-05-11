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
REGISTER_MAP_WITH_PROGRAMSTATE(RetSymToDstMap, SymbolRef, const MemRegion *)

namespace {

class SAGenTestChecker
    : public Checker<check::PostCall,
                     check::BranchCondition,
                     check::PreCall,
                     check::Location,
                     check::Bind> {
  mutable std::unique_ptr<BugType> BT;

public:
  SAGenTestChecker()
      : BT(new BugType(this, "Unchecked avio_read result", "API")) {}

  void checkPostCall(const CallEvent &Call, CheckerContext &C) const;
  void checkBranchCondition(const Stmt *Condition, CheckerContext &C) const;
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
  void checkLocation(SVal Loc, bool IsLoad, const Stmt *S,
                     CheckerContext &C) const;
  void checkBind(SVal Loc, SVal Val, const Stmt *S, CheckerContext &C) const;

private:
  bool isAvioReadCall(const CallExpr *CE, CheckerContext &C) const;
  bool isAvioReadCall(const CallEvent &Call, CheckerContext &C) const;
  bool isMemcmpCall(const CallEvent &Call, CheckerContext &C) const;

  const MemRegion *getTrackedRegionFromExpr(const Expr *E,
                                            CheckerContext &C) const;
  const MemRegion *getTrackedRegionFromAvioReadCall(const CallExpr *CE,
                                                    CheckerContext &C) const;

  bool isSubRegionOf(const MemRegion *Inner, const MemRegion *Outer) const;
  bool regionsOverlapOrSubregion(const MemRegion *A, const MemRegion *B) const;
  bool isTrackedUse(const MemRegion *UseMR, const MemRegion *TrackedMR) const;
  bool isFalsePositive(const MemRegion *UseMR, const MemRegion *TrackedMR,
                       const Stmt *S, CheckerContext &C) const;

  const Expr *stripCasts(const Expr *E) const;
  const CallExpr *getTrackedAvioReadCallForSymbol(SymbolRef Sym,
                                                  ProgramStateRef State) const;
  const MemRegion *getAssignedRegionFromBindLoc(SVal Loc) const;
  SymbolRef getSymbolFromExpr(const Expr *E, CheckerContext &C) const;
  bool getRequestedSizeFromCall(const CallExpr *CE, llvm::APSInt &OutSize,
                                CheckerContext &C) const;

  bool conditionReferencesSymbol(const Expr *E, SymbolRef Sym,
                                 CheckerContext &C) const;
  bool isGuardThatEnsuresFullRead(const Expr *CondE, SymbolRef RetSym,
                                  const llvm::APSInt &RequestedSize,
                                  CheckerContext &C) const;
  bool isKnownSafeUseSite(const Stmt *S, CheckerContext &C) const;

  void clearTrackedRegion(const MemRegion *MR, CheckerContext &C) const;
  void clearTrackedRegionAndRetMappings(const MemRegion *MR,
                                        CheckerContext &C) const;
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

  if (isKnownSafeUseSite(S, C))
    return true;

  return false;
}

const Expr *SAGenTestChecker::stripCasts(const Expr *E) const {
  if (!E)
    return nullptr;
  return E->IgnoreParenImpCasts();
}

const CallExpr *
SAGenTestChecker::getTrackedAvioReadCallForSymbol(SymbolRef Sym,
                                                  ProgramStateRef State) const {
  if (!Sym || !State)
    return nullptr;

  const MemRegion *MR = State->get<RetSymToDstMap>(Sym);
  if (!MR)
    return nullptr;

  return State->get<ShortReadDstMap>(MR);
}

const MemRegion *SAGenTestChecker::getAssignedRegionFromBindLoc(SVal Loc) const {
  return Loc.getAsRegion();
}

SymbolRef SAGenTestChecker::getSymbolFromExpr(const Expr *E,
                                              CheckerContext &C) const {
  if (!E)
    return nullptr;

  SVal V = C.getState()->getSVal(E, C.getLocationContext());
  return V.getAsSymbol();
}

bool SAGenTestChecker::getRequestedSizeFromCall(const CallExpr *CE,
                                                llvm::APSInt &OutSize,
                                                CheckerContext &C) const {
  if (!CE || CE->getNumArgs() != 3)
    return false;
  return EvaluateExprToInt(OutSize, CE->getArg(2), C);
}

bool SAGenTestChecker::conditionReferencesSymbol(const Expr *E, SymbolRef Sym,
                                                 CheckerContext &C) const {
  if (!E || !Sym)
    return false;

  E = stripCasts(E);

  SymbolRef ESym = getSymbolFromExpr(E, C);
  if (ESym && ESym == Sym)
    return true;

  if (const auto *BO = dyn_cast<BinaryOperator>(E))
    return conditionReferencesSymbol(BO->getLHS(), Sym, C) ||
           conditionReferencesSymbol(BO->getRHS(), Sym, C);

  if (const auto *UO = dyn_cast<UnaryOperator>(E))
    return conditionReferencesSymbol(UO->getSubExpr(), Sym, C);

  if (const auto *CO = dyn_cast<ConditionalOperator>(E))
    return conditionReferencesSymbol(CO->getCond(), Sym, C) ||
           conditionReferencesSymbol(CO->getTrueExpr(), Sym, C) ||
           conditionReferencesSymbol(CO->getFalseExpr(), Sym, C);

  return false;
}

bool SAGenTestChecker::isGuardThatEnsuresFullRead(const Expr *CondE,
                                                  SymbolRef RetSym,
                                                  const llvm::APSInt &RequestedSize,
                                                  CheckerContext &C) const {
  if (!CondE || !RetSym)
    return false;

  CondE = cast<Expr>(stripCasts(CondE));

  const auto *BO = dyn_cast<BinaryOperator>(CondE);
  if (!BO || !BO->isComparisonOp())
    return false;

  const Expr *LHS = stripCasts(BO->getLHS());
  const Expr *RHS = stripCasts(BO->getRHS());

  SymbolRef LHSSym = getSymbolFromExpr(LHS, C);
  SymbolRef RHSSym = getSymbolFromExpr(RHS, C);

  llvm::APSInt CmpConst;
  bool HasConst = false;
  bool SymOnLHS = false;

  if (LHSSym == RetSym) {
    HasConst = EvaluateExprToInt(CmpConst, RHS, C);
    SymOnLHS = true;
  } else if (RHSSym == RetSym) {
    HasConst = EvaluateExprToInt(CmpConst, LHS, C);
    SymOnLHS = false;
  } else {
    return false;
  }

  if (!HasConst)
    return false;

  BinaryOperatorKind Op = BO->getOpcode();

  // We want to recognize short-read guards of the form:
  //   ret < N
  //   ret <= N-1
  //   ret != N
  // and mirrored forms:
  //   N > ret
  //   N-1 >= ret
  //   N != ret
  //
  // This checker is path-insensitive at branch callback granularity, so we
  // conservatively treat the existence of such a guard as sufficient to clear
  // the pending-warning state for later code. This fixes the reported FP while
  // still preserving the target true positives.
  if (SymOnLHS) {
    switch (Op) {
    case BO_LT:
      return CmpConst == RequestedSize;
    case BO_LE: {
      llvm::APSInt PlusOne = CmpConst;
      ++PlusOne;
      return PlusOne == RequestedSize;
    }
    case BO_NE:
      return CmpConst == RequestedSize;
    default:
      return false;
    }
  }

  switch (Op) {
  case BO_GT:
    return CmpConst == RequestedSize;
  case BO_GE: {
    llvm::APSInt PlusOne = CmpConst;
    ++PlusOne;
    return PlusOne == RequestedSize;
  }
  case BO_NE:
    return CmpConst == RequestedSize;
  default:
    return false;
  }
}

bool SAGenTestChecker::isKnownSafeUseSite(const Stmt *S,
                                          CheckerContext &C) const {
  if (!S)
    return false;

  // If the current statement is lexically under a branch condition that checks
  // the return of avio_read directly, treat it as safe. This is a lightweight
  // extra suppression for direct forms like:
  //   if (avio_read(pb, buf, 8) < 8) return err;
  //   memcmp(buf, ...);
  //
  // We keep this conservative and syntax-based.
  if (const auto *IfS = findSpecificTypeInParents<IfStmt>(S, C)) {
    const Expr *Cond = IfS->getCond();
    if (!Cond)
      return false;

    const auto *BO = dyn_cast<BinaryOperator>(stripCasts(Cond));
    if (!BO || !BO->isComparisonOp())
      return false;

    const Expr *LHS = stripCasts(BO->getLHS());
    const Expr *RHS = stripCasts(BO->getRHS());

    const auto *LCall = dyn_cast<CallExpr>(LHS);
    const auto *RCall = dyn_cast<CallExpr>(RHS);

    llvm::APSInt C;
    if (LCall && isAvioReadCall(LCall, C) && EvaluateExprToInt(C, RHS, C))
      return true;
    if (RCall && isAvioReadCall(RCall, C) && EvaluateExprToInt(C, LHS, C))
      return true;
  }

  return false;
}

void SAGenTestChecker::clearTrackedRegion(const MemRegion *MR,
                                          CheckerContext &C) const {
  if (!MR)
    return;

  ProgramStateRef State = C.getState();
  if (!State->get<ShortReadDstMap>(MR))
    return;

  State = State->remove<ShortReadDstMap>(MR);
  C.addTransition(State);
}

void SAGenTestChecker::clearTrackedRegionAndRetMappings(const MemRegion *MR,
                                                        CheckerContext &C) const {
  if (!MR)
    return;

  ProgramStateRef State = C.getState();
  bool Changed = false;

  if (State->get<ShortReadDstMap>(MR)) {
    State = State->remove<ShortReadDstMap>(MR);
    Changed = true;
  }

  SmallVector<SymbolRef, 8> ToRemove;
  for (const auto &Entry : State->get<RetSymToDstMap>()) {
    if (Entry.second == MR)
      ToRemove.push_back(Entry.first);
  }

  for (SymbolRef Sym : ToRemove) {
    State = State->remove<RetSymToDstMap>(Sym);
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

  // Also record direct symbolic result if available; assignment-form bindings
  // are handled in checkBind().
  if (SymbolRef RetSym = Call.getReturnValue().getAsSymbol())
    State = State->set<RetSymToDstMap>(RetSym, DstMR);

  C.addTransition(State);
}

void SAGenTestChecker::checkBind(SVal Loc, SVal Val, const Stmt *S,
                                 CheckerContext &C) const {
  if (!S)
    return;

  ProgramStateRef State = C.getState();

  // Track assignments like:
  //   ret = avio_read(pb, buf, n);
  //
  // The CallEvent return symbol may flow into the bound variable here; we map
  // that symbol back to the tracked destination region.
  SymbolRef RHSym = Val.getAsSymbol();
  const MemRegion *AssignedMR = getAssignedRegionFromBindLoc(Loc);
  if (!AssignedMR || !RHSym)
    return;

  // If this symbol is already known as avio_read's return, no update needed.
  if (State->get<RetSymToDstMap>(RHSym))
    return;

  // Try to discover whether the source statement contains an avio_read call and
  // tie its destination region to the symbol being bound.
  const auto *BO = dyn_cast<BinaryOperator>(S);
  if (BO && BO->isAssignmentOp()) {
    const Expr *RHS = stripCasts(BO->getRHS());
    if (const auto *CE = dyn_cast<CallExpr>(RHS)) {
      if (isAvioReadCall(CE, C)) {
        const MemRegion *DstMR = getTrackedRegionFromAvioReadCall(CE, C);
        if (DstMR) {
          State = State->set<RetSymToDstMap>(RHSym, DstMR);
          C.addTransition(State);
        }
        return;
      }
    }
  }

  const auto *DS = dyn_cast<DeclStmt>(S);
  if (!DS)
    return;

  for (const Decl *D : DS->decls()) {
    const auto *VD = dyn_cast<VarDecl>(D);
    if (!VD || !VD->hasInit())
      continue;

    const Expr *Init = stripCasts(VD->getInit());
    const auto *CE = dyn_cast<CallExpr>(Init);
    if (!CE || !isAvioReadCall(CE, C))
      continue;

    const MemRegion *DstMR = getTrackedRegionFromAvioReadCall(CE, C);
    if (!DstMR)
      continue;

    State = State->set<RetSymToDstMap>(RHSym, DstMR);
    C.addTransition(State);
    return;
  }
}

void SAGenTestChecker::checkBranchCondition(const Stmt *Condition,
                                            CheckerContext &C) const {
  if (!Condition)
    return;

  ProgramStateRef State = C.getState();

  // Direct form:
  //   if (avio_read(pb, buf, N) < N) return ...;
  const Expr *CondE = dyn_cast<Expr>(Condition);
  if (!CondE)
    return;

  CondE = stripCasts(CondE);

  if (const auto *BO = dyn_cast<BinaryOperator>(CondE)) {
    const Expr *LHS = stripCasts(BO->getLHS());
    const Expr *RHS = stripCasts(BO->getRHS());

    auto TryDirectCallGuard = [&](const Expr *CallSide, const Expr *OtherSide) {
      const auto *CE = dyn_cast<CallExpr>(CallSide);
      if (!CE || !isAvioReadCall(CE, C))
        return false;

      llvm::APSInt RequestedSize;
      llvm::APSInt ComparedConst;
      if (!getRequestedSizeFromCall(CE, RequestedSize, C))
        return false;
      if (!EvaluateExprToInt(ComparedConst, OtherSide, C))
        return false;
      if (ComparedConst != RequestedSize)
        return false;

      const MemRegion *DstMR = getTrackedRegionFromAvioReadCall(CE, C);
      if (!DstMR)
        return false;

      clearTrackedRegionAndRetMappings(DstMR, C);
      return true;
    };

    if (TryDirectCallGuard(LHS, RHS))
      return;
    if (TryDirectCallGuard(RHS, LHS))
      return;
  }

  // Variable form:
  //   ret = avio_read(pb, buf, N);
  //   if (ret < N) return ...;
  //
  // We inspect tracked return symbols and clear only when the condition is a
  // recognized short-read guard for the same requested size.
  for (const auto &Entry : State->get<RetSymToDstMap>()) {
    SymbolRef RetSym = Entry.first;
    const MemRegion *DstMR = Entry.second;
    if (!RetSym || !DstMR)
      continue;

    if (!conditionReferencesSymbol(CondE, RetSym, C))
      continue;

    const CallExpr *TrackedCall = State->get<ShortReadDstMap>(DstMR);
    if (!TrackedCall)
      continue;

    llvm::APSInt RequestedSize;
    if (!getRequestedSizeFromCall(TrackedCall, RequestedSize, C))
      continue;

    if (isGuardThatEnsuresFullRead(cast<Expr>(CondE), RetSym, RequestedSize, C)) {
      clearTrackedRegionAndRetMappings(DstMR, C);
      return;
    }
  }
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
