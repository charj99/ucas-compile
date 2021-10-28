//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//
#include <stdio.h>
#include <stdlib.h>

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;

typedef long long LL;
#define DEBUG  0
#define Diag if (DEBUG) llvm::errs()
#define MP std::make_pair
#define ALIGN sizeof(int)

class StackFrame {
   /// StackFrame maps Variable Declaration to Value
   /// Which are either integer or addresses (also represented using an Integer value)
   std::map<Decl*, LL> mVars;
   std::map<Stmt*, LL> mExprs;
   std::map<std::pair<Decl*, int>, LL> mArrs;
   /// The current stmt
   Stmt * mPC;
public:
   StackFrame() : mVars(), mExprs(), mPC(NULL), mArrs() {
   }

   void bindDecl(Decl* decl, LL val) {
      mVars[decl] = val;
   }    
   
   bool haveDeclVal(Decl* decl) {
       if (mVars.find(decl) != mVars.end())
           return true;
       return false;
   }

   LL getDeclVal(Decl * decl) {
      assert (mVars.find(decl) != mVars.end());
      return mVars.find(decl)->second;
   }
   
   void bindStmt(Stmt * stmt, LL val) {
	   mExprs[stmt] = val;
   }
   
   LL getStmtVal(Stmt * stmt) {
	   if (IntegerLiteral* IL = dyn_cast<IntegerLiteral>(stmt))
	   	return IL->getValue().getSExtValue();
	   assert (mExprs.find(stmt) != mExprs.end());
	   return mExprs[stmt];
   }

   void setPC(Stmt * stmt) {
	   mPC = stmt;
   }
   
   Stmt * getPC() {
	   return mPC;
   }

   void bindArr(Decl* decl, int idx, LL val) {
        mArrs[MP(decl, idx)] = val;
   }

   bool haveArrVal(Decl* decl, int index) {
       if (mArrs.find(MP(decl, index)) != mArrs.end())
           return true;
       return false;
   }

   LL getArrVal(Decl* decl, int index) {
    assert(mArrs.find(MP(decl, index)) != mArrs.end());
    return mArrs[MP(decl, index)];
   }

   void dumpStackFrame() {
       for (auto stmt : mExprs) {
            stmt.first->dump();
            llvm::errs() << " = " << stmt.second << "\n\n";
       }
       for (auto var : mVars) {
            var.first->dump();
            llvm::errs() << " = " << var.second << "\n\n";
       }
       for (auto arr : mArrs) {
           VarDecl* vardecl = dyn_cast<VarDecl>(arr.first.first);
           assert(vardecl);
           llvm::errs() << vardecl->getName() 
                << "[" << arr.first.second << "] = " 
                << arr.second << "\n\n";
       }
   }
};

// TODO: add char type
/*
class Heap {
    unsigned int address;
    std::map<uint32_t , int> addrValMap;
    std::map<uint32_t, uint32_t> addrSizeMap;
public:
    Heap(): address(0), addrValMap(), addrSizeMap() {}

   uint32_t Malloc(uint32_t size) {
        uint32_t ret = address;
        addrSizeMap[address] = size;
        for (uint32_t i = 0; i < size; i++) {
            addrValMap[address + i] = 0;
        }
        address += size;
        return ret;
    }

    // Q: can i free an address with in a chunck
   void Free (uint32_t addr) {
        assert(addrSizeMap.find(addr) != addrSizeMap.end());
        uint32_t size = addrSizeMap[addr];
        std::map<uint32_t , int>::iterator
        for (uint32_t i = 0; i < size; i++) {

        }
    }

   void Update(int addr, int val) ;
   int get(int addr);
};
 */

class Heap {
    std::map<LL, LL> addrValMap;
public:
    LL Malloc(int size) {
        assert(size >= 0);
        return (LL) malloc(size);
    }

    void Free (LL addr) {
        free((void*) addr);
    }

    void Update(LL addr, LL val) {
        addrValMap[addr] = val;
    }

    LL get(LL addr) {
        if (addrValMap.find(addr) == addrValMap.end()) return 0;
        return addrValMap[addr];
    }
};

