/*
 * dcc_ast_gen_support.c - ast_gen_supported dispatch, call/struct gates, const folds.
 *
 * Split from dcc_ast_gen.c; part of the AST codegen module.  Shared
 * prototypes live in dcc_ast_gen_internal.h.
 */
#include <string.h>
#include <stdint.h>
#include "dcc_ast_gen_internal.h"

#define AST_SUPPORT_CACHE_SIZE 4096

struct AstSupportCacheEntry {
    const struct AstNode *node;
    unsigned stamp;
    int dead;
    int value;
};

static struct AstSupportCacheEntry ast_support_cache[AST_SUPPORT_CACHE_SIZE];
static unsigned ast_support_cache_stamp = 1;

static int ast_gen_supported_uncached(const struct AstNode *n);

void ast_support_cache_begin(void)
{
    ast_support_cache_stamp++;
    if (ast_support_cache_stamp == 0) {
        memset(ast_support_cache, 0, sizeof(ast_support_cache));
        ast_support_cache_stamp = 1;
    }
}


int ast_expr_yields_bool01(const struct AstNode *n)
{
    if (n == NULL)
        return 0;
    if (type_is_bool(n->type))
        return 1;
    switch (n->kind) {
    case AST_INT_LIT:
        return n->ival == 0 || n->ival == 1;
    case AST_UNARY:
        return n->op == '!';
    case AST_BINARY:
        return is_cmp_op(n->op);
    case AST_LOGAND:
    case AST_LOGOR:
        return 1;
    case AST_CAST:
        return type_is_bool(n->type);
    default:
        return 0;
    }
}

/* RHS acceptable for storing into a plain-int (char/int) array or pointer
 * element via assignment `n`.  A plain-int RHS always works.  A long-typed RHS
 * is also accepted for a plain `=`: the element store path evaluates the RHS to
 * DE:HL and writes only the low word (int) or low byte (char), i.e. it
 * truncates to the element width - exactly the C conversion for `int_elem =
 * long_value`.  Compound ops are left to the plain-int-only path. */
static int ast_int_elem_assign_rhs_ok(const struct AstNode *n)
{
    return ast_value_is_plain_int(n->b) ||
           (n->op == '=' && ast_value_is_long_word(n->b));
}

int ast_gen_supported(const struct AstNode *n)
{
    uintptr_t h;
    unsigned idx;
    int dead;
    int value;
    struct AstSupportCacheEntry *e;

    if (n == NULL)
        return 0;

    dead = expr_result_dead ? 1 : 0;
    h = ((uintptr_t)n >> 4) ^ (uintptr_t)(n->kind * 131) ^ (uintptr_t)dead;
    idx = (unsigned)(h & (AST_SUPPORT_CACHE_SIZE - 1));
    e = &ast_support_cache[idx];
    if (e->stamp == ast_support_cache_stamp && e->node == n && e->dead == dead)
        return e->value;

    value = ast_gen_supported_uncached(n);
    e->node = n;
    e->stamp = ast_support_cache_stamp;
    e->dead = dead;
    e->value = value;
    return value;
}

