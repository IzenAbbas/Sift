/*
 * parser.y -- Bison grammar for Sift.
 *
 * Each grammar rule's action builds a piece of the AST (see ast.h).
 * The pipeline operator |> is desugared here into ordinary calls, so
 * later stages never see it (see expr_pipe in ast.c).
 *
 * Operator precedence is given with %left / %right / %nonassoc below,
 * lowest first. The grammar has no shift/reduce or reduce/reduce
 * conflicts (the Makefile makes Bison fail if there are any).
 */

%code requires {
#include "ast.h"
}

%code provides {
/* The parsed program; set when yyparse() succeeds. */
extern Program *g_program;
}

%{
#include <stdio.h>
#include "util.h"
#include "lexer.h"

void yyerror(const char *msg);
%}

%locations
%define parse.error verbose

%union {
    int      num;      /* INT literal value */
    char    *str;      /* identifier name (malloc'd) */
    Type     type;
    Expr    *expr;
    Stmt    *stmt;
    Block   *block;
    Param   *param;
    Process *proc;
    List    *list;
}

/* Keywords. The string after each token is the name Bison uses in
 * error messages, e.g.  unexpected end, expecting ;  */
%token PROCESS "process" USING "using" GIVES "gives" BEGIN_ "begin" END "end"
%token DECLARE "declare" SET "set" GIVE "give" BACK "back"
%token CHECK "check" ELSE "else" OTHERWISE "otherwise"
%token REPEAT "repeat" WHILE "while" FROM "from" BELOW "below"
%token SWEEP "sweep" IN "in" STOP "stop" SKIP "skip" SHOW "show"
%token AND "and" OR "or" NOT "not" WITHIN "within" IS "is" VOID "void"
%token YES "yes" NO "no" COUNT "count" VAL "val" FLAG "flag" BUFFER "buffer"

/* Operators and punctuation */
%token PIPE "|>" DOTDOT ".." EQ "=" NE "!=" LT "<" LE "<=" GT ">" GE ">="
%token PLUS "+" MINUS "-" STAR "*" SLASH "/" PERCENT "%"
%token LPAREN "(" RPAREN ")" LBRACKET "[" RBRACKET "]"
%token COMMA "," COLON ":" SEMI ";"

/* Tokens with a value */
%token <num> INT "integer"
%token <str> IDENT "identifier"

%type <list>  procs param_list params_opt stmts args_opt arg_list
%type <proc>  process
%type <param> param
%type <type>  type gives_opt
%type <block> block else_opt
%type <stmt>  stmt check_head
%type <expr>  expr primary call

/* Precedence, lowest first. */
%precedence PIPE
%left       OR
%left       AND
%precedence NOT
%nonassoc   EQ NE LT LE GT GE WITHIN IS
%left       PLUS MINUS
%left       STAR SLASH PERCENT
%precedence UMINUS

%start program

%%

program
    : procs                         { g_program = xmalloc(sizeof(Program));
                                      g_program->procs = $1; }
    ;

procs
    : %empty                        { $$ = list_new(); }
    | procs process                 { list_push($1, $2); $$ = $1; }
    ;

process
    : PROCESS IDENT params_opt gives_opt block
                                    { $$ = process_new(@1.first_line, $2, $3, $4, $5); }
    ;

params_opt
    : %empty                        { $$ = list_new(); }
    | USING param_list              { $$ = $2; }
    ;

param_list
    : param                         { $$ = list_new(); list_push($$, $1); }
    | param_list COMMA param        { list_push($1, $3); $$ = $1; }
    ;

param
    : IDENT COLON type              { $$ = param_new(@1.first_line, $1, $3); }
    ;

gives_opt
    : %empty                        { $$ = make_type(TY_NONE, -1); }
    | GIVES type                    { $$ = $2; }
    ;

type
    : VAL                           { $$ = make_type(TY_VAL, -1); }
    | FLAG                          { $$ = make_type(TY_FLAG, -1); }
    | BUFFER                        { $$ = make_type(TY_BUFFER, -1); }
    | BUFFER LBRACKET INT RBRACKET  { $$ = make_type(TY_BUFFER, $3); }
    ;

block
    : BEGIN_ stmts END              { $$ = block_new($2, @3.first_line); }
    ;

stmts
    : %empty                        { $$ = list_new(); }
    | stmts stmt                    { list_push($1, $2); $$ = $1; }
    ;

