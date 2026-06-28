/*
 * dcc_stmt.c - statement code generation.
 *
 * The statement dispatcher and lowering for compound blocks, if/else, while,
 * for, do-while, switch (both if-chain and jump-table strategies), return,
 * break/continue and goto, plus several pointer-walking loop idioms.
 *
 * MODULE: compiled as its own translation unit; shared declarations are in dcc.h.
 * Source provenance: monolith src/ddc.c lines 13992-15879.
 */

#include "dcc.h"
#include "dcc_ast.h"

/* Switch context stack: allows case/default labels to be emitted by
 * gen_statement() when they appear inside nested loop bodies (Duff's device). */
#define MAX_SW_NEST 8
struct SwCtx { int *vals; int *labs; int n; int def_lab; };
static struct SwCtx g_sw_ctx[MAX_SW_NEST];
static int g_sw_depth;

void gen_compound(void)
{
    expect('{');
    enter_scope();

    while (tok.kind != TOK_EOF && tok.kind != '}') {
        if (tok.kind == TOK_TYPEDEF) {
            parse_typedef_decl();
        } else if (starts_type()) {
            int t;
            int is_static_local;
            decl_is_extern = 0;
            is_static_local = (tok.kind == TOK_STATIC);
            t = parse_base_type();
            if (tok.kind == ';') {
                next_token();
            } else if (is_static_local) {
                scan_static_local_decl_after_type(t);
            } else {
                gen_local_decl_after_type(t);
            }
        } else {
            gen_statement();
        }
    }

    leave_scope();
    expect('}');
}

void gen_if(void)
{
    int lelse, lend;

    lelse = new_label();
    lend = new_label();

    expect(TOK_IF);
    expect('(');
    if (!try_fast_global_char_array_condition(lelse) &&
        !gen_condition_branch_false(lelse)) {
        gen_expr();
        emit_test_expr_nonzero(g_expr_type, lelse, 0);
    }
    expect(')');

    gen_statement();

    emit_jp_label("jp", lend);
    emit_label(lelse);

    if (accept(TOK_ELSE)) gen_statement();

    emit_label(lend);
}


void emit_load_sym_word_to_bc(struct Sym *s)
{
    if (sym_can_ix_direct(s)) {
        if (current_omit_ix_frame && s->storage == SC_PARAM) {
            emit_load_frame_addr_hl(s);
            emit("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n");
        } else {
            fprintf(outf, "\tld c,(ix%+d)\n", s->offset);
            fprintf(outf, "\tld b,(ix%+d)\n", s->offset + 1);
        }
    } else if (is_global_word_sym(s)) {
        emit_extrn_if_needed(s);
        fprintf(outf, "\tld bc,(%s)\n", asm_name_for(sym_asm_name(s)));
    }
}

void emit_store_de_to_sym_word(struct Sym *s)
{
    if (sym_can_ix_direct(s)) {
        if (current_omit_ix_frame && s->storage == SC_PARAM) {
            emit_load_frame_addr_hl(s);
            emit("\tld (hl),e\n\tinc hl\n\tld (hl),d\n");
        } else {
            fprintf(outf, "\tld (ix%+d),e\n", s->offset);
            fprintf(outf, "\tld (ix%+d),d\n", s->offset + 1);
        }
    } else if (is_global_word_sym(s)) {
        emit_extrn_if_needed(s);
        fprintf(outf, "\tld (%s),de\n", asm_name_for(sym_asm_name(s)));
    }
}

void emit_store_bc_to_sym_word(struct Sym *s)
{
    if (sym_can_ix_direct(s)) {
        if (current_omit_ix_frame && s->storage == SC_PARAM) {
            emit_load_frame_addr_hl(s);
            emit("\tld (hl),c\n\tinc hl\n\tld (hl),b\n");
        } else {
            fprintf(outf, "\tld (ix%+d),c\n", s->offset);
            fprintf(outf, "\tld (ix%+d),b\n", s->offset + 1);
        }
    } else if (is_global_word_sym(s)) {
        emit_extrn_if_needed(s);
        fprintf(outf, "\tld (%s),bc\n", asm_name_for(sym_asm_name(s)));
    }
}

void emit_bc_add_const_small(int n)
{
    int i;
    for (i = 0; i < n; ++i)
        emit("\tinc bc\n");
}


int parse_while_deref_nonzero_id(char *name, int namesz, struct Sym **sp, int *elemp)
{
    if (!accept(TOK_WHILE))
        return 0;
    if (!accept('('))
        return 0;
    if (!accept('*'))
        return 0;
    if (tok.kind != TOK_ID)
        return 0;
    strncpy(name, tok.text, (size_t)namesz - 1);
    name[namesz - 1] = 0;
    sp[0] = find_sym(name);
    if (!sp[0] || !(sym_can_ix_direct(sp[0]) || is_global_word_sym(sp[0])) ||
        type_ptr_depth(sp[0]->type) <= 0 || type_size(sp[0]->type) != 2)
        return 0;
    elemp[0] = type_index_elem_size(sp[0]->type);
    if (elemp[0] != 1 && elemp[0] != 2)
        return 0;
    next_token();
    if (tok.kind == TOK_NE) {
        next_token();
        if (!(tok.kind == TOK_NUM && tok.val == 0))
            return 0;
        next_token();
    }
    if (!accept(')'))
        return 0;
    return 1;
}

void emit_load_bc_pointee_to_hl_or_a(int elem)
{
    if (elem == 1) {
        emit("\tld a,(bc)\n");
    } else {
        emit("\tld h,b\n\tld l,c\n");
        emit("\tld a,(hl)\n");
        emit("\tinc hl\n");
        emit("\tld h,(hl)\n");
        emit("\tld l,a\n");
    }
}

void emit_test_loaded_pointee_zero(int elem)
{
    if (elem == 1)
        emit("\tor a\n");
    else
        emit("\tld a,h\n\tor l\n");
}

void emit_compare_loaded_pointee_to_sym(int elem, struct Sym *csym)
{
    if (elem == 1) {
        if (sym_can_ix_direct(csym)) {
            if (current_omit_ix_frame && csym->storage == SC_PARAM) {
                emit("\tld e,a\n");
                emit_load_frame_addr_hl(csym);
                emit("\tld d,(hl)\n\tld a,e\n\tcp d\n");
            } else {
                fprintf(outf, "\tcp (ix%+d)\n", csym->offset);
            }
        } else {
            emit("\tpush hl\n");
            emit_load_sym_addr(csym);
            emit("\tcp (hl)\n");
            emit("\tpop hl\n");
        }
    } else {
        if (sym_can_ix_direct(csym)) {
            if (current_omit_ix_frame && csym->storage == SC_PARAM) {
                emit("\tex de,hl\n");
                emit_load_frame_addr_hl(csym);
                emit("\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n\tex de,hl\n");
            } else {
                fprintf(outf, "\tld e,(ix%+d)\n", csym->offset);
                fprintf(outf, "\tld d,(ix%+d)\n", csym->offset + 1);
            }
        } else {
            emit_load_sym_addr(csym);
            emit("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n");
        }
        emit("\tor a\n\tsbc hl,de\n");
    }
}

