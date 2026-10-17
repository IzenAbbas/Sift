/*
 * interp.c -- Phase 1: a tree-walking interpreter.
 *
 * The interpreter walks the checked AST directly. Each running process
 * has a Frame holding one Value per variable slot (sema numbered the
 * slots), so a variable is found by its slot number, not by its name.
 *
 * Statements return a Flow value that says whether execution continues
 * normally or a `give back` is unwinding out of the process.
 */
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "interp.h"
#include "sema.h"

/* A buffer value: a pointer to the elements plus the length.
 * Copying a Buffer copies only these two fields, which is what gives
 * buffers their reference semantics. */
typedef struct {
    int32_t *data;
    int32_t len;
} Buffer;

/* The value of a variable or expression. val and flag use `num`
 * (flag: 0 = no, 1 = yes); buffer uses `buf`. */
typedef struct {
    int32_t num;
    Buffer buf;
} Value;

/* One running process. */
typedef struct {
    Process *proc;
    Value *slots;        /* one per variable */
    Value result;        /* set by `give back` */
} Frame;

/* What a statement tells the statement that contains it. */
typedef enum {
    FLOW_NORMAL,         /* carry on with the next statement */
    FLOW_STOP,           /* `stop`: leave the innermost loop */
    FLOW_SKIP,           /* `skip`: go to the next iteration */
    FLOW_RETURN          /* `give back` happened: leave the process */
} Flow;

