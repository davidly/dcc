/**
 * @file dcc_ast_gen_expr.c
 * @brief Provides AST expression helpers, initializer capture, and inline
 * metadata.
 *
 * @par Role
 * Parses and captures scalar/aggregate declaration initializers, and clones
 * inline bodies for non-emitting metadata traversal.
 *
 * @par Key entry points
 * ast_emit_init_expr(), ast_emit_struct_init_expr_assign(),
 * ast_emit_discarded_expr(), and ast_process_inline_call_metadata().
 *
 * @par Boundary
 * Initializer semantics go through mir_capture_initializer() or
 * mir_capture_struct_initializer(). This layer never emits function bodies.
 */
#include <string.h>
#include "dcc_ast_gen_internal.h"
#include "dcc_mir.h"

static const struct AstNode *inline_substitution_body(struct Sym *fn);

void ast_emit_init_expr(void)
{
    struct AstNode *n;
    int ptr_type;
    int no_deref;
    LexState _ls;
    LexState _le;

    _ls = lex_save();

    n = ast_build_assign_expr(&g_ast_init_arena);

    _le = lex_save();
    if (n != NULL)
        ast_validate_expr_symbols(n);

    if (n != NULL && (ast_pointer_expr_type(n, &ptr_type, &no_deref) ||
                      ast_gen_supported(n) || n->kind == AST_CAST ||
                      ast_numeric_value_supported(n) ||
                      ast_pointer_assign_rhs_supported(n) ||
                      (n->kind == AST_CALL && ast_value_is_pointer_word(n) &&
                       ast_call_named_args_supported(n)))) {
        mir_capture_initializer(n);
        ast_process_expr_metadata(n);
        g_expr.type = ast_expr_type_for_sizeof(n);
        g_expr.long_from16 = 0;
        ast_arena_reset(&g_ast_init_arena);
        return;
    }

    if (getenv("DCC_AST_REPORT") != NULL) {
        if (n == NULL)
            fprintf(stderr, "; AST-unsupported init build token=%d text='%s' line=%d\n",
                    g_lex.tok.kind, g_lex.tok.text, g_lex.tok_line);
        else
            fprintf(stderr, "; AST-unsupported init gate kind=%s line=%d\n",
                    ast_kind_name(n->kind), g_lex.tok_line);
    }
    ast_arena_reset(&g_ast_init_arena);

    lex_restore(&_ls);
    error_here(n == NULL ? "malformed initializer expression" : "unsupported initializer expression");

    if (n != NULL) {
        lex_restore(&_le);
    } else {
        while (g_lex.tok.kind != TOK_EOF && g_lex.tok.kind != ',' && g_lex.tok.kind != ';' && g_lex.tok.kind != '}')
            next_token();
    }

    g_expr.type = TYPE_INT;
}

/* Capture an expression whose value is discarded but whose side
 * effects are required.  VLA parameter bounds use this at function entry. */
void ast_emit_discarded_expr(void)
{
    struct AstNode *n = ast_build_assign_expr(&g_ast_init_arena);

    if (n == NULL) {
        ast_arena_reset(&g_ast_init_arena);
        error_here("malformed discarded expression");
        return;
    }
    ast_validate_expr_symbols(n);
    mir_capture_discarded_expr(n);
    ast_process_expr_metadata(n);
    ast_arena_reset(&g_ast_init_arena);
}

void ast_emit_struct_init_expr_assign(struct Sym *s)
{
    struct AstNode *rhs;
    LexState _ls;
    LexState _le;

    _ls = lex_save();

    rhs = ast_build_assign_expr(&g_ast_init_arena);

    _le = lex_save();
    if (rhs != NULL)
        ast_validate_expr_symbols(rhs);

    if (rhs != NULL) {
        mir_capture_struct_initializer(s, rhs);
        ast_process_expr_metadata(rhs);
        ast_arena_reset(&g_ast_init_arena);
        return;
    }

    if (getenv("DCC_AST_REPORT") != NULL) {
        if (rhs == NULL)
            fprintf(stderr, "; AST-unsupported struct init build token=%d text='%s' line=%d\n",
                    g_lex.tok.kind, g_lex.tok.text, g_lex.tok_line);
        else
            fprintf(stderr, "; AST-unsupported struct init gate kind=%s line=%d\n",
                    ast_kind_name(rhs->kind), g_lex.tok_line);
    }
    ast_arena_reset(&g_ast_init_arena);

    lex_restore(&_ls);
    error_here(rhs == NULL ? "malformed initializer expression" : "unsupported struct initializer expression");

    if (rhs != NULL) {
        lex_restore(&_le);
    } else {
        while (g_lex.tok.kind != TOK_EOF && g_lex.tok.kind != ',' && g_lex.tok.kind != ';' && g_lex.tok.kind != '}')
            next_token();
    }
}

