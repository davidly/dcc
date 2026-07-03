/*
 * dcc_ops.c - binary-operator and arithmetic code generation.
 *
 * Lowering for +, -, *, /, %, shifts and bitwise operators across 16- and
 * 32-bit and unsigned variants, integer promotion to the common type, the
 * element-size scaling used by pointer arithmetic, and the float-compare and
 * nonzero-test helpers the AST emitter calls into.
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
    } else if (v == 6) {
        emit("\tpush hl\n");     /* save x */
        emit("\tadd hl,hl\n");   /* 2x */
        emit("\tadd hl,hl\n");   /* 4x */
        emit("\tpop de\n");      /* x */
        emit("\tadd hl,de\n");   /* 5x */
        emit("\tadd hl,de\n");   /* 6x */
    } else if (v == 7) {
        emit("\tpush hl\n");     /* save x */
        emit("\tadd hl,hl\n");   /* 2x */
        emit("\tadd hl,hl\n");   /* 4x */
        emit("\tadd hl,hl\n");   /* 8x */
        emit("\tpop de\n");      /* x */
        emit("\tor a\n");
        emit("\tsbc hl,de\n");   /* 8x - x = 7x */
    } else if (v == 9) {
        emit("\tpush hl\n");     /* save x */
        emit("\tadd hl,hl\n");   /* 2x */
        emit("\tadd hl,hl\n");   /* 4x */
        emit("\tadd hl,hl\n");   /* 8x */
        emit("\tpop de\n");      /* x */
        emit("\tadd hl,de\n");   /* 9x */
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
    if ((t & 15) == TYPE_CHAR || (t & 15) == TYPE_BOOL) return TYPE_INT;
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


int int_log2_pow2(int v);
void emit_logical_shift_right_hl_const(int count);
void emit_and_hl_const(unsigned int mask);

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

    /* Not a power of two: emit_mul_hl_const already knows cheap shift/add
     * sequences for a handful of small constants (3,5,6,7,9,10 - exactly
     * the row/element strides a 2- or 3-column int/char array or a small
     * struct produces) and falls back to __mulu itself for anything else,
     * so delegating here gives every array-index and pointer-arithmetic
     * scaling call site (there are over a dozen) the same fast paths for
     * free instead of duplicating them. */
    emit_mul_hl_const(elem);
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

/* AND one 16-bit register pair (hi_reg:lo_reg, e.g. 'd','e' or 'h','l') with
 * a compile-time word mask in place, without a temporary register pair: a
 * byte that is all-ones in the mask is left untouched, a byte that is
 * all-zero collapses to a single immediate load, and anything else gets one
 * immediate `and`. */
static void emit_and_word_const(char hi_reg, char lo_reg, unsigned int word_mask)
{
    unsigned int hib = (word_mask >> 8) & 0xffU;
    unsigned int lob = word_mask & 0xffU;

    if (word_mask == 0xffffU)
        return;
    if (word_mask == 0) {
        fprintf(outf, "\tld %c%c,0\n", hi_reg, lo_reg);
        return;
    }
    if (hib == 0)
        fprintf(outf, "\tld %c,0\n", hi_reg);
    else if (hib != 0xffU)
        fprintf(outf, "\tld a,%c\n\tand %u\n\tld %c,a\n", hi_reg, hib, hi_reg);
    if (lob == 0)
        fprintf(outf, "\tld %c,0\n", lo_reg);
    else if (lob != 0xffU)
        fprintf(outf, "\tld a,%c\n\tand %u\n\tld %c,a\n", lo_reg, lob, lo_reg);
}

/* AND HL with a compile-time mask in place. Used both for the unsigned `%
 * pow2` fast path and for plain `int_expr & <const>` (see gen_binary_ast):
 * no temporary register pair or stack use, just the byte-wise logic above. */
void emit_and_hl_const(unsigned int mask)
{
    emit_and_word_const('h', 'l', mask & 0xffffU);
}

/* AND the DE:HL long value (DE = high word, HL = low word) with a
 * compile-time 32-bit mask in place. Used for `long_expr & <const>` so the
 * mask never needs to be materialized into a register pair or pushed
 * through the stack alongside the lhs. */
void emit_and_long_const(unsigned long mask)
{
    emit_and_word_const('d', 'e', (unsigned int)((mask >> 16) & 0xffffUL));
    emit_and_word_const('h', 'l', (unsigned int)(mask & 0xffffUL));
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

/* log2 of a power-of-two value in the full unsigned 32-bit long range;
 * int_log2_pow2 is restricted to the host `int` and cannot be trusted with
 * values above INT_MAX (e.g. 0x80000000). Returns -1 if v is 0 or not a
 * power of two. */
static int ulong_log2_pow2(unsigned long v)
{
    int n;

    if (v == 0 || (v & (v - 1)) != 0)
        return -1;
    n = 0;
    while (v > 1) {
        v >>= 1;
        n++;
    }
    return n;
}

/* Strength-reduce `long_expr * <compile-time power-of-two constant>` into a
 * left shift on the already-evaluated DE:HL value, with no push/pop and no
 * __lmul call. Whole-byte shift counts reuse the exact register-move
 * sequences emit_shift_const_long uses for `<<`; any remaining 0-7 bits are
 * unrolled `add hl,hl`/`rl e`/`rl d` steps (cheap and known at compile time,
 * so an actual runtime loop would only add overhead). Returns 0 (and emits
 * nothing) for multipliers that are not an exact power of two, leaving the
 * caller to fall back to the generic path; 0 and 1 are treated as "not a
 * useful shift" for the same reason. */
int emit_mul_pow2_long_const(long multiplier)
{
    int shift;
    int bytes;
    int bits;

    shift = ulong_log2_pow2((unsigned long)multiplier);
    if (shift <= 0)
        return 0;

    if (shift >= 32) {
        emit("\tld hl,0\n\tld de,0\n");
        return 1;
    }

    bytes = shift / 8;
    bits = shift % 8;

    switch (bytes) {
    case 1: emit("\tld d,e\n\tld e,h\n\tld h,l\n\tld l,0\n"); break;
    case 2: emit("\tld e,l\n\tld d,h\n\tld hl,0\n"); break;
    case 3: emit("\tld d,l\n\tld e,0\n\tld hl,0\n"); break;
    default: break;
    }
    while (bits-- > 0)
        emit("\tadd hl,hl\n\trl e\n\trl d\n");
    return 1;
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


