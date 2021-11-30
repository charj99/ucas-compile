//
// Created by weekends on 11/29/21.
//
#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include "utils.h"
#include "FuncPtr.h"
#include "Dataflow.h"
using namespace llvm;

void FuncPtrVisitor::merge(FuncPtrInfo *dest, const FuncPtrInfo &src) {
    for (auto item : src.FuncPtrs) {
        Value* key = item.first;
        ValueSet& value = item.second;
        V2VSetMap::iterator it = dest->FuncPtrs.find(key);
        if (it == dest->FuncPtrs.end()) {
            dest->FuncPtrs[key] = value;
        } else {
            it->second.insert(value.begin(), value.end());
        }
    }
}

/*
 * if src->{}, dst->src
 * if src->S(src), dst->S(src)
 */
bool FuncPtrVisitor::updateDstPointsToWithSrcPointsTo(
        V2VSetMap &dstFuncPtrMap, V2VSetMap& srcFuncPtrMap,
        Value *dst, Value *src, bool strongUpdate) {
    V2VSetMap::iterator it = srcFuncPtrMap.find(src);
    bool changed = false;

    // if b->{}, a->b or a->S(a) \cup b
    if (it == srcFuncPtrMap.end()) {
        // if b->{}, a->b
        if (strongUpdate || !CONTAINS(srcFuncPtrMap, dst)) {
            ValueSet srcPointsTo = ValueSet({src});
            if (dstFuncPtrMap[dst] != srcPointsTo) {
                dstFuncPtrMap[dst] = srcPointsTo;
                changed = true;
            }
        }
        // if b->{}, a->S(a) \cup b
        else changed |= dstFuncPtrMap[dst].insert(src).second;
    }
    // if b->S(b), a->S(b) or a->S(a) \cup S(b)
    else {
        // if b->S(b), a->S(b)
        if (strongUpdate || !CONTAINS(srcFuncPtrMap, dst)) {
            if (dstFuncPtrMap[dst] != it->second) {
                dstFuncPtrMap[dst] = it->second;
                changed = true;
            }
        }
        // if b->S(b), a->S(a) \cup S(b)
        else {
            for (auto srcPointsTo : it->second)
                changed |= dstFuncPtrMap[dst].insert(srcPointsTo).second;
        }
    }
    return changed;
}

void FuncPtrVisitor::linkCallSiteAndCallee(CallInst* CI, Function* callee) {
    CalleeMap[CI].insert(callee);
    CallerMap[callee].insert(CI);
}

// TODO:
void FuncPtrVisitor::getCallees(V2VSetMap& funcPtrMap, CallInst* CI) {
    if (!CI->isIndirectCall()) {
        Function* callee = CI->getCalledFunction();
        if (!callee->isIntrinsic())
            linkCallSiteAndCallee(CI, callee);
        return;
    }
    ValueSet worklist;
    worklist.insert(CI->getCalledValue());

    while (!worklist.empty()) {
        Value* u = *worklist.begin();
        worklist.erase(u);
        if (Function* F = dyn_cast<Function>(u))
            linkCallSiteAndCallee(CI, F);
        else {
            if (!CONTAINS(funcPtrMap, u)) continue;
            const ValueSet& valueSet = funcPtrMap[u];
            for (auto v : valueSet)
                worklist.insert(v);
        }
    }
}

