/*
 * dcc_ast_gen_cond.c - statement-support gates, comparison/condition branch emitters.
 *
 * Split from dcc_ast_gen.c; part of the AST codegen module.  Shared
 * prototypes live in dcc_ast_gen_internal.h.
 */
#include <string.h>
#include "dcc_ast_gen_internal.h"


/* ------------------------------------------------------------------------- *
 * Statement-level AST codegen.
 *
 * A statement hook in gen_statement builds the upcoming statement from the
 * token stream and emits it from the AST.  Unsupported shapes are reported as
 * compiler errors in normal codegen.
 * ------------------------------------------------------------------------- */

/* Gate for `return [expr] ;`. */
int ast_return_stmt_supported(const struct AstNode *n)
{
    int rt = current_return_type;

    if (type_is_struct_object(rt)) {
        int src_type;
        if (n->a == NULL)
            return 0;
        return ast_struct_addr_expr_supported(n->a, &src_type) &&
               same_struct_type(rt, src_type);
    }
    if (rt & (TYPE_PTR | TYPE_PTR2)) {
        int ptr_type;
        int no_deref;
        if (n->a == NULL)
            return 1;
        if (ast_pointer_assign_rhs_supported(n->a))
            return 1;
        return ast_pointer_expr_type(n->a, &ptr_type, &no_deref);
    }
    if ((rt & 15) == TYPE_VOID)
        return n->a == NULL;
    if (type_is_float(rt)) {
        if (n->a == NULL)
            return 0;
        return ast_value_is_float_word(n->a) || ast_value_is_plain_int(n->a);
    }
    if (type_is_long(rt)) {
        if (n->a == NULL)
            return 0;
        return ast_value_is_long_word(n->a) || ast_value_is_plain_int(n->a);
    }
    if (type_size(rt) == 1) {
        if (n->a == NULL)
            return 1;
        if (n->a->kind == AST_IDENT) {
            struct Sym *rs = find_sym(n->a->sval);
            return sym_can_ix_direct(rs) && type_size(rs->type) == 1;
        }
        if (n->a->kind == AST_INT_LIT)
            return n->a->ival >= 0 && n->a->ival <= 255;
        return ast_gen_supported(n->a) && ast_value_is_plain_int(n->a);
    }
    if ((rt & 15) != TYPE_INT || type_size(rt) != 2)
        return 0;

    if (n->a != NULL) {
        if (ast_value_is_long_word(n->a))
            return 1;
        if (!ast_gen_supported(n->a) || !ast_value_is_plain_int(n->a))
            return 0;
    }
    return 1;
}

/* Emit `return [expr] ;`: evaluate the value into the ABI return registers
 * when present, then jump to the function's shared return label. */
void gen_return_ast(const struct AstNode *n)
{
    if (n->a != NULL && type_is_struct_object(current_return_type)) {
        int src_type;
        gen_struct_addr_expr_ast(n->a, &src_type);
        (void)src_type;
        emit("\tex de,hl\n");
        emit("\tld l,(ix+4)\n\tld h,(ix+5)\n");
        emit_copy_de_to_hl_bytes(type_size(current_return_type));
        g_expr_type = current_return_type;
    } else if (n->a != NULL && type_size(current_return_type) == 1) {
        if (n->a->kind == AST_IDENT) {
            struct Sym *rs = find_sym(n->a->sval);
            fprintf(outf, "\tld l,(ix%+d)\n", rs->offset);
            if (current_return_type & TYPE_UNSIGNED)
                emit("\tld h,0\n");
            else
                emit("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n");
            if (type_is_bool(current_return_type) && rs->storage == SC_PARAM)
                emit_bool_normalize_hl(current_return_type);
            g_expr_type = current_return_type;
        } else if (n->a->kind == AST_INT_LIT) {
            fprintf(outf, "\tld hl,%ld\n", n->a->ival & 255);
            g_expr_type = current_return_type;
        } else {
            ast_gen_expr(n->a);
        }
    } else if (n->a != NULL) {
        int ptr_type;
        int no_deref;
        if ((current_return_type & (TYPE_PTR | TYPE_PTR2)) &&
            n->a->kind == AST_CAST)
            ast_gen_expr(n->a->a);
        else if ((current_return_type & (TYPE_PTR | TYPE_PTR2)) &&
                 ast_pointer_expr_type(n->a, &ptr_type, &no_deref))
            gen_pointer_expr_ast(n->a, &ptr_type, &no_deref);
        else
            ast_gen_expr(n->a);
    }
    if (n->a != NULL) {
        if (type_is_bool(current_return_type)) {
            /* Only non-bool sources need normalising; a bool value is 0/1. */
            if (!ast_expr_yields_bool01(n->a))
                emit_bool_normalize_hl(g_expr_type);
            g_expr_type = current_return_type;
        } else if (type_is_float(current_return_type) && !type_is_float(g_expr_type)) {
            emit_convert_int_to_float(g_expr_type);
            g_expr_type = current_return_type;
        } else if (!type_is_float(current_return_type) && type_is_float(g_expr_type)) {
            emit_convert_float_to_intlike(current_return_type);
            g_expr_type = current_return_type;
        } else if (type_size(current_return_type) == 1 && !type_is_long(g_expr_type)) {
            if (current_return_type & TYPE_UNSIGNED)
                emit("\tld h,0\n");
            else
                emit("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n");
            g_expr_type = current_return_type;
        } else if (type_is_long(current_return_type) && !type_is_long(g_expr_type)) {
            emit_promote_int_to_long(g_expr_type, current_return_type);
            g_expr_type = current_return_type;
        }
    }
    emit_jp_label("jp", current_return_label);
}

/* A comparison operand that reaches the plain-16-bit direct-branch
 * path: a non-const, non-array, size-2 plain-int (signed/unsigned) or pointer
 * identifier reachable by the direct load (IX-direct local/param or global
 * word).  A size-1 (char/byte) operand would instead trigger the byte
 * relational fast path, and a constant operand the small-const-int relational
 * fast path, so both are excluded here. */
int ast_cmp_operand_ok(const struct AstNode *e)
{
    struct Sym *s;
    if (e == NULL)
        return 0;
    /* A struct field read `s.f` / `p->f` of a plain INT (size-2) field also
    * reaches the general plain-16-bit compare path: the leading token
     * is a struct/pointer identifier immediately followed by `.`/`->`, so
     * parse_byte_operand_fast declines (a member is neither a bare byte ident
     * nor a global byte array), gen_direct_small_const_int_rel declines (the
     * token after the base ident is `.`/`->`, not a relop), gen_direct_byte_
    * bitand declines (no `&`), and the global-char-array condition probe
    * declines without emitting (a struct base is not a global char
     * array followed by `[`).  Restricted to a 2-byte field so no byte
     * relational path can intervene; ast_member_plain_int_read already excludes
     * pointer/struct/array/bitfield fields, so the value is a true 16-bit int
     * (ptr_cmp detection stays 0, matching snippet_is_single_pointer_id). */
    if (e->kind == AST_MEMBER) {
        int field_type;
        if (!ast_member_plain_int_read(e))
            return 0;
        if (!ast_member_lvalue_type(e, &field_type) || type_size(field_type) != 2)
            return 0;
        return 1;
    }
    /* A pointer deref read `*p` of a 2-byte (int) element also reaches the
     * general plain-16-bit compare: the leading `*` is not TOK_ID, so the
     * global-char-array probe, the byte relational, and the small-const-int
     * relational fast paths all decline at their first-token test with no emit.
    * In the while context a comparison `*p OP x` is an AST_BINARY (not the
    * bare `while (*ptr)` truthiness fast path), so pointer-walk fast paths do
    * not apply.  Restricted to a
     * size-2 element so no byte path intervenes; char* (size-1) defers. */
    if (e->kind == AST_UNARY && e->op == '*') {
        int base;
        struct Sym *ps;
        if (!ast_deref_plain_int_read(e))
            return 0;
        if (e->a->kind != AST_IDENT)
            return 0;
        ps = find_sym(e->a->sval);
        if (ps == NULL)
            return 0;
        base = type_decay_ptr(ps->type);
        if (type_size(base) != 2)
            return 0;
        return 1;
    }
    /* A subscript read `arr[i]` of a 2-byte (int) element reaches the general
     * compare as well.  A size-1 element would be a global *char* array (the
    * only subscript shape with a dead-probe in the global-char-array condition
    * probe) or a byte relational operand, so we require size 2: an
     * int array / int* base whose is_global_char_array_sym test is false, so
     * the global-char-array probe declines at is_global_char_array_sym with no
     * emit.  ast_index_plain_int_read already requires a bare-identifier base
     * and a non-constant index. */
    if (e->kind == AST_INDEX) {
        struct Sym *base;
        int decayed;
        int elem;
        if (!ast_index_plain_int_read(e))
            return 0;
        if (e->a->kind == AST_IDENT) {
            base = find_sym(e->a->sval);
            if (base == NULL)
                return 0;
            decayed = base->is_array ? type_add_ptr(base->type) : base->type;
            elem = type_decay_ptr(decayed);
        } else if (e->a->kind == AST_MEMBER) {
            if (!ast_member_plain_array_field_elem_type(e->a, &elem))
                return 0;
        } else {
            return 0;
        }
        if (type_size(elem) != 2)
            return 0;
        return 1;
    }
    if (e->kind != AST_IDENT)
        return 0;
    s = find_sym(e->sval);
    if (s == NULL)
        return 0;
    if (s->is_array || s->is_const_value || s->storage == SC_FUNC)
        return 0;
    if (type_is_struct_object(s->type) || type_is_long(s->type) ||
        type_is_float(s->type))
        return 0;
    if (type_size(s->type) != 2)
        return 0;                          /* excludes char (byte rel path) */
    if (!sym_can_ix_direct(s) && !is_global_word_sym(s))
        return 0;
    return 1;
}

