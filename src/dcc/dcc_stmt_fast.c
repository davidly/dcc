/*
 * dcc_stmt_fast.c - statement-level fast-path code generation.
 *
 * Recognises whole-statement idioms that can be emitted more tightly than the
 * general expression path: in-place ++/-- on a symbol, self-add accumulation,
 * and the CRC-update byte idiom. Each returns 0 to fall back to the generic
 * path when the statement does not match.
 *
 * MODULE: compiled as its own translation unit; shared declarations are in dcc.h.
 * Source provenance: monolith src/ddc.c lines 12418-13127.
 */

#include "dcc.h"
void emit_incdec_addr(int type, int op)
{
    int done;

    if (type_size(type) == 1) {
        if (op == TOK_INC)
            emit("\tinc (hl)\n");
        else
            emit("\tdec (hl)\n");
        return;
    }

    done = new_label();

    if (type_size(type) == 4) {
        /* 4-byte (long) ripple increment/decrement */
        if (op == TOK_INC) {
            emit("\tinc (hl)\n");
            emit_jp_label("jp nz,", done);
            emit("\tinc hl\n");
            emit("\tinc (hl)\n");
            emit_jp_label("jp nz,", done);
            emit("\tinc hl\n");
            emit("\tinc (hl)\n");
            emit_jp_label("jp nz,", done);
            emit("\tinc hl\n");
            emit("\tinc (hl)\n");
        } else {
            emit("\tld a,(hl)\n");
            emit("\tdec (hl)\n");
            emit("\tor a\n");
            emit_jp_label("jp nz,", done);
            emit("\tinc hl\n");
            emit("\tld a,(hl)\n");
            emit("\tdec (hl)\n");
            emit("\tor a\n");
            emit_jp_label("jp nz,", done);
            emit("\tinc hl\n");
            emit("\tld a,(hl)\n");
            emit("\tdec (hl)\n");
            emit("\tor a\n");
            emit_jp_label("jp nz,", done);
            emit("\tinc hl\n");
            emit("\tdec (hl)\n");
        }
    } else {
        /* 2-byte (int) ripple increment/decrement */
        if (op == TOK_INC) {
            emit("\tinc (hl)\n");
            emit_jp_label("jp nz,", done);
            emit("\tinc hl\n");
            emit("\tinc (hl)\n");
        } else {
            emit("\tld a,(hl)\n");
            emit("\tdec (hl)\n");
            emit("\tor a\n");
            emit_jp_label("jp nz,", done);
            emit("\tinc hl\n");
            emit("\tdec (hl)\n");
        }
    }

    emit_label(done);
}

int lookahead_is_post_incdec_statement(int *op_out)
{
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    struct Token save_tok;
    int r;
    int op;

    if (tok.kind != TOK_ID && tok.kind != '*' && tok.kind != '(')
        return 0;

    save_pos = posi;
    save_tok_start = tok_start_pos;
    save_line = line_no;
    save_tok_line = tok_line;
    save_tok = tok;

    if (!skip_lvalue_syntax()) {
        posi = save_pos;
        tok_start_pos = save_tok_start;
        line_no = save_line;
        tok_line = save_tok_line;
        tok = save_tok;
        return 0;
    }

    r = 0;
    op = tok.kind;
    if (op == TOK_INC || op == TOK_DEC) {
        next_token();
        if (tok.kind == ';') {
            r = 1;
            op_out[0] = op;
        }
    }

    posi = save_pos;
    tok_start_pos = save_tok_start;
    line_no = save_line;
    tok_line = save_tok_line;
    tok = save_tok;
    return r;
}