int try_gen_bc_pointer_copy_while(void)
{
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    struct Token save_tok;
    char srcname[64], dstname[64];
    struct Sym *srcsym, *dstsym;
    int selem, delem;
    int ltop, lend;
    int body_brace;

    if (tok.kind != TOK_WHILE)
        return 0;
    save_pos = posi; save_tok_start = tok_start_pos; save_line = line_no;
    save_tok_line = tok_line; save_tok = tok;

    if (!parse_while_deref_nonzero_id(srcname, sizeof(srcname), &srcsym, &selem))
        goto no;
    body_brace = 0;
    if (accept('{')) body_brace = 1;

    if (!accept('*')) goto no;
    if (tok.kind != TOK_ID) goto no;
    strncpy(dstname, tok.text, sizeof(dstname) - 1); dstname[sizeof(dstname)-1] = 0;
    dstsym = find_sym(dstname);
    if (!dstsym || !(sym_can_ix_direct(dstsym) || is_global_word_sym(dstsym)) ||
        type_ptr_depth(dstsym->type) <= 0 || type_size(dstsym->type) != 2)
        goto no;
    delem = type_index_elem_size(dstsym->type);
    if (delem != selem) goto no;
    next_token();
    if (!accept(TOK_INC)) goto no;
    if (!accept('=')) goto no;
    if (!accept('*')) goto no;
    if (tok.kind != TOK_ID || strcmp(tok.text, srcname) != 0) goto no;
    next_token();
    if (!accept(TOK_INC)) goto no;
    expect(';');
    if (body_brace) expect('}');

    ltop = new_label(); lend = new_label();
    emit_load_sym_word_to_bc(srcsym);       /* BC = source */
    if (sym_can_ix_direct(dstsym)) {
        fprintf(outf, "\tld e,(ix%+d)\n", dstsym->offset);
        fprintf(outf, "\tld d,(ix%+d)\n", dstsym->offset + 1);
    } else {
        emit_extrn_if_needed(dstsym);
        fprintf(outf, "\tld de,(%s)\n", asm_name_for(sym_asm_name(dstsym)));
    }
    emit_label(ltop);
    if (selem == 1) {
        emit("\tld a,(bc)\n\tor a\n");
        emit_jp_label("jp z,", lend);
        emit("\tld (de),a\n");
        emit("\tinc bc\n\tinc de\n");
    } else {
        emit("\tld h,b\n\tld l,c\n");
        emit("\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n");
        emit("\tld a,h\n\tor l\n");
        emit_jp_label("jp z,", lend);
        emit("\tld a,l\n\tld (de),a\n\tinc de\n");
        emit("\tld a,h\n\tld (de),a\n\tinc de\n");
        emit("\tinc bc\n\tinc bc\n");
    }
    emit_jp_label("jp", ltop);
    emit_label(lend);
    emit_store_bc_to_sym_word(srcsym);
    emit_store_de_to_sym_word(dstsym);
    return 1;
no:
    posi = save_pos; tok_start_pos = save_tok_start; line_no = save_line;
    tok_line = save_tok_line; tok = save_tok; return 0;
}

int try_gen_bc_pointer_find_while(void)
{
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    struct Token save_tok;
    char pname[64], cname[64];
    struct Sym *pvar, *csym;
    int elem;
    int ltop, lend, lnomatch;
    int body_brace;

    if (tok.kind != TOK_WHILE) return 0;
    save_pos = posi; save_tok_start = tok_start_pos; save_line = line_no;
    save_tok_line = tok_line; save_tok = tok;
    if (!parse_while_deref_nonzero_id(pname, sizeof(pname), &pvar, &elem)) goto no;
    body_brace = 0; if (accept('{')) body_brace = 1;

    if (!accept(TOK_IF)) goto no;
    if (!accept('(')) goto no;
    if (!accept('*')) goto no;
    if (tok.kind != TOK_ID || strcmp(tok.text, pname) != 0) goto no;
    next_token();
    if (!accept(TOK_EQ)) goto no;
    if (tok.kind != TOK_ID) goto no;
    strncpy(cname, tok.text, sizeof(cname) - 1); cname[sizeof(cname)-1] = 0;
    csym = find_sym(cname);
    if (!csym || !(sym_can_ix_direct(csym) || is_global_word_sym(csym))) goto no;
    next_token();
    if (!accept(')')) goto no;
    if (!accept(TOK_RETURN)) goto no;
    if (accept('(')) {
        while (tok.kind != ')' && tok.kind != TOK_EOF) next_token();
        expect(')');
    }
    if (tok.kind != TOK_ID || strcmp(tok.text, pname) != 0) goto no;
    next_token();
    expect(';');
    if (tok.kind != TOK_ID || strcmp(tok.text, pname) != 0) goto no;
    next_token();
    if (!accept(TOK_INC)) goto no;
    expect(';');
    if (body_brace) expect('}');

    ltop = new_label(); lend = new_label(); lnomatch = new_label();
    emit_load_sym_word_to_bc(pvar);
    emit_label(ltop);
    emit_load_bc_pointee_to_hl_or_a(elem);
    emit_test_loaded_pointee_zero(elem);
    emit_jp_label("jp z,", lend);
    if (elem == 1)
        emit_compare_loaded_pointee_to_sym(elem, csym);
    else {
        emit_load_bc_pointee_to_hl_or_a(elem);
        emit_compare_loaded_pointee_to_sym(elem, csym);
    }
    emit_jp_label("jp nz,", lnomatch);
    emit("\tld h,b\n\tld l,c\n");
    emit_jp_label("jp", current_return_label);
    emit_label(lnomatch);
    emit_bc_add_const_small(elem);
    emit_jp_label("jp", ltop);
    emit_label(lend);
    emit("\tld hl,0\n");
    emit_jp_label("jp", current_return_label);
    return 1;
no:
    posi = save_pos; tok_start_pos = save_tok_start; line_no = save_line;
    tok_line = save_tok_line; tok = save_tok; return 0;
}


int try_gen_bc_pointer_rfind_while(void)
{
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    struct Token save_tok;
    char pname[64], cname[64], lname[64];
    struct Sym *pvar, *csym, *lastsym;
    int elem;
    int ltop, lend, lnomatch;
    int body_brace;

    if (tok.kind != TOK_WHILE) return 0;
    save_pos = posi; save_tok_start = tok_start_pos; save_line = line_no;
    save_tok_line = tok_line; save_tok = tok;
    if (!parse_while_deref_nonzero_id(pname, sizeof(pname), &pvar, &elem)) goto no;
    body_brace = 0; if (accept('{')) body_brace = 1;

    if (!accept(TOK_IF)) goto no;
    if (!accept('(')) goto no;
    if (!accept('*')) goto no;
    if (tok.kind != TOK_ID || strcmp(tok.text, pname) != 0) goto no;
    next_token();
    if (!accept(TOK_EQ)) goto no;
    if (tok.kind != TOK_ID) goto no;
    strncpy(cname, tok.text, sizeof(cname) - 1); cname[sizeof(cname)-1] = 0;
    csym = find_sym(cname);
    if (!csym || !(sym_can_ix_direct(csym) || is_global_word_sym(csym))) goto no;
    next_token();
    if (!accept(')')) goto no;
    if (tok.kind != TOK_ID) goto no;
    strncpy(lname, tok.text, sizeof(lname) - 1); lname[sizeof(lname)-1] = 0;
    lastsym = find_sym(lname);
    if (!lastsym || !(sym_can_ix_direct(lastsym) || is_global_word_sym(lastsym)) ||
        type_ptr_depth(lastsym->type) <= 0 || type_size(lastsym->type) != 2)
        goto no;
    next_token();
    if (!accept('=')) goto no;
    if (tok.kind != TOK_ID || strcmp(tok.text, pname) != 0) goto no;
    next_token();
    expect(';');
    if (tok.kind != TOK_ID || strcmp(tok.text, pname) != 0) goto no;
    next_token();
    if (!accept(TOK_INC)) goto no;
    expect(';');
    if (body_brace) expect('}');

    ltop = new_label(); lend = new_label(); lnomatch = new_label();
    emit_load_sym_word_to_bc(pvar);
    emit_label(ltop);
    emit_load_bc_pointee_to_hl_or_a(elem);
    emit_test_loaded_pointee_zero(elem);
    emit_jp_label("jp z,", lend);
    if (elem == 1)
        emit_compare_loaded_pointee_to_sym(elem, csym);
    else {
        emit_load_bc_pointee_to_hl_or_a(elem);
        emit_compare_loaded_pointee_to_sym(elem, csym);
    }
    emit_jp_label("jp nz,", lnomatch);
    emit_store_bc_to_sym_word(lastsym);
    emit_label(lnomatch);
    emit_bc_add_const_small(elem);
    emit_jp_label("jp", ltop);
    emit_label(lend);
    emit_store_bc_to_sym_word(pvar);
    return 1;
no:
    posi = save_pos; tok_start_pos = save_tok_start; line_no = save_line;
    tok_line = save_tok_line; tok = save_tok; return 0;
}