/* Does identifier operand `e` name a pointer object?  Selects the unsigned
 * compare/branch for a single-pointer-identifier operand. */
int ast_operand_is_ptr_ident(const struct AstNode *e)
{
    struct Sym *s;
    if (e == NULL || e->kind != AST_IDENT)
        return 0;
    s = find_sym(e->sval);
    return s != NULL && type_ptr_depth(s->type) > 0;
}

/* Is `n` a relational comparison `a OP b` of two qualifying identifier operands
 * that lower via the plain-16-bit direct-branch path (ast_gen_cmp_branch)?
 * Equality and ordering ops only; '&' is not a relational op. */
int ast_is_simple_cmp_cond(const struct AstNode *n)
{
    if (n == NULL || n->kind != AST_BINARY || !is_cmp_op(n->op))
        return 0;
    return ast_cmp_operand_ok(n->a) && ast_cmp_operand_ok(n->b);
}

/* Is `n` a relational comparison of a qualifying operand (ast_cmp_operand_ok)
 * against a plain-int constant of ANY value, using ANY relational operator?
 * This is the general counterpart to ast_is_const_cmp_cond's small byte-level
 * fast path (op '<' or '>= 0', constant 0..255): it declines whenever that
 * cheaper path already claims the comparison, and otherwise falls back to the
 * same plain-16-bit direct-branch emitter as ast_is_simple_cmp_cond
 * (ast_gen_cmp_branch), which is agnostic to whether an operand is an
 * identifier or a literal - ast_gen_expr loads either into HL. Without this,
 * a loop bound like `i <= SIZE` for a large SIZE has no direct-branch fast
 * path and falls all the way to the generic materialize-0/1-then-test path,
 * which is both slower and hides the loop shape from later structural
 * peephole passes (e.g. the LDIR-memset and strided-store rewrites). */
int ast_is_general_const_cmp_cond(const struct AstNode *n)
{
    if (n == NULL || n->kind != AST_BINARY || !is_cmp_op(n->op))
        return 0;
    if (ast_is_const_cmp_cond(n))
        return 0;
    if (n->a != NULL && n->a->kind == AST_INT_LIT &&
        ast_is_plain_int_type(n->a->type) && ast_cmp_operand_ok(n->b))
        return 1;
    if (n->b != NULL && n->b->kind == AST_INT_LIT &&
        ast_is_plain_int_type(n->b->type) && ast_cmp_operand_ok(n->a))
        return 1;
    return 0;
}

/* If `n` is a relational comparison lowered via the small-const-int
 * signed-local16 fast path (emit_cmp_const_branch_for_signed_local16 in
 * dcc_cmp.c), fill sp/opp/cp with the (sym, effective-op, const) to hand that
 * emitter and return 1; else 0.  The emitter accepts ONLY an IX-direct SIGNED
 * 16-bit local/param compared with a 0..255 constant, for `var < const` (any
 * 0..255) or `var >= 0`.  Two shapes are accepted: the DIRECT form
 * `var OP const`, and the FLIPPED const-on-left form which accepts only
 * `const > var` (=> var < const) and `const <= var` (=> var >= const).
 * Everything else - a global var (handled by the general plain path),
 * unsigned/char/long/float/struct var, const out of range, or any other
 * operator - is not recognised here.  The earlier byte relational and
 * byte-bitand fast paths decline for a size-2 operand, so this is the path that
 * fires. */
int ast_const_cmp_extract(const struct AstNode *n, struct Sym **sp,
                                 int *opp, long *cp)
{
    const struct AstNode *idn;
    const struct AstNode *cn;
    struct Sym *s;
    long c;
    int op;

    if (n == NULL || n->kind != AST_BINARY)
        return 0;

    if (n->a != NULL && n->a->kind == AST_IDENT &&
        n->b != NULL && n->b->kind == AST_INT_LIT) {
        /* direct: var OP const */
        idn = n->a;
        cn = n->b;
        op = n->op;
    } else if (n->a != NULL && n->a->kind == AST_INT_LIT &&
               n->b != NULL && n->b->kind == AST_IDENT) {
        /* flipped: const OP var - accept only '>' (=> var < const) and
         * TOK_LE (=> var >= const). */
        idn = n->b;
        cn = n->a;
        if (n->op == '>')
            op = '<';
        else if (n->op == TOK_LE)
            op = TOK_GE;
        else
            return 0;
    } else {
        return 0;
    }

    if (op != '<' && op != TOK_GE)
        return 0;

    s = find_sym(idn->sval);
    if (s == NULL)
        return 0;
    if (s->is_array || s->is_const_value || s->storage == SC_FUNC)
        return 0;
    if (type_is_struct_object(s->type) || type_is_long(s->type) ||
        type_is_float(s->type))
        return 0;
    if (type_size(s->type) != 2)
        return 0;
    if (s->type & TYPE_UNSIGNED)
        return 0;
    if (!sym_can_ix_direct(s))
        return 0;

    c = cn->ival;
    if (c < 0 || c > 255)
        return 0;
    if (op == TOK_GE && c != 0)           /* GE handled only for c == 0 */
        return 0;

    *sp = s;
    *opp = op;
    *cp = c;
    return 1;
}

int ast_is_const_cmp_cond(const struct AstNode *n)
{
    struct Sym *s;
    int op;
    long c;
    return ast_const_cmp_extract(n, &s, &op, &c);
}

int ast_is_const_plain_int_cmp_cond(const struct AstNode *n)
{
    return n != NULL && n->kind == AST_BINARY && is_cmp_op(n->op) &&
           ast_const_plain_int_binary_supported(n);
}

/* Translate a comparison operand expression into a ByteOperand, or return 0.
 * Recognises four kinds: kind 1 (IX-direct UNSIGNED char local/param),
 * kind 2 (0..255 constant), kind 3 (global byte array element, indexed by
 * either a constant or an IX-direct UNSIGNED char local/param), and kind 4
 * (`*p`, p an IX-direct pointer-to-unsigned-char local/param - e.g. the very
 * common `for (...) if (*p != val) ...; p++;` byte-scan loop). The kind-3
 * emitters (emit_byte_operand_to_a / emit_cp_byte_operand in dcc_cmp.c)
 * zero-extend op->idx_sym's single byte into D before the address add, so a
 * qualifying index must itself be a byte - a wider index is not handled here
 * (falls through to the generic path, same as any other unsupported shape). */

static int ast_strip_byte_cast_mask_cond(const struct AstNode **ep)
{
    long mask;
    const struct AstNode *e = *ep;

    if (e != NULL && e->kind == AST_CAST && type_size(e->type) == 1)
        e = e->a;
    if (e != NULL && e->kind == AST_BINARY && e->op == '&' &&
        e->b != NULL && e->b->kind == AST_INT_LIT &&
        ((unsigned long)e->b->ival & 0xffffffffUL) == 255UL)
        e = e->a;
    if (e != NULL && e->kind == AST_CAST && type_size(e->type) == 1)
        e = e->a;
    (void)mask;
    *ep = e;
    return e != NULL;
}

static int ast_low_byte_sum_operand_cond(const struct AstNode *e, struct ByteOperand *op)
{
    struct Sym *s;
    struct Sym *t;
    const struct AstNode *lhs;
    const struct AstNode *rhs;

    if (!ast_strip_byte_cast_mask_cond(&e))
        return 0;

    if (e->kind == AST_IDENT) {
        s = find_sym(e->sval);
        if (s != NULL && sym_can_ix_direct(s) && type_size(s->type) <= 4) {
            op->kind = 6;
            op->sym = s;
            op->idx_sym = NULL;
            op->val = 0;
            return 1;
        }
        return 0;
    }

    if (e->kind != AST_BINARY || e->op != '+')
        return 0;
    lhs = e->a;
    rhs = e->b;
    if (lhs == NULL || lhs->kind != AST_IDENT)
        return 0;
    s = find_sym(lhs->sval);
    if (s == NULL || !sym_can_ix_direct(s) || type_size(s->type) > 4)
        return 0;
    op->kind = 6;
    op->sym = s;
    op->idx_sym = NULL;
    op->val = 0;
    if (rhs != NULL && rhs->kind == AST_IDENT) {
        t = find_sym(rhs->sval);
        if (t == NULL || !sym_can_ix_direct(t) || type_size(t->type) > 4)
            return 0;
        op->idx_sym = t;
        return 1;
    }
    if (rhs != NULL && rhs->kind == AST_INT_LIT) {
        op->val = rhs->ival;
        return 1;
    }
    return 0;
}

