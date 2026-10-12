/*
 * lexer.l -- Flex specification for Sift.
 *
 * Turns the source text into tokens for the parser. Each rule returns a
 * token code defined by Bison in parser.h. Identifiers and integers also
 * put their value in yylval.
 */

%option noyywrap nounput noinput yylineno

%{
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "lexer.h"
#include "parser.h"
%}

DIGIT   [0-9]
IDSTART [A-Za-z_]
IDCHAR  [A-Za-z0-9_]

%%

[ \t\r\n]+          { /* skip whitespace; yylineno counts the newlines */ }
"--"[^\n]*          { /* comment runs to the end of the line */ }

 /* Keywords. Flex picks the longest match, and on a tie the rule listed
  * first, so these come before the identifier rule. */
"process"           { return PROCESS; }
"using"             { return USING; }
"gives"             { return GIVES; }
"begin"             { return BEGIN_; }
"end"               { return END; }
"declare"           { return DECLARE; }
"set"               { return SET; }
"give"              { return GIVE; }
"back"              { return BACK; }
"check"             { return CHECK; }
"else"              { return ELSE; }
"otherwise"         { return OTHERWISE; }
"repeat"            { return REPEAT; }
"while"             { return WHILE; }
"from"              { return FROM; }
"below"             { return BELOW; }
"sweep"             { return SWEEP; }
"in"                { return IN; }
"stop"              { return STOP; }
"skip"              { return SKIP; }
"show"              { return SHOW; }
"and"               { return AND; }
"or"                { return OR; }
"not"               { return NOT; }
"within"            { return WITHIN; }
"is"                { return IS; }
"void"              { return VOID; }
"yes"               { return YES; }
"no"                { return NO; }
"count"             { return COUNT; }
"val"               { return VAL; }
"flag"              { return FLAG; }
"buffer"            { return BUFFER; }

 /* Operators and punctuation. Two-character ones are matched first
  * automatically because Flex prefers the longest match. */
"|>"                { return PIPE; }
".."                { return DOTDOT; }
"!="                { return NE; }
"<="                { return LE; }
">="                { return GE; }
"="                 { return EQ; }
"<"                 { return LT; }
">"                 { return GT; }
"+"                 { return PLUS; }
"-"                 { return MINUS; }
"*"                 { return STAR; }
"/"                 { return SLASH; }
"%"                 { return PERCENT; }
"("                 { return LPAREN; }
")"                 { return RPAREN; }
"["                 { return LBRACKET; }
"]"                 { return RBRACKET; }
","                 { return COMMA; }
":"                 { return COLON; }
";"                 { return SEMI; }

{DIGIT}+            {
                        /* Integer literal. Negative numbers come from unary
                         * minus, so the literal itself must fit in 0..2^31-1. */
                        if (strlen(yytext) > 10 || strtoll(yytext, NULL, 10) > 2147483647LL)
                            fatal_error(yylineno, "integer literal '%s' is too large", yytext);
                        yylval.num = (int)strtol(yytext, NULL, 10);
                        return INT;
                    }

{IDSTART}{IDCHAR}*  { yylval.str = xstrdup(yytext); return IDENT; }

.                   { fatal_error(yylineno, "unexpected character '%s'", yytext); }

%%

/* Printable names for `./sift tokens`. */
const char *token_name(int token)
{
    switch (token) {
    case PROCESS:   return "PROCESS";
    case USING:     return "USING";
    case GIVES:     return "GIVES";
    case BEGIN_:    return "BEGIN";
    case END:       return "END";
    case DECLARE:   return "DECLARE";
    case SET:       return "SET";
    case GIVE:      return "GIVE";
    case BACK:      return "BACK";
    case CHECK:     return "CHECK";
    case ELSE:      return "ELSE";
    case OTHERWISE: return "OTHERWISE";
    case REPEAT:    return "REPEAT";
    case WHILE:     return "WHILE";
    case FROM:      return "FROM";
    case BELOW:     return "BELOW";
    case SWEEP:     return "SWEEP";
    case IN:        return "IN";
    case STOP:      return "STOP";
    case SKIP:      return "SKIP";
    case SHOW:      return "SHOW";
    case AND:       return "AND";
    case OR:        return "OR";
    case NOT:       return "NOT";
    case WITHIN:    return "WITHIN";
    case IS:        return "IS";
    case VOID:      return "VOID";
    case YES:       return "YES";
    case NO:        return "NO";
    case COUNT:     return "COUNT";
    case VAL:       return "VAL";
    case FLAG:      return "FLAG";
    case BUFFER:    return "BUFFER";
    case PIPE:      return "PIPE";
    case DOTDOT:    return "DOTDOT";
    case EQ:        return "EQ";
    case NE:        return "NE";
    case LT:        return "LT";
    case LE:        return "LE";
    case GT:        return "GT";
    case GE:        return "GE";
    case PLUS:      return "PLUS";
    case MINUS:     return "MINUS";
    case STAR:      return "STAR";
    case SLASH:     return "SLASH";
    case PERCENT:   return "PERCENT";
    case LPAREN:    return "LPAREN";
    case RPAREN:    return "RPAREN";
    case LBRACKET:  return "LBRACKET";
    case RBRACKET:  return "RBRACKET";
    case COMMA:     return "COMMA";
    case COLON:     return "COLON";
    case SEMI:      return "SEMI";
    case INT:       return "INT";
    case IDENT:     return "IDENT";
    default:        return "UNKNOWN";
    }
}
