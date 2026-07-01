/*
 * dcc_ops.c - binary-operator and arithmetic code generation.
 *
 * Lowering for +, -, *, /, %, shifts and bitwise operators across 16- and
 * 32-bit and unsigned variants, integer promotion to the common type, the
 * element-size scaling used by pointer arithmetic, and power-of-two float
 * scaling fast paths.
 *
 * MODULE: compiled as its own translation unit; shared declarations are in dcc.h.
 * Source provenance: monolith src/ddc.c lines 10185-11520.
 */

#include "dcc.h"
void gen_signed_divmod16(int op)
{
    /*
     * Entry: HL = signed lhs, DE = signed rhs.
     * The RTL wrappers use unsigned division on absolute values and then
     * fix the sign.  Keeping this in the runtime avoids emitting the same
     * 40+ instruction sequence at every signed 16-bit / or % site.
     */
    if (op == '/')
        emit_runtime_call("__divs");
    else
        emit_runtime_call("__mods");
}

void gen_binop(int op)
{
    switch (op) {
    case '+':
        emit("\tadd hl,de\n");
        break;
    case '-':
        emit("\tor a\n\tsbc hl,de\n");
        break;
    case '&':
        emit("\tld a,h\n\tand d\n\tld h,a\n\tld a,l\n\tand e\n\tld l,a\n");
        break;
    case '|':
        emit("\tld a,h\n\tor d\n\tld h,a\n\tld a,l\n\tor e\n\tld l,a\n");
        break;
    case '^':
        emit("\tld a,h\n\txor d\n\tld h,a\n\tld a,l\n\txor e\n\tld l,a\n");
        break;
    case '*':
        emit_runtime_call("__mulu");
        break;
    case '/':
        emit_runtime_call("__divu");
        break;
    case '%':
        emit_runtime_call("__modu");
        break;
    case TOK_EQ:
    case TOK_NE:
    case '<':
    case '>':
    case TOK_LE:
    case TOK_GE:
        gen_cmp(op);
        break;
    default:
        emit("\t; unsupported binary op\n");
        break;
    }
}

void gen_binop_typed(int op, int lhs_type)
{
    if (op == TOK_EQ || op == TOK_NE ||
        op == '<' || op == '>' || op == TOK_LE || op == TOK_GE) {
        gen_cmp_typed(op, lhs_type);
    } else if ((op == '/' || op == '%') && !(lhs_type & TYPE_UNSIGNED)) {
        gen_signed_divmod16(op);
    } else {
        gen_binop(op);
    }
}

/* Zero-extend HL to DE:HL for implicit int-to-long promotion.
 * Explicit signed literals use the L suffix and are always emitted TYPE_LONG. */
void emit_extend_to_long_typed(int source_type)
{
    emit_promote_byte_to_int(source_type);
    if ((source_type & TYPE_UNSIGNED) || type_ptr_depth(source_type)) {
        emit("\tld de,0\n");
        g_long_from16 = 2; /* zero-extended: low word is an unsigned 16-bit value */
    } else {
        /* Sign-extend signed 16-bit HL into DE:HL.  This is also correct
         * when the target type is unsigned long: C converts the negative
         * value modulo 2^32, yielding the same bit pattern. */
        emit("\tld a,h\n");
        emit("\trlca\n");
        emit("\tsbc a,a\n");
        emit("\tld d,a\n");
        emit("\tld e,a\n");
        g_long_from16 = 1; /* sign-extended: low word is a signed 16-bit value */
    }
}

void emit_extend_to_long(int source_is_unsigned)
{
    emit_extend_to_long_typed(source_is_unsigned ? (TYPE_INT | TYPE_UNSIGNED) : TYPE_INT);
}

/*
 * 32-bit binary op: LHS on stack as push de;push hl, RHS in DE:HL.
 * Inline for +,-,&,|,^; runtime call for *,/,%.
 * Result left in DE:HL (DE=high word, HL=low word).
 */
void gen_binop32(int op, int lhs_type)
{
    const char *rname;
    switch (op) {
    case '+':
        emit("\tpop bc\n\tor a\n\tadd hl,bc\n");
        emit("\tex de,hl\n\tpop bc\n\tadc hl,bc\n\tex de,hl\n");
        break;
    case '-':
        emit("\tld b,h\n\tld c,l\n\tpop hl\n\tor a\n\tsbc hl,bc\n");
        emit("\tld b,h\n\tld c,l\n\tpop hl\n\tsbc hl,de\n");
        emit("\tld d,b\n\tld e,c\n\tex de,hl\n");
        break;
    case '&':
        emit("\tpop bc\n");
        emit("\tld a,l\n\tand c\n\tld l,a\n\tld a,h\n\tand b\n\tld h,a\n");
        emit("\tex de,hl\n\tpop bc\n");
        emit("\tld a,l\n\tand c\n\tld l,a\n\tld a,h\n\tand b\n\tld h,a\n");
        emit("\tex de,hl\n");
        break;
    case '|':
        emit("\tpop bc\n");
        emit("\tld a,l\n\tor c\n\tld l,a\n\tld a,h\n\tor b\n\tld h,a\n");
        emit("\tex de,hl\n\tpop bc\n");
        emit("\tld a,l\n\tor c\n\tld l,a\n\tld a,h\n\tor b\n\tld h,a\n");
        emit("\tex de,hl\n");
        break;
    case '^':
        emit("\tpop bc\n");
        emit("\tld a,l\n\txor c\n\tld l,a\n\tld a,h\n\txor b\n\tld h,a\n");
        emit("\tex de,hl\n\tpop bc\n");
        emit("\tld a,l\n\txor c\n\tld l,a\n\tld a,h\n\txor b\n\tld h,a\n");
        emit("\tex de,hl\n");
        break;
    case '*': rname = "__lmul";  goto l32call;
    case '/': rname = (lhs_type & TYPE_UNSIGNED) ? "__ldu" : "__lds"; goto l32call;
    case '%': rname = (lhs_type & TYPE_UNSIGNED) ? "__lmu" : "__lms"; goto l32call;
    l32call:
        /* push RHS (4 bytes) then call; runtime returns DE:HL; caller cleans 8 bytes */
        emit("\tpush de\n\tpush hl\n");
        emit_runtime_call(rname);
        emit("\tld b,d\n\tld c,e\n\tex de,hl\n");
        emit("\tld hl,8\n\tadd hl,sp\n\tld sp,hl\n");
        emit("\tex de,hl\n\tld d,b\n\tld e,c\n");
        break;
    default:
        emit("\t; unsupported 32-bit binary op\n");
        emit("\tpop bc\n\tpop bc\n"); /* balance stack */
        break;
    }
    g_long_from16 = 0;
}

/*
 * 32-bit comparison: LHS on stack as push de;push hl, RHS in DE:HL.
 * Inline for ==,!=; runtime call for ordered comparisons.
 * Result left in HL (0 or 1). Stack cleaned (8 bytes).
 */
void gen_cmp32(int op, int lhs_type)
{
    const char *rname;
    int lt, le;
    int is_unsigned = (lhs_type & TYPE_UNSIGNED) != 0;

    if (op == TOK_EQ || op == TOK_NE) {
        /* XOR all bytes together; A=0 iff equal */
        lt = new_label();
        le = new_label();
        emit("\tpop bc\n");  /* BC=LHS_L */
        emit("\tld a,c\n\txor l\n\tld l,a\n\tld a,b\n\txor h\n\tor l\n\tld l,a\n");
        emit("\tpop bc\n");  /* BC=LHS_H */
        emit("\tld a,c\n\txor e\n\tor l\n\tld l,a\n\tld a,b\n\txor d\n\tor l\n");
        if (op == TOK_EQ) emit_jp_label("jp z,", lt);
        else              emit_jp_label("jp nz,", lt);
        emit("\tld hl,0\n");
        emit_jp_label("jp", le);
        emit_label(lt);
        emit("\tld hl,1\n");
        emit_label(le);
        return;
    }

    /* ordered: push RHS onto stack, call runtime, clean 8 bytes */
    if (op == '<')      rname = is_unsigned ? "__ltu" : "__lts";
    else if (op == TOK_LE) rname = is_unsigned ? "__leu" : "__les";
    else if (op == '>')      rname = is_unsigned ? "__lgu" : "__lgs";
    else                rname = is_unsigned ? "__lku" : "__lks"; /* >= */
    emit("\tpush de\n\tpush hl\n");
    emit_runtime_call(rname);
    emit("\tex de,hl\n\tld hl,8\n\tadd hl,sp\n\tld sp,hl\n\tex de,hl\n");
}

