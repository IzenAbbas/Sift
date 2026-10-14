/*
 * sema.h -- semantic analysis: scopes, symbol tables and type checking.
 */
#ifndef SIFT_SEMA_H
#define SIFT_SEMA_H

#include "ast.h"

/* Check the whole program. Reports every semantic error it finds
 * (through report_error) and returns how many there were.
 * On success it has also filled in the "filled in by sema" fields of
 * the AST: expression types, variable slots and called processes. */
int sema_check(Program *prog);

/* The `main` process (valid after sema_check succeeded). */
Process *sema_main(Program *prog);

#endif
