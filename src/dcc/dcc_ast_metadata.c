/*
 * dcc_ast_metadata.c - non-emitting function-body AST metadata walk.
 *
 * MIR owns production function code generation.  This walker preserves the
 * parser side effects that historically happened while the legacy AST emitter
 * traversed a supported statement: declaration replay, lexical scopes,
 * for-init renames, VLA control metadata, user-label diagnostics, debug
 * events, and AST transforms that reserve compiler temporaries.
 */

#include "dcc.h"
#include "dcc_ast.h"
#include "dcc_mir.h"

static int metadata_switch_depth;

void ast_replay_compound_literal(const struct AstNode *node)
{
    struct AstCompoundLitSpan *span;
    struct Sym *symbol;
    LexState saved;
    int type;

    if (node == NULL || node->kind != AST_COMPOUND_LITERAL ||
        node->aux == NULL || node->sym == NULL)
        return;
    span = (struct AstCompoundLitSpan *)node->aux;
    symbol = node->sym;
    type = node->type;
    saved = lex_save();

    g_lex.posi = span->posi;
    g_lex.tok_start_pos = span->tok_start_pos;
    g_lex.line_no = span->line_no;
    g_lex.tok_line = span->tok_line;
    g_lex.tok = span->tok;

    mir_begin_compound_literal(symbol);
    if ((type & TYPE_STRUCT) && type_ptr_depth(type) == 0) {
        emit_init_auto_struct_from_list(symbol);
    } else if (accept('{')) {
        emit_init_auto_struct_scalar(symbol, 0, type);
        if (g_lex.tok.kind == ',')
            next_token();
        expect('}');
    } else {
        emit_init_auto_struct_scalar(symbol, 0, type);
    }
    mir_end_compound_literal(symbol);
    lex_restore(&saved);
}

void ast_process_expr_metadata(const struct AstNode *node)
{
    int i;

    if (node == NULL)
        return;
    if (node->kind == AST_STR_LIT)
        (void)add_string_ex(
            node->sval, (int)node->uval, (int)node->ival);
    if (node->kind == AST_COMPOUND_LITERAL && !scan_mode)
        ast_replay_compound_literal(node);
    if (node->kind == AST_CALL) {
        if (ast_process_inline_call_metadata(node, 0))
            return;
        if (node->a == NULL || node->a->kind != AST_IDENT ||
            node->a->sym == NULL ||
            node->a->sym->storage != SC_FUNC)
            ast_process_expr_metadata(node->a);
        for (i = node->list_len - 1; i >= 0; --i)
            ast_process_expr_metadata(node->list[i]);
        return;
    }
    if (node->kind == AST_IDENT && node->sym != NULL &&
        node->sym->storage == SC_FUNC && node->sym->is_static)
        node->sym->deferred_body_needed = 1;
    ast_process_expr_metadata(node->a);
    ast_process_expr_metadata(node->b);
    ast_process_expr_metadata(node->c);
    ast_process_expr_metadata(node->d);
    for (i = 0; i < node->list_len; ++i)
        ast_process_expr_metadata(node->list[i]);
}

void ast_validate_expr_symbols(const struct AstNode *node)
{
    int i;

    if (node == NULL)
        return;
    if (node->kind == AST_CALL) {
        if (node->a == NULL || node->a->kind != AST_IDENT)
            ast_validate_expr_symbols(node->a);
        for (i = 0; i < node->list_len; ++i)
            ast_validate_expr_symbols(node->list[i]);
        return;
    }
    if (node->kind == AST_IDENT &&
        find_sym(node->sval) == NULL &&
        find_enum_const(node->sval) < 0) {
        char message[MAX_TOK_TEXT + 64];
        const char *source_file =
            node->file != NULL && node->file[0] != 0
                ? node->file
                : (input_name ? input_name : "<input>");

        sprintf(message, "use of undeclared identifier '%s'",
                node->sval);
        dcc_error_at(source_file,
                     node->line > 0 ? node->line : g_lex.tok_line,
                     -1, message, NULL);
        return;
    }
    ast_validate_expr_symbols(node->a);
    ast_validate_expr_symbols(node->b);
    ast_validate_expr_symbols(node->c);
    ast_validate_expr_symbols(node->d);
    for (i = 0; i < node->list_len; ++i)
        ast_validate_expr_symbols(node->list[i]);
}

static void replay_declaration(const struct AstNode *node)
{
    if (scan_mode) {
        ast_scan_decl_span(node);
    } else {
        mir_begin_declaration(node);
        ast_replay_decl_span(node);
        mir_end_declaration();
    }
}

static void walk_for(const struct AstNode *node)
{
    int for_seq;
    int rename_count = 0;

    for_seq = g_func_pass.for_seq++;
    if (for_seq >= MAX_FOR_SCOPES)
        fatal("too many for statements");
    g_for_rename_count[for_seq] = 0;

    if (node->a != NULL && node->a->kind == AST_DECL) {
        int old_seq = g_func_pass.for_decl_seq;
        int old_index = g_func_pass.for_decl_rename_index;
        int old_recording = g_func_pass.for_decl_recording;

        g_func_pass.for_decl_seq = for_seq;
        g_func_pass.for_decl_rename_index = 0;
        g_func_pass.for_decl_recording = 1;
        replay_declaration(node->a);
        rename_count = g_func_pass.for_decl_rename_index;
        g_func_pass.for_decl_seq = old_seq;
        g_func_pass.for_decl_rename_index = old_index;
        g_func_pass.for_decl_recording = old_recording;
    } else
        ast_process_expr_metadata(node->a);

    ast_plan_for_metadata(node);
    ast_process_expr_metadata(node->b);
    ast_process_expr_metadata(node->c);
    flow_scope_depth[nflow] = g_func_pass.scope_depth;
    ++nflow;
    ast_process_stmt_metadata(node->d);
    --nflow;

    while (rename_count-- > 0)
        pop_for_rename();
}