int try_fast_local_self_add_statement(void)
{
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    struct Token save_tok;
    char lhs_name[64];
    char rhs1_name[64];
    char rhs2_name[64];
    struct Sym *lhs;
    struct Sym *rhs1;
    struct Sym *rhs2;
    int op;
    long cval;
    int rhs2_const;

    if (tok.kind != TOK_ID)
        return 0;

    save_pos = posi;
    save_tok_start = tok_start_pos;
    save_line = line_no;
    save_tok_line = tok_line;
    save_tok = tok;

    strncpy(lhs_name, tok.text, sizeof(lhs_name) - 1);
    lhs_name[sizeof(lhs_name) - 1] = 0;
    lhs = find_sym(lhs_name);
    if (!sym_can_ix_direct(lhs) || type_size(lhs->type) != 2)
        goto no;

    next_token();
    if (!accept('='))
        goto no;

    if (tok.kind != TOK_ID)
        goto no;
    strncpy(rhs1_name, tok.text, sizeof(rhs1_name) - 1);
    rhs1_name[sizeof(rhs1_name) - 1] = 0;
    rhs1 = find_sym(rhs1_name);
    if (!sym_can_ix_direct(rhs1) || type_size(rhs1->type) != 2)
        goto no;
    next_token();

    op = tok.kind;
    if (op != '+' && op != '-')
        goto no;
    next_token();

    rhs2_const = 0;
    cval = 0;
    rhs2 = NULL;

    if (tok.kind == TOK_ID) {
        strncpy(rhs2_name, tok.text, sizeof(rhs2_name) - 1);
        rhs2_name[sizeof(rhs2_name) - 1] = 0;
        rhs2 = find_sym(rhs2_name);
        if (!sym_can_ix_direct(rhs2) || type_size(rhs2->type) != 2)
            goto no;
        next_token();
    } else if (tok.kind == TOK_NUM || tok.kind == TOK_CHARLIT) {
        rhs2_const = 1;
        cval = tok.val & 0xffffL;
        next_token();
    } else {
        goto no;
    }

    if (tok.kind != ';')
        goto no;

    /*
     * This recognizes:
     *     lhs = rhs1 + rhs2;
     *     lhs = rhs1 - rhs2;
     * where lhs/rhs are simple local int objects.  This is especially useful
     * for sieve's k = k + prime.
     */
    fprintf(outf, "\tld l,(ix%+d)\n", rhs1->offset);
    fprintf(outf, "\tld h,(ix%+d)\n", rhs1->offset + 1);

    if (rhs2_const) {
        if (rhs1->type & (TYPE_PTR | TYPE_PTR2))
            cval *= type_index_elem_size(rhs1->type);
        emit_ld_de_const(cval);
    } else {
        fprintf(outf, "\tld e,(ix%+d)\n", rhs2->offset);
        fprintf(outf, "\tld d,(ix%+d)\n", rhs2->offset + 1);
        if ((rhs1->type & (TYPE_PTR | TYPE_PTR2)) &&
            !(rhs2->type & (TYPE_PTR | TYPE_PTR2))) {
            int elem = type_index_elem_size(rhs1->type);
            if (elem > 1) {
                emit("\tpush hl\n");
                emit("\tex de,hl\n");
                scale_hl_by_elem_size(elem);
                emit("\tex de,hl\n");
                emit("\tpop hl\n");
            }
        }
    }

    if (op == '+') {
        emit("\tadd hl,de\n");
    } else {
        emit("\tor a\n\tsbc hl,de\n");
        if ((rhs1->type & (TYPE_PTR | TYPE_PTR2)) && rhs2 &&
            (rhs2->type & (TYPE_PTR | TYPE_PTR2))) {
            int elem = type_index_elem_size(rhs1->type);
            if (elem > 1) {
                fprintf(outf, "\tld de,%d\n", elem);
                emit_runtime_call("__divs");
            }
        }
    }

    fprintf(outf, "\tld (ix%+d),l\n", lhs->offset);
    fprintf(outf, "\tld (ix%+d),h\n", lhs->offset + 1);

    expect(';');
    return 1;

no:
    posi = save_pos;
    tok_start_pos = save_tok_start;
    line_no = save_line;
    tok_line = save_tok_line;
    tok = save_tok;
    return 0;
}

int try_gen_incdec_statement(void)
{
    int op;
    int t;
    struct Sym *s;
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    struct Token save_tok;

    save_pos = posi;
    save_tok_start = tok_start_pos;
    save_line = line_no;
    save_tok_line = tok_line;
    save_tok = tok;

    if (tok.kind == TOK_INC || tok.kind == TOK_DEC) {
        op = tok.kind;
        next_token();

        if (tok.kind == TOK_ID) {
            s = find_sym(tok.text);
            if (sym_can_ix_direct(s) || is_global_word_sym(s)) {
                next_token();
                if (tok.kind == ';') {
                    emit_incdec_sym_direct(s, op);
                    expect(';');
                    return 1;
                }
            }
        }

        posi = save_pos;
        tok_start_pos = save_tok_start;
        line_no = save_line;
        tok_line = save_tok_line;
        tok = save_tok;

        op = tok.kind;
        next_token();
        gen_lvalue_addr(&t);
        emit_incdec_addr(t, op);
        expect(';');
        return 1;
    }

    if (tok.kind == TOK_ID) {
        s = find_sym(tok.text);
        if (sym_can_ix_direct(s) || is_global_word_sym(s)) {
            next_token();
            if (tok.kind == TOK_INC || tok.kind == TOK_DEC) {
                op = tok.kind;
                next_token();
                if (tok.kind == ';') {
                    emit_incdec_sym_direct(s, op);
                    expect(';');
                    return 1;
                }
            }
        }

        posi = save_pos;
        tok_start_pos = save_tok_start;
        line_no = save_line;
        tok_line = save_tok_line;
        tok = save_tok;
    }

    if (lookahead_is_post_incdec_statement(&op)) {
        gen_lvalue_addr(&t);
        next_token();
        emit_incdec_addr(t, op);
        expect(';');
        return 1;
    }

    return 0;
}

