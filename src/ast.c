/*
 * ast.c -- constructors for AST nodes and the tree printer used by
 * `./sift ast`.
 */
#include <stdio.h>
#include "ast.h"

/* ---------- constructors ---------- */

Type make_type(TypeKind kind, int size)
{
    Type t;
    t.kind = kind;
    t.size = size;
    return t;
}

/* Every expression starts from this: all fields zero / NULL. */
static Expr *expr_new(ExprKind kind, int line)
{
    Expr *e = xcalloc(1, sizeof(Expr));
    e->kind = kind;
    e->line = line;
    e->type = TY_ERROR;   /* sema sets the real type */
    e->slot = -1;
    return e;
}

Expr *expr_int(int line, int value)
{
    Expr *e = expr_new(EXPR_INT, line);
    e->ival = value;
    return e;
}

Expr *expr_bool(int line, int value)
{
    Expr *e = expr_new(EXPR_BOOL, line);
    e->ival = value;
    return e;
}

Expr *expr_void(int line)
{
    return expr_new(EXPR_VOID, line);
}

Expr *expr_var(int line, char *name)
{
    Expr *e = expr_new(EXPR_VAR, line);
    e->name = name;
    return e;
}

Expr *expr_index(int line, char *name, Expr *index)
{
    Expr *e = expr_new(EXPR_INDEX, line);
    e->name = name;
    e->left = index;
    return e;
}

Expr *expr_call(int line, char *name, List *args)
{
    Expr *e = expr_new(EXPR_CALL, line);
    e->name = name;
    e->args = args ? args : list_new();
    return e;
}

Expr *expr_count(int line, Expr *operand)
{
    Expr *e = expr_new(EXPR_COUNT, line);
    e->left = operand;
    return e;
}

Expr *expr_neg(int line, Expr *operand)
{
    Expr *e = expr_new(EXPR_NEG, line);
    e->left = operand;
    return e;
}

Expr *expr_not(int line, Expr *operand)
{
    Expr *e = expr_new(EXPR_NOT, line);
    e->left = operand;
    return e;
}

Expr *expr_binary(int line, BinOp op, Expr *left, Expr *right)
{
    Expr *e = expr_new(EXPR_BINARY, line);
    e->op = op;
    e->left = left;
    e->right = right;
    return e;
}

Expr *expr_within(int line, Expr *value, Expr *low, Expr *high)
{
    Expr *e = expr_new(EXPR_WITHIN, line);
    e->left = value;
    e->right = low;
    e->third = high;
    return e;
}

Expr *expr_isvoid(int line, Expr *operand)
{
    Expr *e = expr_new(EXPR_ISVOID, line);
    e->left = operand;
    return e;
}

/* The pipeline is desugared here, while parsing:
 *     input |> f          becomes  f(input)
 *     input |> f(x, y)    becomes  f(input, x, y)
 * So the rest of the compiler never sees a pipeline, only calls. */
Expr *expr_pipe(int line, Expr *input, char *name, List *args)
{
    List *all = list_new();
    list_push(all, input);
    if (args != NULL)
        for (int i = 0; i < args->count; i++)
            list_push(all, args->items[i]);
    return expr_call(line, name, all);
}

Stmt *stmt_new(StmtKind kind, int line)
{
    Stmt *s = xcalloc(1, sizeof(Stmt));
    s->kind = kind;
    s->line = line;
    s->slot = -1;
    return s;
}

Block *block_new(List *stmts, int end_line)
{
    Block *b = xmalloc(sizeof(Block));
    b->stmts = stmts;
    b->end_line = end_line;
    return b;
}

Param *param_new(int line, char *name, Type type)
{
    Param *p = xmalloc(sizeof(Param));
    p->line = line;
    p->name = name;
    p->type = type;
    return p;
}

Process *process_new(int line, char *name, List *params, Type ret, Block *body)
{
    Process *p = xcalloc(1, sizeof(Process));
    p->line = line;
    p->name = name;
    p->params = params ? params : list_new();
    p->ret = ret;
    p->body = body;
    p->slot_types = list_new();
    p->slot_names = list_new();
    return p;
}

/* ---------- printer ---------- */

const char *type_name(TypeKind kind)
{
    switch (kind) {
    case TY_VAL:    return "val";
    case TY_FLAG:   return "flag";
    case TY_BUFFER: return "buffer";
    case TY_NONE:   return "nothing";
    default:        return "<error>";
    }
}

const char *binop_name(BinOp op)
{
    static const char *names[] = {
        "+", "-", "*", "/", "%", "=", "!=", "<", "<=", ">", ">=", "and", "or"
    };
    return names[op];
}

static void print_type(Type t)
{
    printf("%s", type_name(t.kind));
    if (t.kind == TY_BUFFER && t.size >= 0)
        printf("[%d]", t.size);
}

static void indent(int depth)
{
    for (int i = 0; i < depth; i++)
        printf("  ");
}

