/*
 * dcc_stmt.c - statement code generation.
 *
 * Compound-block parsing plus shared statement/codegen helpers used by the AST
 * emitter. Statement lowering itself lives in dcc_ast_gen*.c.
 *
 * MODULE: compiled as its own translation unit; shared declarations are in dcc.h.
 * Source provenance: monolith src/ddc.c lines 13992-15879.
 */

#include "dcc.h"
#include "dcc_ast.h"

static int current_identifier_starts_label(void)
{
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    struct Token save_tok;
    int is_label;

    if (tok.kind != TOK_ID)
        return 0;
    save_pos = posi;
    save_tok_start = tok_start_pos;
    save_line = line_no;
    save_tok_line = tok_line;
    save_tok = tok;
    next_token();
    is_label = (tok.kind == ':');
    posi = save_pos;
    tok_start_pos = save_tok_start;
    line_no = save_line;
    tok_line = save_tok_line;
    tok = save_tok;
    return is_label;
}

void gen_compound(void)
{
    int dead;

    /* Forward-goto VLA fixups are function-scoped; start each body run clean. */
    g_vla_fwd_ngoto = 0;
    expect('{');
    enter_scope();
    dead = 0;
    while (tok.kind != TOK_EOF && tok.kind != '}') {
        if (tok.kind == TOK_STATIC_ASSERT) {
            parse_static_assert_decl();
        } else if (tok.kind == TOK_TYPEDEF) {
            parse_typedef_decl();
        } else if (current_identifier_starts_label()) {
            gen_statement();
            dead = ast_last_statement_exits();
        } else if (starts_type()) {
            int t;
            int is_static_local;
            int decl_line;
            struct Token decl_tok;
            decl_is_extern = 0;
            is_static_local = (tok.kind == TOK_STATIC);
            decl_line = tok_line;
            decl_tok = tok;
            t = parse_base_type();
            if (tok.kind == ';') {
                next_token();
            } else if (is_static_local) {
                scan_static_local_decl_after_type(t);
            } else {
                if (!decl_is_extern && !dead)
                    ast_emit_debug_location(decl_tok.file, decl_line);
                if (dead)
                    asm_suppress_depth++;
                gen_local_decl_after_type(t);
                if (dead)
                    asm_suppress_depth--;
            }
        } else {
            if (dead) {
                struct AstNode *n;

                n = ast_build_stmt(&g_ast_arena);
                if (n != NULL)
                    ast_support_cache_begin();
                if (n == NULL || !ast_stmt_supported(n))
                    fatal("unsupported AST statement");
                if (ast_stmt_has_reentry_label(n)) {
                    ast_gen_stmt(n);
                    dead = ast_stmt_exits(n);
                }
                ast_arena_reset(&g_ast_arena);
            } else {
                gen_statement();
                dead = ast_last_statement_exits();
            }
        }
    }

    if (!dead && tok.kind == '}')
        ast_emit_debug_location(tok.file, tok_line);
    leave_scope();
    expect('}');
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


