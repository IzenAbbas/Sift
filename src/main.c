/*
 * main.c -- command-line driver for the Sift compiler.
 *
 *   ./sift run     file.sift              interpret the program (Phase 1)
 *   ./sift compile file.sift -o prog      compile to a native executable (Phase 2)
 *   ./sift compile file.sift --emit-ll    only write the LLVM IR (file.ll)
 *   ./sift tokens  file.sift              print the token stream
 *   ./sift ast     file.sift              print the syntax tree
 *
 * `compile` writes LLVM IR, then runs the LLVM tools:
 *     [opt -O2]  ->  llc (IR to object file)  ->  clang (link with runtime)
 * The tool names can be changed with the environment variables OPT, LLC
 * and CLANG (e.g. LLC=llc-18).
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "util.h"
#include "ast.h"
#include "sema.h"
#include "interp.h"
#include "codegen.h"
#include "lexer.h"
#include "parser.h"

static void usage(void)
{
    fprintf(stderr,
        "usage:\n"
        "  sift run     file.sift                interpret the program\n"
        "  sift compile file.sift [-o prog] [-O] compile to a native executable\n"
        "  sift compile file.sift --emit-ll      write file.ll and stop\n"
        "  sift tokens  file.sift                print the token stream\n"
        "  sift ast     file.sift                print the syntax tree\n");
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

/* ---------- running the LLVM tools ---------- */

/* Name of an external tool: the environment variable if set, else the
 * default (looked up in PATH). */
static char *tool(const char *env_name, const char *default_name)
{
    const char *value = getenv(env_name);
    return xstrdup((value != NULL && value[0] != '\0') ? value : default_name);
}

/* Run a program (argv[0] is looked up in PATH) and wait for it.
 * Returns 0 if it succeeded. */
static int run_command(char **argv)
{
    pid_t pid = fork();
    if (pid < 0) {
        perror("sift: fork");
        return 1;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        /* only reached if the program could not be started */
        fprintf(stderr, "sift: cannot run '%s': %s\n", argv[0], strerror(errno));
        if (errno == ENOENT)
            fprintf(stderr, "sift: is it installed? On Ubuntu: sudo apt install llvm clang\n"
                            "sift: (or set the environment variable LLC / CLANG / OPT)\n");
        _exit(127);
    }
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        perror("sift: waitpid");
        return 1;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        return 0;
    if (!(WIFEXITED(status) && WEXITSTATUS(status) == 127))
        fprintf(stderr, "sift: '%s' failed\n", argv[0]);
    return 1;
}

/* "dir" + "/" + "name" */
static char *join_path(const char *dir, const char *name)
{
    char *s = xmalloc(strlen(dir) + strlen(name) + 2);
    sprintf(s, "%s/%s", dir, name);
    return s;
}

/* The runtime object file is built by the Makefile next to the sift
 * executable, in build/sift_runtime.o. SIFT_RUNTIME overrides it. */
static char *runtime_path(void)
{
    const char *env = getenv("SIFT_RUNTIME");
    if (env != NULL && env[0] != '\0')
        return xstrdup(env);

    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n < 0) {
        perror("sift: readlink /proc/self/exe");
        exit(1);
    }
    exe[n] = '\0';
    char *slash = strrchr(exe, '/');
    if (slash != NULL)
        *slash = '\0';
    char *path = join_path(exe, "build/sift_runtime.o");
    if (access(path, R_OK) != 0) {
        fprintf(stderr, "sift: runtime '%s' not found (run make, or set SIFT_RUNTIME)\n", path);
        exit(1);
    }
    return path;
}

/* "dir/name.sift" -> "dir/name" + suffix */
static char *replace_extension(const char *path, const char *suffix)
{
    char *s = xmalloc(strlen(path) + strlen(suffix) + 1);
    strcpy(s, path);
    char *dot = strrchr(s, '.');
    char *slash = strrchr(s, '/');
    if (dot != NULL && (slash == NULL || dot > slash))
        *dot = '\0';
    strcat(s, suffix);
    return s;
}

/* Write the program's LLVM IR into the file `path`. */
static int write_ir(const char *path)
{
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        fprintf(stderr, "sift: cannot write '%s'\n", path);
        return 1;
    }
    int errors = codegen_program(g_program, f);
    fclose(f);
    return errors;
}

/* `sift compile`: .sift -> .ll -> .o -> executable */
static int cmd_compile(const char *source, int argc, char **argv)
{
    char *output = NULL;
    int emit_ll = 0, optimise = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            output = argv[++i];
        else if (strcmp(argv[i], "--emit-ll") == 0)
            emit_ll = 1;
        else if (strcmp(argv[i], "-O") == 0)
            optimise = 1;
        else
            usage();
    }

    if (emit_ll) {
        char *ll = output ? output : replace_extension(source, ".ll");
        if (write_ir(ll) != 0) {
            remove(ll);
            return 1;
        }
        return 0;
    }

    if (output == NULL)
        output = replace_extension(source, "");

    /* the .ll and .o files go in a temporary directory */
    char dir[] = "/tmp/sift-XXXXXX";
    if (mkdtemp(dir) == NULL) {
        perror("sift: mkdtemp");
        return 1;
    }
    char *ll = join_path(dir, "program.ll");
    char *opt_ll = join_path(dir, "program.opt.ll");
    char *obj = join_path(dir, "program.o");
    char *runtime = runtime_path();
    char *ll_for_llc = ll;

    int failed = write_ir(ll);
    if (!failed && optimise) {
        char *opt_argv[] = { tool("OPT", "opt"), "-O2", "-S", ll, "-o", opt_ll, NULL };
        failed = run_command(opt_argv);
        ll_for_llc = opt_ll;
    }
    if (!failed) {
        /* PIC because Ubuntu's clang links position-independent executables */
        char *llc_argv[] = { tool("LLC", "llc"), "-filetype=obj", "-relocation-model=pic",
                             ll_for_llc, "-o", obj, NULL };
        failed = run_command(llc_argv);
    }
    if (!failed) {
        char *clang_argv[] = { tool("CLANG", "clang"), obj, runtime, "-o", output, NULL };
        failed = run_command(clang_argv);
    }

    /* clean up the temporary directory */
    remove(ll);
    remove(opt_ll);
    remove(obj);
    rmdir(dir);
    return failed ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc < 3)
        usage();
    const char *cmd = argv[1];
    int is_compile = strcmp(cmd, "compile") == 0;
    if (strcmp(cmd, "run") != 0 && !is_compile &&
        strcmp(cmd, "tokens") != 0 && strcmp(cmd, "ast") != 0)
        usage();
    if (!is_compile && argc != 3)
        usage();
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

    /* Semantic errors: all of them have been reported; stop here. */
    if (sema_check(g_program) > 0)
        return 1;

    if (strcmp(cmd, "run") == 0)
        return interp_run(g_program);   /* main's result is the exit code */
    return cmd_compile(argv[2], argc - 3, argv + 3);
}
