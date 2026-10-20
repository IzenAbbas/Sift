/*
 * runtime.c -- the small C runtime linked into every compiled Sift
 * program.
 *
 * The generated LLVM IR calls these functions for output, runtime
 * checks and buffer storage. The C `main` below starts the program by
 * calling the Sift `main` process, which the code generator emits as
 * the function `sift_main`.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* The value used for `void` (a missing value). */
#define VOID_VALUE INT32_MIN

int32_t sift_main(void);   /* generated from the Sift program */

/* Print "runtime error: line N: ..." and stop with exit code 1. */
static void runtime_error(int32_t line, const char *message)
{
    fflush(stdout);
    fprintf(stderr, "runtime error: line %d: %s\n", (int)line, message);
    exit(1);
}

/* show for a val: a void value prints as "void". */
void sift_show_val(int32_t value)
{
    if (value == VOID_VALUE)
        printf("void\n");
    else
        printf("%d\n", (int)value);
}

/* show for a flag. */
void sift_show_flag(bool value)
{
    printf("%s\n", value ? "yes" : "no");
}

/* Called before every `/`. */
void sift_check_div(int32_t divisor, int32_t line)
{
    if (divisor == 0)
        runtime_error(line, "division by zero");
}

/* Called before every `%`. */
void sift_check_mod(int32_t divisor, int32_t line)
{
    if (divisor == 0)
        runtime_error(line, "modulo by zero");
}

/* Called before every buffer element read or write. */
void sift_check_index(int32_t index, int32_t length, int32_t line)
{
    if (index < 0 || index >= length) {
        fflush(stdout);
        fprintf(stderr, "runtime error: line %d: index %d out of bounds for buffer of length %d\n",
                (int)line, (int)index, (int)length);
        exit(1);
    }
}

/* Storage for a new buffer of n elements, all 0. Never freed: Sift
 * programs are short-lived and a process may give back a buffer it
 * declared. */
int32_t *sift_new_buffer(int32_t n)
{
    int32_t *data = calloc(n > 0 ? (size_t)n : 1, sizeof(int32_t));
    if (data == NULL) {
        fprintf(stderr, "runtime error: out of memory\n");
        exit(1);
    }
    return data;
}

/* Reached if a `gives` process runs off its end (sema normally makes
 * this impossible). */
void sift_missing_give_back(const char *process, int32_t line)
{
    fflush(stdout);
    fprintf(stderr, "runtime error: line %d: process '%s' ended without giving back a value\n",
            (int)line, process);
    exit(1);
}

int main(void)
{
    int32_t result = sift_main();
    fflush(stdout);
    return (int)result;     /* main's result is the exit code */
}
