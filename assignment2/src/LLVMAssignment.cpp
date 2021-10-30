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

#define DEBUG 0
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
typedef set<FVPair> FVPairSet;
typedef map<FVPair, FVPairSet> PVBinds;

typedef set<Function* > FuncSet;
typedef map<FVPair, FuncSet> PVVals;

typedef map<CallInst*, FuncSet> CallGraph;

typedef set<StringRef> StrSet;
typedef map<int, StrSet, less<int> > SortedLineFuncsMap;

typedef set<Value*> ValueSet;
typedef map<Function*, ValueSet> RetVals;

typedef struct GlobalInfo_s {
    PVVals pvVals;
    PVBinds pvBinds;
    CallGraph callGraph;

    RetVals retVals;
} GlobalInfo;

#define FOREACH(Type, item, it) \
    for (Type::iterator it = item.begin(), it##e = item.end(); it##e != it; ++it)
#define INSERT2SETMAP(MapType, item, key, SetType, value, changed) \
    do {                                   \
        MapType::iterator it = item.find(key);\
        if (it != item.end()) \
            changed |= it->second.insert(value).second;                \
        else {                                            \
            SetType tmpSet = SetType();                  \
            tmpSet.insert(value);\
            item[key] = tmpSet;                                    \
            changed = true;                                                          \
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
  void collectIntraPVValsAndRets(Module& M);
  void collectIntraPVBinds(Module& M);
  bool propagateBindings(Module& M);
  bool mergeBinds(FVPair u, FVPair v);
  bool buildCallGraph(Module& M);
  bool isFuncPtrType(Type* type);
  bool isFuncPtr(Value* value);
  bool isDirectCall(CallInst* CI);
  bool addBinds4Params(Function* F, CallInst* CI);
  bool addBinds4Rets(Function* F, CallInst* CI);

  void dumpCallGraph();
  void dumpPVVals();
  void dumpPVBinds();
};

bool FuncPtrPass::isDirectCall(CallInst* CI) {
    return !CI->isIndirectCall() && !CI->getCalledFunction()->isIntrinsic();
}

void FuncPtrPass::collectDirectCalls(Module& M) {
    CallGraph& callGraph = globalInfo.callGraph;
    FOREACH(Module, M, f) {
        Function* F = &*f;
        for (inst_iterator i = inst_begin(F), ie = inst_end(F); i != ie; ++i) {
            Instruction* I = &*i;
            if (CallInst* CI = dyn_cast<CallInst>(I)) {
                if (isDirectCall(CI)) {
                    bool tmpChanged = false;
                    INSERT2SETMAP(CallGraph, callGraph, CI, FuncSet,
                                  CI->getCalledFunction(), tmpChanged);
                }
            }
        }
    }
}

void FuncPtrPass::collectIntraPVValsAndRets(Module& M) {
    PVVals& pvVals = globalInfo.pvVals;
    RetVals& retVals = globalInfo.retVals;
    FOREACH(Module, M, f) {
        Function* F = &*f;
        Type* retType = F->getReturnType();
        bool retsFuncPtr = (isFuncPtrType(retType) || retType->isIntegerTy());
        for (inst_iterator i = inst_begin(F), ie = inst_end(F); i != ie; ++i) {
            Instruction* I = &*i;

            // solve phi node
            if (PHINode* phi = dyn_cast<PHINode>(I)) {
                int n = phi->getNumIncomingValues();
                for (int j = 0; j < n; j++) {
                    Value* value = phi->getIncomingValue(j);
                    if (Function* funcPtr = dyn_cast<Function>(value)) {
                        bool tmpChanged = false;
                        INSERT2SETMAP(PVVals, pvVals, FVPair(F, phi), FuncSet, funcPtr, tmpChanged);
                    }
                }
            }

            if (ReturnInst* RI = dyn_cast<ReturnInst>(I)) {
                if (retsFuncPtr) {
                    bool tmpChanged = false;
                    INSERT2SETMAP(RetVals, retVals, F, ValueSet, RI->getReturnValue(), tmpChanged);
                }
            }
        }
    }
}

bool FuncPtrPass::isFuncPtrType(Type* type) {
    PointerType* PT = dyn_cast<PointerType>(type);
    if (!PT) return false;
    FunctionType* FT = dyn_cast<FunctionType>(PT->getElementType());
    return FT != NULL;
}

bool FuncPtrPass::isFuncPtr(Value* value) {
    return isFuncPtrType(value->getType()) && !isa<Function>(value) && !isa<ConstantPointerNull>(value);
}

void FuncPtrPass::collectIntraPVBinds(Module& M) {
    PVBinds& pvBinds = globalInfo.pvBinds;
    FOREACH(Module, M, f) {
        Function* F = &*f;
        for (inst_iterator i = inst_begin(F), ie = inst_end(F); i != ie; ++i) {
            Instruction* I = &*i;

            // solve phi node
            if (PHINode* phi = dyn_cast<PHINode>(I)) {
                int n = phi->getNumIncomingValues();
                for (int j = 0; j < n; j++) {
                    Value* value = phi->getIncomingValue(j);
                    if (isFuncPtr(value)) {
                        bool tmpChanged = false;
                        INSERT2SETMAP(PVBinds, pvBinds, FVPair(F, phi), FVPairSet, FVPair(F, value), tmpChanged);
                    }
                }
            }
        }
    }
}

bool FuncPtrPass::addBinds4Params(Function* F, CallInst* CI) {
    CallGraph& callGraph = globalInfo.callGraph;
    PVBinds& pvBinds = globalInfo.pvBinds;
    PVVals& pvVals = globalInfo.pvVals;
    bool changed = false;

    int n = CI->getNumArgOperands();
    for (int j = 0; j < n; j++) {
        Value* value = CI->getArgOperand(j);

        // update PVVals
        // useless
        if (Function* f = dyn_cast<Function>(value)) {
            Diag << "update pvVals\n";
            FuncSet& FS = callGraph[CI];
            FOREACH(FuncSet, FS, k) {
                Function* callee = *k;
                FVPair u = FVPair(callee, callee->getArg(j));
                INSERT2SETMAP(PVVals, pvVals, u, FuncSet, f, changed);
            }
        }

        // update PVBinds
        else if (isFuncPtr(value)) {
            Diag << "update pvBinds\n";
            FVPair v = FVPair(F, value);
            FuncSet& FS = callGraph[CI];
            FOREACH(FuncSet, FS, k) {
                Function* callee = *k;
                FVPair u = FVPair(callee, callee->getArg(j));
                INSERT2SETMAP(PVBinds, pvBinds, u, FVPairSet, v, changed);
            }
        }
    }
    return changed;
}

bool FuncPtrPass::addBinds4Rets(Function* F, CallInst* CI) {
    Type* type = CI->getType();
    if (!(isFuncPtrType(CI->getType()) || type->isIntegerTy())) return false;

    CallGraph& callGraph = globalInfo.callGraph;
    PVBinds& pvBinds = globalInfo.pvBinds;
    // PVVals& pvVals = globalInfo.pvVals;
    RetVals& retVals = globalInfo.retVals;
    bool changed = false;
    FuncSet& FS = callGraph[CI];
    FOREACH(FuncSet, FS, i) {
        Function* callee = *i;
            ValueSet& rets = retVals[callee];
            FOREACH(ValueSet, rets, j) {
                FVPair v = FVPair(callee, *j);
                INSERT2SETMAP(PVBinds, pvBinds, FVPair(F, CI), FVPairSet, v, changed);
            }
    }

    return changed;
}

bool FuncPtrPass::buildCallGraph(Module& M) {
    // FVPairSet visited;
    PVBinds& pvBinds = globalInfo.pvBinds;
    PVVals& pvVals = globalInfo.pvVals;
    CallGraph& callGraph = globalInfo.callGraph;
    bool changed = false;
    FOREACH(Module, M, f) {
        Function* F = &*f;
        for (inst_iterator i = inst_begin(F), ie = inst_end(F); i != ie; ++i) {
            Instruction* I = &*i;
            if (CallInst* CI = dyn_cast<CallInst>(I)) {
                Value* value = CI->getCalledValue();
                FVPair fvPair = FVPair(F, value);
                /*
                if(CONTAINS(visited, fvPair)) continue;
                visited.insert(fvPair);
                 */

                // get values
                if (CONTAINS(pvVals, fvPair)) {
                    FuncSet& FS = pvVals[fvPair];
                    FOREACH(FuncSet, FS, k) {
                        bool tmpChanged = false;
                        INSERT2SETMAP(CallGraph, callGraph, CI, FuncSet, *k, tmpChanged);
                    }
                }

                // get bindings
                if (CONTAINS(pvBinds, fvPair)) {
                    FVPairSet &fvSet = pvBinds[fvPair];
                    FOREACH(FVPairSet, fvSet, j) {
                        FVPair ptr = *j;
                        if (!CONTAINS(pvVals, ptr)) continue;

                        // get value of each binding
                        FuncSet &FS = pvVals[ptr];
                        FOREACH(FuncSet, FS, k) {
                            bool tmpChanged = false;
                            INSERT2SETMAP(CallGraph, callGraph, CI, FuncSet, *k, tmpChanged);
                        }
                    }
                }

                // add bindings for function parameters
                changed |= addBinds4Params(F, CI);
                changed |= addBinds4Rets(F, CI);
            }
        }
    }
    return changed;
}

bool FuncPtrPass::mergeBinds(FVPair u, FVPair v) {
    PVBinds& pvBinds = globalInfo.pvBinds;
    /*
    if (isa<ConstantPointerNull>(v.v)) {
        errs() << "here\n";
        return false;
    }
    */

    FVPairSet& uFV = pvBinds[u];

    FVPairSet vFV;
    if (CONTAINS(pvBinds, v))
        vFV = pvBinds[v];
    else return false;

    bool changed = false;
    FOREACH(FVPairSet, vFV, i)
        changed |= uFV.insert(*i).second;

    return changed;
}

bool FuncPtrPass::propagateBindings(Module& M) {
    bool changed = false;
    PVBinds& pvBinds = globalInfo.pvBinds;
    FOREACH(PVBinds, pvBinds, i) {
        FVPair u = i->first;
        FVPairSet& fvSet = i->second;
        FOREACH(FVPairSet, fvSet, j) {
            FVPair v = *j;
            changed |= mergeBinds(u, v);
        }
    }
    Diag << "before build CG\n";
    changed |= buildCallGraph(M);
    Diag << "after build CG\n";
    Diag << "***************************************\n";
    Diag << "PVBinds\n";
    Diag << "***************************************\n";
    if (DEBUG) dumpPVBinds();
    return changed;
}

bool FuncPtrPass::runOnModule(Module &M) {
    // M.dump();

    collectDirectCalls(M);
    Diag << "***************************************\n";
    Diag << "Direct calls\n";
    Diag << "***************************************\n";
    if (DEBUG) dumpCallGraph();

    collectIntraPVValsAndRets(M);
    Diag << "***************************************\n";
    Diag << "PVVals\n";
    Diag << "***************************************\n";
    if (DEBUG) dumpPVVals();

    collectIntraPVBinds(M);
    Diag << "***************************************\n";
    Diag << "PVBinds\n";
    Diag << "***************************************\n";
    if (DEBUG) dumpPVBinds();

    Diag << "propagate\n";
    while (propagateBindings(M));

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
        FVPairSet& fvSet = i->second;
        FOREACH(FVPairSet, fvSet, j) {
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
        errs() << i->first << " : ";
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