static int inline_arg_reusable(const struct AstNode *n)
{
    if (n == NULL)
        return 0;
    return n->kind == AST_INT_LIT || n->kind == AST_STR_LIT ||
           n->kind == AST_SIZEOF_EXPR || n->kind == AST_SIZEOF_TYPE ||
           n->kind == AST_IDENT;
}

static int inline_param_index_for_call(struct Sym *fn, const char *name)
{
    int i;
    if (fn == NULL || name == NULL)
        return -1;
    for (i = 0; i < fn->proto_nargs && i < MAX_PROTO_PARAMS; ++i)
        if (!strcmp(fn->inline_param_names[i], name))
            return i;
    return -1;
}

static void inline_temp_name_for_call(char *dst, int dstsz, int index)
{
    sprintf(dst, "#itmp%d", index);
    (void)dstsz;
}

static const struct AstNode *inline_substitution_body(struct Sym *fn)
{
    if (fn == NULL)
        return NULL;
    if (fn->inline_return_expr != NULL)
        return fn->inline_return_expr;
    if (fn->inline_stmt_expr != NULL)
        return fn->inline_stmt_expr;
    return fn->inline_stmt_body;
}

/* Direct substitution may skip, duplicate, or move an argument into the
 * callee body. That is safe only when the expression depends entirely on
 * constants and caller-local automatic objects that the body cannot reach.
 * Everything else is evaluated once into a temp before body generation,
 * exactly as for a real call. */
static int inline_arg_is_body_independent(const struct AstNode *n)
{
    struct Sym *s;
    int i;

    if (n == NULL)
        return 1;
    switch (n->kind) {
    case AST_INT_LIT:
    case AST_FLOAT_LIT:
    case AST_STR_LIT:
    case AST_SIZEOF_EXPR:
    case AST_SIZEOF_TYPE:
        return 1;
    case AST_IDENT:
        if (find_enum_const(n->sval) >= 0)
            return 1;
        s = find_local(n->sval);
        return s != NULL && !s->is_static && !s->is_volatile &&
               (s->storage == SC_LOCAL || s->storage == SC_PARAM) &&
               local_name_address_taken_in_function(n->sval) == 0;
    case AST_UNARY:
        if (n->op == '*' || n->op == TOK_INC || n->op == TOK_DEC)
            return 0;
        break;
    case AST_BINARY:
    case AST_LOGAND:
    case AST_LOGOR:
    case AST_COMMA:
    case AST_COND:
    case AST_CAST:
        break;
    default:
        return 0;
    }
    if (!inline_arg_is_body_independent(n->a) ||
        !inline_arg_is_body_independent(n->b) ||
        !inline_arg_is_body_independent(n->c) ||
        !inline_arg_is_body_independent(n->d))
        return 0;
    for (i = 0; i < n->list_len; ++i)
        if (!inline_arg_is_body_independent(n->list[i]))
            return 0;
    return 1;
}

static int inline_arg_needs_temp(const struct AstNode *n, int use_count)
{
    if (!inline_arg_is_body_independent(n))
        return 1;
    return use_count > 1 && !inline_arg_reusable(n);
}

static unsigned long g_inline_live_temp_mask;

/* The one local declaration the currently-expanding inline call's callee
 * captured (see struct Sym's inline_local_* fields, dcc_func.c's
 * try_scan_inline_local_decl), and the #itmpN temp name it was
 * materialized into for THIS call site - NULL/NULL when no such local is
 * active. clone_inline_expr checks these directly rather than taking them
 * as parameters (avoiding threading two more arguments through its six
 * recursive call sites), the same "scoped mutable state around one bounded
 * recursive expansion" shape g_inline_live_temp_mask/g_inline_expand_depth
 * below already use. Save/restore around a nested expansion (the local's
 * own initializer, or a call inside another inline call's argument) is
 * required for correctness: without it, an outer local's name would keep
 * shadowing an inner, unrelated identical parameter name, or an inner
 * call's cloning would wrongly see an outer local that isn't in scope for
 * it at all. */