void gen_binop32_typed(int op, int lhs_type)
{
    if (op == TOK_EQ || op == TOK_NE ||
        op == '<' || op == '>' || op == TOK_LE || op == TOK_GE) {
        gen_cmp32(op, lhs_type);
    } else {
        gen_binop32(op, lhs_type);
    }
}


void emit_mul_hl_const(long v)
{
    /*
     * HL = HL * small/power-of-two constant.
     * Power-of-two constants become repeated left shifts.
     */
    if (v == 0) {
        emit("\tld hl,0\n");
    } else if (v == 1) {
        /* no-op */
    } else if (int_log2_pow2((int)(v & 0xffffL)) >= 0) {
        int n;
        n = int_log2_pow2((int)(v & 0xffffL));
        while (n-- > 0)
            emit("\tadd hl,hl\n");
    } else if (v == 3) {
        emit("\tpush hl\n");
        emit("\tadd hl,hl\n");
        emit("\tpop de\n");
        emit("\tadd hl,de\n");
    } else if (v == 5) {
        emit("\tpush hl\n");
        emit("\tadd hl,hl\n");
        emit("\tadd hl,hl\n");
        emit("\tpop de\n");
        emit("\tadd hl,de\n");
    } else if (v == 10) {
        emit("\tpush hl\n");     /* save x */
        emit("\tadd hl,hl\n");   /* 2x */
        emit("\tadd hl,hl\n");   /* 4x */
        emit("\tadd hl,hl\n");   /* 8x */
        emit("\tpop de\n");      /* x */
        emit("\tadd hl,de\n");   /* 9x */
        emit("\tadd hl,de\n");   /* 10x */
    } else {
        emit_ld_de_const(v);
        emit_runtime_call("__mulu");
    }
}

int try_gen_const_times(void)
{
    long v;
    int const_is_wide;          /* constant does not fit a 16-bit int */
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    int save_long_suffix;
    int save_unsigned_suffix;
    struct Token save_tok;
    int const_type;

    if (tok.kind != TOK_NUM && tok.kind != TOK_CHARLIT)
        return 0;

    v = tok.val & 0xffffL;
    if (!(v == 0 || v == 1 || v == 3 || v == 5 || v == 10 ||
          int_log2_pow2((int)v) >= 0))
        return 0;

    /*
     * Whether the literal is wider than a 16-bit int: a long suffix, or a
     * magnitude outside the signed-16/unsigned-16 range.  The masked low word
     * `v` may still be a qualifying small value (e.g. 65536 -> 0, 65541 -> 5),
     * so this flag must be checked before using the 16-bit fast path.
     */
    const_is_wide = (tok.val > 0xffffL || tok.val < -32768L || g_tok_long_suffix);
    const_type = const_is_wide ? TYPE_LONG : TYPE_INT;
    if (g_tok_unsigned_suffix)
        const_type |= TYPE_UNSIGNED;

    save_pos = posi;
    save_tok_start = tok_start_pos;
    save_line = line_no;
    save_tok_line = tok_line;
    save_long_suffix = g_tok_long_suffix;
    save_unsigned_suffix = g_tok_unsigned_suffix;
    save_tok = tok;

    next_token();
    if (!accept('*')) {
        posi = save_pos;
        tok_start_pos = save_tok_start;
        line_no = save_line;
        tok_line = save_tok_line;
        g_tok_long_suffix = save_long_suffix;
        g_tok_unsigned_suffix = save_unsigned_suffix;
        tok = save_tok;
        return 0;
    }

    gen_unary();

    /*
     * emit_mul_hl_const multiplies only the low 16 bits in HL.  Use it only
     * when both the constant and the operand are plain 16-bit values.  If the
     * constant is wide, or the operand turned out to be long or float, that
     * fast path would silently drop the high word (or corrupt the float), so
     * fall back to a correct full-width multiply with the constant as the
     * second factor.  gen_mul's right-operand const fast path is already
     * guarded by !type_is_long(common_type); this mirrors that for the
     * left-operand prefix case.  g_expr_type after gen_unary is the reliable
     * operand type (peek_simple_unary_type cannot see through parenthesized,
     * dereferenced, or unary-prefixed operands).
     */
    if (!const_is_wide &&
        !type_is_long(g_expr_type) && !type_is_float(g_expr_type)) {
        int common;
        common = common_arith_type(g_expr_type, const_type);
        emit_mul_hl_const(v);
        g_expr_type = common;
        g_long_from16 = 0;
        return 1;
    }

    if (type_is_float(g_expr_type)) {
        /* operand (float) is in DE:HL; build (float)constant and multiply
         * (multiplication is commutative, so operand-as-LHS is fine). */
        emit("\tpush de\n\tpush hl\n");
        if (!scan_mode) {
            fprintf(outf, "\tld hl,%ld\n", save_tok.val & 0xffffL);
            fprintf(outf, "\tld de,%ld\n", (save_tok.val >> 16) & 0xffffL);
        }
        emit_convert_int_to_float(save_unsigned_suffix ?
                                  (TYPE_LONG | TYPE_UNSIGNED) : TYPE_LONG);
        emit("\tpush de\n\tpush hl\n");
        emit_runtime_call("__fmul");
        emit("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n");
        g_expr_type = TYPE_FLOAT;
        g_long_from16 = 0;
        return 1;
    }

    /* Long operand, or a wide constant: do a correct full 32-bit multiply.
     * The operand becomes the LHS on the stack and the full constant value is
     * the RHS in DE:HL. */
    {
        int common;

        common = common_arith_type(g_expr_type, const_type);
        emit_cast_16_to_common(g_expr_type, common);
        emit("\tpush de\n\tpush hl\n");
        if (!scan_mode) {
            fprintf(outf, "\tld hl,%ld\n", save_tok.val & 0xffffL);
            fprintf(outf, "\tld de,%ld\n", (save_tok.val >> 16) & 0xffffL);
        }
        gen_binop32('*', common);
        g_expr_type = common;
        g_long_from16 = 0;
        return 1;
    }
}



/* C89 integer promotion / usual arithmetic conversion helpers.
 * This compiler only has 16-bit int and 32-bit long.  Plain char and
 * unsigned char both promote to signed int because 16-bit int represents
 * all unsigned-char values.  For long vs unsigned-int, signed long wins
 * because it represents every 16-bit unsigned int value. */
int type_is_unsigned(int t)
{
    return (t & TYPE_UNSIGNED) != 0;
}

int type_is_arith(int t)
{
    if (t & (TYPE_PTR | TYPE_PTR2 | TYPE_STRUCT)) return 0;
    return 1;
}

int promote_int_type(int t)
{
    if (!type_is_arith(t)) return t;
    if (type_is_float(t)) return t;
    if (type_is_long(t)) return t;
    if ((t & 15) == TYPE_CHAR) return TYPE_INT;
    return (t & TYPE_UNSIGNED) ? (TYPE_INT | TYPE_UNSIGNED) : TYPE_INT;
}

int common_arith_type(int a, int b)
{
    a = promote_int_type(a);
    b = promote_int_type(b);

    if (type_is_float(a) || type_is_float(b))
        return TYPE_FLOAT;

    if (type_is_long(a) || type_is_long(b)) {
        /* unsigned long dominates; otherwise signed long can hold all
         * 16-bit unsigned values on this target. */
        if ((type_is_long(a) && type_is_unsigned(a)) ||
            (type_is_long(b) && type_is_unsigned(b)))
            return TYPE_LONG | TYPE_UNSIGNED;
        return TYPE_LONG;
    }

    if (type_is_unsigned(a) || type_is_unsigned(b))
        return TYPE_INT | TYPE_UNSIGNED;
    return TYPE_INT;
}

void emit_cast_16_to_common(int from_type, int common_type)
{
    if (type_is_long(common_type) && !type_is_long(from_type))
        emit_extend_to_long_typed(from_type);
}

