/*
 * util.h -- small helpers shared by every part of the compiler:
 * error reporting, checked malloc, and a growable list.
 */
#ifndef SIFT_UTIL_H
#define SIFT_UTIL_H

#include <stddef.h>

/* Name of the .sift file being compiled, used in error messages. */
extern const char *g_filename;

/* Number of errors reported so far (semantic analysis reports all
 * errors before stopping, so it needs to count them). */
extern int g_error_count;

/* Print "file.sift:LINE: error: message" to stderr and count it. */
void report_error(int line, const char *fmt, ...);

/* Report an error and exit(1) straight away (lexical/syntax errors). */
void fatal_error(int line, const char *fmt, ...);

/* malloc/strdup that never return NULL (they exit on failure). */
void *xmalloc(size_t size);
void *xcalloc(size_t count, size_t size);
char *xstrdup(const char *s);

/* A growable array of pointers. Used for statement lists, argument
 * lists, parameter lists and so on. */
typedef struct {
    void **items;
    int count;
    int cap;
} List;

List *list_new(void);
void list_push(List *list, void *item);

#endif