int try_gen_bc_pointer_scan_while(void)
{
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    struct Token save_tok;
    char pname[64];
    struct Sym *pvar;
    int elem;
    int ltop;
    int lend;
    int body_brace;

    if (tok.kind != TOK_WHILE)
        return 0;

    save_pos = posi;
    save_tok_start = tok_start_pos;
    save_line = line_no;
    save_tok_line = tok_line;
    save_tok = tok;

    next_token();
    if (!accept('('))
        goto no;
    if (!accept('*'))
        goto no;
    if (tok.kind != TOK_ID)
        goto no;
    strncpy(pname, tok.text, sizeof(pname) - 1);
    pname[sizeof(pname) - 1] = 0;
    pvar = find_sym(pname);
    if (!pvar || !(sym_can_ix_direct(pvar) || is_global_word_sym(pvar)) ||
        type_ptr_depth(pvar->type) <= 0 || type_size(pvar->type) != 2)
        goto no;
    elem = type_index_elem_size(pvar->type);
    if (elem <= 0 || elem > 16)
        goto no;
    next_token();

    if (tok.kind == TOK_NE) {
        next_token();
        if (!(tok.kind == TOK_NUM && tok.val == 0))
            goto no;
        next_token();
    }
    if (!accept(')'))
        goto no;

    body_brace = 0;
    if (accept('{'))
        body_brace = 1;

    if (tok.kind != TOK_ID || strcmp(tok.text, pname) != 0)
        goto no;
    next_token();
    if (!accept(TOK_INC))
        goto no;
    expect(';');
    if (body_brace)
        expect('}');

    ltop = new_label();
    lend = new_label();
    emit_load_sym_word_to_bc(pvar);
    emit_label(ltop);
    if (elem == 1) {
        emit("\tld a,(bc)\n");
        emit("\tor a\n");
    } else {
        emit("\tld h,b\n\tld l,c\n");
        emit("\tld a,(hl)\n");
        emit("\tinc hl\n");
        emit("\tor (hl)\n");
    }
    emit_jp_label("jp z,", lend);
    emit_bc_add_const_small(elem);
    emit_jp_label("jp", ltop);
    emit_label(lend);
    emit_store_bc_to_sym_word(pvar);
    return 1;

no:
    posi = save_pos;
    tok_start_pos = save_tok_start;
    line_no = save_line;
    tok_line = save_tok_line;
    tok = save_tok;
    return 0;
}

void gen_while(void)
{
    int ltop, lend;

    if (try_gen_bc_pointer_copy_while())
        return;
    if (try_gen_bc_pointer_find_while())
        return;
    if (try_gen_bc_pointer_rfind_while())
        return;
    if (try_gen_bc_pointer_scan_while())
        return;

    ltop = new_label();
    lend = new_label();

    expect(TOK_WHILE);
    emit_label(ltop);

    expect('(');
    if (!gen_condition_branch_false(lend)) {
        gen_expr();
        emit_test_expr_nonzero(g_expr_type, lend, 0);
    }
    expect(')');

    break_stack[nflow] = lend;
    cont_stack[nflow] = ltop;
    nflow++;

    gen_statement();

    nflow--;
    emit_jp_label("jp", ltop);
    emit_label(lend);
}

char *copy_range(long a, long b)
{
    long n;
    char *p;
    char fbuf[256];
    char lbuf[384];
    int lno;
    int lnl;

    n = b - a;
    if (n < 0) n = 0;

    source_location_at(a, fbuf, sizeof(fbuf), &lno);
    sprintf(lbuf, "#line %d \"%s\"\n", lno, fbuf);
    lnl = (int)strlen(lbuf);

    p = (char *)xmalloc((size_t)(lnl + n + 2));
    memcpy(p, lbuf, (size_t)lnl);
    memcpy(p + lnl, src + a, (size_t)n);
    p[lnl + n] = ';';
    p[lnl + n + 1] = 0;
    return p;
}

void gen_snippet_expr(const char *snippet)
{
    char *old_src;
    long old_len;
    long old_pos;
    long old_tok_start;
    int old_line;
    int old_tok_line;
    struct Token old_tok;

    old_src = src;
    old_len = src_len;
    old_pos = posi;
    old_tok_start = tok_start_pos;
    old_line = line_no;
    old_tok_line = tok_line;
    old_tok = tok;

    src = (char *)snippet;
    src_len = (long)strlen(snippet);
    posi = 0;
    line_no = 1;
    next_token();

    if (tok.kind != ';') {
        if (expr_result_dead) {
            if (!try_gen_incdec_statement()) {
                if (!try_fast_local_self_add_statement())
                    gen_expr();
            }
        } else {
            gen_expr();
        }
    }

    src = old_src;
    src_len = old_len;
    posi = old_pos;
    tok_start_pos = old_tok_start;
    line_no = old_line;
    tok_line = old_tok_line;
    tok = old_tok;
}

void gen_snippet_lvalue_addr(const char *snippet, int *ptype)
{
    char *old_src;
    long old_len;
    long old_pos;
    long old_tok_start;
    int old_line;
    int old_tok_line;
    struct Token old_tok;

    old_src = src;
    old_len = src_len;
    old_pos = posi;
    old_tok_start = tok_start_pos;
    old_line = line_no;
    old_tok_line = tok_line;
    old_tok = tok;

    src = (char *)snippet;
    src_len = (long)strlen(snippet);
    posi = 0;
    line_no = 1;
    next_token();

    gen_lvalue_addr(ptype);

    src = old_src;
    src_len = old_len;
    posi = old_pos;
    tok_start_pos = old_tok_start;
    line_no = old_line;
    tok_line = old_tok_line;
    tok = old_tok;
}

void skip_expr_until_rparen(long *startp, long *endp)
{
    int depth;
    int done;

    depth = 0;
    done = 0;
    startp[0] = tok_start_pos;

    while (tok.kind != TOK_EOF && !done) {
        if (depth == 0 && tok.kind == ')') {
            done = 1;
        } else {
            if (tok.kind == '(') {
                depth = depth + 1;
            } else {
                if (tok.kind == '[') {
                    depth = depth + 1;
                } else {
                    if (tok.kind == ')') {
                        depth = depth - 1;
                    } else {
                        if (tok.kind == ']') {
                            depth = depth - 1;
                        }
                    }
                }
            }

            if (!done) {
                next_token();
            }
        }
    }

    endp[0] = tok_start_pos;
}

/*
 * Skip past the for-loop condition (up to but not including the ';').
 * Records the source positions so copy_range can capture the text.
 * Like skip_expr_until_rparen but stops at ';' instead of ')'.
 */
void skip_for_cond(long *startp, long *endp)
{
    startp[0] = tok_start_pos;
    while (tok.kind != TOK_EOF && tok.kind != ';')
        next_token();
    endp[0] = tok_start_pos;
}