static const char *g_inline_local_src_name;
static const char *g_inline_local_temp_name;

static int prepare_inline_arg_temps(
    const struct AstNode *n, struct Sym *fn,
    const char **temp_names,
    char temp_name_buf[MAX_PROTO_PARAMS][64])
{
    struct Sym *temp_syms[MAX_PROTO_PARAMS];
    int temp_types[MAX_PROTO_PARAMS];
    int i;
    unsigned long unavailable;
    unsigned long current_mask;

    unavailable = g_inline_live_temp_mask;
    current_mask = 0;
    for (i = 0; i < n->list_len; ++i)
        temp_syms[i] = NULL;

    /* Per-parameter, not per-call: see inline_arg_needs_temp for the two
     * independent reasons a parameter's argument needs a temp (it may have
     * a side effect that could reorder against the callee's own, or it's
     * reused and not cheap to re-evaluate) - a parameter matching neither
     * never needs a temp, regardless of what else in this call needed one,
     * and substituting it directly instead of routing it through a temp
     * preserves its compile-time-constant-ness for arithmetic in the
     * inlined body (e.g. a literal element-size argument multiplying an
     * index can still fold to a shift) - routing every parameter through a
     * temp regardless of need was silently defeating that folding for any
     * call where a completely unrelated parameter happened to need
     * protecting. */
    for (i = 0; i < n->list_len; ++i) {
        struct Sym *tmp;
        int want_type;
        int slot;

        if (!inline_arg_needs_temp(n->list[i], fn->inline_param_use_count[i]))
            continue;

        slot = i;
        if ((unavailable & (1UL << slot)) != 0) {
            for (slot = 0; slot < MAX_PROTO_PARAMS; ++slot)
                if ((unavailable & (1UL << slot)) == 0)
                    break;
            if (slot >= MAX_PROTO_PARAMS)
                return 0;
        }
        unavailable |= 1UL << slot;
        current_mask |= 1UL << slot;
        inline_temp_name_for_call(temp_name_buf[i], 64, slot);
        tmp = find_local(temp_name_buf[i]);
        if (tmp == NULL)
            return 0;
        want_type = fn->proto_types[i] ? fn->proto_types[i] : TYPE_INT;
        /* reserve_inline_temp_locals always reserves 2 bytes for every
         * #itmpN slot (it can't know the eventual parameter type - that's
         * this function's job, called far later, once per actual call
         * site), so a 1-byte want_type just leaves the slot's second byte
         * unused; emit_store_hl_to_sym_direct and the later read through
         * this same symbol both already switch on type_size(s->type)
         * themselves for every size they support, 1 included. */
        if ((type_size(want_type) != 2 && type_size(want_type) != 1) ||
            type_is_float(want_type) || type_is_long(want_type))
            return 0;
        temp_syms[i] = tmp;
        temp_types[i] = want_type;
    }

    /* Commit shared symbol metadata only after every requested slot has
     * passed validation. A failed nested expansion then falls back to a real
     * call without leaving a partially retagged #itmp symbol behind. */
    for (i = 0; i < n->list_len; ++i) {
        if (temp_syms[i] == NULL)
            continue;
        temp_syms[i]->type = temp_types[i];
        temp_names[i] = temp_name_buf[i];
        /* Keep the MIR "declared" table (dcc_mir.c's mir_note_declared_
         * symbol/mir_declared_type_is_unstable) in sync with this retag, so
         * it can tell a #itmp slot reused with a consistent type (the
         * common case, left fully optimized) apart from one reused across
         * call sites with genuinely different types - see the long comment
         * on mir_note_declared_symbol's type-change branch for why that
         * distinction matters. */
        mir_note_declared_symbol(temp_syms[i]);
    }

    /* Reserve every selected slot while arguments are evaluated. A nested
     * inline expansion then allocates from the remaining #itmp slots, so it
     * cannot overwrite either an already-materialized argument or a slot
     * this call will materialize later. All 16 slots already exist in the
     * frame; this remapping adds no runtime instructions or frame bytes. */
    g_inline_live_temp_mask |= current_mask;
    for (i = n->list_len - 1; i >= 0; --i) {
        if (temp_names[i] == NULL)
            continue;
        ast_process_expr_metadata(n->list[i]);
    }
    return 1;
}