void emit_store_a_to_sym_byte_preserve_de(struct Sym *s, int byte_off)
{
    if (sym_can_ix_direct(s)) {
        fprintf(outf, "\tld (ix%+d),a\n", s->offset + byte_off);
        return;
    }

    if (s && (s->storage == SC_LOCAL || s->storage == SC_PARAM)) {
        emit("\tpush de\n");
        emit("\tpush ix\n\tpop hl\n");
        fprintf(outf, "\tld de,%d\n", s->offset + byte_off);
        emit("\tadd hl,de\n\tld (hl),a\n");
        emit("\tpop de\n");
        return;
    }

    /* Conservative fallback for globals: preserve DE while storing A. */
    emit("\tpush de\n");
    fprintf(outf, "\tld hl,%s\n", asm_name_for(sym_asm_name(s)));
    if (byte_off)
        fprintf(outf, "\tld de,%d\n\tadd hl,de\n", byte_off);
    emit("\tld (hl),a\n\tpop de\n");
}


void emit_load_simple_byte_to_c(struct Sym *s, long val, int is_const)
{
    if (is_const) {
        fprintf(outf, "\tld c,%ld\n", val & 255L);
        return;
    }

    if (sym_can_ix_direct(s)) {
        fprintf(outf, "\tld c,(ix%+d)\n", s->offset);
    } else {
        emit("\tpush ix\n\tpop hl\n");
        fprintf(outf, "\tld de,%d\n", s->offset);
        emit("\tadd hl,de\n\tld c,(hl)\n");
    }
}

void emit_and_mask_byte_direct(struct Sym *mask_sym, int byte_index)
{
    /* A = value, mask_sym must be IX-direct.  Result remains in A. */
    emit("\tld b,a\n");
    fprintf(outf, "\tld a,(ix%+d)\n", mask_sym->offset + byte_index);
    emit("\tand b\n");
}