/*
 * Replay a captured condition snippet and emit a branch to ltrue when
 * the condition is true.  Used to place the for-loop condition check at
 * the bottom of the loop (do-while style).
 */
void gen_snippet_cond_true(const char *snippet, int ltrue)
{
    char *old_src;
    long old_len;
    long old_pos;
    long old_tok_start;
    int old_line;
    int old_tok_line;
    struct Token old_tok;

    old_src       = src;
    old_len       = src_len;
    old_pos       = posi;
    old_tok_start = tok_start_pos;
    old_line      = line_no;
    old_tok_line  = tok_line;
    old_tok       = tok;

    src     = (char *)snippet;
    src_len = (long)strlen(snippet);
    posi    = 0;
    line_no = 1;
    next_token();

    if (tok.kind != ';') {
        if (!gen_condition_branch_true_until(ltrue, ';')) {
            gen_expr();
            emit_test_expr_nonzero(g_expr_type, ltrue, 1);
        }
    }

    src           = old_src;
    src_len       = old_len;
    posi          = old_pos;
    tok_start_pos = old_tok_start;
    line_no       = old_line;
    tok_line      = old_tok_line;
    tok           = old_tok;
}

/*
 * Lookahead: return 1 if the for-loop init/condition has the form
 *   ident = const ; ident relop bound ;
 * where const satisfies the condition (so the loop always executes at
 * least once).  Recognises <, <=, != as the relop.
 * Does not consume any tokens.
 */
int for_init_always_enters_loop(void)
{
    long save_pos;
    long save_tok_start;
    int  save_line;
    int  save_tok_line;
    struct Token save_tok;
    char varname[64];
    long initval;
    long bound;
    int  op;
    int  result;

    if (tok.kind != TOK_ID)
        return 0;

    save_pos       = posi;
    save_tok_start = tok_start_pos;
    save_line      = line_no;
    save_tok_line  = tok_line;
    save_tok       = tok;

    result = 0;

    /* ident */
    strncpy(varname, tok.text, sizeof(varname) - 1);
    varname[sizeof(varname) - 1] = 0;
    next_token();

    /* = (plain assignment, not ==) */
    if (tok.kind != '=') goto restore;
    next_token();

    /* non-negative integer constant */
    if (tok.kind != TOK_NUM) goto restore;
    if (tok.val < 0) goto restore;
    initval = tok.val;
    next_token();

    /* ; separating init from condition */
    if (tok.kind != ';') goto restore;
    next_token();

    /* same identifier */
    if (tok.kind != TOK_ID) goto restore;
    if (strcmp(tok.text, varname) != 0) goto restore;
    next_token();

    /* relational operator — exclude <= because its signed-bias sbc pattern is
     * matched by pass_reuse_sbc_result_for_flagcheck / pass_recover_index_from_sbc
     * which both require the while-style jp LE exit jump that do-while omits. */
    op = tok.kind;
    if (op != '<' && op != TOK_NE) goto restore;
    next_token();

    /* integer constant bound */
    if (tok.kind != TOK_NUM) goto restore;
    bound = tok.val;

    /* confirm init value satisfies condition */
    if (op == '<'    && !(initval <  bound)) goto restore;
    if (op == TOK_LE && !(initval <= bound)) goto restore;
    if (op == TOK_NE && !(initval != bound)) goto restore;

    result = 1;

restore:
    posi          = save_pos;
    tok_start_pos = save_tok_start;
    line_no       = save_line;
    tok_line      = save_tok_line;
    tok           = save_tok;
    return result;
}


/*
 * Very conservative counted byte-array cycle fill.
 *
 * Recognises the common C idiom:
 *     for (i = 0; i < N; i++)
 *         byte_array[i] = BASE + (i % MOD);
 *
 * and emits a register loop using BC as the induction variable, DE as the
 * destination pointer, and A as the cycling byte.  There are no calls in the
 * emitted loop, so BC cannot be accidentally clobbered by helper/RTL calls.
 * At loop exit the source induction variable is written back with N, preserving
 * the visible C value after the loop.
 */
int try_gen_bc_byte_array_cycle_for(void)
{
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    struct Token save_tok;
    char iname[64];
    char aname[64];
    struct Sym *isym;
    struct Sym *asym;
    long bound;
    long base;
    long mod;
    int had_outer;
    int had_inner;
    int ltop;
    int lnowrap;

    if (tok.kind != TOK_FOR)
        return 0;

    save_pos = posi;
    save_tok_start = tok_start_pos;
    save_line = line_no;
    save_tok_line = tok_line;
    save_tok = tok;

    next_token();
    if (!accept('(')) goto no;

    if (tok.kind != TOK_ID) goto no;
    strncpy(iname, tok.text, sizeof(iname) - 1);
    iname[sizeof(iname) - 1] = 0;
    isym = find_sym(iname);
    if (!sym_can_ix_direct(isym) || type_size(isym->type) != 2 || type_ptr_depth(isym->type) != 0)
        goto no;
    next_token();
    if (!accept('=')) goto no;
    if (!(tok.kind == TOK_NUM && tok.val == 0)) goto no;
    next_token();
    if (!accept(';')) goto no;

    if (tok.kind != TOK_ID || strcmp(tok.text, iname) != 0) goto no;
    next_token();
    if (!accept('<')) goto no;

    bound = -1;
    if (tok.kind == TOK_NUM) {
        bound = tok.val;
        next_token();
    } else if (tok.kind == TOK_SIZEOF) {
        next_token();
        if (!accept('(')) goto no;
        if (tok.kind != TOK_ID) goto no;
        asym = find_sym(tok.text);
        if (!asym || !asym->is_array || type_size(asym->type) != 1) goto no;
        bound = asym->size;
        next_token();
        if (!accept(')')) goto no;
    } else {
        goto no;
    }
    if (bound <= 0 || bound > 65535) goto no;
    if (!accept(';')) goto no;

    if (tok.kind != TOK_ID || strcmp(tok.text, iname) != 0) goto no;
    next_token();
    if (!accept(TOK_INC)) goto no;
    if (!accept(')')) goto no;

    if (tok.kind != TOK_ID) goto no;
    strncpy(aname, tok.text, sizeof(aname) - 1);
    aname[sizeof(aname) - 1] = 0;
    asym = find_sym(aname);
    if (!asym || asym->storage != SC_GLOBAL || !asym->is_array || type_size(asym->type) != 1)
        goto no;
    if (asym->size < bound) goto no;
    next_token();
    if (!accept('[')) goto no;
    if (tok.kind != TOK_ID || strcmp(tok.text, iname) != 0) goto no;
    next_token();
    if (!accept(']')) goto no;
    if (!accept('=')) goto no;

    had_outer = 0;
    if (accept('(')) had_outer = 1;

    if (!(tok.kind == TOK_NUM || tok.kind == TOK_CHARLIT)) goto no;
    base = tok.val;
    next_token();
    if (!accept('+')) goto no;

    had_inner = 0;
    if (accept('(')) had_inner = 1;
    if (tok.kind != TOK_ID || strcmp(tok.text, iname) != 0) goto no;
    next_token();
    if (!accept('%')) goto no;
    if (!(tok.kind == TOK_NUM || tok.kind == TOK_CHARLIT)) goto no;
    mod = tok.val;
    next_token();
    if (had_inner && !accept(')')) goto no;
    if (had_outer && !accept(')')) goto no;
    if (!accept(';')) goto no;

    if (base < 0 || base > 255 || mod <= 0 || mod > 255 || base + mod > 256)
        goto no;

    ltop = new_label();
    lnowrap = new_label();

    emit("\tld bc,0\n");
    fprintf(outf, "\tld de,%s\n", asm_name_for(sym_asm_name(asym)));
    fprintf(outf, "\tld h,%ld\n", base & 255L);
    emit_label(ltop);
    emit("\tld a,h\n");
    emit("\tld (de),a\n");
    emit("\tinc de\n");
    emit("\tinc h\n");
    emit("\tld a,h\n");
    fprintf(outf, "\tcp %ld\n", (base + mod) & 255L);
    emit_jp_label("jp nz,", lnowrap);
    fprintf(outf, "\tld h,%ld\n", base & 255L);
    emit_label(lnowrap);
    emit("\tinc bc\n");
    fprintf(outf, "\tld a,b\n\tcp %ld\n", (bound >> 8) & 255L);
    emit_jp_label("jp nz,", ltop);
    fprintf(outf, "\tld a,c\n\tcp %ld\n", bound & 255L);
    emit_jp_label("jp nz,", ltop);
    fprintf(outf, "\tld (ix%+d),c\n", isym->offset);
    fprintf(outf, "\tld (ix%+d),b\n", isym->offset + 1);
    return 1;

no:
    posi = save_pos;
    tok_start_pos = save_tok_start;
    line_no = save_line;
    tok_line = save_tok_line;
    tok = save_tok;
    return 0;
}

