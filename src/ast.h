/*
 * ast.h -- the abstract syntax tree.
 *
 * The parser builds this tree, sema.c checks it (and fills in the
 * "filled in by sema" fields), and then either interp.c runs it or
 * codegen.c turns it into LLVM IR.
 */
#ifndef SIFT_AST_H
#define SIFT_AST_H

#include "util.h"

/* ---------- types ---------- */

typedef enum {
    TY_VAL,      /* 32-bit integer */
    TY_FLAG,     /* yes / no */
    TY_BUFFER,   /* array of val + its length */
    TY_NONE,     /* "no value": a process without `gives` */
    TY_ERROR     /* used by sema after an error, to avoid repeated messages */
} TypeKind;

/* A type as written in the source. For `buffer[8]` size is 8;
 * for every other type (and plain `buffer`) size is -1. */
typedef struct {
    TypeKind kind;
    int size;
} Type;

/* ---------- expressions ---------- */

typedef enum {
    EXPR_INT,      /* 42                         ival            */
    EXPR_BOOL,     /* yes / no                   ival = 1 / 0    */
    EXPR_VOID,     /* void                                       */
    EXPR_VAR,      /* x                          name            */
    EXPR_INDEX,    /* x[e]                       name, left = e  */
    EXPR_CALL,     /* f(a, b)                    name, args      */
    EXPR_COUNT,    /* count e                    left            */
    EXPR_NEG,      /* -e                         left            */
    EXPR_NOT,      /* not e                      left            */
    EXPR_BINARY,   /* l op r                     op, left, right */
    EXPR_WITHIN,   /* x within lo..hi            left, right, third */
    EXPR_ISVOID    /* e is void                  left            */
} ExprKind;

typedef enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE,
    OP_AND, OP_OR
} BinOp;

struct Process;

typedef struct Expr {
    ExprKind kind;
    int line;
    int ival;
    char *name;
    BinOp op;
    struct Expr *left, *right, *third;
    List *args;                 /* of Expr*, for EXPR_CALL */

    /* filled in by sema */
    TypeKind type;              /* type of the value this expression produces */
    int slot;                   /* EXPR_VAR / EXPR_INDEX: variable's slot number */
    struct Process *proc;       /* EXPR_CALL: the process being called */
} Expr;

/* ---------- statements ---------- */

typedef enum {
    STMT_DECLARE,  /* declare name: type [= init | = [list]]            */
    STMT_SET,      /* set name[index] = value   (index may be NULL)      */
    STMT_CHECK,    /* check c1 b1 else check c2 b2 ... otherwise ob      */
    STMT_WHILE,    /* repeat while cond body                             */
    STMT_REPEAT,   /* repeat name from start below limit body            */
    STMT_SWEEP,    /* sweep name in buf body                             */
    STMT_GIVE,     /* give back [value]                                  */
    STMT_STOP,
    STMT_SKIP,
    STMT_SHOW,     /* show value                                         */
    STMT_CALL      /* f(args);   value is the EXPR_CALL                  */
} StmtKind;

typedef struct Block {
    List *stmts;          /* of Stmt* */
    int end_line;         /* line of the closing `end` */
} Block;

typedef struct Stmt {
    StmtKind kind;
    int line;
    char *name;           /* DECLARE, SET, REPEAT, SWEEP: variable name */
    Type decl_type;       /* DECLARE */
    Expr *value;          /* DECLARE init, SET value, GIVE, SHOW, CALL, WHILE cond,
                             REPEAT start, SWEEP buffer */
    Expr *index;          /* SET: the index, or NULL for a whole variable */
    Expr *limit;          /* REPEAT: the `below` value */
    List *list;           /* DECLARE with [a, b, c]: list of Expr*, else NULL */
    List *conds;          /* CHECK: one condition per branch (Expr*) */
    List *blocks;         /* CHECK: one block per branch (Block*) */
    Block *else_block;    /* CHECK: the `otherwise` block, or NULL */
    Block *body;          /* WHILE, REPEAT, SWEEP */

    /* filled in by sema */
    int slot;             /* DECLARE, SET, REPEAT, SWEEP: the variable's slot */
} Stmt;

/* ---------- processes and the whole program ---------- */

typedef struct Param {
    char *name;
    Type type;
    int line;
} Param;

typedef struct Process {
    char *name;
    List *params;         /* of Param* */
    Type ret;             /* TY_NONE when there is no `gives` */
    Block *body;
    int line;

    /* filled in by sema: every variable of the process (parameters first,
     * then each declare / loop variable) gets its own slot number. */
    int nslots;
    List *slot_types;     /* TypeKind of each slot, stored as (intptr_t) */
    List *slot_names;     /* name of each slot (char*), used by codegen */
} Process;

typedef struct Program {
    List *procs;          /* of Process* */
} Program;

/* ---------- constructors (used by the parser) ---------- */

Type make_type(TypeKind kind, int size);

Expr *expr_int(int line, int value);
Expr *expr_bool(int line, int value);
Expr *expr_void(int line);
Expr *expr_var(int line, char *name);
Expr *expr_index(int line, char *name, Expr *index);
Expr *expr_call(int line, char *name, List *args);
Expr *expr_count(int line, Expr *operand);
Expr *expr_neg(int line, Expr *operand);
Expr *expr_not(int line, Expr *operand);
Expr *expr_binary(int line, BinOp op, Expr *left, Expr *right);
Expr *expr_within(int line, Expr *value, Expr *low, Expr *high);
Expr *expr_isvoid(int line, Expr *operand);
Expr *expr_pipe(int line, Expr *input, char *name, List *args);

Stmt *stmt_new(StmtKind kind, int line);
Block *block_new(List *stmts, int end_line);
Param *param_new(int line, char *name, Type type);
Process *process_new(int line, char *name, List *params, Type ret, Block *body);

/* ---------- printing (for `./sift ast`) ---------- */

const char *type_name(TypeKind kind);
const char *binop_name(BinOp op);
void ast_print(Program *prog);

#endif
