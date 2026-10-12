/*
 * util.c -- error reporting, checked allocation and the List type.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"

const char *g_filename = "<input>";
int g_error_count = 0;

static void print_error(int line, const char *fmt, va_list ap)
{
    fprintf(stderr, "%s:%d: error: ", g_filename, line);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    g_error_count++;
}

void report_error(int line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    print_error(line, fmt, ap);
    va_end(ap);
}

void fatal_error(int line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    print_error(line, fmt, ap);
    va_end(ap);
    exit(1);
}

void *xmalloc(size_t size)
{
    void *p = malloc(size ? size : 1);
    if (p == NULL) {
        fprintf(stderr, "sift: out of memory\n");
        exit(1);
    }
    return p;
}

void *xcalloc(size_t count, size_t size)
{
    void *p = calloc(count ? count : 1, size ? size : 1);
    if (p == NULL) {
        fprintf(stderr, "sift: out of memory\n");
        exit(1);
    }
    return p;
}

char *xstrdup(const char *s)
{
    char *copy = xmalloc(strlen(s) + 1);
    strcpy(copy, s);
    return copy;
}

List *list_new(void)
{
    List *list = xmalloc(sizeof(List));
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
    return list;
}

void list_push(List *list, void *item)
{
    if (list->count == list->cap) {
        list->cap = list->cap ? list->cap * 2 : 4;
        void **bigger = realloc(list->items, list->cap * sizeof(void *));
        if (bigger == NULL) {
            fprintf(stderr, "sift: out of memory\n");
            exit(1);
        }
        list->items = bigger;
    }
    list->items[list->count++] = item;
}
