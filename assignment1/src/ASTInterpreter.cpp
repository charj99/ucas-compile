//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool --------------===//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"
#include "clang/AST/ASTContext.h"

using namespace clang;

#include "Environment.h"

class InterpreterVisitor : 
   public EvaluatedExprVisitor<InterpreterVisitor> {
public:
   explicit InterpreterVisitor(const ASTContext &context, Environment * env)
   : EvaluatedExprVisitor(context), mEnv(env), mCtx(context) {}
   virtual ~InterpreterVisitor() {}

   virtual void VisitBinaryOperator (BinaryOperator * bop) {
       Diag << "solving binary operator expr\n";
	   VisitStmt(bop);
       mEnv->binop(bop);
       Diag << "finished solving binary operator expr\n";
   }
   virtual void VisitDeclRefExpr(DeclRefExpr * expr) {
       Diag << "solving decl ref expr\n";
	   VisitStmt(expr);
	   mEnv->declref(expr);
       Diag << "finished solving decl ref expr\n";
   }
   virtual void VisitCastExpr(CastExpr * expr) {
       Diag << "solving cast expr\n";
	   VisitStmt(expr);
	   mEnv->cast(expr);
       Diag << "finished solving cast expr\n";
   }
   virtual void VisitCallExpr(CallExpr * call) {
       Diag << "solving call expr\n";
	   VisitStmt(call);
	   mEnv->call(call);

       FunctionDecl* callee = call->getDirectCallee();
       if (callee->hasBody()) { 
           VisitStmt(callee->getBody());
       }
       Diag << "finished solving call expr\n";
   }

   virtual void VisitDeclStmt(DeclStmt * declstmt) {
       Diag << "solving decl stmt\n";
	   mEnv->decl(declstmt);
       Diag << "finished solving decl stmt\n";
   }
    
   Stmt* wrapStmt(Stmt* stmt) {
       if (!isa<CompoundStmt>(stmt)) 
           return CompoundStmt::Create(mCtx, stmt, SourceLocation(), SourceLocation());
       return stmt;
   }

   virtual void VisitIfStmt(IfStmt* ifstmt) {
        Diag << "solving if stmt\n";

        Stmt* cond = wrapStmt(ifstmt->getCond());
        VisitStmt(cond);

        int val = mEnv->cond(ifstmt->getCond());
        Stmt* br = NULL;
        
        if (val) { 
            Diag << "\ttrue branch\n";
            br = ifstmt->getThen();
        } else if (ifstmt->hasElseStorage()) {
            Diag << "\tfalse branch\n";
            br = ifstmt->getElse();
        }
        
        if (br) {
            VisitStmt(wrapStmt(br));
        }
       Diag << "finished solving if stmt\n";
   }

   virtual void VisitReturnStmt(ReturnStmt* retstmt) {
        Diag << "solving return stmt\n";
        VisitStmt(retstmt);
        mEnv->mreturn(retstmt);
       Diag << "finished solving return stmt\n";
   }
   
   virtual void VisitUnaryOperator (UnaryOperator * uop) {
       Diag << "solving unary operator expr\n";
	   VisitStmt(uop);
       mEnv->uniop(uop);
       Diag << "finished solving unary operator expr\n";
   }

   virtual void VisitWhileStmt(WhileStmt* wstmt) {
       Diag << "solving while stmt\n";
    
       Stmt* cond = wrapStmt(wstmt->getCond());
        VisitStmt(cond);
        
        int val = mEnv->cond(wstmt->getCond());

        while (val) {
            VisitStmt(wstmt->getBody());
            VisitStmt(cond);
            val = mEnv->cond(wstmt->getCond());
        }
        Diag << "finished solving while stmt\n";
   }

   virtual void VisitForStmt(ForStmt* forstmt) {
       Diag << "solving for stmt\n";
       Stmt* init = forstmt->getInit();
       if (init) VisitStmt(wrapStmt(init));

       Stmt* cond = forstmt->getCond();
       if (cond) VisitStmt(wrapStmt(cond));
      
       int val = mEnv->cond(forstmt->getCond());
       Expr* inc = forstmt->getInc();
       while (val) {
           VisitStmt(forstmt->getBody());
           if (inc) VisitStmt(wrapStmt(inc));
           if (cond) VisitStmt(wrapStmt(cond));
            val = mEnv->cond(forstmt->getCond());
       }
       Diag << "finished solving for stmt\n";
   }

   virtual void VisitArraySubscriptExpr(ArraySubscriptExpr* arrExpr) {
       Diag << "solving array subscript expr\n";
       Expr* index = arrExpr->getIdx();
       VisitStmt(wrapStmt(index));
       Diag << "finished solving array subscript expr\n";
   }

   virtual void VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr* expr) {
       Diag << "solving unary expr or type trait expr\n";
       assert(expr->getKind() == UETT_SizeOf && expr->isArgumentType());
       mEnv->msizeof(expr);
       Diag << "finished solving unary expr or type trait expr\n";
   }

   virtual void VisitParenExpr(ParenExpr* expr) {
       Diag << "solving paren expr\n";
       VisitStmt(expr);
       mEnv->paren(expr);
       Diag << "finished solving paren expr\n";
   }

private:
   Environment * mEnv;
   const ASTContext& mCtx;
};

class InterpreterConsumer : public ASTConsumer {
public:
   explicit InterpreterConsumer(const ASTContext& context) : mEnv(),
   	   mVisitor(context, &mEnv) {
   }
   virtual ~InterpreterConsumer() {}

   virtual void HandleTranslationUnit(clang::ASTContext &Context) {
	   TranslationUnitDecl * decl = Context.getTranslationUnitDecl();
	   mEnv.init(decl);

	   FunctionDecl * entry = mEnv.getEntry();
	   mVisitor.VisitStmt(entry->getBody());
  }
private:
   Environment mEnv;
   InterpreterVisitor mVisitor;
};

class InterpreterClassAction : public ASTFrontendAction {
public: 
  virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
    clang::CompilerInstance &Compiler, llvm::StringRef InFile) {
    return std::unique_ptr<clang::ASTConsumer>(
        new InterpreterConsumer(Compiler.getASTContext()));
  }
};

int main (int argc, char ** argv) {
   if (argc > 1) {
       clang::tooling::runToolOnCode(std::unique_ptr<clang::FrontendAction>(new InterpreterClassAction), argv[1]);
   }
}