int peek_simple_unary_type(void)
{
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    int save_long_suffix;
    int save_unsigned_suffix;
    struct Token save_tok;
    int t;
    struct Sym *s;

    save_pos = posi;
    save_tok_start = tok_start_pos;
    save_line = line_no;
    save_tok_line = tok_line;
    save_long_suffix = g_tok_long_suffix;
    save_unsigned_suffix = g_tok_unsigned_suffix;
    save_tok = tok;

    t = TYPE_INT;

    if (tok.kind == '(') {
        next_token();
        if (starts_type()) {
            t = parse_type();
            if (tok.kind == ')') {
                posi = save_pos; tok_start_pos = save_tok_start;
                line_no = save_line; tok_line = save_tok_line;
                g_tok_long_suffix = save_long_suffix; g_tok_unsigned_suffix = save_unsigned_suffix; tok = save_tok;
                return promote_int_type(t);
            }
        } else {
            /*
             * Parenthesized expression (not a cast): peek the type of its
             * first operand so a compound RHS such as (fa * fb) or (la + lb)
             * is predicted as float / long instead of defaulting to int.
             * Without this, gen_add/gen_mul/gen_rel/gen_eq pick the 16-bit
             * branch for  x + (fa * fb)  and truncate the float (or long)
             * result.  The recursion is bounded by paren nesting and only
             * refines the lookahead; it returns the inner operand's promoted
             * type.
             */
            int inner = peek_simple_unary_type();
            posi = save_pos; tok_start_pos = save_tok_start;
            line_no = save_line; tok_line = save_tok_line;
            g_tok_long_suffix = save_long_suffix; g_tok_unsigned_suffix = save_unsigned_suffix; tok = save_tok;
            return inner;
        }
    } else if (tok.kind == TOK_FLOATLIT) {
        t = TYPE_FLOAT;
    } else if (tok.kind == TOK_NUM) {
        if (tok.val > 0xffffL || tok.val < -32768L || g_tok_long_suffix)
            t = TYPE_LONG;
        else
            t = TYPE_INT;
        if (g_tok_unsigned_suffix)
            t |= TYPE_UNSIGNED;
    } else if (tok.kind == TOK_CHARLIT) {
        t = TYPE_INT;
    } else if (tok.kind == TOK_ID) {
        s = find_sym(tok.text);
        if (s) {
            int is_arr;
            int tt;
            t = s->type;

            /* For lookahead purposes, recognize calls through function
             * pointers and function-pointer arrays as returning int.
             * Without this, an expression like:
             *
             *     tab[0](30) + tab[1](40)
             *
             * is misclassified as integer + pointer before the RHS is
             * generated, so gen_add() applies pointer scaling to the left
             * call result.  This compiler only tracks int-returning
             * function pointers today, which matches the supported
             * declarator forms. */
            tt = t;
            is_arr = s->is_array;
            next_token();
            {
                int nsubs;
                int dim_count;
                int base_type;
                nsubs = 0;
                dim_count = s->dim_count;
                base_type = s->type;

                for (;;) {
                    if (tok.kind == '[') {
                        skip_balanced_bracket('[', ']');
                        nsubs++;

                        if (is_arr) {
                            /* A real array subscript consumes one array dimension.
                             * If dimensions remain, the result is still an array
                             * expression that decays to a pointer in value context;
                             * otherwise it is the scalar element type. */
                            if (dim_count > 0 && nsubs < dim_count)
                                tt = type_add_ptr(base_type);
                            else
                                tt = base_type;
                            if (dim_count <= 0 || nsubs >= dim_count)
                                is_arr = 0;
                            continue;
                        } else if (dim_count > 0 && type_ptr_depth(base_type) > 0) {
                            /* Pointer-to-array declarators need one more subscript
                             * than their stored array-dimension count to reach the
                             * scalar element. */
                            if (nsubs <= dim_count)
                                tt = type_add_ptr(type_decay_ptr(base_type));
                            else
                                tt = type_decay_ptr(base_type);
                            continue;
                        } else {
                            tt = type_decay_ptr(tt);
                            continue;
                        }
                    }

                    if (tok.kind == '.' || tok.kind == TOK_ARROW) {
                        struct FieldDef *fd;
                        int sid;

                        next_token();
                        if (tok.kind != TOK_ID)
                            break;

                        sid = base_struct_id_from_type(tt);
                        fd = find_field_def(sid, tok.text);
                        if (!fd)
                            break;

                        tt = fd->is_array ? fd->elem_type : fd->type;
                        is_arr = fd->is_array;
                        dim_count = fd->dim_count;
                        base_type = fd->elem_type;
                        nsubs = 0;
                        next_token();
                        continue;
                    }

                    break;
                }
            }
            t = tt;
            if (tok.kind == '(' && type_ptr_depth(tt) > 0)
                t = TYPE_INT;
        }
    }

    posi = save_pos;
    tok_start_pos = save_tok_start;
    line_no = save_line;
    tok_line = save_tok_line;
    g_tok_long_suffix = save_long_suffix;
    g_tok_unsigned_suffix = save_unsigned_suffix;
    tok = save_tok;
    return promote_int_type(t);
}


int float_literal_pow2_exp(const char *s, int *exp_out)
{
    double d;
    int e;

    d = atof(s);
    if (d <= 0.0)
        return 0;

    e = 0;
    while (d > 1.0 && e < 30) {
        d = d / 2.0;
        e++;
    }
    while (d < 1.0 && e > -30) {
        d = d * 2.0;
        e--;
    }

    if (d != 1.0)
        return 0;

    exp_out[0] = e;
    return 1;
}

void emit_fscale_pow2(int exp_delta)
{
    if (exp_delta == 0) {
        g_expr_type = TYPE_FLOAT;
        return;
    }

    if (exp_delta < -128 || exp_delta > 127) {
        /* Out of helper range; this should not happen for normal literals. */
        emit_runtime_call(exp_delta < 0 ? "__fdiv" : "__fmul");
        return;
    }

    if (!scan_mode)
        fprintf(outf, "\tld b,%d\n", exp_delta);
    emit_runtime_call("__fscale_pow2");
    g_expr_type = TYPE_FLOAT;
}

int try_consume_float_pow2_compound_scale(int op)
{
    int pow2_exp;

    if ((op != TOK_MULEQ && op != TOK_DIVEQ) || tok.kind != TOK_FLOATLIT)
        return 0;

    if (!float_literal_pow2_exp(tok.text, &pow2_exp))
        return 0;

    next_token();
    if (op == TOK_DIVEQ)
        pow2_exp = -pow2_exp;
    emit_fscale_pow2(pow2_exp);
    return 1;
}

int try_gen_float_pow2_times(void)
{
    int pow2_exp;
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    int save_long_suffix;
    int save_unsigned_suffix;
    struct Token save_tok;

    if (tok.kind != TOK_FLOATLIT)
        return 0;
    if (!float_literal_pow2_exp(tok.text, &pow2_exp))
        return 0;

    save_pos = posi;
    save_tok_start = tok_start_pos;
    save_line = line_no;
    save_tok_line = tok_line;
    save_long_suffix = g_tok_long_suffix;
    save_unsigned_suffix = g_tok_unsigned_suffix;
    save_tok = tok;

    next_token();
    if (!accept('*')) {
        posi = save_pos;
        tok_start_pos = save_tok_start;
        line_no = save_line;
        tok_line = save_tok_line;
        g_tok_long_suffix = save_long_suffix;
        g_tok_unsigned_suffix = save_unsigned_suffix;
        tok = save_tok;
        return 0;
    }

    gen_unary();
    if (!type_is_float(g_expr_type))
        emit_convert_int_to_float(g_expr_type);
    emit_fscale_pow2(pow2_exp);
    g_expr_type = TYPE_FLOAT;
    return 1;
}

int int_log2_pow2(int v);
void emit_logical_shift_right_hl_const(int count);
void emit_and_hl_const(unsigned int mask);

/*
 * Choose a 16x16->32 multiply helper when both operands of a long multiply
 * are values that were just widened from 16-bit (so their high words are pure
 * sign/zero extension and carry no information).  This replaces the full
 * 32x32 __lmul (a 16-iteration base loop plus two cross-product __mulu calls)
 * with a single 16-iteration register loop.  Mixed signedness has no single
 * correct 16x16 form, so fall back to __lmul there.
 *   widen kind: 0 = not a widened 16-bit value, 1 = signed, 2 = unsigned.
 */
static const char *long_mul_widen_helper(int lhs_w, int rhs_w)
{
    /* 5-char names: L80 only treats the first 6 characters of a public symbol
     * as significant, so __lmuls/__lmulu would alias __lmul.  __m1s/__m1u
     * stay distinct. */
    if (lhs_w == 1 && rhs_w == 1) return "__m1s";
    if (lhs_w == 2 && rhs_w == 2) return "__m1u";
    return 0;
}

/*
 * Slow-path fix-up: a binary operator's LHS was emitted as a 16-bit value
 * (only HL was pushed) but the RHS turned out to be a long that
 * peek_simple_unary_type could not predict -- typically a parenthesized or
 * otherwise compound expression such as  x + (a + b)  with long a, b.
 *
 * On entry: the 16-bit LHS is on top of the stack (one word) and the long
 * RHS is in DE:HL.  Widen the LHS to common_type and run gen_binop32 so the
 * result is the correct LHS op RHS with operand order preserved (which
 * matters for '-', '/', '%').  Result left in DE:HL; the stack is balanced.
 */