static struct AstNode *clone_inline_expr(struct AstArena *ar, struct Sym *fn,
                                         const struct AstNode *src,
                                         const struct AstNode *call,
                                         const char **temp_names)
{
    struct AstNode *dst;
    int i;

    if (src == NULL)
        return NULL;
    if (src->kind == AST_IDENT) {
        if (g_inline_local_src_name != NULL && !strcmp(src->sval, g_inline_local_src_name)) {
            dst = ast_new(ar, AST_IDENT);
            dst->type = src->type;
            dst->sval = ast_arena_strdup(ar, g_inline_local_temp_name);
            dst->line = src->line;
            return dst;
        }
        i = inline_param_index_for_call(fn, src->sval);
        if (i >= 0 && i < call->list_len && temp_names != NULL && temp_names[i] != NULL) {
            dst = ast_new(ar, AST_IDENT);
            dst->type = src->type;
            dst->sval = ast_arena_strdup(ar, temp_names[i]);
            dst->line = src->line;
            return dst;
        }
        if (i >= 0 && i < call->list_len)
            return call->list[i];
    }

    dst = ast_new(ar, src->kind);
    dst->type = src->type;
    dst->op = src->op;
    dst->ival = src->ival;
    dst->uval = src->uval;
    dst->str_index = src->str_index;
    dst->sym = src->sym;
    if (src->sval == NULL)
        dst->sval = NULL;
    else if (src->kind == AST_STR_LIT)
        dst->sval = ast_arena_memdup(ar, src->sval, (int)src->uval);
    else
        dst->sval = ast_arena_strdup(ar, src->sval);
    dst->peek_type = src->peek_type;
    dst->line = src->line;

    dst->a = clone_inline_expr(ar, fn, src->a, call, temp_names);
    dst->b = clone_inline_expr(ar, fn, src->b, call, temp_names);
    dst->c = clone_inline_expr(ar, fn, src->c, call, temp_names);
    dst->d = clone_inline_expr(ar, fn, src->d, call, temp_names);
    for (i = 0; i < src->list_len; ++i)
        ast_list_push(ar, dst, clone_inline_expr(ar, fn, src->list[i], call, temp_names));
    return dst;
}

/* Materializes fn's captured local declaration (struct Sym's
 * inline_local_* fields, populated by dcc_func.c's
 * try_scan_inline_local_decl) into a fresh #itmpN slot for THIS call site,
 * the same way prepare_inline_arg_temps already does for a real parameter
 * that needs one - reusing the identical slot-picking/liveness-tracking
 * (g_inline_live_temp_mask) so it can't collide with a slot a real
 * parameter, or a nested inline expansion, is using at the same time.
 * Always forces a temp (never substitutes the initializer expression
 * directly at each use): the entire reason a local gets captured at all is
 * to read something more than once without recomputing it. Returns 0
 * (nothing emitted) only if out of #itmpN slots - the caller then falls
 * back to a real, non-inlined call, exactly like prepare_inline_arg_temps's
 * own failure path. temp_name_buf is caller-owned and must outlive the
 * g_inline_local_temp_name assignment the caller makes from it. */
