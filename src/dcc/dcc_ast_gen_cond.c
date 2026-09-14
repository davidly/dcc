/**
 * @file dcc_ast_gen_cond.c
 * @brief Classifies statement and condition/comparison AST shapes.
 *
 * @par Role
 * Implements statement admission, return/expression checks, and comparison
 * and truth-condition classifiers consumed by statement gating and MIR
 * lowering. Holds no condition/branch code emitters.
 *
 * @par Key entry points
 * ast_stmt_supported(), ast_return_stmt_supported(), ast_expr_stmt_supported(),
 * and the ast_is_*_cond()/ast_cond_*() predicates.
 *
 * @par Boundary
 * dcc_ast_stmt_meta.c captures accepted production statements into MIR; this
 * module is classification-only and is not a body-codegen fallback.
 */
#include <string.h>
#include "dcc_ast_gen_internal.h"
#include "dcc_mir.h"


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
        /* `return f(args);` where f returns this same struct type: emitted
         * as a destination-passthrough call (gen_return_ast). */
        if (n->a->kind == AST_CALL &&
            ast_struct_return_call_assign_supported(rt, n->a))
            return 1;
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
        /* Returning to a byte type performs the same narrowing conversion as
         * assignment.  Do not require an identifier to already be byte-sized
         * or a literal to fit before conversion: `signed char f(int x) {
         * return x; }` and `return -1` are both ordinary C conversions. */
        return ast_gen_supported(n->a) &&
               (ast_value_is_plain_int(n->a) || ast_value_is_long_word(n->a) ||
                ast_value_is_float_word(n->a));
    }
    if ((rt & 15) != TYPE_INT || type_size(rt) != 2)
        return 0;

    if (n->a != NULL) {
        if (ast_value_is_long_word(n->a))
            return 1;
        if (ast_value_is_float_word(n->a))
            return 1;
        if (!ast_gen_supported(n->a) || !ast_value_is_plain_int(n->a))
            return 0;
    }
    return 1;
}

/* Emit `return [expr] ;`: evaluate the value into the ABI return registers
 * when present, then jump to the function's shared return label. */
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

/* Is `n` a relational comparison `a OP b` of two qualifying identifier
 * operands eligible for the plain-16-bit direct comparison path?
 * Equality and ordering ops only; '&' is not a relational op. */