// TODO:
void FuncPtrVisitor::compDFVal(Instruction *inst, FuncPtrInfo *dfval,
                               FuncSet *funcWorkList,
                               DataflowResult<FuncPtrInfo>::Type* result) {
    /*
     * a = *b,
     * if b->{}, a->b
     * if b->S(b), a->S(b)
     */
    if (LoadInst* LI = dyn_cast<LoadInst>(inst)) {
        Value* src = LI->getPointerOperand();
        updateDstPointsToWithSrcPointsTo(dfval->FuncPtrs, dfval->FuncPtrs, LI, src);
    }

    /*
     * *a = b,
     * if a->{}, do nothing
     * if a->{x}, x->S(b),
     * if a->S(a), a_i->S(a_i) \cup S(b), for a_i \in S(a)
     */
    else if (StoreInst* SI = dyn_cast<StoreInst>(inst)) {
        Value* src = SI->getValueOperand();
        Value* dst = SI->getPointerOperand();
        // a->b
        dfval->FuncPtrs[dst] = ValueSet({src});
        /*
        V2VSetMap::iterator it = dfval->FuncPtrs.find(dst);
        // if a->{}, do nothing
        if (it == dfval->FuncPtrs.end()) return;

        // if a->{x}, x->S(b),
        if (it->second.size() == 1) {
            Value* dstPointsTo = *it->second.begin();
            updateDstPointsToWithSrcPointsTo(dfval->FuncPtrs, dstPointsTo, src);
        }
        // if a->S(a), a_i->S(a_i) \cup S(b), for a_i \in S(a)
        else {
            for (auto dstPointsTo : it->second)
                updateDstPointsToWithSrcPointsTo(dfval->FuncPtrs, dstPointsTo, src, false);
        }
        */
    }

    /*
     * callee: {f_i(p_0, ..., p_n)}
     * args: a_0, ..., a_n
     * p_0->S(p_0) \cup S(a_0)
     * add callee to function worklist if data flow changes
     */
    else if (CallInst* CI = dyn_cast<CallInst>(inst)) {
        getCallees(dfval->FuncPtrs, CI);
        const FuncSet& callees = CalleeMap[CI];
        int argNum = CI->getNumArgOperands();
        for (auto F : callees) {
            for (int i = 0; i < argNum; i++) {
                Value* arg = CI->getArgOperand(i);
                Value* param = F->getArg(i);
                if (updateDstPointsToWithSrcPointsTo(
                        (*result)[&F->getEntryBlock()].first.FuncPtrs,
                        dfval->FuncPtrs,
                        param, arg, false))
                    funcWorkList->insert(F);
            }
        }
    }

   /*
    * return retval_f
    * call-site of f: {cs_i}
    * cs_i->S(cs_i) \cup retval_f
     * add caller to function worklist if data flow changes
    */
    else if (ReturnInst* RI = dyn_cast<ReturnInst>(inst)) {
        const CallSet& callers = CallerMap[RI->getParent()->getParent()];
        Value* src = RI->getReturnValue();
        for (auto callSite : callers) {
            Function* F = callSite->getParent()->getParent();
            if (updateDstPointsToWithSrcPointsTo(
                    (*result)[&F->getEntryBlock()].first.FuncPtrs,
                    dfval->FuncPtrs,
                    callSite, src, false))
                funcWorkList->insert(F);
        }
    }

    /*
    else if (AllocaInst* AI = dyn_cast<AllocaInst>(inst)) {
        dfval->FuncPtrs[AI] = ValueSet({AI});
    }
    */
}

inline raw_ostream& operator<<(raw_ostream& out, const FuncPtrInfo& info) {
    for (auto item : info.FuncPtrs) {
        Value* key = item.first;
        ValueSet& value = item.second;
        key->dump();
        out << ": {\n";
        for (auto v : value) {
            out << "\t";
            v->dump();
            out << ",\n";
        }
        out << "},\n";
    }
    return out;
}

bool FuncPtrPass::runOnModule(Module& M) {
    FuncSet workList;
    for (Module::iterator i = M.begin(), e = M.end(); i != e; ++i) {
        Function* F = &*i;
        workList.insert(F);
    }

    FuncPtrVisitor visitor;
    DataflowResult<FuncPtrInfo>::Type result;
    FuncPtrInfo initval;

    int times = 0;
    while (!workList.empty()) {
        Function* F = *workList.begin();
        workList.erase(F);

        compForwardDataflowInter(F, &visitor, &result, initval, &workList);
        Diag << ++times << ": " << F->getName() << "------------------------------\n";
        if (DEBUG) printDataflowResult<FuncPtrInfo>(errs(), result);
        Diag << "------------------------------\n";
    }

    dumpCallees(visitor.getCalleeMap());
    return false;
}

// TODO:
void FuncPtrPass::dumpCallees(const Call2FuncSetMap& CalleeMap) {
    Int2FuncSetMap result;
    for (auto item : CalleeMap) {
        CallInst* CI = item.first;
        FuncSet& funcSet = item.second;
        int lineNo = CI->getDebugLoc().getLine();
        for (auto F : funcSet) {
            result[lineNo].insert(F);
        }
    }

    for (auto item : result) {
        errs() << item.first << ": ";
        FuncSet& funcSet = item.second;
        for (auto F : funcSet) {
            if (F != *funcSet.begin()) errs() << ", ";
            errs() << F->getName();
        }
        errs() << "\n";
    }
}