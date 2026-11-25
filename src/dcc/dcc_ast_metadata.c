/**
 * @file dcc_ast_metadata.c
 * @brief Replays non-emitting metadata from function-body ASTs.
 *
 * @par Role
 * Walks expressions and statements to preserve declaration replay, lexical
 * scopes, for-init renames, VLA save/restore facts, inline expansion metadata,
 * strings, user-label diagnostics, static-function references, debug events,
 * and temporary-reserving AST plans.
 *
 * @par Key entry points
 * ast_process_stmt_metadata(), ast_process_expr_metadata(),
 * ast_validate_expr_symbols(), and ast_replay_compound_literal().
 *
 * @par Boundary
 * MIR owns production function code generation. This walker records frontend
 * side effects only and does not select or emit a function-body candidate.
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
        parse_auto_struct_initializer_list(symbol);
    } else if (accept('{')) {
        parse_auto_struct_scalar_initializer(symbol, 0, type);
        if (g_lex.tok.kind == ',')
            next_token();
        expect('}');
    } else {
        parse_auto_struct_scalar_initializer(symbol, 0, type);
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
    if (node->kind == AST_SIZEOF_EXPR && !scan_mode)
        mir_resolve_deferred_sizeof(node);
    ast_process_expr_metadata(node->a);
    ast_process_expr_metadata(node->b);
    ast_process_expr_metadata(node->c);
    ast_process_expr_metadata(node->d);
    for (i = 0; i < node->list_len; ++i)
        ast_process_expr_metadata(node->list[i]);
}

/* Whether a declaration anywhere in statement n declares or mentions name. */
static int ast_stmt_decl_touches_name(const struct AstNode *n,
                                      const char *name)
{
    int i;

    if (n == NULL)
        return 0;
    if (n->kind == AST_DECL)
        return ast_decl_span_touches_name(n, name);
    if (ast_stmt_decl_touches_name(n->a, name) ||
        ast_stmt_decl_touches_name(n->b, name) ||
        ast_stmt_decl_touches_name(n->c, name) ||
        ast_stmt_decl_touches_name(n->d, name))
        return 1;
    for (i = 0; i < n->list_len; ++i)
        if (ast_stmt_decl_touches_name(n->list[i], name))
            return 1;
    return 0;
}

/* The struct a member access names when it is a complete struct object
 * (through one pointer for `->`), or 0 when that is not known. */
static int ast_member_complete_struct_id(const struct AstNode *n)
{
    int base = ast_expr_type_for_sizeof(n->a);
    int sid;
    int i;

    if (n->op == TOK_ARROW) {
        if (type_ptr_depth(base) != 1)
            return 0;
        base = type_decay_ptr(base);
    }
    if (!type_is_struct_object(base) || type_ptr_depth(base) != 0)
        return 0;
    sid = base_struct_id_from_type(base);
    if (sid <= 0 || sid > nstruct_defs)
        return 0;
    /* Fields are recorded by parent; a struct with none is incomplete. */
    for (i = 0; i < nfield_defs; ++i)
        if (field_defs[i].parent_struct_id == sid)
            return sid;
    return 0;
}

/* An assignment, subscript or sizeof that needs the size of an incomplete
 * struct type. */
static int ast_expr_is_incomplete_object(const struct AstNode *n)
{
    int type;

    if (n->a == NULL)
        return 0;
    if (n->kind == AST_ASSIGN)
        return type_is_incomplete_struct(ast_expr_type_for_sizeof(n->a));
    if (n->kind == AST_SIZEOF_EXPR)
        return type_is_incomplete_struct(ast_expr_type_for_sizeof(n->a));
    if (n->kind == AST_INDEX) {
        type = ast_expr_type_for_sizeof(n->a);
        return type_ptr_depth(type) == 1 &&
               type_is_incomplete_struct(type_decay_ptr(type));
    }
    return 0;
}