void gen_for(void)
{
    int ltop, linc, lend;
    long inc_start, inc_end;
    long cond_start, cond_end;
    char *inc_code;
    char *cond_code;
    int do_while;
    int for_seq;
    int rename_count;
    int old_for_decl_seq;
    int old_for_decl_rename_index;
    int old_for_decl_recording;

    /* Advance the per-function for-loop counter in pre-order so it matches the
     * frame-sizing scan; g_for_rename_count[seq] tells us whether this loop's
     * for-init declaration introduced C99 loop-scoped names. */
    for_seq = g_for_seq++;
    if (for_seq >= MAX_FOR_SCOPES)
        fatal("too many for statements");
    rename_count = g_for_rename_count[for_seq];

    if (rename_count == 0 && try_gen_bc_byte_array_cycle_for())
        return;

    ltop = new_label();
    linc = new_label();
    lend = new_label();

    expect(TOK_FOR);
    expect('(');

    /* Peek: if init assigns a constant that already satisfies the condition,
     * generate a do-while (check at bottom) to skip the always-true first test. */
    do_while = 0;
    if (!starts_type() && tok.kind != ';')
        do_while = for_init_always_enters_loop();

    if (starts_type()) {
        /* C99-style for-init declaration: for (int i = 0; ...) */
        int t;
        decl_is_extern = 0;
        t = parse_base_type();

        old_for_decl_seq = g_for_decl_seq;
        old_for_decl_rename_index = g_for_decl_rename_index;
        old_for_decl_recording = g_for_decl_recording;
        g_for_decl_seq = for_seq;
        g_for_decl_rename_index = 0;
        g_for_decl_recording = 0;

        if (tok.kind != ';')
            gen_local_decl_after_type(t); /* consumes the ';' */
        else
            next_token();

        if (g_for_decl_rename_index != rename_count)
            fatal("for-init scope mismatch");
        g_for_decl_seq = old_for_decl_seq;
        g_for_decl_rename_index = old_for_decl_rename_index;
        g_for_decl_recording = old_for_decl_recording;
        /* no expect(';') here — already consumed above */
    } else {
        if (tok.kind != ';') gen_expr();
        expect(';');
    }

    if (!do_while) {
        emit_label(ltop);

        if (tok.kind != ';') {
            if (!gen_condition_branch_false_until(lend, ';')) {
                gen_expr();
                emit_test_expr_nonzero(g_expr_type, lend, 0);
            }
        }
        expect(';');
    } else {
        /* Capture condition as text for replay at the bottom; skip it here. */
        cond_start = tok_start_pos;
        skip_for_cond(&cond_start, &cond_end);
        cond_code = copy_range(cond_start, cond_end);
        expect(';');

        emit_label(ltop);
    }

    inc_start = tok_start_pos;
    skip_expr_until_rparen(&inc_start, &inc_end);
    inc_code = copy_range(inc_start, inc_end);

    expect(')');

    break_stack[nflow] = lend;
    cont_stack[nflow] = linc;
    nflow++;

    gen_statement();

    nflow--;

    emit_label(linc);
    {
        int old_dead;

        old_dead = expr_result_dead;
        expr_result_dead = 1;
        gen_snippet_expr(inc_code);
        expr_result_dead = old_dead;
    }
    free(inc_code);

    if (!do_while) {
        emit_jp_label("jp", ltop);
    } else {
        gen_snippet_cond_true(cond_code, ltop);
        free(cond_code);
    }
    emit_label(lend);

    /* Close the for-init scope: source names now resolve to any outer symbols. */
    while (rename_count > 0) {
        pop_for_rename();
        rename_count--;
    }
}

void gen_return(void)
{
    expect(TOK_RETURN);

    if (tok.kind != ';') {
        if (type_is_struct_object(current_return_type)) {
            int rt;
            gen_lvalue_addr(&rt);
            if (!same_struct_type(current_return_type, rt))
                error_here("incompatible struct return");
            emit("\tex de,hl\n");          /* DE = source */
            emit("\tld l,(ix+4)\n");       /* hidden return pointer */
            emit("\tld h,(ix+5)\n");
            emit_copy_de_to_hl_bytes(type_size(current_return_type));
            g_expr_type = current_return_type;
            expect(';');
            emit_jp_label("jp", current_return_label);
            return;
        }

        /*
         * Byte return fast path.  This keeps tiny helpers from materializing a
         * full generic expression when the return value is just a byte local,
         * parameter, or small constant.  It is conservative: anything more
         * complex falls through to gen_expr(), preserving C integer promotions.
         */
        if (type_size(current_return_type) == 1) {
            if (tok.kind == TOK_ID) {
                struct Sym *rs;
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

                rs = find_sym(tok.text);
                if (sym_can_ix_direct(rs) && type_size(rs->type) == 1) {
                    next_token();
                    if (tok.kind == ';') {
                        fprintf(outf, "\tld l,(ix%+d)\n", rs->offset);
                        if (current_return_type & TYPE_UNSIGNED)
                            emit("\tld h,0\n");
                        else
                            emit("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n");
                        g_expr_type = current_return_type;
                        expect(';');
                        emit_jp_label("jp", current_return_label);
                        return;
                    }
                }

                posi = save_pos;
                tok_start_pos = save_tok_start;
                line_no = save_line;
                tok_line = save_tok_line;
                tok = save_tok;
            } else if ((tok.kind == TOK_NUM || tok.kind == TOK_CHARLIT) &&
                       tok.val >= 0 && tok.val <= 255) {
                long rv;
                rv = tok.val;
                next_token();
                if (tok.kind == ';') {
                    fprintf(outf, "\tld hl,%ld\n", rv & 255);
                    g_expr_type = current_return_type;
                    expect(';');
                    emit_jp_label("jp", current_return_label);
                    return;
                }
            }
        }

        /*
         * Float returns used to use try_emit_float_rvalue_dehl(), which only
         * consumes a simple float rvalue.  The normal expression generator now
         * supports full float expressions and leaves float results in DE:HL.
         */
        gen_expr();
        if (type_is_float(current_return_type) && !type_is_float(g_expr_type)) {
            emit_convert_int_to_float(g_expr_type);
        } else if (!type_is_float(current_return_type) && type_is_float(g_expr_type)) {
            emit_convert_float_to_intlike(current_return_type);
        } else if (type_size(current_return_type) == 1 && !type_is_long(g_expr_type)) {
            /*
             * Normalize byte-sized function returns.  The ABI returns all
             * integer scalars in HL, and callers use the function's declared
             * return type to decide whether later widening is signed or
             * unsigned.  Therefore a uint8_t return must leave HL as 00xx,
             * not FFxx inherited from the promoted expression used by
             * `return -1;`.  Likewise an int8_t return should leave HL sign
             * extended from L so direct assignment to wider signed types works.
             */
            if (current_return_type & TYPE_UNSIGNED)
                emit("\tld h,0\n");
            else
                emit("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n");
            g_expr_type = current_return_type;
        } else if (type_is_long(current_return_type) && !type_is_long(g_expr_type)) {
            /*
             * Long returns use DE:HL.  A plain int expression such as
             *
             *     uint32_t f(void) { return 0; }
             *
             * leaves only HL defined.  Without promotion DE contains whatever
             * happened to be live before the return.
             */
            emit_promote_int_to_long(g_expr_type, current_return_type);
            g_expr_type = current_return_type;
        }
    }

    expect(';');
    emit_jp_label("jp", current_return_label);
}

