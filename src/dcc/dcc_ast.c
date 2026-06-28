/*
 * dcc_ast.c - function-local AST: arena allocator and node constructors.
 *
 * See dcc_ast.h for the design rationale.  This module is intentionally
 * self-contained: it depends only on the umbrella header dcc.h for the
 * xmalloc/fatal helpers and the struct Sym definition, and it does not yet
 * participate in code generation.  It is compiled and linked so the data
 * structures and their invariants can be exercised as the AST migration
 * proceeds construct by construct.
 */
#include "dcc.h"
#include "dcc_ast.h"

/* A block large enough that a typical function body needs only one or two.
 * Nodes are ~80 bytes, so 64 KiB holds roughly 800 nodes per block. */
#define AST_BLOCK_SIZE (64u * 1024u)

/* ------------------------------------------------------------------------- *
 * Arena.
 * ------------------------------------------------------------------------- */
void ast_arena_init(struct AstArena *ar)
{
    ar->blocks = NULL;
    ar->nblocks = 0;
    ar->cap_blocks = 0;
    ar->block_size = AST_BLOCK_SIZE;
    ar->used = AST_BLOCK_SIZE;   /* force a fresh block on first alloc */
}

static void ast_arena_add_block(struct AstArena *ar)
{
    if (ar->nblocks >= ar->cap_blocks) {
        int newcap;
        char **nb;
        newcap = (ar->cap_blocks == 0) ? 8 : ar->cap_blocks * 2;
        nb = (char **)xmalloc((size_t)newcap * sizeof(char *));
        if (ar->blocks) {
            int i;
            for (i = 0; i < ar->nblocks; i++)
                nb[i] = ar->blocks[i];
            free(ar->blocks);
        }
        ar->blocks = nb;
        ar->cap_blocks = newcap;
    }
    ar->blocks[ar->nblocks] = (char *)xmalloc(ar->block_size);
    ar->nblocks++;
    ar->used = 0;
}

void *ast_arena_alloc(struct AstArena *ar, size_t n)
{
    char *p;

    /* Round up to pointer alignment so nodes and pointer arrays are aligned. */
    n = (n + (sizeof(void *) - 1)) & ~(size_t)(sizeof(void *) - 1);

    /* A request larger than a block gets its own oversized block. */
    if (n > ar->block_size) {
        char *big = (char *)xmalloc(n);
        /* Park the oversized block at the front of the list but keep the
         * current bump block as the allocation target. */
        if (ar->nblocks >= ar->cap_blocks)
            ast_arena_add_block(ar);   /* ensures capacity, sets a bump block */
        /* Insert big without disturbing the active bump cursor. */
        if (ar->nblocks >= ar->cap_blocks) {
            /* cannot happen after add_block, but stay defensive */
            fatal("ast arena: capacity");
        }
        ar->blocks[ar->nblocks] = big;
        ar->nblocks++;
        return big;
    }

    if (ar->used + n > ar->block_size)
        ast_arena_add_block(ar);

    p = ar->blocks[ar->nblocks - 1] + ar->used;
    ar->used += n;
    return p;
}

void ast_arena_reset(struct AstArena *ar)
{
    /* Keep the first block (amortises re-allocation across functions) and
     * release the rest. */
    int i;
    for (i = 1; i < ar->nblocks; i++)
        free(ar->blocks[i]);
    if (ar->nblocks > 0) {
        ar->nblocks = 1;
        ar->used = 0;
    } else {
        ar->used = ar->block_size;
    }
}

void ast_arena_free(struct AstArena *ar)
{
    int i;
    for (i = 0; i < ar->nblocks; i++)
        free(ar->blocks[i]);
    free(ar->blocks);
    ast_arena_init(ar);
}

/* ------------------------------------------------------------------------- *
 * Node constructors.
 * ------------------------------------------------------------------------- */
