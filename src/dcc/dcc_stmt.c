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
#include "dcc_mir.h"

static int current_identifier_starts_label(void)
{
    LexState _ls;
    int is_label;

    if (g_lex.tok.kind != TOK_ID)
        return 0;
    _ls = lex_save();
    next_token();
    is_label = (g_lex.tok.kind == ':');
    lex_restore(&_ls);
    return is_label;
}

void process_compound(void)
{
    int dead;

    /* Forward-goto VLA fixups are function-scoped; start each body run clean. */
    g_vla_fwd_ngoto = 0;
    g_func_close_line = 0;
    expect('{');
    enter_scope();
    dead = 0;
    while (g_lex.tok.kind != TOK_EOF && g_lex.tok.kind != '}') {
        if (g_lex.tok.kind == TOK_STATIC_ASSERT) {
            parse_static_assert_decl();
        } else if (g_lex.tok.kind == TOK_TYPEDEF) {
            parse_typedef_decl();
        } else if (current_identifier_starts_label()) {
            gen_statement();
            dead = ast_last_statement_exits();
        } else if (starts_type()) {
            int t;
            int is_static_local;
            int decl_line;
            struct Token decl_tok;
            g_decl.is_extern = 0;
            is_static_local = (g_lex.tok.kind == TOK_STATIC);
            decl_line = g_lex.tok_line;
            decl_tok = g_lex.tok;
            t = parse_base_type();
            if (g_lex.tok.kind == ';') {
                next_token();
            } else if (is_static_local) {
                scan_static_local_decl_after_type(t);
            } else {
                if (!g_decl.is_extern && !dead)
                    ast_record_debug_location(decl_tok.file, decl_line);
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
                    mir_capture_stmt(n);
                    ast_process_stmt_metadata(n);
                    dead = ast_stmt_exits(n);
                }
                ast_arena_reset(&g_ast_arena);
            } else {
                gen_statement();
                dead = ast_last_statement_exits();
            }
        }
    }

    if (g_lex.tok.kind == '}') {
        if (!dead) {
            /* Body falls through: the closing brace is a reachable step in
             * this scope, emitted here before the epilogue. */
            ast_record_debug_location(g_lex.tok.file, g_lex.tok_line);
            if ((current_return_type & 15) != TYPE_VOID &&
                strcmp(g_current_compiling_func, "main") != 0)
                warn_at(g_lex.tok.file, g_lex.tok_line, "control reaches end of non-void function");
        } else {
            /* Body always exits: no in-block closing-brace marker is emitted,
             * so hand the location to finish_function_mir, which maps the
             * shared return label to it (early returns jump there). */
            const char *cf = g_lex.tok.file[0] ? g_lex.tok.file :
                             (input_name ? input_name : "<input>");
            strncpy(g_func_close_file, cf, sizeof(g_func_close_file) - 1);
            g_func_close_file[sizeof(g_func_close_file) - 1] = 0;
            g_func_close_line = g_lex.tok_line;
        }
    }
    leave_scope();
    expect('}');
}

void gen_statement(void)
{
    if (ast_process_statement())
        return;

    fatal("unsupported AST statement");
}