int ast_byte_operand(const struct AstNode *e, struct ByteOperand *op)
{
    struct Sym *s;

    memset(op, 0, sizeof(*op));
    if (e == NULL)
        return 0;
    if (e->kind == AST_IDENT) {
        s = find_sym(e->sval);
        if (s != NULL && sym_can_ix_direct(s) && type_size(s->type) == 1 &&
            (s->type & TYPE_UNSIGNED)) {
            op->kind = 1;
            op->sym = s;
            return 1;
        }
        return 0;
    }
    if (e->kind == AST_INT_LIT) {
        if (e->ival >= 0 && e->ival <= 255) {
            op->kind = 2;
            op->val = e->ival;
            return 1;
        }
        return 0;
    }
    if (e->kind == AST_INDEX && e->a != NULL && e->a->kind == AST_IDENT &&
        e->b != NULL) {
        struct Sym *arr = find_global(e->a->sval);
        if (arr != NULL && arr->is_array && type_size(arr->type) == 1) {
            if (e->b->kind == AST_INT_LIT) {
                if (e->b->ival < 0)
                    return 0;
                op->kind = 3;
                op->sym = arr;
                op->idx_sym = NULL;
                op->val = e->b->ival;
                return 1;
            }
            if (e->b->kind == AST_IDENT) {
                struct Sym *idx = find_sym(e->b->sval);
                if (idx == NULL || !sym_can_ix_direct(idx) ||
                    type_size(idx->type) != 1 || !(idx->type & TYPE_UNSIGNED))
                    return 0;
                op->kind = 3;
                op->sym = arr;
                op->idx_sym = idx;
                return 1;
            }
            return 0;
        }
        /* Not a global byte array: fall through so the local pointer
         * subscript case below can recognise forms such as b[i]. */
    }
    if (e->kind == AST_UNARY && e->op == '*' &&
        e->a != NULL && e->a->kind == AST_IDENT) {
        int base;
        struct Sym *ps = find_sym(e->a->sval);
        if (ps == NULL || !sym_can_ix_direct(ps))
            return 0;
        base = type_decay_ptr(ps->type);
        if (type_size(base) != 1 || !(base & TYPE_UNSIGNED))
            return 0;
        op->kind = 4;
        op->sym = ps;
        return 1;
    }
    if (e->kind == AST_INDEX && e->a != NULL && e->a->kind == AST_IDENT &&
        e->b != NULL) {
        int base;
        struct Sym *ps = find_sym(e->a->sval);
        if (ps != NULL && sym_can_ix_direct(ps)) {
            base = type_decay_ptr(ps->type);
            if (type_size(base) == 1) {
                if (e->b->kind == AST_IDENT) {
                    struct Sym *idx = find_sym(e->b->sval);
                    if (idx != NULL && sym_can_ix_direct(idx) && type_size(idx->type) == 2) {
                        op->kind = 5;
                        op->sym = ps;
                        op->idx_sym = idx;
                        return 1;
                    }
                } else if (e->b->kind == AST_INT_LIT && e->b->ival >= 0) {
                    op->kind = 5;
                    op->sym = ps;
                    op->idx_sym = NULL;
                    op->val = e->b->ival;
                    return 1;
                }
            }
        }
    }
    if (ast_low_byte_sum_operand_cond(e, op))
        return 1;
    return 0;
}

/* Is `n` a relational comparison of two byte operands lowered via the direct
 * byte compare/branch path?  That path needs a real byte value in A for the
 * `cp`, so at least one operand must be a byte lvalue (kind 1/3); a compare of
 * two constants is not handled here.  The earlier const-&&-byte and byte-bitand
 * paths require a leading `const &&` or a `bytevar & mask` shape respectively,
 * neither of which is a bare relational comparison, so for a two-byte-operand
 * relation this byte path is what fires. */
int ast_is_byte_cmp_cond(const struct AstNode *n)
{
    struct ByteOperand lhs;
    struct ByteOperand rhs;

    if (n == NULL || n->kind != AST_BINARY || !is_cmp_op(n->op))
        return 0;
    if (!ast_byte_operand(n->a, &lhs) || !ast_byte_operand(n->b, &rhs))
        return 0;
    /* Succeeds iff a byte lvalue can supply A (after the optional const/lvalue
     * swap): at least one operand must be kind 1/3. */
    return byte_operand_can_be_lhs(&lhs) || byte_operand_can_be_lhs(&rhs);
}

int ast_is_direct_byte_bitand_cond(const struct AstNode *n)
{
    struct Sym *s;

    if (n == NULL || n->kind != AST_BINARY || n->op != '&')
        return 0;
    if (n->a == NULL || n->a->kind != AST_IDENT ||
        n->b == NULL || n->b->kind != AST_INT_LIT)
        return 0;
    s = find_sym(n->a->sval);
    if (s == NULL || !sym_can_ix_direct(s) || type_size(s->type) != 1)
        return 0;
    return n->b->ival >= 0 && n->b->ival <= 255;
}

/* Same idea as ast_is_direct_byte_bitand_cond, but for a wider (int/long)
 * ix-direct scalar ANDed with a byte-range mask (e.g. the very common
 * `if (e & 1)` parity test on a uint32_t loop counter). A mask in 0..255 only
 * ever touches the low (first, little-endian) byte of the operand - the AND
 * of that byte with the mask already determines the whole expression's
 * truth value regardless of the operand's width, so only that one byte needs
 * to be loaded and tested, exactly like the byte fast path above. */
int ast_is_direct_wide_bitand_cond(const struct AstNode *n)
{
    struct Sym *s;

    if (n == NULL || n->kind != AST_BINARY || n->op != '&')
        return 0;
    if (n->a == NULL || n->a->kind != AST_IDENT ||
        n->b == NULL || n->b->kind != AST_INT_LIT)
        return 0;
    s = find_sym(n->a->sval);
    if (s == NULL || !sym_can_ix_direct(s))
        return 0;
    if (!ast_is_plain_int_type(s->type) && !type_is_long(s->type))
        return 0;
    if (type_size(s->type) <= 1)
        return 0;
    return n->b->ival >= 0 && n->b->ival <= 255;
}

/* Extract an inclusive lower bound from one relational comparison node:
 * `x >= LO`, `x > LO`, `LO <= x`, or `LO < x` (LO a compile-time constant).
 * The strict forms are folded to their inclusive equivalent (`x > LO`
 * becomes `x >= LO+1`) so the caller only ever deals with inclusive bounds. */
static int ast_range_extract_lower(const struct AstNode *cmp,
                                    const struct AstNode **out_x, long *out_lo)
{
    if (cmp == NULL || cmp->kind != AST_BINARY)
        return 0;
    if (cmp->op == TOK_GE && cmp->b != NULL && cmp->b->kind == AST_INT_LIT) {
        *out_x = cmp->a; *out_lo = cmp->b->ival; return 1;
    }
    if (cmp->op == '>' && cmp->b != NULL && cmp->b->kind == AST_INT_LIT) {
        *out_x = cmp->a; *out_lo = cmp->b->ival + 1; return 1;
    }
    if (cmp->op == TOK_LE && cmp->a != NULL && cmp->a->kind == AST_INT_LIT) {
        *out_x = cmp->b; *out_lo = cmp->a->ival; return 1;
    }
    if (cmp->op == '<' && cmp->a != NULL && cmp->a->kind == AST_INT_LIT) {
        *out_x = cmp->b; *out_lo = cmp->a->ival + 1; return 1;
    }
    return 0;
}

/* Same idea as ast_range_extract_lower, but for an inclusive upper bound:
 * `x <= HI`, `x < HI`, `HI >= x`, or `HI > x`. */
static int ast_range_extract_upper(const struct AstNode *cmp,
                                    const struct AstNode **out_x, long *out_hi)
{
    if (cmp == NULL || cmp->kind != AST_BINARY)
        return 0;
    if (cmp->op == TOK_LE && cmp->b != NULL && cmp->b->kind == AST_INT_LIT) {
        *out_x = cmp->a; *out_hi = cmp->b->ival; return 1;
    }
    if (cmp->op == '<' && cmp->b != NULL && cmp->b->kind == AST_INT_LIT) {
        *out_x = cmp->a; *out_hi = cmp->b->ival - 1; return 1;
    }
    if (cmp->op == TOK_GE && cmp->a != NULL && cmp->a->kind == AST_INT_LIT) {
        *out_x = cmp->b; *out_hi = cmp->a->ival; return 1;
    }
    if (cmp->op == '>' && cmp->a != NULL && cmp->a->kind == AST_INT_LIT) {
        *out_x = cmp->b; *out_hi = cmp->a->ival - 1; return 1;
    }
    return 0;
}

