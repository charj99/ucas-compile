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

/**
 * Override << operator to print FuncPtrInfo
 * @param out The out stream
 * @param info The FuncPtrInfo structure
 * @return The out stream after printing the FuncPtrInfo
 */
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

/**
 * Merge data flow value in src to dest
 * @param dest The destination dataflow
 * @param src The source dataflow
 */
void FuncPtrVisitor::merge(FuncPtrInfo *dest, const FuncPtrInfo &src) {
    for (auto item : src.FuncPtrs) {
        Value* key = item.first;
        ValueSet& value = item.second;
        V2VSetMap::iterator it = dest->FuncPtrs.find(key);
        // if key does not exist in dest, update
        if (it == dest->FuncPtrs.end())
            dest->FuncPtrs[key] = value;
        // if key exists in dest, insert
        else it->second.insert(value.begin(), value.end());
    }
}

/*
 * strong update: if src->S(src), dst->S(src)
 * not strong update: if src->S(src), dst->S(dst) \cup S(src)
 */
/**
 * Update the points-to set of dst, with the one of src.
 * Points-to sets are stored in funcPtrMap.
 * If strongUpdate is true, do strong update.
 * @param funcPtrMap The structure which stores the points-to sets
 * @param dst The destination
 * @param src The source
 * @param strongUpdate If true, do strong update
 * @return Return true, if the points-to set of dst is changed
 * @note Do strong update by default
 */
bool FuncPtrVisitor::updateDstPointsToWithSrcPointsTo(
        V2VSetMap &funcPtrMap, Value *dst, Value *src, bool strongUpdate) {

    V2VSetMap::iterator it = funcPtrMap.find(src);

    // if S(src) = {}, do nothing
    if (it == funcPtrMap.end()) return false;

    bool changed = false;

    // strong update: S(dst) = S(src)
    if (strongUpdate || !CONTAINS(funcPtrMap, dst)) {
        if (funcPtrMap[dst] != it->second) {
            funcPtrMap[dst] = it->second;
            changed = true;
        }
    }
    // not strong update: S(dst) = S(dst) U S(src)
    else {
        for (auto srcPointsTo : it->second)
            changed |= funcPtrMap[dst].insert(srcPointsTo).second;
    }

    return changed;
}

/**
 * Link CallInst to the corresponding called function
 * @param CI The CallInst
 * @param callee The called function
 */
void FuncPtrVisitor::linkCallSiteAndCallee(CallInst* CI, Function* callee) {
    CalleeMap[CI].insert(callee);
    CallerMap[callee].insert(CI);
}

/**
 * Get called functions at CallInst CI
 * @param funcPtrMap The structure which stores the points-to sets
 * @param CI The CallInst
 */
