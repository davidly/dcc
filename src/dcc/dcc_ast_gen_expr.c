/*
 * dcc_ast_gen_expr.c - expression emitters (ast_gen_expr).
 *
 * Split from dcc_ast_gen.c; part of the AST codegen module.  Shared
 * prototypes live in dcc_ast_gen_internal.h.
 */
#include <string.h>
#include "dcc_ast_gen_internal.h"

static const struct AstNode *inline_substitution_body(struct Sym *fn);
static int ast_is_float_madd_rhs(const struct AstNode *rhs);
static int ast_const_int_operand_value(const struct AstNode *n, long *out);
static int ast_masked_byte_operand(const struct AstNode *n, const struct AstNode **out);
static int ast_long_byte_extract(const struct AstNode *n, const struct AstNode **out_base, int *out_byte);
static void emit_long_byte_from_reg(int byte_index);
static int emit_low_byte_expr_to_a(const struct AstNode *n);
static int emit_long_byte_shift_to_reg(const struct AstNode *n);
static int emit_long_oreq_byte_lane(struct Sym *s, const struct AstNode *rhs);
static void gen_compound_literal_ast(const struct AstNode *n);


/* Recognize byte-truncation idioms that are common in hand-written portable C:
 *     (unsigned char)(x & 0xff)
 *     (char)((long)x & 0xffL)
 * The target byte conversion already keeps only L, so the explicit mask is
 * redundant and expensive when the left side forced a long promotion.  Return
 * the expression whose low byte should be used. */
static int ast_masked_byte_operand(const struct AstNode *n, const struct AstNode **out)
{
    long cv;

    if (n == NULL)
        return 0;
    if (n->kind == AST_BINARY && n->op == '&' &&
        ast_const_int_operand_value(n->b, &cv) && ((unsigned long)cv & 0xffffffffUL) == 255UL) {
        *out = n->a;
        return 1;
    }
    return 0;
}

/* Recognize the four byte-extraction forms from a 32-bit value:
 *     long_expr & 0xff
 *     (long_expr >> 8)  & 0xff
 *     (long_expr >> 16) & 0xff
 *     (long_expr >> 24) & 0xff
 * The long value lives in DE:HL as bytes D:E:H:L from high to low, so after
 * evaluating the base we can select the requested byte directly instead of
 * running the generic long shift and long AND helpers. */
static int ast_long_byte_extract(const struct AstNode *n, const struct AstNode **out_base, int *out_byte)
{
    long mask;
    long shift;
    const struct AstNode *lhs;

    if (n == NULL || n->kind != AST_BINARY || n->op != '&')
        return 0;
    if (!ast_const_int_operand_value(n->b, &mask) || ((unsigned long)mask & 0xffffffffUL) != 255UL)
        return 0;

    lhs = n->a;
    if (lhs != NULL && lhs->kind == AST_BINARY && lhs->op == TOK_SHR &&
        ast_const_int_operand_value(lhs->b, &shift)) {
        if (shift != 8 && shift != 16 && shift != 24)
            return 0;
        *out_base = lhs->a;
        *out_byte = (int)(shift / 8);
        return 1;
    }

    *out_base = lhs;
    *out_byte = 0;
    return 1;
}

static void emit_long_byte_from_reg(int byte_index)
{
    switch (byte_index) {
    case 0:
        emit("\tld h,0\n");
        break;
    case 1:
        emit("\tld l,h\n\tld h,0\n");
        break;
    case 2:
        emit("\tld l,e\n\tld h,0\n");
        break;
    default:
        emit("\tld l,d\n\tld h,0\n");
        break;
    }
    emit("\tld de,0\n");
}


static void emit_long_one_byte_from_a(int byte_index)
{
    switch (byte_index) {
    case 0:
        emit("\tld l,a\n\tld h,0\n\tld de,0\n");
        break;
    case 1:
        emit("\tld h,a\n\tld l,0\n\tld de,0\n");
        break;
    case 2:
        emit("\tld e,a\n\tld d,0\n\tld hl,0\n");
        break;
    default:
        emit("\tld d,a\n\tld e,0\n\tld hl,0\n");
        break;
    }
}

/* Recognize `(byte_expr & 255L) << {0,8,16,24}` and materialize the
 * resulting long by loading just the byte and placing it in the target byte
 * lane of DE:HL.  This is the inverse of ast_long_byte_extract() and is the
 * hot get_stamp() pattern: widening, long AND, long shift, and long OR are
 * all unnecessary when only one byte contributes to the result. */
static int emit_long_byte_shift_to_reg(const struct AstNode *n)
{
    const struct AstNode *src;
    long sh;
    int byte_index;

    if (n == NULL)
        return 0;

    src = n;
    sh = 0;
    if (n->kind == AST_BINARY && n->op == TOK_SHL) {
        if (!ast_const_int_operand_value(n->b, &sh))
            return 0;
        if (sh != 0 && sh != 8 && sh != 16 && sh != 24)
            return 0;
        src = n->a;
    }

    if (!ast_masked_byte_operand(src, &src))
        return 0;

    if (!emit_low_byte_expr_to_a(src))
        return 0;
    byte_index = (int)(sh / 8);
    emit_long_one_byte_from_a(byte_index);
    return 1;
}

/* Emit the low byte of a small expression directly into A without widening to
 * int/long.  This is intentionally conservative and targets hot portable-C
 * idioms such as `(char)((rec + i) & 0xff)`: the low byte of an addition only
 * depends on the low bytes of its operands, so an IX-direct load/add sequence
 * is enough.  Declines if evaluating the expression would need calls, memory
 * addressing, or anything that might clobber HL (the lvalue address in the
 * store fast path). */
static int emit_low_byte_expr_to_a(const struct AstNode *n)
{
    const struct AstNode *src;
    struct Sym *s;

    if (n == NULL)
        return 0;
    if (n->kind == AST_CAST) {
        /* A cast (narrowing OR widening) between integer types never changes
         * the low byte's bit pattern, so it's safe to look through it in
         * either direction - e.g. `(long)b[0]` widens a char, but the low
         * byte of the result is exactly b[0]'s bits. Casts touching a float
         * on either side are real numeric conversions, not a bit
         * reinterpretation, so those must not be unwrapped. */
        if (n->a == NULL || type_is_float(n->type) || type_is_float(n->a->type))
            return 0;
        return emit_low_byte_expr_to_a(n->a);
    }
    if (ast_masked_byte_operand(n, &src))
        return emit_low_byte_expr_to_a(src);
    {
        const struct AstNode *byte_base;
        int byte_index;
        if (ast_long_byte_extract(n, &byte_base, &byte_index) &&
            byte_base != NULL && byte_base->kind == AST_IDENT) {
            s = find_sym(byte_base->sval);
            if (s != NULL && sym_can_ix_direct(s) && type_is_long(s->type)) {
                fprintf(outf, "\tld a,(ix%+d)\n", s->offset + byte_index);
                return 1;
            }
        }
    }
    /* A bare `long_ident >> K` (K a multiple of 8): the caller already
     * stripped an enclosing `& 0xff` via ast_masked_byte_operand() above, so
     * ast_long_byte_extract()'s own `& 0xff`-wrapped shape can never match
     * here even though this is exactly the byte it's looking for - e.g.
     * fill_record()'s `(rec >> 24) & 0xff` arrives as a bare `rec >> 24` by
     * the time it reaches this point. */
    if (n->kind == AST_BINARY && n->op == TOK_SHR && n->a != NULL &&
        n->a->kind == AST_IDENT) {
        long shift;
        if (ast_const_int_operand_value(n->b, &shift) &&
            (shift == 0 || shift == 8 || shift == 16 || shift == 24)) {
            s = find_sym(n->a->sval);
            if (s != NULL && sym_can_ix_direct(s) && type_is_long(s->type)) {
                fprintf(outf, "\tld a,(ix%+d)\n", s->offset + (int)(shift / 8));
                return 1;
            }
        }
    }
    if (n->kind == AST_IDENT) {
        s = find_sym(n->sval);
        if (s == NULL || !sym_can_ix_direct(s) || type_size(s->type) > 4)
            return 0;
        fprintf(outf, "\tld a,(ix%+d)\n", s->offset);
        return 1;
    }
    if (n->kind == AST_INT_LIT) {
        fprintf(outf, "\tld a,%ld\n", n->ival & 255L);
        return 1;
    }
    if (n->kind == AST_INDEX && n->a != NULL && n->a->kind == AST_IDENT &&
        n->b != NULL) {
        int base;
        struct Sym *ps = find_sym(n->a->sval);
        if (ps != NULL && sym_can_ix_direct(ps)) {
            base = type_decay_ptr(ps->type);
            if (type_size(base) == 1) {
                fprintf(outf, "\tld l,(ix%+d)\n", ps->offset);
                fprintf(outf, "\tld h,(ix%+d)\n", ps->offset + 1);
                if (n->b->kind == AST_IDENT) {
                    struct Sym *idx = find_sym(n->b->sval);
                    if (idx == NULL || !sym_can_ix_direct(idx) || type_size(idx->type) != 2)
                        return 0;
                    fprintf(outf, "\tld e,(ix%+d)\n", idx->offset);
                    fprintf(outf, "\tld d,(ix%+d)\n", idx->offset + 1);
                    emit("\tadd hl,de\n");
                } else if (n->b->kind == AST_INT_LIT) {
                    if (n->b->ival != 0) {
                        fprintf(outf, "\tld de,%ld\n", n->b->ival & 0xffffL);
                        emit("\tadd hl,de\n");
                    }
                } else {
                    return 0;
                }
                emit("\tld a,(hl)\n");
                return 1;
            }
        }
    }
    if (n->kind == AST_BINARY && (n->op == '+' || n->op == '-')) {
        if (!emit_low_byte_expr_to_a(n->a))
            return 0;
        if (n->b != NULL && n->b->kind == AST_IDENT) {
            s = find_sym(n->b->sval);
            if (s == NULL || !sym_can_ix_direct(s) || type_size(s->type) > 4)
                return 0;
            fprintf(outf, n->op == '+' ? "\tadd a,(ix%+d)\n" : "\tsub (ix%+d)\n", s->offset);
            return 1;
        }
        if (n->b != NULL && n->b->kind == AST_INT_LIT) {
            fprintf(outf, n->op == '+' ? "\tadd a,%ld\n" : "\tsub %ld\n", n->b->ival & 255L);
            return 1;
        }
    }
    return 0;
}

/* Recognize `v |= (byte_expr & 0xff) << K` (K in {0,8,16,24}; K may be
 * absent, meaning 0) when v is an ix-direct long local. The right-hand side
 * only ever has one nonzero byte lane, so OR-ing it into v can touch just
 * that one byte of v's stack storage - load the byte, `or (ix+d)`,
 * `ld (ix+d),a` - instead of the generic path: reload all 4 bytes of v,
 * evaluate the rhs through the widen/mask/shift chain into DE:HL, run a
 * full 32-bit OR, and store all 4 bytes back. This is exactly the
 * `v |= (byte) << K;` shape get_stamp()-style byte-assembly idioms use. */
static int emit_long_oreq_byte_lane(struct Sym *s, const struct AstNode *rhs)
{
    const struct AstNode *src;
    long sh;
    int byte_index;

    if (rhs == NULL)
        return 0;

    src = rhs;
    sh = 0;
    if (rhs->kind == AST_BINARY && rhs->op == TOK_SHL) {
        if (!ast_const_int_operand_value(rhs->b, &sh))
            return 0;
        if (sh != 0 && sh != 8 && sh != 16 && sh != 24)
            return 0;
        src = rhs->a;
    }

    if (!ast_masked_byte_operand(src, &src))
        return 0;

    if (!emit_low_byte_expr_to_a(src))
        return 0;

    byte_index = (int)(sh / 8);
    fprintf(outf, "\tor (ix%+d)\n", s->offset + byte_index);
    fprintf(outf, "\tld (ix%+d),a\n", s->offset + byte_index);
    return 1;
}

void gen_int_lit(const struct AstNode *n)
{
    if (n->type & TYPE_LONG) {
        /* 32-bit literal: low half in HL, high half in DE. */
        fprintf(outf, "\tld hl,%ld\n", n->ival & 0xffffL);
        fprintf(outf, "\tld de,%ld\n", (n->ival >> 16) & 0xffffL);
    } else {
        fprintf(outf, "\tld hl,%ld\n", n->ival & 0xffffL);
    }
    g_expr_type = n->type;
    g_long_from16 = 0;
}

/* Cast `(type)expr` to a 16-bit integer target (float/long/pointer targets
 * excluded by the gate): evaluate the operand, then drop a long high word,
 * sign/zero-extend a byte, or no-op. */
void gen_cast_ast(const struct AstNode *n)
{
    int t = n->type;
    int from16 = 0;
    const struct AstNode *byte_src;

    if (type_size(t) == 1 && ast_masked_byte_operand(n->a, &byte_src)) {
        ast_gen_expr(byte_src);
        if (t & TYPE_UNSIGNED)
            emit("\tld h,0\n");
        else
            emit("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n");
        g_expr_type = t;
        g_long_from16 = 0;
        return;
    }

    ast_gen_expr(n->a);
    if (type_is_float(t)) {
        if (!type_is_float(g_expr_type))
            emit_convert_int_to_float(g_expr_type);
        g_expr_type = t;
        g_long_from16 = 0;
        return;
    }
    if (type_is_long(t)) {
        if (type_is_float(g_expr_type))
            emit_convert_float_to_intlike(t);
        else if (!type_is_long(g_expr_type)) {
            emit_extend_to_long_typed(g_expr_type);
            from16 = g_long_from16;
        }
        g_expr_type = t;
        g_long_from16 = from16;
        return;
    }
    if (type_is_bool(t)) {
        if (!ast_expr_yields_bool01(n->a))
            emit_bool_normalize_hl(g_expr_type);
        g_expr_type = t;
        g_long_from16 = 0;
        return;
    }
    if (type_is_float(g_expr_type)) {
        emit_convert_float_to_intlike(t);
        g_expr_type = t;
        g_long_from16 = 0;
        return;
    }
    if (type_size(t) == 1) {
        if (t & TYPE_UNSIGNED)
            emit("\tld h,0\n");
        else
            emit("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n");
    }
    g_expr_type = t;
    g_long_from16 = 0;
}

void gen_str_lit(const struct AstNode *n)
{
    /* Intern at emit time (the build deferred this codegen side effect); the
     * 1:1 substitution at gen_expr preserves source order, so the assigned
     * string id remains stable. */
    int sid;
    sid = add_string_ex(n->sval, (int)n->uval, (int)n->ival);
    fprintf(outf, "\tld hl,S%d\n", sid);
    g_expr_type = TYPE_CHAR | TYPE_PTR;
}

void gen_ident(const struct AstNode *n)
{
    const char *name = n->sval;
    struct Sym *s;

    /* Mirror gen_primary's prologue resets so the load-address tail observes
     * the same global state (notably current_field_bit_width == 0, which gates
     * the trailing bitfield extract). */
    current_field_bit_width = 0;
    current_field_bit_shift = 0;
    current_field_bit_mask = 0;
    g_array_decay_stride = 0;
    g_expr_no_deref = 0;

    /* stdin/stdout/stderr -> immediate FILE values 0/1/2, checked before
     * symbol resolution. */
    if (!strcmp(name, "stdin")) {
        emit("\tld hl,0\n");
        g_expr_type = TYPE_INT;
        return;
    }
    if (!strcmp(name, "stdout")) {
        emit("\tld hl,1\n");
        g_expr_type = TYPE_INT;
        return;
    }
    if (!strcmp(name, "stderr")) {
        emit("\tld hl,2\n");
        g_expr_type = TYPE_INT;
        return;
    }

    s = find_sym(name);
    if (s == NULL) {
        int ei;
        for (ei = 0; ei < nenum_consts; ++ei) {
            if (!strcmp(enum_const_names[ei], name)) {
                long ev = (long)(int)enum_const_values[ei];
                fprintf(outf, "\tld hl,%ld\n", ev & 0xffffL);
                g_expr_type = TYPE_INT;
                return;
            }
        }
        {
            char msg[MAX_TOK_TEXT + 64];
            sprintf(msg, "use of undeclared identifier '%s'", name);
            dcc_error_at(tok.file[0] ? tok.file : (input_name ? input_name : "<input>"),
                         n->line > 0 ? n->line : tok_line, -1, msg, NULL);
        }
        emit("\tld hl,0\n");
        g_expr_type = TYPE_INT;
        return;
    }

    /* Folded local const scalar -> immediate (helper sets g_expr_type). */
    if (s->is_const_value) {
        emit_load_const_sym_value(s);
        return;
    }

    /* A function name used as a value decays to its address - a real
     * reference regardless of whether it's also inline-eligible, so any
     * static function's buffered body must be kept. A function defined in
     * a different translation unit (any DCCRTL.MAC/library function, or an
     * extern-only prototype) needs an EXTRN for that symbol here, exactly
     * like the general call path already does before its own `call`
     * (dcc_ast_gen_expr.c's emit_extrn_if_needed(fn_sym) right before
     * "call %s") and like a global initializer's function-pointer value
     * already does (dcc_data.c's mark_init_label_extrn). This local-value
     * path was the one case that never called it - harmless for a
     * same-file user function (its own `public` label needs no EXTRN),
     * but a real link failure ("U ... ld hl,__mcmp") for a LOCAL function
     * pointer initialized from any RTL function, found via a stricmp()
     * test that happened to be the first one in this whole codebase to
     * exercise a local (not global) RTL function-pointer variable. */
    if (s->storage == SC_FUNC) {
        if (s->is_static)
            s->deferred_body_needed = 1;
        emit_extrn_if_needed(s);
        fprintf(outf, "\tld hl,%s\n", asm_name_for(sym_asm_name(s)));
        g_expr_type = type_add_ptr(s->type);
        return;
    }

    /* Local scalar reachable with a direct ix-relative load, or resident in
     * a register instead of the frame entirely (reg_alloc != REG_NONE) - the
     * latter never satisfies sym_can_ix_direct (which unconditionally
     * declines any register-resident symbol so every OTHER ix-direct fast
     * path safely falls back instead of reading a stale frame slot), so it
     * needs its own explicit check here rather than folding into that one. */
    if (sym_can_ix_direct(s) || s->reg_alloc != REG_NONE) {
        emit_load_sym_value_direct(s);
        g_expr_type = s->type;
        return;
    }

    /* 16-bit global/extern: ld hl,(nn) direct load. */
    if (is_global_word_sym(s)) {
        emit_load_global_word_direct(s);
        g_expr_type = s->type;
        return;
    }

    /* General case: load the symbol's address, then either decay an array to a
     * pointer-to-element or load the scalar value. */
    emit_load_sym_addr(s);
    if (s->is_array) {
        g_expr_type = type_add_ptr(s->type);
        if (s->dim_count > 1)
            g_array_decay_stride = sym_array_index_elem_size(s, 0);
    } else {
        g_expr_type = s->type;
        emit_load_from_hl(s->type);
    }
}

void gen_unary_ast(const struct AstNode *n)
{
    int op = n->op;
    long fv;
    unsigned long ffv;

    /* A unary +/- chain over a float literal or folded `const float` local
     * (e.g. `-PI`) folds to one immediate with the sign bit already flipped,
     * instead of loading the constant and then flipping its sign at runtime
     * with `ld a,d / xor 80h / ld d,a` on every execution. */
    if ((op == '-' || op == '+') &&
        ast_unary_float_const_fold(n, &ffv)) {
        emit_load_float_bits(ffv);
        g_expr_type = TYPE_FLOAT;
        return;
    }

    /* A unary chain over a single int literal folds to one immediate. */
    if ((op == '-' || op == '+' || op == '~') &&
        ast_unary_long_const_fold(n, &fv)) {
        fprintf(outf, "\tld hl,%ld\n", fv & 0xffffL);
        fprintf(outf, "\tld de,%ld\n", (fv >> 16) & 0xffffL);
        g_expr_type = type_is_long(n->type) ? n->type : n->a->type;
        g_long_from16 = 0;
        return;
    }
    if ((op == '-' || op == '+' || op == '~') &&
        ast_unary_int_const_fold(n, &fv)) {
        fprintf(outf, "\tld hl,%ld\n", fv & 0xffffL);
        g_expr_type = TYPE_INT;
        return;
    }

    /* `!<constant-int-expr>` (including chains like `!!0`) folds to a single
     * 0/1 immediate; ast_const_scalar_fold already yields the final value. */
    if (op == '!' && ast_const_scalar_fold(n, &fv)) {
        fprintf(outf, "\tld hl,%ld\n", fv & 0xffffL);
        g_expr_type = TYPE_INT;
        return;
    }

    /* gen_unary clears the "freshly widened from 16-bit" marker on entry; the
     * long negate/complement paths below re-clear it after producing a value
     * that is no longer a faithful widening. */
    g_long_from16 = 0;

    if (op == '!') {
        /* Labels are allocated BEFORE the operand in gen_unary; preserve that
         * order so the global label counter (and emitted label numbers) match. */
        int lt = new_label();
        int le = new_label();
        ast_gen_expr(n->a);
        emit_test_expr_nonzero(g_expr_type, lt, 0);
        emit("\tld hl,0\n");
        emit_jp_label("jp", le);
        emit_label(lt);
        emit("\tld hl,1\n");
        emit_label(le);
        g_expr_type = TYPE_INT;
        return;
    }

    if (op == '&') {
        /* gen_lvalue_addr for a bare identifier: reset the field-bitfield
         * state, load the symbol's address, and yield pointer-to-element. */
        struct Sym *s;
        int val_type;
        current_field_bit_width = 0;
        current_field_bit_shift = 0;
        current_field_bit_mask = 0;
        if (n->a->kind == AST_INDEX) {
            gen_index_addr_ast(n->a, &val_type);
            g_expr_type = type_add_ptr(val_type);
            return;
        }
        if (n->a->kind == AST_MEMBER) {
            gen_member_addr_ast(n->a, &val_type);
            g_expr_type = type_add_ptr(val_type);
            return;
        }
        if (n->a->kind == AST_UNARY && n->a->op == '*') {
            /* &*X collapses to the pointer value X. */
            gen_deref_addr_ast(n->a, &val_type);
            g_expr_type = type_add_ptr(val_type);
            return;
        }
        if (n->a->kind == AST_COMPOUND_LITERAL) {
            gen_compound_literal_ast(n->a);
            return;
        }
        s = find_sym(n->a->sval);
        emit_load_sym_addr(s);
        g_expr_type = type_add_ptr(s->type);
        return;
    }

    if (op == '*') {
        /* try_gen_simple_deref_value fast path for `*bare_ident`: load the
         * pointer value (global word direct, else address + deref), then load
         * the pointed-to plain-int element. */
        struct Sym *s;
        int ptr_type;
        int no_deref;
        int base;
        if (ast_va_arg_deref_type(n, &base)) {
            gen_va_arg_deref_ast(n, base);
            return;
        }
        if (ast_deref_pointer_array_chain_collect(n, NULL, NULL, NULL, NULL, &base)) {
            gen_deref_addr_ast(n, &base);
            emit_load_from_hl(base);
            g_expr_type = base;
            g_long_from16 = 0;
            return;
        }
        if (n->a->kind != AST_IDENT) {
            gen_pointer_expr_ast(n->a, &ptr_type, &no_deref);
            base = no_deref ? ptr_type : type_decay_ptr(ptr_type);
            if ((base & 15) == TYPE_VOID)
                base = TYPE_CHAR;
            if (!type_is_struct_object(base))
                emit_load_from_hl(base);
            g_expr_type = base;
            g_long_from16 = 0;
            return;
        }
        s = find_sym(n->a->sval);
        if (s->is_array) {
            emit_load_sym_addr(s);
        } else if (is_global_word_sym(s)) {
            emit_load_global_word_direct(s);
        } else {
            emit_load_sym_addr(s);
            emit_load_from_hl(s->type);
        }
        base = type_decay_ptr(s->type);
        if ((base & 15) == TYPE_VOID)
            base = TYPE_CHAR;
        emit_load_from_hl(base);
        g_expr_type = base;
        return;
    }

    if (op == TOK_INC || op == TOK_DEC) {
        /* gen_lvalue_addr + emit_pre_incdec_lvalue: load the object's address,
         * then in-place increment/decrement, leaving the updated value in HL
         * with g_expr_type = the object type. */
        struct Sym *s;
        int val_type;
        current_field_bit_width = 0;
        current_field_bit_shift = 0;
        current_field_bit_mask = 0;
        if (n->a->kind == AST_MEMBER) {
            gen_member_addr_ast(n->a, &val_type);
            emit_pre_incdec_lvalue(val_type, op);
            return;
        }
        if (n->a->kind == AST_INDEX) {
            gen_index_addr_ast(n->a, &val_type);
            emit_pre_incdec_lvalue(val_type, op);
            return;
        }
        if (n->a->kind == AST_UNARY && n->a->op == '*') {
            gen_deref_addr_ast(n->a, &val_type);
            emit_pre_incdec_lvalue(val_type, op);
            return;
        }
        s = find_sym(n->a->sval);
        emit_load_sym_addr(s);
        emit_pre_incdec_lvalue(s->type, op);
        return;
    }

    ast_gen_expr(n->a);

    if (op == '+') {
        if (!type_is_float(g_expr_type) && !type_is_long(g_expr_type))
            g_expr_type = promote_int_type(g_expr_type);
        return;
    }

    if (op == '-') {
        if (type_is_float(g_expr_type)) {
            emit("\tld a,d\n\txor 80h\n\tld d,a\n");
        } else if (type_is_long(g_expr_type)) {
            int lneg_skip = new_label();
            emit("\tld a,l\n\tcpl\n\tld l,a\n");
            emit("\tld a,h\n\tcpl\n\tld h,a\n");
            emit("\tld a,e\n\tcpl\n\tld e,a\n");
            emit("\tld a,d\n\tcpl\n\tld d,a\n");
            emit("\tinc hl\n");
            emit("\tld a,h\n\tor l\n");
            emit_jp_label("jp nz,", lneg_skip);
            emit("\tinc de\n");
            emit_label(lneg_skip);
            g_long_from16 = 0;
        } else {
            emit("\txor a\n\tsub l\n\tld l,a\n\tld a,0\n\tsbc a,h\n\tld h,a\n");
            g_expr_type = promote_int_type(g_expr_type);
        }
        return;
    }

    /* op == '~' */
    if (type_is_long(g_expr_type)) {
        emit("\tld a,h\n\tcpl\n\tld h,a\n\tld a,l\n\tcpl\n\tld l,a\n");
        emit("\tld a,d\n\tcpl\n\tld d,a\n\tld a,e\n\tcpl\n\tld e,a\n");
        g_long_from16 = 0;
    } else {
        emit("\tld a,h\n\tcpl\n\tld h,a\n\tld a,l\n\tcpl\n\tld l,a\n");
        g_expr_type = promote_int_type(g_expr_type);
    }
}