int scan_switch_cases_for_chain(int *case_vals, int *case_labs,
                                        int *case_countp, int *default_labp);

void gen_switch_chain(void)
{
    long save_posi;
    long save_tok_start_pos;
    int save_line_no;
    int save_tok_line;
    struct Token save_tok;
    int case_vals[MAX_SWITCH_CASES];
    int case_labs[MAX_SWITCH_CASES];
    int ncase;
    int default_lab;
    int lend;
    int ci;

    /*
     * General switch path.  Emit one dispatch sequence before the switch
     * body, then emit case/default labels in source order while parsing the
     * body normally.  This preserves C fall-through semantics and, unlike the
     * old interleaved compare/body generator, handles default: appearing
     * before later case labels.
     */
    ncase = 0;
    default_lab = -1;

    save_posi = posi;
    save_tok_start_pos = tok_start_pos;
    save_line_no = line_no;
    save_tok_line = tok_line;
    save_tok = tok;

    next_token();          /* switch */
    if (tok.kind == '(') {
        int depth;
        next_token();
        depth = 1;
        while (tok.kind != TOK_EOF && depth > 0) {
            if (tok.kind == '(') depth++;
            else if (tok.kind == ')') depth--;
            next_token();
        }
    }

    if (tok.kind != '{' ||
        !scan_switch_cases_for_chain(case_vals, case_labs, &ncase, &default_lab)) {
        posi = save_posi;
        tok_start_pos = save_tok_start_pos;
        line_no = save_line_no;
        tok_line = save_tok_line;
        tok = save_tok;
        fatal("switch has too many cases or unsupported syntax; increase MAX_SWITCH_CASES");
    } else {
        posi = save_posi;
        tok_start_pos = save_tok_start_pos;
        line_no = save_line_no;
        tok_line = save_tok_line;
        tok = save_tok;
    }

    expect(TOK_SWITCH);
    expect('(');
    gen_expr();
    expect(')');

    lend = new_label();

    /* Keep the switch value in DE while comparing each case value. */
    emit("\tex de,hl\n");
    for (ci = 0; ci < ncase; ++ci) {
        fprintf(outf, "\tld hl,%ld\n", (long)(case_vals[ci] & 0xffff));
        emit("\tor a\n\tsbc hl,de\n");
        emit_jp_label("jp z,", case_labs[ci]);
    }
    emit_jp_label("jp", default_lab >= 0 ? default_lab : lend);

    expect('{');
    enter_scope();

    break_stack[nflow] = lend;
    /* continue inside a switch should target the enclosing loop, not the switch */
    cont_stack[nflow] = (nflow > 0) ? cont_stack[nflow - 1] : lend;
    nflow++;

    if (g_sw_depth < MAX_SW_NEST) {
        g_sw_ctx[g_sw_depth].vals    = case_vals;
        g_sw_ctx[g_sw_depth].labs    = case_labs;
        g_sw_ctx[g_sw_depth].n       = ncase;
        g_sw_ctx[g_sw_depth].def_lab = default_lab;
        g_sw_depth++;
    }

    ci = 0;
    while (tok.kind != TOK_EOF && tok.kind != '}') {
        if (tok.kind == TOK_CASE) {
            long cv;
            int i;
            int lab;

            next_token();
            cv = parse_const_long_expr();
            expect(':');

            lab = -1;
            for (i = 0; i < ncase; ++i) {
                if ((case_vals[i] & 0xffff) == ((int)cv & 0xffff)) {
                    lab = case_labs[i];
                    break;
                }
            }
            if (lab < 0 && ci < ncase)
                lab = case_labs[ci++];
            if (lab >= 0)
                emit_label(lab);

            while (tok.kind != TOK_EOF &&
                   tok.kind != TOK_CASE &&
                   tok.kind != TOK_DEFAULT &&
                   tok.kind != '}') {
                gen_statement();
            }
        } else if (tok.kind == TOK_DEFAULT) {
            next_token();
            expect(':');

            if (default_lab >= 0)
                emit_label(default_lab);

            while (tok.kind != TOK_EOF &&
                   tok.kind != TOK_CASE &&
                   tok.kind != TOK_DEFAULT &&
                   tok.kind != '}') {
                gen_statement();
            }
        } else {
            gen_statement();
        }
    }

    if (g_sw_depth > 0) g_sw_depth--;

    leave_scope();
    expect('}');

    nflow--;
    emit_label(lend);
}

int scan_switch_cases_for_table(int *case_vals, int *case_labs,
                                       int *case_countp, int *default_labp,
                                       int *minp, int *maxp)
{
    long save_posi;
    long save_tok_start_pos;
    int save_line_no;
    int save_tok_line;
    struct Token save_tok;
    int depth;
    int ncase;
    int have_default;
    int deflab;
    int minv;
    int maxv;
    int ok;
    int i;
    int sw_pending;
    int sw_body_top;
    int sw_body_stack[MAX_SWITCH_CASES];

    if (tok.kind != '{')
        return 0;

    save_posi = posi;
    save_tok_start_pos = tok_start_pos;
    save_line_no = line_no;
    save_tok_line = tok_line;
    save_tok = tok;

    ncase = 0;
    have_default = 0;
    deflab = -1;
    minv = 0;
    maxv = 0;
    ok = 1;
    sw_pending = 0;
    sw_body_top = 0;

    next_token();
    depth = 1;

    while (tok.kind != TOK_EOF && depth > 0 && ok) {
        if (tok.kind == '{') {
            depth++;
            if (sw_pending) { sw_body_stack[sw_body_top++] = depth; sw_pending = 0; }
            next_token();
        } else if (tok.kind == '}') {
            if (sw_body_top > 0 && depth == sw_body_stack[sw_body_top - 1]) sw_body_top--;
            depth--;
            if (depth > 0)
                next_token();
        } else if (tok.kind == TOK_SWITCH) {
            /* Detect nested switch so its case labels are not collected here. */
            next_token();
            if (tok.kind == '(') {
                int pdepth = 1;
                next_token();
                while (tok.kind != TOK_EOF && pdepth > 0) {
                    if (tok.kind == '(') pdepth++;
                    else if (tok.kind == ')') pdepth--;
                    next_token();
                }
            }
            sw_pending = 1;
        } else if (sw_body_top == 0 && tok.kind == TOK_CASE) {
            long cv;
            next_token();
            cv = parse_const_long_expr();
            if (cv < 0 || cv > 32767) {
                ok = 0;
                break;
            }
            if (ncase >= MAX_SWITCH_CASES) {
                ok = 0;
                break;
            }
            for (i = 0; i < ncase; ++i) {
                if (case_vals[i] == (int)cv) {
                    ok = 0;
                    break;
                }
            }
            if (!ok)
                break;
            case_vals[ncase] = (int)cv;
            case_labs[ncase] = new_label();
            if (ncase == 0 || (int)cv < minv) minv = (int)cv;
            if (ncase == 0 || (int)cv > maxv) maxv = (int)cv;
            ncase++;
            if (tok.kind != ':') {
                ok = 0;
                break;
            }
            next_token();
        } else if (sw_body_top == 0 && tok.kind == TOK_DEFAULT) {
            if (have_default) {
                ok = 0;
                break;
            }
            have_default = 1;
            deflab = new_label();
            next_token();
            if (tok.kind != ':') {
                ok = 0;
                break;
            }
            next_token();
        } else {
            next_token();
        }
    }

    if (depth != 0)
        ok = 0;

    posi = save_posi;
    tok_start_pos = save_tok_start_pos;
    line_no = save_line_no;
    tok_line = save_tok_line;
    tok = save_tok;

    if (!ok || ncase < 3)
        return 0;

    /*
     * Dense switches are important for bytecode interpreters such as
     * pint.run().  The old limit rejected opcode dispatch ranges above
     * 31, so OP_HALT..OP_ODD (0..34) fell back to a long compare chain.
     * A 16-bit jump table costs two bytes per slot; allowing a larger
     * dense range is still a good space/time tradeoff for interpreter
     * dispatch and keeps sparse switches on the chain path below.
     */
    if ((maxv - minv) > 255)
        return 0;

    if ((maxv - minv + 1) > ncase * 2)
        return 0;

    case_countp[0] = ncase;
    default_labp[0] = have_default ? deflab : -1;
    minp[0] = minv;
    maxp[0] = maxv;
    return 1;
}

