//
// Created by weekends on 11/29/21.
//

#ifndef ASSIGN3_FUNCPTR_H
#define ASSIGN3_FUNCPTR_H
#include <llvm/Pass.h>
#include "utils.h"
#include "Dataflow.h"

struct FuncPtrInfo {
    V2VSetMap FuncPtrs;

    // TODO:
    bool operator == (const FuncPtrInfo& info) const {
        if (info.FuncPtrs.size() != FuncPtrs.size())
            return false;
        for (auto item : FuncPtrs) {
            Value* key = item.first;
            ValueSet& value = item.second;
            V2VSetMap::const_iterator it = info.FuncPtrs.find(key);
            if (it == info.FuncPtrs.end()) return false;
            if (it->second != value) return false;
        }
        return true;
    }
};

class FuncPtrVisitor : public DataflowVisitor<FuncPtrInfo> {
private:
    Call2FuncSetMap CalleeMap;
    Func2CallSetMap CallerMap;
    int allocCount;
    V2VMap AllocMap;
    bool updateDstPointsToWithSrcPointsTo(
            V2VSetMap& dstFuncPtrMap, V2VSetMap& srcFuncPtrMap,
            llvm::Value* dst, llvm::Value* src, bool strongUpdate = true);
    void getCallees(V2VSetMap& funcPtrMap, llvm::CallInst* CI);
    void linkCallSiteAndCallee(llvm::CallInst* CI, llvm::Function* callee);
public:
    FuncPtrVisitor() : CalleeMap(), CallerMap(), allocCount(0), AllocMap() {}
    void merge(FuncPtrInfo* dest, const FuncPtrInfo& src) override;
    void compDFVal(Instruction* inst, FuncPtrInfo* dfval,
                   FuncSet* funcWorkList, DataflowResult<FuncPtrInfo>::Type* result) override;
    const Call2FuncSetMap& getCalleeMap() {
        return CalleeMap;
    }
};

///!TODO TO BE COMPLETED BY YOU FOR ASSIGNMENT 3
class FuncPtrPass : public llvm::ModulePass {
public:
    static char ID; // Pass identification, replacement for typeid
    FuncPtrPass() : ModulePass(ID){}

    bool runOnModule(llvm::Module &M) override;
    void dumpCallees(const Call2FuncSetMap& CalleeMap);
};
#endif //ASSIGN3_FUNCPTR_H