static void gen_compound_literal_ast(const struct AstNode *n)
{
    struct AstCompoundLitSpan *sp = (struct AstCompoundLitSpan *)n->aux;
    long sv_posi = posi;
    long sv_tok_start = tok_start_pos;
    int sv_line = line_no;
    int sv_tok_line = tok_line;
    struct Token sv_tok = tok;
    /* Capture the fields we still need after the initializer is emitted.
     * `n` itself lives in g_ast_init_arena, and emitting a non-constant field
     * (e.g. .p = &(T){...}) re-enters ast_emit_init_expr, which builds into and
     * then resets that same arena - overwriting this node with the last nested
     * initializer. Reading n->sym/n->type afterwards would then yield the wrong
     * (last nested) compound literal, so snapshot them now. The Sym pointer
     * itself targets the stable locals[] table, so it stays valid. */
    struct Sym *clit_sym = n->sym;
    int clit_type = n->type;

    posi = sp->posi;
    tok_start_pos = sp->tok_start_pos;
    line_no = sp->line_no;
    tok_line = sp->tok_line;
    tok = sp->tok;

    if ((clit_type & TYPE_STRUCT) && type_ptr_depth(clit_type) == 0) {
        emit_init_auto_struct_from_list(clit_sym);
    } else if (accept('{')) {
        emit_init_auto_struct_scalar(clit_sym, 0, clit_type);
        if (tok.kind == ',')
            next_token();
        expect('}');
    } else {
        emit_init_auto_struct_scalar(clit_sym, 0, clit_type);
    }

    posi = sv_posi;
    tok_start_pos = sv_tok_start;
    line_no = sv_line;
    tok_line = sv_tok_line;
    tok = sv_tok;

    emit_load_sym_addr(clit_sym);
    g_expr_type = type_add_ptr(clit_type);
}

static void gen_compound_literal_value_ast(const struct AstNode *n)
{
    int clit_type = n->type;

    gen_compound_literal_ast(n);
    if (!type_is_struct_object(clit_type)) {
        emit_load_from_hl(clit_type);
        g_expr_type = clit_type;
        g_long_from16 = 0;
    }
}

void gen_pointer_cmp_operand_ast(const struct AstNode *n)
{
    int ptr_type;
    int no_deref;

    if (ast_pointer_expr_type(n, &ptr_type, &no_deref)) {
        gen_pointer_expr_ast(n, &ptr_type, &no_deref);
    } else {
        ast_gen_expr(n);
    }
}

static void gen_sizeof_expr_ast(const struct AstNode *n)
{
    long val;

    if (n->kind == AST_SIZEOF_EXPR) {
        /* Resolve the operand at EMIT time.  A local declared in a nested
         * block only enters the symbol table when its declaration span is
         * emitted, which happens before this node is walked but after the AST
         * was built - so a build-time size would resolve the wrong (or no)
         * symbol.  A whole VLA loads its stored run-time byte size; every
         * other operand is a compile-time constant. */
        struct Sym *vsym = ast_sizeof_whole_vla_sym(n->a);
        if (vsym != NULL && vsym->vla_size_offset != 0) {
            emit("\tpush ix\n\tpop hl\n");
            fprintf(outf, "\tld de,%d\n\tadd hl,de\n", vsym->vla_size_offset);
            emit("\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n");
            g_expr_type = TYPE_INT;
            return;
        }
        val = ast_sizeof_expr_value(n->a);
    } else {
        val = n->ival;
    }
    fprintf(outf, "\tld hl,%ld\n", val & 0xffffL);
    g_expr_type = TYPE_INT;
}

void gen_pointer_cmp_ast(const struct AstNode *n)
{
    gen_pointer_cmp_operand_ast(n->a);
    emit("\tpush hl\n");
    gen_pointer_cmp_operand_ast(n->b);
    emit("\tex de,hl\n\tpop hl\n");
    gen_binop_typed(n->op, TYPE_INT | TYPE_UNSIGNED);
    g_expr_type = TYPE_INT;
    g_long_from16 = 0;
}

void gen_pointer_diff_ast(const struct AstNode *n)
{
    int lhs_type;
    int no_deref;
    int elem = 1;

    gen_pointer_cmp_operand_ast(n->a);
    emit("\tpush hl\n");
    gen_pointer_cmp_operand_ast(n->b);
    emit("\tex de,hl\n\tpop hl\n");
    gen_binop('-');
    if (ast_pointer_expr_type(n->a, &lhs_type, &no_deref))
        elem = type_index_elem_size(lhs_type);
    divide_hl_by_elem_size(elem);
    g_expr_type = TYPE_INT;
    g_long_from16 = 0;
}

static int emit_signed_long_const_cmp_ast(int op, long c)
{
    unsigned long threshold;
    unsigned long biased;
    unsigned int lo;
    unsigned int hi;
    int true_label;
    int end_label;
    int true_on_carry;

    threshold = ((unsigned long)c) & 0xffffffffUL;
    if (op == '>' || op == TOK_LE) {
        if (threshold == 0x7fffffffUL) {
            emit(op == '>' ? "\tld hl,0\n" : "\tld hl,1\n");
            return 1;
        }
        threshold = (threshold + 1UL) & 0xffffffffUL;
    }

    biased = (threshold ^ 0x80000000UL) & 0xffffffffUL;
    lo = (unsigned int)(biased & 0xffffUL);
    hi = (unsigned int)((biased >> 16) & 0xffffUL);
    true_on_carry = (op == '<' || op == TOK_LE);
    true_label = new_label();
    end_label = new_label();

    emit("\tld a,d\n\txor 80h\n\tld d,a\n");
    fprintf(outf, "\tld bc,%u\n", lo);
    emit("\tor a\n\tsbc hl,bc\n\tex de,hl\n");
    fprintf(outf, "\tld bc,%u\n", hi);
    emit("\tsbc hl,bc\n");
    emit_jp_label(true_on_carry ? "jp c," : "jp nc,", true_label);
    emit("\tld hl,0\n");
    emit_jp_label("jp", end_label);
    emit_label(true_label);
    emit("\tld hl,1\n");
    emit_label(end_label);
    return 1;
}


void gen_long_cmp_ast(const struct AstNode *n)
{
    int lhs_type;
    int rhs_type;
    int common_type;
    long rhs_const;

    ast_gen_expr(n->a);
    lhs_type = promote_int_type(g_expr_type);
    if (!type_is_long(lhs_type)) {
        /* 16-bit LHS against a signed long compile-time constant (e.g.
         * `int x < 40000L`, where the decimal literal is `long` because it
         * exceeds INT_MAX). Promote the LHS to long in DE:HL and use the
         * inline signed-long-const compare, exactly as the long-LHS path
         * below does. Without this the operand falls into
         * gen_binop32_promote_16lhs_ast, which pushes both operands and calls
         * the __lts/__les runtime helper -- ~40 instructions and a call for
         * what the old stream compiler did inline. */
        common_type = common_arith_type(lhs_type, promote_int_type(n->b->type));
        if ((n->op == '<' || n->op == '>' || n->op == TOK_LE || n->op == TOK_GE) &&
            type_is_long(common_type) && !(common_type & TYPE_UNSIGNED) &&
            type_is_long(n->b->type) && ast_const_scalar_fold(n->b, &rhs_const)) {
            emit_cast_16_to_common(lhs_type, common_type);   /* HL -> DE:HL */
            if (emit_signed_long_const_cmp_ast(n->op, rhs_const)) {
                g_expr_type = TYPE_INT;
                g_long_from16 = 0;
                return;
            }
        }
        emit("\tpush hl\n");
        ast_gen_expr(n->b);
        rhs_type = promote_int_type(g_expr_type);
        common_type = common_arith_type(lhs_type, rhs_type);
        if (type_is_long(rhs_type)) {
            gen_binop32_promote_16lhs_ast(n->op, lhs_type, common_type);
        } else {
            emit("\tex de,hl\n\tpop hl\n");
            gen_binop_typed(n->op, common_type);
        }
        g_expr_type = TYPE_INT;
        g_long_from16 = 0;
        return;
    }

    common_type = common_arith_type(lhs_type, n->b->type);
    if ((n->op == '<' || n->op == '>' || n->op == TOK_LE || n->op == TOK_GE) &&
        !(common_type & TYPE_UNSIGNED) && ast_const_scalar_fold(n->b, &rhs_const) &&
        emit_signed_long_const_cmp_ast(n->op, rhs_const)) {
        g_expr_type = TYPE_INT;
        g_long_from16 = 0;
        return;
    }

    emit("\tpush de\n\tpush hl\n");
    ast_gen_expr(n->b);
    rhs_type = promote_int_type(g_expr_type);
    common_type = common_arith_type(lhs_type, rhs_type);
    emit_cast_16_to_common(rhs_type, common_type);
    gen_binop32_typed(n->op, common_type);
    g_expr_type = TYPE_INT;
    g_long_from16 = 0;
}

/* True if n, once any explicit widening-to-long cast around it is stripped
 * away, is a plain unsigned value no wider than 16 bits - i.e. widening it
 * to long (whether via that cast or via the ordinary usual-arithmetic-
 * conversion promotion applied when it sits next to an already-long sibling
 * operand) is known, from its own declared type alone, to produce a high
 * word of exactly 0. Used by gen_long_arith_ast's '%' case to detect the
 * "((wide)a * (wide)b) % c" idiom - the standard C89 way to multiply two
 * 16-bit values without overflow before reducing - where a, b, and c all
 * satisfy this. That pattern can skip straight to __m1mu (multiply and
 * reduce mod c together, 16 bits at a time) instead of building the full
 * 32-bit product via __m1u and then dividing it by c via __lmu. */
static int ast_is_plain_u16_source(const struct AstNode *n)
{
    int t;

    if (n == NULL)
        return 0;
    if (n->kind == AST_CAST && type_is_long(n->type) && !type_is_float(n->type))
        n = n->a;
    if (n == NULL)
        return 0;
    t = promote_int_type(ast_expr_type_for_sizeof(n));
    return !type_is_long(t) && !type_is_float(t) && !type_ptr_depth(t) &&
           (t & TYPE_UNSIGNED) != 0;
}

/* Extract the compile-time value of `n` when it is a bare integer literal or
 * a cast directly wrapping one (e.g. `(long)128`, which parses as a CAST
 * node over an INT_LIT rather than folding into a single long-typed
 * literal). Declines (returns 0) for anything else: a multi-level cast or a
 * cast of a non-literal might not be safe to fold without evaluating. */
static int ast_const_int_operand_value(const struct AstNode *n, long *out)
{
    if (n == NULL)
        return 0;
    if (n->kind == AST_INT_LIT) {
        *out = n->ival;
        return 1;
    }
    if (n->kind == AST_CAST && n->a != NULL && n->a->kind == AST_INT_LIT) {
        *out = n->a->ival;
        return 1;
    }
    return 0;
}

void gen_long_arith_ast(const struct AstNode *n)
{
    int lhs_type;
    int common_type;
    int lhs_from16;
    int rhs_from16;
    long const_val;

    /* Whole-expression constant fold for a long result (companion to the
     * 16-bit fold in gen_binary_ast). The AST builder never folds, so a source
     * constant such as `100000L + 200000L` would otherwise materialise both
     * 32-bit operands and call a runtime long add/mul. When the whole node is a
     * compile-time constant and the operator's low 32 bits are the same under
     * signed and unsigned interpretation (+, -, *, &, |, ^), emit the folded
     * long immediate (low half in HL, high half in DE) directly. Relational /
     * divide / modulo / shift keep their signedness-aware paths. */
    if (!type_is_float(n->a->type) && !type_is_float(n->b->type) &&
        ast_const_fold_strict(n, &const_val)) {
        common_type = common_arith_type(
            promote_int_type(ast_expr_type_for_sizeof(n->a)), n->peek_type);
        fprintf(outf, "\tld hl,%ld\n", const_val & 0xffffL);
        fprintf(outf, "\tld de,%ld\n", (const_val >> 16) & 0xffffL);
        g_expr_type = common_type;
        g_long_from16 = 0;
        return;
    }

    if ((n->op == TOK_SHL || n->op == '|') && emit_long_byte_shift_to_reg(n)) {
        g_expr_type = TYPE_LONG | TYPE_UNSIGNED;
        g_long_from16 = 0;
        return;
    }

    if (n->op == '&') {
        const struct AstNode *byte_base;
        int byte_index;
        if (ast_long_byte_extract(n, &byte_base, &byte_index)) {
            if (byte_index == 0 && byte_base != NULL && byte_base->kind == AST_CAST &&
                type_is_long(byte_base->type) && ast_value_is_plain_int(byte_base->a)) {
                ast_gen_expr(byte_base->a);
                emit("\tld h,0\n\tld de,0\n");
            } else {
                ast_gen_expr(byte_base);
                if (!type_is_long(g_expr_type))
                    emit_extend_to_long_typed(g_expr_type);
                emit_long_byte_from_reg(byte_index);
            }
            g_expr_type = TYPE_LONG | TYPE_UNSIGNED;
            g_long_from16 = 0;
            return;
        }
    }

    /* "((wide)a * (wide)b) % c" with a, b, c all plain unsigned <=16-bit
     * values (the standard C89 overflow-safe-multiply-then-reduce idiom,
     * e.g. modular exponentiation's `((uint32_t)r * (uint32_t)b) % m`):
     * skip evaluating the '*' as its own 32-bit subexpression (which would
     * call __m1u for the multiply and then __lmu for the mod - profiled at
     * ~59% of total runtime combined in one real workload) and instead
     * evaluate the three underlying 16-bit values directly, left to right
     * (preserving this compiler's normal evaluation order for the
     * expressions such a pattern could contain, e.g. calls with side
     * effects), and call __m1mu once. That single routine reduces the
     * running product mod c after every doubling step, so it never needs
     * more than a carry-extended 16-bit register - see __m1mu in
     * DCCRTL.MAC for the algorithm. Its result is always < c <= 65535, so
     * it comes back in HL alone; zero-extend to DE:HL to match this node's
     * long-typed result. */
    if (n->op == '%' && n->a != NULL && n->a->kind == AST_BINARY && n->a->op == '*' &&
        ast_is_plain_u16_source(n->a->a) && ast_is_plain_u16_source(n->a->b) &&
        ast_is_plain_u16_source(n->b)) {
        ast_gen_expr(n->a->a);
        emit("\tpush hl\n");
        ast_gen_expr(n->a->b);
        emit("\tpush hl\n");
        ast_gen_expr(n->b);
        emit("\tpush hl\n");
        emit("\tpop bc\n");             /* BC = c (modulus) */
        emit("\tpop de\n");             /* DE = b */
        emit("\tpop hl\n");             /* HL = a */
        emit_runtime_call("__m1mu");
        emit("\tld de,0\n");
        g_expr_type = ast_expr_type_for_sizeof(n);
        g_long_from16 = 0;
        return;
    }

    ast_gen_expr(n->a);
    lhs_type = promote_int_type(g_expr_type);
    common_type = common_arith_type(lhs_type, n->peek_type);
    emit_cast_16_to_common(lhs_type, common_type);

    /* `long_expr & <compile-time constant>`: the rhs is known at compile
     * time, so there is nothing to evaluate and nothing that needs pushing
     * through the stack. Any byte of the mask that is all-ones leaves the
     * matching byte of the lhs untouched, any byte that is all-zero clears
     * it outright, and only a genuinely mixed byte needs a real `and`. The
     * generic path materializes the mask constant, pushes the lhs, pops it
     * back, and ANDs four bytes through `ex de,hl` shuffling regardless of
     * the mask's shape; skip all of that. This also covers the common
     * byte-extraction idiom used to pull bytes out of a long
     * (`(v >> 24) & 0xff`) and to zero-extend a narrower value into one
     * (`(long)c & 0xffL`), where the mask is exactly a byte/word boundary. */
    if (n->op == '&' && ast_const_int_operand_value(n->b, &const_val)) {
        emit_and_long_const((unsigned long)const_val);
        g_expr_type = common_type;
        g_long_from16 = 0;
        return;
    }

    /* `long_expr * <compile-time power-of-two constant>`: strength-reduce to
     * a shift instead of a call to __lmul. Byte-aligned shift counts reuse
     * the same cheap register-move sequences as `<<`; any leftover sub-byte
     * bits are a handful of unrolled `add hl,hl`/`rl e`/`rl d` steps, still
     * far cheaper than the generic runtime multiply. Declines (returns 0)
     * for non-power-of-two constants, 0, and 1, which fall through to the
     * generic path below (0 and 1 are rare enough as literal long
     * multipliers not to be worth special-casing separately). */
    if (n->op == '*' && ast_const_int_operand_value(n->b, &const_val) &&
        emit_mul_pow2_long_const(const_val)) {
        g_expr_type = common_type;
        g_long_from16 = 0;
        return;
    }

    lhs_from16 = g_long_from16;
    emit("\tpush de\n\tpush hl\n");
    ast_gen_expr(n->b);
    emit_cast_16_to_common(g_expr_type, common_type);
    rhs_from16 = g_long_from16;
    if (n->op == '*' && lhs_from16 != 0 && lhs_from16 == rhs_from16) {
        emit("\tpop bc\n\tpop de\n");
        emit_runtime_call(lhs_from16 == 2 ? "__m1u" : "__m1s");
        g_expr_type = common_type;
        g_long_from16 = 0;
        return;
    }
    gen_binop32_typed(n->op, common_type);
    g_expr_type = common_type;
    g_long_from16 = 0;
}

void gen_binop32_promote_16lhs_ast(int op, int lhs_type, int common_type)
{
    /* Entry: the 16-bit LHS is on the stack (a single `push hl`) and the long
     * RHS is in DE:HL -- the shape produced when gen_binary_ast pushes a
     * 16-bit LHS and only afterwards discovers the RHS is long. The uniform
     * 32-bit ops expect the LHS as a long on the stack and the RHS in DE:HL.
     *
     * For commutative operators (+, *, &, |, ^) swap the operand roles: pop
     * the 16-bit LHS, spill the long RHS to the stack, extend the LHS into
     * DE:HL, and run the standard 32-bit op. gen_binop32 then computes
     * (stack RHS) op (DE:HL LHS) == LHS op RHS by commutativity, and pops its
     * own 4 bytes -- no deep stack round-trip. */
    if (op == '+' || op == '*' || op == '&' || op == '|' || op == '^') {
        emit("\tpop bc\n");                 /* BC = 16-bit LHS (B hi, C lo) */
        emit("\tpush de\n\tpush hl\n");     /* long RHS -> stack */
        emit("\tld l,c\n\tld h,b\n");       /* HL = LHS low word */
        emit_cast_16_to_common(lhs_type, common_type); /* DE:HL = (long)LHS */
        gen_binop32_typed(op, common_type);
        g_long_from16 = 0;
        return;
    }

    /* Non-commutative (-, /, %): operand order matters, so keep the
     * order-preserving reconstruction (rebuild LHS as a long beneath the RHS). */
    emit("\tpop bc\n");
    emit("\tpush de\n\tpush hl\n");
    emit("\tld h,b\n\tld l,c\n");
    emit_cast_16_to_common(lhs_type, common_type);
    emit("\tpush de\n\tpush hl\n");
    emit("\tld hl,4\n\tadd hl,sp\n");
    emit("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n");
    emit("\tinc hl\n");
    emit("\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n");
    emit("\tex de,hl\n");
    gen_binop32_typed(op, common_type);
    emit("\tpop bc\n\tpop bc\n");
    g_long_from16 = 0;
}

/* Emit a plain-int binary operator with the uniform 16-bit sequence: evaluate
 * lhs into HL, promote, capture the common type from the rhs's stored peek,
 * then push / eval rhs / ex de,hl / pop hl / dispatch. Result type is int for
 * comparisons, common type otherwise. */