static void gen_binop32_promote_16lhs(int op, int lhs_type, int common_type)
{
    emit("\tpop bc\n");                 /* BC = 16-bit LHS */
    emit("\tpush de\n\tpush hl\n");     /* spill the long RHS (high, low) */
    emit("\tld h,b\n\tld l,c\n");       /* HL = 16-bit LHS */
    emit_cast_16_to_common(lhs_type, common_type);  /* DE:HL = LHS widened */
    emit("\tpush de\n\tpush hl\n");     /* push LHS as gen_binop32's left operand */
    emit("\tld hl,4\n\tadd hl,sp\n");   /* skip the pushed LHS to reach the RHS spill */
    emit("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n");  /* DE = RHS low word */
    emit("\tinc hl\n");
    emit("\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n");  /* HL = RHS high word */
    emit("\tex de,hl\n");               /* DE:HL = RHS */
    gen_binop32(op, common_type);       /* LHS op RHS -> DE:HL; pops the stacked LHS */
    emit("\tpop bc\n\tpop bc\n");       /* discard the RHS spill */
    g_long_from16 = 0;
}

/*
 * Arithmetic fallback for a float RHS that peek_simple_unary_type mis-predicted
 * as 16-bit (e.g. a parenthesized "fa * fb" or a float-returning call).  The
 * predictive peek is only a hint; once the RHS has actually been generated the
 * authoritative type lives in g_expr_type, and these fallbacks trust it.
 *
 * On entry the 16-bit LHS is the top word of the stack (pushed by the caller)
 * and the float RHS is in DE:HL.  Convert the LHS to float and evaluate in the
 * canonical deep=LHS / top=RHS stack order the float helpers expect, so the
 * result is LHS op RHS (correct for the non-commutative '-' and '/').  Result
 * left in DE:HL; the stack is fully balanced.  op is one of '+','-','*','/'.
 */
static void gen_float_binop_16lhs(int op, int lhs_type)
{
    const char *helper;

    helper = (op == '+') ? "__fadd" :
             (op == '-') ? "__fsub" :
             (op == '*') ? "__fmul" : "__fdiv";

    /* spill the float RHS above the 16-bit LHS word */
    emit("\tpush de\n\tpush hl\n");                  /* [LHS16][RHSf] */
    /* reload the 16-bit LHS (now at SP+4) and widen it to float */
    emit("\tld hl,4\n\tadd hl,sp\n");
    emit("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n");
    emit_convert_int_to_float(lhs_type);              /* DE:HL = (float)LHS */
    emit("\tpush de\n\tpush hl\n");                  /* [LHS16][RHSf][LHSf] */
    /* reload the spilled RHS float (now the 4 bytes at SP+4) */
    emit("\tld hl,4\n\tadd hl,sp\n");
    emit("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tinc hl\n");
    emit("\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n");
    emit("\tex de,hl\n");                            /* DE:HL = RHS float */
    emit("\tpush de\n\tpush hl\n");                  /* [LHS16][RHSf][LHSf][RHSf] */
    emit_runtime_call(helper);                        /* deep=LHS,top=RHS -> LHS op RHS */
    /* clean all 14 pushed bytes without disturbing DE:HL (the result) */
    emit("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n");
    g_expr_type = TYPE_FLOAT;
    g_long_from16 = 0;
}

/*
 * Arithmetic fallback for a float RHS that the predicted common type put at
 * long (e.g. "longvar + (fa * fb)"): the snippet/peek pre-analysis classified
 * the operation as long, but the RHS actually generated a float.  On entry the
 * 32-bit long LHS is on top of the stack (pushed by the caller as push de;push
 * hl) and the float RHS is in DE:HL.  Widen the long LHS to float, evaluate in
 * canonical deep=LHS / top=RHS order so the result is LHS op RHS, and leave it
 * in DE:HL with the stack fully balanced.  op is one of '+','-','*','/'.
 */
static void gen_float_binop_long_lhs(int op, int lhs_type)
{
    const char *helper;

    helper = (op == '+') ? "__fadd" :
             (op == '-') ? "__fsub" :
             (op == '*') ? "__fmul" : "__fdiv";

    /* spill the float RHS above the 4-byte long LHS */
    emit("\tpush de\n\tpush hl\n");                  /* [LHSlong][RHSf] */
    /* reload the long LHS (now at SP+4) into DE:HL and widen it to float */
    emit("\tld hl,4\n\tadd hl,sp\n");
    emit("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tinc hl\n");
    emit("\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n");
    emit("\tex de,hl\n");                            /* DE:HL = LHS long */
    emit_convert_int_to_float(lhs_type);              /* DE:HL = (float)LHS */
    emit("\tpush de\n\tpush hl\n");                  /* [LHSlong][RHSf][LHSf] */
    /* reload the spilled RHS float (now the 4 bytes at SP+4) */
    emit("\tld hl,4\n\tadd hl,sp\n");
    emit("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tinc hl\n");
    emit("\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n");
    emit("\tex de,hl\n");                            /* DE:HL = RHS float */
    emit("\tpush de\n\tpush hl\n");                  /* [LHSlong][RHSf][LHSf][RHSf] */
    emit_runtime_call(helper);                        /* deep=LHS,top=RHS -> LHS op RHS */
    /* clean all 16 pushed bytes without disturbing DE:HL (the result) */
    emit("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n");
    g_expr_type = TYPE_FLOAT;
    g_long_from16 = 0;
}

