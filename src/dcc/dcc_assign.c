/*
 * dcc_assign.c - assignment, float r-values, and expression entry points.
 *
 * Assignment lowering (plain and compound, scalar/struct/bitfield/array
 * element), floating-point literal and r-value materialisation, and the
 * top-level gen_expr/gen_expr_no_comma entry points that the statement layer
 * calls into.
 *
 * MODULE: compiled as its own translation unit; shared declarations are in dcc.h.
 * Source provenance: monolith src/ddc.c lines 11521-12417.
 */

#include "dcc.h"
#include "dcc_ast.h"
void emit_load_float_bits(unsigned long bits)
{
    if (!scan_mode) {
        fprintf(outf, "\tld hl,%lu\n", bits & 0xffffUL);
        fprintf(outf, "\tld de,%lu\n", (bits >> 16) & 0xffffUL);
    }
}

int parse_float_assignment_literal(unsigned long *bits)
{
    if (tok.kind != TOK_FLOATLIT)
        return 0;

    bits[0] = parse_float_literal_bits(tok.text);
    next_token();
    return 1;
}

/* True when the current token is an infix binary/ternary operator that can
 * follow a primary expression.  Used to detect "a + b" vs "a;" in a float
 * rvalue context: if the token continues an expression, the simple fast path
 * is not applicable and the caller must fall back to gen_assign(). */
static int tok_continues_expr(void)
{
    int k = tok.kind;
    return k == '+' || k == '-' || k == '*' || k == '/' || k == '%' ||
           k == '&' || k == '|' || k == '^' || k == '<' || k == '>' || k == '?' ||
           k == TOK_EQ || k == TOK_NE || k == TOK_LE || k == TOK_GE ||
           k == TOK_ANDAND || k == TOK_OROR || k == TOK_SHL || k == TOK_SHR;
}

int try_emit_float_rvalue_dehl(void)
{
    struct Sym *rs;
    unsigned long bits;

    if (tok.kind == '-') {
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

        next_token();
        if (try_emit_float_rvalue_dehl()) {
            emit("\tld a,d\n\txor 80h\n\tld d,a\n");
            return 1;
        }

        posi = save_pos;
        tok_start_pos = save_tok_start;
        line_no = save_line;
        tok_line = save_tok_line;
        tok = save_tok;
    }

    if (tok.kind == TOK_FLOATLIT) {
        long sv_pos = posi;
        long sv_tok_start = tok_start_pos;
        int sv_line = line_no;
        int sv_tok_line = tok_line;
        struct Token sv_tok = tok;
        parse_float_assignment_literal(&bits);
        if (!tok_continues_expr()) {
            emit_load_float_bits(bits);
            g_expr_type = TYPE_FLOAT;
            return 1;
        }
        posi = sv_pos;
        tok_start_pos = sv_tok_start;
        line_no = sv_line;
        tok_line = sv_tok_line;
        tok = sv_tok;
    }

    if (tok.kind == '*') {
        long save_pos;
        long save_tok_start;
        int save_line;
        int save_tok_line;
        struct Token save_tok;
        int rt;

        save_pos = posi;
        save_tok_start = tok_start_pos;
        save_line = line_no;
        save_tok_line = tok_line;
        save_tok = tok;

        gen_lvalue_addr(&rt);
        if (type_is_float(rt) && !tok_continues_expr()) {
            emit_load_from_hl(rt);
            g_expr_type = rt;
            return 1;
        }

        posi = save_pos;
        tok_start_pos = save_tok_start;
        line_no = save_line;
        tok_line = save_tok_line;
        tok = save_tok;
    }

    if (tok.kind == TOK_ID) {
        /* Direct call returning float, e.g. f_id(x).  Must check this before
         * treating a function name as a scalar addressable object. */
        {
            long save_pos2;
            long save_tok_start2;
            int save_line2;
            int save_tok_line2;
            struct Token save_tok2;
            char cname[64];

            save_pos2 = posi;
            save_tok_start2 = tok_start_pos;
            save_line2 = line_no;
            save_tok_line2 = tok_line;
            save_tok2 = tok;

            strncpy(cname, tok.text, sizeof(cname) - 1);
            cname[sizeof(cname) - 1] = 0;
            rs = find_global(cname);
            next_token();
            if (rs && rs->storage == SC_FUNC && type_is_float(rs->type) && tok.kind == '(') {
                posi = save_pos2;
                tok_start_pos = save_tok_start2;
                line_no = save_line2;
                tok_line = save_tok_line2;
                tok = save_tok2;
                gen_expr();
                return type_is_float(g_expr_type);
            }

            posi = save_pos2;
            tok_start_pos = save_tok_start2;
            line_no = save_line2;
            tok_line = save_tok_line2;
            tok = save_tok2;
        }

        rs = find_sym(tok.text);
        if (rs && type_is_float(rs->type) && !rs->is_array && rs->storage != SC_FUNC) {
            long sv_pos = posi;
            long sv_tok_start = tok_start_pos;
            int sv_line = line_no;
            int sv_tok_line = tok_line;
            struct Token sv_tok = tok;
            next_token();
            if (tok_continues_expr()) {
                posi = sv_pos;
                tok_start_pos = sv_tok_start;
                line_no = sv_line;
                tok_line = sv_tok_line;
                tok = sv_tok;
                return 0;
            }
            if (sym_can_ix_direct(rs)) {
                emit_load_sym_value_direct(rs);
            } else {
                emit_load_sym_addr(rs);
                emit_load_from_hl(rs->type);
            }
            g_expr_type = rs->type;
            return 1;
        }

        /* Float array elements and float struct fields are valid
         * storage-only rvalues even though general float expressions are
         * still unsupported.  Examples:
         *     b = a[i];
         *     b = s.f;
         *     b = ps->f;
         * gen_lvalue_addr leaves the selected object's address in HL; for
         * a 4-byte float, load the raw bits into DE:HL. */
        {
            long save_pos;
            long save_tok_start;
            int save_line;
            int save_tok_line;
            struct Token save_tok;
            int rt;

            save_pos = posi;
            save_tok_start = tok_start_pos;
            save_line = line_no;
            save_tok_line = tok_line;
            save_tok = tok;

            gen_lvalue_addr(&rt);
            if (type_is_float(rt) && !tok_continues_expr()) {
                emit_load_from_hl(rt);
                g_expr_type = rt;
                return 1;
            }

            posi = save_pos;
            tok_start_pos = save_tok_start;
            line_no = save_line;
            tok_line = save_tok_line;
            tok = save_tok;
        }
    }

    return 0;
}