void gen_binary_ast(const struct AstNode *n)
{
    int lhs_type;
    int common_type;
    const char *float_helper;
    long const_val;

    /* Whole-expression constant fold. The AST builder never folds, so without
     * this a source expression like `1000 + 2000` or `(30 * 10) / 3` evaluates
     * BOTH operands and does the arithmetic at run time. If the whole binary
     * node folds to a value that is identical under signed and unsigned
     * interpretation (ast_const_fold_strict enforces this, rejecting only the
     * genuinely signedness-ambiguous divide/modulo/shift/relational cases with
     * a negative operand), materialise the folded immediate directly. Long and
     * float results fold on their own paths. */
    if (!type_is_long(n->a->type) && !type_is_float(n->a->type) &&
        !type_is_long(n->b->type) && !type_is_float(n->b->type) &&
        !type_is_long(n->peek_type) && !type_is_float(n->peek_type) &&
        !type_is_long(ast_expr_type_for_sizeof(n)) &&
        ast_const_fold_strict(n, &const_val)) {
        int fold_type = is_cmp_op(n->op)
            ? TYPE_INT
            : common_arith_type(promote_int_type(n->a->type), n->peek_type);
        fprintf(outf, "\tld hl,%ld\n", const_val & 0xffffL);
        g_expr_type = fold_type;
        g_long_from16 = 0;
        return;
    }

    /* `CONST * expr` mirror of the `expr * CONST` fast path further below:
     * multiplication is commutative, so a qualifying constant on the LEFT
     * gets the same emit_mul_hl_const treatment (no push/pop, no __mulu/__muls
     * call) by evaluating the non-constant operand into HL first. Checked with
     * static type predicates only (nothing evaluated yet), so it is safe to
     * decide before the generic lhs-first evaluation below runs. Declines when
     * the RHS is itself a literal (constant folding elsewhere already handles
     * that) so this never overlaps the existing const-on-the-right check. */
    if (n->op == '*' && n->a->kind == AST_INT_LIT && n->b->kind != AST_INT_LIT &&
        !type_is_long(n->a->type) && !type_is_float(n->a->type) &&
        ast_mul_const_value_ok(n->a->ival) && ast_value_is_plain_int(n->b) &&
        !ast_value_is_long_word(n->b) && !ast_value_is_float_word(n->b)) {
        int rhs_type;
        ast_gen_expr(n->b);
        rhs_type = promote_int_type(g_expr_type);
        emit_mul_hl_const(n->a->ival & 0xffffL);
        g_expr_type = common_arith_type(rhs_type, n->a->type);
        g_long_from16 = 0;
        return;
    }

    if (n->op == '+' || n->op == '-') {
        int ptr_type;
        int no_deref;
        if (ast_pointer_expr_type(n, &ptr_type, &no_deref)) {
            gen_pointer_expr_ast(n, &ptr_type, &no_deref);
            return;
        }
    }

    if (ast_pointer_cmp_supported(n)) {
        gen_pointer_cmp_ast(n);
        return;
    }

    if (ast_pointer_diff_supported(n)) {
        gen_pointer_diff_ast(n);
        return;
    }

    if (ast_long_cmp_supported(n)) {
        gen_long_cmp_ast(n);
        return;
    }

    if (ast_long_arith_supported(n)) {
        gen_long_arith_ast(n);
        return;
    }

    /* `ident & <const < 256>` (e.g. tchess.c's `return sq & 7;`): such a
     * mask's result can never depend on the identifier's high byte,
     * regardless of its value or sign, so load only the low byte instead
     * of the normal full load (which the '&' fast path further below would
     * then immediately discard half of via emit_and_hl_const). Checked
     * before the generic ast_gen_expr(n->a) below runs, since that's
     * exactly the full load this skips. Declined (falls through to the
     * generic path) for register-resident/array/long/float/pointer
     * symbols, where sym_word_load_is_two_byte_fetch's callers already
     * have a cheap or separate path.
     *
     * Excludes the mask 0 and 255 boundary cases: emit_and_word_const
     * already turns a byte mask of 0x00 into a single immediate load and
     * a byte mask of 0xFF into skipping the AND entirely (it's a no-op on
     * a byte), so the generic path's full load followed by that already-
     * cheap masking beats this fast path's load-into-A/and/move-to-L
     * sequence for those two values specifically - found via a real
     * performance regression in tests/trw.c's `x & 0xFF` once measured,
     * not anticipated up front. */
    if (n->op == '&' && n->a->kind == AST_IDENT &&
        ast_const_int_operand_value(n->b, &const_val) &&
        const_val > 0 && const_val < 255) {
        struct Sym *land_sym = find_sym(n->a->sval);
        /* A volatile object's full width must be accessed; loading only its
         * low byte here would drop the high-byte read the abstract machine
         * requires, so decline and let the generic full-word path run. */
        if (land_sym != NULL && !land_sym->is_volatile &&
            sym_word_load_is_two_byte_fetch(land_sym)) {
            emit_load_sym_low_byte_and_const(land_sym, (unsigned int)const_val);
            g_expr_type = TYPE_INT;
            g_long_from16 = 0;
            return;
        }
    }

    ast_gen_expr(n->a);
    lhs_type = promote_int_type(g_expr_type);

    if (is_cmp_op(n->op) &&
        (type_is_float(g_expr_type) || ast_value_is_float_word(n->b))) {
        if (!type_is_float(g_expr_type))
            emit_convert_int_to_float(g_expr_type);
        emit("\tpush de\n\tpush hl\n");
        ast_gen_expr(n->b);
        if (!type_is_float(g_expr_type))
            emit_convert_int_to_float(g_expr_type);
        /* n->b is still live in DE:HL right here - emit_float_compare_call
         * now emits the fastcall comparison variants, which take it that
         * way instead of via a second push/pop round trip. */
        emit_float_compare_call(n->op);
        g_expr_type = TYPE_INT;
        g_long_from16 = 0;
        return;
    }

    if (is_float_arith_op(n->op) &&
        (type_is_float(g_expr_type) || ast_value_is_float_word(n->b))) {
        if (!type_is_float(g_expr_type))
            emit_convert_int_to_float(g_expr_type);

        /* `addend + b*c` (the shape every Horner-scheme polynomial evaluates,
         * e.g. sinf/cosf/atanf's minimax approximations) otherwise evaluates
         * the multiply into DE:HL, pushes it, and calls __fadd separately -
         * two runtime calls and two push/pop round trips through the IEEE
         * packing where one __fmaf(addend, b, c) call does both. n->a (the
         * addend) is already evaluated above; mirrors emit_float_compound_rhs's
         * '+='-only fusion but for the general binary '+' case. __fmaf is
         * the fastcall entry point (b arrives live in DE:HL - see its header
         * in DCCRTL.MAC): it just jumps into __fmadd's own body once
         * unpacked, so linking it in costs only its small unpack prologue,
         * not a second copy of the multiply/normalize logic.
         *
         * Trade-off (measured via perf_results.csv A/B across all 220 tests,
         * against the original 3-push __fmadd, before the fastcall variant
         * existed): a program that newly starts using this fusion pays a
         * one-time link-in cost for the RTL routine (a deliberate full
         * duplicate of __fmul's body, not shared code), but every affected
         * program's cycle count improves (-0.2% to -1.3%); none regressed. */
        if (n->op == '+' && ast_is_float_madd_rhs(n->b)) {
            emit("\tpush de\n\tpush hl\n");
            ast_gen_expr(n->b->a);
            if (!type_is_float(g_expr_type))
                emit_convert_int_to_float(g_expr_type);
            emit("\tpush de\n\tpush hl\n");
            ast_gen_expr(n->b->b);
            if (!type_is_float(g_expr_type))
                emit_convert_int_to_float(g_expr_type);
            /* n->b->b (the second multiplicand) is still live in DE:HL
             * right here - __fmaf takes it that way instead of via a
             * third push/pop round trip, matching __faf/__fsf/__fmf's
             * fastcall convention. */
            emit_runtime_call("__fmaf");
            emit("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n");
            g_expr_type = TYPE_FLOAT;
            g_long_from16 = 0;
            return;
        }

        emit("\tpush de\n\tpush hl\n");
        ast_gen_expr(n->b);
        if (!type_is_float(g_expr_type))
            emit_convert_int_to_float(g_expr_type);
        /* n->b is still live in DE:HL right here - the fastcall variants
         * (__faf/__fsf/__fmf/__fdf) take it that way instead of via a
         * second push/pop round trip, exactly like __fmaf above. These
         * already exist and are already used by DCCRTL.MAC's own
         * internal call sites; this is the same technique applied to
         * ordinary generated '+'/'-'/'*'/'/' float arithmetic, which
         * until now still paid for both operands' stack traffic. */
        float_helper = (n->op == '+') ? "__faf" :
                       (n->op == '-') ? "__fsf" :
                       (n->op == '*') ? "__fmf" : "__fdf";
        emit_runtime_call(float_helper);
        emit("\tpop bc\n\tpop bc\n");
        g_expr_type = TYPE_FLOAT;
        g_long_from16 = 0;
        return;
    }

    common_type = common_arith_type(lhs_type, n->peek_type);

    /* `lhs * <const>` fast path via emit_mul_hl_const (lhs in HL, single
     * const-mul, no push/pop or __mulu). */
    if (n->op == '*' && n->b->kind == AST_INT_LIT &&
        !type_is_long(common_type) && ast_mul_const_value_ok(n->b->ival)) {
        emit_mul_hl_const(n->b->ival & 0xffffL);
        g_expr_type = common_type;
        g_long_from16 = 0;
        return;
    }

    /* unsigned `lhs / pow2` -> logical shift; `lhs % pow2` -> mask (lhs already
     * in HL, no __divu/__remu call). */
    if ((n->op == '/' || n->op == '%') && n->b->kind == AST_INT_LIT &&
        (common_type & TYPE_UNSIGNED) && !type_is_long(common_type)) {
        int sh = int_log2_pow2((int)(n->b->ival & 0xffffL));
        if (sh >= 0) {
            if (n->op == '/')
                emit_logical_shift_right_hl_const(sh);
            else
                emit_and_hl_const((unsigned int)((n->b->ival & 0xffffL) - 1));
            g_expr_type = common_type;
            g_long_from16 = 0;
            return;
        }
    }

    /* `lhs & <compile-time constant>` (plain int, e.g. `sq & 7`): lhs is
     * already in HL and the rhs needs no evaluation, so skip the generic
     * push-lhs/evaluate-rhs/ex de,hl/pop/and sequence entirely and mask HL
     * in place with emit_and_hl_const's byte-wise immediate logic (mirrors
     * the long `& <const>` fast path in gen_long_arith_ast). Tiny one-liner
     * helpers like `sq & 7` get called an enormous number of times in
     * search-heavy code, so trimming the push/pop off each call matters. */
    if (n->op == '&' && ast_const_int_operand_value(n->b, &const_val) &&
        !type_is_long(common_type)) {
        emit_and_hl_const((unsigned int)(const_val & 0xffffL));
        g_expr_type = common_type;
        g_long_from16 = 0;
        return;
    }

    /* Constant RHS on the generic 16-bit path: the right operand is known at
     * compile time, so load it straight into DE rather than running the
     * uniform push-lhs / evaluate-rhs-into-HL / ex de,hl / pop-hl marshaling.
     * The lhs is already in HL and DE is scratch, so the end state
     * (HL = lhs, DE = rhs) is byte-for-byte identical to the generic sequence
     * while dropping a push, a pop and an ex de,hl per operator. The &, *,
     * unsigned /,% constant cases returned above; this covers the remaining
     * +, -, |, ^, signed /,%, and comparison operators against a literal --
     * i.e. the pervasive loop-condition / accumulator shapes like `i < N`,
     * `n - 1`, `x | mask`. */
    if (!type_is_long(common_type) &&
        ast_const_int_operand_value(n->b, &const_val) &&
        !type_is_long(n->b->type) && !type_is_float(n->b->type)) {
        fprintf(outf, "\tld de,%ld\n", const_val & 0xffffL);
        gen_binop_typed(n->op, common_type);
        if (is_cmp_op(n->op))
            g_expr_type = TYPE_INT;
        else
            g_expr_type = common_type;
        g_long_from16 = 0;
        return;
    }

    emit("\tpush hl\n");
    ast_gen_expr(n->b);
    if (type_is_long(g_expr_type)) {
        common_type = common_arith_type(lhs_type, g_expr_type);
        gen_binop32_promote_16lhs_ast(n->op, lhs_type, common_type);
        g_expr_type = common_type;
        g_long_from16 = 0;
        return;
    }
    emit("\tex de,hl\n\tpop hl\n");
    gen_binop_typed(n->op, common_type);

    if (is_cmp_op(n->op))
        g_expr_type = TYPE_INT;
    else
        g_expr_type = common_type;
    g_long_from16 = 0;
}

/* Emit a plain-int shift with the non-literal shape:
 * evaluate lhs into HL, promote it (C89 integer promotion of the left operand;
 * the right operand does not participate in the usual conversions), push it,
 * evaluate rhs, move its low byte into B, restore HL, then run the shift loop.
 * Result type is the promoted left operand. */
void gen_shift_ast(const struct AstNode *n)
{
    int lhs_type;

    /* `(byte_expr & 0xff) << K` (K a multiple of 8): materializing the whole
     * long via emit_long_byte_shift_to_reg() is a handful of instructions
     * (load the byte, drop it in the right DE:HL lane, zero the rest) versus
     * evaluating the lhs generically first (widen/sign-extend to a full
     * long) and only then applying the constant-shift fast path below. Must
     * run before ast_gen_expr(n->a) - that call is the expensive path this
     * is trying to avoid. */
    if (n->op == TOK_SHL && emit_long_byte_shift_to_reg(n)) {
        g_expr_type = TYPE_LONG | TYPE_UNSIGNED;
        g_long_from16 = 0;
        return;
    }

    ast_gen_expr(n->a);
    lhs_type = promote_int_type(g_expr_type);
    if (type_is_long(lhs_type)) {
        if (n->b->kind == AST_INT_LIT && ast_value_is_plain_int(n->b)) {
            if (!emit_shift_const_long(n->op, lhs_type, n->b->ival)) {
                fprintf(outf, "\tld b,%ld\n", n->b->ival & 255L);
                emit_shift_loop(n->op, lhs_type);
            }
        } else {
            emit("\tpush de\n\tpush hl\n");
            ast_gen_expr(n->b);
            emit("\tld b,l\n\tpop hl\n\tpop de\n");
            emit_shift_loop(n->op, lhs_type);
        }
        g_expr_type = lhs_type;
        g_long_from16 = 0;
        return;
    }
    /* Compile-time shift count on a plain int: unroll directly instead of
     * the runtime b-counted loop below. Mirrors the long case's
     * emit_shift_const_long fast path just above: `<<` is a handful of
     * `add hl,hl`, and `>>` reuses the existing arithmetic/logical
     * constant-count shifters already used elsewhere for pointer scaling.
     * Restricted to 0..15 (a 16-bit int's full width) so out-of-range counts
     * keep falling through to the generic runtime loop unchanged. */
    if (n->b->kind == AST_INT_LIT && ast_value_is_plain_int(n->b) &&
        n->b->ival >= 0 && n->b->ival < 16) {
        int count = (int)n->b->ival;
        if (n->op == TOK_SHL || n->op == TOK_SHLEQ) {
            while (count-- > 0)
                emit("\tadd hl,hl\n");
        } else if (lhs_type & TYPE_UNSIGNED) {
            emit_logical_shift_right_hl_const(count);
        } else {
            emit_arith_shift_right_hl_const(count);
        }
        g_expr_type = lhs_type;
        g_long_from16 = 0;
        return;
    }

    emit("\tpush hl\n");
    ast_gen_expr(n->b);
    emit("\tld b,l\n\tpop hl\n");
    emit_shift_loop(n->op, lhs_type);
    g_expr_type = lhs_type;
    g_long_from16 = 0;
}

void gen_index_subscript_expr_ast(const struct AstNode *n)
{
    int saved_dead = expr_result_dead;
    expr_result_dead = 0;
    if (ast_index_subscript_binary_literal(n)) {
        int lhs_type;
        int common_type;
        ast_gen_expr(n->a);
        lhs_type = promote_int_type(g_expr_type);
        common_type = common_arith_type(lhs_type, n->b->type);
        emit("\tpush hl\n");
        gen_int_lit(n->b);
        emit("\tex de,hl\n\tpop hl\n");
        gen_binop_typed(n->op, common_type);
        g_expr_type = common_type;
        g_long_from16 = 0;
        expr_result_dead = saved_dead;
        return;
    }
    ast_gen_expr(n);
    expr_result_dead = saved_dead;
}

/* Pure-AST emission of a declaration initializer's assignment-expression.
 * The declaration codegen has already positioned the target address (or expects
 * the value in HL for a direct store) and adapts the result using g_expr_type
 * afterwards, so this only has to emit the initializer expression's
 * natural-typed value into the ABI result registers.  The type dispatch mirrors
 * an assignment's RHS: pointer-valued expressions go through
 * gen_pointer_expr_ast, everything else (plain int, long, float, casts,
 * pointer-returning calls) through ast_gen_expr.  Fatal on any unsupported
 * construct - this compiler is a pure-AST build. */
void ast_emit_init_expr(void)
{
    struct AstNode *n;
    int ptr_type;
    int no_deref;
    long sv_pos;
    long sv_tok_start;
    int sv_line;
    int sv_tok_line;
    struct Token sv_tok;
    long end_pos;
    long end_tok_start;
    int end_line;
    int end_tok_line;
    struct Token end_tok;

    sv_pos = posi;
    sv_tok_start = tok_start_pos;
    sv_line = line_no;
    sv_tok_line = tok_line;
    sv_tok = tok;

    n = ast_build_assign_expr(&g_ast_init_arena);

    end_pos = posi;
    end_tok_start = tok_start_pos;
    end_line = line_no;
    end_tok_line = tok_line;
    end_tok = tok;

    if (n != NULL && ast_pointer_expr_type(n, &ptr_type, &no_deref)) {
        gen_pointer_expr_ast(n, &ptr_type, &no_deref);
        ast_arena_reset(&g_ast_init_arena);
        return;
    }

    if (n != NULL && (ast_gen_supported(n) || n->kind == AST_CAST ||
                      ast_numeric_value_supported(n) ||
                      ast_pointer_assign_rhs_supported(n) ||
                      (n->kind == AST_CALL && ast_value_is_pointer_word(n) &&
                       ast_call_named_args_supported(n)))) {
        ast_gen_expr(n);
        ast_arena_reset(&g_ast_init_arena);
        return;
    }

    if (getenv("DCC_AST_REPORT") != NULL) {
        if (n == NULL)
            fprintf(stderr, "; AST-unsupported init build token=%d text='%s' line=%d\n",
                    tok.kind, tok.text, tok_line);
        else
            fprintf(stderr, "; AST-unsupported init gate kind=%s line=%d\n",
                    ast_kind_name(n->kind), tok_line);
    }
    ast_arena_reset(&g_ast_init_arena);

    tok = sv_tok;
    tok_line = sv_tok_line;
    line_no = sv_line;
    posi = sv_pos;
    tok_start_pos = sv_tok_start;
    error_here(n == NULL ? "malformed initializer expression" : "unsupported initializer expression");

    if (n != NULL) {
        posi = end_pos;
        tok_start_pos = end_tok_start;
        line_no = end_line;
        tok_line = end_tok_line;
        tok = end_tok;
    } else {
        while (tok.kind != TOK_EOF && tok.kind != ',' && tok.kind != ';' && tok.kind != '}')
            next_token();
    }

    g_expr_type = TYPE_INT;
    emit("\tld hl,0\n");
}

void ast_emit_struct_init_expr_assign(struct Sym *s)
{
    struct AstNode *rhs;
    struct AstNode *lhs;
    long sv_pos;
    long sv_tok_start;
    int sv_line;
    int sv_tok_line;
    struct Token sv_tok;
    long end_pos;
    long end_tok_start;
    int end_line;
    int end_tok_line;
    struct Token end_tok;

    sv_pos = posi;
    sv_tok_start = tok_start_pos;
    sv_line = line_no;
    sv_tok_line = tok_line;
    sv_tok = tok;

    rhs = ast_build_assign_expr(&g_ast_init_arena);

    end_pos = posi;
    end_tok_start = tok_start_pos;
    end_line = line_no;
    end_tok_line = tok_line;
    end_tok = tok;

    lhs = ast_new(&g_ast_init_arena, AST_IDENT);
    lhs->sval = ast_arena_strdup(&g_ast_init_arena, s->name);
    lhs->sym = s;
    lhs->type = s->type;

    if (rhs != NULL && ast_struct_return_call_assign_supported(s->type, rhs)) {
        gen_struct_return_call_assign_ast(lhs, rhs);
        ast_arena_reset(&g_ast_init_arena);
        return;
    }

    /* Struct initializer from any struct-address expression - most notably a
     * compound literal `struct T t = (struct T){ ... };` - is a whole-struct
     * copy from the source object into the new local. */
    if (rhs != NULL) {
        int rhs_type;
        if (ast_struct_addr_expr_supported(rhs, &rhs_type) &&
            same_struct_type(s->type, rhs_type)) {
            emit_load_sym_addr(s);                    /* HL = destination */
            emit("\tpush hl\n");
            gen_struct_addr_expr_ast(rhs, &rhs_type); /* HL = source       */
            emit("\tex de,hl\n\tpop hl\n");           /* DE = source, HL = dest */
            emit_copy_de_to_hl_bytes(type_size(s->type));
            g_expr_type = s->type;
            g_long_from16 = 0;
            ast_arena_reset(&g_ast_init_arena);
            return;
        }
    }

    if (getenv("DCC_AST_REPORT") != NULL) {
        if (rhs == NULL)
            fprintf(stderr, "; AST-unsupported struct init build token=%d text='%s' line=%d\n",
                    tok.kind, tok.text, tok_line);
        else
            fprintf(stderr, "; AST-unsupported struct init gate kind=%s line=%d\n",
                    ast_kind_name(rhs->kind), tok_line);
    }
    ast_arena_reset(&g_ast_init_arena);

    tok = sv_tok;
    tok_line = sv_tok_line;
    line_no = sv_line;
    posi = sv_pos;
    tok_start_pos = sv_tok_start;
    error_here(rhs == NULL ? "malformed initializer expression" : "unsupported struct initializer expression");

    if (rhs != NULL) {
        posi = end_pos;
        tok_start_pos = end_tok_start;
        line_no = end_line;
        tok_line = end_tok_line;
        tok = end_tok;
    } else {
        while (tok.kind != TOK_EOF && tok.kind != ',' && tok.kind != ';' && tok.kind != '}')
            next_token();
    }
}

static int ast_bool_bitand_const_rhs(const struct AstNode *n,
                                     const struct AstNode **out_value,
                                     unsigned int *out_mask)
{
    if (n == NULL || n->kind != AST_BINARY || n->op != '&')
        return 0;
    if (n->a != NULL && n->a->kind == AST_INT_LIT &&
        n->b != NULL && ast_value_is_plain_int(n->b) && ast_gen_supported(n->b)) {
        *out_value = n->b;
        *out_mask = (unsigned int)(n->a->ival & 0xffffL);
        return 1;
    }
    if (n->b != NULL && n->b->kind == AST_INT_LIT &&
        n->a != NULL && ast_value_is_plain_int(n->a) && ast_gen_supported(n->a)) {
        *out_value = n->a;
        *out_mask = (unsigned int)(n->b->ival & 0xffffL);
        return 1;
    }
    return 0;
}

static void emit_store_bool_masked_hl_to_addr_on_stack(unsigned int mask,
                                                       int need_result)
{
    unsigned int byte_mask;

    mask &= 0xffffU;
    if (mask == 0) {
        if (need_result)
            emit("\tld de,0\n");
        emit("\tpop hl\n\tld (hl),0\n");
        return;
    }

    byte_mask = 0;
    if ((mask & 0xff00U) == 0) {
        byte_mask = mask & 0xffU;
        fprintf(outf, "\tld a,l\n\tand %u\n", byte_mask);
    } else if ((mask & 0x00ffU) == 0) {
        byte_mask = (mask >> 8) & 0xffU;
        fprintf(outf, "\tld a,h\n\tand %u\n", byte_mask);
    } else {
        fprintf(outf, "\tld a,h\n\tand %u\n\tld e,a\n\tld a,l\n\tand %u\n\tor e\n",
                (mask >> 8) & 0xffU, mask & 0xffU);
    }
    if (need_result)
        emit("\tld d,0\n");

    if (byte_mask == 1) {
        emit("\tld e,a\n\tpop hl\n\tld (hl),e\n");
        return;
    }
    if (byte_mask == 128) {
        emit("\trlca\n\tld e,a\n\tpop hl\n\tld (hl),e\n");
        return;
    }
    if (byte_mask == 64) {
        emit("\trlca\n\trlca\n\tld e,a\n\tpop hl\n\tld (hl),e\n");
        return;
    }

    emit("\tld e,0\n\tjr z,$+3\n\tinc e\n\tpop hl\n\tld (hl),e\n");
}

/* True if `rhs` is exactly the shape __fmadd can fuse: a float-valued
 * multiplication. Checked via ast_expr_type_for_sizeof, a side-effect-free
 * recursive type walker (originally written for sizeof, general enough to
 * reuse here) - this mirrors the runtime check gen_binary_ast uses to
 * decide whether '*' takes the float path, just evaluated ahead of time
 * instead of after ast_gen_expr(n->a) has already run. */
static int ast_is_float_madd_rhs(const struct AstNode *rhs)
{
    if (rhs->kind != AST_BINARY || rhs->op != '*')
        return 0;
    return type_is_float(ast_expr_type_for_sizeof(rhs));
}

/* Shared tail for a float compound assignment, called once the addend
 * (the compound-assign target's current value) has already been evaluated
 * and pushed as DE:HL. When n is `+=` and the rhs is a float multiply,
 * fuses into a single __fmadd(addend, a, b) call instead of evaluating
 * the multiply into DE:HL, pushing it, and calling __fmul separately -
 * skipping one pack+unpack round trip through the runtime's IEEE format.
 * Falls back to the original evaluate/push/call/pop sequence for every
 * other float compound assignment (+=, -=, *=, /= with a non-fusable rhs).
 * g_expr_type is left as whatever ast_gen_expr(n->b) (or n->b->a/n->b->b)
 * last set it to, matching the pre-existing behavior at every call site -
 * each site overwrites it with the (float) lvalue type immediately after. */
static void emit_float_compound_rhs(const struct AstNode *n, int saved_dead)
{
    const char *helper;

    if (n->op == TOK_ADDEQ && ast_is_float_madd_rhs(n->b)) {
        expr_result_dead = 0;
        ast_gen_expr(n->b->a);
        if (!type_is_float(g_expr_type))
            emit_convert_int_to_float(g_expr_type);
        emit("\tpush de\n\tpush hl\n");
        ast_gen_expr(n->b->b);
        if (!type_is_float(g_expr_type))
            emit_convert_int_to_float(g_expr_type);
        expr_result_dead = saved_dead;
        /* n->b->b is still live in DE:HL right here - see __fmaf's
         * call site above for why this skips a third push/pop. */
        emit_runtime_call("__fmaf");
        emit("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n");
        return;
    }

    expr_result_dead = 0;
    ast_gen_expr(n->b);
    expr_result_dead = saved_dead;
    if (!type_is_float(g_expr_type))
        emit_convert_int_to_float(g_expr_type);
    /* n->b is still live in DE:HL right here - see the fastcall call
     * site in gen_binary_ast above for why this skips a second push. */
    helper = n->op == TOK_ADDEQ ? "__faf" :
             n->op == TOK_SUBEQ ? "__fsf" :
             n->op == TOK_MULEQ ? "__fmf" : "__fdf";
    emit_runtime_call(helper);
    emit("\tpop bc\n\tpop bc\n");
}