void gen_mul(void)
{
    int op;
    int lhs_type;
    int rhs_type;
    int common_type;

    if (!try_gen_float_pow2_times() && !try_gen_const_times())
        gen_unary();
    lhs_type = promote_int_type(g_expr_type);

    while (tok.kind == '*' || tok.kind == '/' || tok.kind == '%') {
        op = tok.kind;
        next_token();

        rhs_type = peek_simple_unary_type();
        common_type = common_arith_type(lhs_type, rhs_type);

        if (type_is_float(common_type)) {
            if (op == '%') {
                error_float_unsupported("float modulo not supported");
                gen_unary();
                lhs_type = TYPE_INT;
                g_expr_type = TYPE_INT;
                continue;
            }

            /*
             * Exact power-of-two scale fast path:
             *     x * 16.0f   => __fscale_pow2(x, +4)
             *     x / 16.0f   => __fscale_pow2(x, -4)
             *
             * This avoids generic __fmul/__fdiv while preserving the operation
             * as exponent scaling in the RTL.
             */
            if ((op == '*' || op == '/') && tok.kind == TOK_FLOATLIT) {
                int pow2_exp;
                if (float_literal_pow2_exp(tok.text, &pow2_exp)) {
                    next_token();
                    if (!type_is_float(lhs_type))
                        emit_convert_int_to_float(lhs_type);
                    if (op == '/')
                        pow2_exp = -pow2_exp;
                    emit_fscale_pow2(pow2_exp);
                    lhs_type = TYPE_FLOAT;
                    g_expr_type = TYPE_FLOAT;
                    continue;
                }
            }

            if (!type_is_float(lhs_type))
                emit_convert_int_to_float(lhs_type);
            emit("\tpush de\n\tpush hl\n");
            gen_unary();
            if (!type_is_float(g_expr_type))
                emit_convert_int_to_float(g_expr_type);
            emit("\tpush de\n\tpush hl\n");
            emit_runtime_call(op == '*' ? "__fmul" : "__fdiv");
            emit("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n");
            lhs_type = TYPE_FLOAT;
            g_expr_type = TYPE_FLOAT;
            continue;
        }

        if (!type_is_long(common_type) && op == '*' &&
            (tok.kind == TOK_NUM || tok.kind == TOK_CHARLIT)) {
            long rhs_val = tok.val & 0xffffL;
            if (rhs_val == 0 || rhs_val == 1 || rhs_val == 3 ||
                rhs_val == 5 || rhs_val == 10 ||
                int_log2_pow2((int)rhs_val) >= 0) {
                next_token();
                emit_mul_hl_const(rhs_val);
                lhs_type = common_type;
                g_expr_type = common_type;
                continue;
            }
        }

        if (!type_is_long(common_type) && (common_type & TYPE_UNSIGNED) &&
            (op == '/' || op == '%') &&
            (tok.kind == TOK_NUM || tok.kind == TOK_CHARLIT)) {
            unsigned int rhs_val;
            int shift;

            rhs_val = (unsigned int)(tok.val & 0xffffL);
            shift = int_log2_pow2((int)rhs_val);
            if (shift >= 0) {
                next_token();
                if (op == '/')
                    emit_logical_shift_right_hl_const(shift);
                else
                    emit_and_hl_const(rhs_val - 1U);
                lhs_type = common_type;
                g_expr_type = common_type;
                continue;
            }

            /*
             * x % C with unsigned 16-bit x and an 8-bit non-power-of-two
             * constant C can use a smaller remainder-only helper.  The full
             * fixed divider maintains a quotient and compares/subtracts a
             * 16-bit divisor; __r1u keeps only an 8-bit divisor/remainder.
             */
            if (op == '%' && rhs_val > 1U && rhs_val <= 255U) {
                next_token();
                fprintf(outf, "\tld e,%u\n", rhs_val);
                emit_runtime_call("__r1u");
                lhs_type = common_type;
                g_expr_type = common_type;
                continue;
            }
        }

        /*
         * Signed x % C, where C is a small positive int constant, is also
         * common in simple loop setup code such as i % 26.  Use a 16/8
         * signed remainder helper instead of the full 16/16 fixed divider.
         * This preserves C's usual remainder sign convention as implemented
         * by the existing signed modulo helpers.
         */
        if (!type_is_long(common_type) && !(common_type & TYPE_UNSIGNED) &&
            op == '%' && (tok.kind == TOK_NUM || tok.kind == TOK_CHARLIT) &&
            tok.val > 1 && tok.val <= 255) {
            long rhs_sval;
            rhs_sval = tok.val;
            next_token();
            fprintf(outf, "\tld e,%ld\n", rhs_sval);
            emit_runtime_call("__r1s");
            lhs_type = common_type;
            g_expr_type = common_type;
            continue;
        }

        if (type_is_long(common_type)) {
            int lhs_w;
            int rhs_w;
            const char *mulhelp;

            emit_cast_16_to_common(lhs_type, common_type);
            lhs_w = g_long_from16;      /* widen kind of the LHS operand */
            emit("\tpush de\n\tpush hl\n");
            gen_unary();
            if (type_is_float(g_expr_type)) {
                /* The RHS generated a float though the predicted common type
                 * was long (a parenthesized/compound/member float that
                 * peek_simple_unary_type could not see).  Float dominates:
                 * widen the stacked long LHS and operate as float.  Modulo
                 * has no float form. */
                if (op == '%') {
                    error_float_unsupported("float modulo not supported");
                    emit("\tpop bc\n\tpop bc\n\tld hl,0\n");
                    common_type = TYPE_INT;
                } else {
                    gen_float_binop_long_lhs(op, common_type);
                    common_type = TYPE_FLOAT;
                }
            } else {
                emit_cast_16_to_common(g_expr_type, common_type);
                rhs_w = g_long_from16;      /* widen kind of the RHS operand */

                mulhelp = (op == '*') ? long_mul_widen_helper(lhs_w, rhs_w) : 0;
                if (mulhelp) {
                    /* LHS is already stacked as high,low and RHS is in DE:HL.
                     * The widened-16 multiply only needs the low words, so use
                     * a register ABI helper: BC=LHS low, HL=RHS low -> DE:HL. */
                    emit("\tpop bc\n\tpop af\n");
                    emit_runtime_call(mulhelp);
                } else {
                    gen_binop32(op, common_type);
                }
            }
        } else {
            emit("\tpush hl\n");
            gen_unary();
            if (type_is_long(g_expr_type)) {
                common_type = common_arith_type(lhs_type, g_expr_type);
                gen_binop32_promote_16lhs(op, lhs_type, common_type);
            } else if (type_is_float(g_expr_type)) {
                /* RHS is float but peek predicted 16-bit (parenthesized or
                 * compound operand).  Trust the computed type. */
                if (op == '%') {
                    error_float_unsupported("float modulo not supported");
                    emit("\tpop bc\n\tld hl,0\n");
                    common_type = TYPE_INT;
                } else {
                    gen_float_binop_16lhs(op, lhs_type);
                    common_type = TYPE_FLOAT;
                }
            } else {
                emit("\tex de,hl\n\tpop hl\n");
                gen_binop_typed(op, common_type);
            }
        }
        lhs_type = common_type;
        g_expr_type = common_type;
        /* The operator's result is a computed value, not a freshly widened
         * 16-bit one, so a following multiply must not treat it as such. */
        g_long_from16 = 0;
    }
}

void scale_hl_by_elem_size(int elem)
{
    int shift;

    if (elem <= 1)
        return;

    shift = int_log2_pow2(elem);
    if (shift >= 0) {
        while (shift-- > 0)
            emit("\tadd hl,hl\n");
        return;
    }

    fprintf(outf, "\tld de,%d\n", elem);
    emit_runtime_call("__mulu");
}

int int_log2_pow2(int v)
{
    int n;

    if (v <= 0 || (v & (v - 1)) != 0)
        return -1;

    n = 0;
    while (v > 1) {
        v >>= 1;
        n++;
    }
    return n;
}

void emit_arith_shift_right_hl_const(int count)
{
    if (count <= 0) return;
    if (count >= 16) {
        emit("\tld a,h\n\trla\n\tsbc a,a\n\tld h,a\n\tld l,a\n");
        return;
    }
    if (count >= 8) {
        /* >> 8: L = old H (result low byte); H = sign extension of old H */
        emit("\tld a,h\n\tld l,h\n\trla\n\tsbc a,a\n\tld h,a\n");
        count -= 8;
        while (count-- > 0)
            emit("\tsra h\n\trr l\n");
        return;
    }
    while (count-- > 0)
        emit("\tsra h\n\trr l\n");
}

void emit_logical_shift_right_hl_const(int count)
{
    if (count <= 0) return;
    if (count >= 16) { emit("\tld hl,0\n"); return; }
    if (count >= 8) {
        emit("\tld l,h\n\tld h,0\n");
        count -= 8;
        while (count-- > 0)
            emit("\tsrl l\n");
        return;
    }
    while (count-- > 0)
        emit("\tsrl h\n\trr l\n");
}

void emit_shift_left_hl_const(int count)
{
    if (count <= 0) return;
    if (count >= 16) { emit("\tld hl,0\n"); return; }
    if (count >= 8) {
        emit("\tld h,l\n\tld l,0\n");
        count -= 8;
        while (count-- > 0)
            emit("\tadd hl,hl\n");
        return;
    }
    while (count-- > 0)
        emit("\tadd hl,hl\n");
}

void emit_and_hl_const(unsigned int mask)
{
    fprintf(outf, "\tld de,%u\n", mask & 0xffffU);
    gen_binop('&');
}

void divide_hl_by_elem_size(int elem)
{
    int shift;

    if (elem <= 1)
        return;

    shift = int_log2_pow2(elem);
    if (shift >= 0) {
        emit_arith_shift_right_hl_const(shift);
        return;
    }

    fprintf(outf, "\tld de,%d\n", elem);
    emit_runtime_call("__divs");
}

