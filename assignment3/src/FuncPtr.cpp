//
// Created by weekends on 11/29/21.
//
#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/IntrinsicInst.h>
#include "utils.h"
#include "FuncPtr.h"
#include "Dataflow.h"
using namespace llvm;

inline raw_ostream& operator<<(raw_ostream& out, const FuncPtrInfo& info) {
    for (auto item : info.FuncPtrs) {
        Value* key = item.first;
        ValueSet& value = item.second;
        if (key->hasName()) out << key->getName();
        else key->dump();
        out << ": {\n";
        for (auto v : value) {
            out << "\t";
            if (v->hasName()) out << v->getName();
            else v->dump();
            out << ",\n";
        }
        out << "},\n";
    }
    return out;
}

bool FuncPtrVisitor::merge(FuncPtrInfo *dest, const FuncPtrInfo &src) {
    bool changed = false;
    for (auto item : src.FuncPtrs) {
        Value* key = item.first;
        ValueSet& value = item.second;
        V2VSetMap::iterator it = dest->FuncPtrs.find(key);
        if (it == dest->FuncPtrs.end()) {
            dest->FuncPtrs[key] = value;
            changed = true;
        } else {
            for (auto v : value)
                changed |= it->second.insert(v).second;
        }
    }
    return changed;
}

/*
 * strong update: if src->S(src), dst->S(src)
 * not strong update: if src->S(src), dst->S(dst) \cup S(src)
 */
