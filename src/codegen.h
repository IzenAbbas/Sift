/*
 * codegen.h -- Phase 2: LLVM IR generation.
 */
#ifndef SIFT_CODEGEN_H
#define SIFT_CODEGEN_H

#include <stdio.h>
#include "ast.h"

/* Write the checked program as textual LLVM IR to `out`.
 * Returns the number of errors (0 on success). */
int codegen_program(Program *prog, FILE *out);

#endif