void gen_add(void)
{
    int op;
    int lhs_type;
    int rhs_type;
    int common_type;

    gen_mul();
    lhs_type = promote_int_type(g_expr_type);

    while (tok.kind == '+' || tok.kind == '-') {
        op = tok.kind;
        next_token();
        rhs_type = peek_simple_unary_type();

        /* Pointer arithmetic is not an arithmetic conversion case.
         *
         * Supported C89 forms:
         *   ptr + n       -> byte address plus n * sizeof(*ptr)
         *   ptr - n       -> byte address minus n * sizeof(*ptr)
         *   n + ptr       -> n * sizeof(*ptr) plus byte address
         *   ptr2 - ptr1   -> signed element distance
         */
        if ((lhs_type & (TYPE_PTR | TYPE_PTR2))) {
            int was_row_ptr = (g_array_decay_stride > 0);
            int elem = was_row_ptr ? g_array_decay_stride : type_index_elem_size(lhs_type);
            g_array_decay_stride = 0;

            emit("\tpush hl\n");
            gen_mul();

            if (g_expr_type & (TYPE_PTR | TYPE_PTR2)) {
                if (op == '-') {
                    emit("\tex de,hl\n\tpop hl\n");
                    gen_binop('-');
                    divide_hl_by_elem_size(elem);
                    lhs_type = TYPE_INT;
                    g_expr_type = TYPE_INT;
                    was_row_ptr = 0;
                    continue;
                }

                error_here("invalid pointer arithmetic");
                emit("\tpop de\n");
                lhs_type = TYPE_INT;
                g_expr_type = TYPE_INT;
                was_row_ptr = 0;
                continue;
            }

            scale_hl_by_elem_size(elem);
            emit("\tex de,hl\n\tpop hl\n");
            gen_binop(op);
            g_expr_type = lhs_type;
            if (was_row_ptr && op == '+')
                g_expr_no_deref = 1;
            continue;
        }

        if ((rhs_type & (TYPE_PTR | TYPE_PTR2)) && op == '+') {
            int elem = type_index_elem_size(rhs_type);

            scale_hl_by_elem_size(elem);
            emit("\tpush hl\n");
            gen_mul();
            emit("\tex de,hl\n\tpop hl\n");
            gen_binop('+');
            lhs_type = rhs_type;
            g_expr_type = rhs_type;
            continue;
        }

        common_type = common_arith_type(lhs_type, rhs_type);

        if (type_is_float(common_type)) {
            if (!type_is_float(lhs_type))
                emit_convert_int_to_float(lhs_type);
            emit("\tpush de\n\tpush hl\n");
            gen_mul();
            if (!type_is_float(g_expr_type))
                emit_convert_int_to_float(g_expr_type);
            emit("\tpush de\n\tpush hl\n");
            emit_runtime_call(op == '+' ? "__fadd" : "__fsub");
            emit("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n");
            lhs_type = TYPE_FLOAT;
            g_expr_type = TYPE_FLOAT;
            continue;
        }

        if (type_is_long(common_type)) {
            emit_cast_16_to_common(lhs_type, common_type);
            emit("\tpush de\n\tpush hl\n");
            gen_mul();
            if (type_is_float(g_expr_type)) {
                /* RHS generated a float though the predicted common type was
                 * long; float dominates.  Widen the stacked long LHS and add
                 * or subtract as float. */
                gen_float_binop_long_lhs(op, common_type);
                common_type = TYPE_FLOAT;
            } else {
                emit_cast_16_to_common(g_expr_type, common_type);
                gen_binop32(op, common_type);
            }
        } else {
            emit("\tpush hl\n");
            gen_mul();
            if (type_is_long(g_expr_type)) {
                common_type = common_arith_type(lhs_type, g_expr_type);
                gen_binop32_promote_16lhs(op, lhs_type, common_type);
            } else if (type_is_float(g_expr_type)) {
                /* RHS is float but peek predicted 16-bit (parenthesized or
                 * compound operand).  Trust the computed type. */
                gen_float_binop_16lhs(op, lhs_type);
                common_type = TYPE_FLOAT;
            } else {
                emit("\tex de,hl\n\tpop hl\n");
                gen_binop_typed(op, common_type);
            }
        }
        lhs_type = common_type;
        g_expr_type = common_type;
        g_long_from16 = 0;
    }
}

int emit_shift_const_long(int op, int lhs_type, long count)
{
    int is_left;
    int is_unsigned;

    if (!type_is_long(lhs_type))
        return 0;

    is_left = (op == TOK_SHL || op == TOK_SHLEQ);
    is_unsigned = (lhs_type & TYPE_UNSIGNED) != 0;

    if (count <= 0) {
        g_long_from16 = 0;
        return 1;
    }

    if (count >= 32) {
        if (is_left || is_unsigned)
            emit("\tld hl,0\n\tld de,0\n");
        else
            emit("\tld a,d\n\trla\n\tsbc a,a\n\tld h,a\n\tld l,a\n\tld d,a\n\tld e,a\n");
        g_long_from16 = 0;
        return 1;
    }

    if (is_left) {
        if (count == 8) { emit("\tld d,e\n\tld e,h\n\tld h,l\n\tld l,0\n"); g_long_from16 = 0; return 1; }
        if (count == 16) { emit("\tld e,l\n\tld d,h\n\tld hl,0\n"); g_long_from16 = 0; return 1; }
        if (count == 24) { emit("\tld d,l\n\tld e,0\n\tld hl,0\n"); g_long_from16 = 0; return 1; }
    } else if (is_unsigned) {
        if (count == 8) { emit("\tld l,h\n\tld h,e\n\tld e,d\n\tld d,0\n"); g_long_from16 = 0; return 1; }
        if (count == 16) { emit("\tld l,e\n\tld h,d\n\tld de,0\n"); g_long_from16 = 0; return 1; }
        if (count == 24) { emit("\tld l,d\n\tld h,0\n\tld de,0\n"); g_long_from16 = 0; return 1; }
    } else {
        /*
         * Signed right shift by a whole number of bytes: the same byte
         * moves as the unsigned case, but the vacated high bytes are filled
         * with the replicated sign byte (0x00 or 0xFF) computed in A rather
         * than zero.  DE:HL holds the value (D = MSB, L = LSB).
         *   ld a,d / rla / sbc a,a  ->  A = 0x00 if non-negative, 0xFF if negative.
         */
        if (count == 8) {
            emit("\tld a,d\n\trla\n\tsbc a,a\n");
            emit("\tld l,h\n\tld h,e\n\tld e,d\n\tld d,a\n");
            g_long_from16 = 0;
            return 1;
        }
        if (count == 16) {
            emit("\tld a,d\n\trla\n\tsbc a,a\n");
            emit("\tld l,e\n\tld h,d\n\tld e,a\n\tld d,a\n");
            g_long_from16 = 0;
            return 1;
        }
        if (count == 24) {
            emit("\tld a,d\n\trla\n\tsbc a,a\n");
            emit("\tld l,d\n\tld h,a\n\tld e,a\n\tld d,a\n");
            g_long_from16 = 0;
            return 1;
        }
    }

    return 0;
}

void emit_shift_loop(int op, int lhs_type)
{
    int ltop = new_label();
    int ldone = new_label();

    emit_label(ltop);
    emit("\tld a,b\n\tor a\n");
    emit_jp_label("jp z,", ldone);

    if (type_is_long(lhs_type)) {
        if (op == TOK_SHL || op == TOK_SHLEQ) {
            emit("\tadd hl,hl\n\trl e\n\trl d\n");
        } else if (lhs_type & TYPE_UNSIGNED) {
            emit("\tsrl d\n\trr e\n\trr h\n\trr l\n");
        } else {
            emit("\tsra d\n\trr e\n\trr h\n\trr l\n");
        }
    } else if (op == TOK_SHL || op == TOK_SHLEQ) {
        emit("\tadd hl,hl\n");
    } else if (lhs_type & TYPE_UNSIGNED) {
        emit("\tsrl h\n\trr l\n");
    } else if (type_size(lhs_type) == 1) {
        emit("\tsra l\n");
    } else {
        emit("\tsra h\n\trr l\n");
    }

    emit("\tdec b\n");
    emit_jp_label("jp", ltop);
    emit_label(ldone);
    if (type_is_long(lhs_type))
        g_long_from16 = 0;
}

void gen_shift(void)
{
    int op;
    int lhs_type;

    gen_add();
    /*
     * C89: the left operand of a shift undergoes integer promotion, but
     * the right operand does not participate in the usual arithmetic
     * conversions.  This matters for uint8_t: b << 1 is an int expression
     * on DCC's 16-bit-int target, so 200 << 1 is 400, not 8-bit 144.
     */
    lhs_type = promote_int_type(g_expr_type);

    while (tok.kind == TOK_SHL || tok.kind == TOK_SHR) {
        op = tok.kind;
        next_token();

        if (type_is_long(lhs_type) && (tok.kind == TOK_NUM || tok.kind == TOK_CHARLIT)) {
            long scount;
            scount = tok.val;
            next_token();
            if (!emit_shift_const_long(op, lhs_type, scount)) {
                fprintf(outf, "\tld b,%ld\n", scount & 255L);
                emit_shift_loop(op, lhs_type);
            }
        } else if (type_is_long(lhs_type)) {
            emit("\tpush de\n\tpush hl\n");
            gen_add();
            emit("\tld b,l\n");
            emit("\tpop hl\n\tpop de\n");
            emit_shift_loop(op, lhs_type);
        } else if (tok.kind == TOK_NUM || tok.kind == TOK_CHARLIT) {
            int scount = (int)(tok.val & 255);
            next_token();
            if (op == TOK_SHL)
                emit_shift_left_hl_const(scount);
            else if (lhs_type & TYPE_UNSIGNED)
                emit_logical_shift_right_hl_const(scount);
            else
                emit_arith_shift_right_hl_const(scount);
        } else {
            emit("\tpush hl\n");
            gen_add();
            emit("\tld b,l\n");
            emit("\tpop hl\n");
            emit_shift_loop(op, lhs_type);
        }

        g_expr_type = lhs_type;
        g_long_from16 = 0;
    }
}

void emit_float_compare_call(int op)
{
    const char *helper;
    int k;

    /* The evaluation sequence pushes the left operand first, computes and
     * pushes the right operand second.  That means the C-style helper sees
     * arguments in reversed order: arg1=right, arg2=left.  Use the inverse
     * ordered helper where necessary.
     */
    if (op == TOK_EQ) helper = "__feq";
    else if (op == TOK_NE) helper = "__fneq";
    else if (op == '<') helper = "__fgt";      /* rhs > lhs */
    else if (op == '>') helper = "__flt";      /* rhs < lhs */
    else if (op == TOK_LE) helper = "__fge";   /* rhs >= lhs */
    else helper = "__fle";                     /* rhs <= lhs */

    emit_runtime_call(helper);
    for (k = 0; k < 4; ++k)
        emit("\tpop bc\n");
    g_expr_type = TYPE_INT;
}