int try_fast_crc_update_byte_simple_args(struct Sym *dst)
{
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    struct Token save_tok;
    struct Sym *arg_crc;
    struct Sym *arg_tbl;
    struct Sym *arg_mask;
    struct Sym *arg_byte;
    long byte_val;
    int byte_is_const;

    save_pos = posi;
    save_tok_start = tok_start_pos;
    save_line = line_no;
    save_tok_line = tok_line;
    save_tok = tok;

    if (tok.kind != TOK_ID)
        goto no;
    arg_crc = find_sym(tok.text);
    if (arg_crc != dst)
        goto no;
    next_token();
    if (!accept(','))
        goto no;

    if (tok.kind != TOK_ID)
        goto no;
    arg_tbl = find_sym(tok.text);
    if (!arg_tbl || !sym_can_ix_direct(arg_tbl))
        goto no;
    next_token();
    if (!accept(','))
        goto no;

    if (tok.kind != TOK_ID)
        goto no;
    arg_mask = find_sym(tok.text);
    if (!arg_mask || !sym_can_ix_direct(arg_mask) || !type_is_long(arg_mask->type))
        goto no;
    next_token();
    if (!accept(','))
        goto no;

    arg_byte = NULL;
    byte_val = 0;
    byte_is_const = 0;
    if (tok.kind == TOK_ID) {
        arg_byte = find_sym(tok.text);
        if (!arg_byte)
            goto no;
        next_token();
    } else if (tok.kind == TOK_NUM || tok.kind == TOK_CHARLIT) {
        byte_val = tok.val;
        byte_is_const = 1;
        next_token();
    } else {
        goto no;
    }

    if (!accept(')'))
        goto no;
    if (!accept(';'))
        goto no;

    /* This direct path is tuned for compute_crc_fb(), where the CRC local is
     * outside IX's signed 8-bit displacement range.  The older generic inline
     * path remains for IX-direct locals and complex byte expressions. */
    if (sym_can_ix_direct(dst))
        goto no;
    if (!(dst->storage == SC_LOCAL || dst->storage == SC_PARAM))
        goto no;

    /* Put destination base address in the alternate HL set. */
    emit("\tpush ix\n\tpop hl\n");
    fprintf(outf, "\tld de,%d\n", dst->offset);
    emit("\tadd hl,de\n\texx\n");

    /* C = input byte. */
    emit_load_simple_byte_to_c(arg_byte, byte_val, byte_is_const);

    /* idx = original_crc_byte3 ^ input_byte.  Read byte3 through alt HL. */
    emit("\texx\n\tinc hl\n\tinc hl\n\tinc hl\n\tld a,(hl)\n\tdec hl\n\tdec hl\n\tdec hl\n\texx\n");
    emit("\txor c\n\tld l,a\n\tld h,0\n\tadd hl,hl\n\tadd hl,hl\n");

    /* DE = &tbl[idx]. */
    emit("\tld b,h\n\tld c,l\n");
    fprintf(outf, "\tld l,(ix%+d)\n\tld h,(ix%+d)\n", arg_tbl->offset, arg_tbl->offset + 1);
    emit("\tld d,b\n\tld e,c\n\tadd hl,de\n\tex de,hl\n");

    /* Store high-to-low so original crc bytes are read before overwrite. */

    /* byte3 = table[3] ^ old byte2, masked. */
    emit("\tinc de\n\tinc de\n\tinc de\n\tld a,(de)\n");
    emit("\texx\n\tinc hl\n\tinc hl\n\txor (hl)\n\tinc hl\n\texx\n");
    emit_and_mask_byte_direct(arg_mask, 3);
    emit("\texx\n\tld (hl),a\n\texx\n");

    /* byte2 = table[2] ^ old byte1, masked. */
    emit("\tdec de\n\tld a,(de)\n");
    emit("\texx\n\tdec hl\n\tdec hl\n\txor (hl)\n\tinc hl\n\texx\n");
    emit_and_mask_byte_direct(arg_mask, 2);
    emit("\texx\n\tld (hl),a\n\texx\n");

    /* byte1 = table[1] ^ old byte0, masked. */
    emit("\tdec de\n\tld a,(de)\n");
    emit("\texx\n\tdec hl\n\tdec hl\n\txor (hl)\n\tinc hl\n\texx\n");
    emit_and_mask_byte_direct(arg_mask, 1);
    emit("\texx\n\tld (hl),a\n\texx\n");

    /* byte0 = table[0], masked. */
    emit("\tdec de\n\tld a,(de)\n");
    emit_and_mask_byte_direct(arg_mask, 0);
    emit("\texx\n\tdec hl\n\tld (hl),a\n\texx\n");

    return 1;

no:
    posi = save_pos;
    tok_start_pos = save_tok_start;
    line_no = save_line;
    tok_line = save_tok_line;
    tok = save_tok;
    return 0;
}