int ast_is_simple_cmp_cond(const struct AstNode *n)
{
    if (n == NULL || n->kind != AST_BINARY || !is_cmp_op(n->op))
        return 0;
    return ast_cmp_operand_ok(n->a) && ast_cmp_operand_ok(n->b);
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
        if (s != NULL && sym_can_ix_direct(s) &&
            type_size(s->type) == 1 && (s->type & TYPE_UNSIGNED)) {
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
                    /* A byte-sized (unsigned) index is just as valid as a
                     * plain int one here - emit_byte_operand_to_a/
                     * emit_cp_byte_operand (dcc_cmp.c) branch on the index
                     * symbol's own size to zero-extend a byte or load both
                     * bytes of an int, either way producing the right 16-bit
                     * offset. Originally only the 2-byte case was handled;
                     * once a loop counter narrows to a byte (e.g. via
                     * try_narrow_for_counter), a `p[i]` comparison like
                     * tests/tbig.c's `b[i] != (char)((rec+i)&0xff)` no
                     * longer matched this fast path at all and fell all the
                     * way back to full long-arithmetic codegen - a real
                     * performance regression, not just a missed byte-sized
                     * optimization. */
                    if (idx != NULL && sym_can_ix_direct(idx) &&
                        (type_size(idx->type) == 2 ||
                         (type_size(idx->type) == 1 && (idx->type & TYPE_UNSIGNED)))) {
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
static int ast_cond_indexed_array_row_operand(const struct AstNode *n, int *out_count);

static int ast_cond_not_indexed_scalar(const struct AstNode *n)
{
    int elem_type;

    if (n == NULL || n->kind != AST_UNARY || n->op != '!' ||
        n->a == NULL || n->a->kind != AST_INDEX)
        return 0;
    if (ast_cond_indexed_array_row_operand(n->a, NULL))
        return 0;
    if (!ast_index_lvalue_elem_type(n->a, &elem_type))
        return 0;
    return !type_is_struct_object(elem_type);
}

static int ast_cond_indexed_array_row_operand(const struct AstNode *n, int *out_count)
{
    const struct AstNode *root;
    struct Sym *s;
    int count;

    if (n == NULL || n->kind != AST_INDEX)
        return 0;

    root = n;
    count = 0;
    while (root != NULL && root->kind == AST_INDEX) {
        if (root->b == NULL || !ast_index_subscript_supported(root->b))
            return 0;
        count++;
        root = root->a;
    }

    if (root == NULL || count <= 0)
        return 0;
    if (out_count != NULL)
        *out_count = count;
    if (root->kind == AST_IDENT) {
        s = find_sym(root->sval);
        if (s == NULL || s->is_const_value || s->storage == SC_FUNC)
            return 0;
        if (s->is_array)
            return s->dim_count > count;
        return type_ptr_depth(s->type) > 0 && s->dim_count + 1 > count;
    }
    if (root->kind == AST_MEMBER) {
        int cur_type;
        int sid;
        struct FieldDef *fd;

        if (!ast_member_base_type(root, &cur_type))
            return 0;
        sid = base_struct_id_from_type(cur_type);
        fd = find_field_def(sid, root->sval);
        return fd != NULL && fd->is_array && fd->bit_width <= 0 &&
               fd->dim_count > count;
    }
    return 0;
}

static int ast_cond_not_indexed_array_row(const struct AstNode *n)
{
    int count;

    return n != NULL && n->kind == AST_UNARY && n->op == '!' &&
           ast_cond_indexed_array_row_operand(n->a, &count) && count == 1;
}

int ast_cond_generic(const struct AstNode *n)
{
    long cv;
    if (n == NULL)
        return 0;
    if (n->kind == AST_COMMA)
        /* `a , b` as a controlling expression: `a` is evaluated for its side
         * effects (value discarded) and `b` is the condition tested.  Gate the
         * left operand as a dead-result expression (same rule the for-init /
         * increment / expression-statement paths use, so a pointer postfix or
         * `x += c` left operand that has no value-context lowering is still
         * accepted) and require the right operand to be a generic condition. */
        return (ast_is_local_self_add_stmt(n->a) || ast_dead_expr_supported(n->a)) &&
               ast_cond_generic(n->b);
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
    if (ast_cond_not_indexed_array_row(n))
        return 1;
    if (ast_cond_not_indexed_scalar(n))
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
         * is eligible for the general plain-16-bit compare path - the byte and
         * small-const-int relational fast paths decline for two size-2
         * non-const operands.  Other supported comparisons whose operands are
         * not direct fast-path shapes (for example struct-member comparisons)
         * fall back to the generic value-emit + nonzero-test classification. */
        if (is_cmp_op(n->op))
            return ast_is_simple_cmp_cond(n) || ast_gen_supported(n);
        if (n->op == '&')
            return !ast_is_direct_byte_bitand_cond(n);
        return 1;
    case AST_LOGAND:
    case AST_LOGOR:
        /* if (a && b) / while (a || b): condition fast paths all
         * decline for a top-level &&/|| (simple_direct_condition_until requires
         * no logical operator), so this falls to the generic
         * gen_expr + emit_test_expr_nonzero classification (the short-circuit
         * 0/1 value followed by a nonzero test).  The guard above already
         * required ast_gen_supported && plain-int && non-const. */
        return 1;
    case AST_COND:
        /* A top-level ?: controlling expression has no condition fast path;
         * supported plain-int conditionals reach the generic
         * gen_expr + nonzero-test classification. */
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
        return type_index_elem_size(t) > 0;
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

int ast_dead_expr_supported(const struct AstNode *e)
{
    int old_dead;
    int ok;
    if (e == NULL)
        return 0;
    if (e->kind == AST_COMMA)
        return (ast_is_local_self_add_stmt(e->a) || ast_dead_expr_supported(e->a)) &&
               (ast_is_local_self_add_stmt(e->b) || ast_dead_expr_supported(e->b));
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
        return e->a != NULL &&
               (ast_is_local_self_add_stmt(e->a) || ast_dead_expr_supported(e->a));
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
    /* The for-init clause is a full expression evaluated only for its side
     * effects - its value is discarded - exactly like the third (increment)
     * clause and an ordinary expression statement (C89 6.6.5 / C99-C11 6.8.5:
     * `for ( expression_opt ; expression_opt ; expression_opt )`).  Gate it
     * with the same dead-result rule those two paths use so every emittable
     * side-effecting form is accepted uniformly: plain and compound assignment
     * (`i = 0`, `i += 5`, `*p -= 1`, `a[k] |= m`), pre/post increment and
     * decrement (including pointer postfix, which has no value-context
     * lowering), comma expressions, function calls, the `x = a + b` local
     * self-add shape, and a discarded `(void)` cast.  The walker emits the
     * init through ast_gen_dead_expr, which mirrors this gate exactly, so
     * anything accepted here is emittable and anything genuinely unsupported
     * on the Z80 target is declined cleanly (the whole for statement falls
     * back to the DCC-E1002 diagnostic rather than miscompiling). */
    if (e == NULL)
        return 1;                         /* empty init clause */
    return ast_is_local_self_add_stmt(e) || ast_dead_expr_supported(e);
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
         * declaration arrives as an AST_DECL span; the metadata walker replays
         * it through declaration parsing and for-scope rename
         * machinery.  An expression init excludes the transform-prone constant
         * assignment shape and must have no recorded for-scope renames.
         *
         * This gate mirrors the metadata walker's pre-order for_seq numbering.
         * For a for-init declaration the loop variable's local slot does not
         * exist yet (codegen creates it only when the declaration is emitted),
         * so we replay the declaration with emission suppressed (scan_mode) to
         * materialise the slot and push its rename, gate the
         * condition/increment/body while they can resolve, then roll back the
         * local table and rename stack.  g_func_pass.for_seq is intentionally left at the
         * post-order cursor for sibling gates; the top-level AST statement
         * probe restores it before real emission. */
        if (n->d == NULL)
            return 0;
        if (g_func_pass.for_seq >= MAX_FOR_SCOPES)
            return 0;
        for_seq = g_func_pass.for_seq++;
        /* g_for_rename_count[] is indexed by for_seq, reused across
         * functions and across passes; nothing resets it on its own now
         * that dcc_func.c's old hand-written frame-sizing scanner (which
         * used to zero this slot for every for-loop it walked past) is
         * gone. Reset unconditionally before reading it, so a plain
         * expression init reliably sees "no renames" instead of whatever a
         * decl-init loop that previously owned this for_seq slot left
         * behind - matching the same reset in ast_plan_for_metadata. Not rolled
         * back afterward: this probe's own speculative work IS rolled back
         * below (nlocals/local_size/etc.), but the real metadata walk
         * that follows for this same for_seq always resets and
         * re-records its own count independently regardless, so leaving
         * this slot at whatever this probe computed cannot affect it. */
        g_for_rename_count[for_seq] = 0;
        rename_count = g_for_rename_count[for_seq];

        if (n->a != NULL && n->a->kind == AST_DECL) {
            int s_nlocals = g_frame.nlocals;
            int s_local_size = g_frame.local_size;
            int s_forren_n = g_func_pass.forren_n;
            int s_nulabels = nulabels;
            int s_static_seq = g_func_pass.static_local_seq;
            int s_scope_depth = g_func_pass.scope_depth;
            int s_has_call = current_function_has_call;
            int s_decl_seq = g_func_pass.for_decl_seq;
            int s_decl_index = g_func_pass.for_decl_rename_index;
            int s_decl_recording = g_func_pass.for_decl_recording;
            int s_decl_nonobject = g_for_decl_saw_nonobject;
            int decl_object_count;
            int decl_saw_nonobject;
            int s_scan_mode = scan_mode;

            ok = ast_for_decl_storage_supported(n->a);
            /* g_func_pass.for_decl_recording=1 (not 0): nothing pre-populates
            * g_for_rename_count[for_seq] any more (see ast_plan_for_metadata);
            * the hand-written frame-sizing scanner
             * that used to do that recording is gone), so this probe must
             * record its own fresh count from the just-reset slot rather
             * than validate against a stale/zero one. */
            scan_mode = 1;
            g_func_pass.for_decl_seq = for_seq;
            g_func_pass.for_decl_rename_index = 0;
            g_func_pass.for_decl_recording = 1;
            g_for_decl_saw_nonobject = 0;
            if (ok) {
                if (mir_is_active()) {
                    int checkpoint = mir_instruction_checkpoint();
                    ast_replay_decl_span(n->a);
                    mir_neutralize_since(checkpoint);
                } else
                    ast_scan_decl_span(n->a);
            }
            decl_object_count = g_func_pass.for_decl_rename_index;
            decl_saw_nonobject = g_for_decl_saw_nonobject;
            /* Declaration replay changes the symbols visible to the loop's
             * condition, increment and body. Discard support decisions that
             * may have been cached while the builder inspected those nodes
             * before the for-init local existed. */
            ast_support_cache_begin();
            g_func_pass.for_decl_seq = s_decl_seq;
            g_func_pass.for_decl_rename_index = s_decl_index;
            g_func_pass.for_decl_recording = s_decl_recording;
            g_for_decl_saw_nonobject = s_decl_nonobject;

            if (ok && (decl_object_count == 0 || decl_saw_nonobject)) {
                ok = 0;
            }
            if (ok && n->b != NULL && !ast_cond_generic(n->b)) {
                ok = 0;
            }
            if (ok && n->c != NULL &&
                !ast_is_local_self_add_stmt(n->c) && !ast_dead_expr_supported(n->c)) {
                ok = 0;
            }
            if (ok) {
                old_nflow = nflow;
                nflow++;
                ok = ast_stmt_supported(n->d);
                nflow = old_nflow;
            }
            scan_mode = s_scan_mode;

            g_frame.nlocals = s_nlocals;
            g_frame.local_size = s_local_size;
            g_func_pass.forren_n = s_forren_n;
            nulabels = s_nulabels;
            g_func_pass.static_local_seq = s_static_seq;
            g_func_pass.scope_depth = s_scope_depth;
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
        if (n->c != NULL &&
            !ast_is_local_self_add_stmt(n->c) && !ast_dead_expr_supported(n->c)) {
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
         * in scan mode to materialise its local slots and scope, gate the
         * remaining children while they resolve, then roll back every mutated
         * parser counter. */
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
            int s_nlocals = g_frame.nlocals;
            int s_local_size = g_frame.local_size;
            int s_scope_depth = g_func_pass.scope_depth;
            int s_forren_n = g_func_pass.forren_n;
            int s_nulabels = nulabels;
            int s_static_seq = g_func_pass.static_local_seq;
            int s_has_call = current_function_has_call;
            int s_scan_mode = scan_mode;

            scan_mode = 1;
            enter_scope();
            ok = 1;
            for (i = 0; i < n->list_len; ++i) {
                struct AstNode *c = n->list[i];
                if (c->kind == AST_DECL) {
                    if (mir_is_active()) {
                        int checkpoint = mir_instruction_checkpoint();
                        ast_replay_decl_span(c);
                        mir_neutralize_since(checkpoint);
                    } else
                        ast_scan_decl_span(c);
                } else if (!ast_stmt_supported(c)) {
                    ok = 0;
                    break;
                }
            }
            leave_scope();
            scan_mode = s_scan_mode;

            g_frame.nlocals = s_nlocals;
            g_frame.local_size = s_local_size;
            g_func_pass.scope_depth = s_scope_depth;
            g_func_pass.forren_n = s_forren_n;
            nulabels = s_nulabels;
            g_func_pass.static_local_seq = s_static_seq;
            current_function_has_call = s_has_call;
            return ok;
        }
    }
    default:
        return 0;
    }
}