class Environment {
   std::vector<StackFrame> mStack;

   FunctionDecl * mFree;				/// Declartions to the built-in functions
   FunctionDecl * mMalloc;
   FunctionDecl * mInput;
   FunctionDecl * mOutput;

   FunctionDecl * mEntry;

   Heap mHeap;
public:
   /// Get the declartions to the built-in functions
   Environment() : mStack(), mFree(NULL), mMalloc(NULL), mInput(NULL), mOutput(NULL), mEntry(NULL), mHeap() {
   }
    
   void initVars(VarDecl* vardecl) {
       IntegerLiteral* IL;
       if (vardecl->hasInit() && (IL = dyn_cast<IntegerLiteral>(vardecl->getInit()))) {
            mStack.back().bindDecl(vardecl, IL->getValue().getSExtValue());
       } else if (const ConstantArrayType* CAT = dyn_cast<ConstantArrayType>(vardecl->getType())) {
            int dim = (int) CAT->getSize().getSExtValue();
            for (int i = 0; i < dim; i++) {
                mStack.back().bindArr(vardecl, i, 0);
            }
       } 
       else mStack.back().bindDecl(vardecl, 0);
   }

   /// Initialize the Environment
   void init(TranslationUnitDecl * unit) {
	   mStack.push_back(StackFrame());
	   for (TranslationUnitDecl::decl_iterator i =unit->decls_begin(), e = unit->decls_end(); i != e; ++ i) {
		   if (FunctionDecl * fdecl = dyn_cast<FunctionDecl>(*i) ) {
			   if (fdecl->getName().equals("FREE")) mFree = fdecl;
			   else if (fdecl->getName().equals("MALLOC")) mMalloc = fdecl;
			   else if (fdecl->getName().equals("GET")) mInput = fdecl;
			   else if (fdecl->getName().equals("PRINT")) mOutput = fdecl;
			   else if (fdecl->getName().equals("main")) mEntry = fdecl;
		   } else if (VarDecl* vdecl = dyn_cast<VarDecl>(*i)) {
               initVars(vdecl);
           }
	   }
	   mStack.push_back(StackFrame());
   }

   FunctionDecl * getEntry() {
	   return mEntry;
   }
    
    LL getDeclVal(Decl* decl) {
    std::vector<StackFrame>::iterator it;
    for (it = mStack.end() - 1; ; it--) {
        if (it->haveDeclVal(decl)) return it->getDeclVal(decl);
        if (it == mStack.begin()) assert((it == mStack.begin()) && 0);
    } 
   }

   LL getArrVal(Decl* decl, int index) {
    std::vector<StackFrame>::iterator it;
    for (it = mStack.end() - 1; ; it--) {
        if (it->haveArrVal(decl, index)) return it->getArrVal(decl, index);
        if (it == mStack.begin()) assert((it == mStack.begin()) && 0);
    } 
   }

   void bindDecl(Decl* decl, LL val) {
       std::vector<StackFrame>::iterator it;
       for (it = mStack.end() - 1; ; it--) {
           if (it->haveDeclVal(decl)) {
               it->bindDecl(decl, val);
               return;
           }
           if (it == mStack.begin()) assert((it == mStack.begin()) && 0);
    } 
   }

   void bindArr(Decl* decl, int index, LL val) {
       std::vector<StackFrame>::iterator it;
       for (it = mStack.end() - 1; ; it--) {
           if (it->haveArrVal(decl, index)) {
               it->bindArr(decl, index, val);
               return;
           }
           if (it == mStack.begin()) assert((it == mStack.begin()) && 0);
    } 
   }

   Decl* arr(ArraySubscriptExpr* arrExpr, int* idxval_p) {
       Expr* base = arrExpr->getBase();
       Expr* index = arrExpr->getIdx();
       
       *idxval_p = getExactVal(index);

       if (CastExpr* cstexpr = dyn_cast<CastExpr>(base))
           base = cstexpr->getSubExpr();
        
       DeclRefExpr* declref = dyn_cast<DeclRefExpr>(base);
       assert(declref);

       Decl* decl = declref->getFoundDecl();

       return decl;
   }