static void ast_validate_symbols_in(const struct AstNode *node,
                                    const struct AstNode *scope,
                                    int members)
{
    int i;
    int sid;

    if (node == NULL || node->kind == AST_DECL)
        return;
    if (members && ast_expr_is_incomplete_object(node)) {
        int type = node->kind == AST_INDEX
            ? type_decay_ptr(ast_expr_type_for_sizeof(node->a))
            : ast_expr_type_for_sizeof(node->kind == AST_ASSIGN ? node->a : node);
        char message[160];

        if (node->kind == AST_SIZEOF_EXPR)
            type = ast_expr_type_for_sizeof(node->a);
        snprintf(message, sizeof(message),
                 "expression has incomplete type 'struct %s'",
                 struct_defs[type_struct_id(type) - 1].name);
        dcc_error_at(node->file != NULL && node->file[0] != 0
                         ? node->file
                         : (input_name ? input_name : "<input>"),
                     node->line > 0 ? node->line : g_lex.tok_line,
                     node->line > 0 ? node->pos : -1, message, NULL);
        return;
    }
    if (members && node->kind == AST_MEMBER && node->sval != NULL &&
        node->a != NULL &&
        (sid = ast_member_complete_struct_id(node)) != 0 &&
        find_field_def(sid, node->sval) == NULL) {
        char message[MAX_TOK_TEXT + 64];

        sprintf(message, "unknown struct field '%s'", node->sval);
        dcc_error_at(node->file != NULL && node->file[0] != 0
                         ? node->file
                         : (input_name ? input_name : "<input>"),
                     node->line > 0 ? node->line : g_lex.tok_line,
                     node->line > 0 ? node->pos : -1, message, NULL);
        return;
    }
    if (node->kind == AST_CALL) {
        if (node->a == NULL || node->a->kind != AST_IDENT)
            ast_validate_symbols_in(node->a, scope, members);
        for (i = 0; i < node->list_len; ++i)
            ast_validate_symbols_in(node->list[i], scope, members);
        return;
    }
    if (node->kind == AST_IDENT &&
        find_sym(node->sval) == NULL &&
        find_enum_const(node->sval) < 0 &&
        !ast_stmt_decl_touches_name(scope, node->sval)) {
        char message[MAX_TOK_TEXT + 64];
        const char *source_file =
            node->file != NULL && node->file[0] != 0
                ? node->file
                : (input_name ? input_name : "<input>");

        sprintf(message, "use of undeclared identifier '%s'",
                node->sval);
        dcc_error_at(source_file,
                     node->line > 0 ? node->line : g_lex.tok_line,
                     node->line > 0 ? node->pos : -1, message, NULL);
        return;
    }
    ast_validate_symbols_in(node->a, scope, members);
    ast_validate_symbols_in(node->b, scope, members);
    ast_validate_symbols_in(node->c, scope, members);
    ast_validate_symbols_in(node->d, scope, members);
    for (i = 0; i < node->list_len; ++i)
        ast_validate_symbols_in(node->list[i], scope, members);
}

void ast_validate_expr_symbols(const struct AstNode *node)
{
    ast_validate_symbols_in(node, NULL, 0);
}

/* Checks a statement the AST gate accepted before it reaches code
 * generation. Locals its own declarations introduce are not in the symbol
 * table yet, so any name one of them touches is taken as declared; member
 * names are checked too, since this statement will not be rejected later. */
void ast_validate_stmt_symbols(const struct AstNode *node)
{
    ast_validate_symbols_in(node, node, 1);
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
    if (!mir_is_active())
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
        capture_vla_restore_for_flow(flow_scope_depth[nflow - 1]);
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
                capture_vla_restore_to_label_scope(label_index);
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
        vla_resolve_fwd_gotos(label_index);
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
                /* MIR still contains this subtree. Replay its declarations
                 * and scopes without executing their initializers, including
                 * neutralizing initializer instructions from support probes. */
                ++asm_suppress_depth;
                ast_process_stmt_metadata(body->list[i]);
                --asm_suppress_depth;
                continue;
            }
            ast_process_stmt_metadata(body->list[i]);
            dead = ast_stmt_exits(body->list[i]);
        }
        if (!dead && g_func_pass.scope_depth < MAX_SCOPE_DEPTH &&
            g_vla_scope_off[g_func_pass.scope_depth] != 0)
            capture_vla_restore_sp(
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