/* Is `n` the classic range-check idiom `x >= LO && x <= HI` (any mix of
 * inclusive/strict spellings and operand orders, e.g. `x > LO && x < HI` or
 * `LO <= x && HI > x`), where LO and HI are compile-time constants and both
 * halves name the exact same plain scalar identifier? This is extremely
 * common for character classification (`p >= 'A' && p <= 'Z'`) and bounds
 * checks (`sq >= 0 && sq < 64`). The generic codegen evaluates and promotes
 * `x` twice - once per comparison, each a fresh reload-and-sign-extend for a
 * byte-sized x - and materializes an intermediate 0/1 bool for each half
 * before combining them. Since `x` is a bare identifier read here (never a
 * call, dereference, or anything else with a side effect or a reason to
 * produce a different value on a second read), fusing the two reads into one
 * is always safe: nothing can change `x` between them. ast_gen_range_check_
 * branch turns the pair into a single evaluation, a single promotion, and one
 * unsigned-subtract-and-compare against the span, with a direct branch and no
 * intermediate bool at all. */
int ast_is_range_check_cond(const struct AstNode *n, const struct AstNode **out_x,
                             long *out_lo, long *out_hi)
{
    const struct AstNode *x1;
    const struct AstNode *x2;
    long lo;
    long hi;

    if (n == NULL || n->kind != AST_LOGAND)
        return 0;
    if (!ast_range_extract_lower(n->a, &x1, &lo))
        return 0;
    if (!ast_range_extract_upper(n->b, &x2, &hi))
        return 0;
    if (x1 == NULL || x2 == NULL || x1->kind != AST_IDENT || x2->kind != AST_IDENT)
        return 0;
    if (strcmp(x1->sval, x2->sval) != 0)
        return 0;
    if (!ast_gen_supported(x1) || !ast_value_is_plain_int(x1))
        return 0;
    if (lo > hi || (hi - lo) >= 0xffffL)
        return 0;

    if (out_x) *out_x = x1;
    if (out_lo) *out_lo = lo;
    if (out_hi) *out_hi = hi;
    return 1;
}

/* Is `n` an `==`/`!=` comparison of a long (4-byte) ix-direct scalar against
 * a compile-time integer constant (either operand order)?  ast_long_cmp_supported
 * already accepts this shape, but its emitter (gen_long_cmp_ast) treats the
 * constant as an ordinary runtime operand - loading it through the general
 * expression path, pushing it, sign-extending it - before doing a full
 * generic long compare and materialising a 0/1 bool to test.  Since the
 * "other operand" here is known at compile time, the whole comparison
 * collapses to XOR-ing each of the 4 stored bytes against its matching
 * constant byte and OR-ing the results together (zero iff equal), with no
 * register shuffling or intermediate bool at all - direct-branchable exactly
 * like the byte/plain-int fast paths above. Relational ops (<, >=, ...) are
 * not handled here: unlike equality, they need sign-aware multi-byte
 * subtraction, not a bitwise fast path. */
int ast_is_direct_long_const_eq_cond(const struct AstNode *n)
{
    struct Sym *s;
    const struct AstNode *idn;

    if (n == NULL || n->kind != AST_BINARY)
        return 0;
    if (n->op != TOK_EQ && n->op != TOK_NE)
        return 0;
    if (n->a != NULL && n->a->kind == AST_IDENT &&
        n->b != NULL && n->b->kind == AST_INT_LIT) {
        idn = n->a;
    } else if (n->a != NULL && n->a->kind == AST_INT_LIT &&
               n->b != NULL && n->b->kind == AST_IDENT) {
        idn = n->b;
    } else {
        return 0;
    }
    s = find_sym(idn->sval);
    if (s == NULL || s->is_array || s->is_const_value || s->storage == SC_FUNC)
        return 0;
    if (!type_is_long(s->type))
        return 0;
    return sym_can_ix_direct(s);
}

int ast_global_char_index_cond(const struct AstNode *n, struct Sym **out_sym)
{
    struct Sym *s;

    if (n == NULL || n->kind != AST_INDEX || n->a == NULL || n->b == NULL ||
        n->a->kind != AST_IDENT)
        return 0;
    s = find_global(n->a->sval);
    if (!is_global_char_array_sym(s))
        return 0;
    if (!ast_index_subscript_supported(n->b))
        return 0;
    if (out_sym != NULL)
        *out_sym = s;
    return 1;
}

void ast_gen_global_char_index_branch(const struct AstNode *n, int label,
                                             int branch_when_true)
{
    struct Sym *s;
    int saved_dead;

    ast_global_char_index_cond(n, &s);
    saved_dead = expr_result_dead;
    expr_result_dead = 0;
    ast_gen_expr(n->b);
    expr_result_dead = saved_dead;
    if (!branch_when_true) {
        emit_test_global_char_index_zero(s, label);
    } else {
        int lzero = new_label();
        emit_test_global_char_index_zero(s, lzero);
        emit_jp_label("jp", label);
        emit_label(lzero);
    }
}

int ast_is_float_cmp_cond(const struct AstNode *n)
{
    int lhs_float;
    int rhs_float;

    if (n == NULL || n->kind != AST_BINARY || !is_cmp_op(n->op))
        return 0;
    lhs_float = ast_value_is_float_word(n->a);
    rhs_float = ast_value_is_float_word(n->b);
    if (!lhs_float && !rhs_float)
        return 0;
    return (lhs_float || ast_value_is_plain_int(n->a) ||
            ast_value_is_long_word(n->a)) &&
           (rhs_float || ast_value_is_plain_int(n->b) ||
            ast_value_is_long_word(n->b));
}

/* Is the controlling expression of an `if` / `while` one that AST should lower
 * via its GENERIC condition path (gen_expr + emit_test_expr_nonzero), rather
 * through the generic condition path rather than one of the specialised
 * direct-branch fast paths? Those
 * decline for a condition that has no top-level relational/logical/conditional
 * operator, is not a constant, is not a global-char-array subscript, and is not
 * the `char_ixvar & byteconst` bitand shape (the latter is already excluded
 * because a binary with a literal operand is not ast_gen_supported).  We accept
 * only a conservative whitelist proven to reach the generic path; anything else
 * defers (always safe).  The while gate additionally excludes bare deref
 * conditions that belong to pointer-walk fast paths. */
int ast_cond_generic(const struct AstNode *n)
{
    long cv;
    if (n == NULL)
        return 0;
    if (ast_const_condition_fold(n, &cv))
        return 1;
    if (ast_is_const_cmp_cond(n))
        return 1;
    if (ast_is_const_plain_int_cmp_cond(n))
        return 1;
    if (ast_is_byte_cmp_cond(n))
        return 1;
    if (ast_is_direct_byte_bitand_cond(n))
        return 1;
    if (ast_is_direct_wide_bitand_cond(n))
        return 1;
    if (ast_global_char_index_cond(n, NULL))
        return 1;
    if (ast_is_float_cmp_cond(n))
        return 1;
    if (ast_is_direct_long_const_eq_cond(n))
        return 1;
    if (ast_long_cmp_supported(n))
        return 1;
    if (ast_index_cmp_cond_supported(n))
        return 1;
    if (ast_gen_supported(n) &&
        (ast_value_is_float_word(n) || ast_value_is_pointer_word(n) ||
         ast_value_is_long_word(n)) &&
        !ast_node_is_const(n))
        return 1;
    if (!ast_gen_supported(n) || !ast_value_is_plain_int(n))
        return 0;
    switch (n->kind) {
    case AST_IDENT:
        /* if (x): no '[' follows (a subscript would be AST_INDEX), so the
         * global-char-array and byte fast paths all decline. */
        return 1;
    case AST_CALL:
        /* if (f(...)): a call result, never a fast-path shape. */
        return 1;
    case AST_MEMBER:
    case AST_POSTFIX:
        /* if (s.f) / if (p++): leading TOK_ID but not a global-char-array
         * subscript nor a char-ix '& mask'; parse_byte_operand_fast only
         * matches byte consts / unsigned-char ix vars / global byte arrays,
         * so the byte and relational fast paths all decline. */
        return 1;
    case AST_INDEX: {
        struct Sym *gs;
        if (n->a != NULL && n->a->kind == AST_IDENT) {
            gs = find_global(n->a->sval);
            if (is_global_char_array_sym(gs))
                return 0;
        }
        return 1;
    }
    case AST_UNARY:
        /* if (!x) / if (*p) / if (-x) etc.: the leading token is the operator
         * (not TOK_ID/NUM), so every condition fast path declines.  '&'
         * address-of yields a pointer and is already filtered above by the
         * ast_value_is_plain_int check. */
        return 1;
    case AST_BINARY:
        /* A relational comparison of two plain-int-16 (or pointer) identifiers
         * uses the general plain-16-bit compare/branch path (ast_gen_cmp_branch)
         * - the byte and small-const-int relational fast paths decline for two
         * size-2 non-const operands.  Other supported comparisons whose operands
         * are not direct-branch fast-path shapes (for example struct-member
         * comparisons) use the generic value-emit + nonzero-test path. */
        if (is_cmp_op(n->op))
            return ast_is_simple_cmp_cond(n) || ast_gen_supported(n);
        if (n->op == '&')
            return !ast_is_direct_byte_bitand_cond(n);
        return 1;
    case AST_LOGAND:
    case AST_LOGOR:
        /* if (a && b) / while (a || b): condition fast paths all
         * decline for a top-level &&/|| (simple_direct_condition_until requires
         * no logical operator), so AST falls to the
         * generic gen_expr + emit_test_expr_nonzero - which the AST reproduces
         * via ast_gen_cond_branch's generic fallback (ast_gen_expr emits the
         * same short-circuit 0/1 value, then the same nonzero test).  The guard
         * above already required ast_gen_supported && plain-int && non-const. */
        return 1;
    case AST_COND:
        /* A top-level ?: controlling expression has no condition fast path in
         * the AST direct-branch helpers; supported plain-int conditionals reach the generic
         * gen_expr + nonzero-test path that ast_gen_cond_branch emits. */
        return 1;
    case AST_ASSIGN:
        /* Assignment in a controlling expression is excluded from all direct
         * condition probes, so AST evaluates it normally and tests the
         * resulting value. */
        return 1;
    default:
        return 0;
    }
}