   LL getExactVal(Stmt* stmt) {
           /*
           if (CastExpr* castexpr = dyn_cast<CastExpr>(stmt)) {
               stmt = castexpr->getSubExpr();
           }
           */
          
           LL val = 0;
           if (DeclRefExpr* declexpr = dyn_cast<DeclRefExpr>(stmt)) {
               Decl* decl = declexpr->getFoundDecl();
               val = getDeclVal(decl);
           } else if (ArraySubscriptExpr* arrexpr = dyn_cast<ArraySubscriptExpr>(stmt)) {
               int index = -1;
                Decl* decl = arr(arrexpr, &index);
                val = getArrVal(decl, index);
           } else val = mStack.back().getStmtVal(stmt);
           return val;
   }

    int getTypeSize(QualType type) {
        if (const auto* BT = dyn_cast<BuiltinType>(type)) {
            switch (BT->getKind()) {
                case BuiltinType::Int:
                    Diag << "int\n";
                    return sizeof(int);
                default:
                    break;
            }
        } else if (isa<PointerType>(type)) {
            if (const auto* BT = dyn_cast<BuiltinType>(type->getPointeeType())) {
                switch (BT->getKind()) {
                    case BuiltinType::Int:
                        Diag << "int*\n";
                        return sizeof(int*);
                    default:
                        break;
                }
            }
        }
        assert("unknown type" && 0);
        return -1;
    }

   void solveAddr(BinaryOperator* bop, LL* lval, LL* rval) {
       QualType lType = bop->getLHS()->getType();
       QualType rType = bop->getRHS()->getType();

       if (!bop->getType()->isPointerType()) return;

       if (lType->isPointerType()) *rval = *rval * getTypeSize(lType->getPointeeType());
       else if (rType->isPointerType()) *lval = *lval * getTypeSize(rType->getPointeeType());
   }

   /// !TODO Support comparison operation
   void binop(BinaryOperator *bop) {
	   // mStack.back().setPC(bop);
	   Expr * left = bop->getLHS();
	   Expr * right = bop->getRHS();

	   if (bop->isAssignmentOp()) {
		   LL val = getExactVal(right);
		   if (DeclRefExpr * declexpr = dyn_cast<DeclRefExpr>(left)) {
			   Decl * decl = declexpr->getFoundDecl();
               bindDecl(decl, val);
		   } else if (ArraySubscriptExpr* arrExpr = dyn_cast<ArraySubscriptExpr>(left)) {
               int index = -1;
               Decl* decl = arr(arrExpr, &index);                
               bindArr(decl, index, val);
           } else if (UnaryOperator* uop = dyn_cast<UnaryOperator>(left)) {
               assert(uop->getOpcode() == UO_Deref);
               LL addr = getExactVal(uop->getSubExpr());
               mHeap.Update(addr, val);
           } else mStack.back().bindStmt(left, val);
	   } else if (bop->isComparisonOp() 
               || bop->isAdditiveOp()
               || bop->isMultiplicativeOp()) {
           LL lval = getExactVal(left);
           LL rval = getExactVal(right);

            switch(bop->getOpcode()) {
                case BO_GT:
                    mStack.back().bindStmt(bop, lval > rval ? 1 : 0);
                    break;
                case BO_LT:
                    mStack.back().bindStmt(bop, lval < rval ? 1 : 0);
                    break;
                case BO_EQ:
                    mStack.back().bindStmt(bop, lval == rval ? 1 : 0);
                    break;
                case BO_Add:
                    solveAddr(bop, &lval, &rval);
                    mStack.back().bindStmt(bop, lval + rval);
                    break;
                case BO_Sub:
                    solveAddr(bop, &lval, &rval);
                    mStack.back().bindStmt(bop, lval - rval);
                    break;
                case BO_Mul:
                    mStack.back().bindStmt(bop, lval * rval);
                    break;
                case BO_Div:
                    mStack.back().bindStmt(bop, lval / rval);
                    break;
                default:
                    break;
            }
       }
   }