int scan_switch_cases_for_chain(int *case_vals, int *case_labs,
                                        int *case_countp, int *default_labp)
{
    long save_posi;
    long save_tok_start_pos;
    int save_line_no;
    int save_tok_line;
    struct Token save_tok;
    int depth;
    int ncase;
    int have_default;
    int deflab;
    int ok;
    int i;
    int sw_pending;
    int sw_body_top;
    int sw_body_stack[MAX_SWITCH_CASES];

    if (tok.kind != '{')
        return 0;

    save_posi = posi;
    save_tok_start_pos = tok_start_pos;
    save_line_no = line_no;
    save_tok_line = tok_line;
    save_tok = tok;

    ncase = 0;
    have_default = 0;
    deflab = -1;
    ok = 1;
    sw_pending = 0;
    sw_body_top = 0;

    next_token();
    depth = 1;

    while (tok.kind != TOK_EOF && depth > 0 && ok) {
        if (tok.kind == '{') {
            depth++;
            if (sw_pending) { sw_body_stack[sw_body_top++] = depth; sw_pending = 0; }
            next_token();
        } else if (tok.kind == '}') {
            if (sw_body_top > 0 && depth == sw_body_stack[sw_body_top - 1]) sw_body_top--;
            depth--;
            if (depth > 0)
                next_token();
        } else if (tok.kind == TOK_SWITCH) {
            /* Detect nested switch so its case labels are not collected here. */
            next_token();
            if (tok.kind == '(') {
                int pdepth = 1;
                next_token();
                while (tok.kind != TOK_EOF && pdepth > 0) {
                    if (tok.kind == '(') pdepth++;
                    else if (tok.kind == ')') pdepth--;
                    next_token();
                }
            }
            sw_pending = 1;
        } else if (sw_body_top == 0 && tok.kind == TOK_CASE) {
            long cv;
            int v16;

            next_token();
            cv = parse_const_long_expr();
            v16 = (int)(cv & 0xffffL);
            if (ncase >= MAX_SWITCH_CASES) {
                ok = 0;
                break;
            }
            for (i = 0; i < ncase; ++i) {
                if ((case_vals[i] & 0xffff) == v16) {
                    error_here("duplicate case value");
                    ok = 0;
                    break;
                }
            }
            if (!ok)
                break;
            case_vals[ncase] = v16;
            case_labs[ncase] = new_label();
            ncase++;
            if (tok.kind != ':') {
                ok = 0;
                break;
            }
            next_token();
        } else if (sw_body_top == 0 && tok.kind == TOK_DEFAULT) {
            if (have_default) {
                error_here("duplicate default label");
                ok = 0;
                break;
            }
            have_default = 1;
            deflab = new_label();
            next_token();
            if (tok.kind != ':') {
                ok = 0;
                break;
            }
            next_token();
        } else {
            next_token();
        }
    }

    if (depth != 0)
        ok = 0;

    posi = save_posi;
    tok_start_pos = save_tok_start_pos;
    line_no = save_line_no;
    tok_line = save_tok_line;
    tok = save_tok;

    if (!ok)
        return 0;

    case_countp[0] = ncase;
    default_labp[0] = have_default ? deflab : -1;
    return 1;
}

int switch_label_for_value(int value, int *case_vals, int *case_labs, int ncase, int default_lab, int lend)
{
    int i;
    for (i = 0; i < ncase; ++i)
        if (case_vals[i] == value)
            return case_labs[i];
    return default_lab >= 0 ? default_lab : lend;
}

void emit_switch_jump_table(int minv, int maxv,
                                   int *case_vals, int *case_labs,
                                   int ncase, int default_lab, int lend)
{
    int lok;
    int ltab;
    int v;
    int target;

    lok = new_label();
    ltab = new_label();

    if (minv != 0) {
        fprintf(outf, "\tld de,%d\n", minv);
        emit("\tor a\n\tsbc hl,de\n");
        emit_jp_label("jp c,", default_lab >= 0 ? default_lab : lend);
    }

    emit("\tpush hl\n");
    fprintf(outf, "\tld de,%d\n", maxv - minv);
    emit("\tor a\n\tsbc hl,de\n");
    emit("\tpop hl\n");
    emit_jp_label("jp z,", lok);
    emit_jp_label("jp nc,", default_lab >= 0 ? default_lab : lend);
    emit_label(lok);

    emit("\tadd hl,hl\n");
    fprintf(outf, "\tld de,L%d\n", ltab);
    emit("\tadd hl,de\n");
    emit("\tld e,(hl)\n");
    emit("\tinc hl\n");
    emit("\tld d,(hl)\n");
    emit("\tex de,hl\n");
    emit("\tjp (hl)\n");

    emit_label(ltab);
    for (v = minv; v <= maxv; ++v) {
        target = switch_label_for_value(v, case_vals, case_labs, ncase, default_lab, lend);
        fprintf(outf, "\tdw L%d\n", target);
    }
}