/* Recognise the `lhs = rhs1 +/- rhs2` simple-local self-add statement shape.
 * The AST walker emits the compact historical sequence directly when lhs,
 * rhs1 and rhs2 are all ix-direct 2-byte locals. */
int ast_is_local_self_add_stmt(const struct AstNode *e)
{
    const struct AstNode *lhs;
    const struct AstNode *rhs;
    const struct AstNode *a;
    const struct AstNode *b;
    struct Sym *s;

    if (e->kind != AST_ASSIGN || e->op != '=')
        return 0;
    lhs = e->a;

    if (lhs == NULL || lhs->kind != AST_IDENT)
        return 0;
    s = find_sym(lhs->sval);
    if (s == NULL || !sym_can_ix_direct(s) || type_size(s->type) != 2)
        return 0;
    rhs = e->b;
    if (rhs == NULL || rhs->kind != AST_BINARY)
        return 0;
    if (rhs->op != '+' && rhs->op != '-')
        return 0;
    a = rhs->a;
    b = rhs->b;
    if (a == NULL || a->kind != AST_IDENT || b == NULL || b->kind != AST_IDENT)
        return 0;
    s = find_sym(a->sval);
    if (s == NULL || !sym_can_ix_direct(s) || type_size(s->type) != 2)
        return 0;
    s = find_sym(b->sval);
    if (s == NULL || !sym_can_ix_direct(s) || type_size(s->type) != 2)
        return 0;
    return 1;
}

void ast_emit_local_self_add_stmt(const struct AstNode *e)
{
    const struct AstNode *lhs = e->a;
    const struct AstNode *rhs = e->b;
    const struct AstNode *rhs1 = rhs->a;
    const struct AstNode *rhs2 = rhs->b;
    struct Sym *lhs_sym = find_sym(lhs->sval);
    struct Sym *rhs1_sym = find_sym(rhs1->sval);
    struct Sym *rhs2_sym = find_sym(rhs2->sval);

    fprintf(outf, "\tld l,(ix%+d)\n", rhs1_sym->offset);
    fprintf(outf, "\tld h,(ix%+d)\n", rhs1_sym->offset + 1);
    fprintf(outf, "\tld e,(ix%+d)\n", rhs2_sym->offset);
    fprintf(outf, "\tld d,(ix%+d)\n", rhs2_sym->offset + 1);
    if ((rhs1_sym->type & (TYPE_PTR | TYPE_PTR2)) &&
        !(rhs2_sym->type & (TYPE_PTR | TYPE_PTR2))) {
        int elem = type_index_elem_size(rhs1_sym->type);
        if (elem > 1) {
            emit("\tpush hl\n");
            emit("\tex de,hl\n");
            scale_hl_by_elem_size(elem);
            emit("\tex de,hl\n");
            emit("\tpop hl\n");
        }
    }
    if (rhs->op == '+') {
        emit("\tadd hl,de\n");
    } else {
        emit("\tor a\n\tsbc hl,de\n");
        if ((rhs1_sym->type & (TYPE_PTR | TYPE_PTR2)) &&
            (rhs2_sym->type & (TYPE_PTR | TYPE_PTR2))) {
            int elem = type_index_elem_size(rhs1_sym->type);
            if (elem > 1) {
                fprintf(outf, "\tld de,%d\n", elem);
                emit_runtime_call("__divs");
            }
        }
    }
    fprintf(outf, "\tld (ix%+d),l\n", lhs_sym->offset);
    fprintf(outf, "\tld (ix%+d),h\n", lhs_sym->offset + 1);
    g_expr_type = lhs_sym->type;
}

/* For a dead-result top-level ++/-- statement on a bare identifier, return the
 * symbol if it matches emit_incdec_sym_direct's fast path (ix-direct
 * or global word, any scalar/pointer/long size), else NULL. */
struct Sym *ast_deadincdec_sym_direct(const struct AstNode *e)
{
    struct Sym *s;
    if (e->a == NULL || e->a->kind != AST_IDENT)
        return NULL;
    s = find_sym(e->a->sval);
    if (s == NULL || s->is_const_value || s->storage == SC_FUNC || s->is_array)
        return NULL;
    if (!sym_can_ix_direct(s) && !is_global_word_sym(s))
        return NULL;
    return s;
}

/* Dead-result ++/-- on a struct member lvalue: AST computes the field
 * address (gen_lvalue_addr) then emit_incdec_addr, which inc/decs in place by
 * 1 for sizes 1/2/4.  Pointers are advanced by 1 byte here, so only elem-size-1
 * pointers (e.g. char*) match; wider element pointers stay deferred. */
int ast_deadincdec_member_ok(const struct AstNode *e)
{
    int t;
    if (e->a == NULL || e->a->kind != AST_MEMBER)
        return 0;
    if (!ast_member_bitfield_lvalue_type(e->a, &t) &&
        !ast_member_lvalue_type(e->a, &t))
        return 0;
    if (type_ptr_depth(t) > 0)
        return type_index_elem_size(t) == 1;
    return ast_is_plain_int_type(t) ||
           (type_size(t) == 4 && type_ptr_depth(t) == 0);
}

int ast_incdec_addr_type_ok(int t)
{
    if (type_ptr_depth(t) > 0)
        return type_index_elem_size(t) == 1;
    return ast_is_plain_int_type(t) || type_size(t) == 4;
}

int ast_index_lvalue_elem_type(const struct AstNode *n, int *out_type)
{
    struct Sym *s;
    int decayed;
    int elem;

    if (n == NULL || n->kind != AST_INDEX || n->a == NULL)
        return 0;
    if (!ast_index_subscript_supported(n->b))
        return 0;
    if (ast_index_symbol_nd_elem_type(n, &elem) ||
        ast_index_deref_pointer_array_collect(n, &s, NULL, NULL, NULL, &elem) ||
        ast_index_2d_array_elem_type(n, &elem) ||
        ast_index_pointer_expr_elem_type(n, &elem) ||
        ast_index_reversed_pointer_expr_elem_type(n, &elem) ||
        ast_index_pointer_array_elem_type(n, &elem) ||
        ast_index_member_pointer_elem_type(n, &elem)) {
        *out_type = elem;
        return 1;
    }
    if (n->a->kind == AST_IDENT) {
        s = find_sym(n->a->sval);
        if (s == NULL || s->is_const_value || s->storage == SC_FUNC)
            return 0;
        decayed = s->is_array ? type_add_ptr(s->type) : s->type;
    } else if (n->a->kind == AST_MEMBER) {
        if (ast_member_array_field_elem_type(n->a, &elem)) {
            *out_type = elem;
            return 1;
        }
        if (!ast_member_lvalue_type(n->a, &decayed))
            return 0;
    } else if (n->a->kind == AST_INDEX) {
        int no_deref;
        if (!ast_pointer_expr_type(n->a, &decayed, &no_deref))
            return 0;
    } else {
        return 0;
    }
    if (type_ptr_depth(decayed) <= 0)
        return 0;
    *out_type = type_decay_ptr(decayed);
    return 1;
}