/* Runtime errors: "runtime error: line N: message", exit code 1. */
static void runtime_error(int line, const char *fmt, ...)
{
    va_list ap;
    fflush(stdout);      /* keep the output printed so far */
    fprintf(stderr, "runtime error: line %d: ", line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}

/* ---------- arithmetic ----------
 * A val is a 32-bit two's-complement integer and overflow wraps around,
 * the same as LLVM's add/sub/mul. Signed overflow is undefined in C, so
 * the sums are done on unsigned numbers and converted back. */

static int32_t wrap(uint32_t u)
{
    return (int32_t)u;
}

static int32_t do_arith(Expr *e, int32_t a, int32_t b)
{
    switch (e->op) {
    case OP_ADD: return wrap((uint32_t)a + (uint32_t)b);
    case OP_SUB: return wrap((uint32_t)a - (uint32_t)b);
    case OP_MUL: return wrap((uint32_t)a * (uint32_t)b);
    case OP_DIV:
        if (b == 0)
            runtime_error(e->line, "division by zero");
        /* INT_MIN / -1 does not fit in 32 bits; like the other
         * operators it wraps around, giving INT_MIN. */
        if (b == -1)
            return wrap(0u - (uint32_t)a);
        return a / b;           /* C division truncates toward zero */
    case OP_MOD:
        if (b == 0)
            runtime_error(e->line, "modulo by zero");
        if (b == -1)
            return 0;
        return a % b;
    default:
        return 0;
    }
}

/* ---------- buffers ---------- */

/* The value used for `void` (a missing value). */
#define VOID_VALUE INT32_MIN

/* New buffer storage of n elements, all 0. It is never freed: Sift
 * programs are short-lived, and a process may give back a buffer it
 * declared, so the storage must outlive the process. */
static Buffer new_buffer(int32_t n)
{
    Buffer b;
    b.data = xcalloc(n, sizeof(int32_t));
    b.len = n;
    return b;
}

static void check_index(int line, int32_t index, Buffer b)
{
    if (index < 0 || index >= b.len)
        runtime_error(line, "index %d out of bounds for buffer of length %d", index, b.len);
}

/* ---------- expressions ---------- */

static Value eval(Frame *f, Expr *e);
static Value call_process(Frame *caller, Expr *call);

static Value make_num(int32_t n)
{
    Value v;
    v.num = n;
    v.buf.data = NULL;
    v.buf.len = 0;
    return v;
}

static Value eval_binary(Frame *f, Expr *e)
{
    /* `and` / `or` short-circuit: the right side is only evaluated
     * when the left side does not already decide the answer. */
    if (e->op == OP_AND) {
        if (!eval(f, e->left).num)
            return make_num(0);
        return make_num(eval(f, e->right).num != 0);
    }
    if (e->op == OP_OR) {
        if (eval(f, e->left).num)
            return make_num(1);
        return make_num(eval(f, e->right).num != 0);
    }

    int32_t a = eval(f, e->left).num;     /* left operand first */
    int32_t b = eval(f, e->right).num;
    switch (e->op) {
    case OP_EQ: return make_num(a == b);
    case OP_NE: return make_num(a != b);
    case OP_LT: return make_num(a < b);
    case OP_LE: return make_num(a <= b);
    case OP_GT: return make_num(a > b);
    case OP_GE: return make_num(a >= b);
    default:    return make_num(do_arith(e, a, b));
    }
}

static Value eval(Frame *f, Expr *e)
{
    switch (e->kind) {
    case EXPR_INT:
    case EXPR_BOOL:
        return make_num(e->ival);
    case EXPR_VOID:
        return make_num(VOID_VALUE);
    case EXPR_VAR:
        return f->slots[e->slot];
    case EXPR_INDEX: {
        Buffer b = f->slots[e->slot].buf;
        int32_t index = eval(f, e->left).num;
        check_index(e->line, index, b);
        return make_num(b.data[index]);
    }
    case EXPR_COUNT:
        return make_num(eval(f, e->left).buf.len);
    case EXPR_ISVOID:
        return make_num(eval(f, e->left).num == VOID_VALUE);
    case EXPR_WITHIN: {
        /* x within lo..hi  is  lo <= x and x <= hi, with x evaluated once */
        int32_t x = eval(f, e->left).num;
        int32_t lo = eval(f, e->right).num;
        int32_t hi = eval(f, e->third).num;
        return make_num(lo <= x && x <= hi);
    }
    case EXPR_NEG:
        return make_num(wrap(0u - (uint32_t)eval(f, e->left).num));
    case EXPR_NOT:
        return make_num(!eval(f, e->left).num);
    case EXPR_BINARY:
        return eval_binary(f, e);
    case EXPR_CALL:
        return call_process(f, e);
    }
    runtime_error(e->line, "internal error: unknown expression");
    return make_num(0);
}

/* ---------- statements ---------- */

static Flow exec_block(Frame *f, Block *b);

static void show(Expr *e, Value v)
{
    if (e->type == TY_FLAG)
        printf("%s\n", v.num ? "yes" : "no");
    else if (v.num == VOID_VALUE)
        printf("void\n");
    else
        printf("%d\n", v.num);
}

/* Value of a declaration: the initialiser, a new buffer, or the default
 * (0 for val, no for flag). */
static Value declare_value(Frame *f, Stmt *s)
{
    if (s->list != NULL) {
        /* declare d: buffer = [a, b, c]; elements evaluated left to right */
        Value v = make_num(0);
        v.buf = new_buffer(s->list->count);
        for (int i = 0; i < s->list->count; i++)
            v.buf.data[i] = eval(f, s->list->items[i]).num;
        return v;
    }
    if (s->value != NULL)
        return eval(f, s->value);
    if (s->decl_type.kind == TY_BUFFER) {
        Value v = make_num(0);
        v.buf = new_buffer(s->decl_type.size);    /* all zeros */
        return v;
    }
    return make_num(0);
}

/* Run one iteration's body and decide what the loop does next.
 * Returns 1 if the loop must end (stop, or give back which is then
 * passed on through *flow), 0 to carry on. */
static int run_body(Frame *f, Block *body, Flow *flow)
{
    Flow result = exec_block(f, body);
    if (result == FLOW_STOP) {
        *flow = FLOW_NORMAL;      /* stop ends this loop only */
        return 1;
    }
    if (result == FLOW_RETURN) {
        *flow = FLOW_RETURN;
        return 1;
    }
    return 0;                     /* normal end of body, or skip */
}

static Flow exec_stmt(Frame *f, Stmt *s)
{
    Flow flow = FLOW_NORMAL;
    switch (s->kind) {
    case STMT_DECLARE:
        /* no initialiser: val starts at 0, flag at no (also 0) */
        f->slots[s->slot] = declare_value(f, s);
        return FLOW_NORMAL;

    case STMT_SET:
        if (s->index != NULL) {
            /* set d[i] = v : index first, bounds check, then the value */
            Buffer b = f->slots[s->slot].buf;
            int32_t index = eval(f, s->index).num;
            check_index(s->line, index, b);
            b.data[index] = eval(f, s->value).num;
        } else {
            /* for a buffer this copies the pointer and length only */
            f->slots[s->slot] = eval(f, s->value);
        }
        return FLOW_NORMAL;

    case STMT_CHECK:
        /* try each condition in turn; run the first branch whose
         * condition is yes, or the otherwise block if none is */
        for (int i = 0; i < s->conds->count; i++)
            if (eval(f, s->conds->items[i]).num)
                return exec_block(f, s->blocks->items[i]);
        if (s->else_block != NULL)
            return exec_block(f, s->else_block);
        return FLOW_NORMAL;

    case STMT_WHILE:
        while (eval(f, s->value).num)
            if (run_body(f, s->body, &flow))
                break;
        return flow;

    case STMT_REPEAT: {
        /* start and limit are evaluated once, before the loop */
        int32_t start = eval(f, s->value).num;
        int32_t limit = eval(f, s->limit).num;
        for (int32_t i = start; i < limit; i++) {
            f->slots[s->slot] = make_num(i);
            if (run_body(f, s->body, &flow))
                break;
        }
        return flow;
    }

    case STMT_SWEEP: {
        /* the buffer (and so its length) is read once, at the start */
        Buffer b = eval(f, s->value).buf;
        for (int32_t k = 0; k < b.len; k++) {
            f->slots[s->slot] = make_num(b.data[k]);   /* a copy of the element */
            if (run_body(f, s->body, &flow))
                break;
        }
        return flow;
    }

    case STMT_STOP:
        return FLOW_STOP;

    case STMT_SKIP:
        return FLOW_SKIP;

    case STMT_SHOW:
        show(s->value, eval(f, s->value));
        return FLOW_NORMAL;

    case STMT_CALL:
        call_process(f, s->value);     /* any result is thrown away */
        return FLOW_NORMAL;

    case STMT_GIVE:
        if (s->value != NULL)
            f->result = eval(f, s->value);
        return FLOW_RETURN;

    default:
        runtime_error(s->line, "internal error: unknown statement");
        return FLOW_NORMAL;
    }
}

static Flow exec_block(Frame *f, Block *b)
{
    for (int i = 0; i < b->stmts->count; i++) {
        Flow flow = exec_stmt(f, b->stmts->items[i]);
        if (flow != FLOW_NORMAL)
            return flow;      /* pass stop/skip/give back up to the enclosing statement */
    }
    return FLOW_NORMAL;
}

/* ---------- processes ---------- */

/* Run a process with already-evaluated arguments and return what it
 * gives back. Every call gets a fresh frame, so recursion works: each
 * active call has its own copy of the variables. */
static Value run_process(Process *p, Value *args)
{
    Frame frame;
    frame.proc = p;
    frame.slots = xcalloc(p->nslots, sizeof(Value));
    frame.result = make_num(0);

    /* parameters are slots 0 .. n-1. val and flag are copied (by value);
     * a buffer copies only its pointer and length (by reference). */
    for (int i = 0; i < p->params->count; i++)
        frame.slots[i] = args[i];

    Flow flow = exec_block(&frame, p->body);
    if (flow != FLOW_RETURN && p->ret.kind != TY_NONE)
        runtime_error(p->body->end_line, "process '%s' ended without giving back a value", p->name);

    free(frame.slots);
    return frame.result;
}

static Value call_process(Frame *caller, Expr *call)
{
    int n = call->args->count;
    Value *args = xcalloc(n, sizeof(Value));
    for (int i = 0; i < n; i++)            /* arguments left to right */
        args[i] = eval(caller, call->args->items[i]);
    Value result = run_process(call->proc, args);
    free(args);
    return result;
}

int interp_run(Program *prog)
{
    Value result = run_process(sema_main(prog), NULL);
    fflush(stdout);
    return result.num;     /* main's result becomes the exit code */
}
