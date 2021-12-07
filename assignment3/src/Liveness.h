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

#include <llvm/IR/Function.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/IntrinsicInst.h>

#include "Dataflow.h"
using namespace llvm;


struct LivenessInfo {
   std::set<Instruction *> LiveVars;             /// Set of variables which are live
   LivenessInfo() : LiveVars() {}
   LivenessInfo(const LivenessInfo & info) : LiveVars(info.LiveVars) {}
  
   bool operator == (const LivenessInfo & info) const {
       return LiveVars == info.LiveVars;
   }
};

inline raw_ostream &operator<<(raw_ostream &out, const LivenessInfo &info) {
    for (std::set<Instruction *>::iterator ii=info.LiveVars.begin(), ie=info.LiveVars.end();
         ii != ie; ++ ii) {
       const Instruction * inst = *ii;
       out << inst->getName();
       out << " ";
    }
    return out;
}

	
class LivenessVisitor : public DataflowVisitor<struct LivenessInfo> {
public:
   LivenessVisitor() {}
   bool merge(LivenessInfo * dest, const LivenessInfo & src) override {
       bool changed = false;
       for (std::set<Instruction *>::const_iterator ii = src.LiveVars.begin(),
            ie = src.LiveVars.end(); ii != ie; ++ii) {
           changed |= dest->LiveVars.insert(*ii).second;
       }
       return changed;
   }

   bool compDFVal(Instruction *inst, LivenessInfo * dfval,
                  DataflowVisitor<LivenessInfo>* visitor,
                  DataflowResult<LivenessInfo>::Type* result,
                  const LivenessInfo& initval,
                  FuncSet* funcWorkList) override{
        if (isa<DbgInfoIntrinsic>(inst)) return false;
        dfval->LiveVars.erase(inst);
        for(User::op_iterator oi = inst->op_begin(), oe = inst->op_end();
            oi != oe; ++oi) {
           Value * val = *oi;
           if (isa<Instruction>(val)) 
               dfval->LiveVars.insert(cast<Instruction>(val));
        }
        return false;
   }
};


class Liveness : public FunctionPass {
public:

   static char ID;
   Liveness() : FunctionPass(ID) {} 

   bool runOnFunction(Function &F) override {
       F.dump();
       LivenessVisitor visitor;
       DataflowResult<LivenessInfo>::Type result;
       LivenessInfo initval;

       compBackwardDataflow(&F, &visitor, &result, initval);
       printDataflowResult<LivenessInfo>(errs(), result);
       return false;
   }
};