static int ast_gen_supported_uncached(const struct AstNode *n)
{
    if (n == NULL)
        return 0;
    switch (n->kind) {
    case AST_INT_LIT:
    case AST_FLOAT_LIT:
    case AST_STR_LIT:
    case AST_SIZEOF_EXPR:
    case AST_SIZEOF_TYPE:
    case AST_COMPOUND_LITERAL:
        return 1;
    case AST_IDENT:
        return ident_supported(n->sval);
    case AST_UNARY:
        if (n->op == '&')
            return ast_address_of_supported(n->a);
        if (n->op == '-' || n->op == '+' || n->op == '~' || n->op == '!') {
            /* A unary chain over a single int literal folds to one immediate,
             * so it is emitted directly; allow it.  Other constant operands
             * (binary const, float) decline here to keep their folded form. */
            long fv;
            if (ast_unary_long_const_fold(n, &fv))
                return n->op != '!';
            if (ast_unary_int_const_fold(n, &fv))
                return n->op != '!';
            if ((n->op == '-' || n->op == '+') && n->a != NULL &&
                n->a->kind == AST_FLOAT_LIT)
                return 1;
            /* `-PI` / `+PI` where PI is a const-qualified float identifier:
             * the AST path loads the value (exactly as a bare `PI` operand,
             * which is already supported) and negates it with gen_unary_ast's
             * float sign-bit flip.  Allow it so float expressions like
             * `-PI - x` and `x < -PI` can be emitted. */
            if ((n->op == '-' || n->op == '+') && n->a != NULL &&
                n->a->kind == AST_IDENT) {
                struct Sym *fs = find_sym(n->a->sval);
                if (fs != NULL && fs->storage != SC_FUNC && !fs->is_array &&
                    type_is_float(fs->type))
                    return 1;
            }
            if (n->op == '~' && ast_const_plain_int_binary_supported(n->a))
                return 1;
            if (n->op == '!' && n->a != NULL && n->a->kind == AST_BINARY &&
                is_cmp_op(n->a->op) && ast_gen_supported(n->a) &&
                (ast_value_is_float_word(n->a->a) || ast_value_is_float_word(n->a->b)))
                return 1;
            /* `!<constant-int-expr>` (including chains like `!!0`) folds to a
             * single 0/1 immediate in gen_unary_ast, so allow it even though
             * the operand is itself a constant. */
            if (n->op == '!') {
                long cv;
                if (ast_const_scalar_fold(n, &cv))
                    return 1;
            }
            if (ast_node_is_const(n->a))
                return 0;
            return ast_gen_supported(n->a);
        }
        /* Address-of a bare addressable identifier reduces - in gen_lvalue_addr
         * with no trailing [ ] / . / -> - to emit_load_sym_addr(s) plus a
         * pointer-to-element result type.  The deref / phantom-deref / global
         * pointer-subscript preload special cases all need a following token,
         * so a bare-id operand cannot reach them.  Exclude const-value (enum /
         * folded) and function symbols, which are not plain addressable
         * objects. */
        if (n->op == '&') {
            struct Sym *as;
            if (n->a == NULL || n->a->kind != AST_IDENT)
                return 0;
            as = find_sym(n->a->sval);
            if (as == NULL || as->is_const_value || as->storage == SC_FUNC)
                return 0;
            return 1;
        }
        if (n->op == '*')
            return ast_deref_plain_int_read(n) || ast_deref_pointer_word_read(n) ||
                   ast_deref_long_read(n) || ast_deref_float_read(n);
        if (n->op == TOK_INC || n->op == TOK_DEC)
            return ast_preincdec_plain_int(n) || ast_preincdec_pointer_word(n);
        return 0;
    case AST_BINARY:
        if (!is_supported_binary_op(n->op) && !is_shift_op(n->op))
            return 0;
        {
            int ptr_type;
            int no_deref;
            if ((n->op == '+' || n->op == '-') &&
                ast_pointer_expr_type(n, &ptr_type, &no_deref))
                return 1;
        }
        if (ast_const_plain_int_binary_supported(n))
            return 1;
        if (ast_long_cmp_supported(n))
            return 1;
        if (ast_node_is_const(n) && ast_long_arith_supported(n))
            return 1;
        if (ast_node_is_const(n) && ast_mixed_long_rhs_arith_supported(n)) {
            long rhs;
            if ((n->op == '/' || n->op == '%') &&
                ast_unary_long_const_fold(n->b, &rhs) && rhs == 0)
                return 0;
            return 1;
        }
        if (ast_node_is_const(n) && is_shift_op(n->op) &&
            ast_value_is_long_word(n->a))
            return ast_value_is_plain_int(n->b);
        if (ast_node_is_const(n) &&
            (is_cmp_op(n->op) || is_float_arith_op(n->op)) &&
            (ast_value_is_float_word(n->a) || ast_value_is_float_word(n->b)))
            return (ast_value_is_float_word(n->a) || ast_value_is_plain_int(n->a) ||
                    ast_value_is_long_word(n->a)) &&
                   (ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b) ||
                    ast_value_is_long_word(n->b));
        /* Most fully constant expressions should fold to a single immediate;
         * decline unless a narrow plain-int binary slice above has opted in. */
        if (ast_node_is_const(n))
            return 0;
        /* A float literal operand needs the float const-folding path; decline.
         * An integer literal enables constant specialisations for *, /, %,
         * shifts and comparisons (e.g. x*10 becomes a shift/add chain, x<0 a
         * sign-bit test) that are not emitted here; allow a literal only for
         * +, -, &, |, ^, whose AST emit is a single uniform sequence. */
        if (ast_pointer_cmp_supported(n))
            return 1;
        if (ast_pointer_diff_supported(n))
            return 1;
        if (ast_long_arith_supported(n))
            return 1;
        if (ast_mixed_long_rhs_arith_supported(n))
            return 1;
        if (!ast_gen_supported(n->a) || !ast_gen_supported(n->b))
            return 0;
        if (is_cmp_op(n->op) &&
            (ast_value_is_float_word(n->a) || ast_value_is_float_word(n->b)))
            return (ast_value_is_float_word(n->a) || ast_value_is_plain_int(n->a) ||
                    ast_value_is_long_word(n->a)) &&
                   (ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b) ||
                    ast_value_is_long_word(n->b));
        if (is_float_arith_op(n->op) &&
            (ast_value_is_float_word(n->a) || ast_value_is_float_word(n->b)))
            return (ast_value_is_float_word(n->a) || ast_value_is_plain_int(n->a) ||
                    ast_value_is_long_word(n->a)) &&
                   (ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b) ||
                    ast_value_is_long_word(n->b));
        if (n->a->kind == AST_FLOAT_LIT || n->b->kind == AST_FLOAT_LIT)
            return 0;
        if (is_shift_op(n->op) && ast_value_is_long_word(n->a))
            return ast_value_is_plain_int(n->b);
        /* Only the plain-int branch produces the uniform sequence; pointers,
         * longs, floats and structs use other lowerings. */
        if (!ast_value_is_plain_int(n->a) || !ast_value_is_plain_int(n->b))
            return 0;
        /* Shifts derive their result/branch shape from the (already plain-int)
         * left operand; the arithmetic/comparison group instead depends on the
         * stored rhs peek staying on the 16-bit branch. */
        if (!is_shift_op(n->op) &&
            (type_is_long(n->peek_type) || type_is_float(n->peek_type)))
            return 0;
        return 1;
    case AST_ASSIGN: {
        /* Narrow slice: `lhs = rhs` and compound `lhs OP= rhs` where lhs is a
         * bare plain-int (16-bit, signed or unsigned) scalar reachable by the
         * direct store helper - an IX-direct local/param or a non-array
         * global/extern word.  Plain `=` uses the general `=` lowering; the
         * compound ops (+=,-=,*=,/=,%=,&=,|=,^=) use the general compound-assign
         * lowering (load lhs, push, eval rhs, combine, store).  The dead-result
         * `i += const` / `i -= ix_local` fast paths are declined so the AST
         * never emits a longer sequence.  A plain `=` to a SUBSCRIPT lvalue
         * `arr[i] = rhs` (int element) is also supported via the general
         * address+store lowering.  Shift-assigns, other non-identifier lvalues,
         * char-element subscripts, arrays, consts, structs, pointers, chars,
         * longs and floats all decline here. */
        struct Sym *s;
        int is_compound = (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                           n->op == TOK_MULEQ || n->op == TOK_DIVEQ ||
                           n->op == TOK_MODEQ || n->op == TOK_ANDEQ ||
                           n->op == TOK_OREQ  || n->op == TOK_XOREQ ||
                           n->op == TOK_SHLEQ || n->op == TOK_SHREQ);
        if (n->op != '=' && !is_compound)
            return 0;                     /* shift-assign etc. decline */
        if (ast_global_byte_array_const_store(n, NULL, NULL, NULL))
            return 1;
        if (ast_global_byte_array_fast_store(n, NULL, NULL, NULL, NULL,
                                             NULL, NULL, NULL))
            return 1;
        if (n->op == '=' && n->a->kind == AST_IDENT) {
            s = find_sym(n->a->sval);
            if (s != NULL && !s->is_array && !s->is_const_value &&
                type_is_struct_object(s->type) &&
                ast_struct_return_call_assign_supported(s->type, n->b))
                return 1;
        }
        if (ast_struct_deref_copy_assign_supported(n))
            return 1;
        if (ast_struct_member_copy_assign_supported(n))
            return 1;
        if (ast_struct_chain_copy_assign_supported(n))
            return 1;
        if (ast_struct_copy_assign_supported(n))
            return 1;
        if (ast_long_va_arg_self_assign_supported(n, NULL))
            return 1;
        {
            int lhs_type;
            if (n->op == '=' && ast_struct_addr_expr_supported(n->a, &lhs_type) &&
                ast_struct_return_call_assign_supported(lhs_type, n->b))
                return 1;
        }
        {
            int rhs_ptr_type;
            int rhs_no_deref;
            int rhs_va_type;
            if (!ast_gen_supported(n->b) && n->b->kind != AST_CAST &&
                !ast_pointer_expr_type(n->b, &rhs_ptr_type, &rhs_no_deref) &&
                !(n->b->kind == AST_CALL && ast_value_is_pointer_word(n->b) &&
                  ast_call_named_args_supported(n->b)) &&
                !ast_va_arg_deref_type(n->b, &rhs_va_type) &&
                !ast_value_is_long_word(n->b))
                return 0;
        }
        /* Subscript lvalue store: arr[i] = rhs / arr[i] OP= rhs.  Restricted to
         * an INT element (size 2) so the general address+store lowering is used
         * with no byte-store fast path intervening (the global byte-array fast
         * path needs a size-1 element).  The element/index/base constraints are
         * the same as a plain-int subscript READ. */
        if (n->a->kind == AST_INDEX) {
            struct Sym *base;
            int decayed;
            int elem;
            if (is_compound && expr_result_dead &&
                ast_index_2d_array_elem_type(n->a, &elem) &&
                ast_is_plain_int_type(elem) && type_size(elem) == 2 &&
                ast_value_is_plain_int(n->b))
                return 1;
            if (ast_index_lvalue_elem_type(n->a, &elem)) {
                if (type_is_long(elem)) {
                    if (n->op == '=')
                        return ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b) ||
                               ast_value_is_float_word(n->b);
                    if (n->op == TOK_SHLEQ || n->op == TOK_SHREQ)
                        return ast_value_is_plain_int(n->b) || ast_value_is_long_word(n->b);
                    if (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                        n->op == TOK_MULEQ || n->op == TOK_DIVEQ || n->op == TOK_MODEQ ||
                        n->op == TOK_ANDEQ || n->op == TOK_OREQ  || n->op == TOK_XOREQ)
                        return ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b);
                    return 0;
                }
                if (type_is_float(elem)) {
                    if (n->op == '=')
                        return ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b) ||
                               ast_value_is_long_word(n->b);
                    if (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                        n->op == TOK_MULEQ || n->op == TOK_DIVEQ)
                        return ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b) ||
                               ast_value_is_long_word(n->b);
                    return 0;
                }
            }
            /* Deref-of-pointer-to-array subscript store `(*p)[i] = rhs` (p a
             * pointer-to-array local/param, e.g. `int (*p)[4]`).  Long and
             * float element stores are already accepted by the
             * ast_index_lvalue_elem_type block above; add the plain-int and
             * pointer element cases so `(*p)[i]` matches the address machine
             * that gen_index_addr_ast already emits for this shape (via
             * ast_index_deref_pointer_array_collect). */
            if (n->op == '=' &&
                ast_index_deref_pointer_array_collect(n->a, &base, NULL, NULL,
                                                      NULL, &elem)) {
                if (type_ptr_depth(elem) > 0)
                    return type_size(elem) == 2 && ast_pointer_assign_rhs_supported(n->b);
                if (ast_is_plain_int_type(elem))
                    return (type_size(elem) == 1 || type_size(elem) == 2) &&
                           ast_int_elem_assign_rhs_ok(n);
            }
            if (ast_index_symbol_nd_elem_type(n->a, &elem)) {
                if (type_is_long(elem))
                    return (n->op == '=' || (is_compound && expr_result_dead)) &&
                           (ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b));
                if (type_is_float(elem))
                    return (n->op == '=' || n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                            n->op == TOK_MULEQ || n->op == TOK_DIVEQ) &&
                           (ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b));
                if (n->op != '=' && !(is_compound && expr_result_dead))
                    return 0;
                if (type_ptr_depth(elem) > 0)
                    return n->op == '=' && type_size(elem) == 2 &&
                           ast_pointer_assign_rhs_supported(n->b);
                if (!ast_is_plain_int_type(elem))
                    return 0;
                return (type_size(elem) == 1 || type_size(elem) == 2) &&
                       ast_int_elem_assign_rhs_ok(n);
            }
            if (ast_index_pointer_expr_elem_type(n->a, &elem)) {
                if (type_is_long(elem))
                    return n->op == '=' &&
                           (ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b));
                if (type_is_float(elem))
                    return (n->op == '=' || n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                            n->op == TOK_MULEQ || n->op == TOK_DIVEQ) &&
                           (ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b));
                if (n->op != '=')
                    return 0;
                if (type_ptr_depth(elem) > 0)
                    return type_size(elem) == 2 && ast_pointer_assign_rhs_supported(n->b);
                if (!ast_is_plain_int_type(elem))
                    return 0;
                return (type_size(elem) == 1 || type_size(elem) == 2) &&
                       ast_int_elem_assign_rhs_ok(n);
            }
            if (n->op == '=' && ast_index_addressable_addr(n->a)) {
                if (ast_index_2d_array_elem_type(n->a, &elem)) {
                    /* elem set by helper */
                } else if (n->a->a != NULL && n->a->a->kind == AST_IDENT) {
                    base = find_sym(n->a->a->sval);
                    if (base == NULL)
                        return 0;
                    decayed = base->is_array ? type_add_ptr(base->type) : base->type;
                    elem = type_decay_ptr(decayed);
                } else {
                    elem = 0;
                }
                if (type_is_long(elem))
                    return ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b);
                if (type_is_float(elem))
                    return ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b);
                if (type_ptr_depth(elem) > 0)
                    return type_size(elem) == 2 && ast_pointer_assign_rhs_supported(n->b);
            }
            if (is_compound && ast_index_addressable_addr(n->a)) {
                if (ast_index_2d_array_elem_type(n->a, &elem)) {
                    /* elem set by helper */
                } else if (n->a->a != NULL && n->a->a->kind == AST_IDENT) {
                    base = find_sym(n->a->a->sval);
                    if (base == NULL)
                        return 0;
                    decayed = base->is_array ? type_add_ptr(base->type) : base->type;
                    elem = type_decay_ptr(decayed);
                } else {
                    elem = 0;
                }
                if (type_is_long(elem))
                    return expr_result_dead &&
                           (ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b));
                if (type_is_float(elem))
                    return (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                            n->op == TOK_MULEQ || n->op == TOK_DIVEQ) &&
                           expr_result_dead &&
                           (ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b));
            }
            if (ast_index_2d_array_elem_type(n->a, &elem)) {
                if (type_is_long(elem))
                    return n->op == '=' &&
                           (ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b));
                if (type_is_float(elem))
                    return (n->op == '=' || n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                            n->op == TOK_MULEQ || n->op == TOK_DIVEQ) &&
                           (ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b));
                if (n->op != '=')
                    return 0;
                if (type_ptr_depth(elem) > 0)
                    return type_size(elem) == 2 && ast_pointer_assign_rhs_supported(n->b);
                if (!ast_is_plain_int_type(elem))
                    return 0;
                return (type_size(elem) == 1 || type_size(elem) == 2) &&
                       ast_int_elem_assign_rhs_ok(n);
            }
            if (ast_index_pointer_array_elem_type(n->a, &elem)) {
                if (n->op != '=')
                    return 0;
                return ast_pointer_assign_rhs_supported(n->b);
            }
            if (ast_index_member_pointer_elem_type(n->a, &elem)) {
                if (type_is_long(elem))
                    return n->op == '=' &&
                           (ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b));
                if (type_is_float(elem))
                    return n->op == '=' &&
                           (ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b));
                if (type_ptr_depth(elem) > 0)
                    return n->op == '=' && type_size(elem) == 2 &&
                           ast_pointer_assign_rhs_supported(n->b);
                if (!ast_is_plain_int_type(elem))
                    return 0;
                return (type_size(elem) == 2 || (n->op == '=' && type_size(elem) == 1)) &&
                       ast_value_is_plain_int(n->b);
            }
            if (n->a->a->kind == AST_MEMBER &&
                ast_member_pointer_array_field_elem_type(n->a->a, &elem)) {
                if (n->op != '=')
                    return 0;
                if (n->a->b == NULL)
                    return 0;
                if (n->a->b->kind == AST_INT_LIT) {
                    if (!ast_value_is_plain_int(n->a->b))
                        return 0;
                } else {
                    if (ast_node_is_const(n->a->b))
                        return 0;
                    if (!ast_gen_supported(n->a->b) || !ast_value_is_plain_int(n->a->b))
                        return 0;
                }
                return ast_pointer_assign_rhs_supported(n->b);
            }
            if (n->a->a->kind == AST_MEMBER &&
                ast_member_array_field_elem_type(n->a->a, &elem)) {
                if (n->op != '=')
                    return is_compound && expr_result_dead &&
                           ast_is_plain_int_type(elem) && type_size(elem) == 2 &&
                           ast_value_is_plain_int(n->b);
                if (!ast_index_subscript_supported(n->a->b))
                    return 0;
                if (type_is_float(elem))
                    return ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b);
                if (type_is_bool(elem))
                    return ast_value_is_plain_int(n->b) || ast_value_is_long_word(n->b) || ast_value_is_float_word(n->b);
                if (type_is_long(elem))
                    return ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b);
                if (type_ptr_depth(elem) > 0)
                    return type_size(elem) == 2 && ast_pointer_assign_rhs_supported(n->b);
                if (!ast_is_plain_int_type(elem))
                    return 0;
                return type_size(elem) == 1 || type_size(elem) == 2;
            }
            if (!ast_index_plain_int_read(n->a))
                return 0;
            if (n->a->a->kind == AST_IDENT) {
                if (!ast_int_elem_assign_rhs_ok(n))
                    return 0;
                base = find_sym(n->a->a->sval);
                decayed = base->is_array ? type_add_ptr(base->type) : base->type;
                elem = type_decay_ptr(decayed);
                /* A byte element normally requires plain `=`; also accept a
                 * dead-result compound assign (`a[i] += k;` as its own
                 * statement, e.g. inside a for-loop body) the same way the
                 * N-D array and pointer-element branches above already do -
                 * this final fallback (a plain local/global 1-D array
                 * reached by a computed index) had no such exception, so
                 * every byte array here declined += even though nothing
                 * about reaching the array through this specific helper
                 * makes that unsafe. */
                if (type_size(elem) != 2 &&
                    (type_size(elem) != 1 ||
                     (n->op != '=' && !(is_compound && expr_result_dead))))
                    return 0;
                if (type_size(elem) == 1 && base->is_array &&
                    (base->storage == SC_GLOBAL || base->storage == SC_EXTERN))
                    return expr_result_dead && n->op == '=';
            } else if (n->a->a->kind == AST_MEMBER) {
                if (ast_member_plain_array_field_elem_type(n->a->a, &elem)) {
                    if (!ast_value_is_plain_int(n->b))
                        return 0;
                    if (type_size(elem) != 2 && (n->op != '=' || type_size(elem) != 1))
                        return 0;
                } else {
                    int field_type;
                    if (!ast_member_lvalue_type(n->a->a, &field_type))
                        return 0;
                    if (type_ptr_depth(field_type) <= 0)
                        return 0;
                    elem = type_decay_ptr(field_type);
                    if (!ast_is_plain_int_type(elem))
                        return 0;
                    if (type_size(elem) != 2 && (n->op != '=' || type_size(elem) != 1))
                        return 0;
                    if (!ast_value_is_plain_int(n->b))
                        return 0;
                }
            } else {
                return 0;
            }
            return 1;
        }
        /* Member lvalue store: s.f = rhs / p->f OP= rhs.  Plain int fields use
         * the general store lowering; long and float fields also use the
         * wide-value tail for the supported arithmetic compound operators. */
        if (n->a->kind == AST_MEMBER) {
            int field_type;
            if (ast_member_bitfield_lvalue_type(n->a, &field_type))
                return ast_value_is_plain_int(n->b);
            if (!ast_member_lvalue_type(n->a, &field_type))
                return 0;
            if (type_is_long(field_type)) {
                if (n->op == '=')
                    return ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b) ||
                           ast_value_is_float_word(n->b);
                if (n->op == TOK_SHLEQ || n->op == TOK_SHREQ)
                    return ast_value_is_plain_int(n->b) || ast_value_is_long_word(n->b);
                if (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                    n->op == TOK_MULEQ || n->op == TOK_DIVEQ || n->op == TOK_MODEQ ||
                    n->op == TOK_ANDEQ || n->op == TOK_OREQ  || n->op == TOK_XOREQ)
                    return ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b);
                return 0;
            }
            if (type_is_float(field_type)) {
                if (n->op == '=')
                    return ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b) ||
                           ast_value_is_long_word(n->b);
                  return (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                        n->op == TOK_MULEQ || n->op == TOK_DIVEQ) &&
                       (ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b) ||
                        ast_value_is_long_word(n->b));
            }
            if (type_is_bool(field_type))
                return n->op == '=' &&
                       (ast_value_is_plain_int(n->b) || ast_value_is_long_word(n->b) || ast_value_is_float_word(n->b));
            if (type_ptr_depth(field_type) > 0) {
                if (n->op == '=')
                    return type_size(field_type) == 2 &&
                           ast_pointer_assign_rhs_supported(n->b);
                if (type_size(field_type) == 2 &&
                    (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ))
                    return ast_value_is_plain_int(n->b);
                return 0;
            }
            if (!ast_value_is_plain_int(n->b))
                return 0;
            if (type_size(field_type) == 1)
                return 1;
            if (type_size(field_type) != 2)
                return 0;
            return 1;
        }
        /* Deref lvalue store: *p = rhs / *p OP= rhs.  `*ident` always uses the
         * general store lowering (no byte fast path matches a leading `*`), so
         * both char* and int* targets are safe.  ast_deref_plain_int_read
         * validates a single-level pointer to a plain int/char (size 1 or 2). */
        if (n->a->kind == AST_UNARY && n->a->op == '*') {
            int deref_type;
            if (!ast_deref_lvalue_type(n->a, &deref_type))
                return 0;
            if (ast_is_plain_int_type(deref_type) &&
                (type_size(deref_type) == 1 || type_size(deref_type) == 2))
                return ast_value_is_plain_int(n->b);
            if (n->op != '=') {
                if (type_is_long(deref_type) &&
                    (n->op == TOK_SHLEQ || n->op == TOK_SHREQ))
                    return ast_value_is_plain_int(n->b) || ast_value_is_long_word(n->b);
                if (type_is_long(deref_type) &&
                    (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                     n->op == TOK_MULEQ || n->op == TOK_DIVEQ ||
                     n->op == TOK_MODEQ || n->op == TOK_ANDEQ ||
                     n->op == TOK_OREQ  || n->op == TOK_XOREQ))
                    return ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b);
                return type_is_float(deref_type) &&
                       (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                        n->op == TOK_MULEQ || n->op == TOK_DIVEQ) &&
                       (ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b) ||
                        ast_value_is_long_word(n->b));
            }
            if (type_is_long(deref_type))
                return ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b) ||
                       ast_value_is_float_word(n->b);
            if (type_is_float(deref_type))
                return ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b) ||
                       ast_value_is_long_word(n->b);
            if (type_ptr_depth(deref_type) > 0)
                return type_size(deref_type) == 2 && ast_pointer_assign_rhs_supported(n->b);
            return 0;
        }
        if (n->a->kind != AST_IDENT)
            return 0;
        s = find_sym(n->a->sval);
        if (s == NULL)
            return 0;
        if (s->is_array || s->is_const_value)
            return 0;
        if (type_is_struct_object(s->type))
            return n->op == '=' &&
                   ast_struct_return_call_assign_supported(s->type, n->b);
        if (type_is_float(s->type)) {
            if (n->op == '=')
                return ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b) ||
                       ast_value_is_long_word(n->b);
            if (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                n->op == TOK_MULEQ || n->op == TOK_DIVEQ)
                return ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b) ||
                       ast_value_is_long_word(n->b);
            return 0;
        }
        if (type_is_long(s->type)) {
            int va_type;
            if (n->op == '=' && ast_va_arg_deref_type(n->b, &va_type))
                return type_is_long(va_type);
            if (n->op == '=')
                return ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b) ||
                       ast_value_is_float_word(n->b);
            if (n->op == TOK_SHLEQ || n->op == TOK_SHREQ)
                return ast_value_is_plain_int(n->b) || ast_value_is_long_word(n->b);
            if (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                n->op == TOK_MULEQ || n->op == TOK_DIVEQ || n->op == TOK_MODEQ ||
                n->op == TOK_ANDEQ || n->op == TOK_OREQ  || n->op == TOK_XOREQ)
                return ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b);
            return 0;
        }
        if (!sym_can_ix_direct(s) && !is_global_word_sym(s) &&
            !(n->op == '=' && type_size(s->type) == 1 &&
                            (s->storage == SC_GLOBAL || s->storage == SC_EXTERN)) &&
                        !(expr_result_dead && type_size(s->type) == 1 &&
                            (n->op == TOK_ANDEQ || n->op == TOK_OREQ ||
                             n->op == TOK_XOREQ))) {
            if (n->op != '=') {
                if ((n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                     n->op == TOK_ANDEQ || n->op == TOK_OREQ ||
                     n->op == TOK_XOREQ) &&
                    ast_is_plain_int_type(s->type) &&
                    (type_size(s->type) == 1 || type_size(s->type) == 2))
                    return ast_value_is_plain_int(n->b);
                return 0;
            }
            if (type_ptr_depth(s->type) > 0)
                return expr_result_dead && type_size(s->type) == 2 &&
                       ast_pointer_assign_rhs_supported(n->b);
            if (!ast_value_is_plain_int(n->b)) {
                /* A long-word rhs narrows to a size-2 plain int by storing
                 * only its low word.  The non-ix-direct store tail
                 * (emit_store_de_to_addr_hl with a 2-byte type) already does
                 * exactly that, so accept it the same way the ix-direct /
                 * global-word path does.  Without this, an out-of-int-range
                 * constant such as `p = -32768;` (a long literal on the
                 * 16-bit target) is rejected only when the frame is large
                 * enough to push the local out of ix-direct range. */
                if (!(n->op == '=' && ast_is_plain_int_type(s->type) &&
                      type_size(s->type) == 2 && ast_value_is_long_word(n->b)))
                    return 0;
            }
            if (!ast_is_plain_int_type(s->type))
                return 0;
            if (type_size(s->type) != 1 && type_size(s->type) != 2)
                return 0;
            return 1;
        }
        if (expr_result_dead &&
            (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ) &&
            !type_is_long(s->type) && !type_is_float(s->type)) {
            if (n->b->kind == AST_INT_LIT)
                return 1;
            if (n->b->kind == AST_IDENT) {
                struct Sym *rs = find_sym(n->b->sval);
                return sym_can_ix_direct(rs) || is_global_word_sym(rs) ||
                       (rs != NULL &&
                        (rs->storage == SC_GLOBAL || rs->storage == SC_EXTERN) &&
                        !rs->is_array && type_size(rs->type) == 1 &&
                        ast_is_plain_int_type(rs->type));
            }
            if (ast_const_plain_int_binary_supported(n->b))
                return 1;
            if (n->b->kind == AST_MEMBER && ast_member_plain_int_read(n->b))
                return 1;
            if (type_ptr_depth(s->type) > 0)
                return ast_gen_supported(n->b) && ast_value_is_plain_int(n->b);
        }
        if (type_ptr_depth(s->type) > 0) {
            if (n->op != '=' || type_size(s->type) != 2)
                return 0;
            return ast_pointer_assign_rhs_supported(n->b);
        }
        if (!ast_is_plain_int_type(s->type))
            return 0;
        if (type_is_bool(s->type) && n->op == '=')
            return ast_value_is_plain_int(n->b) || ast_value_is_long_word(n->b) || ast_value_is_float_word(n->b);
        if (type_size(s->type) == 2 && n->op == '=' &&
            ast_value_is_float_word(n->b))
            return 1;
        if (type_size(s->type) == 2 && n->op == '=' &&
            ast_value_is_long_word(n->b))
            return 1;
        if (type_size(s->type) == 2 &&
            (n->op == TOK_MULEQ || n->op == TOK_DIVEQ || n->op == TOK_MODEQ) &&
            ast_long_word_type(n->b, NULL))
            return 1;
        if (type_size(s->type) == 2 &&
            (n->op == TOK_ANDEQ || n->op == TOK_OREQ || n->op == TOK_XOREQ) &&
            ast_value_is_long_word(n->b))
            return 1;
        if (type_size(s->type) == 2 && n->op == '=' &&
            n->b->kind == AST_CAST && n->b->a != NULL) {
            long fv;
            if (ast_int_const_cast_fold(n->b, &fv))
                return 1;
        }
        if (type_size(s->type) == 1 && n->op == '=' &&
            ast_value_is_float_word(n->b))
            return 1;
        if (type_size(s->type) == 1 && n->op == '=') {
            long fv;
            if (ast_int_const_cast_fold(n->b, &fv))
                return 1;
        }
        if (type_size(s->type) == 1 && n->op == '=' &&
            n->b->kind == AST_CALL && ast_value_is_long_word(n->b))
            return 1;
        if (!ast_value_is_plain_int(n->b))
            return 0;
        if (type_size(s->type) == 1) {
            long fv;
            if (n->op != '=') {
                if (expr_result_dead &&
                    (s->storage == SC_GLOBAL || s->storage == SC_EXTERN) &&
                    (n->op == TOK_ANDEQ || n->op == TOK_OREQ ||
                     n->op == TOK_XOREQ))
                    return ast_value_is_plain_int(n->b);
                return sym_can_ix_direct(s);
            }
            if (s->storage == SC_GLOBAL || s->storage == SC_EXTERN)
                return 1;
            if (!sym_can_ix_direct(s))
                return 0;
            /* Slice 3: store a supported plain-int rvalue (comparison,
             * arithmetic, ...) into a size-1 (char/bool) ix-direct local.  The
             * general size-1 `=` emit tail evaluates the rhs into HL and
             * emit_store_hl_to_sym_direct stores L only, truncating correctly.
             * Calls use a dedicated emitter below with a truncating
             * store-from-call tail (no byte->int promote).
             * A long-returning call truncates to the low byte the same way
             * (handled by the early long-call return above). */
            if (ast_gen_supported(n->b) && ast_value_is_plain_int(n->b))
                return 1;
            /* Constant casts outside the general support gate can still fold
             * to an immediate store. */
            if (n->b->kind == AST_INT_LIT && n->b->ival >= 0 &&
                n->b->ival <= 255)
                return 1;
            if (ast_unary_int_const_fold(n->b, &fv))
                return 1;
            if (n->b->kind == AST_CAST && n->b->a != NULL &&
                ast_unary_int_const_fold(n->b->a, &fv))
                return 1;
            return 0;
        }
        if ((s->type & 15) != TYPE_INT || type_size(s->type) != 2)
            return 0;
        if (is_compound && expr_result_dead &&
            (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ)) {
            /* True dead-result fast RHS shapes (`i += 1`, `i += j`,
             * `i += s.f`) return above and use the dedicated fast emitter.
             * Other supported plain-int RHS expressions use the general direct
             * compound tail emitted by gen_assign_ast. */
            return ast_value_is_plain_int(n->b);
        }
        return 1;
    }
    case AST_INDEX: {
        int ptr_type;
        int no_deref;
        return ast_index_plain_int_read(n) || ast_index_long_read(n) ||
               ast_index_float_read(n) ||
               ast_pointer_expr_type(n, &ptr_type, &no_deref);
    }
    case AST_CALL: {
        /* Direct, named call `f(a, b, ...)` whose arguments are all 16-bit
         * word values (plain ints or string-literal pointers) pushed as one
         * word each.  Emits the named-call tail (reverse-order arg push +
         * `call name` + emit_cleanup_stack_bytes) for the common case.  Handled
         * elsewhere: calls through a function POINTER (indirect __call_hl);
         * the name-recognised builtins with argument fast paths
         * (__va_start/__va_arg/__va_end, strcpy, strlen, strchr, cb_is_zero(p));
         * any prototype that widens an argument to float/long/struct (the push
         * is then 4 bytes / an address, not a single word); and any other
         * non-word argument (a float/long/struct actual, a pointer expression
         * not yet covered by the AST, etc.). */
        int rt;
        struct Sym *cs;
        if (ast_call_star_indirect_supported(n))
            return 1;
        if (ast_call_indirect_supported(n))
            return 1;
        if (ast_va_builtin_supported(n))
            return 1;
        if (!ast_call_named_args_supported(n))
            return 0;
        cs = find_global(n->a->sval);
        rt = cs != NULL ? cs->type : TYPE_INT;
        if (type_is_struct_object(rt))
            return expr_result_dead;
        return 1;
    }
    case AST_POSTFIX:
        return ast_postfix_plain_int(n);
    case AST_MEMBER:
        {
            int elem_type;
            return ast_member_plain_int_read(n) || ast_member_bitfield_read(n) ||
                   ast_member_long_read(n) || ast_member_float_read(n) ||
                   ast_member_pointer_read(n) ||
                   ast_member_array_field_elem_type(n, &elem_type);
        }
    case AST_LOGAND:
    case AST_LOGOR:
        /* Short-circuit `a && b` / `a || b`: pure test-and-branch producing a
         * 0/1 int, no arithmetic conversion.  The builder nests these
         * left-associatively, so a recursive walk emits a flat short-circuit
         * chain label-for-label.  A wholly constant logical is folded by the
         * constant evaluator -> decline.  Operands are required to be supported
         * plain-int values so the tested type stays 16-bit. */
        if (ast_node_is_const(n))
            return 0;
        if (!ast_logical_operand_ok(n->a) || !ast_logical_operand_ok(n->b))
            return 0;
        return 1;
    case AST_COND:
        /* `cond ? a : b` with plain-int condition and both arms plain int.
         * For this case the conditional emit takes neither the float nor the
         * long-result path: result_is_float is false (no float arm) and
         * need_long_result is false (no long arm), so the emit reduces to
         * test + true-arm + speculative emit_extend_to_long(true arm) + jp end
         * + false-arm + common_arith_type result.  Restricting all three parts
         * to supported plain-int values keeps that shape and avoids the
         * float-arm and long-widening branches entirely. */
        {
            int ptr_type;
            int no_deref;
            if (ast_pointer_expr_type(n, &ptr_type, &no_deref))
                return 1;
        }
        if (ast_cond_void_supported(n))
            return 1;
        return ast_cond_numeric_supported(n);
    case AST_CAST:
        /* `(type)expr` to an integer target: evaluate the operand, then
         * widen/narrow.  Constant operands are folded to an immediate, so
         * decline to keep it.  Float/long/struct/pointer targets decline
         * (specialised handling). */
        {
            long folded;
            if (ast_int_const_cast_fold(n, &folded))
                return 1;
        }
        if (n->a == NULL)
            return 0;
        if ((n->type & 15) == TYPE_VOID)
            return ast_gen_supported(n->a);
        if (type_is_float(n->type)) {
            if (!ast_gen_supported(n->a))
                return 0;
            return ast_value_is_float_word(n->a) || ast_value_is_plain_int(n->a) ||
                   ast_value_is_long_word(n->a);
        }
        if (type_is_long(n->type)) {
            if (!ast_gen_supported(n->a))
                return 0;
            return ast_value_is_plain_int(n->a) || ast_value_is_long_word(n->a) ||
                   ast_value_is_float_word(n->a) || ast_value_is_pointer_word(n->a);
        }
        if (type_ptr_depth(n->type) > 0 && type_size(n->type) == 2) {
            /* `(T *)expr`: pointers are 16-bit words, so gen_cast_ast just
             * evaluates the operand and retags the type - no narrowing or
             * conversion.  Accept pointer-typed operands (mirroring
             * ast_pointer_expr_type's cast rule), integer constants, and
             * plain-int / pointer-word values. */
            int pt, nd;
            if (ast_node_is_const(n->a))
                return ast_value_is_plain_int(n->a);
            if (ast_pointer_expr_type(n->a, &pt, &nd))
                return !nd;
            if (!ast_gen_supported(n->a))
                return 0;
            return ast_value_is_plain_int(n->a) || ast_value_is_pointer_word(n->a);
        }
        if (!ast_is_plain_int_type(n->type) || type_size(n->type) > 2)
            return 0;
        if (ast_node_is_const(n->a))
            return ast_gen_supported(n->a) &&
                   (ast_value_is_plain_int(n->a) || ast_value_is_long_word(n->a) ||
                    ast_value_is_float_word(n->a));
        if (n->a->kind != AST_CALL && n->a->kind != AST_IDENT &&
            n->a->kind != AST_INDEX && n->a->kind != AST_MEMBER &&
            n->a->kind != AST_BINARY && n->a->kind != AST_UNARY &&
            n->a->kind != AST_COMMA && n->a->kind != AST_COND &&
            n->a->kind != AST_CAST &&
            n->a->kind != AST_LOGAND && n->a->kind != AST_LOGOR &&
            n->a->kind != AST_SIZEOF_EXPR && n->a->kind != AST_SIZEOF_TYPE)
            return 0;                  /* avoid ptr-sub / folded const operands */
        if (n->a->a != NULL && n->a->a->kind == AST_IDENT &&
            n->a->a->sval != NULL && !strcmp(n->a->a->sval, "__offsetof"))
            return 0;                  /* handled by the constant folder */
        return ast_gen_supported(n->a);
    case AST_COMMA:
        /* `a , b`: evaluate the left operand (value discarded), then the
         * right, whose value/type is the result.  Gate the left under the same
         * dead-result rules used by expression statements, so forms such as
         * `(ptr++, *ptr)` do not require a nonexistent value-context lowering
         * for the discarded pointer postfix result. */
        return (ast_is_local_self_add_stmt(n->a) || ast_dead_expr_supported(n->a)) &&
               ast_gen_supported(n->b);
    default:
        return 0;
    }
}

