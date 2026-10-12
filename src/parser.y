/*
 * parser.y -- Bison grammar for Sift.
 *
 * Milestone 1: only the token declarations live here, so that Bison
 * generates parser.h with the token numbers the lexer returns.
 * The real grammar is added in milestone 2.
 */

%{
#include <stdio.h>
#include "util.h"
#include "lexer.h"

void yyerror(const char *msg);
%}

%define parse.error verbose

%union {
    int   num;      /* INT literal value */
    char *str;      /* identifier name (malloc'd) */
}

/* Keywords. The string after each token is the name Bison uses in
 * error messages, e.g.  unexpected "end", expecting ";"  */
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

%%

program
    : %empty        /* grammar comes in milestone 2 */
    ;

%%

void yyerror(const char *msg)
{
    report_error(yylineno, "%s", msg);
}