void gen_assign_ast(const struct AstNode *n)
{
    struct Sym *s;
    int common_type;
    int binop;
    int saved_dead;

    if (ast_struct_deref_copy_assign_supported(n)) {
        gen_struct_deref_copy_assign_ast(n);
        return;
    }

    if (ast_struct_member_copy_assign_supported(n)) {
        gen_struct_member_copy_assign_ast(n);
        return;
    }

    if (ast_is_byte_addr_copy_assign(n)) {
        gen_byte_addr_copy_assign_ast(n);
        return;
    }

    if (ast_struct_copy_assign_supported(n)) {
        gen_struct_copy_assign_ast(n);
        return;
    }

    if (ast_struct_chain_copy_assign_supported(n)) {
        gen_struct_chain_copy_assign_ast(n);
        return;
    }

    if (ast_long_va_arg_self_assign_supported(n, NULL)) {
        gen_long_va_arg_self_assign_ast(n);
        return;
    }

    {
        int lhs_type;
        if (n->op == '=' && ast_struct_addr_expr_supported(n->a, &lhs_type) &&
            ast_struct_return_call_assign_supported(lhs_type, n->b)) {
            gen_struct_return_call_assign_ast(n->a, n->b);
            return;
        }
    }

    /* Non-identifier lvalue store: a subscript element `arr[i]`, a struct field
     * `s.f` / `p->f`, or a deref `*p`.  The address machine differs per lvalue
     * kind (factored helpers), but the store tail is uniform.  Handles both
     * plain `=` and the arithmetic/bitwise compound operators; shift-assigns
     * and any wider/pointer element are excluded by the gate. */
    if (n->a->kind == AST_INDEX || n->a->kind == AST_MEMBER ||
        (n->a->kind == AST_UNARY && n->a->op == '*')) {
        struct Sym *byte_arr;
        struct Sym *byte_idx_sym;
        struct Sym *byte_rhs_sym;
        long byte_idx;
        long byte_rhs;
        int byte_idx_has_const;
        int byte_rhs_kind;
        int val_type;
        int want_dead = expr_result_dead;
        int bf_width;
        int bf_shift;
        unsigned int bf_mask;
        int rhs_bool01;
        const struct AstNode *mask_value;
        unsigned int bool_mask;

        if (ast_global_byte_array_const_store(n, &byte_arr, &byte_idx, &byte_rhs)) {
            emit_global_byte_array_index_addr(byte_arr, NULL, byte_idx, 1);
            fprintf(outf, "\tld (hl),%ld\n", byte_rhs & 255);
            g_expr_type = byte_arr->type;
            g_long_from16 = 0;
            return;
        }

        if (ast_global_byte_array_fast_store(n, &byte_arr, &byte_idx_sym,
                                             &byte_idx, &byte_idx_has_const,
                                             &byte_rhs_sym, &byte_rhs,
                                             &byte_rhs_kind)) {
            emit_global_byte_array_index_addr(byte_arr, byte_idx_sym, byte_idx,
                                              byte_idx_has_const);
            if (byte_rhs_kind == 1) {
                fprintf(outf, "\tld a,(ix%+d)\n", byte_rhs_sym->offset);
                emit("\tld (hl),a\n");
            } else {
                fprintf(outf, "\tld (hl),%ld\n", byte_rhs & 255);
            }
            g_expr_type = byte_arr->type;
            g_long_from16 = 0;
            return;
        }

        if (n->a->kind == AST_INDEX)
            gen_index_addr_ast(n->a, &val_type);    /* HL = element address */
        else if (n->a->kind == AST_MEMBER)
            gen_member_addr_ast(n->a, &val_type);   /* HL = field address */
        else
            gen_deref_addr_ast(n->a, &val_type);    /* HL = target address */

        bf_width = current_field_bit_width;
        bf_shift = current_field_bit_shift;
        bf_mask = current_field_bit_mask;

        /* The low-byte store shortcut is correct for char/uchar fields and
         * array elements, but not for _Bool.  _Bool assignment must first
         * normalize every non-zero RHS value to exactly 1; otherwise cases
         * like arr[1] = 123, flags.a = 1000L, and *p = 77 store the raw low
         * byte and break C99 _Bool semantics.  Let the normal assignment path
         * below evaluate and emit_bool_normalize_hl() for boolean lvalues. */
        if (n->op == '=' && type_size(val_type) == 1 && !type_is_bool(val_type) &&
            bf_width == 0 && emit_low_byte_expr_to_a(n->b)) {
            emit("\tld (hl),a\n");
            if (!want_dead) {
                emit("\tld l,a\n");
                if (val_type & TYPE_UNSIGNED)
                    emit("\tld h,0\n");
                else
                    emit("\trlca\n\tsbc a,a\n\tld h,a\n");
            }
            g_expr_type = val_type;
            g_long_from16 = 0;
            return;
        }

        if (n->op == '=') {
            emit("\tpush hl\n");
            rhs_bool01 = 0;

            if (type_is_bool(val_type) &&
                ast_bool_bitand_const_rhs(n->b, &mask_value, &bool_mask)) {
                saved_dead = expr_result_dead;
                expr_result_dead = 0;
                ast_gen_expr(mask_value);
                expr_result_dead = saved_dead;
                emit_store_bool_masked_hl_to_addr_on_stack(bool_mask, !want_dead);
                if (!want_dead)
                    emit("\tex de,hl\n");
                g_long_from16 = 0;
                return;
            }

            saved_dead = expr_result_dead;
            expr_result_dead = 0;
            if (type_ptr_depth(val_type) > 0 && n->b->kind == AST_CAST) {
                ast_gen_expr(n->b->a);              /* rhs -> HL */
            } else if (type_ptr_depth(val_type) > 0) {
                int ptr_type;
                int no_deref;
                if (ast_pointer_expr_type(n->b, &ptr_type, &no_deref))
                    gen_pointer_expr_ast(n->b, &ptr_type, &no_deref);
                else
                    ast_gen_expr(n->b);
            } else {
                ast_gen_expr(n->b);                 /* rhs -> HL */
            }
            expr_result_dead = saved_dead;
            if (type_is_bool(val_type)) {
                rhs_bool01 = ast_expr_yields_bool01(n->b);
                if (!rhs_bool01) {
                    emit_bool_normalize_hl(g_expr_type);
                    rhs_bool01 = 1;
                }
            }
            if (type_size(val_type) == 4) {
                if (type_is_float(val_type)) {
                    if (!type_is_float(g_expr_type))
                        emit_convert_int_to_float(g_expr_type);
                } else if (!type_is_long(g_expr_type)) {
                    emit_extend_to_long_typed(g_expr_type);
                }
                emit_store_de_to_addr_hl(val_type);  /* pops address itself */
                /* emit_store_de_to_addr_hl leaves the stored 32-bit value as
                 * DE=low word, BC=high word; rebuild DE:HL=value for a live
                 * result (e.g. `x = (p->f = v)`). */
                if (!want_dead)
                    emit("\tex de,hl\n\tld d,b\n\tld e,c\n");
                g_long_from16 = 0;
                return;
            }
            if (bf_width > 0) {
                current_field_bit_width = bf_width;
                current_field_bit_shift = bf_shift;
                current_field_bit_mask = bf_mask;
                emit_store_bitfield_from_hl();
                g_long_from16 = 0;
                return;
            }
            emit("\tex de,hl\n\tpop hl\n");         /* DE = value, HL = address */
            if (type_is_bool(val_type) && rhs_bool01)
                emit("\tld (hl),e\n");
            else
                emit_store_de_to_addr_hl(val_type);
            if (!want_dead)
                emit("\tex de,hl\n");
            g_long_from16 = 0;
            return;
        }

        switch (n->op) {
        case TOK_ADDEQ: binop = '+'; break;
        case TOK_SUBEQ: binop = '-'; break;
        case TOK_MULEQ: binop = '*'; break;
        case TOK_DIVEQ: binop = '/'; break;
        case TOK_MODEQ: binop = '%'; break;
        case TOK_ANDEQ: binop = '&'; break;
        case TOK_OREQ:  binop = '|'; break;
        default:        binop = '^'; break;   /* TOK_XOREQ */
        }

        if ((n->op == TOK_ADDEQ || n->op == TOK_SUBEQ) &&
            type_ptr_depth(val_type) > 0 && type_size(val_type) == 2) {
            int elem_size;

            elem_size = type_index_elem_size(val_type);
            emit("\tpush hl\n");                    /* save lvalue address */
            emit_load_from_hl(val_type);            /* HL = current pointer */
            emit("\tpush hl\n");                    /* save current pointer */

            saved_dead = expr_result_dead;
            expr_result_dead = 0;
            ast_gen_expr(n->b);                     /* HL = element count */
            expr_result_dead = saved_dead;
            scale_hl_by_elem_size(elem_size);

            emit("\tex de,hl\n\tpop hl\n");         /* DE = scaled count, HL = current pointer */
            if (n->op == TOK_ADDEQ)
                emit("\tadd hl,de\n");
            else
                emit("\tor a\n\tsbc hl,de\n");
            emit("\tex de,hl\n\tpop hl\n");         /* DE = result, HL = address */
            emit_store_de_to_addr_hl(val_type);
            if (!want_dead)
                emit("\tex de,hl\n");
            g_expr_type = val_type;
            g_long_from16 = 0;
            return;
        }

        if (type_size(val_type) == 4) {
            emit("\tpush hl\n");                    /* save lvalue address */
            emit_load_from_hl(val_type);             /* DE:HL = current value */

            if (type_is_long(val_type) &&
                (n->op == TOK_SHLEQ || n->op == TOK_SHREQ) &&
                n->b->kind == AST_INT_LIT) {
                if (!emit_shift_const_long(n->op, val_type, n->b->ival)) {
                    fprintf(outf, "\tld b,%ld\n", n->b->ival & 255L);
                    emit_shift_loop(n->op, val_type);
                }
                emit("\tld b,d\n\tld c,e\n");
                emit("\tpop de\n");
                emit("\tex de,hl\n");
                emit("\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tinc hl\n\tld (hl),c\n\tinc hl\n\tld (hl),b\n");
                if (!want_dead) {
                    emit("\tex de,hl\n");
                    emit("\tld d,b\n\tld e,c\n");
                }
                g_expr_type = val_type;
                g_long_from16 = 0;
                return;
            }

            emit("\tpush de\n\tpush hl\n");         /* save current value */
            saved_dead = expr_result_dead;

            if (type_is_float(val_type)) {
                emit_float_compound_rhs(n, saved_dead);
                emit_store_de_to_addr_hl(val_type);
                /* Rebuild DE:HL=value from the store's DE=low/BC=high leftovers
                 * so a live result (`x = (p->f += v)`) is correct. */
                if (!want_dead)
                    emit("\tex de,hl\n\tld d,b\n\tld e,c\n");
                g_expr_type = val_type;
                g_long_from16 = 0;
                return;
            }

            expr_result_dead = 0;
            ast_gen_expr(n->b);                      /* rhs -> DE:HL or HL */
            expr_result_dead = saved_dead;

            if (type_is_long(val_type) &&
                (n->op == TOK_SHLEQ || n->op == TOK_SHREQ)) {
                emit("\tld b,l\n\tpop hl\n\tpop de\n");
                emit_shift_loop(n->op, val_type);
                emit("\tld b,d\n\tld c,e\n");
                emit("\tpop de\n");
                emit("\tex de,hl\n");
                emit("\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tinc hl\n\tld (hl),c\n\tinc hl\n\tld (hl),b\n");
                if (!want_dead) {
                    emit("\tex de,hl\n");
                    emit("\tld d,b\n\tld e,c\n");
                }
                g_expr_type = val_type;
                g_long_from16 = 0;
                return;
            }

            common_type = common_arith_type(val_type, g_expr_type);
            emit_cast_16_to_common(g_expr_type, common_type);
            gen_binop32_typed(binop, common_type);
            emit_store_de_to_addr_hl(val_type);
            /* Rebuild DE:HL=value from the store's DE=low/BC=high leftovers so a
             * live result (`x = (p->lf += v)`) is correct. */
            if (!want_dead)
                emit("\tex de,hl\n\tld d,b\n\tld e,c\n");
            g_expr_type = val_type;
            g_long_from16 = 0;
            return;
        }

        /* Compound assignment to a non-identifier lvalue.  Streaming's general
         * normal_assign compound tail: save the address, load the current
         * value, save it, evaluate the RHS, combine, then store back (also
         * leaving the result in HL when the statement value is live). */
        switch (n->op) {
        case TOK_ADDEQ: binop = '+'; break;
        case TOK_SUBEQ: binop = '-'; break;
        case TOK_MULEQ: binop = '*'; break;
        case TOK_DIVEQ: binop = '/'; break;
        case TOK_MODEQ: binop = '%'; break;
        case TOK_ANDEQ: binop = '&'; break;
        case TOK_OREQ:  binop = '|'; break;
        default:        binop = '^'; break;   /* TOK_XOREQ */
        }

        emit("\tpush hl\n");                    /* save lvalue address */
        emit_load_from_hl(val_type);            /* HL = current value */
        if (bf_width > 0) {
            current_field_bit_width = bf_width;
            current_field_bit_shift = bf_shift;
            current_field_bit_mask = bf_mask;
            g_expr_type = val_type;
            emit_extract_bitfield();
        }
        emit("\tpush hl\n");                    /* save current value */

        saved_dead = expr_result_dead;
        expr_result_dead = 0;
        ast_gen_expr(n->b);                     /* rhs -> HL */
        expr_result_dead = saved_dead;

        if (n->op == TOK_SHLEQ || n->op == TOK_SHREQ) {
            emit("\tld b,l\n\tpop hl\n");
            emit_shift_loop(n->op, val_type);
            emit("\tex de,hl\n\tpop hl\n");
            if (bf_width > 0) {
                current_field_bit_width = bf_width;
                current_field_bit_shift = bf_shift;
                current_field_bit_mask = bf_mask;
                g_expr_type = val_type;   /* field type -> correct extract sign */
                emit_store_bitfield_de_to_addr_hl(!want_dead);
            } else {
                emit_store_de_to_addr_hl(val_type);
                if (!want_dead)
                    emit("\tex de,hl\n");
            }
            g_long_from16 = 0;
            return;
        }

        emit("\tex de,hl\n\tpop hl\n");         /* DE = rhs, HL = current value */
        common_type = common_arith_type(val_type, g_expr_type);
        gen_binop_typed(binop, common_type);    /* HL = result */
        emit("\tex de,hl\n\tpop hl\n");         /* DE = result, HL = address */
        if (bf_width > 0) {
            current_field_bit_width = bf_width;
            current_field_bit_shift = bf_shift;
            current_field_bit_mask = bf_mask;
            g_expr_type = val_type;   /* field type -> correct extract sign */
            emit_store_bitfield_de_to_addr_hl(!want_dead);
        } else {
            emit_store_de_to_addr_hl(val_type);
            if (!want_dead)
                emit("\tex de,hl\n");
        }
        g_long_from16 = 0;
        return;
    }

    s = find_sym(n->a->sval);

    if (n->op == '=' && type_is_struct_object(s->type) &&
        ast_struct_return_call_assign_supported(s->type, n->b)) {
        gen_struct_return_call_assign_ast(n->a, n->b);
        return;
    }

    if (n->op == '=' && type_ptr_depth(s->type) > 0 && !sym_can_ix_direct(s) &&
        !is_global_word_sym(s) && expr_result_dead) {
        saved_dead = expr_result_dead;
        emit_load_sym_addr(s);
        emit("\tpush hl\n");
        expr_result_dead = 0;
        if (n->b->kind == AST_CAST) {
            ast_gen_expr(n->b->a);
        } else {
            int ptr_type;
            int no_deref;
            if (ast_pointer_expr_type(n->b, &ptr_type, &no_deref))
                gen_pointer_expr_ast(n->b, &ptr_type, &no_deref);
            else
                ast_gen_expr(n->b);
        }
        expr_result_dead = saved_dead;
        emit("\tex de,hl\n\tpop hl\n");
        emit_store_de_to_addr_hl(s->type);
        g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    if (n->op == '=' && type_is_float(s->type) && !sym_can_ix_direct(s)) {
        saved_dead = expr_result_dead;
        emit_load_sym_addr(s);
        emit("\tpush hl\n");
        expr_result_dead = 0;
        ast_gen_expr(n->b);
        expr_result_dead = saved_dead;
        if (!type_is_float(g_expr_type))
            emit_convert_int_to_float(g_expr_type);
        emit_store_de_to_addr_hl(s->type);
        if (!saved_dead)
            emit("\tex de,hl\n\tld d,b\n\tld e,c\n");
        g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    if (n->op == '=' && type_is_float(s->type) && sym_can_ix_direct(s)) {
        saved_dead = expr_result_dead;
        expr_result_dead = 0;
        ast_gen_expr(n->b);
        expr_result_dead = saved_dead;
        if (!type_is_float(g_expr_type))
            emit_convert_int_to_float(g_expr_type);
        emit_store_hl_to_sym_direct(s);
        g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    if ((n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
         n->op == TOK_MULEQ || n->op == TOK_DIVEQ) &&
        type_is_float(s->type) && !sym_can_ix_direct(s)) {
        saved_dead = expr_result_dead;
        emit_load_sym_addr(s);
        emit("\tpush hl\n");
        emit_load_from_hl(s->type);
        emit("\tpush de\n\tpush hl\n");
        emit_float_compound_rhs(n, saved_dead);
        emit_store_de_to_addr_hl(s->type);
        if (!saved_dead)
            emit("\tex de,hl\n\tld d,b\n\tld e,c\n");
        g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    if (n->op == '=' && type_is_long(s->type) && !sym_can_ix_direct(s)) {
        saved_dead = expr_result_dead;
        emit_load_sym_addr(s);
        emit("\tpush hl\n");
        expr_result_dead = 0;
        ast_gen_expr(n->b);
        expr_result_dead = saved_dead;
        if (type_is_float(g_expr_type))
            emit_convert_float_to_intlike(s->type);
        else if (!type_is_long(g_expr_type))
            emit_extend_to_long_typed(g_expr_type);
        emit_store_de_to_addr_hl(s->type);
        if (!saved_dead)
            emit("\tex de,hl\n\tld d,b\n\tld e,c\n");
        g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    if ((n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
         n->op == TOK_MULEQ || n->op == TOK_DIVEQ) &&
        type_is_float(s->type) && sym_can_ix_direct(s)) {
        saved_dead = expr_result_dead;
        emit_load_sym_value_direct(s);
        emit("\tpush de\n\tpush hl\n");
        emit_float_compound_rhs(n, saved_dead);
        emit_store_hl_to_sym_direct(s);
        g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    if (n->op == '=' && type_is_long(s->type) && sym_can_ix_direct(s)) {
        saved_dead = expr_result_dead;
        expr_result_dead = 0;
        ast_gen_expr(n->b);
        expr_result_dead = saved_dead;
        if (type_is_float(g_expr_type))
            emit_convert_float_to_intlike(s->type);
        else if (!type_is_long(g_expr_type))
            emit_extend_to_long_typed(g_expr_type);
        emit_store_hl_to_sym_direct(s);
        g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    if (n->op == TOK_OREQ && type_is_long(s->type) && sym_can_ix_direct(s) &&
        emit_long_oreq_byte_lane(s, n->b)) {
        g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    if ((n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
         n->op == TOK_MULEQ || n->op == TOK_DIVEQ || n->op == TOK_MODEQ ||
         n->op == TOK_ANDEQ || n->op == TOK_OREQ  || n->op == TOK_XOREQ) &&
        type_is_long(s->type) && sym_can_ix_direct(s)) {
        int b32;
        saved_dead = expr_result_dead;
        emit_load_sym_value_direct(s);
        emit("\tpush de\n\tpush hl\n");
        expr_result_dead = 0;
        ast_gen_expr(n->b);
        expr_result_dead = saved_dead;
        if (!type_is_long(g_expr_type))
            emit_extend_to_long_typed(g_expr_type);
        switch (n->op) {
        case TOK_ADDEQ: b32 = '+'; break;
        case TOK_SUBEQ: b32 = '-'; break;
        case TOK_MULEQ: b32 = '*'; break;
        case TOK_DIVEQ: b32 = '/'; break;
        case TOK_MODEQ: b32 = '%'; break;
        case TOK_ANDEQ: b32 = '&'; break;
        case TOK_OREQ:  b32 = '|'; break;
        default:        b32 = '^'; break;   /* TOK_XOREQ */
        }
        gen_binop32(b32, s->type);
        emit_store_hl_to_sym_direct(s);
        g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    if ((n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
         n->op == TOK_MULEQ || n->op == TOK_DIVEQ || n->op == TOK_MODEQ ||
         n->op == TOK_ANDEQ || n->op == TOK_OREQ  || n->op == TOK_XOREQ) &&
        type_is_long(s->type) && !sym_can_ix_direct(s)) {
        int b32;
        saved_dead = expr_result_dead;
        emit_load_sym_addr(s);
        emit("\tpush hl\n");
        emit_load_from_hl(s->type);
        emit("\tpush de\n\tpush hl\n");
        expr_result_dead = 0;
        ast_gen_expr(n->b);
        expr_result_dead = saved_dead;
        if (!type_is_long(g_expr_type))
            emit_extend_to_long_typed(g_expr_type);
        switch (n->op) {
        case TOK_ADDEQ: b32 = '+'; break;
        case TOK_SUBEQ: b32 = '-'; break;
        case TOK_MULEQ: b32 = '*'; break;
        case TOK_DIVEQ: b32 = '/'; break;
        case TOK_MODEQ: b32 = '%'; break;
        case TOK_ANDEQ: b32 = '&'; break;
        case TOK_OREQ:  b32 = '|'; break;
        default:        b32 = '^'; break;   /* TOK_XOREQ */
        }
        gen_binop32(b32, s->type);
        emit_store_de_to_addr_hl(s->type);
        if (!saved_dead)
            emit("\tex de,hl\n\tld d,b\n\tld e,c\n");
        g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    if ((n->op == TOK_SHLEQ || n->op == TOK_SHREQ) &&
        type_is_long(s->type) && !sym_can_ix_direct(s)) {
        saved_dead = expr_result_dead;
        emit_load_sym_addr(s);
        emit("\tpush hl\n");
        emit_load_from_hl(s->type);
        if (n->b->kind == AST_INT_LIT && ast_value_is_plain_int(n->b)) {
            if (!emit_shift_const_long(n->op, s->type, n->b->ival)) {
                fprintf(outf, "\tld b,%ld\n", n->b->ival & 255L);
                emit_shift_loop(n->op, s->type);
            }
        } else {
            emit("\tpush de\n\tpush hl\n");
            expr_result_dead = 0;
            ast_gen_expr(n->b);
            expr_result_dead = saved_dead;
            emit("\tld b,l\n\tpop hl\n\tpop de\n");
            emit_shift_loop(n->op, s->type);
        }
        emit("\tld b,d\n\tld c,e\n");
        emit("\tpop de\n");
        emit("\tex de,hl\n");
        emit("\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tinc hl\n\tld (hl),c\n\tinc hl\n\tld (hl),b\n");
        if (!saved_dead)
            emit("\tex de,hl\n\tld d,b\n\tld e,c\n");
        g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    if ((n->op == TOK_SHLEQ || n->op == TOK_SHREQ) &&
        type_is_long(s->type) && sym_can_ix_direct(s)) {
        saved_dead = expr_result_dead;
        emit_load_sym_value_direct(s);
        expr_result_dead = 0;
        if (n->b->kind == AST_INT_LIT && ast_value_is_plain_int(n->b)) {
            if (!emit_shift_const_long(n->op, s->type, n->b->ival)) {
                fprintf(outf, "\tld b,%ld\n", n->b->ival & 255L);
                emit_shift_loop(n->op, s->type);
            }
        } else {
            emit("\tpush de\n\tpush hl\n");
            ast_gen_expr(n->b);
            emit("\tld b,l\n\tpop hl\n\tpop de\n");
            emit_shift_loop(n->op, s->type);
        }
        expr_result_dead = saved_dead;
        emit_store_hl_to_sym_direct(s);
        g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    if (n->op == '=' && s->reg_alloc == REG_NONE && !sym_can_ix_direct(s) && !is_global_word_sym(s) &&
        ast_is_plain_int_type(s->type) &&
        (type_size(s->type) == 1 || type_size(s->type) == 2)) {
        int want_dead = expr_result_dead;

        emit_load_sym_addr(s);
        emit("\tpush hl\n");
        saved_dead = expr_result_dead;
        expr_result_dead = 0;
        ast_gen_expr(n->b);
        expr_result_dead = saved_dead;
        if (type_is_bool(s->type) && !ast_expr_yields_bool01(n->b))
            emit_bool_normalize_hl(g_expr_type);
        if (type_size(s->type) > 1)
            emit_promote_byte_to_int(g_expr_type);
        emit("\tex de,hl\n\tpop hl\n");
        emit_store_de_to_addr_hl(s->type);
        if (!want_dead)
            emit("\tex de,hl\n");
        /* A long rhs was narrowed by storing only its low word; the live
         * result in HL is that 16-bit word, so the expression's type is the
         * lvalue's int type.  Leaving g_expr_type as TYPE_LONG would make an
         * enclosing consumer (e.g. `y = (p = -32768L)`) treat leftover DE as
         * the high word. */
        if (type_is_long(g_expr_type))
            g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    if (s->reg_alloc == REG_NONE && !sym_can_ix_direct(s) && !is_global_word_sym(s) &&
        (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
         n->op == TOK_ANDEQ || n->op == TOK_OREQ || n->op == TOK_XOREQ) &&
        ast_is_plain_int_type(s->type) &&
        (type_size(s->type) == 1 || type_size(s->type) == 2)) {
        int want_dead = expr_result_dead;
        if (n->op == TOK_ADDEQ)
            binop = '+';
        else if (n->op == TOK_SUBEQ)
            binop = '-';
        else if (n->op == TOK_ANDEQ)
            binop = '&';
        else if (n->op == TOK_OREQ)
            binop = '|';
        else
            binop = '^';

        emit_load_sym_addr(s);
        emit("\tpush hl\n");
        emit_load_from_hl(s->type);
        emit("\tpush hl\n");

        saved_dead = expr_result_dead;
        expr_result_dead = 0;
        ast_gen_expr(n->b);
        expr_result_dead = saved_dead;

        emit("\tex de,hl\n\tpop hl\n");
        common_type = common_arith_type(s->type, g_expr_type);
        gen_binop_typed(binop, common_type);
        emit("\tex de,hl\n\tpop hl\n");
        emit_store_de_to_addr_hl(s->type);
        if (!want_dead)
            emit("\tex de,hl\n");
        g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    if (expr_result_dead &&
        (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ) &&
        !type_is_long(s->type) && !type_is_float(s->type) &&
        type_ptr_depth(s->type) == 0 &&
                sym_can_ix_direct(s) && ast_value_is_plain_int(n->b) &&
                !(n->b->kind == AST_INT_LIT ||
                    (n->b->kind == AST_IDENT && sym_can_ix_direct(find_sym(n->b->sval))))) {
                binop = (n->op == TOK_ADDEQ) ? '+' : '-';

                emit_load_sym_value_direct(s);
                emit("\tpush hl\n");
                saved_dead = expr_result_dead;
                expr_result_dead = 0;
                ast_gen_expr(n->b);
                expr_result_dead = saved_dead;
                emit("\tex de,hl\n\tpop hl\n");
                common_type = common_arith_type(s->type, g_expr_type);
                gen_binop_typed(binop, common_type);
                emit_store_hl_to_sym_direct(s);
                g_expr_type = s->type;
                g_long_from16 = 0;
                return;
        }

        if (expr_result_dead &&
                (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ) &&
                !type_is_long(s->type) && !type_is_float(s->type) &&
        (n->b->kind == AST_INT_LIT || n->b->kind == AST_IDENT ||
         ast_const_plain_int_binary_supported(n->b) ||
         (type_ptr_depth(s->type) > 0 && ast_value_is_plain_int(n->b)))) {
        emit_load_sym_value_direct(s);
        if (n->b->kind == AST_INT_LIT) {
            long scaled = n->b->ival;
            if (s->type & (TYPE_PTR | TYPE_PTR2))
                scaled *= type_index_elem_size(s->type);
            emit_ld_de_const(scaled);
        } else if (n->b->kind == AST_IDENT) {
            struct Sym *rs = find_sym(n->b->sval);
            emit_load_sym_de_direct(rs);
            if (s->type & (TYPE_PTR | TYPE_PTR2)) {
                int elem = type_index_elem_size(s->type);
                if (elem > 1) {
                    emit("\tpush hl\n");
                    emit("\tex de,hl\n");
                    scale_hl_by_elem_size(elem);
                    emit("\tex de,hl\n");
                    emit("\tpop hl\n");
                }
            }
        } else {
            emit("\tpush hl\n");
            saved_dead = expr_result_dead;
            expr_result_dead = 0;
            ast_gen_expr(n->b);
            expr_result_dead = saved_dead;
            if (s->type & (TYPE_PTR | TYPE_PTR2))
                scale_hl_by_elem_size(type_index_elem_size(s->type));
            emit("\tex de,hl\n\tpop hl\n");
        }
        if (n->op == TOK_ADDEQ)
            emit("\tadd hl,de\n");
        else
            emit("\tor a\n\tsbc hl,de\n");
        emit_store_hl_to_sym_direct(s);
        g_long_from16 = 0;
        return;
    }

    if (n->op == '=') {
        if (type_size(s->type) == 1 &&
            (s->storage == SC_GLOBAL || s->storage == SC_EXTERN)) {
            int want_dead = expr_result_dead;
            int saved_dead;
            emit_load_sym_addr(s);
            emit("\tpush hl\n");
            saved_dead = expr_result_dead;
            expr_result_dead = 0;
            ast_gen_expr(n->b);
            expr_result_dead = saved_dead;
            emit("\tex de,hl\n\tpop hl\n");
            emit_store_de_to_addr_hl(s->type);
            if (!want_dead)
                emit("\tex de,hl\n");
            g_expr_type = s->type;
            g_long_from16 = 0;
            return;
        }
        if (type_size(s->type) == 1 && s->reg_alloc == REG_NONE && n->b->kind == AST_IDENT) {
            struct Sym *rs = find_sym(n->b->sval);
            if (rs != NULL && sym_can_ix_direct(rs) &&
                !type_is_float(rs->type) && !type_is_long(rs->type)) {
                if (type_is_bool(s->type)) {
                    emit_load_sym_value_direct(rs);
                    emit_store_hl_to_sym_direct(s);
                    g_expr_type = s->type;
                    g_long_from16 = 0;
                    return;
                }
                fprintf(outf, "\tld a,(ix%+d)\n", rs->offset);
                fprintf(outf, "\tld (ix%+d),a\n", s->offset);
                g_expr_type = s->type;
                g_long_from16 = 0;
                /* This statement's value may itself be read by an enclosing
                 * expression (e.g. a chained `outer = byte_var = other;`) -
                 * the store above only wrote the byte, it never put the
                 * (possibly sign/zero-extended) result in HL. Only bother
                 * when the caller actually needs it: this exact gap (found
                 * via tests/00040.c's `for (r=i=0; ...)` once `i` narrowed
                 * to a byte) silently left HL holding unrelated leftover
                 * register contents, corrupting the outer assignment. */
                if (!expr_result_dead)
                    emit_load_sym_value_direct(s);
                return;
            }
        }
        if (type_size(s->type) == 1 && s->reg_alloc == REG_NONE && n->b->kind == AST_INT_LIT) {
            if (type_is_bool(s->type)) {
                fprintf(outf, "\tld (ix%+d),%d\n", s->offset, n->b->ival ? 1 : 0);
                g_expr_type = s->type;
                g_long_from16 = 0;
                if (!expr_result_dead)
                    emit_load_sym_value_direct(s);
                return;
            }
            fprintf(outf, "\tld (ix%+d),%ld\n", s->offset, n->b->ival & 255);
            g_expr_type = s->type;
            g_long_from16 = 0;
            if (!expr_result_dead)
                emit_load_sym_value_direct(s);
            return;
        }
        if (type_size(s->type) == 1 && s->reg_alloc == REG_NONE) {
            long fv;
            if (ast_int_const_cast_fold(n->b, &fv)) {
                if (type_is_bool(s->type))
                    fv = fv ? 1 : 0;
                fprintf(outf, "\tld (ix%+d),%ld\n", s->offset, fv & 255);
                g_expr_type = s->type;
                g_long_from16 = 0;
                if (!expr_result_dead)
                    emit_load_sym_value_direct(s);
                return;
            }
        }
        if (type_size(s->type) == 1 && sym_can_ix_direct(s) &&
            n->b->kind == AST_CALL) {
            /* Size-1 store-from-call tail: evaluate the call into HL and store
             * L only (no byte->int promote). */
            saved_dead = expr_result_dead;
            expr_result_dead = 0;
            ast_gen_expr(n->b);
            expr_result_dead = saved_dead;
            emit_store_hl_to_sym_direct(s);
            g_expr_type = s->type;
            g_long_from16 = 0;
            return;
        }
        if (type_ptr_depth(s->type) > 0 && n->b->kind == AST_CAST) {
            saved_dead = expr_result_dead;
            expr_result_dead = 0;
            ast_gen_expr(n->b->a);
            expr_result_dead = saved_dead;
        } else if (type_ptr_depth(s->type) > 0) {
            int ptr_type;
            int no_deref;
            saved_dead = expr_result_dead;
            expr_result_dead = 0;
            if (ast_pointer_expr_type(n->b, &ptr_type, &no_deref))
                gen_pointer_expr_ast(n->b, &ptr_type, &no_deref);
            else
                ast_gen_expr(n->b);
            expr_result_dead = saved_dead;
        } else {
            saved_dead = expr_result_dead;
            expr_result_dead = 0;
            ast_gen_expr(n->b);
            expr_result_dead = saved_dead;
        }
        if (type_is_bool(s->type)) {
            /* emit_store_hl_to_sym_direct normalises the 16-bit value; only
             * wide (long/float) sources need reducing to 0/1 up front. */
            if (!ast_expr_yields_bool01(n->b) &&
                (type_is_float(g_expr_type) || type_is_long(g_expr_type)))
                emit_bool_normalize_hl(g_expr_type);
        } else if (type_is_float(g_expr_type))
            emit_convert_float_to_intlike(s->type);
        else if (!type_is_long(g_expr_type) &&
                 /* gen_index_ast/gen_member_ast already promote a byte-sized
                  * element's value in H via emit_load_from_hl during the
                  * read itself - they just leave g_expr_type as the raw
                  * (unpromoted) element type afterward, since other callers
                  * need to see the true small type (e.g. this same
                  * function's own type_size(s->type)==1 fast paths above).
                  * Re-promoting here is a correct no-op (re-sign-extending
                  * an already-extended value doesn't change it) but wastes
                  * 4-5 real instructions every time - measurable in a hot
                  * path (profiled: tchess.c's `p = board[s]`, the single
                  * hottest statement shape in its two hottest functions).
                  * Bitfield extractions (current_field_bit_width > 0) go
                  * through emit_extract_bitfield() instead and are left
                  * alone here - not verified to leave H in the same
                  * already-promoted state. */
                 !((n->b->kind == AST_INDEX || n->b->kind == AST_MEMBER) &&
                   type_size(g_expr_type) == 1 && current_field_bit_width == 0))
            emit_promote_byte_to_int(g_expr_type);
        emit_store_hl_to_sym_direct(s);
        g_long_from16 = 0;
        return;
    }

    if (expr_result_dead && type_size(s->type) == 1 && s->reg_alloc == REG_NONE && !sym_can_ix_direct(s) &&
        (n->op == TOK_ANDEQ || n->op == TOK_OREQ || n->op == TOK_XOREQ)) {
        if (n->op == TOK_ANDEQ)
            binop = '&';
        else if (n->op == TOK_OREQ)
            binop = '|';
        else
            binop = '^';
        emit_load_sym_addr(s);
        emit("\tpush hl\n");
        emit_load_from_hl(s->type);
        emit("\tpush hl\n");
        saved_dead = expr_result_dead;
        expr_result_dead = 0;
        ast_gen_expr(n->b);
        expr_result_dead = saved_dead;
        emit("\tex de,hl\n\tpop hl\n");
        common_type = common_arith_type(s->type, g_expr_type);
        gen_binop_typed(binop, common_type);
        emit("\tex de,hl\n\tpop hl\n");
        emit_store_de_to_addr_hl(s->type);
        g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    if (n->op == TOK_SHLEQ || n->op == TOK_SHREQ) {
        emit_load_sym_value_direct(s);
        emit("\tpush hl\n");
        saved_dead = expr_result_dead;
        expr_result_dead = 0;
        ast_gen_expr(n->b);
        expr_result_dead = saved_dead;
        emit("\tld b,l\n\tpop hl\n");
        emit_shift_loop(n->op, s->type);
        emit_store_hl_to_sym_direct(s);
        g_long_from16 = 0;
        return;
    }

    if (type_size(s->type) == 2 &&
        (n->op == TOK_ANDEQ || n->op == TOK_OREQ || n->op == TOK_XOREQ) &&
        ast_value_is_long_word(n->b)) {
        emit_load_sym_value_direct(s);
        emit("\tpush hl\n");
        saved_dead = expr_result_dead;
        expr_result_dead = 0;
        ast_gen_expr(n->b);
        expr_result_dead = saved_dead;
        emit("\tex de,hl\n\tpop hl\n");
        if (n->op == TOK_ANDEQ)
            binop = '&';
        else if (n->op == TOK_OREQ)
            binop = '|';
        else
            binop = '^';
        gen_binop_typed(binop, s->type);
        emit_store_hl_to_sym_direct(s);
        g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    if (type_size(s->type) == 2 &&
        (n->op == TOK_MULEQ || n->op == TOK_DIVEQ || n->op == TOK_MODEQ) &&
        ast_long_word_type(n->b, &common_type)) {
        int b32;
        saved_dead = expr_result_dead;
        emit_load_sym_value_direct(s);
        common_type = common_arith_type(s->type, common_type);
        emit_cast_16_to_common(s->type, common_type);
        emit("\tpush de\n\tpush hl\n");
        expr_result_dead = 0;
        ast_gen_expr(n->b);
        expr_result_dead = saved_dead;
        emit_cast_16_to_common(g_expr_type, common_type);
        switch (n->op) {
        case TOK_MULEQ: b32 = '*'; break;
        case TOK_DIVEQ: b32 = '/'; break;
        default:        b32 = '%'; break;
        }
        gen_binop32(b32, common_type);
        emit_store_hl_to_sym_direct(s);
        g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    /* Compound assignment to a plain-int 16-bit scalar via the general compound
     * tail: load the LHS value, push it, evaluate the RHS, then combine with
     * the usual arithmetic conversions and store the result back (also leaving
     * it in HL). */
    switch (n->op) {
    case TOK_ADDEQ: binop = '+'; break;
    case TOK_SUBEQ: binop = '-'; break;
    case TOK_MULEQ: binop = '*'; break;
    case TOK_DIVEQ: binop = '/'; break;
    case TOK_MODEQ: binop = '%'; break;
    case TOK_ANDEQ: binop = '&'; break;
    case TOK_OREQ:  binop = '|'; break;
    default:        binop = '^'; break;   /* TOK_XOREQ */
    }

    emit_load_sym_value_direct(s);
    emit("\tpush hl\n");

    saved_dead = expr_result_dead;
    expr_result_dead = 0;
    ast_gen_expr(n->b);
    expr_result_dead = saved_dead;

    emit("\tex de,hl\n\tpop hl\n");
    common_type = common_arith_type(s->type, g_expr_type);
    gen_binop_typed(binop, common_type);
    emit_store_hl_to_sym_direct(s);
    g_long_from16 = 0;
}

/* First-subscript fast path for a global/static array base. The base address is
 * a compile-time symbol, so compute the scaled index into HL first and add the
 * base via `ld de,SYM` -- avoiding the `push hl / ex de,hl / pop hl` round-trip
 * the generic base-in-HL sequence needs for a non-constant index. The peephole
 * cannot remove that push/pop because a relocatable symbol is not a foldable
 * literal (its sxt/const folds skip symbol loads). Returns 1 when it emitted the
 * whole first-subscript address (HL = &SYM[idx]); 0 when the caller should fall
 * back (constant index, or a non-array / frame-relative base). */
static int emit_array_symbase_index(struct Sym *s, const struct AstNode *idx,
                                    int elem)
{
    if (s == NULL || !s->is_array)
        return 0;
    if (s->storage == SC_LOCAL || s->storage == SC_PARAM)
        return 0;                        /* frame-relative base, not a symbol */
    if (idx == NULL || idx->kind == AST_INT_LIT)
        return 0;                        /* constant index: add-const path is fine */
    gen_index_subscript_expr_ast(idx);   /* index -> HL */
    scale_hl_by_elem_size(elem);         /* index * elem */
    emit_extrn_if_needed(s);
    fprintf(outf, "\tld de,%s\n", asm_name_for(sym_asm_name(s)));
    emit("\tadd hl,de\n");
    return 1;
}

static int emit_runtime_pointer_array_stride(struct Sym *s)
{
    struct Sym *stride;
    int elem;

    if (s == NULL || s->runtime_stride_name[0] == 0)
        return 0;

    stride = find_sym(s->runtime_stride_name);
    if (stride == NULL)
        return 0;

    emit("\tpush hl\n");
    emit_load_sym_de_direct(stride);
    emit("\tpop hl\n");
    gen_binop_typed('*', TYPE_INT);
    elem = type_size(type_decay_ptr(s->type));
    if (elem <= 0)
        elem = 2;
    scale_hl_by_elem_size(elem);
    return 1;
}

/* Emit a plain-int subscript read `base[index]` via the IDENTIFIER-ROOTED
 * subscript machine (NOT the postfix chain - the two use different base loads
 * and element-size helpers).  The gate (ast_index_plain_int_read) guarantees a
 * bare-identifier base that is a 1-D plain-int array or an int* pointer, with a
 * supported, non-constant plain-int index, so exactly one subscript iteration
 * runs and the element load is a plain 16-bit load. */
void gen_index_addr_ast(const struct AstNode *n, int *out_val_type)
{
    struct Sym *s = NULL;
    int cur_type;
    int val_type;
    int elem_size;
    int no_deref;
    int global_ptr_preloaded = 0;
    int field_array = 0;
    int member_pointer = 0;
    int fa_dimc = 0;
    int fa_dims[4];
    int di;

    for (di = 0; di < 4; ++di)
        fa_dims[di] = 0;

    if (ast_index_reversed_pointer_expr_elem_type(n, &val_type)) {
        gen_pointer_expr_ast(n->b, &cur_type, &no_deref);
        elem_size = type_index_elem_size(cur_type);
        if (n->a->kind == AST_INT_LIT) {
            emit_add_const_to_hl(n->a->ival * elem_size);
        } else {
            emit("\tpush hl\n");
            gen_index_subscript_expr_ast(n->a);
            scale_hl_by_elem_size(elem_size);
            emit("\tex de,hl\n");
            emit("\tpop hl\n");
            emit("\tadd hl,de\n");
        }
        *out_val_type = val_type;
        return;
    }

    if (ast_index_symbol_nd_addressable_addr(n)) {
        const struct AstNode *idxs[MAX_INDEX_DEPTH];
        struct Sym *ns;
        int count;
        int idx;
        int first = 0;
        ast_index_symbol_nd_collect(n, &ns, idxs, &count);
        /* Symbol-base fast path for the first subscript of a global/static
         * array: scale the index and add `ld de,SYM` instead of push/ex/pop. */
        if (count > 0 && ns->is_array &&
            emit_array_symbase_index(ns, idxs[0],
                                     sym_array_index_elem_size(ns, 0))) {
            cur_type = ns->type;
            first = 1;
        } else {
            emit_load_sym_addr(ns);
            cur_type = ns->type;
            if (!ns->is_array && type_ptr_depth(cur_type) > 0)
                emit_load_from_hl(cur_type);
        }
        for (idx = first; idx < count; ++idx) {
            if (ns->is_array)
                elem_size = sym_array_index_elem_size(ns, idx);
            else
                elem_size = sym_pointer_array_index_elem_size(ns, cur_type, idx);
            if (idxs[idx]->kind == AST_INT_LIT) {
                emit_add_const_to_hl(idxs[idx]->ival * elem_size);
            } else {
                emit("\tpush hl\n");
                gen_index_subscript_expr_ast(idxs[idx]);
                if (!(idx == 0 && emit_runtime_pointer_array_stride(ns)))
                    scale_hl_by_elem_size(elem_size);
                emit("\tex de,hl\n");
                emit("\tpop hl\n");
                emit("\tadd hl,de\n");
            }
            if (!ns->is_array)
                cur_type = type_decay_ptr(cur_type);
        }
        *out_val_type = ns->is_array ? ns->type : type_decay_ptr(ns->type);
        return;
    }

    {
        const struct AstNode *idxs[MAX_INDEX_DEPTH];
        const struct AstNode *base;
        struct Sym *ps;
        int count;
        int idx;
        int di2;

        if (ast_index_deref_pointer_array_collect(n, &ps, &base, idxs, &count,
                                                  &val_type)) {
            gen_pointer_expr_ast(base, &cur_type, &no_deref);
            for (idx = 0; idx < count; ++idx) {
                elem_size = type_size(val_type);
                if (elem_size <= 0)
                    elem_size = 2;
                for (di2 = idx + 1; di2 < ps->dim_count; ++di2)
                    if (ps->dims[di2] > 0)
                        elem_size *= ps->dims[di2];
                if (idxs[idx]->kind == AST_INT_LIT) {
                    emit_add_const_to_hl(idxs[idx]->ival * elem_size);
                } else {
                    emit("\tpush hl\n");
                    gen_index_subscript_expr_ast(idxs[idx]);
                    if (!(idx == 0 && emit_runtime_pointer_array_stride(ps)))
                        scale_hl_by_elem_size(elem_size);
                    emit("\tex de,hl\n");
                    emit("\tpop hl\n");
                    emit("\tadd hl,de\n");
                }
            }
            *out_val_type = val_type;
            return;
        }
    }

    if (ast_index_2d_addressable_addr(n)) {
        const struct AstNode *outer = n->a;
        s = find_sym(outer->a->sval);
        elem_size = sym_array_index_elem_size(s, 0);
        if (!emit_array_symbase_index(s, outer->b, elem_size)) {
            emit_load_sym_addr(s);
            if (outer->b->kind == AST_INT_LIT) {
                emit_add_const_to_hl(outer->b->ival * elem_size);
            } else {
                emit("\tpush hl\n");
                gen_index_subscript_expr_ast(outer->b);
                scale_hl_by_elem_size(elem_size);
                emit("\tex de,hl\n");
                emit("\tpop hl\n");
                emit("\tadd hl,de\n");
            }
        }
        elem_size = sym_array_index_elem_size(s, 1);
        if (n->b->kind == AST_INT_LIT) {
            emit_add_const_to_hl(n->b->ival * elem_size);
        } else {
            emit("\tpush hl\n");
            gen_index_subscript_expr_ast(n->b);
            scale_hl_by_elem_size(elem_size);
            emit("\tex de,hl\n");
            emit("\tpop hl\n");
            emit("\tadd hl,de\n");
        }
        *out_val_type = s->type;
        return;
    }

    {
        const struct AstNode *member;
        const struct AstNode *idxs[4];
        int count;
        int dimc;
        int dims[4];
        int idx;

        if (ast_index_member_array_nd_collect(n, &member, idxs, &count, &val_type)) {
            gen_member_addr_ast(member, &cur_type);
            dimc = current_field_array_dim_count;
            for (di = 0; di < 4; ++di)
                dims[di] = current_field_array_dims[di];
            for (idx = 0; idx < count; ++idx) {
                elem_size = ast_field_array_index_stride(type_size(cur_type), dimc,
                                                         dims, idx);
                if (idxs[idx]->kind == AST_INT_LIT) {
                    emit_add_const_to_hl(idxs[idx]->ival * elem_size);
                } else {
                    emit("\tpush hl\n");
                    gen_index_subscript_expr_ast(idxs[idx]);
                    scale_hl_by_elem_size(elem_size);
                    emit("\tex de,hl\n");
                    emit("\tpop hl\n");
                    emit("\tadd hl,de\n");
                }
            }
            *out_val_type = val_type;
            return;
        }
    }

    if (n->a != NULL && n->a->kind == AST_INDEX &&
        n->a->a != NULL && n->a->a->kind == AST_MEMBER) {
        const struct AstNode *outer = n->a;
        int dimc;
        int dims[4];
        if (ast_index_2d_array_elem_type(n, &val_type)) {
            gen_member_addr_ast(outer->a, &cur_type);
            dimc = current_field_array_dim_count;
            for (di = 0; di < 4; ++di)
                dims[di] = current_field_array_dims[di];
            elem_size = ast_field_array_index_stride(type_size(cur_type), dimc, dims, 0);
            if (outer->b->kind == AST_INT_LIT) {
                emit_add_const_to_hl(outer->b->ival * elem_size);
            } else {
                emit("\tpush hl\n");
                gen_index_subscript_expr_ast(outer->b);
                scale_hl_by_elem_size(elem_size);
                emit("\tex de,hl\n");
                emit("\tpop hl\n");
                emit("\tadd hl,de\n");
            }
            elem_size = ast_field_array_index_stride(type_size(cur_type), dimc, dims, 1);
            if (n->b->kind == AST_INT_LIT) {
                emit_add_const_to_hl(n->b->ival * elem_size);
            } else {
                emit("\tpush hl\n");
                gen_index_subscript_expr_ast(n->b);
                scale_hl_by_elem_size(elem_size);
                emit("\tex de,hl\n");
                emit("\tpop hl\n");
                emit("\tadd hl,de\n");
            }
            *out_val_type = val_type;
            return;
        }
    }

    if (ast_index_pointer_expr_elem_type(n, &val_type)) {
        gen_pointer_expr_ast(n->a, &cur_type, &no_deref);
        elem_size = type_index_elem_size(cur_type);
        if (n->b->kind == AST_INT_LIT) {
            emit_add_const_to_hl(n->b->ival * elem_size);
        } else {
            emit("\tpush hl\n");
            gen_index_subscript_expr_ast(n->b);
            scale_hl_by_elem_size(elem_size);
            emit("\tex de,hl\n");
            emit("\tpop hl\n");
            emit("\tadd hl,de\n");
        }
        *out_val_type = val_type;
        return;
    }

    if (n->a->kind == AST_MEMBER) {
        gen_member_addr_ast(n->a, &val_type);
        cur_type = val_type;
        if (type_ptr_depth(cur_type) > 0 && current_field_array_dim_count == 0) {
            emit_load_from_hl(cur_type);
            member_pointer = 1;
        } else {
            field_array = 1;
            fa_dimc = current_field_array_dim_count;
            for (di = 0; di < 4; ++di)
                fa_dims[di] = current_field_array_dims[di];
        }
    } else {
        s = find_sym(n->a->sval);

        /* Base load: a global pointer immediately subscripted loads its value with
         * a direct ld hl,(nn); an ix-direct local/param pointer loads its value
         * directly too (ld l,(ix+d)/ld h,(ix+d+1)) instead of computing its
         * frame address and then dereferencing it; arrays and any other
         * pointer load their address. */
        if (is_global_word_sym(s) && !s->is_array && type_ptr_depth(s->type) > 0) {
            emit_load_global_word_direct(s);
            global_ptr_preloaded = 1;
        } else if (!s->is_array && type_ptr_depth(s->type) > 0 && s->reg_alloc == REG_BC) {
            emit_load_sym_value_direct(s);
            global_ptr_preloaded = 1;
        } else if (!s->is_array && type_ptr_depth(s->type) > 0 && sym_can_ix_direct(s)) {
            emit_load_sym_value_direct(s);
            global_ptr_preloaded = 1;
        } else {
            emit_load_sym_addr(s);
        }

        val_type = s->type;
        cur_type = val_type;

        /* A pointer variable is dereferenced to its target before indexing; an
         * array symbol already denotes the address of its first element. */
        if (!s->is_array && type_ptr_depth(cur_type) > 0) {
            if (!global_ptr_preloaded)
                emit_load_from_hl(cur_type);
        }
    }

    if (field_array)
        elem_size = ast_field_array_index_stride(type_size(cur_type), fa_dimc, fa_dims, 0);
    else if (member_pointer)
        elem_size = type_index_elem_size(cur_type);
    else if (s->is_array)
        elem_size = sym_array_index_elem_size(s, 0);
    else
        elem_size = sym_pointer_array_index_elem_size(s, cur_type, 0);

    if (n->b->kind == AST_INT_LIT) {
        emit_add_const_to_hl(n->b->ival * elem_size);
    } else {
        emit("\tpush hl\n");
        gen_index_subscript_expr_ast(n->b); /* index -> HL */
        scale_hl_by_elem_size(elem_size);
        emit("\tex de,hl\n");
        emit("\tpop hl\n");
        emit("\tadd hl,de\n");        /* HL = element address */
    }

    if (field_array)
        val_type = cur_type;
    else if (member_pointer)
        val_type = type_decay_ptr(cur_type);
    else if (s->is_array)
        val_type = cur_type;          /* element type of the 1-D array */
    else
        val_type = type_decay_ptr(cur_type);

    *out_val_type = val_type;
}

/* Emit a plain-int subscript read `base[index]` via the IDENTIFIER-ROOTED
 * subscript machine (NOT the postfix chain - the two use different base loads
 * and element-size helpers).  The gate (ast_index_plain_int_read) guarantees a
 * bare-identifier base that is a 1-D plain-int array or an int* pointer, with a
 * supported, non-constant plain-int index, so exactly one subscript iteration
 * runs and the element load is a plain 16-bit load. */
void gen_index_ast(const struct AstNode *n)
{
    int val_type;

    gen_index_addr_ast(n, &val_type);
    g_expr_type = val_type;
    emit_load_from_hl(val_type);
    if (current_field_bit_width > 0)
        emit_extract_bitfield();
}

void gen_call_star_indirect_ast(const struct AstNode *n)
{
    const struct AstNode *base;
    struct Sym *proto;
    int callee_type;
    int arg_bytes;
    int old_dead;
    int i;

    base = ast_call_star_indirect_base(n);
    proto = ast_indirect_call_proto_sym(n);
    arg_bytes = 0;

    old_dead = expr_result_dead;
    expr_result_dead = 0;
    if (base->kind == AST_INDEX) {
        int elem_type;
        if (!ast_index_lvalue_elem_type(base, &elem_type) ||
            type_ptr_depth(elem_type) <= 0 || type_size(elem_type) != 2)
            elem_type = TYPE_INT | TYPE_PTR;
        callee_type = elem_type;
        gen_index_addr_ast(base, &elem_type);
        emit_load_from_hl(elem_type);
        g_expr_type = elem_type;
    } else {
        ast_gen_expr(base);
        callee_type = g_expr_type;
    }
    emit("\tpush hl\n");
    for (i = n->list_len - 1; i >= 0; --i) {
        int actual_type;
        int want_type;
        int have_want;
        int ptr_type;
        int no_deref;

        have_want = expected_arg_type(proto, i, &want_type);
        if (ast_pointer_expr_type(n->list[i], &ptr_type, &no_deref))
            gen_pointer_expr_ast(n->list[i], &ptr_type, &no_deref);
        else
            ast_gen_expr(n->list[i]);
        actual_type = g_expr_type;
        if (have_want && type_is_float(want_type)) {
            if (!type_is_float(actual_type))
                emit_convert_int_to_float(actual_type);
            emit("\tpush de\n\tpush hl\n");
            arg_bytes += 4;
        } else if (have_want && type_is_long(want_type)) {
            if (!type_is_long(actual_type))
                emit_promote_int_to_long(actual_type, want_type);
            emit("\tpush de\n\tpush hl\n");
            arg_bytes += 4;
        } else if (have_want) {
            if (type_is_float(actual_type))
                emit_convert_float_to_intlike(want_type);
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

    emit_call_hl_from_stack_offset(arg_bytes);
    emit_cleanup_stack_bytes(arg_bytes + 2);
    g_expr_type = type_decay_ptr(callee_type);
    g_long_from16 = 0;
}

/* Emit a direct named call `f(a, b, ...)`: the C89 implicit-declaration side
 * effect, reverse-order argument evaluation and push (one 16-bit word each),
 * the deferred-EXTRN bookkeeping, `call <asm-name>`, and the stack cleanup.
 * The gate guarantees a direct (non function pointer) callee, all-plain-int
 * arguments and no prototype-driven widening, so each argument pushes exactly
 * one word and none of the builtin fast paths apply. */
static int inline_arg_reusable(const struct AstNode *n)
{
    if (n == NULL)
        return 0;
    return n->kind == AST_INT_LIT || n->kind == AST_STR_LIT ||
           n->kind == AST_SIZEOF_EXPR || n->kind == AST_SIZEOF_TYPE ||
           n->kind == AST_IDENT;
}

static int inline_param_index_for_call(struct Sym *fn, const char *name)
{
    int i;
    if (fn == NULL || name == NULL)
        return -1;
    for (i = 0; i < fn->proto_nargs && i < MAX_PROTO_PARAMS; ++i)
        if (!strcmp(fn->inline_param_names[i], name))
            return i;
    return -1;
}

static void inline_temp_name_for_call(char *dst, int dstsz, int index)
{
    sprintf(dst, "#itmp%d", index);
    (void)dstsz;
}

static const struct AstNode *inline_substitution_body(struct Sym *fn)
{
    if (fn == NULL)
        return NULL;
    if (fn->inline_return_expr != NULL)
        return fn->inline_return_expr;
    if (fn->inline_stmt_expr != NULL)
        return fn->inline_stmt_expr;
    return fn->inline_stmt_body;
}

static int inline_expr_contains_inline_call(const struct AstNode *n)
{
    int i;

    if (n == NULL)
        return 0;
    if (n->kind == AST_CALL && n->a != NULL && n->a->kind == AST_IDENT) {
        struct Sym *s;
        s = find_global(n->a->sval);
        if (s != NULL && s->is_static && s->is_inline && inline_substitution_body(s) != NULL)
            return 1;
    }
    if (inline_expr_contains_inline_call(n->a) || inline_expr_contains_inline_call(n->b) ||
        inline_expr_contains_inline_call(n->c) || inline_expr_contains_inline_call(n->d))
        return 1;
    for (i = 0; i < n->list_len; ++i)
        if (inline_expr_contains_inline_call(n->list[i]))
            return 1;
    return 0;
}

static int inline_call_needs_arg_temps(const struct AstNode *n, struct Sym *fn)
{
    int i;

    for (i = 0; i < n->list_len; ++i)
        if (fn->inline_param_use_count[i] > 1 && !inline_arg_reusable(n->list[i]))
            return 1;
    return 0;
}

static void emit_inline_arg_temp_store(struct Sym *tmp, const struct AstNode *arg,
                                       int want_type)
{
    int actual_type;
    int ptr_type;
    int no_deref;

    tmp->type = want_type;
    if (ast_pointer_expr_type(arg, &ptr_type, &no_deref))
        gen_pointer_expr_ast(arg, &ptr_type, &no_deref);
    else
        ast_gen_expr(arg);

    actual_type = g_expr_type;
    if (type_is_float(actual_type))
        emit_convert_float_to_intlike(want_type);
    else if (type_size(want_type) > 1 && !type_is_long(actual_type))
        emit_promote_byte_to_int(actual_type);
    emit_store_hl_to_sym_direct(tmp);
}

static int emit_inline_arg_temps(const struct AstNode *n, struct Sym *fn,
                                 const char **temp_names,
                                 char temp_name_buf[MAX_PROTO_PARAMS][64])
{
    int i;

    if (inline_expr_contains_inline_call(inline_substitution_body(fn)))
        return 0;

    for (i = 0; i < n->list_len; ++i) {
        struct Sym *tmp;
        int want_type;

        inline_temp_name_for_call(temp_name_buf[i], 64, i);
        tmp = find_local(temp_name_buf[i]);
        if (tmp == NULL)
            return 0;
        want_type = fn->proto_types[i] ? fn->proto_types[i] : TYPE_INT;
        if (type_size(want_type) != 2 || type_is_float(want_type) || type_is_long(want_type))
            return 0;
        tmp->type = want_type;
        temp_names[i] = temp_name_buf[i];
    }

    for (i = n->list_len - 1; i >= 0; --i) {
        struct Sym *tmp;
        int want_type;

        tmp = find_local(temp_name_buf[i]);
        want_type = fn->proto_types[i] ? fn->proto_types[i] : TYPE_INT;
        emit_inline_arg_temp_store(tmp, n->list[i], want_type);
    }
    return 1;
}

static struct AstNode *clone_inline_expr(struct AstArena *ar, struct Sym *fn,
                                         const struct AstNode *src,
                                         const struct AstNode *call,
                                         const char **temp_names)
{
    struct AstNode *dst;
    int i;

    if (src == NULL)
        return NULL;
    if (src->kind == AST_IDENT) {
        i = inline_param_index_for_call(fn, src->sval);
        if (i >= 0 && i < call->list_len && temp_names != NULL && temp_names[i] != NULL) {
            dst = ast_new(ar, AST_IDENT);
            dst->type = src->type;
            dst->sval = ast_arena_strdup(ar, temp_names[i]);
            dst->line = src->line;
            return dst;
        }
        if (i >= 0 && i < call->list_len)
            return call->list[i];
    }

    dst = ast_new(ar, src->kind);
    dst->type = src->type;
    dst->op = src->op;
    dst->ival = src->ival;
    dst->uval = src->uval;
    dst->str_index = src->str_index;
    dst->sym = src->sym;
    if (src->sval == NULL)
        dst->sval = NULL;
    else if (src->kind == AST_STR_LIT)
        dst->sval = ast_arena_memdup(ar, src->sval, (int)src->uval);
    else
        dst->sval = ast_arena_strdup(ar, src->sval);
    dst->peek_type = src->peek_type;
    dst->line = src->line;

    dst->a = clone_inline_expr(ar, fn, src->a, call, temp_names);
    dst->b = clone_inline_expr(ar, fn, src->b, call, temp_names);
    dst->c = clone_inline_expr(ar, fn, src->c, call, temp_names);
    dst->d = clone_inline_expr(ar, fn, src->d, call, temp_names);
    for (i = 0; i < src->list_len; ++i)
        ast_list_push(ar, dst, clone_inline_expr(ar, fn, src->list[i], call, temp_names));
    return dst;
}

static int g_inline_expand_depth;

static int try_gen_inline_call_ast(const struct AstNode *n, struct Sym *fn_sym)
{
    struct AstNode *expr;
    struct AstNode *stmt;
    const struct AstNode *src_expr;
    const char *temp_names[MAX_PROTO_PARAMS];
    char temp_name_buf[MAX_PROTO_PARAMS][64];
    int i;

    if (opt_debug || fn_sym == NULL || !fn_sym->is_static || !fn_sym->is_inline ||
        inline_substitution_body(fn_sym) == NULL)
        return 0;
    if ((fn_sym->inline_stmt_expr != NULL || fn_sym->inline_stmt_body != NULL) &&
        !expr_result_dead)
        return 0;
    if (g_inline_expand_depth >= 8)
        return 0;
    if (n->list_len != fn_sym->proto_nargs || n->list_len > MAX_PROTO_PARAMS)
        return 0;
    for (i = 0; i < MAX_PROTO_PARAMS; ++i)
        temp_names[i] = NULL;

    if (inline_call_needs_arg_temps(n, fn_sym) &&
        !emit_inline_arg_temps(n, fn_sym, temp_names, temp_name_buf))
        return 0;

    g_inline_expand_depth++;
    src_expr = inline_substitution_body(fn_sym);
    if (fn_sym->inline_stmt_body != NULL) {
        stmt = clone_inline_expr(&g_ast_arena, fn_sym, src_expr, n, temp_names);
        ast_gen_stmt(stmt);
    } else {
        expr = clone_inline_expr(&g_ast_arena, fn_sym, src_expr, n, temp_names);
        ast_gen_expr(expr);
    }
    g_inline_expand_depth--;
    g_expr_type = (fn_sym->inline_stmt_expr != NULL || fn_sym->inline_stmt_body != NULL) ?
                  TYPE_VOID : fn_sym->type;
    g_long_from16 = 0;
    return 1;
}

/* Evaluate one fastcall argument the same way the general per-argument
 * call-evaluation loop below does: a pointer-typed expression (an array
 * decaying to its address, e.g. `names[nn++]` from a 2D array) must go
 * through gen_pointer_expr_ast, not plain ast_gen_expr, or it gets
 * evaluated as a value-context dereference instead of an address - a real
 * miscompile (crash: "not-implemented z80 instruction 0xdd") caught by a
 * strcpy(names[nn++], text) call in tests/pint.c's identifier table,
 * which every fastcall special case below had been skipping since none of
 * their own hand-written tests happened to pass a 2D-array-row argument. */
static void gen_fastcall_arg(const struct AstNode *arg)
{
    int ptr_type;
    int no_deref;

    if (ast_pointer_expr_type(arg, &ptr_type, &no_deref))
        gen_pointer_expr_ast(arg, &ptr_type, &no_deref);
    else
        ast_gen_expr(arg);
}

void gen_call_ast(const struct AstNode *n)
{
    const char *name;
    struct Sym *fn_sym;
    int arg_bytes = 0;
    int old_dead;
    int i;

    if (ast_call_star_indirect_supported(n)) {
        gen_call_star_indirect_ast(n);
        return;
    }

    if (ast_call_indirect_supported(n)) {
        int callee_type;
        int no_deref;
        struct Sym *proto;
        proto = ast_indirect_call_proto_sym(n);
        gen_pointer_expr_ast(n->a, &callee_type, &no_deref);
        emit("\tpush hl\n");
        old_dead = expr_result_dead;
        expr_result_dead = 0;
        for (i = n->list_len - 1; i >= 0; --i) {
            int actual_type;
            int want_type;
            int have_want;
            int ptr_type;
            int arg_no_deref;

            have_want = expected_arg_type(proto, i, &want_type);
            if (ast_pointer_expr_type(n->list[i], &ptr_type, &arg_no_deref))
                gen_pointer_expr_ast(n->list[i], &ptr_type, &arg_no_deref);
            else
                ast_gen_expr(n->list[i]);
            actual_type = g_expr_type;
            if (have_want && type_is_float(want_type)) {
                if (!type_is_float(actual_type))
                    emit_convert_int_to_float(actual_type);
                emit("\tpush de\n\tpush hl\n");
                arg_bytes += 4;
            } else if (have_want && type_is_long(want_type)) {
                if (!type_is_long(actual_type))
                    emit_promote_int_to_long(actual_type, want_type);
                emit("\tpush de\n\tpush hl\n");
                arg_bytes += 4;
            } else if (have_want) {
                if (type_is_float(actual_type))
                    emit_convert_float_to_intlike(want_type);
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
        emit_call_hl_from_stack_offset(arg_bytes);
        g_expr_type = type_decay_ptr(callee_type);
        g_long_from16 = 0;
        emit_cleanup_stack_bytes(arg_bytes + 2);
        return;
    }

    name = n->a->sval;
    fn_sym = find_global(name);

    if (try_gen_inline_call_ast(n, fn_sym))
        return;

    /* va_start(ap, last) / va_end(ap) builtins: emit the __va_start /
     * __va_end address arithmetic.  va_start sets ap to the
     * address just past the last fixed parameter; va_end clears ap. */
    if (ast_va_builtin_supported(n)) {
        struct Sym *ap = find_sym(n->list[0]->sval);
        if (!strcmp(name, "__va_start")) {
            struct Sym *last = find_sym(n->list[1]->sval);
            int sz = type_size(last->type);
            if (sz < 2)
                sz = 2;
            emit_load_sym_addr(last);       /* HL = &last */
            emit_add_const_to_hl(sz);       /* HL = first unnamed arg */
            emit("\tex de,hl\n");
            emit_load_sym_addr(ap);         /* HL = &ap */
            emit_store_de_to_addr_hl(ap->type);
        } else {                            /* __va_end */
            emit("\tld de,0\n");
            emit_load_sym_addr(ap);         /* HL = &ap */
            emit_store_de_to_addr_hl(ap->type);
        }
        emit("\tld hl,0\n");
        g_expr_type = TYPE_INT;
        g_long_from16 = 0;
        return;
    }


    if (fn_sym == NULL) {
        fn_sym = add_global(name, TYPE_INT, SC_FUNC);
        fn_sym->needs_extrn = 1;
    }

    /* Fastcall strlen(p): DCCRTL's __slf takes s directly in HL and returns
     * the length in HL, skipping the push-arg/call/pop-arg dance the general
     * path below would use to call __slen. strlen is common enough in hot
     * code (e.g. inside loops) that this pays off broadly. asm_name_for
     * already treats a bare "strlen" call as the runtime function
     * unconditionally (see dcc_asmname.c), so this makes the same
     * assumption. */
    if (n->list_len == 1 && !strcmp(name, "strlen")) {
        old_dead = expr_result_dead;
        expr_result_dead = 0;
        gen_fastcall_arg(n->list[0]);       /* HL = s */
        expr_result_dead = old_dead;
        emit_runtime_call("__slf");
        g_expr_type = fn_sym->type;
        g_long_from16 = 0;
        return;
    }

    /* Fastcall strchr(p,c): DCCRTL's __chf takes s in HL and c's low byte in
     * A, returning the match (or 0) in HL - same rationale as strlen above. */
    if (n->list_len == 2 && !strcmp(name, "strchr")) {
        old_dead = expr_result_dead;
        expr_result_dead = 0;
        gen_fastcall_arg(n->list[0]);       /* HL = s */
        emit("\tpush hl\n");
        gen_fastcall_arg(n->list[1]);       /* HL = c */
        expr_result_dead = old_dead;
        emit("\tld a,l\n");
        emit("\tpop hl\n");
        emit_runtime_call("__chf");
        g_expr_type = fn_sym->type;
        g_long_from16 = 0;
        return;
    }

    /* Fastcall memcmp(s1,s2,n): DCCRTL's __cmpf takes s1 in DE, s2 in HL,
     * n in BC directly - skipping both the general push-3-args/call/pop-3
     * convention this call would otherwise use, and __mcmp's own ~20-
     * instruction stack-argument-reconstruction prologue (see DCCRTL.MAC).
     * Same rationale as strlen/strchr above, extended to three arguments:
     * each earlier argument is pushed while the next is evaluated, then
     * unwound directly into the target registers instead of the stack. */
    if (n->list_len == 3 && !strcmp(name, "memcmp")) {
        old_dead = expr_result_dead;
        expr_result_dead = 0;
        gen_fastcall_arg(n->list[0]);       /* HL = s1 */
        emit("\tpush hl\n");
        gen_fastcall_arg(n->list[1]);       /* HL = s2 */
        emit("\tpush hl\n");
        gen_fastcall_arg(n->list[2]);       /* HL = n */
        expr_result_dead = old_dead;
        emit("\tld b,h\n\tld c,l\n");   /* BC = n */
        emit("\tpop hl\n");             /* HL = s2 */
        emit("\tpop de\n");             /* DE = s1 */
        emit_runtime_call("__cmpf");
        g_expr_type = fn_sym->type;
        g_long_from16 = 0;
        return;
    }

    /* Fastcall memset(dest,c,count): DCCRTL's __msf takes dest in HL, the
     * fill byte in E, count in BC directly - skipping the general
     * push-3-args/call/pop-3 convention and __mset's own ~10-instruction
     * stack-argument-reconstruction prologue (see DCCRTL.MAC). Same
     * rationale as memcmp above. memset is common enough (buffer/struct
     * zeroing) that this pays off broadly.
     *
     * Named __msf, not __msetf: M80 truncates public symbols to 6
     * significant characters, and __msetf truncates to __MSET, colliding
     * with __mset's own name (a real "%Mult. Def. Global __MSET" linker
     * warning plus a miscompile - call sites got linked to __mset's
     * stack-marshaling entry instead). */
    if (n->list_len == 3 && !strcmp(name, "memset")) {
        old_dead = expr_result_dead;
        expr_result_dead = 0;
        gen_fastcall_arg(n->list[0]);       /* HL = dest */
        emit("\tpush hl\n");
        gen_fastcall_arg(n->list[1]);       /* HL = c */
        emit("\tpush hl\n");
        gen_fastcall_arg(n->list[2]);       /* HL = count */
        expr_result_dead = old_dead;
        emit("\tld b,h\n\tld c,l\n");   /* BC = count */
        emit("\tpop de\n");             /* E = fill byte (low byte of c) */
        emit("\tpop hl\n");             /* HL = dest */
        emit_runtime_call("__msf");
        g_expr_type = fn_sym->type;
        g_long_from16 = 0;
        return;
    }

    /* Fastcall bdos(fn,dearg): DCCRTL's __bdosf takes fn's low byte in C and
     * dearg in DE directly, skipping the general push-2-args/call/pop-2
     * convention this call would otherwise use - same rationale as
     * strlen/strchr/memcmp above. BDOS calls are frequent enough in CP/M
     * programs (console/file I/O) that this pays off broadly. */
    if (n->list_len == 2 && !strcmp(name, "bdos")) {
        old_dead = expr_result_dead;
        expr_result_dead = 0;
        gen_fastcall_arg(n->list[0]);       /* HL = fn */
        emit("\tpush hl\n");
        gen_fastcall_arg(n->list[1]);       /* HL = dearg */
        expr_result_dead = old_dead;
        emit("\tex de,hl\n");           /* DE = dearg */
        emit("\tpop hl\n");             /* HL = fn */
        emit("\tld c,l\n");             /* C = fn low byte */
        emit_runtime_call("__bdosf");
        g_expr_type = fn_sym->type;
        g_long_from16 = 0;
        return;
    }

    /* Fastcall bdoshl(fn,dearg): same shape as bdos above, but calls
     * DCCRTL's __bhlf, which returns HL exactly as BDOS left it instead
     * of zero-extending A into HL. For BDOS functions whose useful result
     * is a 16-bit HL value rather than a byte in A. */
    if (n->list_len == 2 && !strcmp(name, "bdoshl")) {
        old_dead = expr_result_dead;
        expr_result_dead = 0;
        gen_fastcall_arg(n->list[0]);       /* HL = fn */
        emit("\tpush hl\n");
        gen_fastcall_arg(n->list[1]);       /* HL = dearg */
        expr_result_dead = old_dead;
        emit("\tex de,hl\n");           /* DE = dearg */
        emit("\tpop hl\n");             /* HL = fn */
        emit("\tld c,l\n");             /* C = fn low byte */
        emit_runtime_call("__bhlf");
        g_expr_type = fn_sym->type;
        g_long_from16 = 0;
        return;
    }

    /* Fastcall bios(fn,dearg): same shape as bdos above, but calls
     * DCCRTL's __biosf, which invokes the CP/M BIOS jump table instead of
     * BDOS's fixed CALL 5. See DCCRTL.MAC for why fn/dearg still land in
     * C/DE despite the BIOS having no single argument convention. */
    if (n->list_len == 2 && !strcmp(name, "bios")) {
        old_dead = expr_result_dead;
        expr_result_dead = 0;
        gen_fastcall_arg(n->list[0]);       /* HL = fn */
        emit("\tpush hl\n");
        gen_fastcall_arg(n->list[1]);       /* HL = dearg */
        expr_result_dead = old_dead;
        emit("\tex de,hl\n");           /* DE = dearg */
        emit("\tpop hl\n");             /* HL = fn */
        emit("\tld c,l\n");             /* C = fn low byte */
        emit_runtime_call("__biosf");
        g_expr_type = fn_sym->type;
        g_long_from16 = 0;
        return;
    }

    /* Fastcall bioshl(fn,dearg): same shape as bios above, but calls
     * DCCRTL's __bhf, which returns HL exactly as the BIOS call left it
     * instead of zero-extending A into HL. For BIOS functions whose useful
     * result is a 16-bit HL value (e.g. SELDSK, SECTRAN) rather than a
     * byte in A. */
    if (n->list_len == 2 && !strcmp(name, "bioshl")) {
        old_dead = expr_result_dead;
        expr_result_dead = 0;
        gen_fastcall_arg(n->list[0]);       /* HL = fn */
        emit("\tpush hl\n");
        gen_fastcall_arg(n->list[1]);       /* HL = dearg */
        expr_result_dead = old_dead;
        emit("\tex de,hl\n");           /* DE = dearg */
        emit("\tpop hl\n");             /* HL = fn */
        emit("\tld c,l\n");             /* C = fn low byte */
        emit_runtime_call("__bhf");
        g_expr_type = fn_sym->type;
        g_long_from16 = 0;
        return;
    }

    /* Fastcall memcpy(dst,src,n): DCCRTL's __mcf takes dst in DE, src in HL,
     * n in BC directly - same rationale/shape as memcmp above, extended to
     * memcpy's copy-not-compare semantics. memcpy is common enough
     * (struct/buffer copies) that this pays off broadly. */
    if (n->list_len == 3 && !strcmp(name, "memcpy")) {
        old_dead = expr_result_dead;
        expr_result_dead = 0;
        gen_fastcall_arg(n->list[0]);       /* HL = dst */
        emit("\tpush hl\n");
        gen_fastcall_arg(n->list[1]);       /* HL = src */
        emit("\tpush hl\n");
        gen_fastcall_arg(n->list[2]);       /* HL = n */
        expr_result_dead = old_dead;
        emit("\tld b,h\n\tld c,l\n");   /* BC = n */
        emit("\tpop hl\n");             /* HL = src */
        emit("\tpop de\n");             /* DE = dst */
        emit_runtime_call("__mcf");
        g_expr_type = fn_sym->type;
        g_long_from16 = 0;
        return;
    }

    /* Fastcall memchr(s,c,n): DCCRTL's __mhf takes s in HL, c's low byte in
     * E, n in BC directly - same rationale/shape as memset above. */
    if (n->list_len == 3 && !strcmp(name, "memchr")) {
        old_dead = expr_result_dead;
        expr_result_dead = 0;
        gen_fastcall_arg(n->list[0]);       /* HL = s */
        emit("\tpush hl\n");
        gen_fastcall_arg(n->list[1]);       /* HL = c */
        emit("\tpush hl\n");
        gen_fastcall_arg(n->list[2]);       /* HL = n */
        expr_result_dead = old_dead;
        emit("\tld b,h\n\tld c,l\n");   /* BC = n */
        emit("\tpop de\n");             /* E = c (low byte) */
        emit("\tpop hl\n");             /* HL = s */
        emit_runtime_call("__mhf");
        g_expr_type = fn_sym->type;
        g_long_from16 = 0;
        return;
    }

    /* Fastcall strcpy(dst,src): DCCRTL's __scf takes dst in DE, src in HL
     * directly - same rationale as memcmp/bdos above, extended to strcpy's
     * two pointer arguments. */
    if (n->list_len == 2 && !strcmp(name, "strcpy")) {
        old_dead = expr_result_dead;
        expr_result_dead = 0;
        gen_fastcall_arg(n->list[0]);       /* HL = dst */
        emit("\tpush hl\n");
        gen_fastcall_arg(n->list[1]);       /* HL = src */
        expr_result_dead = old_dead;
        emit("\tpop de\n");             /* DE = dst */
        emit_runtime_call("__scf");
        g_expr_type = fn_sym->type;
        g_long_from16 = 0;
        return;
    }

    /* Fastcall strrchr(s,c): DCCRTL's __rcf takes s in HL and c's low byte
     * in A - same shape as strchr above. */
    if (n->list_len == 2 && !strcmp(name, "strrchr")) {
        old_dead = expr_result_dead;
        expr_result_dead = 0;
        gen_fastcall_arg(n->list[0]);       /* HL = s */
        emit("\tpush hl\n");
        gen_fastcall_arg(n->list[1]);       /* HL = c */
        expr_result_dead = old_dead;
        emit("\tld a,l\n");
        emit("\tpop hl\n");
        emit_runtime_call("__rcf");
        g_expr_type = fn_sym->type;
        g_long_from16 = 0;
        return;
    }

    /* Fastcall strstr(haystack,needle): DCCRTL's __ssf takes haystack in
     * DE, needle in HL directly - same shape as strcpy above. */
    if (n->list_len == 2 && !strcmp(name, "strstr")) {
        old_dead = expr_result_dead;
        expr_result_dead = 0;
        gen_fastcall_arg(n->list[0]);       /* HL = haystack */
        emit("\tpush hl\n");
        gen_fastcall_arg(n->list[1]);       /* HL = needle */
        expr_result_dead = old_dead;
        emit("\tpop de\n");             /* DE = haystack */
        emit_runtime_call("__ssf");
        g_expr_type = fn_sym->type;
        g_long_from16 = 0;
        return;
    }

    /* Fastcall stricmp(s1,s2): DCCRTL's __icf takes s1 in DE, s2 in HL
     * directly - same shape as strstr above. */
    if (n->list_len == 2 && !strcmp(name, "stricmp")) {
        old_dead = expr_result_dead;
        expr_result_dead = 0;
        gen_fastcall_arg(n->list[0]);       /* HL = s1 */
        emit("\tpush hl\n");
        gen_fastcall_arg(n->list[1]);       /* HL = s2 */
        expr_result_dead = old_dead;
        emit("\tpop de\n");             /* DE = s1 */
        emit_runtime_call("__icf");
        g_expr_type = fn_sym->type;
        g_long_from16 = 0;
        return;
    }

    /* A real (non-inlined) call to any static function - inline-eligible or
     * not - is what its buffered body's dead-code elimination decision
     * hinges on. */
    if (fn_sym->is_static)
        fn_sym->deferred_body_needed = 1;

    if (expr_result_dead && type_is_struct_object(fn_sym->type)) {
        gen_struct_return_call_arg_ast(n, fn_sym->type);
        emit_cleanup_stack_bytes(type_size(fn_sym->type));
        g_expr_type = fn_sym->type;
        g_long_from16 = 0;
        return;
    }

    /* Push arguments right-to-left, one 16-bit word each (prototype-16-bit /
     * default-int push), with call arguments forced live across evaluation. */
    old_dead = expr_result_dead;
    expr_result_dead = 0;
    for (i = n->list_len - 1; i >= 0; --i) {
        int actual_type;
        int want_type;
        int have_want;
        int ptr_type;
        int no_deref;
        struct Sym *arg_sym;

        have_want = expected_arg_type(fn_sym, i, &want_type);
        if (have_want && type_is_struct_object(want_type)) {
            gen_call_struct_arg_ast(n->list[i], want_type);
            arg_bytes += type_size(want_type);
            continue;
        }
        if (!have_want && n->list[i]->kind == AST_IDENT) {
            arg_sym = find_sym(n->list[i]->sval);
            if (arg_sym != NULL && !arg_sym->is_array &&
                type_is_struct_object(arg_sym->type)) {
                gen_call_struct_arg_ast(n->list[i], arg_sym->type);
                arg_bytes += type_size(arg_sym->type);
                continue;
            }
        }

        if (ast_pointer_expr_type(n->list[i], &ptr_type, &no_deref))
            gen_pointer_expr_ast(n->list[i], &ptr_type, &no_deref);
        else
            ast_gen_expr(n->list[i]);
        actual_type = g_expr_type;
        if (have_want && type_is_float(want_type)) {
            if (!type_is_float(actual_type))
                emit_convert_int_to_float(actual_type);
            emit("\tpush de\n\tpush hl\n");
            arg_bytes += 4;
        } else if (have_want && type_is_long(want_type)) {
            if (!type_is_long(actual_type))
                emit_promote_int_to_long(actual_type, want_type);
            emit("\tpush de\n\tpush hl\n");
            arg_bytes += 4;
        } else if (have_want && !type_is_long(want_type) &&
                   !type_is_float(want_type)) {
            if (type_is_float(actual_type))
                emit_convert_float_to_intlike(want_type);
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

    {
        int fmt_idx = asm_printf_family_fmt_arg_index(name);
        if (fmt_idx >= 0 && fmt_idx < n->list_len) {
            /* printf-family call: pick the runtime entry point this specific
             * call needs from its own format-string literal (or the
             * -ffloatio/-flongio override, or conservatively assume both
             * when the format isn't a compile-time-visible literal) - see
             * asm_name_for_pf_call. emit_runtime_call (not the usual Sym-
             * based emit_extrn_if_needed/asm_name_for pairing) is used
             * because two call sites for the same C function can legitimately
             * resolve to two different runtime entry points within one file
             * (e.g. one sprintf() call with no %f, another with %.2f), and
             * each one needs its own matching extrn. */
            int needs_float = 0;
            int needs_long = 0;
            int needs_hex = 0;
            int needs_octal = 0;
            const struct AstNode *fmt_arg = n->list[fmt_idx];

            if (fmt_arg->kind == AST_STR_LIT)
                asm_scan_format_specifiers(fmt_arg->sval, &needs_float, &needs_long,
                                            &needs_hex, &needs_octal);
            else {
                needs_float = 1;
                needs_long = 1;
                needs_hex = 1;
                needs_octal = 1;
            }
            /* -f<x>io/-fno-<x>io are a blanket override: they win over
             * whatever the literal scan (or the non-literal conservative
             * fallback above) concluded, in either direction. This is the
             * only way to claw back the fallback's "assume everything"
             * cost for a format string that isn't a compile-time literal -
             * see opt_floatio's declaration in dcc.h. */
            if (opt_floatio > 0) needs_float = 1; else if (opt_floatio < 0) needs_float = 0;
            if (opt_longio > 0) needs_long = 1; else if (opt_longio < 0) needs_long = 0;
            if (opt_hexio > 0) needs_hex = 1; else if (opt_hexio < 0) needs_hex = 0;
            if (opt_octio > 0) needs_octal = 1; else if (opt_octio < 0) needs_octal = 0;
            /* %x/%X and %o don't need a variant per printf-family function
             * (unlike float/long): the hook they install is independent of,
             * and composes freely with, whichever entry point the float/
             * long decision above resolves to - see __pfehx/__pfeoc. Names
             * deliberately differ within their first 6 characters (M80
             * truncates PUBLIC symbols there) - __pf_ehx/__pf_eoc collided
             * as "__PF_E" and caused a real miscompilation before this. */
            if (needs_hex)
                emit_runtime_call("__pfehx");
            if (needs_octal)
                emit_runtime_call("__pfeoc");
            emit_runtime_call(asm_name_for_pf_call(name, needs_float, needs_long));
        } else {
            emit_extrn_if_needed(fn_sym);
            fprintf(outf, "\tcall %s\n", asm_name_for(name));
        }
    }
    g_expr_type = fn_sym->type;
    g_long_from16 = 0;

    emit_cleanup_stack_bytes(arg_bytes);
}

void gen_struct_return_call_assign_ast(const struct AstNode *lhs,
                                              const struct AstNode *rhs)
{
    const char *name = rhs->a->sval;
    struct Sym *fn_sym = find_global(name);
    int lhs_type;
    int arg_bytes = 0;
    int old_dead;
    int i;

    /* This emits its own `call` directly rather than going through
     * gen_call_ast, so it needs its own deferred_body_needed marking too. */
    if (fn_sym != NULL && fn_sym->is_static)
        fn_sym->deferred_body_needed = 1;

    if (lhs == NULL) {
        /* `return f(args);` in a struct-returning function: the destination
         * is the caller's own hidden return buffer, whose pointer is the
         * hidden first parameter at (ix+4/5).  Passing it straight through
         * as f's destination writes the result in place - no temp, no copy. */
        if (fn_sym == NULL)
            fatal("struct-return callee missing after support gate");
        emit("\tld l,(ix+4)\n\tld h,(ix+5)\n");
        lhs_type = fn_sym->type;
    } else {
        gen_struct_addr_expr_ast(lhs, &lhs_type);
    }
    emit("\tpush hl\n");

    old_dead = expr_result_dead;
    expr_result_dead = 0;
    for (i = rhs->list_len - 1; i >= 0; --i) {
        int actual_type;
        int want_type;
        int have_want;
        int ptr_type;
        int no_deref;

        have_want = expected_arg_type(fn_sym, i, &want_type);
        if (have_want && type_is_struct_object(want_type)) {
            gen_call_struct_arg_ast(rhs->list[i], want_type);
            arg_bytes += type_size(want_type);
            continue;
        }

        if (ast_pointer_expr_type(rhs->list[i], &ptr_type, &no_deref))
            gen_pointer_expr_ast(rhs->list[i], &ptr_type, &no_deref);
        else
            ast_gen_expr(rhs->list[i]);
        actual_type = g_expr_type;
        if (have_want && type_is_float(want_type)) {
            if (!type_is_float(actual_type))
                emit_convert_int_to_float(actual_type);
            emit("\tpush de\n\tpush hl\n");
            arg_bytes += 4;
        } else if (have_want && type_is_long(want_type)) {
            if (!type_is_long(actual_type))
                emit_promote_int_to_long(actual_type, want_type);
            emit("\tpush de\n\tpush hl\n");
            arg_bytes += 4;
        } else if (have_want && !type_is_long(want_type) &&
                   !type_is_float(want_type)) {
            if (type_is_float(actual_type))
                emit_convert_float_to_intlike(want_type);
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
    g_expr_type = lhs_type;
    g_long_from16 = 0;
}

void gen_struct_addr_expr_ast(const struct AstNode *n, int *out_type)
{
    struct Sym *s;

    switch (n->kind) {
    case AST_IDENT:
        s = n->sym != NULL ? n->sym : find_sym(n->sval);
        emit_load_sym_addr(s);
        *out_type = s->type;
        return;
    case AST_INDEX:
        gen_index_addr_ast(n, out_type);
        return;
    case AST_UNARY:
        gen_deref_addr_ast(n, out_type);
        return;
    case AST_MEMBER:
        gen_member_addr_ast(n, out_type);
        return;
    case AST_COMPOUND_LITERAL:
        /* Materialize the literal into its backing local; gen_compound_literal_ast
         * emits the initializer and leaves the object's address in HL. */
        gen_compound_literal_ast(n);
        if (out_type)
            *out_type = n->type;
        return;
    default:
        fatal("gen_struct_addr_expr_ast: unsupported node");
    }
}

void gen_struct_copy_assign_ast(const struct AstNode *n)
{
    int lhs_type;
    int rhs_type;

    gen_struct_addr_expr_ast(n->a, &lhs_type);  /* HL = destination */
    emit("\tpush hl\n");
    gen_struct_addr_expr_ast(n->b, &rhs_type);  /* HL = source */
    (void)rhs_type;
    emit("\tex de,hl\n\tpop hl\n");
    emit_copy_de_to_hl_bytes(type_size(lhs_type));
    g_expr_type = lhs_type;
    g_long_from16 = 0;
}

void gen_struct_chain_copy_assign_ast(const struct AstNode *n)
{
    const struct AstNode *inner = n->b;
    int lhs_type;
    int rhs_type;

    gen_struct_copy_assign_ast(inner);
    gen_struct_addr_expr_ast(n->a, &lhs_type);
    emit("\tpush hl\n");
    gen_struct_addr_expr_ast(inner->a, &rhs_type);
    (void)rhs_type;
    emit("\tex de,hl\n\tpop hl\n");
    emit_copy_de_to_hl_bytes(type_size(lhs_type));
    g_expr_type = lhs_type;
    g_long_from16 = 0;
}

void gen_struct_deref_copy_assign_ast(const struct AstNode *n)
{
    int lhs_type;
    int rhs_type;

    gen_deref_addr_ast(n->a, &lhs_type);     /* HL = destination */
    emit("\tpush hl\n");
    gen_deref_addr_ast(n->b, &rhs_type);     /* HL = source */
    (void)rhs_type;
    emit("\tex de,hl\n\tpop hl\n");       /* DE = source, HL = dest */
    emit_copy_de_to_hl_bytes(type_size(lhs_type));
    g_expr_type = lhs_type;
    g_long_from16 = 0;
}

void gen_struct_member_copy_assign_ast(const struct AstNode *n)
{
    int lhs_type;
    struct Sym *rhs;

    gen_member_addr_ast(n->a, &lhs_type);  /* HL = destination */
    emit("\tpush hl\n");
    rhs = find_sym(n->b->sval);
    emit_load_sym_addr(rhs);               /* HL = source */
    emit("\tex de,hl\n\tpop hl\n");
    emit_copy_de_to_hl_bytes(type_size(lhs_type));
    g_expr_type = lhs_type;
    g_long_from16 = 0;
}

/* Resolve one side of gen_byte_addr_copy_assign_ast to its address in HL.
 * `n` is already known (via ast_is_byte_addr_lvalue) to be an AST_INDEX,
 * AST_MEMBER, AST_UNARY deref, or a zero-arg static inline call that
 * substitutes to one of those - so a bare call node here is resolved to its
 * inline body first rather than falling into the deref catch-all below. */
static void gen_byte_lvalue_addr_ast(const struct AstNode *n, int *out_type)
{
    const struct AstNode *sub;

    if (n->kind == AST_INDEX) {
        gen_index_addr_ast(n, out_type);
    } else if (n->kind == AST_MEMBER) {
        gen_member_addr_ast(n, out_type);
    } else if (n->kind == AST_UNARY && n->op == '*') {
        gen_deref_addr_ast(n, out_type);
    } else {
        sub = ast_zero_arg_inline_body(n);
        gen_byte_lvalue_addr_ast(sub, out_type);
    }
}

/* Emitter for ast_is_byte_addr_copy_assign: compute both addresses (lhs
 * first, matching the generic non-identifier-lvalue assignment path's
 * evaluation order just below in this file), then copy the single byte
 * directly address-to-address - no promotion to int, no shuffling of the
 * value itself through the stack, just `ld a,(hl)` / `ld (de),a`. */
void gen_byte_addr_copy_assign_ast(const struct AstNode *n)
{
    int val_type;

    gen_byte_lvalue_addr_ast(n->a, &val_type);   /* HL = destination address */
    emit("\tpush hl\n");

    gen_byte_lvalue_addr_ast(n->b, &val_type);   /* HL = source address */

    emit("\tpop de\n");        /* DE = destination address, HL = source address */
    emit("\tld a,(hl)\n");
    emit("\tld (de),a\n");
    g_expr_type = val_type;
    g_long_from16 = 0;
}

/* Emit a single struct field read `id.f` / `id->f` via the identifier-rooted
 * field machine: load the base address, dereference once for `->`, add the
 * field offset, publish the field metadata into the current_field_* globals,
 * then load the scalar value.  The gate guarantees a plain 16-bit int,
 * non-array, non-bitfield field, so the load is a plain load and the
 * bitfield extract never fires. */
void gen_member_addr_ast(const struct AstNode *n, int *out_val_type)
{
    struct Sym *s;
    int arrow = (n->op == TOK_ARROW);
    int cur_type;
    int no_deref;
    struct FieldDef *fd;
    int sid;
    int di;
    int val_type;

    if (arrow && n->a->kind == AST_INDEX &&
        ast_pointer_expr_type(n->a, &cur_type, &no_deref)) {
        gen_pointer_expr_ast(n->a, &cur_type, &no_deref);
    } else if (n->a->kind == AST_INDEX) {
        gen_index_addr_ast(n->a, &cur_type);
    } else if (arrow && n->a->kind != AST_IDENT &&
               ast_pointer_expr_type(n->a, &cur_type, &no_deref)) {
        gen_pointer_expr_ast(n->a, &cur_type, &no_deref);
    } else if (!arrow && n->a->kind == AST_UNARY && n->a->op == '*' &&
               ast_pointer_expr_type(n->a->a, &cur_type, &no_deref)) {
        gen_pointer_expr_ast(n->a->a, &cur_type, &no_deref);
        cur_type = type_decay_ptr(cur_type);
        if ((cur_type & 15) == TYPE_VOID)
            cur_type = TYPE_CHAR;
    } else if (!arrow && n->a->kind == AST_MEMBER) {
        gen_member_addr_ast(n->a, &cur_type);
    } else if (!arrow && n->a->kind == AST_COMPOUND_LITERAL &&
               type_is_struct_object(n->a->type)) {
        gen_compound_literal_ast(n->a);
        cur_type = n->a->type;
    } else if (!arrow && n->a->kind == AST_CALL && n->a->a != NULL &&
               n->a->a->kind == AST_IDENT) {
        struct AstNode lhs;
        struct Sym *fn;
        struct Sym *tmp;

        fn = find_global(n->a->a->sval);
        if (fn == NULL || fn->storage != SC_FUNC ||
            !ast_struct_return_call_assign_supported(fn->type, n->a) ||
            n->sym == NULL)
            fatal("gen_member_addr_ast: unsupported struct-return member base");

        tmp = n->sym;
        memset(&lhs, 0, sizeof(lhs));
        lhs.kind = AST_IDENT;
        lhs.sval = tmp->name;
        lhs.sym = tmp;
        lhs.type = tmp->type;
        gen_struct_return_call_assign_ast(&lhs, n->a);
        emit_load_sym_addr(tmp);
        cur_type = tmp->type;
    } else {
        s = find_sym(n->a->sval);
        cur_type = s->type;
        /* Mirrors gen_deref_addr_ast's identical fast path for plain `*p`:
         * a `->` access needs the POINTER VARIABLE'S VALUE, not its own
         * address, so for a plain ix-direct local/param or global-word
         * symbol, load that value directly (`ld hl,(name)` for a global -
         * one instruction - or the equivalent direct ix-relative load for a
         * local) instead of computing &s and then dereferencing it, which
         * is the correct-but-needlessly-expensive general path required
         * only when s has no direct load form at all. */
        if (arrow && (sym_can_ix_direct(s) || is_global_word_sym(s))) {
            emit_load_sym_value_direct(s);
        } else {
            emit_load_sym_addr(s);
            if (arrow)
                emit_load_from_hl(cur_type);
        }
    }

    sid = base_struct_id_from_type(cur_type);
    fd = find_field_def(sid, n->sval);
    if (fd == NULL && arrow && n->a->kind == AST_CALL)
        fd = ast_unique_field_by_name(n->sval);
    emit_add_field_offset(fd);            /* HL = field address */

    /* Mirror apply_field_access_from_addr's field-metadata publication so the
    * global state after this node matches the helper contract byte-for-byte. */
    current_field_array_elem_size = fd->elem_size ? fd->elem_size
                                                  : type_size(fd->type);
    if (fd->is_array && type_is_float(fd->elem_type))
        current_field_array_elem_size = 4;
    else if (!fd->is_array && type_is_float(fd->type))
        current_field_array_elem_size = 4;
    current_field_array_dim_count = fd->dim_count;
    for (di = 0; di < 4; ++di)
        current_field_array_dims[di] = fd->dims[di];
    current_field_bit_width = fd->bit_width;
    current_field_bit_shift = fd->bit_shift;
    current_field_bit_mask = fd->bit_mask;

    val_type = fd->is_array ? fd->elem_type : fd->type;
    *out_val_type = val_type;
}

/* Look up a plain `BASE->FIELD` member node's value type without emitting
 * anything - the same resolution gen_member_addr_ast's plain-identifier-base
 * fallback performs (base symbol's type -> struct id -> field def). Used by
 * ast_for_hoist_global_member_value_supported (dcc_ast_gen_support.c) to
 * size the value-cache temp it allocates. Only meaningful for exactly the
 * shape that predicate matches: n->a is a plain identifier. */
int ast_member_field_value_type(const struct AstNode *n)
{
    struct Sym *s;
    int sid;
    struct FieldDef *fd;

    s = find_sym(n->a->sval);
    sid = base_struct_id_from_type(s->type);
    fd = find_field_def(sid, n->sval);
    return fd->is_array ? fd->elem_type : fd->type;
}

void gen_member_ast(const struct AstNode *n)
{
    int val_type;
    int elem_type;

    gen_member_addr_ast(n, &val_type);
    if (ast_member_array_field_elem_type(n, &elem_type)) {
        g_expr_type = type_add_ptr(elem_type);
        g_long_from16 = 0;
        return;
    }
    g_expr_type = val_type;
    emit_load_from_hl(val_type);
    if (current_field_bit_width > 0)
        emit_extract_bitfield();
}

/* Does `n` (a pointer-arithmetic operand) resolve - through any number of
 * pointer casts, which only reinterpret a value and never change it - to a
 * bare reference to a fixed-address global array? Its own address is then a
 * link-time constant, so `arr + CONST` can fold straight into a single
 * `ld hl,SYM+OFFSET` instead of loading the symbol, loading the constant, and
 * adding them at runtime. Excludes VLAs (whose "array" is really a pointer
 * loaded from a frame slot, not a fixed address) and multi-dimensional
 * arrays (whose row-decay stride semantics this fold doesn't attempt to
 * replicate). */
static struct Sym *ast_const_ptr_array_base(const struct AstNode *n)
{
    struct Sym *s;

    while (n != NULL && n->kind == AST_CAST)
        n = n->a;
    if (n == NULL || n->kind != AST_IDENT)
        return NULL;
    s = find_sym(n->sval);
    if (s == NULL || !s->is_array || s->is_vla || s->dim_count > 1)
        return NULL;
    if (s->storage == SC_LOCAL || s->storage == SC_PARAM)
        return NULL;
    return s;
}

void gen_pointer_expr_ast(const struct AstNode *n, int *out_type,
                                 int *out_no_deref)
{
    int ptr_type;
    int no_deref;
    int base;

    if (n->kind == AST_IDENT) {
        ast_gen_expr(n);
        *out_type = g_expr_type;
        *out_no_deref = 0;
        return;
    }

    if (n->kind == AST_STR_LIT) {
        ast_gen_expr(n);
        *out_type = g_expr_type;
        *out_no_deref = 0;
        return;
    }

    if (n->kind == AST_POSTFIX) {
        if (n->a->kind == AST_MEMBER) {
            int val_type;
            gen_member_addr_ast(n->a, &val_type);
            gen_post_update_from_addr(val_type, n->op);
            *out_type = val_type;
        } else {
            struct Sym *s = find_sym(n->a->sval);
            gen_post_update_symbol_addr_value(s, n->op);
            *out_type = s->type;
        }
        *out_no_deref = 0;
        return;
    }

    if (n->kind == AST_UNARY && n->op == '*') {
        int decay_type;
        int decay_stride;
        /* `*P` where P is a pointer-to-array (int (*P)[N]) decays, as an
         * rvalue, to a pointer to the array element.  Its value is simply P's
         * stored pointer, so load P and perform no dereference. */
        if (ast_deref_pointer_array_decay(n, &decay_type, &decay_stride)) {
            ast_gen_expr(n->a);
            g_expr_type = decay_type;
            g_long_from16 = 0;
            if (decay_stride > 0)
                g_array_decay_stride = decay_stride;
            *out_type = decay_type;
            *out_no_deref = (decay_stride > 0);
            return;
        }
        gen_pointer_expr_ast(n->a, &ptr_type, &no_deref);
        if (no_deref) {
            *out_type = ptr_type;
            *out_no_deref = 0;
            return;
        }
        base = type_decay_ptr(ptr_type);
        if ((base & 15) == TYPE_VOID)
            base = TYPE_CHAR;
        if (!type_is_struct_object(base))
            emit_load_from_hl(base);
        g_expr_type = base;
        *out_type = base;
        *out_no_deref = 0;
        return;
    }

    if (n->kind == AST_UNARY && n->op == '&') {
        ast_gen_expr(n);
        *out_type = g_expr_type;
        *out_no_deref = 0;
        return;
    }

    if (n->kind == AST_UNARY && (n->op == TOK_INC || n->op == TOK_DEC)) {
        ast_gen_expr(n);
        *out_type = g_expr_type;
        *out_no_deref = 0;
        return;
    }

    if (n->kind == AST_CAST && type_ptr_depth(n->type) > 0) {
        if (ast_pointer_expr_type(n->a, &ptr_type, &no_deref))
            gen_pointer_expr_ast(n->a, &ptr_type, &no_deref);
        else
            ast_gen_expr(n->a);
        /* A cast re-types the pointer, so any row-decay stride the operand
         * signalled (e.g. a 2D array decaying to a pointer-to-row) no
         * longer applies - subsequent pointer arithmetic on the CAST's
         * result must scale by the cast's own target type, not the
         * pre-cast operand's row size. Without this reset, a caller like
         * `(ftype*)C2D + n` would inherit C2D's stale row stride from the
         * inner gen_pointer_expr_ast call and scale by the row size
         * instead of sizeof(ftype). */
        g_array_decay_stride = 0;
        g_expr_type = n->type;
        g_long_from16 = 0;
        *out_type = n->type;
        *out_no_deref = 0;
        return;
    }

    if (n->kind == AST_COND) {
        int lfalse = new_label();
        int lend = new_label();
        int true_type;
        int false_type;
        int true_no_deref;
        int false_no_deref;
        int true_ptr;
        int false_ptr;
        ast_gen_expr(n->a);
        emit_test_expr_nonzero(g_expr_type, lfalse, 0);
        true_ptr = ast_pointer_expr_type(n->b, &true_type, &true_no_deref);
        if (true_ptr)
            gen_pointer_expr_ast(n->b, &true_type, &true_no_deref);
        else
            ast_gen_expr(n->b);
        emit_jp_label("jp", lend);
        emit_label(lfalse);
        false_ptr = ast_pointer_expr_type(n->c, &false_type, &false_no_deref);
        if (false_ptr)
            gen_pointer_expr_ast(n->c, &false_type, &false_no_deref);
        else
            ast_gen_expr(n->c);
        emit_label(lend);
        g_expr_type = true_ptr ? true_type : false_type;
        g_long_from16 = 0;
        *out_type = g_expr_type;
        *out_no_deref = true_ptr && false_ptr && true_no_deref && false_no_deref;
        return;
    }

    if (n->kind == AST_BINARY && (n->op == '+' || n->op == '-')) {
        int elem;
        int was_row_ptr;
        int saved_dead;

        if (n->op == '+' && !ast_pointer_expr_type(n->a, &ptr_type, &no_deref)) {
            gen_pointer_expr_ast(n->b, &ptr_type, &no_deref);
            was_row_ptr = (g_array_decay_stride > 0);
            elem = was_row_ptr ? g_array_decay_stride : type_index_elem_size(ptr_type);
            g_array_decay_stride = 0;

            emit("\tpush hl\n");
            saved_dead = expr_result_dead;
            expr_result_dead = 0;
            ast_gen_expr(n->a);
            expr_result_dead = saved_dead;
            scale_hl_by_elem_size(elem);
            emit("\tex de,hl\n");
            emit("\tpop hl\n");
            emit("\tadd hl,de\n");
            g_expr_type = ptr_type;
            g_long_from16 = 0;
            *out_type = ptr_type;
            *out_no_deref = was_row_ptr;
            return;
        }

        if (n->op == '+' && !no_deref && n->b->kind == AST_INT_LIT) {
            struct Sym *base_sym = ast_const_ptr_array_base(n->a);
            if (base_sym != NULL) {
                elem = type_index_elem_size(ptr_type);
                emit_extrn_if_needed(base_sym);
                fprintf(outf, "\tld hl,%s+%ld\n", asm_name_for(sym_asm_name(base_sym)),
                        (n->b->ival * elem) & 0xffffL);
                g_expr_type = ptr_type;
                g_long_from16 = 0;
                *out_type = ptr_type;
                *out_no_deref = 0;
                return;
            }
        }

        gen_pointer_expr_ast(n->a, &ptr_type, &no_deref);
        was_row_ptr = (g_array_decay_stride > 0);
        elem = was_row_ptr ? g_array_decay_stride : type_index_elem_size(ptr_type);
        g_array_decay_stride = 0;

        emit("\tpush hl\n");
        saved_dead = expr_result_dead;
        expr_result_dead = 0;
        ast_gen_expr(n->b);
        expr_result_dead = saved_dead;
        scale_hl_by_elem_size(elem);
        emit("\tex de,hl\n");
        emit("\tpop hl\n");
        if (n->op == '+')
            emit("\tadd hl,de\n");
        else
            emit("\tor a\n\tsbc hl,de\n");
        g_expr_type = ptr_type;
        g_long_from16 = 0;
        *out_type = ptr_type;
        *out_no_deref = was_row_ptr;
        return;
    }

    if (n->kind == AST_MEMBER) {
        int member_type;
        gen_member_addr_ast(n, &member_type);
        if (ast_member_array_field_elem_type(n, &member_type)) {
            g_expr_type = type_add_ptr(member_type);
            g_long_from16 = 0;
            *out_type = g_expr_type;
            *out_no_deref = 0;
            return;
        }
        emit_load_from_hl(member_type);
        g_expr_type = member_type;
        g_long_from16 = 0;
        *out_type = member_type;
        *out_no_deref = 0;
        return;
    }

    if (n->kind == AST_INDEX) {
        int member_type;
        if (ast_pointer_expr_type(n, &member_type, &no_deref)) {
            int addr_type;
            gen_index_addr_ast(n, &addr_type);
            if (!no_deref)
                emit_load_from_hl(addr_type);
            g_expr_type = no_deref ? member_type : addr_type;
            g_long_from16 = 0;
            *out_type = g_expr_type;
            *out_no_deref = no_deref;
            return;
        }
        gen_index_addr_ast(n, &member_type);
        emit_load_from_hl(member_type);
        g_expr_type = member_type;
        g_long_from16 = 0;
        *out_type = member_type;
        *out_no_deref = 0;
        return;
    }

    if (n->kind == AST_ASSIGN) {
        ast_gen_expr(n);
        *out_type = g_expr_type;
        *out_no_deref = 0;
        return;
    }

    if (n->kind == AST_CALL) {
        ast_gen_expr(n);
        *out_type = g_expr_type;
        *out_no_deref = 0;
        return;
    }
}

/* Emit the target ADDRESS of `*ident` into HL for an lvalue store: an IX-direct
 * local pointer or a global word pointer loads its value directly; otherwise
 * the symbol address is loaded and dereferenced.  This is NOT the same byte
 * sequence as the deref value-read path (gen_unary_ast '*'), which is why the
 * store needs its own helper. */
void gen_deref_addr_ast(const struct AstNode *n, int *out_val_type)
{
    struct Sym *s;
    struct Sym *ps;
    const struct AstNode *base_expr;
    const struct AstNode *idxs[DCC_MAX_DEREF_CHAIN];
    int idx_count;
    int no_deref;
    int ptr_type;
    int base;
    int elem_size;

    if (ast_deref_pointer_array_chain_collect(n, &ps, &base_expr, idxs,
                                              &idx_count, &base)) {
        int d;
        int di;
        gen_pointer_expr_ast(base_expr, &ptr_type, &no_deref);

        for (d = 0; d < idx_count; ++d) {
            /* Stride for dimension d is the element size scaled by the product
             * of all inner dimensions from d onward: dimension 0 spans a whole
             * pointed-to array object, the last index spans one element. */
            elem_size = type_size(base);
            if (elem_size <= 0)
                elem_size = 2;
            for (di = d; di < ps->dim_count; ++di)
                if (ps->dims[di] > 0)
                    elem_size *= ps->dims[di];

            if (idxs[d]->kind == AST_INT_LIT) {
                emit_add_const_to_hl(idxs[d]->ival * elem_size);
            } else {
                emit("\tpush hl\n");
                gen_index_subscript_expr_ast(idxs[d]);
                if (!(d == 0 && emit_runtime_pointer_array_stride(ps)))
                    scale_hl_by_elem_size(elem_size);
                emit("\tex de,hl\n");
                emit("\tpop hl\n");
                emit("\tadd hl,de\n");
            }
        }

        *out_val_type = base;
        return;
    }

    if (n->a->kind == AST_POSTFIX &&
        (n->a->op == TOK_INC || n->a->op == TOK_DEC) &&
        n->a->a != NULL && n->a->a->kind == AST_IDENT) {
        s = find_sym(n->a->a->sval);
        gen_post_update_symbol_addr_value(s, n->a->op);
        base = type_decay_ptr(s->type);
        if ((base & 15) == TYPE_VOID)
            base = TYPE_CHAR;
        *out_val_type = base;
        return;
    }

    if (n->a->kind != AST_IDENT) {
        gen_pointer_expr_ast(n->a, &ptr_type, &no_deref);
        base = no_deref ? ptr_type : type_decay_ptr(ptr_type);
        if ((base & 15) == TYPE_VOID)
            base = TYPE_CHAR;
        *out_val_type = base;
        return;
    }

    s = find_sym(n->a->sval);

    if (s->is_array) {
        emit_load_sym_addr(s);
    } else if (sym_can_ix_direct(s) || is_global_word_sym(s)) {
        emit_load_sym_value_direct(s);
    } else {
        emit_load_sym_addr(s);
        emit_load_from_hl(s->type);
    }

    base = type_decay_ptr(s->type);
    if ((base & 15) == TYPE_VOID)
        base = TYPE_CHAR;
    *out_val_type = base;
}

/* Emit a short-circuit `a && b` (AST_LOGAND) or `a || b` (AST_LOGOR): evaluate
 * the left operand, test it, conditionally short-circuit, otherwise evaluate
 * and test the right operand, and materialise a 0/1 result in HL.  The builder
 * nests chained operators left-associatively, so the recursive evaluation of
 * the left operand allocates its labels first, keeping label numbering
 * consistent across the chain. */
void gen_logical_ast(const struct AstNode *n)
{
    int lhs_type;
    int le;

    /* `x >= LO && x <= HI` (see ast_is_range_check_cond in
     * dcc_ast_gen_cond.c) used as a plain value, e.g. `return sq >= 0 && sq
     * < 64;` rather than an if/while condition: reuse the same fused single-
     * evaluation range-check branch and just materialize its 0/1 result,
     * instead of the generic path's two independent evaluations/promotions
     * of x plus two intermediate booleans. */
    if (ast_is_range_check_cond(n, NULL, NULL, NULL)) {
        int lt = new_label();
        le = new_label();
        ast_gen_range_check_branch(n, lt, 1);
        emit("\tld hl,0\n");
        emit_jp_label("jp", le);
        emit_label(lt);
        emit("\tld hl,1\n");
        emit_label(le);
        g_expr_type = TYPE_INT;
        return;
    }

    if (n->kind == AST_LOGAND) {
        int lf = 0;
        ast_gen_expr(n->a);
        lhs_type = g_expr_type;
        lf = new_label();
        le = new_label();
        emit_test_expr_nonzero(lhs_type, lf, 0);
        ast_gen_expr(n->b);
        emit_test_expr_nonzero(g_expr_type, lf, 0);
        emit("\tld hl,1\n");
        emit_jp_label("jp", le);
        emit_label(lf);
        emit("\tld hl,0\n");
        emit_label(le);
    } else {
        int lt = 0;
        ast_gen_expr(n->a);
        lhs_type = g_expr_type;
        lt = new_label();
        le = new_label();
        emit_test_expr_nonzero(lhs_type, lt, 1);
        ast_gen_expr(n->b);
        emit_test_expr_nonzero(g_expr_type, lt, 1);
        emit("\tld hl,0\n");
        emit_jp_label("jp", le);
        emit_label(lt);
        emit("\tld hl,1\n");
        emit_label(le);
    }
    g_expr_type = TYPE_INT;
}

/* Emit a plain-int conditional `cond ? a : b` (AST_COND) via the
 * neither-float-nor-long path: evaluate the condition before the labels are
 * allocated, test it, emit the true arm and speculatively widen it to long
 * (consumers read HL and ignore the stale DE for a 16-bit result), jump to the
 * end, then emit the false arm.  The result type is the C89-balanced common
 * type of the two arms. */
void gen_cond_ast(const struct AstNode *n)
{
    int lfalse;
    int lend;
    int true_type;
    int false_type;
    int need_long_result;
    int result_is_float;
    int no_deref;
    int know_not_long;

    if (ast_pointer_expr_type(n, &true_type, &no_deref)) {
        gen_pointer_expr_ast(n, &true_type, &no_deref);
        return;
    }

    {
        const struct AstNode *abs_x;
        if (ast_cond_is_abs_idiom(n, &abs_x)) {
            ast_gen_abs_idiom_value(abs_x);
            return;
        }
    }

    ast_gen_expr(n->a);                 /* condition */
    lfalse = new_label();
    lend = new_label();
    emit_test_expr_nonzero(g_expr_type, lfalse, 0);

    if (ast_cond_void_supported(n)) {
        ast_gen_expr(n->b);
        emit_jp_label("jp", lend);
        emit_label(lfalse);
        ast_gen_expr(n->c);
        emit_label(lend);
        g_expr_type = TYPE_VOID;
        g_long_from16 = 0;
        return;
    }

    result_is_float = ast_cond_result_is_float(n);

    /* `cond ? a : b` where neither arm's static type is long (the common
     * case, e.g. tchess.c's `x < 0 ? -x : x`) - proven with the same
     * static, side-effect-free type predicates ast_cond_result_is_long
     * already uses elsewhere (dcc_ast_gen_support.c), just not previously
     * consulted here. Without this, the true arm below was unconditionally
     * widened to a long result in DE:HL (on the chance the false arm might
     * turn out to be long) even when it plainly isn't - dead work on every
     * evaluation, since DE never ends up read: the whole expression's type
     * only depends on true_type/false_type, and once both arms are known
     * in hand after evaluating them, a not-actually-long result already
     * takes the plain `g_expr_type = common_arith_type(...)` path at the
     * bottom regardless of what got widened along the way. */
    know_not_long = !result_is_float && ast_cond_numeric_supported(n) &&
                     !ast_cond_result_is_long(n);

    ast_gen_expr(n->b);                 /* true arm */
    true_type = g_expr_type;
    need_long_result = 0;
    if (result_is_float) {
        if (!type_is_float(true_type))
            emit_convert_int_to_float(true_type);
    } else {
        need_long_result = type_is_long(true_type);
        if (!type_is_long(true_type) && !know_not_long)
            emit_extend_to_long((true_type & TYPE_UNSIGNED) ||
                                (true_type & (TYPE_PTR | TYPE_PTR2)));
    }
    emit_jp_label("jp", lend);

    emit_label(lfalse);
    ast_gen_expr(n->c);                 /* false arm */
    false_type = g_expr_type;

    if (result_is_float) {
        if (!type_is_float(false_type))
            emit_convert_int_to_float(false_type);
    } else {
        if (type_is_long(false_type))
            need_long_result = 1;
        if (need_long_result && !type_is_long(false_type)) {
            emit_extend_to_long((false_type & TYPE_UNSIGNED) ||
                                (false_type & (TYPE_PTR | TYPE_PTR2)));
            false_type = (false_type & TYPE_UNSIGNED) ?
                         (TYPE_LONG | TYPE_UNSIGNED) : TYPE_LONG;
        }
    }

    emit_label(lend);
    if (result_is_float) {
        g_expr_type = TYPE_FLOAT;
    } else if (need_long_result) {
        if ((true_type & TYPE_UNSIGNED) || (false_type & TYPE_UNSIGNED))
            g_expr_type = TYPE_LONG | TYPE_UNSIGNED;
        else
            g_expr_type = TYPE_LONG;
    } else {
        g_expr_type = common_arith_type(true_type, false_type);
    }
    g_long_from16 = 0;
}

/* Emit a postfix `lv++` / `lv--` on a plain-int identifier/member. */
void gen_postfix_ast(const struct AstNode *n)
{
    struct Sym *s;
    int val_type;

    if (n->a->kind == AST_MEMBER) {
        gen_member_addr_ast(n->a, &val_type);
        gen_post_update_from_addr(val_type, n->op);
        g_long_from16 = 0;
        return;
    }

    if (n->a->kind == AST_INDEX) {
        gen_index_addr_ast(n->a, &val_type);
        gen_post_update_from_addr(val_type, n->op);
        g_long_from16 = 0;
        return;
    }

    if (n->a->kind == AST_UNARY && n->a->op == '*') {
        gen_deref_addr_ast(n->a, &val_type);
        gen_post_update_from_addr(val_type, n->op);
        g_long_from16 = 0;
        return;
    }

    s = find_sym(n->a->sval);

    /* Pointer and plain-int identifiers: the post-update helper advances
     * pointers by their element size, stores the full value, and returns the
     * old value in HL.  It deliberately bails (returns 0) on long/float and on
     * symbols it cannot address directly. */
    if (try_emit_post_update_sym_direct(s, n->op)) {
        g_long_from16 = 0;
        return;
    }

    if (s != NULL && s->storage == SC_LOCAL && ast_is_plain_int_type(s->type) &&
        (type_size(s->type) == 1 || type_size(s->type) == 2)) {
        emit_load_sym_addr(s);
        gen_post_update_from_addr(s->type, n->op);
        g_long_from16 = 0;
        return;
    }

    /* Global/static byte scalar (uint8_t/char): try_emit_post_update_sym_direct
     * handles global WORDS (is_global_word_sym) and IX-direct locals but
     * declines a global BYTE, and emit_load_sym_value_direct's byte branch
     * assumes an (ix+d) frame slot.  Route it through its address like the
     * SC_LOCAL case above so the load/increment/store and byte wrap are
     * correct. */
    if (s != NULL && (s->storage == SC_GLOBAL || s->storage == SC_EXTERN) &&
        !s->is_array && ast_is_plain_int_type(s->type) &&
        type_size(s->type) == 1) {
        emit_load_sym_addr(s);
        gen_post_update_from_addr(s->type, n->op);
        g_long_from16 = 0;
        return;
    }

    /* long identifiers: update the full 32-bit value with carry ripple across
     * all four bytes via the address-based helper, which also selects an
     * in-place statement-context fast path when the result is dead (e.g. a
     * for-loop increment).  A plain `inc hl` would corrupt the high word. */
    if (s != NULL && type_is_long(s->type)) {
        emit_load_sym_addr(s);
        gen_post_update_from_addr(s->type, n->op);
        g_long_from16 = 0;
        return;
    }

    emit_load_sym_value_direct(s);      /* HL = old value (result) */
    emit("\tpush hl\n");
    if (n->op == TOK_INC)
        emit("\tinc hl\n");
    else
        emit("\tdec hl\n");
    emit_store_hl_to_sym_direct(s);     /* store new value */
    emit("\tpop hl\n");                 /* old value = expression result */
    g_expr_type = s->type;
}

void ast_gen_expr(const struct AstNode *n)
{
    switch (n->kind) {
    case AST_INT_LIT:
        gen_int_lit(n);
        break;
    case AST_SIZEOF_EXPR:
    case AST_SIZEOF_TYPE:
        gen_sizeof_expr_ast(n);
        break;
    case AST_FLOAT_LIT:
        emit_load_float_bits(n->uval);
        g_expr_type = TYPE_FLOAT;
        break;
    case AST_STR_LIT:
        gen_str_lit(n);
        break;
    case AST_IDENT:
        gen_ident(n);
        break;
    case AST_UNARY:
        gen_unary_ast(n);
        break;
    case AST_BINARY:
        if (is_shift_op(n->op))
            gen_shift_ast(n);
        else
            gen_binary_ast(n);
        break;
    case AST_ASSIGN:
        gen_assign_ast(n);
        break;
    case AST_INDEX:
        gen_index_ast(n);
        break;
    case AST_CALL:
        gen_call_ast(n);
        break;
    case AST_MEMBER:
        gen_member_ast(n);
        break;
    case AST_LOGAND:
    case AST_LOGOR:
        gen_logical_ast(n);
        break;
    case AST_COND:
        gen_cond_ast(n);
        break;
    case AST_POSTFIX:
        gen_postfix_ast(n);
        break;
    case AST_CAST:
        gen_cast_ast(n);
        break;
    case AST_COMPOUND_LITERAL:
        gen_compound_literal_value_ast(n);
        break;
    case AST_COMMA:
        {
            int old_dead = expr_result_dead;
            expr_result_dead = 1;
            ast_gen_dead_expr(n->a);
            expr_result_dead = old_dead;
        }
        ast_gen_expr(n->b);
        break;
    default:
        /* ast_gen_supported() gates entry; reaching here is a bug. */
        fatal("ast_gen_expr: unsupported node");
    }
}