int ast_deadincdec_addr_lvalue_type(const struct AstNode *e, int *out_type)
{
    const struct AstNode *lv;
    struct Sym *s;
    int t;

    if (e == NULL || (e->kind != AST_UNARY && e->kind != AST_POSTFIX))
        return 0;
    if (e->op != TOK_INC && e->op != TOK_DEC)
        return 0;
    lv = e->a;
    if (lv == NULL)
        return 0;
    switch (lv->kind) {
    case AST_IDENT:
        s = find_sym(lv->sval);
        if (s == NULL || s->is_const_value || s->storage == SC_FUNC || s->is_array)
            return 0;
        t = s->type;
        break;
    case AST_MEMBER:
        if (!ast_member_bitfield_lvalue_type(lv, &t) &&
            !ast_member_lvalue_type(lv, &t))
            return 0;
        break;
    case AST_UNARY:
        if (lv->op != '*' || !ast_deref_lvalue_type(lv, &t))
            return 0;
        break;
    case AST_INDEX:
        if (!ast_index_lvalue_elem_type(lv, &t))
            return 0;
        break;
    default:
        return 0;
    }
    if (!ast_incdec_addr_type_ok(t))
        return 0;
    *out_type = t;
    return 1;
}

void gen_deadincdec_addr_lvalue_ast(const struct AstNode *e, int *out_type)
{
    const struct AstNode *lv = e->a;
    struct Sym *s;

    switch (lv->kind) {
    case AST_IDENT:
        s = find_sym(lv->sval);
        emit_load_sym_addr(s);
        *out_type = s->type;
        return;
    case AST_MEMBER:
        gen_member_addr_ast(lv, out_type);
        return;
    case AST_UNARY:
        gen_deref_addr_ast(lv, out_type);
        return;
    case AST_INDEX:
        gen_index_addr_ast(lv, out_type);
        return;
    default:
        fatal("gen_deadincdec_addr_lvalue_ast: unsupported lvalue");
    }
}

int ast_dead_expr_supported(const struct AstNode *e)
{
    int old_dead;
    int ok;
    if (e == NULL)
        return 0;
    if ((e->kind == AST_UNARY || e->kind == AST_POSTFIX) &&
        (e->op == TOK_INC || e->op == TOK_DEC)) {
        /* Dead-result ++/-- statement: mirror the sym-direct fast path
         * (emit_incdec_sym_direct) for an ix-direct or global-word ident.
         * Other lvalues (members, deref) stay deferred. */
        if (ast_deadincdec_sym_direct(e) != NULL)
            return 1;
        if (ast_deadincdec_member_ok(e))
            return 1;
        if (ast_deadincdec_addr_lvalue_type(e, &ok))
            return 1;
        return 0;
    }
    if (e->kind == AST_CAST && (e->type & 15) == TYPE_VOID)
        return e->a != NULL && ast_gen_supported(e->a);
    if (ast_is_local_self_add_stmt(e))
        return 0;
    /* Evaluate the support gate in the SAME dead-result context the walker will
    * emit under: expression statements set expr_result_dead = 1 before lowering,
     * and ast_gen_supported's AST_ASSIGN case defers the dead-result `+=`/`-=`
     * fast paths only when expr_result_dead is set.  Without this, those shapes
     * (e.g. `x += 5;`) would wrongly pass the gate here and the walker would
    * emit a divergent (longer) sequence instead of the compact one. */
    old_dead = expr_result_dead;
    expr_result_dead = 1;
    ok = ast_gen_supported(e);
    expr_result_dead = old_dead;
    return ok;
}

int ast_for_init_expr_supported(const struct AstNode *e)
{
    int old_dead;
    int ok;
    if (e == NULL)
        return 1;
    old_dead = expr_result_dead;
    expr_result_dead = 1;
    if (e->kind == AST_ASSIGN)
        ok = e->op == '=' && ast_gen_supported(e);
    else if (e->kind == AST_COMMA)
        ok = ast_gen_supported(e);
    else
        ok = e->kind == AST_CALL && ast_gen_supported(e);
    expr_result_dead = old_dead;
    return ok;
}

/* Is the expression-statement node `n` (n->a is the expression) AST-emittable?
 * An expression statement is emitted with dead-result semantics.  The AST path
 * handles the compact top-level inc/dec and simple local self-add shapes
 * directly; global char-array stores and CRC-update byte idioms are excluded by
 * the ordinary expression support gates. */
int ast_expr_stmt_supported(const struct AstNode *n)
{
    const struct AstNode *e = n->a;
    if (e == NULL)
        return 0;
    if (ast_is_local_self_add_stmt(e))
        return 1;
    return ast_dead_expr_supported(e);
}