void emit_global_byte_array_index_addr(struct Sym *arr, struct Sym *idx_sym, long idx_const, int has_const)
{
    emit_extrn_if_needed(arr);
    if (has_const) {
        if (idx_const == 0)
            fprintf(outf, "\tld hl,%s\n", asm_name_for(sym_asm_name(arr)));
        else
            fprintf(outf, "\tld hl,%s+%ld\n", asm_name_for(sym_asm_name(arr)), idx_const & 0xffffL);
    } else {
        fprintf(outf, "\tld hl,%s\n", asm_name_for(sym_asm_name(arr)));
        fprintf(outf, "\tld e,(ix%+d)\n", idx_sym->offset);
        emit("\tld d,0\n");
        emit("\tadd hl,de\n");
    }
}

int try_fast_global_byte_array_store(void)
{
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    struct Token save_tok;
    struct Sym *arr;
    struct Sym *idx_sym;
    struct Sym *rhs_sym;
    long idx_const;
    long rhs_const;
    int idx_has_const;
    int rhs_kind;       /* 1 = sym, 2 = const */

    save_pos = posi;
    save_tok_start = tok_start_pos;
    save_line = line_no;
    save_tok_line = tok_line;
    save_tok = tok;

    if (tok.kind != TOK_ID)
        goto fail;

    arr = find_sym(tok.text);
    if (!arr || arr->storage != SC_GLOBAL || !arr->is_array || type_size(arr->type) != 1)
        goto fail;

    next_token();
    if (!accept('['))
        goto fail;

    idx_sym = NULL;
    idx_const = 0;
    idx_has_const = 0;

    if (tok.kind == TOK_NUM || tok.kind == TOK_CHARLIT) {
        idx_const = tok.val;
        idx_has_const = 1;
        next_token();
    } else if (tok.kind == TOK_ID) {
        idx_sym = find_sym(tok.text);
        if (!sym_can_ix_direct(idx_sym) || type_size(idx_sym->type) != 1)
            goto fail;
        next_token();
    } else {
        goto fail;
    }

    if (!accept(']'))
        goto fail;

    if (!accept('='))
        goto fail;

    rhs_kind = 0;
    rhs_sym = NULL;
    rhs_const = 0;

    if (tok.kind == TOK_ID) {
        rhs_sym = find_sym(tok.text);
        if (!sym_can_ix_direct(rhs_sym) || type_size(rhs_sym->type) != 1)
            goto fail;
        rhs_kind = 1;
        next_token();
    } else if (tok.kind == TOK_NUM || tok.kind == TOK_CHARLIT) {
        rhs_const = tok.val;
        if (rhs_const < 0 || rhs_const > 255)
            goto fail;
        rhs_kind = 2;
        next_token();
    } else {
        goto fail;
    }

    /*
     * Keep this statement-only.  If assignment value is needed, or if a larger
     * expression follows, let the normal C expression path preserve semantics.
     */
    if (!expr_result_dead || tok.kind != ';')
        goto fail;

    emit_global_byte_array_index_addr(arr, idx_sym, idx_const, idx_has_const);
    if (rhs_kind == 1) {
        fprintf(outf, "\tld a,(ix%+d)\n", rhs_sym->offset);
        emit("\tld (hl),a\n");
    } else {
        fprintf(outf, "\tld (hl),%ld\n", rhs_const & 255);
    }

    g_expr_type = arr->type;
    return 1;

fail:
    posi = save_pos;
    tok_start_pos = save_tok_start;
    line_no = save_line;
    tok_line = save_tok_line;
    tok = save_tok;
    return 0;
}

