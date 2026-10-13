/*
 * main.c -- command-line driver for the Sift compiler.
 *
 *   ./sift tokens file.sift     print the token stream
 *   ./sift run    file.sift     interpret the program   (milestone 3+)
 *   ./sift ast    file.sift     print the syntax tree   (milestone 2+)
 *   ./sift compile file.sift    compile to native code  (milestone 9+)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "ast.h"
#include "lexer.h"
#include "parser.h"

static void usage(void)
{
    fprintf(stderr,
        "usage:\n"
        "  sift run     file.sift              interpret the program\n"
        "  sift compile file.sift -o prog      compile to a native executable\n"
        "  sift compile file.sift --emit-ll    write file.ll and stop\n"
        "  sift tokens  file.sift              print the token stream\n"
        "  sift ast     file.sift              print the syntax tree\n");
    exit(1);
}

/* Open the source file and point the lexer at it. */
static void open_source(const char *path)
{
    yyin = fopen(path, "r");
    if (yyin == NULL) {
        fprintf(stderr, "sift: cannot open '%s'\n", path);
        exit(1);
    }
    g_filename = path;
}

/* `sift tokens`: print one token per line as  LINE  NAME  text. */
static int cmd_tokens(void)
{
    int tok;
    while ((tok = yylex()) != 0) {
        printf("%d\t%s\t%s\n", yylineno, token_name(tok), yytext);
        if (tok == IDENT)
            free(yylval.str);
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3)
        usage();
    const char *cmd = argv[1];
    open_source(argv[2]);

    if (strcmp(cmd, "tokens") == 0)
        return cmd_tokens();

    /* Every other command needs the parsed program. A syntax error
     * stops inside yyparse (see yyerror in parser.y). */
    if (yyparse() != 0)
        return 1;
    fclose(yyin);

    if (strcmp(cmd, "ast") == 0) {
        ast_print(g_program);
        return 0;
    }
    if (strcmp(cmd, "run") == 0 || strcmp(cmd, "compile") == 0) {
        fprintf(stderr, "sift: '%s' is not implemented yet\n", cmd);
        return 1;
    }
    usage();
    return 1;
}