struct AstNode *ast_new(struct AstArena *ar, int kind)
{
    struct AstNode *n;
    n = (struct AstNode *)ast_arena_alloc(ar, sizeof(struct AstNode));
    n->kind = kind;
    n->type = 0;
    n->op = 0;
    n->ival = 0;
    n->uval = 0;
    n->str_index = -1;
    n->sym = NULL;
    n->sval = NULL;
    n->a = NULL;
    n->b = NULL;
    n->c = NULL;
    n->d = NULL;
    n->list = NULL;
    n->list_len = 0;
    n->list_cap = 0;
    n->peek_type = 0;
    n->line = 0;
    return n;
}

struct AstNode *ast_int_lit(struct AstArena *ar, long value, int type)
{
    struct AstNode *n = ast_new(ar, AST_INT_LIT);
    n->ival = value;
    n->type = type;
    return n;
}

struct AstNode *ast_float_lit(struct AstArena *ar, unsigned long bits, int type)
{
    struct AstNode *n = ast_new(ar, AST_FLOAT_LIT);
    n->uval = bits;
    n->type = type;
    return n;
}

struct AstNode *ast_str_lit(struct AstArena *ar, int str_index, int type)
{
    struct AstNode *n = ast_new(ar, AST_STR_LIT);
    n->str_index = str_index;
    n->type = type;
    return n;
}

struct AstNode *ast_ident(struct AstArena *ar, struct Sym *sym, int type)
{
    struct AstNode *n = ast_new(ar, AST_IDENT);
    n->sym = sym;
    n->type = type;
    return n;
}

struct AstNode *ast_unary(struct AstArena *ar, int op, struct AstNode *operand,
                          int type)
{
    struct AstNode *n = ast_new(ar, AST_UNARY);
    n->op = op;
    n->a = operand;
    n->type = type;
    return n;
}

struct AstNode *ast_binary(struct AstArena *ar, int kind, int op,
                           struct AstNode *lhs, struct AstNode *rhs, int type)
{
    struct AstNode *n = ast_new(ar, kind);
    n->op = op;
    n->a = lhs;
    n->b = rhs;
    n->type = type;
    return n;
}

struct AstNode *ast_assign(struct AstArena *ar, int op, struct AstNode *lhs,
                           struct AstNode *rhs, int type)
{
    struct AstNode *n = ast_new(ar, AST_ASSIGN);
    n->op = op;
    n->a = lhs;
    n->b = rhs;
    n->type = type;
    return n;
}

struct AstNode *ast_cond(struct AstArena *ar, struct AstNode *cnd,
                         struct AstNode *then_e, struct AstNode *else_e,
                         int type)
{
    struct AstNode *n = ast_new(ar, AST_COND);
    n->a = cnd;
    n->b = then_e;
    n->c = else_e;
    n->type = type;
    return n;
}

struct AstNode *ast_cast(struct AstArena *ar, int type, struct AstNode *operand)
{
    struct AstNode *n = ast_new(ar, AST_CAST);
    n->a = operand;
    n->type = type;
    return n;
}

struct AstNode *ast_call(struct AstArena *ar, struct AstNode *callee, int type)
{
    struct AstNode *n = ast_new(ar, AST_CALL);
    n->a = callee;
    n->type = type;
    return n;
}

void ast_list_push(struct AstArena *ar, struct AstNode *parent,
                   struct AstNode *child)
{
    if (parent->list_len >= parent->list_cap) {
        int newcap;
        struct AstNode **nl;
        int i;
        newcap = (parent->list_cap == 0) ? 4 : parent->list_cap * 2;
        nl = (struct AstNode **)ast_arena_alloc(ar,
                 (size_t)newcap * sizeof(struct AstNode *));
        for (i = 0; i < parent->list_len; i++)
            nl[i] = parent->list[i];
        parent->list = nl;
        parent->list_cap = newcap;
    }
    parent->list[parent->list_len] = child;
    parent->list_len++;
}

char *ast_arena_strdup(struct AstArena *ar, const char *s)
{
    size_t n;
    char *p;
    if (s == NULL)
        return NULL;
    n = strlen(s) + 1;
    p = (char *)ast_arena_alloc(ar, n);
    memcpy(p, s, n);
    return p;
}