/* Fast path for dead-result assignment to a global struct's byte field:
 *   global_id . field = expr ;
 * Evaluates the RHS into HL, then stores A = L directly to sym+offset,
 * bypassing the runtime HL-address computation and push/pop shuffle. */
static int try_fast_global_struct_byte_store(void)
{
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    struct Token save_tok;
    struct Sym *struct_sym;
    struct FieldDef *fd;

    if (!expr_result_dead)
        return 0;

    save_pos = posi;
    save_tok_start = tok_start_pos;
    save_line = line_no;
    save_tok_line = tok_line;
    save_tok = tok;

    if (tok.kind != TOK_ID)
        goto fail;

    struct_sym = find_sym(tok.text);
    if (!struct_sym || struct_sym->storage != SC_GLOBAL ||
        !type_is_struct_object(struct_sym->type))
        goto fail;

    next_token();
    if (tok.kind != '.')
        goto fail;
    next_token();
    if (tok.kind != TOK_ID)
        goto fail;

    fd = find_field_def(type_struct_id(struct_sym->type), tok.text);
    if (!fd || fd->bit_width > 0 || type_size(fd->type) != 1)
        goto fail;

    next_token();
    if (tok.kind != '=')
        goto fail;
    next_token();

    /* Committed: evaluate RHS into HL, then emit direct byte store. */
    expr_result_dead = 0;
    gen_assign();
    expr_result_dead = 1;

    emit_extrn_if_needed(struct_sym);
    emit("\tld a,l\n");
    if (fd->offset == 0)
        fprintf(outf, "\tld (%s),a\n", asm_name_for(sym_asm_name(struct_sym)));
    else
        fprintf(outf, "\tld (%s+%d),a\n", asm_name_for(sym_asm_name(struct_sym)), fd->offset);

    g_expr_type = fd->type;
    g_long_from16 = 0;
    return 1;

fail:
    posi = save_pos;
    tok_start_pos = save_tok_start;
    line_no = save_line;
    tok_line = save_tok_line;
    tok = save_tok;
    return 0;
}