int ast_call_arg_word_supported(const struct AstNode *arg)
{
    struct Sym *s;
    int ptr_type;
    int no_deref;

    if (arg == NULL)
        return 0;
    if (ast_pointer_expr_type(arg, &ptr_type, &no_deref))
        return 1;
    if (!ast_gen_supported(arg))
        return 0;
    if (arg->kind == AST_STR_LIT)
        return 1;                         /* char * literal in HL */
    if (arg->kind == AST_UNARY && arg->op == '&')
        return 1;                         /* object address in HL */
    if (arg->kind == AST_IDENT) {
        s = find_sym(arg->sval);
        if (s != NULL && !s->is_const_value && s->storage == SC_FUNC)
            return 1;                     /* function designator in HL */
        if (s != NULL && !s->is_const_value && s->storage != SC_FUNC &&
            (s->is_array || type_ptr_depth(s->type) > 0))
            return 1;                     /* pointer/array value in HL */
    }
    return ast_value_is_plain_int(arg);
}

int ast_call_struct_arg_supported(int want_type, const struct AstNode *arg)
{
    int arg_type;

    if (!type_is_struct_object(want_type) || arg == NULL)
        return 0;
    if (ast_struct_addr_expr_supported(arg, &arg_type))
        return same_struct_type(want_type, arg_type);
    if (arg->kind == AST_CALL)
        return ast_struct_return_call_assign_supported(want_type, arg);
    return 0;
}