/* Is statement node `n` within the AST-emittable subset? */
int ast_stmt_supported(const struct AstNode *n)
{
    switch (n->kind) {
    case AST_EMPTY:
        return 1;                         /* `;` emits nothing */
    case AST_DECL:
        /* A captured declaration span is always emittable: it delegates to
         * declaration codegen (which rebuilds locals[] / frame
         * offsets exactly as the frame-sizing scan did). */
        return 1;
    case AST_EXPR_STMT:
        return ast_expr_stmt_supported(n);
    case AST_RETURN:
        return ast_return_stmt_supported(n);
    case AST_BREAK:
    case AST_CONTINUE:
        /* A bare jump to the innermost loop/switch exit/continue label. */
        return nflow > 0;
    case AST_GOTO:
        /* Unconditional jump to a named user label. */
        return n->sval != NULL;
    case AST_LABEL:
        /* A user label is emittable when its labeled statement is too. */
        return n->sval != NULL && n->b != NULL && ast_stmt_supported(n->b);
    case AST_CASE:
    case AST_DEFAULT:
        return ast_switch_gate_depth > 0 && n->b != NULL && ast_stmt_supported(n->b);
    case AST_IF:
        /* Generic-path condition + both branch statements emittable. */
        if (!ast_cond_generic(n->a))
            return 0;
        if (n->b == NULL || !ast_stmt_supported(n->b))
            return 0;
        if (n->c != NULL && !ast_stmt_supported(n->c))
            return 0;
        return 1;
    case AST_WHILE:
        /* Generic-path condition + emittable body. */
        {
            int old_nflow;
            int ok;
            if (!ast_is_const_nonzero_condition(n->a) && !ast_cond_generic(n->a))
                return 0;
            if (n->b == NULL)
                return 0;
            old_nflow = nflow;
            nflow++;
            ok = ast_stmt_supported(n->b);
            nflow = old_nflow;
            return ok;
        }
    case AST_DOWHILE:
        /* Generic-path condition + emittable body.  A bare `*ptr` condition
         * reaches the generic path here (no deref fast path), so no extra
         * exclusion is needed.  The `do{}while(0)` macro-wrapper idiom keeps
         * labels but emits no condition/back-edge code, so accept that
         * const-zero case too. */
        {
            int old_nflow;
            int ok;
            if (!ast_is_const_zero_condition(n->a) &&
                !ast_is_const_nonzero_condition(n->a) && !ast_cond_generic(n->a))
                return 0;
            if (n->b == NULL)
                return 0;
            old_nflow = nflow;
            nflow++;
            ok = ast_stmt_supported(n->b);
            nflow = old_nflow;
            return ok;
        }
    case AST_FOR: {
        int old_nflow;
        int ok;
        int for_seq;
        int rename_count;
        /* Narrow slice: for ([init] ; [cond] ; [inc]) body.  The builder
         * stores init in a, cond in b, inc in c, and body in d.  A C99 for-init
         * declaration arrives as an AST_DECL span; ast_gen_for_stmt replays it
         * through declaration codegen and for-scope rename
         * machinery.  An expression init excludes the transform-prone constant
         * assignment shape and must have no recorded for-scope renames.
         *
         * This gate mirrors ast_gen_for_stmt's pre-order g_for_seq numbering.
         * For a for-init declaration the loop variable's local slot does not
         * exist yet (codegen creates it only when the declaration is emitted),
         * so we replay the declaration with emission suppressed (scan_mode) to
         * materialise the slot and push its rename, gate the
         * condition/increment/body while they can resolve, then roll back the
         * local table and rename stack.  g_for_seq is intentionally left at the
         * post-order cursor for sibling gates; the top-level AST statement
         * probe restores it before real emission. */
        if (n->d == NULL)
            return 0;
        if (g_for_seq >= MAX_FOR_SCOPES)
            return 0;
        for_seq = g_for_seq++;
        /* g_for_rename_count[] is indexed by for_seq, reused across
         * functions and across passes; nothing resets it on its own now
         * that dcc_func.c's old hand-written frame-sizing scanner (which
         * used to zero this slot for every for-loop it walked past) is
         * gone. Reset unconditionally before reading it, so a plain
         * expression init reliably sees "no renames" instead of whatever a
         * decl-init loop that previously owned this for_seq slot left
         * behind - matching the same reset in ast_gen_for_stmt. Not rolled
         * back afterward: this probe's own speculative work IS rolled back
         * below (nlocals/local_size/etc.), but the real ast_gen_for_stmt
         * call that follows for this same for_seq always resets and
         * re-records its own count independently regardless, so leaving
         * this slot at whatever this probe computed cannot affect it. */
        g_for_rename_count[for_seq] = 0;
        rename_count = g_for_rename_count[for_seq];

        if (n->a != NULL && n->a->kind == AST_DECL) {
            int s_nlocals = nlocals;
            int s_local_size = local_size;
            int s_forren_n = g_forren_n;
            int s_nulabels = nulabels;
            int s_static_seq = g_static_local_seq;
            int s_scope_depth = g_scope_depth;
            int s_has_call = current_function_has_call;
            int s_decl_seq = g_for_decl_seq;
            int s_decl_index = g_for_decl_rename_index;
            int s_decl_recording = g_for_decl_recording;
            int s_scan_mode = scan_mode;
            FILE *s_outf = outf;
            static FILE *sink = NULL;

            ok = 1;
            /* Redirect emission to a throwaway sink so the suppressed replay
             * cannot leak partial output (scan_mode guards most but not every
             * emit path), and set scan_mode so nested AST build/gen and the
             * remaining guarded emits stay quiet.
             *
             * g_for_decl_recording=1 (not 0): nothing pre-populates
             * g_for_rename_count[for_seq] any more (see the matching comment
             * in ast_gen_for_stmt - the hand-written frame-sizing scanner
             * that used to do that recording is gone), so this probe must
             * record its own fresh count from the just-reset slot rather
             * than validate against a stale/zero one. */
            if (sink == NULL)
                sink = fopen(DCC_NULL_DEVICE, "w");
            if (sink != NULL)
                outf = sink;
            scan_mode = 1;
            g_for_decl_seq = for_seq;
            g_for_decl_rename_index = 0;
            g_for_decl_recording = 1;
            ast_emit_decl_span(n->a);
            g_for_decl_seq = s_decl_seq;
            g_for_decl_rename_index = s_decl_index;
            g_for_decl_recording = s_decl_recording;

            if (n->b != NULL && !ast_cond_generic(n->b)) {
                ok = 0;
            }
            if (ok && n->c != NULL && !ast_dead_expr_supported(n->c)) {
                ok = 0;
            }
            if (ok) {
                old_nflow = nflow;
                nflow++;
                ok = ast_stmt_supported(n->d);
                nflow = old_nflow;
            }
            scan_mode = s_scan_mode;
            outf = s_outf;

            nlocals = s_nlocals;
            local_size = s_local_size;
            g_forren_n = s_forren_n;
            nulabels = s_nulabels;
            g_static_local_seq = s_static_seq;
            g_scope_depth = s_scope_depth;
            current_function_has_call = s_has_call;
            return ok;
        }

        if (rename_count != 0) {
            return 0;
        }
        if (!ast_for_init_expr_supported(n->a)) {
            return 0;
        }
        if (n->b != NULL && !ast_cond_generic(n->b)) {
            return 0;
        }
        if (n->c != NULL && !ast_dead_expr_supported(n->c)) {
            return 0;
        }
        old_nflow = nflow;
        nflow++;
        ok = ast_stmt_supported(n->d);
        nflow = old_nflow;
        return ok;
    }
    case AST_SWITCH: {
        int old_nflow;
        int ok;
        if (n->a == NULL || !ast_gen_supported(n->a) ||
            (!ast_value_is_plain_int(n->a) && !ast_value_is_long_word(n->a)))
            return 0;
        if (n->b == NULL)
            return 0;
        old_nflow = nflow;
        nflow++;
        ast_switch_gate_depth++;
        ok = ast_stmt_supported(n->b);
        ast_switch_gate_depth--;
        nflow = old_nflow;
        return ok;
    }
    case AST_COMPOUND: {
        /* A brace block is emittable when every child statement is.  Block
         * declarations are AST_DECL spans (always emittable themselves); but a
         * *later* sibling referencing a block-local name cannot resolve it at
         * gate time because codegen only creates the local when the decl is
         * emitted.  So replay each declaration with emission suppressed
         * (scan_mode + a throwaway outf sink) to materialise its local slots
         * and scope, gate the remaining children while they resolve, then roll
         * back every mutated codegen counter (ast_gen_stmt re-emits the decls
         * for real). */
        int i;
        int ok;
        int has_decl;

        has_decl = 0;
        for (i = 0; i < n->list_len; ++i) {
            if (n->list[i]->kind == AST_DECL) {
                has_decl = 1;
                break;
            }
        }
        if (!has_decl) {
            for (i = 0; i < n->list_len; ++i)
                if (!ast_stmt_supported(n->list[i]))
                    return 0;
            return 1;
        }

        {
            int s_nlocals = nlocals;
            int s_local_size = local_size;
            int s_scope_depth = g_scope_depth;
            int s_forren_n = g_forren_n;
            int s_nulabels = nulabels;
            int s_static_seq = g_static_local_seq;
            int s_has_call = current_function_has_call;
            int s_scan_mode = scan_mode;
            FILE *s_outf = outf;
            static FILE *sink = NULL;

            if (sink == NULL)
                sink = fopen(DCC_NULL_DEVICE, "w");
            if (sink != NULL)
                outf = sink;
            scan_mode = 1;
            enter_scope();
            ok = 1;
            for (i = 0; i < n->list_len; ++i) {
                struct AstNode *c = n->list[i];
                if (c->kind == AST_DECL) {
                    ast_emit_decl_span(c);
                } else if (!ast_stmt_supported(c)) {
                    ok = 0;
                    break;
                }
            }
            leave_scope();
            scan_mode = s_scan_mode;
            outf = s_outf;

            nlocals = s_nlocals;
            local_size = s_local_size;
            g_scope_depth = s_scope_depth;
            g_forren_n = s_forren_n;
            nulabels = s_nulabels;
            g_static_local_seq = s_static_seq;
            current_function_has_call = s_has_call;
            return ok;
        }
    }
    default:
        return 0;
    }
}

/* Emit a relational comparison `a OP b` as a direct conditional branch to
 * `label` via the plain-16-bit path: load LHS into HL, push it, load RHS into
 * HL, ex de,hl / pop hl (HL=lhs, DE=rhs), then the signed or unsigned
 * compare/branch for the operator and branch sense.  The operand loads come
 * from ast_gen_expr.
 * Caller guarantees ast_is_simple_cmp_cond(n). */
void ast_gen_cmp_branch(const struct AstNode *n, int label,
                               int branch_when_true)
{
    int lhs_type;
    int rhs_type;
    int common_type;
    int ptr_cmp;
    int op = n->op;

    ptr_cmp = ast_operand_is_ptr_ident(n->a) || ast_operand_is_ptr_ident(n->b);
    ast_gen_expr(n->a);
    lhs_type = g_expr_type;
    emit("\tpush hl\n");
    ast_gen_expr(n->b);
    rhs_type = g_expr_type;
    common_type = common_arith_type(lhs_type, rhs_type);
    emit("\tex de,hl\n\tpop hl\n");
    if ((common_type & TYPE_UNSIGNED) || ptr_cmp) {
        if (branch_when_true)
            emit_cmp_branch_true_unsigned(op, label);
        else
            emit_cmp_branch_false_unsigned(op, label);
    } else {
        if (branch_when_true)
            emit_cmp_branch_true(op, label);
        else
            emit_cmp_branch_false(op, label);
    }
}

/* Emit `ident OP const` (gated by ast_is_const_cmp_cond) by calling the shared
 * emitter.  The internal label it allocates lands at the same relative point
 * because the AST if/while/do-while walkers allocate their loop labels up front
 * before emitting the condition. */
void ast_gen_const_cmp_branch(const struct AstNode *n, int label,
                                     int branch_when_true)
{
    struct Sym *s;
    int op;
    long c;
    ast_const_cmp_extract(n, &s, &op, &c);
    emit_cmp_const_branch_for_signed_local16(s, op, c, label, branch_when_true);
}

/* Emit a two-byte-operand relational comparison (gated by ast_is_byte_cmp_cond)
 * by building the ByteOperands and emitting the byte compare/branch sequence:
 * optional const/lvalue swap (inverting the relop), load LHS to A, `cp` RHS,
 * then the byte compare/branch. */
void ast_gen_byte_cmp_branch(const struct AstNode *n, int label,
                                    int branch_when_true)
{
    struct ByteOperand lhs;
    struct ByteOperand rhs;
    struct ByteOperand tmp;
    int op = n->op;

    ast_byte_operand(n->a, &lhs);
    ast_byte_operand(n->b, &rhs);
    if (!byte_operand_can_be_lhs(&lhs) && byte_operand_can_be_lhs(&rhs)) {
        op = invert_relop_for_swap(op);
        tmp = lhs;
        lhs = rhs;
        rhs = tmp;
    }
    emit_byte_operand_to_a(&lhs);
    emit_cp_byte_operand(&rhs);
    emit_byte_cmp_branch_after_cp(op, label, branch_when_true);
}