   void decl(DeclStmt * declstmt) {
	   for (DeclStmt::decl_iterator it = declstmt->decl_begin(), ie = declstmt->decl_end();
			   it != ie; ++ it) {
		   Decl * decl = *it;
		   if (VarDecl * vardecl = dyn_cast<VarDecl>(decl)) {
               initVars(vardecl);
		   }
	   }
   }
   
   void declref(DeclRefExpr * declref) {
       /*
       mStack.back().setPC(declref);
	   if (declref->getType()->isIntegerType()) {
		   Decl* decl = declref->getFoundDecl();

		   int val = getDeclVal(decl);
		   mStack.back().bindStmt(declref, val);
	   }
       */
   }

   void cast(CastExpr * castexpr) {
	   // mStack.back().setPC(castexpr);
       QualType type = castexpr->getType();
	   if (type->isIntegerType() ||
       (type->isPointerType() && !type->isFunctionPointerType())) {
		   Expr * expr = castexpr->getSubExpr();
		   LL val = getExactVal(expr);
		   mStack.back().bindStmt(castexpr, val);
	   }
   }

   /// !TODO Support Function Call
   void call(CallExpr * callexpr) {
	   mStack.back().setPC(callexpr);
	   LL val = 0;
	   FunctionDecl * callee = callexpr->getDirectCallee();
	   if (callee == mInput) {
		  llvm::errs() << "Please Input an Integer Value : ";
		  scanf("%lld", &val);

		  mStack.back().bindStmt(callexpr, val);
	   } else if (callee == mOutput) {
		   Expr * decl = callexpr->getArg(0);
		   val = getExactVal(decl);
           if (DEBUG) dumpStack();
           llvm::errs() << val;
	   } else if (callee == mMalloc) {
           Expr* decl = callexpr->getArg(0);
           val = getExactVal(decl);
           LL ptraddr = mHeap.Malloc(val);
           mStack.back().bindStmt(callexpr, ptraddr);
       } else if (callee == mFree) {
           Expr* decl = callexpr->getArg(0);
           LL ptraddr = getExactVal(decl);
           mHeap.Free(ptraddr);
       } else {
		   /// You could add your code here for Function call Return
	        StackFrame sf = StackFrame();
            for (int i = 0; i < callexpr->getNumArgs(); i++) {
                Expr* arg = callexpr->getArg(i);
                val = getExactVal(arg);
                ParmVarDecl* param = callee->getParamDecl(i); 
                sf.bindDecl(param, val);
            }
            mStack.push_back(sf);
       }
   }

   int cond(Stmt* stmt) {
        // mStack.back().setPC(stmt);
        int val = getExactVal(stmt);
        return val;
   }

   void mreturn(ReturnStmt* retstmt) {
	   mStack.back().setPC(retstmt);
       Expr* retValue = retstmt->getRetValue();
       LL val = getExactVal(retValue);
       mStack.pop_back();
       if (!mStack.back().getPC()) return;
       CallExpr* callexpr = dyn_cast<CallExpr>(mStack.back().getPC());
       assert(callexpr);
       mStack.back().bindStmt(callexpr, val);
   }

   void uniop(UnaryOperator* uop) {
	   // mStack.back().setPC(uop);
       LL val = getExactVal(uop->getSubExpr());
       assert(uop->getOpcode() == UO_Minus
       || uop->getOpcode() == UO_Deref);
       switch (uop->getOpcode()) {
           case UO_Minus:
               mStack.back().bindStmt(uop, -val);
               break;
           case UO_Deref:
               mStack.back().bindStmt(uop, mHeap.get(val));
               break;
           default:
               break;
       }

   }

   void msizeof(UnaryExprOrTypeTraitExpr* expr) {
       QualType type = expr->getArgumentType();
       int size = getTypeSize(type);
       mStack.back().bindStmt(expr, size);
   }

   void paren(ParenExpr* expr) {
       LL val = getExactVal(expr->getSubExpr());
       mStack.back().bindStmt(expr, val);
   }

   // for debug 
   void dumpStack() {
       for (auto sf : mStack) { 
           sf.dumpStackFrame();
           llvm::errs() << "******************************\n";
       }
   }
};