void gen_call_struct_arg_ast(const struct AstNode *arg, int want_type)
{
    int arg_type;

    if (arg->kind == AST_CALL) {
        gen_struct_return_call_arg_ast(arg, want_type);
        return;
    }
    gen_struct_addr_expr_ast(arg, &arg_type);
    (void)arg_type;
    emit_push_struct_arg_from_hl(type_size(want_type));
}

void gen_struct_return_call_arg_ast(const struct AstNode *call,
                                           int want_type)
{
    const char *name = call->a->sval;
    struct Sym *fn_sym = find_global(name);
    int struct_bytes = type_size(want_type);
    int arg_bytes = 0;
    int old_dead;
    int i;

    /* This emits its own `call` directly rather than going through
     * gen_call_ast, so it needs its own deferred_body_needed marking too. */
    if (fn_sym != NULL && fn_sym->is_static)
        fn_sym->deferred_body_needed = 1;

    fprintf(outf, "\tld hl,-%d\n", struct_bytes);
    emit("\tadd hl,sp\n");
    emit("\tld sp,hl\n");
    emit("\tpush hl\n");

    old_dead = expr_result_dead;
    expr_result_dead = 0;
    for (i = call->list_len - 1; i >= 0; --i) {
        int actual_type;
        int inner_want;
        int have_want;
        int ptr_type;
        int no_deref;

        have_want = expected_arg_type(fn_sym, i, &inner_want);
        if (have_want && type_is_struct_object(inner_want)) {
            gen_call_struct_arg_ast(call->list[i], inner_want);
            arg_bytes += type_size(inner_want);
            continue;
        }

        if (ast_pointer_expr_type(call->list[i], &ptr_type, &no_deref))
            gen_pointer_expr_ast(call->list[i], &ptr_type, &no_deref);
        else
            ast_gen_expr(call->list[i]);
        actual_type = g_expr_type;
        if (have_want && type_is_float(inner_want)) {
            if (!type_is_float(actual_type))
                emit_convert_int_to_float(actual_type);
            emit("\tpush de\n\tpush hl\n");
            arg_bytes += 4;
        } else if (have_want && type_is_long(inner_want)) {
            if (!type_is_long(actual_type))
                emit_promote_int_to_long(actual_type, inner_want);
            emit("\tpush de\n\tpush hl\n");
            arg_bytes += 4;
        } else if (have_want && !type_is_long(inner_want) &&
                   !type_is_float(inner_want)) {
            emit("\tpush hl\n");
            arg_bytes += 2;
        } else if (type_is_long(actual_type) || type_is_float(actual_type)) {
            emit("\tpush de\n\tpush hl\n");
            arg_bytes += 4;
        } else {
            emit("\tpush hl\n");
            arg_bytes += 2;
        }
    }
    expr_result_dead = old_dead;

    emit_load_hl_from_sp_offset(arg_bytes);
    emit("\tpush hl\n");
    emit_extrn_if_needed(fn_sym);
    fprintf(outf, "\tcall %s\n", asm_name_for(name));
    emit_cleanup_stack_bytes(arg_bytes + 2);
    emit("\tpop bc\n");
    g_expr_type = want_type;
    g_long_from16 = 0;
}

static int ast_update_lvalue_long_type(const struct AstNode *n, int *out_type)
{
    struct Sym *s;
    int val_type;

    if (n == NULL)
        return 0;
    if (n->kind == AST_INDEX) {
        if (!ast_index_lvalue_elem_type(n, &val_type) || !type_is_long(val_type))
            return 0;
        if (out_type)
            *out_type = val_type;
        return 1;
    }
    if (n->kind == AST_MEMBER) {
        if (!ast_member_lvalue_type(n, &val_type) || !type_is_long(val_type))
            return 0;
        if (out_type)
            *out_type = val_type;
        return 1;
    }
    if (n->kind == AST_UNARY && n->op == '*') {
        if (!ast_deref_lvalue_type(n, &val_type) || !type_is_long(val_type))
            return 0;
        if (out_type)
            *out_type = val_type;
        return 1;
    }
    if (n->kind != AST_IDENT)
        return 0;
    s = find_sym(n->sval);
    if (s == NULL || s->is_const_value || s->storage == SC_FUNC ||
        s->is_array || !type_is_long(s->type))
        return 0;
    if (out_type)
        *out_type = s->type;
    return 1;
}

int ast_value_is_long_word(const struct AstNode *arg)
{
    struct Sym *s;

    if (arg == NULL)
        return 0;
    if (arg->kind == AST_INT_LIT)
        return type_is_long(arg->type);
    if (arg->kind == AST_UNARY) {
        long fv;
        if (ast_unary_long_const_fold(arg, &fv))
            return 1;
        if (arg->op == '*')
            return ast_deref_long_read(arg);
        if ((arg->op == TOK_INC || arg->op == TOK_DEC) && arg->a != NULL)
            return ast_update_lvalue_long_type(arg->a, NULL);
        if ((arg->op == '-' || arg->op == '~' || arg->op == '+') &&
            arg->a != NULL && ast_value_is_long_word(arg->a))
            return 1;
    }
    if (arg->kind == AST_POSTFIX &&
        (arg->op == TOK_INC || arg->op == TOK_DEC) &&
        arg->a != NULL) {
        /* `ul++` / `ul--` as a long-word value (e.g. `old = ul++`).  The
         * postfix emitter now updates the full 32-bit word with carry and
         * returns the old value in DE:HL. */
        return ast_update_lvalue_long_type(arg->a, NULL);
    }
    if (!ast_gen_supported(arg))
        return 0;
    if (arg->kind == AST_COMPOUND_LITERAL)
        return type_is_long(arg->type);
    if (arg->kind == AST_IDENT) {
        s = find_sym(arg->sval);
         return s != NULL && s->storage != SC_FUNC && !s->is_array &&
             type_is_long(s->type);
    }
    if (arg->kind == AST_CALL && arg->a != NULL && arg->a->kind == AST_IDENT) {
        s = find_global(arg->a->sval);
        if (s != NULL && s->storage == SC_FUNC)
            return type_is_long(s->type);
    }
    if (arg->kind == AST_CALL && ast_call_indirect_supported(arg)) {
        int callee_type;
        int no_deref;
        if (ast_pointer_expr_type(arg->a, &callee_type, &no_deref))
            return type_is_long(type_decay_ptr(callee_type));
    }
    if (arg->kind == AST_CALL && ast_call_star_indirect_supported(arg)) {
        s = ast_indirect_call_proto_sym(arg);
        return s != NULL && type_is_long(type_decay_ptr(s->type));
    }
    if (arg->kind == AST_ASSIGN && arg->a != NULL) {
        int lhs_type;
        if (arg->a->kind == AST_IDENT) {
            s = find_sym(arg->a->sval);
            return s != NULL && !s->is_const_value && s->storage != SC_FUNC &&
                   !s->is_array && type_is_long(s->type);
        }
        if (arg->a->kind == AST_UNARY && arg->a->op == '*' &&
            ast_deref_lvalue_type(arg->a, &lhs_type))
            return type_is_long(lhs_type);
        if (arg->a->kind == AST_INDEX &&
            ast_index_lvalue_elem_type(arg->a, &lhs_type))
            return type_is_long(lhs_type);
        if (arg->a->kind == AST_MEMBER && ast_member_lvalue_type(arg->a, &lhs_type))
            return type_is_long(lhs_type);
    }
    if (arg->kind == AST_CAST && type_is_long(arg->type))
        return ast_gen_supported(arg);
    if (arg->kind == AST_COMMA)
        return ast_value_is_long_word(arg->b);
    if (arg->kind == AST_COND)
        return ast_cond_result_is_long(arg);
    if (arg->kind == AST_BINARY && is_shift_op(arg->op))
        return ast_value_is_long_word(arg->a) && ast_value_is_plain_int(arg->b);
    if (arg->kind == AST_BINARY && ast_long_arith_supported(arg))
        return 1;
    if (arg->kind == AST_BINARY && ast_mixed_long_rhs_arith_supported(arg))
        return 1;
    if (arg->kind == AST_INDEX)
        return ast_index_long_read(arg);
    return ast_member_long_read(arg);
}

int ast_long_word_type(const struct AstNode *arg, int *out_type)
{
    struct Sym *rhs_sym;

    if (!ast_value_is_long_word(arg))
        return 0;

    switch (arg->kind) {
    case AST_INT_LIT:
    case AST_CAST:
        if (out_type)
            *out_type = arg->type;
        return type_is_long(arg->type);
    case AST_IDENT:
        rhs_sym = find_sym(arg->sval);
        if (rhs_sym == NULL || rhs_sym->is_const_value || rhs_sym->storage == SC_FUNC ||
            rhs_sym->is_array || !type_is_long(rhs_sym->type))
            return 0;
        if (out_type)
            *out_type = rhs_sym->type;
        return 1;
    case AST_UNARY:
        if (arg->op != TOK_INC && arg->op != TOK_DEC)
            return 0;
        return ast_update_lvalue_long_type(arg->a, out_type);
    case AST_POSTFIX:
        return ast_update_lvalue_long_type(arg->a, out_type);
    case AST_CALL:
        if (arg->a == NULL || arg->a->kind != AST_IDENT)
            return 0;
        rhs_sym = find_global(arg->a->sval);
        if (rhs_sym == NULL || !type_is_long(rhs_sym->type))
            return 0;
        if (out_type)
            *out_type = rhs_sym->type;
        return 1;
    default:
        return 0;
    }
}

int ast_call_arg_supported(struct Sym *fn_sym, int arg_index,
                                  const struct AstNode *arg)
{
    int want_type;
    int ptr_type;
    int no_deref;
    struct Sym *arg_sym;

    if (arg == NULL)
        return 0;
    if (ast_pointer_expr_type(arg, &ptr_type, &no_deref))
        return 1;
    if (expected_arg_type(fn_sym, arg_index, &want_type)) {
        if (type_is_struct_object(want_type))
            return ast_call_struct_arg_supported(want_type, arg);
        if (type_is_float(want_type))
            return ast_value_is_float_word(arg) || ast_call_arg_word_supported(arg) ||
                   ast_value_is_long_word(arg);
        if (type_is_long(want_type))
            return ast_value_is_long_word(arg) || ast_call_arg_word_supported(arg);
        return ast_call_arg_word_supported(arg) || ast_value_is_long_word(arg) ||
               ast_value_is_float_word(arg);
    }
    if (arg->kind == AST_IDENT) {
        arg_sym = find_sym(arg->sval);
        if (arg_sym != NULL && type_is_struct_object(arg_sym->type))
            return 1;
    }
    return ast_call_arg_word_supported(arg) || ast_value_is_long_word(arg) ||
           ast_value_is_float_word(arg);
}

/* `va_start(ap, last)` / `va_end(ap)` after macro expansion to the builtin
 * calls `__va_start(ap, last)` / `__va_end(ap)`.  Both take bare identifier
 * arguments naming a va_list cursor (and, for va_start, the last fixed
 * parameter).  gen_call_ast emits the required address arithmetic for the
 * builtin.  va_arg is not handled here (its `type` argument is a sizeof the AST
 * does not preserve as a simple operand). */
int ast_va_builtin_supported(const struct AstNode *n)
{
    const char *cname;
    if (n == NULL || n->kind != AST_CALL || n->a == NULL ||
        n->a->kind != AST_IDENT)
        return 0;
    cname = n->a->sval;
    if (!strcmp(cname, "__va_end"))
        return n->list_len == 1 && n->list[0] != NULL &&
               n->list[0]->kind == AST_IDENT &&
               find_sym(n->list[0]->sval) != NULL;
    if (!strcmp(cname, "__va_start"))
        return n->list_len == 2 &&
               n->list[0] != NULL && n->list[0]->kind == AST_IDENT &&
               n->list[1] != NULL && n->list[1]->kind == AST_IDENT &&
               find_sym(n->list[0]->sval) != NULL &&
               find_sym(n->list[1]->sval) != NULL;
    return 0;
}

/* Direct named call with all arguments supported (return type ignored). */
int ast_call_named_args_supported(const struct AstNode *n)
{
    struct Sym *call_sym;
    struct Sym *fn_sym;
    const char *cname;
    int i;
    if (n == NULL || n->kind != AST_CALL || n->a == NULL ||
        n->a->kind != AST_IDENT)
        return 0;
    cname = n->a->sval;
    if (!strcmp(cname, "__va_start") || !strcmp(cname, "__va_arg") ||
        !strcmp(cname, "__va_end") ||
        (!strcmp(cname, "cb_is_zero") && n->list_len == 1))
        return 0;
    call_sym = find_sym(cname);
    if (call_sym != NULL && call_sym->storage != SC_FUNC &&
        type_ptr_depth(call_sym->type) > 0)
        return 0;
    fn_sym = find_global(cname);
    for (i = 0; i < n->list_len; ++i) {
        if (!ast_call_arg_supported(fn_sym, i, n->list[i]))
            return 0;
    }
    return 1;
}

