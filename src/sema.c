/*
 * sema.c -- semantic analysis.
 *
 * Walks the AST once, keeping a stack of scopes (one per block), and
 *   - checks that every variable is declared before it is used,
 *   - works out the type of every expression and checks operand types,
 *   - gives every variable of a process its own "slot" number, which the
 *     interpreter and the code generator use to find the variable.
 *
 * All errors are reported (not just the first one). After an error an
 * expression gets type TY_ERROR, and TY_ERROR is accepted everywhere, so
 * one mistake does not cause a flood of follow-up messages.
 */
#include <stdint.h>
#include <string.h>
#include "sema.h"

/* ---------- scopes and symbols ---------- */

typedef struct Symbol {
    char *name;
    TypeKind type;
    int slot;           /* slot number inside the current process */
    int line;           /* where it was declared */
    int readonly;       /* loop variables cannot be changed with `set` */
} Symbol;

typedef struct Scope {
    List *symbols;          /* of Symbol* */
    struct Scope *parent;   /* enclosing scope, NULL for the outermost */
} Scope;

static Scope *cur_scope = NULL;      /* innermost open scope */
static Process *cur_proc = NULL;     /* process being checked */
static int loop_depth = 0;           /* > 0 while inside a loop body */

static void push_scope(void)
{
    Scope *s = xmalloc(sizeof(Scope));
    s->symbols = list_new();
    s->parent = cur_scope;
    cur_scope = s;
}

static void pop_scope(void)
{
    cur_scope = cur_scope->parent;
}

/* Look a name up in the innermost scope only. */
static Symbol *lookup_here(const char *name)
{
    for (int i = 0; i < cur_scope->symbols->count; i++) {
        Symbol *sym = cur_scope->symbols->items[i];
        if (strcmp(sym->name, name) == 0)
            return sym;
    }
    return NULL;
}

/* Look a name up in every open scope, innermost first. */
static Symbol *lookup(const char *name)
{
    for (Scope *s = cur_scope; s != NULL; s = s->parent) {
        for (int i = 0; i < s->symbols->count; i++) {
            Symbol *sym = s->symbols->items[i];
            if (strcmp(sym->name, name) == 0)
                return sym;
        }
    }
    return NULL;
}

/* Declare a new variable in the current scope and give it the next free
 * slot of the current process. Returns the slot number. */
static int declare(const char *name, TypeKind type, int line, int readonly)
{
    Symbol *old = lookup_here(name);
    if (old != NULL)
        report_error(line, "'%s' is already declared in this block (at line %d)", name, old->line);

    Symbol *sym = xmalloc(sizeof(Symbol));
    sym->name = xstrdup(name);
    sym->type = type;
    sym->line = line;
    sym->readonly = readonly;
    sym->slot = cur_proc->nslots++;
    list_push(cur_proc->slot_types, (void *)(intptr_t)type);
    list_push(cur_proc->slot_names, sym->name);
    list_push(cur_scope->symbols, sym);
    return sym->slot;
}

/* ---------- expressions ---------- */

static TypeKind check_expr(Expr *e);

/* Check that expression e has type `want`. TY_ERROR always matches,
 * because the real problem has already been reported. */
static void expect_type(Expr *e, TypeKind want, const char *what)
{
    TypeKind got = check_expr(e);
    if (got != want && got != TY_ERROR)
        report_error(e->line, "%s must be %s, not %s", what, type_name(want), type_name(got));
}

static TypeKind check_binary(Expr *e)
{
    TypeKind l, r;
    switch (e->op) {
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD:
        expect_type(e->left, TY_VAL, "operand of arithmetic");
        expect_type(e->right, TY_VAL, "operand of arithmetic");
        return TY_VAL;

    case OP_LT: case OP_LE: case OP_GT: case OP_GE:
        expect_type(e->left, TY_VAL, "operand of comparison");
        expect_type(e->right, TY_VAL, "operand of comparison");
        return TY_FLAG;

    case OP_EQ: case OP_NE:
        /* = and != work on two vals or two flags */
        l = check_expr(e->left);
        r = check_expr(e->right);
        if (l == TY_ERROR || r == TY_ERROR)
            return TY_FLAG;
        if (l != r)
            report_error(e->line, "cannot compare %s with %s", type_name(l), type_name(r));
        else if (l != TY_VAL && l != TY_FLAG)
            report_error(e->line, "'%s' only works on val or flag, not %s", binop_name(e->op), type_name(l));
        return TY_FLAG;

    case OP_AND: case OP_OR:
        expect_type(e->left, TY_FLAG, "operand of 'and'/'or'");
        expect_type(e->right, TY_FLAG, "operand of 'and'/'or'");
        return TY_FLAG;
    }
    return TY_ERROR;
}