/*
 * Compare fallback for a long RHS that peek_simple_unary_type mis-predicted as
 * 16-bit (e.g. a parenthesized or compound "y + la").  The 16-bit LHS is the
 * top word of the stack and the long RHS is in DE:HL.  Widen the LHS and
 * compare as 32-bit with the operands swapped: the RHS becomes gen_cmp32's
 * stacked left operand, so the relop is inverted (== / != are commutative, so
 * the swap is also correct for them).  Result int 0/1 in HL.
 */
static void gen_cmp_promote_16lhs(int op, int lhs_type)
{
    int ct = common_arith_type(lhs_type, g_expr_type);
    emit("\tpop bc\n");
    emit("\tpush de\n\tpush hl\n");
    emit("\tld h,b\n\tld l,c\n");
    emit_cast_16_to_common(lhs_type, ct);
    gen_cmp32(invert_relop_for_swap(op), ct);
}

/*
 * Compare fallback for a float RHS that peek_simple_unary_type mis-predicted as
 * 16-bit (e.g. a parenthesized "fa * fb").  The 16-bit LHS is the top word of
 * the stack and the float RHS is in DE:HL.  Convert the LHS to float and lay
 * out canonical deep=LHS / top=RHS so emit_float_compare_call computes the
 * relation LHS op RHS.  Result int 0/1 in HL; the stack is fully balanced.
 */
void gen_float_cmp_16lhs(int op, int lhs_type)
{
    /* spill the float RHS above the 16-bit LHS word */
    emit("\tpush de\n\tpush hl\n");                  /* [LHS16][RHSf] */
    /* reload the 16-bit LHS (now at SP+4) and widen it to float */
    emit("\tld hl,4\n\tadd hl,sp\n");
    emit("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n");
    emit_convert_int_to_float(lhs_type);              /* DE:HL = (float)LHS */
    emit("\tpush de\n\tpush hl\n");                  /* [LHS16][RHSf][LHSf] */
    /* reload the spilled RHS float (now the 4 bytes at SP+4) */
    emit("\tld hl,4\n\tadd hl,sp\n");
    emit("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tinc hl\n");
    emit("\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n");
    emit("\tex de,hl\n");                            /* DE:HL = RHS float */
    emit("\tpush de\n\tpush hl\n");                  /* [LHS16][RHSf][LHSf][RHSf] */
    emit_float_compare_call(op);                      /* pops LHSf+RHSf; HL = 0/1 */
    emit("\tpop bc\n\tpop bc\n\tpop bc\n");          /* discard LHS16 + RHS spill */
    g_expr_type = TYPE_INT;
}

/*
 * Compare fallback for a float RHS that the predicted common type put at long
 * (e.g. "longvar < (fa * fb)" or "longvar < s->f").  On entry the 32-bit long
 * LHS is on top of the stack (pushed by the caller as push de;push hl) and the
 * float RHS is in DE:HL.  Widen the long LHS to float and lay out canonical
 * deep=LHS / top=RHS so emit_float_compare_call computes LHS op RHS.  Result
 * int 0/1 in HL; the stack is fully balanced.
 */
void gen_float_cmp_long_lhs(int op, int lhs_type)
{
    /* spill the float RHS above the 4-byte long LHS */
    emit("\tpush de\n\tpush hl\n");                  /* [LHSlong][RHSf] */
    /* reload the long LHS (now at SP+4) into DE:HL and widen it to float */
    emit("\tld hl,4\n\tadd hl,sp\n");
    emit("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tinc hl\n");
    emit("\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n");
    emit("\tex de,hl\n");                            /* DE:HL = LHS long */
    emit_convert_int_to_float(lhs_type);              /* DE:HL = (float)LHS */
    emit("\tpush de\n\tpush hl\n");                  /* [LHSlong][RHSf][LHSf] */
    /* reload the spilled RHS float (now the 4 bytes at SP+4) */
    emit("\tld hl,4\n\tadd hl,sp\n");
    emit("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tinc hl\n");
    emit("\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n");
    emit("\tex de,hl\n");                            /* DE:HL = RHS float */
    emit("\tpush de\n\tpush hl\n");                  /* [LHSlong][RHSf][LHSf][RHSf] */
    emit_float_compare_call(op);                      /* pops LHSf+RHSf; HL = 0/1 */
    emit("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n"); /* discard LHSlong + RHS spill */
    g_expr_type = TYPE_INT;
}

void gen_rel(void)
{
    int op;
    int lhs_type;
    int rhs_type;
    int common_type;
    gen_shift();
    lhs_type = promote_int_type(g_expr_type);

    while (tok.kind == '<' || tok.kind == '>' || tok.kind == TOK_LE || tok.kind == TOK_GE) {
        op = tok.kind;
        next_token();
        rhs_type = peek_simple_unary_type();
        common_type = common_arith_type(lhs_type, rhs_type);

        if (type_is_float(common_type)) {
            if (!type_is_float(lhs_type))
                emit_convert_int_to_float(lhs_type);
            emit("\tpush de\n\tpush hl\n");
            gen_shift();
            if (!type_is_float(g_expr_type))
                emit_convert_int_to_float(g_expr_type);
            emit("\tpush de\n\tpush hl\n");
            emit_float_compare_call(op);
        } else if (type_is_long(common_type)) {
            emit_cast_16_to_common(lhs_type, common_type);
            emit("\tpush de\n\tpush hl\n");
            gen_shift();
            if (type_is_float(g_expr_type)) {
                /* RHS generated a float though the predicted common type was
                 * long (hidden float the peek could not see).  Widen the
                 * stacked long LHS and compare as float. */
                gen_float_cmp_long_lhs(op, common_type);
            } else {
                emit_cast_16_to_common(g_expr_type, common_type);
                gen_binop32_typed(op, common_type);
            }
        } else {
            int did_sign_opt = 0;

            if ((op == '<' || op == TOK_GE) && !(common_type & TYPE_UNSIGNED) &&
                tok.kind == TOK_NUM && tok.val == 0) {
                long sv_pos = posi; long sv_ts = tok_start_pos;
                int sv_ln = line_no, sv_tl = tok_line;
                struct Token sv_tok = tok;
                next_token();
                /* confirm RHS is exactly 0 (no trailing binary operator) */
                if (tok.kind != '+' && tok.kind != '-' && tok.kind != '*' &&
                    tok.kind != '/' && tok.kind != '%' &&
                    tok.kind != TOK_SHL && tok.kind != TOK_SHR) {
                    emit("\tld a,h\n\trlca\n");
                    if (op == TOK_GE) emit("\tccf\n");
                    emit("\tsbc a,a\n\tand 1\n\tld h,0\n\tld l,a\n");
                    did_sign_opt = 1;
                } else {
                    posi = sv_pos; tok_start_pos = sv_ts;
                    line_no = sv_ln; tok_line = sv_tl; tok = sv_tok;
                }
            }

            if (!did_sign_opt) {
                emit("\tpush hl\n");
                gen_shift();
                if (type_is_long(g_expr_type)) {
                    gen_cmp_promote_16lhs(op, lhs_type);
                } else if (type_is_float(g_expr_type)) {
                    gen_float_cmp_16lhs(op, lhs_type);
                } else {
                    emit("\tex de,hl\n\tpop hl\n");
                    gen_binop_typed(op, common_type);
                }
            }
        }
        g_expr_type = TYPE_INT;
        lhs_type = TYPE_INT;
    }
}

void gen_eq(void)
{
    int op;
    int lhs_type;
    int rhs_type;
    int common_type;
    gen_rel();
    lhs_type = promote_int_type(g_expr_type);

    while (tok.kind == TOK_EQ || tok.kind == TOK_NE) {
        op = tok.kind;
        next_token();
        rhs_type = peek_simple_unary_type();
        common_type = common_arith_type(lhs_type, rhs_type);

        if (type_is_float(common_type)) {
            if (!type_is_float(lhs_type))
                emit_convert_int_to_float(lhs_type);
            emit("\tpush de\n\tpush hl\n");
            gen_rel();
            if (!type_is_float(g_expr_type))
                emit_convert_int_to_float(g_expr_type);
            emit("\tpush de\n\tpush hl\n");
            emit_float_compare_call(op);
        } else if (type_is_long(common_type)) {
            emit_cast_16_to_common(lhs_type, common_type);
            emit("\tpush de\n\tpush hl\n");
            gen_rel();
            if (type_is_float(g_expr_type)) {
                /* RHS generated a float though the predicted common type was
                 * long (hidden float the peek could not see).  Widen the
                 * stacked long LHS and compare as float. */
                gen_float_cmp_long_lhs(op, common_type);
            } else {
                emit_cast_16_to_common(g_expr_type, common_type);
                gen_binop32_typed(op, common_type);
            }
        } else {
            emit("\tpush hl\n");
            gen_rel();
            if (type_is_long(g_expr_type)) {
                gen_cmp_promote_16lhs(op, lhs_type);
            } else if (type_is_float(g_expr_type)) {
                gen_float_cmp_16lhs(op, lhs_type);
            } else {
                emit("\tex de,hl\n\tpop hl\n");
                gen_binop_typed(op, common_type);
            }
        }
        g_expr_type = TYPE_INT;
        lhs_type = TYPE_INT;
    }
}