const struct AstNode *ast_call_star_indirect_base(const struct AstNode *n)
{
    const struct AstNode *callee;
    int saw_star;

    if (n == NULL || n->kind != AST_CALL || n->a == NULL)
        return NULL;
    callee = n->a;
    saw_star = 0;
    while (callee != NULL && callee->kind == AST_UNARY && callee->op == '*') {
        saw_star = 1;
        callee = callee->a;
    }
    return saw_star ? callee : NULL;
}

struct Sym *ast_indirect_call_proto_sym(const struct AstNode *n)
{
    const struct AstNode *callee;

    if (n == NULL || n->kind != AST_CALL || n->a == NULL)
        return NULL;
    callee = n->a;
    while (callee != NULL && callee->kind == AST_UNARY && callee->op == '*')
        callee = callee->a;
    while (callee != NULL && callee->kind == AST_INDEX)
        callee = callee->a;
    if (callee != NULL && callee->kind == AST_IDENT)
        return callee->sym != NULL ? callee->sym : find_sym(callee->sval);
    return NULL;
}

int ast_call_star_indirect_supported(const struct AstNode *n)
{
    const struct AstNode *base;
    struct Sym *s;
    int callee_type;
    int no_deref;
    int i;
    struct Sym *proto;

    base = ast_call_star_indirect_base(n);
    if (base == NULL)
        return 0;
    if (base->kind == AST_IDENT) {
        s = find_sym(base->sval);
        if (s == NULL || s->is_const_value || s->is_array)
            return 0;
        if (s->storage != SC_FUNC && type_ptr_depth(s->type) <= 0)
            return 0;
    } else {
        if (base->kind == AST_INDEX) {
            if (!ast_index_lvalue_elem_type(base, &callee_type) ||
                type_ptr_depth(callee_type) <= 0 || type_size(callee_type) != 2)
                callee_type = TYPE_INT | TYPE_PTR;
            no_deref = 0;
        } else if (!ast_pointer_expr_type(base, &callee_type, &no_deref) || no_deref) {
                return 0;
        }
        if (type_ptr_depth(callee_type) <= 0 || type_size(callee_type) != 2)
            return 0;
    }
    proto = ast_indirect_call_proto_sym(n);
    if (proto != NULL && proto->has_proto &&
        ((!proto->proto_variadic && n->list_len != proto->proto_nargs) ||
         (proto->proto_variadic && n->list_len < proto->proto_nargs)))
        return 0;
    for (i = 0; i < n->list_len; ++i) {
        if (!ast_call_arg_supported(proto, i, n->list[i]))
            return 0;
    }
    return 1;
}

int ast_call_indirect_supported(const struct AstNode *n)
{
    struct Sym *callee_sym;
    int callee_type;
    int no_deref;
    int i;
    struct Sym *proto;

    if (n == NULL || n->kind != AST_CALL || n->a == NULL)
        return 0;
    if (n->a->kind == AST_IDENT) {
        callee_sym = find_sym(n->a->sval);
        if (callee_sym == NULL || callee_sym->storage == SC_FUNC ||
            type_ptr_depth(callee_sym->type) <= 0)
            return 0;
    }
    if (!ast_pointer_expr_type(n->a, &callee_type, &no_deref) || no_deref)
        return 0;
    if (type_ptr_depth(callee_type) <= 0 || type_size(callee_type) != 2)
        return 0;
    if (type_is_struct_object(type_decay_ptr(callee_type)))
        return 0;
    proto = ast_indirect_call_proto_sym(n);
    if (proto != NULL && proto->has_proto &&
        ((!proto->proto_variadic && n->list_len != proto->proto_nargs) ||
         (proto->proto_variadic && n->list_len < proto->proto_nargs)))
        return 0;
    for (i = 0; i < n->list_len; ++i) {
        if (!ast_call_arg_supported(proto, i, n->list[i]))
            return 0;
    }
    return 1;
}

int ast_value_is_float_word(const struct AstNode *arg)
{
    struct Sym *s;

    if (arg == NULL || !ast_gen_supported(arg))
        return 0;
    if (arg->kind == AST_FLOAT_LIT)
        return 1;
    if (arg->kind == AST_COMPOUND_LITERAL)
        return type_is_float(arg->type);
    if (arg->kind == AST_UNARY && (arg->op == '-' || arg->op == '+') &&
        arg->a != NULL)
        return ast_value_is_float_word(arg->a);
    if (arg->kind == AST_UNARY && arg->op == '*')
        return ast_deref_float_read(arg);
    if (arg->kind == AST_IDENT) {
        s = find_sym(arg->sval);
        return s != NULL && s->storage != SC_FUNC && !s->is_array &&
               type_is_float(s->type);
    }
    if (arg->kind == AST_CALL && arg->a != NULL && arg->a->kind == AST_IDENT) {
        s = find_global(arg->a->sval);
        if (s != NULL && s->storage == SC_FUNC)
            return type_is_float(s->type);
    }
    if (arg->kind == AST_CALL && ast_call_indirect_supported(arg)) {
        int callee_type;
        int no_deref;
        if (ast_pointer_expr_type(arg->a, &callee_type, &no_deref))
            return type_is_float(type_decay_ptr(callee_type));
    }
    if (arg->kind == AST_CALL && ast_call_star_indirect_supported(arg)) {
        s = ast_indirect_call_proto_sym(arg);
        return s != NULL && type_is_float(type_decay_ptr(s->type));
    }
    if (arg->kind == AST_CAST && type_is_float(arg->type))
        return ast_gen_supported(arg);
    if (arg->kind == AST_ASSIGN && arg->a != NULL) {
        int lhs_type;
        if (arg->a->kind == AST_IDENT) {
            s = find_sym(arg->a->sval);
            return s != NULL && !s->is_const_value && s->storage != SC_FUNC &&
                   !s->is_array && type_is_float(s->type);
        }
        if (arg->a->kind == AST_UNARY && arg->a->op == '*' &&
            ast_deref_lvalue_type(arg->a, &lhs_type))
            return type_is_float(lhs_type);
        if (arg->a->kind == AST_INDEX &&
            ast_index_lvalue_elem_type(arg->a, &lhs_type))
            return type_is_float(lhs_type);
        if (arg->a->kind == AST_MEMBER && ast_member_lvalue_type(arg->a, &lhs_type))
            return type_is_float(lhs_type);
    }
    if (arg->kind == AST_COND)
        return ast_cond_result_is_float(arg);
    if (arg->kind == AST_BINARY && is_float_arith_op(arg->op)) {
        int lhs_float = ast_value_is_float_word(arg->a);
        int rhs_float = ast_value_is_float_word(arg->b);
        if (!lhs_float && !rhs_float)
            return 0;
        return (lhs_float || ast_value_is_plain_int(arg->a) ||
                ast_value_is_long_word(arg->a)) &&
               (rhs_float || ast_value_is_plain_int(arg->b) ||
                ast_value_is_long_word(arg->b));
    }
    if (arg->kind == AST_INDEX)
        return ast_index_float_read(arg);
    if (arg->kind == AST_MEMBER)
        return ast_member_float_read(arg);
    return 0;
}

int ast_value_is_pointer_word(const struct AstNode *n)
{
    struct Sym *s;
    int ptr_type;
    int no_deref;
    if (n == NULL)
        return 0;
    switch (n->kind) {
    case AST_STR_LIT:
        return 1;
    case AST_UNARY:
        if (n->op == '&')
            return ast_address_of_supported(n->a);
        if (n->op == '*')
            return ast_deref_pointer_word_read(n);
        return 0;
    case AST_IDENT:
        s = find_sym(n->sval);
         return s != NULL && !s->is_const_value &&
             (s->storage == SC_FUNC || s->is_array || type_ptr_depth(s->type) > 0);
    case AST_CALL:
        if (n->a == NULL || n->a->kind != AST_IDENT)
            return 0;
        s = find_global(n->a->sval);
        return s != NULL && type_ptr_depth(s->type) > 0;
    case AST_MEMBER:
        return ast_member_pointer_read(n);
    case AST_INDEX:
        return ast_pointer_expr_type(n, &ptr_type, &no_deref);
    default:
        return 0;
    }
}

int ast_pointer_assign_rhs_supported(const struct AstNode *n)
{
    const struct AstNode *value;
    int ptr_type;
    int no_deref;
    int va_type;
    if (n == NULL)
        return 0;
    value = (n->kind == AST_CAST) ? n->a : n;
    if (ast_null_pointer_const(value))
        return 1;
    if (ast_va_arg_deref_type(value, &va_type))
        return type_ptr_depth(va_type) > 0;
    if (n->kind == AST_CAST && ast_gen_supported(value) &&
        ast_value_is_plain_int(value))
        return 1;
    if (ast_pointer_expr_type(value, &ptr_type, &no_deref))
        return 1;
    if (value != NULL && value->kind == AST_CALL &&
        ast_value_is_pointer_word(value) && ast_call_named_args_supported(value))
        return 1;                      /* pointer-returning direct call */
    return ast_gen_supported(value) && ast_value_is_pointer_word(value);
}

/* A unary chain (-, +, ~) bottoming out in a single non-long INT_LIT folds to
 * a 16-bit immediate.  Returns 1 and stores the folded value when foldable;
 * 0 otherwise. */
int ast_unary_int_const_fold(const struct AstNode *n, long *out)
{
    long v;
    if (n == NULL)
        return 0;
    if (n->kind == AST_INT_LIT) {
        if (type_is_long(n->type))
            return 0;
        *out = n->ival;
        return 1;
    }
    if (n->kind == AST_UNARY && !type_is_long(n->type) &&
        (n->op == '-' || n->op == '+' || n->op == '~') &&
        ast_unary_int_const_fold(n->a, &v)) {
        *out = (n->op == '-') ? -v : (n->op == '~') ? ~v : v;
        return 1;
    }
    return 0;
}

int ast_int_const_cast_fold(const struct AstNode *n, long *out)
{
    long v;

    if (ast_unary_int_const_fold(n, out))
        return 1;
    if (n == NULL || n->kind != AST_CAST || n->a == NULL)
        return 0;
    if (!ast_is_plain_int_type(n->type) || type_size(n->type) > 2)
        return 0;
    if (!ast_int_const_cast_fold(n->a, &v))
        return 0;
    *out = v;
    return 1;
}

int ast_unary_long_const_fold(const struct AstNode *n, long *out)
{
    long v;
    if (n == NULL)
        return 0;
    if (n->kind == AST_INT_LIT) {
        if (!type_is_long(n->type))
            return 0;
        *out = n->ival;
        return 1;
    }
    if (n->kind == AST_UNARY &&
        (type_is_long(n->type) || (n->a != NULL && type_is_long(n->a->type))) &&
        (n->op == '-' || n->op == '+' || n->op == '~') &&
        ast_unary_long_const_fold(n->a, &v)) {
        *out = (n->op == '-') ? -v : (n->op == '~') ? ~v : v;
        return 1;
    }
    return 0;
}

/* A unary +/- chain bottoming out in a float literal or a folded (`const
 * float x = <literal>;`) local folds to a single immediate with the sign bit
 * flipped at compile time, e.g. `-PI` from `const float PI = 3.14159265f;`.
 * Mirrors ast_unary_int_const_fold/ast_unary_long_const_fold above, but for
 * float: without this, gen_unary_ast emitted the constant's bit pattern via
 * the normal float load and then a runtime `ld a,d / xor 80h / ld d,a` to
 * flip its sign on every execution, even though the negated value is just as
 * knowable at compile time as the original. */
int ast_unary_float_const_fold(const struct AstNode *n, unsigned long *out)
{
    unsigned long v;
    struct Sym *s;

    if (n == NULL)
        return 0;
    if (n->kind == AST_FLOAT_LIT) {
        *out = n->uval;
        return 1;
    }
    if (n->kind == AST_IDENT) {
        s = find_sym(n->sval);
        if (s != NULL && s->is_const_value && type_is_float(s->type)) {
            *out = (unsigned long)s->const_value;
            return 1;
        }
        return 0;
    }
    if (n->kind == AST_UNARY && (n->op == '-' || n->op == '+') &&
        ast_unary_float_const_fold(n->a, &v)) {
        *out = (n->op == '-') ? (v ^ 0x80000000UL) : v;
        return 1;
    }
    return 0;
}

/* Fold one integer binary operator with TARGET semantics (16-bit int,
 * 32-bit long) so a compile-time fold produces exactly what the same
 * expression computes at run time.  Applies the usual arithmetic
 * conversions - both operands are cast to the operation's common type
 * before the operator, and the arithmetic/bitwise result is wrapped back to
 * that common type's width - and evaluates through unsigned host arithmetic
 * so a target-defined wrap (e.g. `65535u + 1u == 0`, or `-1 == 65535u`)
 * neither trips host signed-overflow UB nor depends on host long width
 * (64-bit LP64 vs 32-bit LLP64).  Shifts follow their own C rule instead:
 * each operand is promoted independently and the result type / signedness
 * come from the promoted LEFT operand, never a two-operand common type.
 *
 * type_a / type_b are the operands' own (pre-promotion) source types.
 * Returns 1 with *out set (as a sign/zero-extended host long matching the
 * result type), or 0 to decline the fold (divide/modulo by zero, signed
 * minimum divided/modulo -1, or an out-of-range shift count). */