void ast_process_stmt_metadata(const struct AstNode *node)
{
    int i;

    if (node == NULL)
        return;
    ast_record_debug_location(node->file, node->line);

    switch (node->kind) {
    case AST_EMPTY:
    case AST_DIVMOD_CALL:
        return;
    case AST_EXPR_STMT: {
        if (node->a == NULL ||
            !ast_process_inline_call_metadata(node->a, 1))
            ast_process_expr_metadata(node->a);
        return;
    }
    case AST_RETURN:
        ast_process_expr_metadata(node->a);
        return;
    case AST_DECL:
        replay_declaration(node);
        return;
    case AST_BREAK:
    case AST_CONTINUE:
        if (nflow <= 0)
            fatal(node->kind == AST_BREAK
                ? "break statement outside loop or switch"
                : "continue statement outside loop");
        mir_begin_flow_replay();
        emit_vla_restore_for_flow(flow_scope_depth[nflow - 1]);
        mir_end_flow_replay();
        return;
    case AST_GOTO: {
        int label_index = find_or_alloc_user_label_index(node->sval);
        int active_depth;

        mir_begin_flow_replay();
        ulabel_referenced[label_index] = 1;
        active_depth = vla_active_scope_depth();
        if (ulabel_defined[label_index]) {
            if (vla_jump_enters_label_scope(label_index))
                error_here(
                    "goto into a variable-length array scope is not supported");
            else if (active_depth != 0)
                emit_vla_restore_to_label_scope(label_index);
        } else if (active_depth != 0) {
            (void)vla_record_fwd_goto(label_index, node->line);
        } else {
            ulabel_shallow_fwd_ref[label_index] = 1;
        }
        mir_end_flow_replay();
        return;
    }
    case AST_LABEL: {
        int label_index = find_or_alloc_user_label_index(node->sval);

        mir_begin_label_replay(node->sval);
        if (vla_active_scope_depth() != 0 &&
            ulabel_shallow_fwd_ref[label_index])
            error_here(
                "goto into a variable-length array scope is not supported");
        vla_resolve_fwd_gotos(label_index, ulabel_ids[label_index]);
        mir_end_label_replay();
        (void)define_user_label(node->sval);
        ast_process_stmt_metadata(node->b);
        return;
    }
    case AST_CASE:
    case AST_DEFAULT:
        if (metadata_switch_depth <= 0)
            fatal(node->kind == AST_CASE
                ? "case label outside switch"
                : "default label outside switch");
        if (vla_active_scope_depth() != 0)
            error_here(node->kind == AST_CASE
                ? "case label inside a variable-length array scope is not supported"
                : "default label inside a variable-length array scope is not supported");
        ast_process_stmt_metadata(node->b);
        return;
    case AST_IF:
        ast_process_expr_metadata(node->a);
        ast_process_stmt_metadata(node->b);
        ast_process_stmt_metadata(node->c);
        return;
    case AST_WHILE:
    case AST_DOWHILE:
        ast_process_expr_metadata(node->a);
        flow_scope_depth[nflow] = g_func_pass.scope_depth;
        ++nflow;
        ast_process_stmt_metadata(node->b);
        --nflow;
        return;
    case AST_FOR:
        walk_for(node);
        return;
    case AST_SWITCH:
        ast_process_expr_metadata(node->a);
        enter_scope();
        flow_scope_depth[nflow] = g_func_pass.scope_depth;
        ++nflow;
        ++metadata_switch_depth;
        ast_process_stmt_metadata(node->b);
        --metadata_switch_depth;
        --nflow;
        leave_scope();
        return;
    case AST_COMPOUND: {
        int dead = 0;
        const struct AstNode *fused =
            ast_divmod_fuse_compound(node);
        const struct AstNode *body =
            fused != NULL ? fused : node;

        mir_begin_scope_replay();
        enter_scope();
        for (i = 0; i < body->list_len; ++i) {
            if (dead && !ast_stmt_has_reentry_label(body->list[i])) {
                int j;

                if (body->list[i]->kind == AST_DECL)
                    for (j = i + 1; j < body->list_len; ++j)
                        if (ast_stmt_has_reentry_label(body->list[j])) {
                            ++asm_suppress_depth;
                            ast_process_stmt_metadata(body->list[i]);
                            --asm_suppress_depth;
                            break;
                        }
                continue;
            }
            ast_process_stmt_metadata(body->list[i]);
            dead = ast_stmt_exits(body->list[i]);
        }
        if (!dead && g_func_pass.scope_depth < MAX_SCOPE_DEPTH &&
            g_vla_scope_off[g_func_pass.scope_depth] != 0)
            emit_vla_restore_sp(
                g_vla_scope_off[g_func_pass.scope_depth]);
        mir_end_scope_replay();
        if (!dead)
            ast_record_debug_location(node->end_file, node->end_line);
        leave_scope();
        return;
    }
    default:
        fatal("unsupported AST metadata node");
    }
}
