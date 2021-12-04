//
// Created by weekends on 11/30/21.
//

#ifndef ASSIGN3_UTILS_H
#define ASSIGN3_UTILS_H
#include <llvm/IR/Function.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/Support/raw_ostream.h>
#include <set>
#include <map>
#include <vector>
typedef std::set<llvm::Function*> FuncSet;
typedef llvm::SmallPtrSet<llvm::Value*, 4> ValueSet;
typedef std::map<llvm::Value*, ValueSet> V2VSetMap;
typedef std::map<llvm::CallInst*, FuncSet> Call2FuncSetMap;
typedef llvm::SmallPtrSet<llvm::CallInst*, 4> CallSet;
typedef std::map<llvm::Function*, CallSet> Func2CallSetMap;
typedef std::map<int, FuncSet, std::less<int>> Int2FuncSetMap;
typedef std::map<llvm::Value*, llvm::Value*> V2VMap;
typedef std::map<llvm::Function*, llvm::BasicBlock*> Func2BBMap;
typedef std::map<ValueSet, ValueSet> VSet2VSetMap;
#define DEBUG 1
#define Diag if (DEBUG) llvm::errs()
#define CONTAINS(container, key) \
    (container.find(key) != container.end())
#endif //ASSIGN3_UTILS_H
