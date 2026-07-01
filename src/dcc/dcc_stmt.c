/*
 * dcc_stmt.c - statement code generation.
 *
 * Compound-block parsing plus shared statement/codegen helpers used by the AST
 * emitter. Statement lowering itself lives in dcc_ast_gen.c.
 *
 * MODULE: compiled as its own translation unit; shared declarations are in dcc.h.
 * Source provenance: monolith src/ddc.c lines 13992-15879.
 */

#include "dcc.h"
#include "dcc_ast.h"

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

void gen_statement(void)
{
    if (ast_try_emit_statement())
        return;

    fatal("unsupported AST statement");
}


