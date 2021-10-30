//===- Hello.cpp - Example code from "Writing an LLVM Pass" ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements two versions of the LLVM "Hello World" pass described
// in docs/WritingAnLLVMPass.html
//
//===----------------------------------------------------------------------===//

#include <llvm/Support/CommandLine.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/ToolOutputFile.h>

#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Utils.h>

#include <llvm/IR/Function.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>

#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>

#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/InstIterator.h>

#define DEBUG 1
#define Diag if (DEBUG) errs()

using namespace llvm;
using namespace std;
static ManagedStatic<LLVMContext> GlobalContext;
static LLVMContext &getGlobalContext() { return *GlobalContext; }
struct FVPair {
    Function* f;
    Value* v;
    FVPair(Function* _f, Value* _v): f(_f), v(_v) {}
    bool operator==(const FVPair& a) const {
        return f == a.f && v == a.v;
    }
    bool operator!=(const FVPair& a) const {
        return !(*this == a);
    }
    bool operator<(const FVPair& a) const {
        if (f == a.f)
            return v < a.v;
        return f < a.f;
    }
};
// focus on load, store, and callInst to build PVBinds
typedef set<FVPair> FVSet;
typedef map<FVPair, FVSet> PVBinds;

typedef set<Function* > FuncSet;
typedef map<FVPair, FuncSet> PVVals;

typedef map<CallInst*, FuncSet> CallGraph;

typedef set<StringRef> StrSet;
typedef map<int, StrSet, greater<int> > SortedLineFuncsMap;

typedef struct GlobalInfo_s {
    PVVals pvVals;
    PVBinds pvBinds;
    CallGraph callGraph;
} GlobalInfo;

