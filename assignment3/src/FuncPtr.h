//
// Created by weekends on 11/29/21.
//

#ifndef ASSIGN3_FUNCPTR_H
#define ASSIGN3_FUNCPTR_H
#include <llvm/Pass.h>
///!TODO TO BE COMPLETED BY YOU FOR ASSIGNMENT 3
class FuncPtrPass : public llvm::ModulePass {
public:
    static char ID; // Pass identification, replacement for typeid
    FuncPtrPass() : ModulePass(ID) {}

    bool runOnModule(llvm::Module &M) override;
};
#endif //ASSIGN3_FUNCPTR_H