void ast_gen_direct_byte_bitand_branch(const struct AstNode *n, int label,
                                             int branch_when_true)
{
    struct Sym *s;
    long mask;

    s = find_sym(n->a->sval);
    mask = n->b->ival & 255;
    fprintf(outf, "\tld a,(ix%+d)\n", s->offset);
    fprintf(outf, "\tand %ld\n", mask);
    if (branch_when_true)
        emit_jp_label("jp nz,", label);
    else
        emit_jp_label("jp z,", label);
}

/* Emitter for ast_is_direct_wide_bitand_cond: identical shape to the byte
 * fast path above - the operand's low byte lives at its own frame offset
 * regardless of the symbol's full width (Z80 is little-endian), so no extra
 * work is needed to reach it. */
void ast_gen_direct_wide_bitand_branch(const struct AstNode *n, int label,
                                              int branch_when_true)
{
    struct Sym *s;
    long mask;

    s = find_sym(n->a->sval);
    mask = n->b->ival & 255;
    fprintf(outf, "\tld a,(ix%+d)\n", s->offset);
    fprintf(outf, "\tand %ld\n", mask);
    if (branch_when_true)
        emit_jp_label("jp nz,", label);
    else
        emit_jp_label("jp z,", label);
}

/* Emitter for ast_is_range_check_cond: evaluate `x` exactly once (instead of
 * once per original comparison - ast_gen_expr on a bare identifier always
 * arrives already promoted to int width, per gen_ident/emit_load_sym_value_
 * direct/emit_load_from_hl, so there is no separate promotion step to run
 * here), then reduce `LO <= x <= HI` to the standard single-unsigned-compare
 * range trick: bias x down by LO (a no-op when LO is 0) and check that the
 * unsigned result is no more than (HI-LO), i.e. strictly less than
 * (HI-LO)+1. This works regardless of x's or the bounds' signedness - it
 * operates on the raw 16-bit bit pattern the whole way through, the same
 * reason the trick is standard practice in hand-written C. */
void ast_gen_range_check_branch(const struct AstNode *n, int label,
                                       int branch_when_true)
{
    const struct AstNode *x;
    long lo;
    long hi;

    ast_is_range_check_cond(n, &x, &lo, &hi);

    ast_gen_expr(x);

    if ((lo & 0xffffL) != 0)
        fprintf(outf, "\tld de,%ld\n\tor a\n\tsbc hl,de\n", lo & 0xffffL);
    fprintf(outf, "\tld de,%ld\n\tor a\n\tsbc hl,de\n", (hi - lo + 1) & 0xffffL);
    emit_jp_label(branch_when_true ? "jp c," : "jp nc,", label);
}

/* Emitter for ast_is_direct_long_const_eq_cond: XOR each stored byte against
 * its matching constant byte (skipping a byte whose constant is 0 - xor 0 is
 * a no-op), OR-ing the running result in C so the whole thing collapses to a
 * single zero/nonzero test - zero iff all 4 bytes matched. */
void ast_gen_direct_long_const_eq_branch(const struct AstNode *n, int label,
                                                int branch_when_true)
{
    struct Sym *s;
    const struct AstNode *idn;
    const struct AstNode *cn;
    unsigned long uval;
    int kbyte[4];
    int branch_on_zero;
    int i;

    if (n->a->kind == AST_IDENT) {
        idn = n->a;
        cn = n->b;
    } else {
        idn = n->b;
        cn = n->a;
    }
    s = find_sym(idn->sval);
    uval = (unsigned long)cn->ival;
    kbyte[0] = (int)(uval & 0xff);
    kbyte[1] = (int)((uval >> 8) & 0xff);
    kbyte[2] = (int)((uval >> 16) & 0xff);
    kbyte[3] = (int)((uval >> 24) & 0xff);

    /* n->op is TOK_EQ or TOK_NE; branch_when_true says which way `label` is
     * taken. Equal <=> the XOR/OR chain is zero. */
    branch_on_zero = (n->op == TOK_EQ) ? branch_when_true : !branch_when_true;

    for (i = 0; i < 4; ++i) {
        fprintf(outf, "\tld a,(ix%+d)\n", s->offset + i);
        if (kbyte[i] != 0)
            fprintf(outf, "\txor %d\n", kbyte[i]);
        if (i > 0)
            emit("\tor c\n");
        if (i < 3)
            emit("\tld c,a\n");
    }
    if (branch_on_zero)
        emit_jp_label("jp z,", label);
    else
        emit_jp_label("jp nz,", label);
}

void ast_gen_float_cmp_branch(const struct AstNode *n, int label,
                                     int branch_when_true)
{
    ast_gen_expr(n->a);
    if (!type_is_float(g_expr_type))
        emit_convert_int_to_float(g_expr_type);
    emit("\tpush de\n\tpush hl\n");
    ast_gen_expr(n->b);
    if (!type_is_float(g_expr_type))
        emit_convert_int_to_float(g_expr_type);
    emit("\tpush de\n\tpush hl\n");
    emit_float_compare_call(n->op);
    emit_branch_on_bool_hl(label, branch_when_true);
}

void ast_gen_long_cmp_branch(const struct AstNode *n, int label,
                                    int branch_when_true)
{
    gen_long_cmp_ast(n);
    emit_branch_on_bool_hl(label, branch_when_true);
}

/* Emit the controlling expression of an if/while/do-while as a branch to
 * `label` taken when the condition is true (branch_when_true=1) or false (0).
 * A simple relational comparison uses the direct compare/branch; everything
 * else falls back to the generic value-test (gen_expr + emit_test_expr_nonzero). */
void ast_gen_cond_branch(const struct AstNode *n, int label,
                                int branch_when_true)
{
    long cv;
    if (ast_const_condition_fold(n, &cv)) {
        if ((cv != 0) == branch_when_true)
            emit_jp_label("jp", label);
        return;
    }
    if (n != NULL && n->kind == AST_LOGAND && ast_const_condition_fold(n->a, &cv)) {
        if (cv == 0) {
            if (!branch_when_true)
                emit_jp_label("jp", label);
        } else {
            ast_gen_cond_branch(n->b, label, branch_when_true);
        }
        return;
    }
    if (n != NULL && n->kind == AST_LOGOR && ast_const_condition_fold(n->a, &cv)) {
        if (cv != 0) {
            if (branch_when_true)
                emit_jp_label("jp", label);
        } else {
            ast_gen_cond_branch(n->b, label, branch_when_true);
        }
        return;
    }
    if (ast_is_range_check_cond(n, NULL, NULL, NULL)) {
        ast_gen_range_check_branch(n, label, branch_when_true);
        return;
    }
    if (ast_is_const_cmp_cond(n)) {
        ast_gen_const_cmp_branch(n, label, branch_when_true);
        return;
    }
    if (ast_is_byte_cmp_cond(n)) {
        ast_gen_byte_cmp_branch(n, label, branch_when_true);
        return;
    }
    if (ast_is_direct_byte_bitand_cond(n)) {
        ast_gen_direct_byte_bitand_branch(n, label, branch_when_true);
        return;
    }
    if (ast_is_direct_wide_bitand_cond(n)) {
        ast_gen_direct_wide_bitand_branch(n, label, branch_when_true);
        return;
    }
    if (ast_global_char_index_cond(n, NULL)) {
        ast_gen_global_char_index_branch(n, label, branch_when_true);
        return;
    }
    if (ast_is_float_cmp_cond(n)) {
        ast_gen_float_cmp_branch(n, label, branch_when_true);
        return;
    }
    if (ast_is_direct_long_const_eq_cond(n)) {
        ast_gen_direct_long_const_eq_branch(n, label, branch_when_true);
        return;
    }
    if (ast_long_cmp_supported(n)) {
        ast_gen_long_cmp_branch(n, label, branch_when_true);
        return;
    }
    if (ast_is_simple_cmp_cond(n)) {
        ast_gen_cmp_branch(n, label, branch_when_true);
        return;
    }
    if (ast_is_general_const_cmp_cond(n)) {
        ast_gen_cmp_branch(n, label, branch_when_true);
        return;
    }
    ast_gen_expr(n);
    emit_test_expr_nonzero(g_expr_type, label, branch_when_true);
}


int ast_switch_find_case(int value, int *vals, int ncase)
{
    int i;
    for (i = 0; i < ncase; ++i)
        if (vals[i] == value)
            return i;
    return -1;
}

int ast_switch_table_ok(int *case_vals, int ncase, int *minp, int *maxp)
{
    int i;
    int minv;
    int maxv;
    if (ncase < 3)
        return 0;
    minv = case_vals[0];
    maxv = case_vals[0];
    for (i = 1; i < ncase; ++i) {
        if (case_vals[i] < minv) minv = case_vals[i];
        if (case_vals[i] > maxv) maxv = case_vals[i];
    }
    if (minv < 0 || maxv > 32767)
        return 0;
    if ((maxv - minv) > 255)
        return 0;
    if ((maxv - minv + 1) > ncase * 2)
        return 0;
    minp[0] = minv;
    maxp[0] = maxv;
    return 1;
}