void gen_band(void)
{
    int lhs_type;
    int rhs_type;
    int common_type;
    gen_eq();
    lhs_type = promote_int_type(g_expr_type);

    while (accept('&')) {
        rhs_type = peek_simple_unary_type();
        common_type = common_arith_type(lhs_type, rhs_type);
        if (type_is_long(common_type)) {
            emit_cast_16_to_common(lhs_type, common_type);
            emit("\tpush de\n\tpush hl\n");
            gen_eq();
            emit_cast_16_to_common(g_expr_type, common_type);
            gen_binop32('&', common_type);
        } else {
            emit("\tpush hl\n");
            gen_eq();
            if (type_is_long(g_expr_type)) {
                common_type = common_arith_type(lhs_type, g_expr_type);
                gen_binop32_promote_16lhs('&', lhs_type, common_type);
            } else {
                emit("\tex de,hl\n\tpop hl\n");
                gen_binop('&');
            }
        }
        lhs_type = common_type;
        g_expr_type = common_type;
        g_long_from16 = 0;
    }
}

void gen_bxor(void)
{
    int lhs_type;
    int rhs_type;
    int common_type;
    gen_band();
    lhs_type = promote_int_type(g_expr_type);

    while (accept('^')) {
        rhs_type = peek_simple_unary_type();
        common_type = common_arith_type(lhs_type, rhs_type);
        if (type_is_long(common_type)) {
            emit_cast_16_to_common(lhs_type, common_type);
            emit("\tpush de\n\tpush hl\n");
            gen_band();
            emit_cast_16_to_common(g_expr_type, common_type);
            gen_binop32('^', common_type);
        } else {
            emit("\tpush hl\n");
            gen_band();
            if (type_is_long(g_expr_type)) {
                common_type = common_arith_type(lhs_type, g_expr_type);
                gen_binop32_promote_16lhs('^', lhs_type, common_type);
            } else {
                emit("\tex de,hl\n\tpop hl\n");
                gen_binop('^');
            }
        }
        lhs_type = common_type;
        g_expr_type = common_type;
        g_long_from16 = 0;
    }
}

void gen_bor(void)
{
    int lhs_type;
    int rhs_type;
    int common_type;
    gen_bxor();
    lhs_type = promote_int_type(g_expr_type);

    while (accept('|')) {
        rhs_type = peek_simple_unary_type();
        common_type = common_arith_type(lhs_type, rhs_type);
        if (type_is_long(common_type)) {
            emit_cast_16_to_common(lhs_type, common_type);
            emit("\tpush de\n\tpush hl\n");
            gen_bxor();
            emit_cast_16_to_common(g_expr_type, common_type);
            gen_binop32('|', common_type);
        } else {
            emit("\tpush hl\n");
            gen_bxor();
            if (type_is_long(g_expr_type)) {
                common_type = common_arith_type(lhs_type, g_expr_type);
                gen_binop32_promote_16lhs('|', lhs_type, common_type);
            } else {
                emit("\tex de,hl\n\tpop hl\n");
                gen_binop('|');
            }
        }
        lhs_type = common_type;
        g_expr_type = common_type;
        g_long_from16 = 0;
    }
}


void emit_test_expr_nonzero(int expr_type, int true_label, int branch_when_true)
{
    if (type_is_float(expr_type))
        /* A float is false only for +0.0 and -0.0.  Mask the sign bit (bit 7
         * of D) so -0.0 (0x80000000) tests as zero too, then OR the remaining
         * magnitude bytes: A==0 iff the value is +/-0.0.  The float lives in
         * DE:HL with D holding the sign/exponent MSB. */
        emit("\tld a,d\n\tand 7fh\n\tor e\n\tor h\n\tor l\n");
    else if (type_is_long(expr_type))
        emit("\tld a,h\n\tor l\n\tor d\n\tor e\n");
    else
        emit("\tld a,h\n\tor l\n");

    if (branch_when_true)
        emit_jp_label("jp nz,", true_label);
    else
        emit_jp_label("jp z,", true_label);
}

void gen_land(void)
{
    int lf, le;
    int lhs_type;

    gen_bor();
    lhs_type = g_expr_type;

    while (accept(TOK_ANDAND)) {
        lf = new_label();
        le = new_label();

        emit_test_expr_nonzero(lhs_type, lf, 0);

        gen_bor();

        emit_test_expr_nonzero(g_expr_type, lf, 0);

        emit("\tld hl,1\n");
        emit_jp_label("jp", le);
        emit_label(lf);
        emit("\tld hl,0\n");
        emit_label(le);

        g_expr_type = TYPE_INT;
        lhs_type = TYPE_INT;
    }
}

void gen_lor(void)
{
    int lt, le;
    int lhs_type;

    gen_land();
    lhs_type = g_expr_type;

    while (accept(TOK_OROR)) {
        lt = new_label();
        le = new_label();

        emit_test_expr_nonzero(lhs_type, lt, 1);

        gen_land();

        emit_test_expr_nonzero(g_expr_type, lt, 1);

        emit("\tld hl,0\n");
        emit_jp_label("jp", le);
        emit_label(lt);
        emit("\tld hl,1\n");
        emit_label(le);

        g_expr_type = TYPE_INT;
        lhs_type = TYPE_INT;
    }
}

void gen_conditional(void)
{
    int lfalse;
    int lend;
    int true_type;
    int false_type;
    int need_long_result;
    int result_is_float;

    gen_lor();

    if (accept('?')) {
        lfalse = new_label();
        lend = new_label();

        emit_test_expr_nonzero(g_expr_type, lfalse, 0);

        gen_expr();
        true_type = g_expr_type;
        if (type_is_struct_object(true_type))
            error_here("unsupported struct conditional expression");

        expect(':');

        result_is_float = type_is_float(true_type);

        need_long_result = 0;

        if (result_is_float) {
            if (!type_is_float(true_type))
                emit_convert_int_to_float(true_type);
        } else {
            /*
             * DCC has no full typed IR, so a narrow true arm used with a long
             * false arm could leave DE stale.  Speculatively widen a 16-bit
             * true arm to long; that is safe when the whole expression is
             * ultimately 16-bit because consumers read HL and ignore DE.
             * (tarray.c: int32_t cap = (end_of_row > beyond) ? beyond : end_of_row;)
             */
            need_long_result = type_is_long(true_type);
            if (!type_is_long(true_type)) {
                emit_extend_to_long((true_type & TYPE_UNSIGNED) ||
                                    (true_type & (TYPE_PTR | TYPE_PTR2)));
            }
        }

        emit_jp_label("jp", lend);

        emit_label(lfalse);

        gen_conditional();
        false_type = g_expr_type;
        if (type_is_struct_object(false_type))
            error_here("unsupported struct conditional expression");

        if (result_is_float) {
            if (!type_is_float(false_type))
                emit_convert_int_to_float(false_type);
        } else {
            if (type_is_long(false_type))
                need_long_result = 1;

            if (need_long_result && !type_is_long(false_type)) {
                emit_extend_to_long((false_type & TYPE_UNSIGNED) ||
                                    (false_type & (TYPE_PTR | TYPE_PTR2)));
                false_type = (false_type & TYPE_UNSIGNED) ? (TYPE_LONG | TYPE_UNSIGNED) : TYPE_LONG;
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
            /*
             * C89 conditional operator balancing still matters when both
             * result arms are 16-bit or narrower.  Without this, the type of
             * the whole expression accidentally remained the false arm type.
             *
             * Example:
             *     (long)(1 ? (uint16_t)50000 : (int8_t)-10)
             *
             * If the expression type is left as int8_t, the later cast to
             * long widens only the low byte (0x50 -> 80).  The balanced type
             * is unsigned int, so the cast must widen the full 16-bit HL value.
             */
            g_expr_type = common_arith_type(true_type, false_type);
        }
        g_long_from16 = 0;
    }
}


