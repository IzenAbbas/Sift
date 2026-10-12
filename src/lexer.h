/*
 * lexer.h -- interface to the Flex-generated lexer (lexer.l).
 */
#ifndef SIFT_LEXER_H
#define SIFT_LEXER_H

#include <stdio.h>

extern FILE *yyin;      /* file the lexer reads from */
extern int yylineno;    /* current line number (Flex keeps it up to date) */
extern char *yytext;    /* text of the current token */

int yylex(void);

/* Printable name of a token code, e.g. "PROCESS" or "IDENT". */
const char *token_name(int token);

#endif