#define FOREACH(Type, item, it) \
    for (Type::iterator it = item.begin(), it##e = item.end(); it##e != it; ++it)
#define INSERT2SETMAP(MapType, item, key, SetType, value) \
    do {                                   \
        MapType::iterator it = item.find(key);\
        if (it != item.end())                 \
            it->second.insert(value);                \
        else {                                            \
            SetType tmpSet = SetType();                  \
            tmpSet.insert(value);\
            item[key] = tmpSet;\
        }                                 \
    } while (0)
#define CONTAINS(item, target) \
    (item.find(target) != item.end())

/* In LLVM 5.0, when  -O0 passed to clang , the functions generated with clang will
 * have optnone attribute which would lead to some transform passes disabled, like mem2reg.
 */
struct EnableFunctionOptPass: public FunctionPass {
    static char ID;
    EnableFunctionOptPass():FunctionPass(ID){}
    bool runOnFunction(Function & F) override{
        if(F.hasFnAttribute(Attribute::OptimizeNone))
        {
            F.removeFnAttr(Attribute::OptimizeNone);
        }
        return true;
    }
};

char EnableFunctionOptPass::ID=0;

	
///!TODO TO BE COMPLETED BY YOU FOR ASSIGNMENT 2
///Updated 11/10/2017 by fargo: make all functions
///processed by mem2reg before this pass.
struct FuncPtrPass : public ModulePass {
  static char ID; // Pass identification, replacement for typeid
  GlobalInfo globalInfo;
  FuncPtrPass() : ModulePass(ID) {}

  // The runOnModule method performs the interesting work of the pass.
  // It should return true if the module was modified by the transformation
  // and false otherwise.
  bool runOnModule(Module &M) override;
  void collectDirectCalls(Module &M);
  void collectPVVals(Module& M);
  void collectPVBinds(Module& M);
  bool propagateBindings();
  bool mergeBinds(FVPair u, FVPair v);
  bool haveSamePVBinds(FVSet& uFV, FVSet& vFV);
  void buildCallGraph(Module& M);
  bool isFuncPtrType(Value* value);

  void dumpCallGraph();
  void dumpPVVals();
  void dumpPVBinds();
};

// TODO:
void FuncPtrPass::collectDirectCalls(Module& M) {
    CallGraph& callGraph = globalInfo.callGraph;
    FOREACH(Module, M, f) {
        Function* F = &*f;
        for (inst_iterator i = inst_begin(F), ie = inst_end(F); i != ie; ++i) {
            Instruction* I = &*i;
            if (CallInst* CI = dyn_cast<CallInst>(I)) {
                if (CI->isIndirectCall()) continue;
                Function* callee = CI->getCalledFunction();
                if (callee->isIntrinsic()) continue;
                INSERT2SETMAP(CallGraph, callGraph, CI, FuncSet, callee);
            }
        }
    }
}

void FuncPtrPass::collectPVVals(Module& M) {
    PVVals& pvVals = globalInfo.pvVals;
    FOREACH(Module, M, f) {
        Function* F = &*f;
        for (inst_iterator i = inst_begin(F), ie = inst_end(F); i != ie; ++i) {
            Instruction* I = &*i;
            if (PHINode* phi = dyn_cast<PHINode>(I)) {
                int n = phi->getNumIncomingValues();
                for (int j = 0; j < n; j++) {
                    Value* value = phi->getIncomingValue(j);
                    if (Function* funcPtr = dyn_cast<Function>(value))
                        INSERT2SETMAP(PVVals, pvVals, FVPair(F, phi), FuncSet, funcPtr);
                }
            }
        }
    }
}

bool FuncPtrPass::isFuncPtrType(Value* value) {
    PointerType* PT = dyn_cast<PointerType>(value->getType());
    if (!PT) return false;
    FunctionType* FT = dyn_cast<FunctionType>(PT->getElementType());
    return FT != NULL;
}

void FuncPtrPass::collectPVBinds(Module& M) {
    PVBinds& pvBinds = globalInfo.pvBinds;
    FOREACH(Module, M, f) {
        Function* F = &*f;
        for (inst_iterator i = inst_begin(F), ie = inst_end(F); i != ie; ++i) {
            Instruction* I = &*i;
            if (PHINode* phi = dyn_cast<PHINode>(I)) {
                int n = phi->getNumIncomingValues();
                for (int j = 0; j < n; j++) {
                    Value* value = phi->getIncomingValue(j);
                    if (isFuncPtrType(value) && !isa<Function>(value)) {
                        INSERT2SETMAP(PVBinds, pvBinds, FVPair(F, phi), FVSet, FVPair(F, value));
                    }
                }
            }
        }
    }
}

void FuncPtrPass::buildCallGraph(Module& M) {
    FVSet visited;
    PVBinds& pvBinds = globalInfo.pvBinds;
    PVVals& pvVals = globalInfo.pvVals;
    CallGraph& callGraph = globalInfo.callGraph;
    FOREACH(Module, M, f) {
        Function* F = &*f;
        for (inst_iterator i = inst_begin(F), ie = inst_end(F); i != ie; ++i) {
            Instruction* I = &*i;
            if (CallInst* CI = dyn_cast<CallInst>(I)) {
                Value* value = CI->getCalledValue();
                FVPair fvPair = FVPair(F, value);
                if(CONTAINS(visited, fvPair)) continue;
                visited.insert(fvPair);

                // get values
                if (CONTAINS(pvVals, fvPair)) {
                    FuncSet& FS = pvVals[fvPair];
                    FOREACH(FuncSet, FS, k)
                        INSERT2SETMAP(CallGraph, callGraph, CI, FuncSet, *k);
                }

                // get bindings
                if (CONTAINS(pvBinds, fvPair)) {
                    FVSet &fvSet = pvBinds[fvPair];
                    FOREACH(FVSet, fvSet, j) {
                        FVPair ptr = *j;
                        if (!CONTAINS(pvVals, ptr)) continue;

                        // get value of each binding
                        FuncSet &FS = pvVals[ptr];
                        FOREACH(FuncSet, FS, k)
                            INSERT2SETMAP(CallGraph, callGraph, CI, FuncSet, *k);
                    }
                }
            }
        }
    }
}


bool FuncPtrPass::haveSamePVBinds(FVSet& uFV, FVSet& vFV) {
    // have the same size
    if (uFV.size() != vFV.size()) return false;

    // TODO: add sort function?
    for (FVSet::iterator i = uFV.begin(), j = vFV.begin(), ie = uFV.end();
        i != ie; ++i, ++j) {
        if (*i != *j) return false;
    }
    return true;
}

bool FuncPtrPass::mergeBinds(FVPair u, FVPair v) {
    PVBinds& pvBinds = globalInfo.pvBinds;
    FVSet uFV;
    if (CONTAINS(pvBinds, u))
        uFV = pvBinds[u];
    else uFV = FVSet();

    FVSet vFV;
    if (CONTAINS(pvBinds, v))
        vFV = pvBinds[v];
    else vFV = FVSet();

    if (haveSamePVBinds(uFV, vFV)) return false;

    uFV.insert(vFV.begin(), vFV.end());
    pvBinds[u] = uFV;
    return true;
}

bool FuncPtrPass::propagateBindings() {
    bool changed = false;
    PVBinds& pvBinds = globalInfo.pvBinds;
    FOREACH(PVBinds, pvBinds, i) {
        FVPair u = i->first;
        FVSet& fvSet = i->second;
        FOREACH(FVSet, fvSet, j) {
            FVPair v = *j;
            Diag << "mergeBinds begin\n";
            changed |= mergeBinds(u, v);
            Diag << "mergeBinds end\n";
        }
    }
    return changed;
}

bool FuncPtrPass::runOnModule(Module &M) {
    // M.dump();

    collectDirectCalls(M);
    Diag << "***************************************\n";
    Diag << "Direct calls\n";
    Diag << "***************************************\n";
    if (DEBUG) dumpCallGraph();

    collectPVVals(M);
    Diag << "***************************************\n";
    Diag << "PVVals\n";
    Diag << "***************************************\n";
    if (DEBUG) dumpPVVals();

    collectPVBinds(M);
    Diag << "***************************************\n";
    Diag << "PVBinds\n";
    Diag << "***************************************\n";
    if (DEBUG) dumpPVBinds();

    Diag << "propagate\n";
    while (propagateBindings());

    buildCallGraph(M);
    Diag << "***************************************\n";
    Diag << "Call graph\n";
    Diag << "***************************************\n";
    dumpCallGraph();
    return false;
}

void FuncPtrPass::dumpPVVals() {
    PVVals& pvVals = globalInfo.pvVals;
    FOREACH(PVVals, pvVals, i) {
        FVPair fvPair = i->first;
        fvPair.v->dump();
        errs() << ": ";
        FuncSet& FS = i->second;
        FOREACH(FuncSet, FS, j) {
            Function* F = *j;
            errs() << F->getName() << ", ";
        }
        errs() << "\n";
    }
}

void FuncPtrPass::dumpPVBinds() {
    PVBinds& pvBinds = globalInfo.pvBinds;
    FOREACH(PVBinds, pvBinds, i) {
        FVPair u = i->first;
        u.v->dump();
        errs() << ":\n";
        FVSet& fvSet = i->second;
        FOREACH(FVSet, fvSet, j) {
            FVPair v = *j;
            v.v->dump();
            errs() << ", ";
        }
        errs() << "\n\n";
    }
}

void FuncPtrPass::dumpCallGraph() {
    SortedLineFuncsMap result;
    CallGraph& callGraph = globalInfo.callGraph;
    FOREACH(CallGraph, callGraph, i) {
        CallInst* CI = i->first;
        FuncSet& FS = i->second;
        StrSet strSet;
        FOREACH(FuncSet, FS, fi) {
            Function* F = *fi;
            strSet.insert(F->getName());
        }

        int lineNo = CI->getDebugLoc()->getLine();
        result[lineNo] = strSet;
    }

    FOREACH(SortedLineFuncsMap, result, i) {
        errs() << i->first << ": ";
        StrSet strSet = i->second;
        StrSet::iterator j = strSet.begin(), je = strSet.end();
        errs() << *j;
        for (++j; j != je; ++j) {
            errs() << ", " << *j;
        }
        errs() << "\n";
    }
}

char FuncPtrPass::ID = 0;
static RegisterPass<FuncPtrPass> X("funcptrpass", "Print function call instruction");

static cl::opt<std::string>
InputFilename(cl::Positional,
              cl::desc("<filename>.bc"),
              cl::init(""));


int main(int argc, char **argv) {
   LLVMContext &Context = getGlobalContext();
   SMDiagnostic Err;
   // Parse the command line to read the Inputfilename
   cl::ParseCommandLineOptions(argc, argv,
                              "FuncPtrPass \n My first LLVM too which does not do much.\n");


   // Load the input module
   std::unique_ptr<Module> M = parseIRFile(InputFilename, Err, Context);
   if (!M) {
      Err.print(argv[0], errs());
      return 1;
   }

   llvm::legacy::PassManager Passes;
   	
   ///Remove functions' optnone attribute in LLVM5.0
   Passes.add(new EnableFunctionOptPass());
   ///Transform it to SSA
   Passes.add(llvm::createPromoteMemoryToRegisterPass());

   /// Your pass to print Function and Call Instructions
   Passes.add(new FuncPtrPass());
   Passes.run(*M.get());
}
