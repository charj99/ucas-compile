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
       V2VSetMap &funcPtrMap, Value *dst, Value *src, bool strongUpdate) {
    V2VSetMap::iterator it = funcPtrMap.find(src);
    bool changed = false;

    // if b->{}, a->b or a->S(a) \cup b
    if (it == funcPtrMap.end()) {
        // if b->{}, a->b
        if (strongUpdate || !CONTAINS(funcPtrMap, dst)) {
            ValueSet srcPointsTo = ValueSet({src});
            if (funcPtrMap[dst] != srcPointsTo) {
                funcPtrMap[dst] = srcPointsTo;
                changed = true;
            }
        }
        // if b->{}, a->S(a) \cup b
        else changed |= funcPtrMap[dst].insert(src).second;
    }
    // if b->S(b), a->S(b) or a->S(a) \cup S(b)
    else {
        // if b->S(b), a->S(b)
        if (strongUpdate || !CONTAINS(funcPtrMap, dst)) {
            if (funcPtrMap[dst] != it->second) {
                funcPtrMap[dst] = it->second;
                changed = true;
            }
        }
        // if b->S(b), a->S(a) \cup S(b)
        else {
            for (auto srcPointsTo : it->second)
                changed |= funcPtrMap[dst].insert(srcPointsTo).second;
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
            if (!CONTAINS(funcPtrMap, u)) assert(0 && "u is not in funcPtrMap"); // continue;
            const ValueSet& valueSet = funcPtrMap[u];
            for (auto v : valueSet)
                worklist.insert(v);
        }
    }
}

// TODO:
void FuncPtrVisitor::compDFVal(Instruction *inst, FuncPtrInfo *dfval, FuncSet *funcWorkList) {
    /*
     * a = *b,
     * if b->{}, a->b
     * if b->S(b), a->S(b)
     */
    if (LoadInst* LI = dyn_cast<LoadInst>(inst)) {
        Value* src = LI->getPointerOperand();
        updateDstPointsToWithSrcPointsTo(dfval->FuncPtrs, LI, src);
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
                if (updateDstPointsToWithSrcPointsTo(dfval->FuncPtrs, param, arg, false))
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
            if (updateDstPointsToWithSrcPointsTo(dfval->FuncPtrs, src, callSite, false))
                funcWorkList->insert(callSite->getParent()->getParent());
        }
    }
}

inline raw_ostream& operator<<(raw_ostream& out, const FuncPtrInfo& info) {
    for (auto item : info.FuncPtrs) {
        Value* key = item.first;
        ValueSet& value = item.second;
        out << key->getName() << ": {";
        for (auto v : value)
            out << v->getName() << ", ";
        out << "}, ";
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
    while (!workList.empty()) {
        Function* F = *workList.begin();
        workList.erase(F);

        compForwardDataflowInter(F, &visitor, &result, initval, &workList);
    }

    if (DEBUG) printDataflowResult<FuncPtrInfo>(errs(), result);
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