stmt
    : DECLARE IDENT COLON type SEMI
        { $$ = stmt_new(STMT_DECLARE, @1.first_line);
          $$->name = $2; $$->decl_type = $4; }
    | DECLARE IDENT COLON type EQ expr SEMI
        { $$ = stmt_new(STMT_DECLARE, @1.first_line);
          $$->name = $2; $$->decl_type = $4; $$->value = $6; }
    | DECLARE IDENT COLON type EQ LBRACKET arg_list RBRACKET SEMI
        { $$ = stmt_new(STMT_DECLARE, @1.first_line);
          $$->name = $2; $$->decl_type = $4; $$->list = $7; }
    | SET IDENT EQ expr SEMI
        { $$ = stmt_new(STMT_SET, @1.first_line);
          $$->name = $2; $$->value = $4; }
    | SET IDENT LBRACKET expr RBRACKET EQ expr SEMI
        { $$ = stmt_new(STMT_SET, @1.first_line);
          $$->name = $2; $$->index = $4; $$->value = $7; }
    | check_head else_opt
        { $$ = $1; $$->else_block = $2; }
    | REPEAT WHILE expr block
        { $$ = stmt_new(STMT_WHILE, @1.first_line);
          $$->value = $3; $$->body = $4; }
    | REPEAT IDENT FROM expr BELOW expr block
        { $$ = stmt_new(STMT_REPEAT, @1.first_line);
          $$->name = $2; $$->value = $4; $$->limit = $6; $$->body = $7; }
    | SWEEP IDENT IN expr block
        { $$ = stmt_new(STMT_SWEEP, @1.first_line);
          $$->name = $2; $$->value = $4; $$->body = $5; }
    | GIVE BACK SEMI
        { $$ = stmt_new(STMT_GIVE, @1.first_line); }
    | GIVE BACK expr SEMI
        { $$ = stmt_new(STMT_GIVE, @1.first_line); $$->value = $3; }
    | STOP SEMI
        { $$ = stmt_new(STMT_STOP, @1.first_line); }
    | SKIP SEMI
        { $$ = stmt_new(STMT_SKIP, @1.first_line); }
    | SHOW expr SEMI
        { $$ = stmt_new(STMT_SHOW, @1.first_line); $$->value = $2; }
    | call SEMI
        { $$ = stmt_new(STMT_CALL, @1.first_line); $$->value = $1; }
    ;

/* check c1 begin..end  else check c2 begin..end ...  (the `otherwise`
 * part is added by the stmt rule above). Each branch adds one condition
 * and one block to the same STMT_CHECK node. */
check_head
    : CHECK expr block
        { $$ = stmt_new(STMT_CHECK, @1.first_line);
          $$->conds = list_new(); $$->blocks = list_new();
          list_push($$->conds, $2); list_push($$->blocks, $3); }
    | check_head ELSE CHECK expr block
        { list_push($1->conds, $4); list_push($1->blocks, $5); $$ = $1; }
    ;

else_opt
    : %empty                        { $$ = NULL; }
    | OTHERWISE block               { $$ = $2; }
    ;

expr
    : expr PIPE IDENT               { $$ = expr_pipe(@2.first_line, $1, $3, NULL); }
    | expr PIPE IDENT LPAREN args_opt RPAREN
                                    { $$ = expr_pipe(@2.first_line, $1, $3, $5); }
    | expr OR expr                  { $$ = expr_binary(@2.first_line, OP_OR, $1, $3); }
    | expr AND expr                 { $$ = expr_binary(@2.first_line, OP_AND, $1, $3); }
    | NOT expr                      { $$ = expr_not(@1.first_line, $2); }
    | expr EQ expr                  { $$ = expr_binary(@2.first_line, OP_EQ, $1, $3); }
    | expr NE expr                  { $$ = expr_binary(@2.first_line, OP_NE, $1, $3); }
    | expr LT expr                  { $$ = expr_binary(@2.first_line, OP_LT, $1, $3); }
    | expr LE expr                  { $$ = expr_binary(@2.first_line, OP_LE, $1, $3); }
    | expr GT expr                  { $$ = expr_binary(@2.first_line, OP_GT, $1, $3); }
    | expr GE expr                  { $$ = expr_binary(@2.first_line, OP_GE, $1, $3); }
    | expr WITHIN expr DOTDOT expr %prec WITHIN
                                    { $$ = expr_within(@2.first_line, $1, $3, $5); }
    | expr IS VOID                  { $$ = expr_isvoid(@2.first_line, $1); }
    | expr PLUS expr                { $$ = expr_binary(@2.first_line, OP_ADD, $1, $3); }
    | expr MINUS expr               { $$ = expr_binary(@2.first_line, OP_SUB, $1, $3); }
    | expr STAR expr                { $$ = expr_binary(@2.first_line, OP_MUL, $1, $3); }
    | expr SLASH expr               { $$ = expr_binary(@2.first_line, OP_DIV, $1, $3); }
    | expr PERCENT expr             { $$ = expr_binary(@2.first_line, OP_MOD, $1, $3); }
    | MINUS expr %prec UMINUS       { $$ = expr_neg(@1.first_line, $2); }
    | primary                       { $$ = $1; }
    ;

primary
    : INT                           { $$ = expr_int(@1.first_line, $1); }
    | YES                           { $$ = expr_bool(@1.first_line, 1); }
    | NO                            { $$ = expr_bool(@1.first_line, 0); }
    | VOID                          { $$ = expr_void(@1.first_line); }
    | COUNT IDENT                   { $$ = expr_count(@1.first_line, expr_var(@2.first_line, $2)); }
    | COUNT call                    { $$ = expr_count(@1.first_line, $2); }
    | COUNT LPAREN expr RPAREN      { $$ = expr_count(@1.first_line, $3); }
    | IDENT                         { $$ = expr_var(@1.first_line, $1); }
    | IDENT LBRACKET expr RBRACKET  { $$ = expr_index(@1.first_line, $1, $3); }
    | call                          { $$ = $1; }
    | LPAREN expr RPAREN            { $$ = $2; }
    ;

call
    : IDENT LPAREN args_opt RPAREN  { $$ = expr_call(@1.first_line, $1, $3); }
    ;

args_opt
    : %empty                        { $$ = list_new(); }
    | arg_list                      { $$ = $1; }
    ;

arg_list
    : expr                          { $$ = list_new(); list_push($$, $1); }
    | arg_list COMMA expr           { list_push($1, $3); $$ = $1; }
    ;

%%

Program *g_program = NULL;

void yyerror(const char *msg)
{
    /* Syntax errors: report the first one and stop. */
    fatal_error(yylloc.first_line, "%s", msg);
}
