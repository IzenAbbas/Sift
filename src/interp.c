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

/* ---------- expressions ---------- */

static Value eval(Frame *f, Expr *e);

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
    case EXPR_VAR:
        return f->slots[e->slot];
    case EXPR_NEG:
        return make_num(wrap(0u - (uint32_t)eval(f, e->left).num));
    case EXPR_NOT:
        return make_num(!eval(f, e->left).num);
    case EXPR_BINARY:
        return eval_binary(f, e);
    default:
        runtime_error(e->line, "internal error: unknown expression");
        return make_num(0);
    }
}

/* ---------- statements ---------- */

static Flow exec_block(Frame *f, Block *b);

static void show(Expr *e, Value v)
{
    if (e->type == TY_FLAG)
        printf("%s\n", v.num ? "yes" : "no");
    else
        printf("%d\n", v.num);
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
        f->slots[s->slot] = s->value ? eval(f, s->value) : make_num(0);
        return FLOW_NORMAL;

    case STMT_SET:
        f->slots[s->slot] = eval(f, s->value);
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

int interp_run(Program *prog)
{
    Process *main_proc = sema_main(prog);
    Frame frame;
    frame.proc = main_proc;
    frame.slots = xcalloc(main_proc->nslots, sizeof(Value));
    frame.result = make_num(0);
    exec_block(&frame, main_proc->body);
    fflush(stdout);
    return frame.result.num;
}