int try_fast_crc_update_byte_statement(void)
{
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    struct Token save_tok;
    struct Sym *dst;
    int t_arg1;
    int t_arg2;
    int t_arg3;
    int t_arg4;
    int k;
    int use_alt_dst;

    if (tok.kind != TOK_ID)
        return 0;

    save_pos = posi;
    save_tok_start = tok_start_pos;
    save_line = line_no;
    save_tok_line = tok_line;
    save_tok = tok;

    dst = find_sym(tok.text);
    if (!dst || !type_is_long(dst->type))
        goto no;

    next_token();
    if (!accept('='))
        goto no;

    if (tok.kind != TOK_ID || strcmp(tok.text, "crc_update_byte") != 0)
        goto no;

    next_token();
    if (!accept('('))
        goto no;

    if (try_fast_crc_update_byte_simple_args(dst))
        return 1;

    /*
     * Fast path for crc.c's inner loop:
     *
     *     crc = crc_update_byte(crc, tbl, mask32, byte_expr);
     *
     * crc_update_byte does:
     *     idx = ((crc >> 24) ^ byte) & 0xff;
     *     crc = ((crc << 8) ^ tbl[idx]) & mask32;
     *
     * Stack layout after evaluating arguments below:
     *     sp+0..3  mask32     (low word, high word)
     *     sp+4..5  tbl pointer
     *     sp+6..9  original crc (low word, high word)
     *
     * The generated byte stores use:
     *     result byte0 = tbl[0] ^ 0
     *     result byte1 = tbl[1] ^ crc byte0
     *     result byte2 = tbl[2] ^ crc byte1
     *     result byte3 = tbl[3] ^ crc byte2
     * and each byte is ANDed with the corresponding mask byte.
     */

    expr_result_dead = 0;
    gen_assign();                       /* arg1: crc */
    t_arg1 = g_expr_type;
    if (!type_is_long(t_arg1))
        emit_extend_to_long_typed(t_arg1);
    emit("\tpush de\n\tpush hl\n");

    expect(',');

    gen_assign();                       /* arg2: tbl */
    t_arg2 = g_expr_type;
    (void)t_arg2;
    emit("\tpush hl\n");

    expect(',');

    gen_assign();                       /* arg3: mask32 */
    t_arg3 = g_expr_type;
    if (!type_is_long(t_arg3))
        emit_extend_to_long_typed(t_arg3);
    emit("\tpush de\n\tpush hl\n");

    expect(',');

    gen_assign();                       /* arg4: byte */
    t_arg4 = g_expr_type;
    (void)t_arg4;

    expect(')');
    expect(';');

    use_alt_dst = (!sym_can_ix_direct(dst) &&
                   (dst->storage == SC_LOCAL || dst->storage == SC_PARAM));

    if (use_alt_dst) {
        /*
         * Keep the non-IX-direct destination pointer in the alternate HL
         * register pair.  That avoids recomputing ix+offset and push/pop'ing
         * DE for each of the four result-byte stores in compute_crc_fb().
         * Save the byte expression result while EXX makes the main register
         * set temporarily undefined.
         */
        emit("\tpush hl\n");
        emit("\tpush ix\n\tpop hl\n");
        fprintf(outf, "\tld de,%d\n", dst->offset);
        emit("\tadd hl,de\n");
        emit("\texx\n");
        emit("\tpop hl\n");
    }

    /* C = input byte.  idx = high byte of crc ^ input byte. */
    emit("\tld c,l\n");
    emit("\tld hl,9\n\tadd hl,sp\n\tld a,(hl)\n\txor c\n\tld l,a\n\tld h,0\n");

    /* HL = idx * 4. */
    emit("\tadd hl,hl\n\tadd hl,hl\n");

    /* DE = tbl pointer, then DE = &tbl[idx]. */
    emit("\tld b,h\n\tld c,l\n");
    emit("\tld hl,4\n\tadd hl,sp\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n");
    emit("\tld h,b\n\tld l,c\n\tadd hl,de\n\tex de,hl\n");

    /* result byte 0: table byte 0, masked by mask byte 0. */
    emit("\tld a,(de)\n");
    emit("\tld hl,0\n\tadd hl,sp\n\tand (hl)\n");
    if (use_alt_dst)
        emit("\texx\n\tld (hl),a\n\tinc hl\n\texx\n");
    else
        emit_store_a_to_sym_byte_preserve_de(dst, 0);
    emit("\tinc de\n");

    /* result byte 1: table byte 1 ^ original crc byte 0. */
    emit("\tld a,(de)\n");
    emit("\tld hl,6\n\tadd hl,sp\n\txor (hl)\n");
    emit("\tld hl,1\n\tadd hl,sp\n\tand (hl)\n");
    if (use_alt_dst)
        emit("\texx\n\tld (hl),a\n\tinc hl\n\texx\n");
    else
        emit_store_a_to_sym_byte_preserve_de(dst, 1);
    emit("\tinc de\n");

    /* result byte 2: table byte 2 ^ original crc byte 1. */
    emit("\tld a,(de)\n");
    emit("\tld hl,7\n\tadd hl,sp\n\txor (hl)\n");
    emit("\tld hl,2\n\tadd hl,sp\n\tand (hl)\n");
    if (use_alt_dst)
        emit("\texx\n\tld (hl),a\n\tinc hl\n\texx\n");
    else
        emit_store_a_to_sym_byte_preserve_de(dst, 2);
    emit("\tinc de\n");

    /* result byte 3: table byte 3 ^ original crc byte 2. */
    emit("\tld a,(de)\n");
    emit("\tld hl,8\n\tadd hl,sp\n\txor (hl)\n");
    emit("\tld hl,3\n\tadd hl,sp\n\tand (hl)\n");
    if (use_alt_dst)
        emit("\texx\n\tld (hl),a\n\texx\n");
    else
        emit_store_a_to_sym_byte_preserve_de(dst, 3);

    for (k = 0; k < 10; ++k)
        emit("\tinc sp\n");

    return 1;

no:
    posi = save_pos;
    tok_start_pos = save_tok_start;
    line_no = save_line;
    tok_line = save_tok_line;
    tok = save_tok;
    return 0;
}

void skip_initializer_or_decl_tail(void);

