# README.md

* Course:  Advanced compilation techniques tutorial 21-22 fall by Lian Li,  at UCAS

* File Structure:

  ```shell
  .
  ├── assignment1
  │   ├── judge	# auto judgement
  │   ├── src		# source files
  │   └── test	# test cases
  ├── assignment2
  │   ├── judge
  │   ├── src
  │   └── test
  └── assignment3
      ├── judge
      ├── src
      └── test
  ```

* Environments:

  * llvm-10.0.0

  * docker on dockerhub

    ```shell
    docker pull lczxxx123/llvm_10_hw:0.2
    ```

* Assignment details:

  1. Implement a basic interpreter based on Clang

     * Marking: 25 testcases are provided and each test case counts for 1 mark

     * Supported Language: We support a subset of C language constructs, as follows: 

       ```
       Type: int | char | void | *
       Operator: * | + | - | * | / | < | > | == | = | [ ] 
       Statements: IfStmt | WhileStmt | ForStmt | DeclStmt 
       Expr : BinaryOperator | UnaryOperator | DeclRefExpr | CallExpr | CastExpr 
       ```

     * We also need to support 4 external functions `int GET()`, `void * MALLOC(int)`, `void FREE (void *)`, `void PRINT(int)`, the semantics of the 4 functions are self-explanatory. 

     * A skeleton implementation ast-interpreter.tgz is provided, and you are welcome to make any changes to the implementation. The provided implementation is able to interpreter the simple program like : 

       ```
       extern int GET();
       extern void * MALLOC(int);
       extern void FREE(void *);
       extern void PRINT(int);
       int main() {
         int a;
         a = GET();
         PRINT(a);
       }
       ```

     * We provide a more formal definition of the accepted language, as follows: 

       ```
       Program : DeclList
       DeclList : Declaration DeclList | empty
       Declaration : VarDecl FuncDecl
       VarDecl : Type VarList;
       Type : BaseType | QualType
       BaseType : int | char | void
       QualType : Type * 
       VarList : ID, VarList | | ID[num], VarList | emtpy
       FuncDecl : ExtFuncDecl | FuncDefinition
       ExtFuncDecl : extern int GET(); | extern void * MALLOC(int); | extern void FREE(void *); | extern void PRINT(int);
       FuncDefinition : Type ID (ParamList) { StmtList }
       ParamList : Param, ParamList | empty
       Param : Type ID
       StmtList : Stmt, StmtList | empty
       Stmt : IfStmt | WhileStmt | ForStmt | DeclStmt | CompoundStmt | CallStmt | AssignStmt | 
       IfStmt : if (Expr) Stmt | if (Expr) Stmt else Stmt
       WhileStmt : while (Expr) Stmt
       DeclStmt : Type VarList;
       AssignStmt : DeclRefExpr = Expr;
       CallStmt : CallExpr;
       CompoundStmt : { StmtList }
       ForStmt : for ( Expr; Expr; Expr) Stmt
       Expr : BinaryExpr | UnaryExpr | DeclRefExpr | CallExpr | CastExpr | ArrayExpr | DerefExpr | (Expr) | num
       BinaryExpr : Expr BinOP Expr
       BinaryOP : + | - | * | / | < | > | ==
       UnaryExpr : - Expr
       DeclRefExpr : ID
       CallExpr : DeclRefExpr (ExprList)
       ExprList : Expr, ExprList | empty
       CastExpr : (Type) Expr
       ArrayExpr : DeclRefExpr [Expr]
       DerefExpr : * DeclRefExpr
       ```

  2. Implement a basic Call Graph builder

     * output format：

       ```
       ${line} : ${func_name1}, ${func_name2} 
       ```

       Note that ​\${line} is unique in output.

     * Given the test file:

       ```c
       #include <stdio.h>
       
       int add(int a, int b) {
          return a+b;
       }
       
       int sub(int a, int b) {
          return a-b;
       }
       
       int foo(int a, int b, int (*a_fptr)(int, int)) {
           return a_fptr(a, b);
       }
       
       
       int main() {
           int (*a_fptr)(int, int) = add;
           int (*s_fptr)(int, int) = sub;
           int (*t_fptr)(int, int) = 0;
       
           char x;
           int op1, op2;
           fprintf(stderr, "Please input num1 +/- num2 \n");
       
           fscanf(stdin, "%d %c %d", &op1, &x, &op2);
       
           if (x == '+') {
              t_fptr = a_fptr;
           }
       
           if (x == '-') {
              t_fptr = s_fptr;
           }
       
           if (t_fptr != NULL) {
              unsigned result = foo(op1, op2, t_fptr);
              fprintf (stderr, "Result is %d \n", result);
           } else {
              fprintf (stderr, "Unrecoganised operation %c", x);
           }
           return 0;
       }
       
       ```

     * The output should be:

       ```
       12 : add, sub
       23 : fprintf
       25 : fscanf
       36 : foo
       37 : fprintf
       39 : fprintf
       ```

  3. Implement a flow-sensitive, field- and context-insensitive algorithm to compute the points-to set for each variable at every call instructions. And print the callee functions each distinct program point.

     * The printed format should be:

       ```
       ${line} : ${func_name1}, ${func_name2}
       ```

        Here ${line} is unique in output.

     * One approach is to extend the provided dataflow analysis framework as follows: 

       * Implement your representation of points-to sets
       * Implement the transfer function for each instruction
       * Implement the MEET operator
       * Extend the control flow graph to be inter-procedural

       Note that we do not require the analysis to be context-sensitive or field-sensitive.

     * Give the test file:

       ```c
       #include <stdlib.h>
       struct fpstruct {
           int (*t_fptr)(int,int);
       };
              
       int clever(int x) {
           int (*a_fptr)(int, int) = plus;
           int (*s_fptr)(int, int) = minus;
           int op1=1, op2=2;
           struct fpstruct * t1 = malloc(sizeof (struct fpstruct));
           if (x == 3) {
               t1->t_fptr = a_fptr;  
           } else {
               t1->t_fptr = s_fptr;
           } 
           unsigned result = t1->t_fptr(op1, op2);
           return 0;
       }
       ```

     * Your output should be：

       ```
       9 : malloc
       15 : plus, minus
       ```