void FuncPtrVisitor::getCallees(V2VSetMap& funcPtrMap, CallInst* CI) {
    // direct call
    if (!CI->isIndirectCall()) {
        Function* callee = CI->getCalledFunction();
        // ignore intrinsic function, like "llvm.*"
        if (!callee->isIntrinsic()) linkCallSiteAndCallee(CI, callee);
        return;
    }

    // indirect call, bfs the callees
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

/**
 * Map value to an abstract alloc-site
 * @param v The value
 * @param dfval Current dataflow value
 * @return The alloc-site as a ConstInst value
 */
Value* FuncPtrVisitor::mapAllocSite(Value* v, FuncPtrInfo* dfval) {
    Value* allocSite = NULL;
    // if mapped, restore from AllocaMap
    if (CONTAINS(AllocMap, v))
        allocSite = AllocMap[v];
    // if not mapped, create and store in AllocaMap
    else {
        allocSite = ConstantInt::get(IntegerType::getInt32Ty(v->getContext()), allocCount++);
        AllocMap[v] = allocSite;
    }
    dfval->FuncPtrs[v] = ValueSet({allocSite});
    return allocSite;
}

/**
 * Process LoadInst.
 * For a = load b, let S(a) = S(S(b))
 * @param LI The LoadInst
 * @param dfval The dataflow value
 */
void FuncPtrVisitor::processLoadInst(LoadInst* LI, FuncPtrInfo* dfval) {
    Value* src = LI->getPointerOperand();
    V2VSetMap::iterator it = dfval->FuncPtrs.find(src);

    // if S(b) is empty, do nothing
    if (it == dfval->FuncPtrs.end()) return;

    // if S(b) is not empty, for each element ptb_i in S(b), update S(a) by {S(ptb_0) U S(ptb_1) U ...}
    bool begin = true; // trick: first time is strong update, others are merge
    ValueSet& valueSet = it->second;
    for (auto v : valueSet) {
        updateDstPointsToWithSrcPointsTo(dfval->FuncPtrs, LI, v, begin);
        begin = false;
    }
}

/**
 * Process StoreInst.
 * For store src, dst:
 * if S(dst) is empty, do nothing
 * if S(dst) = {x}, S(x) = S(src),
 * if S(dst) = {dpt_0, dpt_1, ...}, S(dpt_i) = S(dpt_i) U S(src)
 * @param SI The SotreInst
 * @param dfval The dataflow value
 */
void FuncPtrVisitor::processStoreInst(StoreInst* SI, FuncPtrInfo* dfval) {
    Value* src = SI->getValueOperand();
    Value* dst = SI->getPointerOperand();

    V2VSetMap::iterator it = dfval->FuncPtrs.find(dst);
    // if S(dst) is empty, do nothing
    if (it == dfval->FuncPtrs.end()) return;

    // if S(dst) = {x}, S(x) = S(src),
    if (it->second.size() == 1) {
        Value* dstPointsTo = *it->second.begin();
        updateDstPointsToWithSrcPointsTo(dfval->FuncPtrs, dstPointsTo, src);
    }
    // if S(dst) = {dpt_0, dpt_1, ...}, S(dpt_i) = S(dpt_i) U S(src)
    else {
        for (auto dstPointsTo : it->second)
            updateDstPointsToWithSrcPointsTo(dfval->FuncPtrs, dstPointsTo, src, false);
    }
}

/**
 * Process CallInst.
 * If callees are {f_i(p_i_0, p_i_1, ...)},
 * arguments are a_0, a_1, ...,
 * then S(p_i_j) = S(p_i_j) U S(a_j)
 * @param CI The CallInst
 * @param dfval The dataflow value
 * @param visitor The dataflow visitor
 * @param result The dataflow result
 * @param initval The initial dataflow value
 */
void FuncPtrVisitor::processCallInst(
        CallInst *CI, FuncPtrInfo *dfval, DataflowVisitor<FuncPtrInfo> *visitor,
        DataflowResult<FuncPtrInfo>::Type *result, const FuncPtrInfo &initval) {

    // if calls llvm.memcpy(dst, src), S(dpt_i) = S(dpt_i) U S(spt_j)
    if (MemCpyInst* MCI = dyn_cast<MemCpyInst>(CI)) {
        Value* dst = MCI->getArgOperand(0);
        Value* src = MCI->getArgOperand(1);
        V2VSetMap::iterator it1 = dfval->FuncPtrs.find(dst);
        V2VSetMap::iterator it2 = dfval->FuncPtrs.find(src);
        if (it1 == dfval->FuncPtrs.end() || it2 == dfval->FuncPtrs.end()) return;
        ValueSet& valueSet1 = it1->second;
        ValueSet &valueSet2 = it2->second;
        for (auto v1 : valueSet1) {
            bool begin = true;
            for (auto v2: valueSet2) {
                updateDstPointsToWithSrcPointsTo(dfval->FuncPtrs, v1, v2, begin);
                begin = false;
            }
        }
        return;
    }

    getCallees(dfval->FuncPtrs, CI);
    const FuncSet& callees = CalleeMap[CI];
    if (callees.empty()) return;

    FuncPtrInfo out;
    Diag << "Caller: " << CI->getParent()->getParent()->getName() << "\n";
    for (auto F : callees) {
        // if calls malloc, map alloc-site
        if (F->getName() == "malloc") {
            mapAllocSite(CI, dfval);
            continue;
        }
        Diag << "Callee: " << F->getName() << "\n";
        // pass arguments
        int argNum = CI->getNumArgOperands();
        for (int i = 0; i < argNum; i++) {
            Value* arg = CI->getArgOperand(i);
            Value* param = F->getArg(i);
            updateDstPointsToWithSrcPointsTo( dfval->FuncPtrs, param, arg);
        }

        // calculate dataflow of sub-function recursively, and merge result
        merge(&(*result)[&F->getEntryBlock()].first, *dfval);
        compForwardDataflowInter(F, visitor, result, initval);
        BasicBlock* exitBlock = ExitBlockMap[F];
        merge(&out, (*result)[exitBlock].second);
    }
    if (!out.FuncPtrs.empty())
        *dfval = out;
}

/**
 * Process ReturnInst.
 * For return retval,
 * S(call-site_i) = S(call-site_i) U S(retval)
 * @param RI The ReturnInst
 * @param dfval The dataflow value
 */
void FuncPtrVisitor::processReturnInst(ReturnInst *RI, FuncPtrInfo *dfval) {
    Function* F = RI->getParent()->getParent();
    const CallSet& callers = CallerMap[F];
    Value* src = RI->getReturnValue();

    // ignore void-typed return
    if (F->getReturnType()->isVoidTy()) return;

    // S(call-site_i) = S(call-site_i) U S(retval)
    for (auto callSite : callers) {
        updateDstPointsToWithSrcPointsTo(dfval->FuncPtrs, callSite, src, false);
    }
}

/**
 * Porcess GetElementPtrInst.
 * For field = getelementptr struct,
 * S(field) = S(struct)
 * @param GEP The GetElementPtrInst
 * @param dfval The dataflow value
 */
void FuncPtrVisitor::processGetElementPtrInst(GetElementPtrInst *GEP, FuncPtrInfo *dfval) {
    Value* src = GEP->getPointerOperand();
    updateDstPointsToWithSrcPointsTo(dfval->FuncPtrs, GEP, src);
}

/**
 * Process AllocaInst.
 * For a.addr = alloc,
 * S(a.addr)->abstract alloc-site
 * @param AI The AllocaInst
 * @param dfval The dataflow value
 */
void FuncPtrVisitor::processAllocaInst(AllocaInst *AI, FuncPtrInfo *dfval) {
    mapAllocSite(AI, dfval);
}

/**
 * Process CastInst.
 * For %a = bitcast %b,
 * S(%a) = S(%b)
 * @param castInst
 * @param dfval
 */
void FuncPtrVisitor::processCastInst(CastInst *castInst, FuncPtrInfo *dfval) {
    Value* src = castInst->getOperand(0);
    updateDstPointsToWithSrcPointsTo(dfval->FuncPtrs, castInst, src);
}

/**
 * Process PHINode.
 * For phi = {v_0, v_1, ...},
 * S(phi) = S(v_0) U S(v_1) ...
 * @param phi
 * @param dfval
 */
void FuncPtrVisitor::processPHINode(PHINode *PHI, FuncPtrInfo *dfval) {
    int num = PHI->getNumIncomingValues();
    bool begin = true;
    for (int i = 0; i < num; i++) {
        Value* src = PHI->getIncomingValue(i);
        updateDstPointsToWithSrcPointsTo(dfval->FuncPtrs, PHI, src, begin);
        begin = false;
    }
}

/**
 * Calculate dataflow for instruction
 * @param inst The instruction
 * @param dfval The dataflow value
 * @param visitor The dataflow vistor
 * @param result The dataflow result
 * @param initval The initial dataflow
 */
void FuncPtrVisitor::compDFVal(Instruction *inst, FuncPtrInfo *dfval,
                               DataflowVisitor<FuncPtrInfo>* visitor,
                               DataflowResult<FuncPtrInfo>::Type* result,
                               const FuncPtrInfo& initval) {
    Diag << "################## pointer before #################\n";
    if (DEBUG) inst->dump();
    Diag << *dfval;

    // TODO: make it more elegant, use "subclass"?
    if (LoadInst* LI = dyn_cast<LoadInst>(inst))
        processLoadInst(LI, dfval);
    else if (StoreInst* SI = dyn_cast<StoreInst>(inst))
        processStoreInst(SI, dfval);
    else if (CallInst* CI = dyn_cast<CallInst>(inst))
        processCallInst(CI, dfval, visitor, result, initval);
    else if (ReturnInst* RI = dyn_cast<ReturnInst>(inst))
        processReturnInst(RI, dfval);
    else if (GetElementPtrInst* GEP = dyn_cast<GetElementPtrInst>(inst))
        processGetElementPtrInst(GEP, dfval);
    else if (AllocaInst* AI = dyn_cast<AllocaInst>(inst))
        processAllocaInst(AI, dfval);
    else if (CastInst* castInst = dyn_cast<CastInst>(inst))
        processCastInst(castInst, dfval);
    else if (PHINode* PHI = dyn_cast<PHINode>(inst))
        processPHINode(PHI, dfval);
}

/**
 * Main processing function for FuncPtrPass,
 * calculate the points-to information for module
 * @param M The target module
 * @return false, since there is no change to the IR
 */
bool FuncPtrPass::runOnModule(Module& M) {
    FuncPtrVisitor visitor;
    DataflowResult<FuncPtrInfo>::Type result;
    FuncPtrInfo initval;

    // do initialization
    for (Module::iterator i = M.begin(), e = M.end(); i != e; ++i) {
        Function* F = &*i;
        // trick: let function points-to function
        if (!F->isIntrinsic()) initval.FuncPtrs[F] = ValueSet({F});
        // record the exit block for each function, to pass inter-procedural dataflow
        for (Function::iterator bi = F->begin(), be = F->end(); bi != be; ++bi) {
            BasicBlock* bb = &*bi;
            if (succ_empty(bb)) visitor.setExitBlock(F, bb);
        }
    }

    // iterate each function to calculate the dataflow
    int times = 0;
    for (Module::iterator i = M.begin(), e = M.end(); i != e; ++i) {
        Function* F = &*i;
        if (F->isIntrinsic()) continue;
        compForwardDataflowInter(F, &visitor, &result, initval);
        Diag << ++times << ": " << F->getName() << "------------------------------\n";
        if (DEBUG) printDataflowResult<FuncPtrInfo>(errs(), result);
        Diag << "------------------------------\n";
    }

    // dump callee for each call-site
    dumpCallees(visitor.getCalleeMap());
    return false;
}

/**
 * Dump callee for each call-site,
 * in format: "line number: function0, function1, ...",
 * e.g. 1: foo, bar
 * @param CalleeMap The map of CallInst and called functions at the site
 */
void FuncPtrPass::dumpCallees(const Call2FuncSetMap& CalleeMap) {
    Int2FuncSetMap result;
    // derive the map of line number and called functions at the line
    for (auto item : CalleeMap) {
        CallInst* CI = item.first;
        FuncSet& funcSet = item.second;
        int lineNo = CI->getDebugLoc().getLine();
        for (auto F : funcSet) {
            result[lineNo].insert(F);
        }
    }

    // print the called functions by lines
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