static void print_expr(Expr *e, int depth)
{
    indent(depth);
    switch (e->kind) {
    case EXPR_INT:    printf("int %d\n", e->ival); break;
    case EXPR_BOOL:   printf("flag %s\n", e->ival ? "yes" : "no"); break;
    case EXPR_VOID:   printf("void\n"); break;
    case EXPR_VAR:    printf("var %s\n", e->name); break;
    case EXPR_INDEX:
        printf("index %s\n", e->name);
        print_expr(e->left, depth + 1);
        break;
    case EXPR_CALL:
        printf("call %s\n", e->name);
        for (int i = 0; i < e->args->count; i++)
            print_expr(e->args->items[i], depth + 1);
        break;
    case EXPR_COUNT:
        printf("count\n");
        print_expr(e->left, depth + 1);
        break;
    case EXPR_NEG:
        printf("negate\n");
        print_expr(e->left, depth + 1);
        break;
    case EXPR_NOT:
        printf("not\n");
        print_expr(e->left, depth + 1);
        break;
    case EXPR_BINARY:
        printf("binary %s\n", binop_name(e->op));
        print_expr(e->left, depth + 1);
        print_expr(e->right, depth + 1);
        break;
    case EXPR_WITHIN:
        printf("within\n");
        print_expr(e->left, depth + 1);
        print_expr(e->right, depth + 1);
        print_expr(e->third, depth + 1);
        break;
    case EXPR_ISVOID:
        printf("is void\n");
        print_expr(e->left, depth + 1);
        break;
    }
}

static void print_block(Block *b, int depth);

static void print_stmt(Stmt *s, int depth)
{
    indent(depth);
    switch (s->kind) {
    case STMT_DECLARE:
        printf("declare %s: ", s->name);
        print_type(s->decl_type);
        printf("  (line %d)\n", s->line);
        if (s->value)
            print_expr(s->value, depth + 1);
        if (s->list) {
            indent(depth + 1);
            printf("list\n");
            for (int i = 0; i < s->list->count; i++)
                print_expr(s->list->items[i], depth + 2);
        }
        break;
    case STMT_SET:
        printf("set %s%s  (line %d)\n", s->name, s->index ? "[...]" : "", s->line);
        if (s->index) {
            indent(depth + 1);
            printf("index\n");
            print_expr(s->index, depth + 2);
        }
        print_expr(s->value, depth + 1);
        break;
    case STMT_CHECK:
        printf("check  (line %d)\n", s->line);
        for (int i = 0; i < s->conds->count; i++) {
            indent(depth + 1);
            printf(i == 0 ? "if\n" : "else if\n");
            print_expr(s->conds->items[i], depth + 2);
            print_block(s->blocks->items[i], depth + 2);
        }
        if (s->else_block) {
            indent(depth + 1);
            printf("otherwise\n");
            print_block(s->else_block, depth + 2);
        }
        break;
    case STMT_WHILE:
        printf("repeat while  (line %d)\n", s->line);
        print_expr(s->value, depth + 1);
        print_block(s->body, depth + 1);
        break;
    case STMT_REPEAT:
        printf("repeat %s from/below  (line %d)\n", s->name, s->line);
        print_expr(s->value, depth + 1);
        print_expr(s->limit, depth + 1);
        print_block(s->body, depth + 1);
        break;
    case STMT_SWEEP:
        printf("sweep %s in  (line %d)\n", s->name, s->line);
        print_expr(s->value, depth + 1);
        print_block(s->body, depth + 1);
        break;
    case STMT_GIVE:
        printf("give back  (line %d)\n", s->line);
        if (s->value)
            print_expr(s->value, depth + 1);
        break;
    case STMT_STOP:
        printf("stop  (line %d)\n", s->line);
        break;
    case STMT_SKIP:
        printf("skip  (line %d)\n", s->line);
        break;
    case STMT_SHOW:
        printf("show  (line %d)\n", s->line);
        print_expr(s->value, depth + 1);
        break;
    case STMT_CALL:
        printf("call statement  (line %d)\n", s->line);
        print_expr(s->value, depth + 1);
        break;
    }
}

static void print_block(Block *b, int depth)
{
    indent(depth);
    printf("block\n");
    for (int i = 0; i < b->stmts->count; i++)
        print_stmt(b->stmts->items[i], depth + 1);
}

void ast_print(Program *prog)
{
    printf("program\n");
    for (int i = 0; i < prog->procs->count; i++) {
        Process *p = prog->procs->items[i];
        printf("  process %s", p->name);
        if (p->params->count > 0) {
            printf(" using ");
            for (int j = 0; j < p->params->count; j++) {
                Param *param = p->params->items[j];
                printf("%s%s: ", j ? ", " : "", param->name);
                print_type(param->type);
            }
        }
        if (p->ret.kind != TY_NONE) {
            printf(" gives ");
            print_type(p->ret);
        }
        printf("  (line %d)\n", p->line);
        print_block(p->body, 2);
    }
}