void gen_switch_table(void)
{
    int case_vals[MAX_SWITCH_CASES];
    int case_labs[MAX_SWITCH_CASES];
    int ncase;
    int default_lab;
    int minv;
    int maxv;
    int lend;
    int ci;
    int active_default_lab;

    expect(TOK_SWITCH);
    expect('(');
    gen_expr();
    expect(')');

    if (!scan_switch_cases_for_table(case_vals, case_labs, &ncase, &default_lab, &minv, &maxv)) {
        /* Re-parse via the old chain generator by restoring from the caller is
         * hard after emitting the expression, so this entry point is only used
         * after gen_switch() has already scanned successfully. */
        return;
    }

    lend = new_label();
    emit_switch_jump_table(minv, maxv, case_vals, case_labs, ncase, default_lab, lend);

    expect('{');
    enter_scope();

    break_stack[nflow] = lend;
    cont_stack[nflow] = (nflow > 0) ? cont_stack[nflow - 1] : lend;
    nflow++;

    ci = 0;
    active_default_lab = default_lab;

    if (g_sw_depth < MAX_SW_NEST) {
        g_sw_ctx[g_sw_depth].vals    = case_vals;
        g_sw_ctx[g_sw_depth].labs    = case_labs;
        g_sw_ctx[g_sw_depth].n       = ncase;
        g_sw_ctx[g_sw_depth].def_lab = default_lab;
        g_sw_depth++;
    }

    while (tok.kind != TOK_EOF && tok.kind != '}') {
        if (tok.kind == TOK_CASE) {
            next_token();
            (void)parse_const_long_expr();
            expect(':');

            if (ci < ncase)
                emit_label(case_labs[ci++]);

            while (tok.kind != TOK_EOF &&
                   tok.kind != TOK_CASE &&
                   tok.kind != TOK_DEFAULT &&
                   tok.kind != '}') {
                gen_statement();
            }
        } else if (tok.kind == TOK_DEFAULT) {
            next_token();
            expect(':');

            if (active_default_lab >= 0)
                emit_label(active_default_lab);

            while (tok.kind != TOK_EOF &&
                   tok.kind != TOK_CASE &&
                   tok.kind != TOK_DEFAULT &&
                   tok.kind != '}') {
                gen_statement();
            }
        } else {
            gen_statement();
        }
    }

    if (g_sw_depth > 0) g_sw_depth--;

    leave_scope();
    expect('}');
    nflow--;
    emit_label(lend);
}

void gen_switch(void)
{
    long save_posi;
    long save_tok_start_pos;
    int save_line_no;
    int save_tok_line;
    struct Token save_tok;
    int case_vals[MAX_SWITCH_CASES];
    int case_labs[MAX_SWITCH_CASES];
    int ncase;
    int default_lab;
    int minv;
    int maxv;

    /* Scan before consuming anything so fallback can use the old chain path. */
    save_posi = posi;
    save_tok_start_pos = tok_start_pos;
    save_line_no = line_no;
    save_tok_line = tok_line;
    save_tok = tok;

    next_token();          /* switch */
    if (tok.kind == '(') {
        int depth;
        next_token();
        depth = 1;
        while (tok.kind != TOK_EOF && depth > 0) {
            if (tok.kind == '(') depth++;
            else if (tok.kind == ')') depth--;
            next_token();
        }
    }

    if (tok.kind != '{' ||
        !scan_switch_cases_for_table(case_vals, case_labs, &ncase, &default_lab, &minv, &maxv)) {
        posi = save_posi;
        tok_start_pos = save_tok_start_pos;
        line_no = save_line_no;
        tok_line = save_tok_line;
        tok = save_tok;
        gen_switch_chain();
        return;
    }

    posi = save_posi;
    tok_start_pos = save_tok_start_pos;
    line_no = save_line_no;
    tok_line = save_tok_line;
    tok = save_tok;
    gen_switch_table();
}

void gen_do_while(void)
{
    int ltop, lcont, lend;
    int const_zero;

    ltop  = new_label();
    lcont = new_label();
    lend  = new_label();

    expect(TOK_DO);
    emit_label(ltop);

    break_stack[nflow] = lend;
    cont_stack[nflow]  = lcont;
    nflow++;

    gen_statement();

    nflow--;

    emit_label(lcont);
    expect(TOK_WHILE);
    expect('(');

    /*
     * The common macro wrapper
     *
     *     do { ... } while (0)
     *
     * should compile as one execution of the body.  Keep the loop labels so
     * break and continue inside the body retain correct C semantics, but do
     * not emit code to materialize/test constant zero or a dead back-edge.
     */
    const_zero = 0;
    if (tok.kind == TOK_NUM && tok.val == 0) {
        long save_posi;
        long save_tok_start_pos;
        int save_line_no;
        int save_tok_line;
        struct Token save_tok;

        save_posi = posi;
        save_tok_start_pos = tok_start_pos;
        save_line_no = line_no;
        save_tok_line = tok_line;
        save_tok = tok;

        next_token();
        if (tok.kind == ')') {
            const_zero = 1;
        } else {
            posi = save_posi;
            tok_start_pos = save_tok_start_pos;
            line_no = save_line_no;
            tok_line = save_tok_line;
            tok = save_tok;
        }
    }

    if (!const_zero) {
        if (!gen_condition_branch_true(ltop)) {
            gen_expr();
            emit_test_expr_nonzero(g_expr_type, ltop, 1);
        }
    }

    expect(')');
    expect(';');
    emit_label(lend);
}

void gen_statement(void)
{
    if (ast_try_emit_statement())
        return;

    if (tok.kind == '{') {
        gen_compound();
    } else if (tok.kind == TOK_DO) {
        gen_do_while();
    } else if (tok.kind == TOK_IF) {
        gen_if();
    } else if (tok.kind == TOK_WHILE) {
        gen_while();
    } else if (tok.kind == TOK_FOR) {
        gen_for();
    } else if (tok.kind == TOK_SWITCH) {
        gen_switch();
    } else if (tok.kind == TOK_RETURN) {
        gen_return();
    } else if (tok.kind == TOK_BREAK) {
        next_token();
        expect(';');
        if (nflow <= 0) error_here("break outside loop");
        else emit_jp_label("jp", break_stack[nflow - 1]);
    } else if (tok.kind == TOK_CONTINUE) {
        next_token();
        expect(';');
        if (nflow <= 0) error_here("continue outside loop");
        else emit_jp_label("jp", cont_stack[nflow - 1]);
    } else if (tok.kind == TOK_GOTO) {
        char lname[64];
        next_token();
        if (tok.kind != TOK_ID) {
            error_here("label name expected after goto");
        } else {
            strncpy(lname, tok.text, sizeof(lname) - 1);
            lname[sizeof(lname) - 1] = 0;
            next_token();
            emit_jp_label("jp", mark_user_label_reference(lname));
        }
        expect(';');
    } else if (tok.kind == TOK_ID) {
        /* Peek one token ahead to check for a user label: identifier ':' */
        long save_pos;
        long save_tok_start;
        int save_line, save_tok_line;
        struct Token save_tok;
        char lname[64];
        save_pos = posi;
        save_tok_start = tok_start_pos;
        save_line = line_no;
        save_tok_line = tok_line;
        save_tok = tok;
        strncpy(lname, tok.text, sizeof(lname) - 1);
        lname[sizeof(lname) - 1] = 0;
        next_token();
        if (tok.kind == ':') {
            /* This is a labeled statement: emit the label, consume ':', recurse */
            emit_label(define_user_label(lname));
            next_token();
            gen_statement();
        } else {
            /* Not a label — restore state and fall through to expression statement */
            posi = save_pos;
            tok_start_pos = save_tok_start;
            line_no = save_line;
            tok_line = save_tok_line;
            tok = save_tok;
            gen_expr_statement();
        }
    } else if (tok.kind == TOK_CASE && g_sw_depth > 0) {
        /* case label inside a nested statement within a switch (e.g. Duff's device) */
        long cv;
        int i, lab;
        struct SwCtx *sw = &g_sw_ctx[g_sw_depth - 1];
        next_token();
        cv = parse_const_long_expr();
        expect(':');
        lab = -1;
        for (i = 0; i < sw->n; ++i) {
            if ((sw->vals[i] & 0xffff) == ((int)cv & 0xffff)) {
                lab = sw->labs[i];
                break;
            }
        }
        if (lab >= 0) emit_label(lab);
        gen_statement();
    } else if (tok.kind == TOK_DEFAULT && g_sw_depth > 0) {
        struct SwCtx *sw = &g_sw_ctx[g_sw_depth - 1];
        next_token();
        expect(':');
        if (sw->def_lab >= 0) emit_label(sw->def_lab);
        gen_statement();
    } else {
        gen_expr_statement();
    }
}