static int prepare_inline_local_temp(
    struct Sym *fn, const struct AstNode *call,
    const char **temp_names, char temp_name_buf[64])
{
    struct AstNode *init_substituted;
    struct Sym *tmp;
    int slot;
    const char *save_src_name;
    const char *save_temp_name;

    slot = fn->proto_nargs;
    if ((g_inline_live_temp_mask & (1UL << slot)) != 0) {
        for (slot = 0; slot < MAX_PROTO_PARAMS; ++slot)
            if ((g_inline_live_temp_mask & (1UL << slot)) == 0)
                break;
        if (slot >= MAX_PROTO_PARAMS)
            return 0;
    }

    /* The local's own initializer can only reference real parameters (see
     * try_scan_inline_local_decl / inline_expr_is_simple), never the local
     * itself, so no local-name substitution is active while cloning it -
     * and it must not see whatever OUTER local (if any) is currently
     * active either. */
    save_src_name = g_inline_local_src_name;
    save_temp_name = g_inline_local_temp_name;
    g_inline_local_src_name = NULL;
    g_inline_local_temp_name = NULL;
    init_substituted = clone_inline_expr(&g_ast_arena, fn, fn->inline_local_init, call, temp_names);
    g_inline_local_src_name = save_src_name;
    g_inline_local_temp_name = save_temp_name;

    inline_temp_name_for_call(temp_name_buf, 64, slot);
    tmp = find_local(temp_name_buf);
    if (tmp == NULL)
        return 0;
    tmp->type = fn->inline_local_type;

    g_inline_live_temp_mask |= 1UL << slot;
    ast_process_expr_metadata(init_substituted);
    return 1;
}

static int g_inline_expand_depth;

int ast_process_inline_call_metadata(
    const struct AstNode *n, int result_dead)
{
    struct Sym *fn_sym;
    struct AstNode *body;
    const struct AstNode *source;
    const char *temp_names[MAX_PROTO_PARAMS];
    char temp_name_buf[MAX_PROTO_PARAMS][64];
    char local_temp_buf[64];
    unsigned long old_live_mask;
    const char *old_local_src_name;
    const char *old_local_temp_name;
    int i;

    if (n == NULL || n->kind != AST_CALL ||
        n->a == NULL || n->a->kind != AST_IDENT)
        return 0;
    fn_sym = find_global(n->a->sval);
    if (opt_debug || fn_sym == NULL || !fn_sym->is_static ||
        !fn_sym->is_inline ||
        inline_substitution_body(fn_sym) == NULL)
        return 0;
    if ((fn_sym->inline_stmt_expr != NULL ||
         fn_sym->inline_stmt_body != NULL) &&
        !result_dead)
        return 0;
    if (g_inline_expand_depth >= 8 ||
        n->list_len != fn_sym->proto_nargs ||
        n->list_len > MAX_PROTO_PARAMS)
        return 0;
    for (i = 0; i < MAX_PROTO_PARAMS; ++i)
        temp_names[i] = NULL;

    old_live_mask = g_inline_live_temp_mask;
    if (!prepare_inline_arg_temps(
            n, fn_sym, temp_names, temp_name_buf))
        return 0;

    old_local_src_name = g_inline_local_src_name;
    old_local_temp_name = g_inline_local_temp_name;
    if (fn_sym->has_inline_local) {
        if (!prepare_inline_local_temp(
                fn_sym, n, temp_names, local_temp_buf)) {
            g_inline_live_temp_mask = old_live_mask;
            return 0;
        }
        g_inline_local_src_name = fn_sym->inline_local_name;
        g_inline_local_temp_name = local_temp_buf;
    } else {
        g_inline_local_src_name = NULL;
        g_inline_local_temp_name = NULL;
    }

    ++g_inline_expand_depth;
    source = inline_substitution_body(fn_sym);
    body = clone_inline_expr(
        &g_ast_arena, fn_sym, source, n, temp_names);
    if (fn_sym->inline_stmt_body != NULL)
        ast_process_stmt_metadata(body);
    else
        ast_process_expr_metadata(body);
    --g_inline_expand_depth;

    g_inline_live_temp_mask = old_live_mask;
    g_inline_local_src_name = old_local_src_name;
    g_inline_local_temp_name = old_local_temp_name;
    return 1;
}

/* Look up a plain `BASE->FIELD` member node's value type without emitting
 * anything (base symbol's type -> struct id -> field def). Used by
 * ast_for_hoist_global_member_value_supported (dcc_ast_gen_support.c) to
 * size the value-cache temp it allocates. Only meaningful for exactly the
 * shape that predicate matches: n->a is a plain identifier. */
int ast_member_field_value_type(const struct AstNode *n)
{
    struct Sym *s;
    int sid;
    struct FieldDef *fd;

    s = find_sym(n->a->sval);
    sid = base_struct_id_from_type(s->type);
    fd = find_field_def(sid, n->sval);
    return fd->is_array ? fd->elem_type : fd->type;
}