void gen_assign(void)
{
    if (try_gen_const_expr())
        return;

    int t, op;
    int want_dead;
    struct Sym *direct_sym;
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    struct Token save_tok;
    struct Sym *rhs_sym;
    long rhs_val;
    int fast_rhs;
    int rhs_type;
    int common_type;
    int bf_width;
    int bf_shift;
    unsigned int bf_mask;

    if (try_fast_global_byte_array_store())
        return;

    if (try_fast_global_struct_byte_store())
        return;

    direct_sym = NULL;

    if (tok.kind == TOK_ID) {
        direct_sym = find_sym(tok.text);

        if (!sym_can_ix_direct(direct_sym) && !is_global_word_sym(direct_sym))
            direct_sym = NULL;

        if (direct_sym) {
            save_pos = posi;
            save_tok_start = tok_start_pos;
            save_line = line_no;
            save_tok_line = tok_line;
            save_tok = tok;

            next_token();

            if (!is_assignment_token(tok.kind)) {
                posi = save_pos;
                tok_start_pos = save_tok_start;
                line_no = save_line;
                tok_line = save_tok_line;
                tok = save_tok;
                direct_sym = NULL;
            }
        }
    }

    if (direct_sym) {
        if (type_is_struct_object(direct_sym->type)) {
            posi = save_pos;
            tok_start_pos = save_tok_start;
            line_no = save_line;
            tok_line = save_tok_line;
            tok = save_tok;
            direct_sym = NULL;
            goto normal_assign;
        }
        want_dead = expr_result_dead;
        op = tok.kind;
        next_token();

        /* Fast path for simple dead-result local compound updates, e.g.
         *     i += step;
         *     i -= 3;
         * This avoids push/pop shuffling in tight loops.  Only fire when
         * the RHS is a single constant or IX-direct local/param and the
         * assignment expression value is not needed.
         */
        if (!type_is_long(direct_sym->type) && !type_is_float(direct_sym->type) &&
            want_dead && (op == TOK_ADDEQ || op == TOK_SUBEQ)) {
            fast_rhs = 0;
            rhs_sym = NULL;
            rhs_val = 0;

            if (tok.kind == TOK_NUM || tok.kind == TOK_CHARLIT) {
                rhs_val = tok.val;
                next_token();
                fast_rhs = 1;
            } else if (tok.kind == TOK_ID) {
                rhs_sym = find_sym(tok.text);
                if (sym_can_ix_direct(rhs_sym)) {
                    next_token();
                    fast_rhs = 2;
                }
            }

            if (fast_rhs && tok.kind == ';') {
                emit_load_sym_value_direct(direct_sym);
                if (fast_rhs == 1) {
                    long scaled = rhs_val;
                    if (direct_sym->type & (TYPE_PTR | TYPE_PTR2))
                        scaled *= type_index_elem_size(direct_sym->type);
                    emit_ld_de_const(scaled);
                } else {
                    emit_load_sym_de_direct(rhs_sym);
                    if (direct_sym->type & (TYPE_PTR | TYPE_PTR2)) {
                        int elem = type_index_elem_size(direct_sym->type);
                        if (elem > 1) {
                            emit("\tpush hl\n");     /* save pointer */
                            emit("\tex de,hl\n");    /* HL = step */
                            scale_hl_by_elem_size(elem);
                            emit("\tex de,hl\n");    /* DE = scaled step */
                            emit("\tpop hl\n");      /* HL = pointer */
                        }
                    }
                }

                if (op == TOK_ADDEQ) {
                    emit("\tadd hl,de\n");
                } else {
                    emit("\tor a\n\tsbc hl,de\n");
                }

                emit_store_hl_to_sym_direct(direct_sym);
                g_long_from16 = 0;
                return;
            }

            /* Fall back to normal parsing if RHS was not exactly simple. */
            posi = save_pos;
            tok_start_pos = save_tok_start;
            line_no = save_line;
            tok_line = save_tok_line;
            tok = save_tok;
            direct_sym = NULL;
            goto normal_assign;
        }

        if (op == '=') {
            if (type_size(direct_sym->type) == 1 && tok.kind == TOK_ID) {
                struct Sym *bs;
                long rhs_save_pos;
                long rhs_save_tok_start;
                int rhs_save_line;
                int rhs_save_tok_line;
                struct Token rhs_save_tok;

                rhs_save_pos = posi;
                rhs_save_tok_start = tok_start_pos;
                rhs_save_line = line_no;
                rhs_save_tok_line = tok_line;
                rhs_save_tok = tok;

                bs = find_sym(tok.text);
                if (sym_can_ix_direct(bs) && type_size(bs->type) == 1) {
                    next_token();
                    if (tok.kind == ';' || tok.kind == ',' || tok.kind == ')' || tok.kind == ']') {
                        fprintf(outf, "\tld a,(ix%+d)\n", bs->offset);
                        fprintf(outf, "\tld (ix%+d),a\n", direct_sym->offset);
                        g_expr_type = direct_sym->type;
                        g_long_from16 = 0;
                        return;
                    }
                }

                posi = rhs_save_pos;
                tok_start_pos = rhs_save_tok_start;
                line_no = rhs_save_line;
                tok_line = rhs_save_tok_line;
                tok = rhs_save_tok;
            }
            if (type_size(direct_sym->type) == 1 && (tok.kind == TOK_NUM || tok.kind == TOK_CHARLIT) && tok.val >= 0 && tok.val <= 255) {
                long rhs_save_pos;
                long rhs_save_tok_start;
                int rhs_save_line;
                int rhs_save_tok_line;
                struct Token rhs_save_tok;
                long rhs_byte_val;

                rhs_save_pos = posi;
                rhs_save_tok_start = tok_start_pos;
                rhs_save_line = line_no;
                rhs_save_tok_line = tok_line;
                rhs_save_tok = tok;
                rhs_byte_val = tok.val;

                next_token();
                if (tok.kind == ';' || tok.kind == ',' || tok.kind == ')' || tok.kind == ']') {
                    fprintf(outf, "\tld (ix%+d),%ld\n", direct_sym->offset, rhs_byte_val & 255);
                    g_expr_type = direct_sym->type;
                    g_long_from16 = 0;
                    return;
                }

                posi = rhs_save_pos;
                tok_start_pos = rhs_save_tok_start;
                line_no = rhs_save_line;
                tok_line = rhs_save_tok_line;
                tok = rhs_save_tok;
            }

            if (type_is_float(direct_sym->type)) {
                expr_result_dead = 0;
                gen_assign();
                expr_result_dead = want_dead;
                if (!type_is_float(g_expr_type))
                    emit_convert_int_to_float(g_expr_type);
                emit_store_hl_to_sym_direct(direct_sym);
                g_long_from16 = 0;
                return;
            }

            expr_result_dead = 0;
            gen_assign();
            expr_result_dead = want_dead;

            if (type_is_float(g_expr_type)) {
                emit_convert_float_to_intlike(direct_sym->type);
            }
            if (type_is_long(direct_sym->type) && !type_is_long(g_expr_type))
                emit_extend_to_long_typed(g_expr_type);
            else if (type_size(direct_sym->type) > 1 && !type_is_long(g_expr_type))
                emit_promote_byte_to_int(g_expr_type);
            emit_store_hl_to_sym_direct(direct_sym);
            g_long_from16 = 0;
            return;
        }

        rhs_type = peek_simple_unary_type();

        /* Compound assignment uses the usual arithmetic conversions for
         * the operation, then converts the result back to the LHS type.
         * The old path used the LHS width too early, so cases such as
         *     int i; long l; i += l;
         * silently discarded the high word before doing the operation. */
        if (!type_is_long(direct_sym->type) &&
            !(direct_sym->type & (TYPE_PTR | TYPE_PTR2)) &&
            (op == TOK_ADDEQ || op == TOK_SUBEQ || op == TOK_MULEQ ||
             op == TOK_DIVEQ || op == TOK_MODEQ || op == TOK_ANDEQ ||
             op == TOK_OREQ  || op == TOK_XOREQ)) {
            common_type = common_arith_type(direct_sym->type, rhs_type);
            if (type_is_long(common_type)) {
                emit_load_sym_value_direct(direct_sym);
                emit_cast_16_to_common(direct_sym->type, common_type);
                emit("\tpush de\n\tpush hl\n");

                expr_result_dead = 0;
                gen_assign();
                expr_result_dead = want_dead;
                emit_cast_16_to_common(g_expr_type, common_type);

                if (op == TOK_ADDEQ) gen_binop32('+', common_type);
                else if (op == TOK_SUBEQ) gen_binop32('-', common_type);
                else if (op == TOK_MULEQ) gen_binop32('*', common_type);
                else if (op == TOK_DIVEQ) gen_binop32('/', common_type);
                else if (op == TOK_MODEQ) gen_binop32('%', common_type);
                else if (op == TOK_ANDEQ) gen_binop32('&', common_type);
                else if (op == TOK_OREQ)  gen_binop32('|', common_type);
                else if (op == TOK_XOREQ) gen_binop32('^', common_type);

                /* Convert back to the non-long LHS by storing the low word. */
                emit_store_hl_to_sym_direct(direct_sym);
                g_long_from16 = 0;
                return;
            }
        }

        emit_load_sym_value_direct(direct_sym);
        if ((op == TOK_SHLEQ || op == TOK_SHREQ) && type_is_long(direct_sym->type) &&
            (tok.kind == TOK_NUM || tok.kind == TOK_CHARLIT)) {
            long scount;
            scount = tok.val;
            next_token();
            if (!emit_shift_const_long(op, direct_sym->type, scount)) {
                fprintf(outf, "\tld b,%ld\n", scount & 255L);
                emit_shift_loop(op, direct_sym->type);
            }
            emit_store_hl_to_sym_direct(direct_sym);
            g_long_from16 = 0;
            return;
        }

        if ((op == TOK_SHLEQ || op == TOK_SHREQ) &&
            !type_is_long(direct_sym->type) && !type_is_float(direct_sym->type) &&
            (tok.kind == TOK_NUM || tok.kind == TOK_CHARLIT)) {
            int scount = (int)(tok.val & 255);
            next_token();
            if (op == TOK_SHLEQ)
                emit_shift_left_hl_const(scount);
            else if (direct_sym->type & TYPE_UNSIGNED)
                emit_logical_shift_right_hl_const(scount);
            else
                emit_arith_shift_right_hl_const(scount);
            emit_store_hl_to_sym_direct(direct_sym);
            g_long_from16 = 0;
            return;
        }

        if (type_is_float(direct_sym->type) &&
            (op == TOK_MULEQ || op == TOK_DIVEQ) &&
            try_consume_float_pow2_compound_scale(op)) {
            emit_store_hl_to_sym_direct(direct_sym);
            g_expr_type = direct_sym->type;
            g_long_from16 = 0;
            return;
        }

        if (type_is_long(direct_sym->type) || type_is_float(direct_sym->type))
            emit("\tpush de\n\tpush hl\n");
        else
            emit("\tpush hl\n");

        expr_result_dead = 0;
        gen_assign();
        expr_result_dead = want_dead;

        if (op == TOK_SHLEQ || op == TOK_SHREQ) {
            if (type_is_long(direct_sym->type)) {
                emit("\tld b,l\n\tpop hl\n\tpop de\n");
            } else {
                emit("\tld b,l\n\tpop hl\n");
            }
            emit_shift_loop(op, direct_sym->type);
            emit_store_hl_to_sym_direct(direct_sym);
            g_long_from16 = 0;
            return;
        }

        if (type_is_float(direct_sym->type) && (op == TOK_ADDEQ || op == TOK_SUBEQ || op == TOK_MULEQ || op == TOK_DIVEQ)) {
            if (!type_is_float(g_expr_type))
                emit_convert_int_to_float(g_expr_type);
            emit("\tpush de\n\tpush hl\n");
            emit_runtime_call(op == TOK_MULEQ ? "__fmul" : (op == TOK_DIVEQ ? "__fdiv" : (op == TOK_ADDEQ ? "__fadd" : "__fsub")));
            emit("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n");
            emit_store_hl_to_sym_direct(direct_sym);
            g_long_from16 = 0;
            return;
        }

        if (type_is_long(direct_sym->type)) {
            if (!type_is_long(g_expr_type))
                emit_extend_to_long_typed(g_expr_type);
            if (op == TOK_ADDEQ) gen_binop32('+', direct_sym->type);
            else if (op == TOK_SUBEQ) gen_binop32('-', direct_sym->type);
            else if (op == TOK_MULEQ) gen_binop32('*', direct_sym->type);
            else if (op == TOK_DIVEQ) gen_binop32('/', direct_sym->type);
            else if (op == TOK_MODEQ) gen_binop32('%', direct_sym->type);
            else if (op == TOK_ANDEQ) gen_binop32('&', direct_sym->type);
            else if (op == TOK_OREQ)  gen_binop32('|', direct_sym->type);
            else if (op == TOK_XOREQ) gen_binop32('^', direct_sym->type);
        } else {
            if ((direct_sym->type & (TYPE_PTR | TYPE_PTR2)) && (op == TOK_ADDEQ || op == TOK_SUBEQ)) {
                int elem = type_index_elem_size(direct_sym->type);
                scale_hl_by_elem_size(elem);
            }
            emit("\tex de,hl\n\tpop hl\n");
            common_type = common_arith_type(direct_sym->type, g_expr_type);
            if (op == TOK_ADDEQ) gen_binop_typed('+', common_type);
            else if (op == TOK_SUBEQ) gen_binop_typed('-', common_type);
            else if (op == TOK_MULEQ) gen_binop_typed('*', common_type);
            else if (op == TOK_DIVEQ) gen_binop_typed('/', common_type);
            else if (op == TOK_MODEQ) gen_binop_typed('%', common_type);
            else if (op == TOK_ANDEQ) gen_binop_typed('&', common_type);
            else if (op == TOK_OREQ) gen_binop_typed('|', common_type);
            else if (op == TOK_XOREQ) gen_binop_typed('^', common_type);
        }

        emit_store_hl_to_sym_direct(direct_sym);
        g_long_from16 = 0;
        return;
    }

normal_assign:
    if (lookahead_is_assignment()) {
        want_dead = expr_result_dead;

        gen_lvalue_addr(&t);
        bf_width = current_field_bit_width;
        bf_shift = current_field_bit_shift;
        bf_mask = current_field_bit_mask;
        op = tok.kind;
        next_token();

        if (op == '=') {
            if (type_is_struct_object(t)) {
                int rt;
                emit("\tpush hl\n"); /* push destination address */
                if (try_emit_struct_return_call_assignment(t))
                    return;
                gen_lvalue_addr(&rt);
                if (!same_struct_type(t, rt))
                    error_here("incompatible struct assignment");
                emit("\tex de,hl\n"); /* DE = source */
                emit("\tpop hl\n");   /* HL = destination */
                emit_copy_de_to_hl_bytes(type_size(t));
                g_expr_type = t;
                g_long_from16 = 0;
                return;
            }

            emit("\tpush hl\n"); /* push address */

            if (type_is_float(t)) {
                if (try_emit_float_rvalue_dehl()) {
                    emit_store_de_to_addr_hl(t);  /* pops address */
                    g_long_from16 = 0;
                    return;
                }
                expr_result_dead = 0;
                gen_assign();
                expr_result_dead = want_dead;
                if (!type_is_float(g_expr_type))
                    emit_convert_int_to_float(g_expr_type);
                emit_store_de_to_addr_hl(t);  /* pops address */
                g_long_from16 = 0;
                return;
            }

            /* The RHS value is needed for the store even when the whole
             * assignment statement result is dead.  This preserves cases
             * such as a = b = 1.
             */
            expr_result_dead = 0;
            gen_assign();
            expr_result_dead = want_dead;
            if (type_is_float(g_expr_type)) {
                emit_convert_float_to_intlike(t);
            }
            current_field_bit_width = bf_width;
            current_field_bit_shift = bf_shift;
            current_field_bit_mask = bf_mask;

            if (current_field_bit_width > 0) {
                emit_store_bitfield_from_hl();
            } else if (type_is_long(t)) {
                if (!type_is_long(g_expr_type))
                    emit_extend_to_long_typed(g_expr_type);
                /* emit_store_de_to_addr_hl for 32-bit pops the address itself */
                emit_store_de_to_addr_hl(t);
                /* result: value gone (address popped inside store) */
            } else {
                emit("\tex de,hl\n\tpop hl\n");
                emit_store_de_to_addr_hl(t);
                if (!want_dead)
                    emit("\tex de,hl\n");
            }
            g_long_from16 = 0;
            return;
        }

        if ((op == TOK_SHLEQ || op == TOK_SHREQ) && type_is_long(t) &&
            (tok.kind == TOK_NUM || tok.kind == TOK_CHARLIT)) {
            long scount;
            scount = tok.val;
            next_token();

            emit("\tpush hl\n");
            emit_load_from_hl(t);
            if (!emit_shift_const_long(op, t, scount)) {
                fprintf(outf, "\tld b,%ld\n", scount & 255L);
                emit_shift_loop(op, t);
            }

            emit("\tld b,d\n\tld c,e\n");
            emit("\tpop de\n");
            emit("\tex de,hl\n");
            emit("\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tinc hl\n\tld (hl),c\n\tinc hl\n\tld (hl),b\n");
            if (!want_dead) {
                emit("\tex de,hl\n");
                emit("\tld d,b\n\tld e,c\n");
            }
            g_expr_type = t;
            g_long_from16 = 0;
            return;
        }

        rhs_type = peek_simple_unary_type();

        emit("\tpush hl\n");

        if (!type_is_long(t) && !(t & (TYPE_PTR | TYPE_PTR2)) &&
            (op == TOK_ADDEQ || op == TOK_SUBEQ || op == TOK_MULEQ ||
             op == TOK_DIVEQ || op == TOK_MODEQ || op == TOK_ANDEQ ||
             op == TOK_OREQ  || op == TOK_XOREQ)) {
            common_type = common_arith_type(t, rhs_type);
            if (type_is_long(common_type)) {
                emit_load_from_hl(t);
                emit_cast_16_to_common(t, common_type);
                emit("\tpush de\n\tpush hl\n");

                expr_result_dead = 0;
                gen_assign();
                expr_result_dead = want_dead;
                emit_cast_16_to_common(g_expr_type, common_type);

                if (op == TOK_ADDEQ) gen_binop32('+', common_type);
                else if (op == TOK_SUBEQ) gen_binop32('-', common_type);
                else if (op == TOK_MULEQ) gen_binop32('*', common_type);
                else if (op == TOK_DIVEQ) gen_binop32('/', common_type);
                else if (op == TOK_MODEQ) gen_binop32('%', common_type);
                else if (op == TOK_ANDEQ) gen_binop32('&', common_type);
                else if (op == TOK_OREQ)  gen_binop32('|', common_type);
                else if (op == TOK_XOREQ) gen_binop32('^', common_type);

                emit("\tex de,hl\n\tpop hl\n");
                emit_store_de_to_addr_hl(t);
                if (!want_dead)
                    emit("\tex de,hl\n");
                g_long_from16 = 0;
                return;
            }
        }

        emit_load_from_hl(t);

        if (type_is_float(t) &&
            (op == TOK_MULEQ || op == TOK_DIVEQ) &&
            try_consume_float_pow2_compound_scale(op)) {
            emit_store_de_to_addr_hl(t); /* pops saved lvalue address */
            g_expr_type = t;
            g_long_from16 = 0;
            return;
        }

        if (type_is_long(t) || type_is_float(t))
            emit("\tpush de\n\tpush hl\n");
        else
            emit("\tpush hl\n");

        expr_result_dead = 0;
        gen_assign();
        expr_result_dead = want_dead;

        if (op == TOK_SHLEQ || op == TOK_SHREQ) {
            if (type_is_long(t)) {
                emit("\tld b,l\n\tpop hl\n\tpop de\n");
            } else {
                emit("\tld b,l\n\tpop hl\n");
            }
            emit_shift_loop(op, t);
            /* store: for 32-bit, need address from deeper in stack */
            if (type_is_long(t)) {
                /*
                 * Result is in DE:HL and the saved lvalue address is still
                 * on top of the stack.
                 */
                emit("\tld b,d\n\tld c,e\n");   /* BC = high word */
                emit("\tpop de\n");               /* DE = address */
                emit("\tex de,hl\n");             /* HL = address, DE = low word */
                emit("\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tinc hl\n\tld (hl),c\n\tinc hl\n\tld (hl),b\n");
                if (!want_dead) {
                    emit("\tex de,hl\n");         /* HL = low word */
                    emit("\tld d,b\n\tld e,c\n"); /* DE = high word */
                }
            } else {
                emit("\tex de,hl\n\tpop hl\n");
                emit_store_de_to_addr_hl(t);
                if (!want_dead)
                    emit("\tex de,hl\n");
            }
            g_expr_type = t;
            g_long_from16 = 0;
            return;
        }

        if (type_is_float(t) && (op == TOK_ADDEQ || op == TOK_SUBEQ || op == TOK_MULEQ || op == TOK_DIVEQ)) {
            if (!type_is_float(g_expr_type))
                emit_convert_int_to_float(g_expr_type);
            emit("\tpush de\n\tpush hl\n");
            emit_runtime_call(op == TOK_MULEQ ? "__fmul" : (op == TOK_DIVEQ ? "__fdiv" : (op == TOK_ADDEQ ? "__fadd" : "__fsub")));
            emit("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n");
            emit_store_de_to_addr_hl(t); /* pops saved lvalue address */
        } else if (type_is_long(t)) {
            if (!type_is_long(g_expr_type))
                emit_extend_to_long_typed(g_expr_type);
            if (op == TOK_ADDEQ) gen_binop32('+', t);
            else if (op == TOK_SUBEQ) gen_binop32('-', t);
            else if (op == TOK_MULEQ) gen_binop32('*', t);
            else if (op == TOK_DIVEQ) gen_binop32('/', t);
            else if (op == TOK_MODEQ) gen_binop32('%', t);
            else if (op == TOK_ANDEQ) gen_binop32('&', t);
            else if (op == TOK_OREQ)  gen_binop32('|', t);
            else if (op == TOK_XOREQ) gen_binop32('^', t);
            /* result in DE:HL (DE=high, HL=low), address at top of stack */
            emit_store_de_to_addr_hl(t); /* pops address and stores 4 bytes */
        } else {
            if ((t & (TYPE_PTR | TYPE_PTR2)) && (op == TOK_ADDEQ || op == TOK_SUBEQ)) {
                int elem = type_index_elem_size(t);
                scale_hl_by_elem_size(elem);
            }
            emit("\tex de,hl\n\tpop hl\n");
            common_type = common_arith_type(t, g_expr_type);
            if (op == TOK_ADDEQ) gen_binop_typed('+', common_type);
            else if (op == TOK_SUBEQ) gen_binop_typed('-', common_type);
            else if (op == TOK_MULEQ) gen_binop_typed('*', common_type);
            else if (op == TOK_DIVEQ) gen_binop_typed('/', common_type);
            else if (op == TOK_MODEQ) gen_binop_typed('%', common_type);
            else if (op == TOK_ANDEQ) gen_binop_typed('&', common_type);
            else if (op == TOK_OREQ) gen_binop_typed('|', common_type);
            else if (op == TOK_XOREQ) gen_binop_typed('^', common_type);
            emit("\tex de,hl\n\tpop hl\n");
            emit_store_de_to_addr_hl(t);
            if (!want_dead)
                emit("\tex de,hl\n");
        }
        g_long_from16 = 0;
        return;
    }

    gen_conditional();
}
void gen_expr_no_comma(void)
{
    /*
     * Assignment-expression, not full expression.  Declaration initializers
     * use assignment-expression grammar, so a comma at this level separates
     * declarators rather than becoming the comma operator:
     *
     *     int a = 0, b = 1;
     *
     * The comma operator is still available when parenthesized because
     * gen_primary() calls gen_expr() for the expression inside parentheses:
     *
     *     int a = (0, 1);
     */
    static int depth = 0;

    /* AST-only assignment-expression path for declaration initializers. */
    if (g_ast_build_enabled && !scan_mode && depth == 0) {
        long sv_pos = posi;
        long sv_tok_start = tok_start_pos;
        int sv_line = line_no;
        int sv_tok_line = tok_line;
        struct Token sv_tok = tok;
        int report = getenv("DCC_AST_REPORT") != NULL;
        struct AstNode *n = ast_build_assign_expr(&g_ast_arena);

        if (g_ast_gen_enabled && ast_gen_supported(n)) {
            depth++;
            ast_gen_expr(n);
            depth--;
            if (g_ast_gen_enabled == 2)
                fprintf(stderr, "; AST-emit %s\n", ast_kind_name(n->kind));
            ast_arena_reset(&g_ast_arena);
            return;
        }

        if (g_ast_build_enabled == 2) {
            fprintf(stderr, "; AST gen_expr_no_comma:\n");
            ast_dump(n, 1);
        }
        if (report) {
            if (n == NULL) {
                fprintf(stderr, "; AST-unsupported expr build token=%d text='%s' line=%d\n",
                        sv_tok.kind, sv_tok.text, sv_tok_line);
            } else {
                fprintf(stderr, "; AST-unsupported expr gate kind=%s line=%d\n",
                        ast_kind_name(n->kind), sv_tok_line);
            }
        }
        posi = sv_pos;
        tok_start_pos = sv_tok_start;
        line_no = sv_line;
        tok_line = sv_tok_line;
        tok = sv_tok;
        ast_arena_reset(&g_ast_arena);
        fatal("unsupported AST assignment expression");
    }

    depth++;
    gen_assign();
    depth--;
}