static int ast_fold_binary_target(int op, int type_a, int type_b,
                                  long a, long b, long *out)
{
    int common;
    int unsigned_op;
    unsigned long ua, ub;
    unsigned long width_mask;

    if (op == TOK_SHL || op == TOK_SHR) {
        int lt = promote_int_type(type_a);
        int lbits;
        if (type_is_float(lt))
            return 0;
        lbits = type_is_long(lt) ? 32 : 16;
        if (b < 0 || b >= lbits)
            return 0;
        a = ast_const_apply_int_cast(a, lt);
        if (op == TOK_SHL) {
            *out = ast_const_apply_int_cast(
                (long)((unsigned long)a << (unsigned int)b), lt);
        } else if (lt & TYPE_UNSIGNED) {
            unsigned long m = type_is_long(lt) ? 0xffffffffUL : 0xffffUL;
            *out = ast_const_apply_int_cast(
                (long)(((unsigned long)a & m) >> (unsigned int)b), lt);
        } else {
            *out = ast_const_apply_int_cast(
                (long)((unsigned long)(a >> (unsigned int)b)), lt);
        }
        return 1;
    }

    common = common_arith_type(type_a, type_b);
    if (type_is_float(common))
        return 0;
    unsigned_op = (common & TYPE_UNSIGNED) != 0;
    width_mask = type_is_long(common) ? 0xffffffffUL : 0xffffUL;

    a = ast_const_apply_int_cast(a, common);
    b = ast_const_apply_int_cast(b, common);
    ua = (unsigned long)a & width_mask;
    ub = (unsigned long)b & width_mask;

    switch (op) {
    case '+': *out = ast_const_apply_int_cast((long)(ua + ub), common); return 1;
    case '-': *out = ast_const_apply_int_cast((long)(ua - ub), common); return 1;
    case '*': *out = ast_const_apply_int_cast((long)(ua * ub), common); return 1;
    case '&': *out = ast_const_apply_int_cast((long)(ua & ub), common); return 1;
    case '|': *out = ast_const_apply_int_cast((long)(ua | ub), common); return 1;
    case '^': *out = ast_const_apply_int_cast((long)(ua ^ ub), common); return 1;
    case '/':
        if (b == 0) return 0;
        if (!unsigned_op && b == -1 &&
            a == (type_is_long(common) ? (-2147483647L - 1L) : -32768L))
            return 0;
        *out = unsigned_op ? ast_const_apply_int_cast((long)(ua / ub), common)
                           : ast_const_apply_int_cast((long)(a / b), common);
        return 1;
    case '%':
        if (b == 0) return 0;
        if (!unsigned_op && b == -1 &&
            a == (type_is_long(common) ? (-2147483647L - 1L) : -32768L))
            return 0;
        *out = unsigned_op ? ast_const_apply_int_cast((long)(ua % ub), common)
                           : ast_const_apply_int_cast((long)(a % b), common);
        return 1;
    case '<':    *out = unsigned_op ? (ua <  ub) : (a <  b); return 1;
    case '>':    *out = unsigned_op ? (ua >  ub) : (a >  b); return 1;
    case TOK_LE: *out = unsigned_op ? (ua <= ub) : (a <= b); return 1;
    case TOK_GE: *out = unsigned_op ? (ua >= ub) : (a >= b); return 1;
    case TOK_EQ: *out = (ua == ub); return 1;
    case TOK_NE: *out = (ua != ub); return 1;
    default: return 0;
    }
}

int ast_const_scalar_fold(const struct AstNode *n, long *out)
{
    long a;
    long b;
    struct Sym *s;
    int ei;

    if (n == NULL)
        return 0;
    switch (n->kind) {
    case AST_INT_LIT:
        *out = n->ival;
        return 1;
    case AST_IDENT:
        for (ei = 0; ei < nenum_consts; ++ei) {
            if (!strcmp(enum_const_names[ei], n->sval)) {
                *out = (long)(int)enum_const_values[ei];
                return 1;
            }
        }
        s = find_sym(n->sval);
        if (s != NULL && s->is_const_value && !type_is_float(s->type)) {
            *out = (long)s->const_value;
            return 1;
        }
        return 0;
    case AST_UNARY:
        if (!ast_const_scalar_fold(n->a, &a))
            return 0;
        switch (n->op) {
        case '+': *out = a; return 1;
        case '-': *out = -a; return 1;
        case '~': *out = ~a; return 1;
        case '!': *out = !a; return 1;
        default: return 0;
        }
    case AST_CAST:
        if (!ast_const_scalar_fold(n->a, &a))
            return 0;
        if (type_is_float(n->type) || type_ptr_depth(n->type) > 0)
            return 0;
        *out = ast_const_apply_int_cast(a, n->type);
        return 1;
    case AST_BINARY:
    case AST_LOGAND:
    case AST_LOGOR:
        if (!ast_const_scalar_fold(n->a, &a) || !ast_const_scalar_fold(n->b, &b))
            return 0;
        if (n->kind == AST_LOGAND) { *out = (a != 0) && (b != 0); return 1; }
        if (n->kind == AST_LOGOR)  { *out = (a != 0) || (b != 0); return 1; }
        /* Evaluate the operator with target-width semantics (see
         * ast_fold_binary_target): operands converted to the operation's
         * common type, wrapping arithmetic done at the target width through
         * unsigned host math, so the fold matches the target's runtime
         * result on every host and never leaks host long width or signed
         * overflow into the folded constant. */
        return ast_fold_binary_target(n->op,
                                      ast_expr_type_for_sizeof(n->a),
                                      ast_expr_type_for_sizeof(n->b),
                                      a, b, out);
    default:
        return 0;
    }
}

long ast_const_apply_int_cast(long v, int type)
{
    unsigned long u;

    if (type_is_float(type) || type_ptr_depth(type) > 0)
        return v;
    if (type_size(type) <= 1) {
        u = ((unsigned long)v) & 0xffUL;
        if (!(type & TYPE_UNSIGNED) && (u & 0x80UL))
            return (long)(u | ~0xffUL);
        return (long)u;
    }
    if (type_size(type) <= 2) {
        u = ((unsigned long)v) & 0xffffUL;
        if (!(type & TYPE_UNSIGNED) && (u & 0x8000UL))
            return (long)(u | ~0xffffUL);
        return (long)u;
    }
    u = ((unsigned long)v) & 0xffffffffUL;
    if (!(type & TYPE_UNSIGNED) && (u & 0x80000000UL))
        return (long)(u | ~0xffffffffUL);
    return (long)u;
}

int ast_const_condition_fold(const struct AstNode *n, long *out)
{
    return ast_const_scalar_fold(n, out);
}

/*
 * Strict constant fold. Like ast_const_scalar_fold, it evaluates an integer
 * constant expression with exact target semantics (both walkers now share
 * ast_fold_binary_target, so a folded value equals what the target computes
 * at run time regardless of host long width or operand sign). The remaining
 * difference is caller intent: ast_const_fold_strict is the entry point used
 * where the whole node is about to be replaced by an emitted immediate
 * (gen_binary_ast / gen_long_arith_ast), and it declines (returns 0) the two
 * cases with no defined target value - divide/modulo by zero, signed minimum
 * divided/modulo -1, and an out-of-range shift count - so the caller falls
 * back to ordinary codegen rather than baking in a bogus constant. Callers
 * may emit the folded immediate (masked to the result width) directly
 * whenever it returns 1.
 */
int ast_const_fold_strict(const struct AstNode *n, long *out)
{
    long a;
    long b;

    if (n == NULL)
        return 0;
    switch (n->kind) {
    case AST_INT_LIT:
        *out = n->ival;
        return 1;
    case AST_IDENT:
        return ast_const_scalar_fold(n, out);   /* enum / const value: a leaf */
    case AST_UNARY:
        if (!ast_const_fold_strict(n->a, &a))
            return 0;
        switch (n->op) {
        case '+': *out = a; return 1;
        case '-': *out = -a; return 1;
        case '~': *out = ~a; return 1;
        case '!': *out = !a; return 1;
        default: return 0;
        }
    case AST_CAST:
        if (type_is_float(n->type) || type_ptr_depth(n->type) > 0)
            return 0;
        if (!ast_const_fold_strict(n->a, &a))
            return 0;
        *out = ast_const_apply_int_cast(a, n->type);
        return 1;
    case AST_LOGAND:
    case AST_LOGOR:
        if (!ast_const_fold_strict(n->a, &a) || !ast_const_fold_strict(n->b, &b))
            return 0;
        *out = (n->kind == AST_LOGAND) ? ((a != 0) && (b != 0))
                                       : ((a != 0) || (b != 0));
        return 1;
    case AST_BINARY:
        if (!ast_const_fold_strict(n->a, &a) || !ast_const_fold_strict(n->b, &b))
            return 0;
        /* Target-width fold via the shared helper: operands converted to the
         * common type, wrapping arithmetic at the target width through
         * unsigned host math, shifts typed from the promoted left operand.
         * Declines only for operations with no defined target value; every
         * other integer binary operator folds to its exact target value, so
         * the emitted immediate matches the target's runtime result
         * regardless of host long width or operand sign. */
        return ast_fold_binary_target(n->op,
                                      ast_expr_type_for_sizeof(n->a),
                                      ast_expr_type_for_sizeof(n->b),
                                      a, b, out);
    default:
        return 0;
    }
}

int ast_global_byte_array_const_store(const struct AstNode *n,
                                             struct Sym **out_arr,
                                             long *out_idx,
                                             long *out_rhs)
{
    struct Sym *arr;
    long rhs;

    if (n == NULL || n->kind != AST_ASSIGN || n->op != '=' || !expr_result_dead)
        return 0;
    if (n->a == NULL || n->a->kind != AST_INDEX || n->a->a == NULL ||
        n->a->a->kind != AST_IDENT || n->a->b == NULL ||
        n->a->b->kind != AST_INT_LIT)
        return 0;
    arr = find_sym(n->a->a->sval);
    if (arr == NULL || arr->storage != SC_GLOBAL || !arr->is_array ||
        type_size(arr->type) != 1)
        return 0;
    if (!ast_int_const_cast_fold(n->b, &rhs))
        return 0;
    if (rhs < 0 || rhs > 255)
        return 0;
    if (out_arr != NULL)
        *out_arr = arr;
    if (out_idx != NULL)
        *out_idx = n->a->b->ival;
    if (out_rhs != NULL)
        *out_rhs = rhs;
    return 1;
}

int ast_global_byte_array_fast_store(const struct AstNode *n,
                                            struct Sym **out_arr,
                                            struct Sym **out_idx_sym,
                                            long *out_idx_const,
                                            int *out_idx_has_const,
                                            struct Sym **out_rhs_sym,
                                            long *out_rhs_const,
                                            int *out_rhs_kind)
{
    struct Sym *arr;
    struct Sym *idx_sym;
    struct Sym *rhs_sym;
    long rhs_const;
    long idx_const;
    int idx_has_const;
    int rhs_kind;

    if (n == NULL || n->kind != AST_ASSIGN || n->op != '=' || !expr_result_dead)
        return 0;
    if (n->a == NULL || n->a->kind != AST_INDEX || n->a->a == NULL ||
        n->a->a->kind != AST_IDENT || n->a->b == NULL)
        return 0;
    arr = find_sym(n->a->a->sval);
    if (arr == NULL || arr->storage != SC_GLOBAL || !arr->is_array ||
        type_size(arr->type) != 1)
        return 0;

    idx_sym = NULL;
    idx_const = 0;
    idx_has_const = 0;
    if (n->a->b->kind == AST_INT_LIT) {
        idx_const = n->a->b->ival;
        idx_has_const = 1;
    } else if (n->a->b->kind == AST_IDENT) {
        idx_sym = find_sym(n->a->b->sval);
        if (!sym_can_ix_direct(idx_sym))
            return 0;
        if (type_size(idx_sym->type) != 1 &&
            !(type_size(idx_sym->type) == 2 && ast_is_plain_int_type(idx_sym->type)))
            return 0;
    } else {
        return 0;
    }

    rhs_sym = NULL;
    rhs_const = 0;
    rhs_kind = 0;
    if (n->b != NULL && n->b->kind == AST_IDENT) {
        rhs_sym = find_sym(n->b->sval);
        if (!sym_can_ix_direct(rhs_sym) || type_size(rhs_sym->type) != 1)
            return 0;
        rhs_kind = 1;
    } else if (n->b != NULL && n->b->kind == AST_INT_LIT) {
        rhs_const = n->b->ival;
        if (rhs_const < 0 || rhs_const > 255)
            return 0;
        rhs_kind = 2;
    } else {
        return 0;
    }

    if (out_arr != NULL)
        *out_arr = arr;
    if (out_idx_sym != NULL)
        *out_idx_sym = idx_sym;
    if (out_idx_const != NULL)
        *out_idx_const = idx_const;
    if (out_idx_has_const != NULL)
        *out_idx_has_const = idx_has_const;
    if (out_rhs_sym != NULL)
        *out_rhs_sym = rhs_sym;
    if (out_rhs_const != NULL)
        *out_rhs_const = rhs_const;
    if (out_rhs_kind != NULL)
        *out_rhs_kind = rhs_kind;
    return 1;
}

/* Detects the narrow "cyclic byte fill" loop idiom:
 *
 *     for (ivar = INIT; ivar < BOUND; ivar++)
 *         ARR[ivar] = BASE + (ivar % MOD);        (or the commuted order)
 *
 * with ARR a global byte array and INIT/BASE/MOD compile-time int literals
 * (INIT >= 0, BASE in 0..255, MOD in 1..255, BASE+MOD <= 256 so the rolling
 * byte counter never needs to wrap outside that range). Pure analysis of the
 * already-built for-header/body shape - no allocation, no emission - so it is
 * safe to call from both ast_build_for_stmt (to decide whether to reserve the
 * rolling-counter's frame slot) and ast_gen_for_stmt (to re-extract the same
 * constants for the specialised emission). Declining (0) is always safe: the
 * caller falls back to the ordinary loop path.
 *
 * This does not disturb ivar's own storage/increment at all - the loop's
 * normal test/increment logic keeps running unchanged. Only the body's
 * BASE+(ivar%MOD) computation is replaced, by a byte counter that starts at
 * BASE+(INIT%MOD) and simply increments (wrapping at BASE+MOD back to BASE)
 * once per iteration, which stays equal to BASE+(ivar%MOD) by induction
 * however ivar's real storage is used elsewhere. */