bool FuncPtrVisitor::updateDstPointsToWithSrcPointsTo(
        V2VSetMap &dstFuncPtrMap, V2VSetMap& srcFuncPtrMap,
        Value *dst, Value *src, bool strongUpdate) {
    V2VSetMap::iterator it = srcFuncPtrMap.find(src);
    bool changed = false;
    // if src->{}, dst->src
    if (it == srcFuncPtrMap.end()) {
        return changed;
        /*
        if (strongUpdate || !CONTAINS(dstFuncPtrMap, dst)) {
            ValueSet srcPointsTo = ValueSet({src});
            if (dstFuncPtrMap[dst] != srcPointsTo) {
                dstFuncPtrMap[dst] = srcPointsTo;
                changed = true;
            }
        }
        else changed |= dstFuncPtrMap[dst].insert(src).second;
        return changed;
        */
    }

    // strong update: if src->S(src), dst->S(src)
    if (strongUpdate || !CONTAINS(dstFuncPtrMap, dst)) {
        if (dstFuncPtrMap[dst] != it->second) {
            dstFuncPtrMap[dst] = it->second;
            changed = true;
        }
    }
    // not strong update: if src->S(src), dst->S(dst) \cup S(src)
    else {
        for (auto srcPointsTo : it->second)
            changed |= dstFuncPtrMap[dst].insert(srcPointsTo).second;
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
    ValueSet visited;
    worklist.insert(CI->getCalledValue());

    while (!worklist.empty()) {
        Value* u = *worklist.begin();
        worklist.erase(u);
        visited.insert(u);

        if (Function* F = dyn_cast<Function>(u))
            linkCallSiteAndCallee(CI, F);
        else {
            if (!CONTAINS(funcPtrMap, u)) continue;
            const ValueSet& valueSet = funcPtrMap[u];
            for (auto v : valueSet)
                if (!CONTAINS(visited, v))
                    worklist.insert(v);
        }
    }
}

Value* FuncPtrVisitor::mapAllocSite(Value* v, FuncPtrInfo* dfval) {
    Value* allocSite = NULL;
    if (CONTAINS(AllocMap, v))
        allocSite = AllocMap[v];
    else {
        allocSite = ConstantInt::get(IntegerType::getInt32Ty(v->getContext()), allocCount++);
        AllocMap[v] = allocSite;
    }
    dfval->FuncPtrs[v] = ValueSet({allocSite});
    return allocSite;
}

void FuncPtrVisitor::clearDFVal(
        Function* F, DataflowResult<FuncPtrInfo>::Type* result, const FuncPtrInfo& initval) {
    for (Function::iterator bi = F->begin(); bi != F->end(); ++bi) {
        BasicBlock * bb = &*bi;
        (*result)[bb] = std::make_pair(initval, initval);
    }
}
// TODO:
bool FuncPtrVisitor::compDFVal(Instruction *inst, FuncPtrInfo *dfval,
                               DataflowVisitor<FuncPtrInfo>* visitor,
                               DataflowResult<FuncPtrInfo>::Type* result,
                               const FuncPtrInfo& initval,
                               FuncSet* funcWorkList) {
    Diag << "################## pointer before #################\n";
    if (DEBUG) inst->dump();
    Diag << *dfval;
    /*
     * a = *b,
     * if b->S(b), a->S(S(b))
     */
    if (LoadInst* LI = dyn_cast<LoadInst>(inst)) {
        Value* src = LI->getPointerOperand();
        V2VSetMap::iterator it = dfval->FuncPtrs.find(src);
        if (it == dfval->FuncPtrs.end()) return false;
        ValueSet& valueSet = it->second;
        for (auto v : valueSet)
            updateDstPointsToWithSrcPointsTo(
                    dfval->FuncPtrs, dfval->FuncPtrs, LI, v, false);
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
        if (it == dfval->FuncPtrs.end()) return false;

        // if a->{x}, x->S(b),
        if (it->second.size() == 1) {
            Value* dstPointsTo = *it->second.begin();
            updateDstPointsToWithSrcPointsTo(
                    dfval->FuncPtrs, dfval->FuncPtrs, dstPointsTo, src);
        }
        // if a->S(a), a_i->S(a_i) \cup S(b), for a_i \in S(a)
        else {
            // Value* dstPointsTo = mapAllocSite(dst, dfval);
            /*
            ValueSet dstPointsToSet = ValueSet({dstPointsTo});
            for (auto item : dfval->FuncPtrs) {
                if (item.second == it->second)
                    item.second = dstPointsToSet;
            }
            */
            // updateDstPointsToWithSrcPointsTo(dfval->FuncPtrs, dfval->FuncPtrs, dstPointsTo, src);

            for (auto dstPointsTo : it->second)
                updateDstPointsToWithSrcPointsTo(
                        dfval->FuncPtrs, dfval->FuncPtrs, dstPointsTo, src, false);

        }
    }

    /*
     * callee: {f_i(p_0, ..., p_n)}
     * args: a_0, ..., a_n
     * p_0->S(p_0) \cup S(a_0)
     * add callee to function worklist if data flow changes
     */
    else if (CallInst* CI = dyn_cast<CallInst>(inst)) {
        if (MemCpyInst* MCI = dyn_cast<MemCpyInst>(CI)) {
            Value* dst = MCI->getArgOperand(0);
            Value* src = MCI->getArgOperand(1);
            V2VSetMap::iterator it1 = dfval->FuncPtrs.find(dst);
            V2VSetMap::iterator it2 = dfval->FuncPtrs.find(src);
            if (it1 == dfval->FuncPtrs.end() || it2 == dfval->FuncPtrs.end()) return false;
            ValueSet& valueSet1 = it1->second;
            ValueSet &valueSet2 = it2->second;
            for (auto v1 : valueSet1) {
                bool begin = true;
                for (auto v2: valueSet2) {
                    updateDstPointsToWithSrcPointsTo(dfval->FuncPtrs, dfval->FuncPtrs, v1, v2, begin);
                    begin = false;
                }
            }
            return false;
        }

        getCallees(dfval->FuncPtrs, CI);
        const FuncSet& callees = CalleeMap[CI];
        if (callees.empty()) return false;

        FuncPtrInfo out;
        Function* caller = CI->getParent()->getParent();
        Diag << "Caller: " << caller << "\n";
        for (auto F : callees) {
            if (F->getName() == "malloc") {
                mapAllocSite(CI, dfval);
                continue;
            }
            Diag << "Callee: " << F->getName() << "\n";
            int argNum = CI->getNumArgOperands();
            for (int i = 0; i < argNum; i++) {
                Value* arg = CI->getArgOperand(i);
                Value* param = F->getArg(i);
                updateDstPointsToWithSrcPointsTo(
                        // (*result)[&F->getEntryBlock()].first.FuncPtrs,
                        dfval->FuncPtrs,
                        dfval->FuncPtrs,
                        param, arg);
            }

            if (merge(&(*result)[&F->getEntryBlock()].first, *dfval)) {
                funcWorkList->insert(F);
                funcWorkList->insert(caller);
                return true;
            }
            BasicBlock* exitBlock = ExitBlockMap[F];
            merge(&out, (*result)[exitBlock].second);
        }
        if (!out.FuncPtrs.empty())
            *dfval = out;
    }

    /*
    * return retval_f
    * call-site of f: {cs_i}
    * cs_i->S(cs_i) \cup S(retval_f)
    * add caller to function worklist if data flow changes
    */
    else if (ReturnInst* RI = dyn_cast<ReturnInst>(inst)) {
        Function* F = RI->getParent()->getParent();
        const CallSet& callers = CallerMap[F];
        Value* src = RI->getReturnValue();
        if (F->getReturnType()->isVoidTy()) return false;
        for (auto callSite : callers) {
            updateDstPointsToWithSrcPointsTo(
                    // (*result)[&F->getEntryBlock()].first.FuncPtrs,
                    dfval->FuncPtrs,
                    dfval->FuncPtrs,
                    callSite, src, false);
        }
    }

    /*
     * field = getelementptr struct
     * field->S(struct)
     */
    else if (GetElementPtrInst* GEP = dyn_cast<GetElementPtrInst>(inst)) {
        Value* src = GEP->getPointerOperand();
        updateDstPointsToWithSrcPointsTo(dfval->FuncPtrs, dfval->FuncPtrs, GEP, src);
    }

    /*
     * x->allocSite number
     */
    else if (AllocaInst* AI = dyn_cast<AllocaInst>(inst)) {
        mapAllocSite(AI, dfval);
    }

    /*
     * %call = call @malloc
     * %x = bitcast %call
     * %x->allocSite number
     *
     */
    else if (CastInst* castInst = dyn_cast<CastInst>(inst)) {
        /*
        CallInst* callInst = dyn_cast<CallInst>(castInst->getOperand(0));
        if (callInst && !callInst->isIndirectCall()
            && callInst->getCalledFunction()->getName() == "malloc")
            mapAllocSite(castInst, dfval);
        */
        Value* src = castInst->getOperand(0);
        updateDstPointsToWithSrcPointsTo(dfval->FuncPtrs, dfval->FuncPtrs, castInst, src);
    }

    else if (PHINode* PHI = dyn_cast<PHINode>(inst)) {
        int num = PHI->getNumIncomingValues();
        for (int i = 0; i < num; i++) {
            Value* src = PHI->getIncomingValue(i);
            // dfval->FuncPtrs[PHI].insert(src);
            updateDstPointsToWithSrcPointsTo(dfval->FuncPtrs, dfval->FuncPtrs, PHI, src, false);
        }
    }
    return false;
}


bool FuncPtrPass::runOnModule(Module& M) {
    FuncSet workList;
    FuncPtrVisitor visitor;
    DataflowResult<FuncPtrInfo>::Type result;
    FuncPtrInfo initval;

    for (Module::iterator i = M.begin(), e = M.end(); i != e; ++i) {
        Function* F = &*i;
        if (!F->isIntrinsic()) {
            workList.insert(F);
            initval.FuncPtrs[F] = ValueSet({F});
        }
        for (Function::iterator bi = F->begin(), be = F->end(); bi != be; ++bi) {
            BasicBlock* bb = &*bi;
            if (succ_empty(bb)) visitor.setExitBlock(F, bb);
        }
    }

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
        errs() << item.first << " : ";
        FuncSet& funcSet = item.second;
        for (auto F : funcSet) {
            if (F != *funcSet.begin()) errs() << ", ";
            errs() << F->getName();
        }
        errs() << "\n";
    }
}