void gen_expr(void)
{
    static int depth = 0;

    /* AST-only top-level expression path.  Nested calls still use the shared
     * recursive expression helpers used by the AST emitter itself. */
    if (g_ast_build_enabled && !scan_mode && depth == 0) {
        long sv_pos = posi;
        long sv_tok_start = tok_start_pos;
        int sv_line = line_no;
        int sv_tok_line = tok_line;
        struct Token sv_tok = tok;
        int report = getenv("DCC_AST_REPORT") != NULL;
        struct AstNode *n = ast_build_expr(&g_ast_arena);

        if (g_ast_gen_enabled && ast_gen_supported(n)) {
            depth++;
            ast_gen_expr(n);
            depth--;
            if (g_ast_gen_enabled == 2)
                fprintf(stderr, "; AST-emit %s\n", ast_kind_name(n->kind));
            ast_arena_reset(&g_ast_arena);
            return;
        }

        if (g_ast_build_enabled == 2) {
            fprintf(stderr, "; AST gen_expr:\n");
            ast_dump(n, 1);
        }
        if (report) {
            if (n == NULL) {
                fprintf(stderr, "; AST-unsupported expr build token=%d text='%s' line=%d\n",
                        sv_tok.kind, sv_tok.text, sv_tok_line);
            } else {
                fprintf(stderr, "; AST-unsupported expr gate kind=%s line=%d\n",
                        ast_kind_name(n->kind), sv_tok_line);
            }
        }
        posi = sv_pos;
        tok_start_pos = sv_tok_start;
        line_no = sv_line;
        tok_line = sv_tok_line;
        tok = sv_tok;
        ast_arena_reset(&g_ast_arena);
        fatal("unsupported AST expression");
    }

    depth++;
    gen_assign();
    /* comma operator: evaluate and discard left, then evaluate right.
     * DCC has no struct-valued expression temporary model, so reject
     * struct operands here instead of emitting bogus scalar loads/copies. */
    while (tok.kind == ',') {
        if (type_is_struct_object(g_expr_type))
            error_here("unsupported struct comma expression");
        next_token();
        gen_assign();
        if (type_is_struct_object(g_expr_type))
            error_here("unsupported struct comma expression");
    }
    depth--;
}

