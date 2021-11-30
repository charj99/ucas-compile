/************************************************************************
 *
 * @file Dataflow.h
 *
 * General dataflow framework
 *
 ***********************************************************************/

#ifndef _DATAFLOW_H_
#define _DATAFLOW_H_

#include <llvm/Support/raw_ostream.h>
#include <map>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Function.h>
#include "utils.h"

using namespace llvm;

///
/// Dummy class to provide a typedef for the detailed result set
/// For each basicblock, we compute its input dataflow val and its output dataflow val
///
template<class T>
struct DataflowResult {
    typedef typename std::map<BasicBlock *, std::pair<T, T> > Type;
};

///Base dataflow visitor class, defines the dataflow function
template <class T>
class DataflowVisitor {
public:
    virtual ~DataflowVisitor() { }

    /// Dataflow Function invoked for each basic block 
    /// 
    /// @block the Basic Block
    /// @dfval the input dataflow value
    /// @isforward true to compute dfval forward, otherwise backward
    /// @funcWorkList the inter-procedural worklist
    virtual void compDFVal(BasicBlock *block, T *dfval, bool isforward,
                           FuncSet* funcWorkList = NULL,
                           typename DataflowResult<T>::Type *result = NULL) {
        if (isforward == true) {
           for (BasicBlock::iterator ii=block->begin(), ie=block->end(); 
                ii!=ie; ++ii) {
                Instruction * inst = &*ii;
                compDFVal(inst, dfval, funcWorkList, result);
           }
        } else {
           for (BasicBlock::reverse_iterator ii=block->rbegin(), ie=block->rend();
                ii != ie; ++ii) {
                Instruction * inst = &*ii;
                compDFVal(inst, dfval, funcWorkList, result);
           }
        }
    }

    ///
    /// Dataflow Function invoked for each instruction
    ///
    /// @inst the Instruction
    /// @dfval the input dataflow value
    /// @funcWorkList the inter-procedural worklist
    /// @return true if dfval changed
    virtual void compDFVal(Instruction *inst, T *dfval, FuncSet* funcWorkList,
                           typename DataflowResult<T>::Type *result) = 0;

    ///
    /// Merge of two dfvals, dest will be ther merged result
    /// @return true if dest changed
    ///
    virtual void merge( T *dest, const T &src ) = 0;
};


/// 
/// Compute a forward iterated fixedpoint dataflow function, using a user-supplied
/// visitor function. Note that the caller must ensure that the function is
/// in fact a monotone function, as otherwise the fixedpoint may not terminate.
/// 
/// @param fn The function
/// @param visitor A function to compute dataflow vals
/// @param result The results of the dataflow 
/// @initval the Initial dataflow value
/// @param funcWorkList The work list of inter-procedural analysis(stores functions)
template<class T>
void compForwardDataflowInter(Function *fn,
                              DataflowVisitor<T> *visitor,
                              typename DataflowResult<T>::Type *result,
                              const T & initval,
                              FuncSet* funcWorkList) {

    std::set<BasicBlock *> worklist;

    // Initialize the worklist with all exit blocks
    for (Function::iterator bi = fn->begin(); bi != fn->end(); ++bi) {
        BasicBlock * bb = &*bi;
        // TODO: init
        if (result->find(bb) == result->end())
            result->insert(std::make_pair(bb, std::make_pair(initval, initval)));
        worklist.insert(bb);
    }

    // Iteratively compute the dataflow result
    while (!worklist.empty()) {
        BasicBlock *bb = *worklist.begin();
        if (bb == &bb->getParent()->getEntryBlock())
            Diag << "is entry block!\n";
        worklist.erase(worklist.begin());

        // Merge all incoming value
        T bbEntryVal = (*result)[bb].first;
        for (pred_iterator pi = pred_begin(bb), pe = pred_end(bb); pi != pe; pi++) {
            BasicBlock *pred = *pi;
            visitor->merge(&bbEntryVal, (*result)[pred].second);
        }

        (*result)[bb].first = bbEntryVal;
        visitor->compDFVal(bb, &bbEntryVal, true, funcWorkList, result);

        // If outgoing value changed, propagate it along the CFG
        if (bbEntryVal == (*result)[bb].second) continue;
        (*result)[bb].second = bbEntryVal;

        for (succ_iterator si = succ_begin(bb), se = succ_end(bb); si != se; si++) {
            worklist.insert(*si);
        }
    }
}
/// 
/// Compute a backward iterated fixedpoint dataflow function, using a user-supplied
/// visitor function. Note that the caller must ensure that the function is
/// in fact a monotone function, as otherwise the fixedpoint may not terminate.
/// 
/// @param fn The function
/// @param visitor A function to compute dataflow vals
/// @param result The results of the dataflow 
/// @initval The initial dataflow value
template<class T>
void compBackwardDataflow(Function *fn,
    DataflowVisitor<T> *visitor,
    typename DataflowResult<T>::Type *result,
    const T &initval) {

    std::set<BasicBlock *> worklist;

    // Initialize the worklist with all exit blocks
    for (Function::iterator bi = fn->begin(); bi != fn->end(); ++bi) {
        BasicBlock * bb = &*bi;
        result->insert(std::make_pair(bb, std::make_pair(initval, initval)));
        worklist.insert(bb);
    }

    // Iteratively compute the dataflow result
    while (!worklist.empty()) {
        BasicBlock *bb = *worklist.begin();
        worklist.erase(worklist.begin());

        // Merge all incoming value
        T bbexitval = (*result)[bb].second;
        for (auto si = succ_begin(bb), se = succ_end(bb); si != se; si++) {
            BasicBlock *succ = *si;
            visitor->merge(&bbexitval, (*result)[succ].first);
        }

        (*result)[bb].second = bbexitval;
        visitor->compDFVal(bb, &bbexitval, false);

        // If outgoing value changed, propagate it along the CFG
        if (bbexitval == (*result)[bb].first) continue;
        (*result)[bb].first = bbexitval;

        for (pred_iterator pi = pred_begin(bb), pe = pred_end(bb); pi != pe; pi++) {
            worklist.insert(*pi);
        }
    }
}

template<class T>
void printDataflowResult(raw_ostream &out,
                         const typename DataflowResult<T>::Type &dfresult) {
    for ( typename DataflowResult<T>::Type::const_iterator it = dfresult.begin();
            it != dfresult.end(); ++it ) {
        if (it->first == NULL) out << "*";
        else it->first->dump();
        out << "\n\tin : "
            << it->second.first 
            << "\n\tout :  "
            << it->second.second
            << "\n";
    }
}







#endif /* !_DATAFLOW_H_ */