/* Work out (and record in e->type) the type of an expression. */
static TypeKind check_expr(Expr *e)
{
    TypeKind t = TY_ERROR;
    Symbol *sym;

    switch (e->kind) {
    case EXPR_INT:
        t = TY_VAL;
        break;
    case EXPR_BOOL:
        t = TY_FLAG;
        break;
    case EXPR_VAR:
        sym = lookup(e->name);
        if (sym == NULL) {
            report_error(e->line, "'%s' is not declared", e->name);
        } else {
            e->slot = sym->slot;
            t = sym->type;
        }
        break;
    case EXPR_NEG:
        expect_type(e->left, TY_VAL, "operand of unary '-'");
        t = TY_VAL;
        break;
    case EXPR_NOT:
        expect_type(e->left, TY_FLAG, "operand of 'not'");
        t = TY_FLAG;
        break;
    case EXPR_BINARY:
        t = check_binary(e);
        break;
    default:
        report_error(e->line, "this kind of expression is not supported yet");
        break;
    }
    e->type = t;
    return t;
}

/* ---------- statements ---------- */

static void check_block(Block *b);

static void check_declare(Stmt *s)
{
    TypeKind t = s->decl_type.kind;
    if (t == TY_BUFFER) {
        report_error(s->line, "buffers are not supported yet");
        return;
    }
    if (s->list != NULL)
        report_error(s->line, "a list can only initialise a buffer");
    /* The initialiser is checked before the new name exists, so
     * `declare x: val = x + 1;` refers to an outer x. */
    if (s->value != NULL)
        expect_type(s->value, t, "initial value");
    s->slot = declare(s->name, t, s->line, 0);
}

static void check_set(Stmt *s)
{
    Symbol *sym = lookup(s->name);
    if (sym == NULL) {
        report_error(s->line, "'%s' is not declared", s->name);
        check_expr(s->value);
        return;
    }
    s->slot = sym->slot;
    if (sym->readonly)
        report_error(s->line, "cannot set loop variable '%s'", s->name);
    if (s->index != NULL) {
        report_error(s->line, "indexing is not supported yet");
        return;
    }
    expect_type(s->value, sym->type, "assigned value");
}

/* A loop body is checked with loop_depth raised, so that `stop` and
 * `skip` inside it are allowed. */
static void check_loop_body(Block *body)
{
    loop_depth++;
    check_block(body);
    loop_depth--;
}

/* repeat i from a below b / sweep x in buf: the loop variable lives in
 * its own scope around the body, and is read-only. */
static void check_counted_loop(Stmt *s)
{
    if (s->kind == STMT_REPEAT) {
        expect_type(s->value, TY_VAL, "start of 'repeat'");
        expect_type(s->limit, TY_VAL, "limit of 'repeat'");
    } else {
        expect_type(s->value, TY_BUFFER, "what 'sweep' goes through");
    }
    push_scope();
    s->slot = declare(s->name, TY_VAL, s->line, 1);
    check_loop_body(s->body);
    pop_scope();
}

static void check_stmt(Stmt *s)
{
    switch (s->kind) {
    case STMT_DECLARE:
        check_declare(s);
        break;
    case STMT_SET:
        check_set(s);
        break;
    case STMT_CHECK:
        for (int i = 0; i < s->conds->count; i++) {
            expect_type(s->conds->items[i], TY_FLAG, "condition of 'check'");
            check_block(s->blocks->items[i]);
        }
        if (s->else_block != NULL)
            check_block(s->else_block);
        break;
    case STMT_WHILE:
        expect_type(s->value, TY_FLAG, "condition of 'repeat while'");
        check_loop_body(s->body);
        break;
    case STMT_REPEAT:
    case STMT_SWEEP:
        check_counted_loop(s);
        break;
    case STMT_STOP:
    case STMT_SKIP:
        if (loop_depth == 0)
            report_error(s->line, "'%s' used outside a loop", s->kind == STMT_STOP ? "stop" : "skip");
        break;
    case STMT_SHOW:
        check_expr(s->value);
        break;
    case STMT_GIVE:
        if (s->value != NULL)
            expect_type(s->value, cur_proc->ret.kind, "value given back");
        break;
    default:
        report_error(s->line, "this kind of statement is not supported yet");
        break;
    }
}

/* Every block is its own scope. */
static void check_block(Block *b)
{
    push_scope();
    for (int i = 0; i < b->stmts->count; i++)
        check_stmt(b->stmts->items[i]);
    pop_scope();
}

/* ---------- processes ---------- */

Process *sema_main(Program *prog)
{
    for (int i = 0; i < prog->procs->count; i++) {
        Process *p = prog->procs->items[i];
        if (strcmp(p->name, "main") == 0)
            return p;
    }
    return NULL;
}

int sema_check(Program *prog)
{
    int errors_before = g_error_count;

    for (int i = 0; i < prog->procs->count; i++) {
        cur_proc = prog->procs->items[i];
        check_block(cur_proc->body);
    }

    if (sema_main(prog) == NULL)
        report_error(1, "the program has no 'main' process");

    return g_error_count - errors_before;
}