int ast_for_mod_fill_supported(const struct AstNode *n, struct Sym **out_arr,
                                      long *out_init, long *out_base,
                                      long *out_mod, const char **out_ivar_name)
{
    const struct AstNode *body_assign;
    const struct AstNode *lhs;
    const struct AstNode *rhs;
    const struct AstNode *mod_expr;
    const struct AstNode *base_lit;
    struct Sym *arr;
    const char *ivar_name;
    long init_val, base_val, mod_val;

    if (n->a == NULL || n->a->kind != AST_ASSIGN || n->a->op != '=')
        return 0;
    if (n->a->a == NULL || n->a->a->kind != AST_IDENT)
        return 0;
    if (n->a->b == NULL || n->a->b->kind != AST_INT_LIT)
        return 0;
    ivar_name = n->a->a->sval;
    init_val = n->a->b->ival;
    if (init_val < 0)
        return 0;

    /* The bound itself is never inspected below - the loop's own condition
     * codegen (ast_gen_cond_branch) still owns it untouched, so any bound
     * expression is fine so long as ivar is compared with '<'. */
    if (n->b == NULL || n->b->kind != AST_BINARY || n->b->op != '<')
        return 0;
    if (n->b->a == NULL || n->b->a->kind != AST_IDENT ||
        strcmp(n->b->a->sval, ivar_name) != 0)
        return 0;

    if (n->c == NULL)
        return 0;
    if (!((n->c->kind == AST_UNARY || n->c->kind == AST_POSTFIX) &&
          n->c->op == TOK_INC))
        return 0;
    if (n->c->a == NULL || n->c->a->kind != AST_IDENT ||
        strcmp(n->c->a->sval, ivar_name) != 0)
        return 0;

    if (n->d == NULL || n->d->kind != AST_EXPR_STMT || n->d->a == NULL)
        return 0;
    body_assign = n->d->a;
    if (body_assign->kind != AST_ASSIGN || body_assign->op != '=')
        return 0;

    lhs = body_assign->a;
    rhs = body_assign->b;
    if (lhs == NULL || lhs->kind != AST_INDEX || lhs->a == NULL ||
        lhs->a->kind != AST_IDENT || lhs->b == NULL ||
        lhs->b->kind != AST_IDENT || strcmp(lhs->b->sval, ivar_name) != 0)
        return 0;
    arr = find_global(lhs->a->sval);
    if (arr == NULL || !arr->is_array || arr->storage != SC_GLOBAL ||
        type_size(arr->type) != 1)
        return 0;

    if (rhs == NULL || rhs->kind != AST_BINARY || rhs->op != '+')
        return 0;
    if (rhs->a != NULL && rhs->a->kind == AST_INT_LIT &&
        rhs->b != NULL && rhs->b->kind == AST_BINARY) {
        base_lit = rhs->a;
        mod_expr = rhs->b;
    } else if (rhs->b != NULL && rhs->b->kind == AST_INT_LIT &&
               rhs->a != NULL && rhs->a->kind == AST_BINARY) {
        base_lit = rhs->b;
        mod_expr = rhs->a;
    } else {
        return 0;
    }
    if (mod_expr->op != '%')
        return 0;
    if (mod_expr->a == NULL || mod_expr->a->kind != AST_IDENT ||
        strcmp(mod_expr->a->sval, ivar_name) != 0)
        return 0;
    if (mod_expr->b == NULL || mod_expr->b->kind != AST_INT_LIT)
        return 0;

    base_val = base_lit->ival;
    mod_val = mod_expr->b->ival;
    if (base_val < 0 || base_val > 255)
        return 0;
    if (mod_val < 1 || mod_val > 255)
        return 0;
    if (base_val + mod_val > 256)
        return 0;

    if (out_arr != NULL) *out_arr = arr;
    if (out_init != NULL) *out_init = init_val;
    if (out_base != NULL) *out_base = base_val;
    if (out_mod != NULL) *out_mod = mod_val;
    if (out_ivar_name != NULL) *out_ivar_name = ivar_name;
    return 1;
}

/* Does `n`'s subtree contain an AST_IDENT node named exactly `name`
 * anywhere - however deeply nested (array indices, member accesses,
 * casts, call arguments, ...)? Used to prove a would-be-hoisted expression
 * truly does not depend on a given loop's induction variable. Unrecognised
 * node shapes are not special-cased: every child field and list entry is
 * always walked, so declining to recurse into something is never a way
 * this can silently miss a reference. */
int ast_expr_references_ident(const struct AstNode *n, const char *name)
{
    int i;

    if (n == NULL)
        return 0;
    if (n->kind == AST_IDENT && n->sval != NULL && !strcmp(n->sval, name))
        return 1;
    if (ast_expr_references_ident(n->a, name)) return 1;
    if (ast_expr_references_ident(n->b, name)) return 1;
    if (ast_expr_references_ident(n->c, name)) return 1;
    if (ast_expr_references_ident(n->d, name)) return 1;
    for (i = 0; i < n->list_len; ++i)
        if (ast_expr_references_ident(n->list[i], name))
            return 1;
    return 0;
}

/* Does `n`'s subtree contain anything with an observable side effect -
 * any assignment (plain or compound), ++/--, or a function call? Used to
 * prove an expression can safely be evaluated once and reused across loop
 * iterations instead of once per iteration (for a would-be-hoisted lvalue
 * address), or that skipping a would-be extra evaluation changes nothing
 * observable (for a statement's rhs, which must have none of these for its
 * absence from a hoisted address computation to be irrelevant). Same
 * always-recurse convention as ast_expr_references_ident: an unrecognised
 * shape is scanned, not assumed innocent. */
int ast_expr_has_side_effects(const struct AstNode *n)
{
    int i;

    if (n == NULL)
        return 0;
    if (n->kind == AST_ASSIGN || n->kind == AST_CALL)
        return 1;
    if ((n->kind == AST_UNARY || n->kind == AST_POSTFIX) &&
        (n->op == TOK_INC || n->op == TOK_DEC))
        return 1;
    if (ast_expr_has_side_effects(n->a)) return 1;
    if (ast_expr_has_side_effects(n->b)) return 1;
    if (ast_expr_has_side_effects(n->c)) return 1;
    if (ast_expr_has_side_effects(n->d)) return 1;
    for (i = 0; i < n->list_len; ++i)
        if (ast_expr_has_side_effects(n->list[i]))
            return 1;
    return 0;
}

/* Finds a '%' or '/' AST_BINARY node reachable UNCONDITIONALLY from `n` -
 * i.e. not nested under a ?: / && / ||, any of which can skip evaluating
 * one side - with both operands bare plain-int (not char/bool - the exact
 * 16-bit width DCCRTL.MAC's __udivmod/__sdivmod expect, sidestepping any
 * question of whether a narrower type's promotion is already reflected in
 * a bare identifier read) identifiers of the same signedness. Returns the
 * first match found via a left-to-right, depth-first walk, or NULL.
 * Declines (NULL) on anything not specifically recognized - v1 has no
 * general expression-equality checker, only this exact bare-identifier
 * shape (matching tests/e.c and the bignum-style tests in this suite that
 * motivated it - see ast_divmod_fuse_compound below). */
static const struct AstNode *ast_find_unconditional_divmod_op(const struct AstNode *n, int op)
{
    const struct AstNode *found;

    if (n == NULL)
        return NULL;
    if (n->kind == AST_COND || n->kind == AST_LOGAND || n->kind == AST_LOGOR)
        return NULL;
    if (n->kind == AST_BINARY && n->op == op &&
        n->a != NULL && n->a->kind == AST_IDENT && n->a->sval != NULL &&
        n->b != NULL && n->b->kind == AST_IDENT && n->b->sval != NULL) {
        /* AST_IDENT nodes carry no reliable ->type of their own until
         * codegen actually evaluates them (gen_ident resolves the symbol
         * and sets g_expr_type at that point, not stored back onto the
         * node) - ast_expr_type_for_sizeof is the existing static
         * inference path built for exactly this "need a node's type
         * before/without running its codegen" situation. */
        /* Promote each operand's type before comparing, the same as C's own
         * usual-arithmetic-conversions would before evaluating % or / - a
         * char/bool-narrowed identifier (e.g. a register-allocated loop
         * counter narrowed by try_narrow_for_counter) always promotes to
         * plain signed int regardless of its own narrowed storage's
         * unsigned-ness, exactly like a real `char` operand would. Checking
         * the raw unpromoted types here would wrongly reject e.g. `int x`
         * paired with a narrowed-to-unsigned-char `int n` as a signedness
         * mismatch, when C itself treats that pairing as signed-int/signed-
         * int throughout. */
        int a_type = promote_int_type(ast_expr_type_for_sizeof(n->a));
        int b_type = promote_int_type(ast_expr_type_for_sizeof(n->b));
        if ((a_type & 15) == TYPE_INT && !(a_type & (TYPE_PTR | TYPE_PTR2 | TYPE_STRUCT)) &&
            (b_type & 15) == TYPE_INT && !(b_type & (TYPE_PTR | TYPE_PTR2 | TYPE_STRUCT)) &&
            (a_type & TYPE_UNSIGNED) == (b_type & TYPE_UNSIGNED))
            return n;
    }
    if ((found = ast_find_unconditional_divmod_op(n->a, op)) != NULL) return found;
    if ((found = ast_find_unconditional_divmod_op(n->b, op)) != NULL) return found;
    if ((found = ast_find_unconditional_divmod_op(n->c, op)) != NULL) return found;
    if ((found = ast_find_unconditional_divmod_op(n->d, op)) != NULL) return found;
    return NULL;
}

/* Returns a copy of `tree` with every occurrence of `target` (found by
 * pointer identity) replaced by `replacement`, sharing every subtree that
 * did not itself need to change (the same copy-on-write-while-propagating-
 * up shape as ast_hoist_row_invariant_2d_reads above) - or `tree` itself,
 * completely unchanged, if `target` is not reachable from it at all (the
 * common case when called against the ONE of the two fused statements
 * that does not contain a given target - a safe no-op, not an error). */
static struct AstNode *ast_replace_subtree(const struct AstNode *tree,
                                                  const struct AstNode *target,
                                                  struct AstNode *replacement)
{
    struct AstNode *na, *nb, *nc, *nd;
    struct AstNode *copy;

    if (tree == NULL)
        return NULL;
    if (tree == target)
        return replacement;

    na = ast_replace_subtree(tree->a, target, replacement);
    nb = ast_replace_subtree(tree->b, target, replacement);
    nc = ast_replace_subtree(tree->c, target, replacement);
    nd = ast_replace_subtree(tree->d, target, replacement);
    if (na == tree->a && nb == tree->b && nc == tree->c && nd == tree->d)
        return (struct AstNode *)tree;

    copy = ast_new(&g_ast_arena, tree->kind);
    *copy = *tree;
    copy->a = na;
    copy->b = nb;
    copy->c = nc;
    copy->d = nd;
    return copy;
}

/* Detects two ADJACENT statements in a compound block's own list - one
 * containing `X % Y`, the other `X / Y` (either order), both reachable
 * unconditionally and both bare-identifier operands of identical name and
 * signedness - and rewrites them to share one DCCRTL.MAC __udivmod/
 * __sdivmod call instead of each separately calling __modu/__divu (or
 * __mods/__divs), which independently do the same division: DCCRTL.MAC
 * already has a *runtime* cache for exactly this pattern (see __modu's own
 * comment), but the cache-check itself costs real instructions on every
 * call whether it hits or misses, and profiling tests/e.c (via dccprof,
 * after fixing a real attribution bug it exposed) found that program's
 * division family - __udivmod's now-exposed core plus __divu's/__modu's
 * own cache-check overhead - at very roughly 60% of its ENTIRE runtime.
 *
 * v1 is deliberately narrow, matching this file's usual discipline:
 *   - both statements must be exactly `IDENT_OR_LVALUE = RHS;` (a plain
 *     AST_EXPR_STMT wrapping a plain '=' AST_ASSIGN) - nothing else yet;
 *   - the matched operands must be bare identifiers, not general matching
 *     subtrees - no expression-equality checker exists yet;
 *   - the FIRST of the two statements (in program order - whichever list
 *     index is lower, regardless of which one holds the % vs the /) must
 *     not modify X or Y anywhere, including as its own assignment target:
 *     the fused call captures both values before EITHER statement runs, so
 *     if the first statement could change one of them before the second
 *     statement's own original operator would have read it, the fused
 *     value would be stale. The SECOND statement may freely reassign X or
 *     Y as its own top-level assignment target (exactly tests/e.c's own
 *     `x = 10*a[n-1] + x/n;`) - C's own "evaluate the whole rhs before the
 *     assignment takes effect" rule already guarantees that read sees the
 *     pre-assignment value, identical to what the fused call captured -
 *     but not through any other, less obvious side effect;
 *   - neither statement's lvalue address computation or rhs may have any
 *     OTHER side effect at all (ast_expr_has_side_effects, unmodified - a
 *     matched node's own two bare-identifier operands can never trip it).
 *
 * Returns a rewritten copy of `n` (only ever the first qualifying pair
 * found, scanning adjacent statements in order) to use in its place, or
 * NULL if nothing qualifies (use `n` unchanged) - n itself, and the two
 * original statement nodes, are never mutated, matching every other hoist
 * in this file.
 */
struct AstNode *ast_divmod_fuse_compound(const struct AstNode *n)
{
    int i, j;

    if (n == NULL || n->kind != AST_COMPOUND)
        return NULL;

