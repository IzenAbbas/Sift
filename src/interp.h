/*
 * interp.h -- Phase 1: tree-walking interpreter.
 */
#ifndef SIFT_INTERP_H
#define SIFT_INTERP_H

#include "ast.h"

/* Run a checked program, starting at `main`.
 * Returns main's result, which becomes the exit code. */
int interp_run(Program *prog);

#endif
