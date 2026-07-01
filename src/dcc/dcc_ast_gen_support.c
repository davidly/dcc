/*
 * dcc_ast_gen_support.c - ast_gen_supported dispatch, call/struct gates, const folds.
 *
 * Split from dcc_ast_gen.c; part of the AST codegen module.  Shared
 * prototypes live in dcc_ast_gen_internal.h.
 */
#include <string.h>
#include "dcc_ast_gen_internal.h"


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


int ast_gen_supported(const struct AstNode *n)
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
            return ast_preincdec_plain_int(n);
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
            if (!ast_gen_supported(n->b) && n->b->kind != AST_CAST &&
                !ast_pointer_expr_type(n->b, &rhs_ptr_type, &rhs_no_deref) &&
                !(n->b->kind == AST_CALL && ast_value_is_pointer_word(n->b) &&
                  ast_call_named_args_supported(n->b)) &&
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
                       ast_value_is_plain_int(n->b);
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
                       ast_value_is_plain_int(n->b);
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
                       ast_value_is_plain_int(n->b);
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
                    return 0;
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
                if (!ast_value_is_plain_int(n->b))
                    return 0;
                base = find_sym(n->a->a->sval);
                decayed = base->is_array ? type_add_ptr(base->type) : base->type;
                elem = type_decay_ptr(decayed);
                if (type_size(elem) != 2 && (n->op != '=' || type_size(elem) != 1))
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
        /* Member lvalue store: s.f = rhs / p->f OP= rhs.  Restricted to an INT
         * (size 2) plain scalar field so the general store lowering is used
         * with no byte-field fast path intervening (the global struct byte-field
         * fast path needs a size-1 field). */
        if (n->a->kind == AST_MEMBER) {
            int field_type;
            if (ast_member_bitfield_lvalue_type(n->a, &field_type))
                return n->op == '=' && ast_value_is_plain_int(n->b);
            if (!ast_member_lvalue_type(n->a, &field_type))
                return 0;
            if (type_is_long(field_type)) {
                if (n->op == '=')
                    return ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b);
                if (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                    n->op == TOK_MULEQ || n->op == TOK_DIVEQ || n->op == TOK_MODEQ ||
                    n->op == TOK_ANDEQ || n->op == TOK_OREQ  || n->op == TOK_XOREQ)
                    return ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b);
                return 0;
            }
            if (type_is_float(field_type))
                return n->op == '=' &&
                       (ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b));
            if (type_is_bool(field_type))
                return n->op == '=' &&
                       (ast_value_is_plain_int(n->b) || ast_value_is_long_word(n->b) || ast_value_is_float_word(n->b));
            if (type_ptr_depth(field_type) > 0) {
                if (n->op == '=')
                    return type_size(field_type) == 2 &&
                           ast_pointer_assign_rhs_supported(n->b);
                /* char* (size-1 element) compound += / -= needs no scaling, so
                 * the unscaled compound tail is used directly. */
                if ((n->op == TOK_ADDEQ || n->op == TOK_SUBEQ) &&
                    type_index_elem_size(field_type) == 1)
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
                if (type_is_long(deref_type) && expr_result_dead &&
                    (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                     n->op == TOK_MULEQ || n->op == TOK_DIVEQ ||
                     n->op == TOK_MODEQ || n->op == TOK_ANDEQ ||
                     n->op == TOK_OREQ  || n->op == TOK_XOREQ))
                    return ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b);
                return type_is_float(deref_type) &&
                       (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                        n->op == TOK_MULEQ || n->op == TOK_DIVEQ) &&
                       (ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b));
            }
            if (type_is_long(deref_type))
                return ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b);
            if (type_is_float(deref_type))
                return ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b);
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
                return (sym_can_ix_direct(s) || expr_result_dead) &&
                       (ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b) ||
                        ast_value_is_long_word(n->b));
            if (!sym_can_ix_direct(s))
                return expr_result_dead &&
                       (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                        n->op == TOK_MULEQ || n->op == TOK_DIVEQ) &&
                       (ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b) ||
                        ast_value_is_long_word(n->b));
            if (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                n->op == TOK_MULEQ || n->op == TOK_DIVEQ)
                return ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b) ||
                       ast_value_is_long_word(n->b);
            return 0;
        }
        if (type_is_long(s->type)) {
            if (n->op == '=')
                return (sym_can_ix_direct(s) || expr_result_dead) &&
                       (ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b) ||
                        ast_value_is_float_word(n->b));
            if ((n->op == TOK_SHLEQ || n->op == TOK_SHREQ) &&
                !sym_can_ix_direct(s) && expr_result_dead)
                return ast_value_is_plain_int(n->b) || ast_value_is_long_word(n->b);
            if ((n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                 n->op == TOK_MULEQ || n->op == TOK_DIVEQ || n->op == TOK_MODEQ ||
                 n->op == TOK_ANDEQ || n->op == TOK_OREQ  || n->op == TOK_XOREQ) &&
                !sym_can_ix_direct(s) && expr_result_dead)
                return ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b);
            if (!sym_can_ix_direct(s))
                return 0;
            if (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                n->op == TOK_MULEQ || n->op == TOK_DIVEQ || n->op == TOK_MODEQ ||
                n->op == TOK_ANDEQ || n->op == TOK_OREQ  || n->op == TOK_XOREQ)
                return ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b);
            if (n->op == TOK_SHLEQ || n->op == TOK_SHREQ)
                return ast_value_is_plain_int(n->b) || ast_value_is_long_word(n->b);
            return 0;
        }
        if (!sym_can_ix_direct(s) && !is_global_word_sym(s) &&
            !(n->op == '=' && type_size(s->type) == 1 &&
                            (s->storage == SC_GLOBAL || s->storage == SC_EXTERN)) &&
                        !(expr_result_dead && type_size(s->type) == 1 &&
                            (n->op == TOK_ANDEQ || n->op == TOK_OREQ ||
                             n->op == TOK_XOREQ))) {
            if (n->op != '=') {
                if (expr_result_dead &&
                    (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
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
            if (!ast_value_is_plain_int(n->b))
                return 0;
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
                return sym_can_ix_direct(rs) || is_global_word_sym(rs);
            }
            if (ast_const_plain_int_binary_supported(n->b))
                return 1;
            if (n->b->kind == AST_MEMBER && ast_member_plain_int_read(n->b))
                return 1;
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
    case AST_INDEX:
        return ast_index_plain_int_read(n) || ast_index_long_read(n) ||
               ast_index_float_read(n);
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
            return 0;
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
            n->a->kind != AST_SIZEOF_EXPR && n->a->kind != AST_SIZEOF_TYPE)
            return 0;                  /* avoid ptr-sub / folded const operands */
        if (n->a->a != NULL && n->a->a->kind == AST_IDENT &&
            n->a->a->sval != NULL && !strcmp(n->a->a->sval, "__offsetof"))
            return 0;                  /* handled by the constant folder */
        return ast_gen_supported(n->a);
    case AST_COMMA:
        /* `a , b`: evaluate the left operand (value discarded), then the
         * right, whose value/type is the result.  A flat recursive walk emits
         * that when both sides are supported. */
        return ast_gen_supported(n->a) && ast_gen_supported(n->b);
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
    if (arg->kind == AST_IDENT) {
        s = find_sym(arg->sval);
         return s != NULL && s->storage != SC_FUNC && !s->is_array &&
             type_is_long(s->type);
    }
    if (arg->kind == AST_CALL && arg->a != NULL && arg->a->kind == AST_IDENT) {
        s = find_global(arg->a->sval);
        return s != NULL && type_is_long(s->type);
    }
    if (arg->kind == AST_CALL && ast_call_indirect_supported(arg)) {
        int callee_type;
        int no_deref;
        if (ast_pointer_expr_type(arg->a, &callee_type, &no_deref))
            return type_is_long(type_decay_ptr(callee_type));
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
            (ast_index_symbol_nd_elem_type(arg->a, &lhs_type) ||
             ast_index_pointer_expr_elem_type(arg->a, &lhs_type) ||
             ast_index_2d_array_elem_type(arg->a, &lhs_type)))
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

int ast_call_star_indirect_supported(const struct AstNode *n)
{
    const struct AstNode *base;
    struct Sym *s;
    int i;

    base = ast_call_star_indirect_base(n);
    if (base == NULL || base->kind != AST_IDENT)
        return 0;
    s = find_sym(base->sval);
    if (s == NULL || s->is_const_value || s->is_array)
        return 0;
    if (s->storage != SC_FUNC && type_ptr_depth(s->type) <= 0)
        return 0;
    for (i = 0; i < n->list_len; ++i) {
        if (!ast_call_arg_supported(NULL, i, n->list[i]))
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
    for (i = 0; i < n->list_len; ++i) {
        if (!ast_call_arg_supported(NULL, i, n->list[i]))
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
        return s != NULL && type_is_float(s->type);
    }
    if (arg->kind == AST_CALL && ast_call_indirect_supported(arg)) {
        int callee_type;
        int no_deref;
        if (ast_pointer_expr_type(arg->a, &callee_type, &no_deref))
            return type_is_float(type_decay_ptr(callee_type));
    }
    if (arg->kind == AST_CAST && type_is_float(arg->type))
        return ast_gen_supported(arg);
    if (arg->kind == AST_ASSIGN && arg->a != NULL && arg->a->kind == AST_IDENT) {
        s = find_sym(arg->a->sval);
        return s != NULL && !s->is_const_value && s->storage != SC_FUNC &&
               !s->is_array && type_is_float(s->type);
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
    default:
        return 0;
    }
}

int ast_pointer_assign_rhs_supported(const struct AstNode *n)
{
    const struct AstNode *value;
    int ptr_type;
    int no_deref;
    if (n == NULL)
        return 0;
    value = (n->kind == AST_CAST) ? n->a : n;
    if (ast_null_pointer_const(value))
        return 1;
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
        switch (n->kind == AST_BINARY ? n->op : n->kind) {
        case '+': *out = a + b; return 1;
        case '-': *out = a - b; return 1;
        case '*': *out = a * b; return 1;
        case '/': if (b == 0) return 0; *out = a / b; return 1;
        case '%': if (b == 0) return 0; *out = a % b; return 1;
        case TOK_SHL: *out = a << b; return 1;
        case TOK_SHR: *out = a >> b; return 1;
        case '&': *out = a & b; return 1;
        case '^': *out = a ^ b; return 1;
        case '|': *out = a | b; return 1;
        case '<': *out = a < b; return 1;
        case '>': *out = a > b; return 1;
        case TOK_LE: *out = a <= b; return 1;
        case TOK_GE: *out = a >= b; return 1;
        case TOK_EQ: *out = a == b; return 1;
        case TOK_NE: *out = a != b; return 1;
        case AST_LOGAND: *out = (a != 0) && (b != 0); return 1;
        case AST_LOGOR: *out = (a != 0) || (b != 0); return 1;
        default: return 0;
        }
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
        if (!sym_can_ix_direct(idx_sym) || type_size(idx_sym->type) != 1)
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