    for (i = 0; i + 1 < n->list_len; ++i) {
        const struct AstNode *s1 = n->list[i];
        const struct AstNode *s2 = n->list[i + 1];
        const struct AstNode *mod_node;
        const struct AstNode *div_node;
        const char *x_name;
        const char *y_name;
        struct Sym *x_sym;
        struct Sym *y_sym;
        int is_signed;
        struct Sym *quot_sym;
        struct Sym *rem_sym;
        struct AstNode *call_node;
        struct AstNode *quot_ident;
        struct AstNode *rem_ident;
        struct AstNode *new_s1_rhs;
        struct AstNode *new_s2_rhs;
        struct AstNode *new_s1_assign;
        struct AstNode *new_s2_assign;
        struct AstNode *new_s1_stmt;
        struct AstNode *new_s2_stmt;
        struct AstNode *compound;
        char qname[24];
        char rname[24];

        if (s1 == NULL || s1->kind != AST_EXPR_STMT || s1->a == NULL ||
            s1->a->kind != AST_ASSIGN || s1->a->op != '=' ||
            s1->a->a == NULL || s1->a->b == NULL)
            continue;
        if (s2 == NULL || s2->kind != AST_EXPR_STMT || s2->a == NULL ||
            s2->a->kind != AST_ASSIGN || s2->a->op != '=' ||
            s2->a->a == NULL || s2->a->b == NULL)
            continue;

        mod_node = ast_find_unconditional_divmod_op(s1->a->b, '%');
        div_node = ast_find_unconditional_divmod_op(s2->a->b, '/');
        if (mod_node == NULL || div_node == NULL) {
            mod_node = ast_find_unconditional_divmod_op(s2->a->b, '%');
            div_node = ast_find_unconditional_divmod_op(s1->a->b, '/');
            if (mod_node == NULL || div_node == NULL)
                continue;
        }

        if (strcmp(mod_node->a->sval, div_node->a->sval) != 0 ||
            strcmp(mod_node->b->sval, div_node->b->sval) != 0)
            continue;

        x_name = mod_node->a->sval;
        y_name = mod_node->b->sval;
        x_sym = find_sym(x_name);
        y_sym = find_sym(y_name);
        is_signed = !(promote_int_type(ast_expr_type_for_sizeof(mod_node->a)) & TYPE_UNSIGNED);

        /* Capturing both operands before s1 is only safe when s1 cannot
         * modify either through an alias. The direct-target check below
         * catches `x = x % n`; address-taken information catches indirect
         * forms such as `*p = x % n` when p points at x. */
        if (x_sym == NULL || y_sym == NULL ||
            ((x_sym->storage == SC_GLOBAL || x_sym->storage == SC_EXTERN) ?
                global_text_addr_taken_count(x_name) != 0 :
                local_name_address_taken_in_function(x_name) != 0) ||
            ((y_sym->storage == SC_GLOBAL || y_sym->storage == SC_EXTERN) ?
                global_text_addr_taken_count(y_name) != 0 :
                local_name_address_taken_in_function(y_name) != 0))
            continue;

        if (ast_expr_has_side_effects(s1->a->a) || ast_expr_has_side_effects(s1->a->b))
            continue;
        if (s1->a->a->kind == AST_IDENT && s1->a->a->sval != NULL &&
            (!strcmp(s1->a->a->sval, x_name) || !strcmp(s1->a->a->sval, y_name)))
            continue;
        if (ast_expr_has_side_effects(s2->a->a) || ast_expr_has_side_effects(s2->a->b))
            continue;

        if (!ast_gen_supported(s1->a->a) || !ast_gen_supported(s1->a->b) ||
            !ast_gen_supported(s2->a->a) || !ast_gen_supported(s2->a->b))
            continue;

        sprintf(qname, "#dmq%d", g_licm_seq++);
        sprintf(rname, "#dmr%d", g_licm_seq++);
        quot_sym = add_local_alloc(qname, is_signed ? TYPE_INT : (TYPE_INT | TYPE_UNSIGNED), 2);
        rem_sym = add_local_alloc(rname, is_signed ? TYPE_INT : (TYPE_INT | TYPE_UNSIGNED), 2);

        call_node = ast_new(&g_ast_arena, AST_DIVMOD_CALL);
        call_node->a = (struct AstNode *)mod_node->a;
        call_node->b = (struct AstNode *)mod_node->b;
        call_node->sym = quot_sym;
        call_node->sval = ast_arena_strdup(&g_ast_arena, rem_sym->name);
        call_node->ival = is_signed;

        quot_ident = ast_new(&g_ast_arena, AST_IDENT);
        quot_ident->sval = ast_arena_strdup(&g_ast_arena, quot_sym->name);
        quot_ident->type = quot_sym->type;
        rem_ident = ast_new(&g_ast_arena, AST_IDENT);
        rem_ident->sval = ast_arena_strdup(&g_ast_arena, rem_sym->name);
        rem_ident->type = rem_sym->type;

        new_s1_rhs = ast_replace_subtree(s1->a->b, mod_node, rem_ident);
        new_s1_rhs = ast_replace_subtree(new_s1_rhs, div_node, quot_ident);
        new_s2_rhs = ast_replace_subtree(s2->a->b, mod_node, rem_ident);
        new_s2_rhs = ast_replace_subtree(new_s2_rhs, div_node, quot_ident);

        new_s1_assign = ast_new(&g_ast_arena, AST_ASSIGN);
        *new_s1_assign = *(s1->a);
        new_s1_assign->b = new_s1_rhs;
        new_s1_stmt = ast_new(&g_ast_arena, AST_EXPR_STMT);
        new_s1_stmt->a = new_s1_assign;

        new_s2_assign = ast_new(&g_ast_arena, AST_ASSIGN);
        *new_s2_assign = *(s2->a);
        new_s2_assign->b = new_s2_rhs;
        new_s2_stmt = ast_new(&g_ast_arena, AST_EXPR_STMT);
        new_s2_stmt->a = new_s2_assign;

        compound = ast_new(&g_ast_arena, AST_COMPOUND);
        *compound = *n;
        compound->list = (struct AstNode **)ast_arena_alloc(&g_ast_arena,
            sizeof(struct AstNode *) * (size_t)(n->list_len + 1));
        compound->list_len = n->list_len + 1;
        compound->list_cap = n->list_len + 1;
        for (j = 0; j < i; ++j)
            compound->list[j] = n->list[j];
        compound->list[i] = call_node;
        compound->list[i + 1] = new_s1_stmt;
        compound->list[i + 2] = new_s2_stmt;
        for (j = i + 2; j < n->list_len; ++j)
            compound->list[j + 1] = n->list[j];
        return compound;
    }
    return NULL;
}

/* Detects a for-loop whose entire body is exactly one assignment (plain '='
 * or arithmetic compound +=/-=/ *=// =) to an array-element lvalue whose
 * address does not depend on the loop's own induction variable and has no
 * side effects anywhere in the statement - i.e. the store target is
 * provably the SAME memory location on every iteration, so its address can
 * be computed once before the loop instead of on every one of it:
 *
 *     for (k = ...; k < BOUND; k++)
 *         C[i][j] += A[i][k] * B[k][j];       (i, j do not involve k)
 *
 * is exactly tests/mm.c's matmult() inner loop: C[i][j]'s address never
 * changes across the k loop (i and j belong to the enclosing loops and are
 * untouched here), yet the ordinary codegen recomputes it from scratch on
 * all `m` iterations. Hoisting it collapses that to the same "compute the
 * address once, then walk" shape the same file's hand-optimised fmatmult()
 * already uses via an explicit local pointer.
 *
 * Conservative on purpose, in every direction that matters for a first cut
 * of this optimisation: the lvalue must be a bare AST_INDEX (no member/
 * deref lvalues yet), the rhs must have zero side effects at all (not just
 * "doesn't touch i/j" - ruling out e.g. a call that could mutate a global
 * i/j through some other path), and the assignment op is restricted to the
 * plain arithmetic set. Declining (0) is always safe: the caller falls
 * back to the ordinary per-iteration address computation. out_ivar_name/
 * out_lhs/out_val_type are only meaningful when this returns 1. */
int ast_for_hoist_lvalue_addr_supported(const struct AstNode *n,
                                               const char **out_ivar_name,
                                               const struct AstNode **out_lhs,
                                               int *out_val_type)
{
    const char *ivar_name;
    const struct AstNode *body_assign;
    const struct AstNode *lhs;
    const struct AstNode *rhs;
    int val_type;

    if (n == NULL || n->c == NULL)
        return 0;
    if ((n->c->kind == AST_UNARY || n->c->kind == AST_POSTFIX) &&
        (n->c->op == TOK_INC || n->c->op == TOK_DEC) &&
        n->c->a != NULL && n->c->a->kind == AST_IDENT) {
        ivar_name = n->c->a->sval;
    } else {
        return 0;
    }

    if (n->d == NULL || n->d->kind != AST_EXPR_STMT || n->d->a == NULL)
        return 0;
    body_assign = n->d->a;
    if (body_assign->kind != AST_ASSIGN)
        return 0;
    if (body_assign->op != '=' && body_assign->op != TOK_ADDEQ &&
        body_assign->op != TOK_SUBEQ && body_assign->op != TOK_MULEQ &&
        body_assign->op != TOK_DIVEQ)
        return 0;

    lhs = body_assign->a;
    rhs = body_assign->b;
    if (lhs == NULL || lhs->kind != AST_INDEX)
        return 0;
    if (!ast_index_lvalue_elem_type(lhs, &val_type))
        return 0;
    if (type_is_struct_object(val_type))
        return 0;

    if (ast_expr_references_ident(lhs, ivar_name))
        return 0;
    if (ast_expr_has_side_effects(lhs))
        return 0;
    if (rhs == NULL || ast_expr_has_side_effects(rhs))
        return 0;
    if (!ast_gen_supported(lhs))
        return 0;

    if (out_ivar_name != NULL) *out_ivar_name = ivar_name;
    if (out_lhs != NULL) *out_lhs = lhs;
    if (out_val_type != NULL) *out_val_type = val_type;
    return 1;
}

/* Same ivar/body shape check as ast_for_hoist_lvalue_addr_supported (a plain
 * "for (...; ...; ivar++/ivar--)" loop whose whole body is one assignment
 * statement with a side-effect-free rhs), but says nothing about the lhs -
 * this fires whether or not the lhs address happens to be hoistable, since a
 * row-invariant 2D array read living in the rhs (see
 * ast_hoist_row_invariant_2d_reads in dcc_ast_gen_stmt.c) is a useful hoist
 * on its own, independent of that other optimisation. Declining (0) is
 * always safe: the caller falls back to ordinary per-iteration codegen. */
int ast_for_rhs_hoist_scan_supported(const struct AstNode *n,
                                            const char **out_ivar_name,
                                            const struct AstNode **out_rhs)
{
    const char *ivar_name;
    const struct AstNode *body_assign;

    if (n == NULL || n->c == NULL)
        return 0;
    if ((n->c->kind == AST_UNARY || n->c->kind == AST_POSTFIX) &&
        (n->c->op == TOK_INC || n->c->op == TOK_DEC) &&
        n->c->a != NULL && n->c->a->kind == AST_IDENT) {
        ivar_name = n->c->a->sval;
    } else {
        return 0;
    }

    if (n->d == NULL || n->d->kind != AST_EXPR_STMT || n->d->a == NULL)
        return 0;
    body_assign = n->d->a;
    if (body_assign->kind != AST_ASSIGN)
        return 0;
    if (body_assign->op != '=' && body_assign->op != TOK_ADDEQ &&
        body_assign->op != TOK_SUBEQ && body_assign->op != TOK_MULEQ &&
        body_assign->op != TOK_DIVEQ)
        return 0;
    if (body_assign->b == NULL || ast_expr_has_side_effects(body_assign->b))
        return 0;

    if (out_ivar_name != NULL) *out_ivar_name = ivar_name;
    if (out_rhs != NULL) *out_rhs = body_assign->b;
    return 1;
}

/* Detects a for-loop whose body's FIRST statement is exactly
 *     IDENT = & BASE->FIELD[ INDEX_EXPR ];
 * where BASE is a plain file-scope static global and FIELD is a
 * pointer-typed member - tests/cint.c's run(): `in = &G->code[pc++];`, the
 * first statement of an otherwise call-heavy bytecode-dispatch loop body.
 * BASE->FIELD's VALUE (not its address) is re-fetched - a base-symbol load
 * then a dereference through the field's byte offset - on every iteration,
 * even though it cannot change across the whole loop's execution... which
 * ordinary side-effect analysis can't decide here, since the loop body
 * (the switch after this statement) is full of calls that analysis alone
 * cannot rule out as reassigning BASE.
 *
 * That is instead proven via the whole-file lexical write scan in
 * dcc_global_scan.c: this only fires when BASE is written in EXACTLY ONE
 * place in the entire translation unit and that place is not the function
 * currently being compiled (g_current_compiling_func) - and BASE must be
 * `static`, so (unlike a plain extern global) the whole file the scan
 * covers is *by language guarantee* every place that could possibly write
 * to it, not just everywhere this compiler happens to have looked. See
 * dcc_global_scan.c's header for exactly what this does and does not
 * prove - in particular, it does not verify the one writer function can
 * never be re-entered from this function's own call graph, only that it is
 * not this function itself.
 *
 * Conservative in every other direction too: only the loop's first
 * statement is ever inspected or rewritten - nothing about the rest of the
 * body (the switch) needs to be, or is, examined. Declining (0) is always
 * safe: the caller falls back to ordinary per-iteration codegen for the
 * whole loop, unchanged. */
int ast_for_hoist_global_member_value_supported(const struct AstNode *n,
                                                 const struct AstNode **out_member,
                                                 int *out_val_type)
{
    const struct AstNode *body;
    const struct AstNode *stmt0;
    const struct AstNode *assign;
    const struct AstNode *addr_of;
    const struct AstNode *index;
    const struct AstNode *member;
    struct Sym *base_sym;
    int val_type;

    if (n == NULL || n->d == NULL)
        return 0;
    body = n->d;
    if (body->kind != AST_COMPOUND || body->list_len < 1)
        return 0;

    stmt0 = body->list[0];
    if (stmt0 == NULL || stmt0->kind != AST_EXPR_STMT || stmt0->a == NULL)
        return 0;
    assign = stmt0->a;
    if (assign->kind != AST_ASSIGN || assign->op != '=')
        return 0;
    if (assign->a == NULL || assign->a->kind != AST_IDENT)
        return 0;

    addr_of = assign->b;
    if (addr_of == NULL || addr_of->kind != AST_UNARY || addr_of->op != '&')
        return 0;

    index = addr_of->a;
    if (index == NULL || index->kind != AST_INDEX || index->a == NULL)
        return 0;

    member = index->a;
    if (member->kind != AST_MEMBER || member->op != TOK_ARROW)
        return 0;
    if (member->a == NULL || member->a->kind != AST_IDENT || member->a->sval == NULL)
        return 0;

    base_sym = find_sym(member->a->sval);
    if (base_sym == NULL || base_sym->storage != SC_GLOBAL || !base_sym->is_static)
        return 0;
    if (!is_global_word_sym(base_sym))
        return 0;

    if (global_text_addr_taken_count(base_sym->name) != 0)
        return 0;
    if (global_text_write_count(base_sym->name) != 1)
        return 0;
    if (global_text_written_in_function(base_sym->name, g_current_compiling_func))
        return 0;

    val_type = ast_member_field_value_type(member);
    if (type_ptr_depth(val_type) == 0)
        return 0;

    *out_member = member;
    *out_val_type = val_type;
    return 1;
}
