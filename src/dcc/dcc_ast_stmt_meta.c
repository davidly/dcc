/**
 * @file dcc_ast_stmt_meta.c
 * @brief Coordinates statement acceptance, MIR capture, and metadata analysis.
 *
 * @par Role
 * Builds and support-checks one statement, captures accepted semantics into
 * MIR, runs the non-emitting metadata walk, and tracks fallthrough/re-entry
 * behavior. It also owns scan-mode statement sizing and loop metadata plans
 * that reserve safe optimization temporaries.
 *
 * @par Key entry points
 * ast_process_statement(), ast_scan_for_stmt(), ast_stmt_exits(),
 * ast_stmt_has_reentry_label(), and ast_plan_for_metadata().
 *
 * @par Boundary
 * Statement legality classifiers live in dcc_ast_gen_cond.c; final body Z80
 * comes from a selected MIR candidate rather than this orchestration layer.
 */
#include <string.h>
#include "dcc_ast_gen_internal.h"
#include "dcc_mir.h"

static int g_ast_last_stmt_exits;


/* Rewrites a copy of `rhs` (a for-loop body's assignment right-hand side),
 * hoisting the address of any 2D array read within it whose OUTER (row)
 * subscript does not reference `ivar_name` and has no side effects.
 * tests/mm.c's matmult() inner loop is the motivating case:
 *
 *     for (k = 0; k < m; k++)
 *         C[i][j] += A[i][k] * B[k][j];
 *
 * A[i][k]'s row subscript is `i`, invariant across the k-loop, but the
 * expensive non-power-of-2 row-stride multiply (i*80) needed to form
 * A[i][k]'s address is recomputed from scratch on every one of the m
 * iterations by ordinary codegen - exactly the same waste
 * ast_for_hoist_lvalue_addr_supported's hoist eliminates for a loop-
 * invariant lhs, just one level removed (inside the rhs rather than being
 * the lhs itself). B[k][j]'s row subscript is `k` itself, so it is NOT
 * eligible: hoisting a column-only invariant there would only save the
 * already-cheap power-of-2 column scale (j*4), not the expensive row
 * multiply, so that case is deliberately left alone.
 *
 * For each qualifying 2D read found, this computes &ARR[row][0] once (via
 * gen_index_addr_ast + emit_store_hl_to_sym_direct, as a side effect of
 * this call - so this must be invoked exactly once, right before the loop
 * whose body it is rewriting), stores it into a fresh compiler-temp
 * pointer local, and replaces the read with a 1-D index through that
 * pointer using the original (unhoisted) column subscript.
 *
 * Recurses through the tree sharing any subtree that needed no rewrite, and
 * returns `rhs` itself unchanged if nothing in it qualifies - so a no-op
 * call has no side effects and allocates nothing. Only descends through
 * binary/unary operand positions (`a`/`b`), which is sufficient for the
 * `A[i][k] * B[k][j]`-shaped expressions this targets; a hoistable read
 * reachable only through some other AST shape is simply left un-hoisted,
 * which is always safe, just less thorough. */

/* True only for scalar arithmetic that contains no indirect memory read.
 * Plain identifiers are allowed: when the body assigns a plain scalar, a
 * different identifier denotes a different object. Dereferences, indexes,
 * members, calls, and unrecognized shapes are rejected because they could
 * read that scalar through an alias. */
static int ast_expr_has_only_direct_scalar_reads(const struct AstNode *n)
{
    if (n == NULL)
        return 1;
    switch (n->kind) {
    case AST_INT_LIT:
    case AST_IDENT:
        return 1;
    case AST_CAST:
        return ast_expr_has_only_direct_scalar_reads(n->a);
    case AST_UNARY:
        if (n->op == '*')
            return 0;                  /* dereference reads memory */
        return ast_expr_has_only_direct_scalar_reads(n->a);
    case AST_BINARY:
        return ast_expr_has_only_direct_scalar_reads(n->a) &&
               ast_expr_has_only_direct_scalar_reads(n->b);
    default:
        return 0;                      /* AST_INDEX/AST_MEMBER/AST_CALL/... */
    }
}

/* True only when an unknown memory store cannot change this expression.
 * Mutable locals and parameters are safe while their addresses have not
 * escaped anywhere in the function; globals are conservatively rejected. */
static int ast_expr_cannot_alias_memory_store(const struct AstNode *n)
{
    struct Sym *s;

    if (n == NULL)
        return 1;
    switch (n->kind) {
    case AST_INT_LIT:
        return 1;
    case AST_IDENT:
        s = find_sym(n->sval);
        if (s == NULL)
            return 0;
        if (s->is_const_value)
            return 1;
        return (s->storage == SC_LOCAL || s->storage == SC_PARAM) &&
               !s->is_array && !s->is_vla &&
               local_name_address_taken_in_function(s->name) == 0;
    case AST_CAST:
        return ast_expr_cannot_alias_memory_store(n->a);
    case AST_UNARY:
        return n->op != '*' && ast_expr_cannot_alias_memory_store(n->a);
    case AST_BINARY:
        return ast_expr_cannot_alias_memory_store(n->a) &&
               ast_expr_cannot_alias_memory_store(n->b);
    default:
        return 0;
    }
}

static struct AstNode *ast_hoist_row_invariant_2d_reads(const struct AstNode *rhs,
                                                        const char *ivar_name,
                                                        const char *modified_name)
{
    const struct AstNode *outer;
    const struct AstNode *row_idx;
    struct AstNode *na;
    struct AstNode *nb;
    struct AstNode *copy;
    int elem_val_type;

    if (rhs == NULL)
        return NULL;

    if (rhs->kind == AST_INDEX && ast_index_2d_addressable_addr(rhs)) {
        outer = rhs->a;
        row_idx = outer->b;
        /* The row subscript must be provably unchanged by the body store.
         * For a plain scalar target, reject both direct references to that
         * target and indirect reads that could alias it. For an unknown
         * memory target, only a compile-time-only row is independent of the
         * store without further pointer-alias analysis. */
        if (!ast_expr_references_ident(row_idx, ivar_name) &&
            (modified_name != NULL
                ? (!ast_expr_references_ident(row_idx, modified_name) &&
                   ast_expr_has_only_direct_scalar_reads(row_idx))
                : ast_expr_cannot_alias_memory_store(row_idx)) &&
            !ast_expr_has_side_effects(row_idx) &&
            ast_index_2d_array_elem_type(rhs, &elem_val_type) &&
            !type_is_struct_object(elem_val_type)) {
            struct Sym *addr_tmp;
            struct AstNode *ident;
            struct AstNode *replaced;
            char tmp_name[24];

            sprintf(tmp_name, "#licm%d", g_func_pass.licm_seq++);
            addr_tmp = add_local_alloc(tmp_name, type_add_ptr(elem_val_type), 2);

            ident = ast_new(&g_ast_arena, AST_IDENT);
            ident->sval = ast_arena_strdup(&g_ast_arena, addr_tmp->name);
            replaced = ast_new(&g_ast_arena, AST_INDEX);
            replaced->a = ident;
            replaced->b = rhs->b;
            return replaced;
        }
        return (struct AstNode *)rhs;
    }

    if (rhs->kind != AST_BINARY && rhs->kind != AST_UNARY)
        return (struct AstNode *)rhs;

    na = ast_hoist_row_invariant_2d_reads(
        rhs->a, ivar_name, modified_name);
    nb = ast_hoist_row_invariant_2d_reads(
        rhs->b, ivar_name, modified_name);
    if (na == rhs->a && nb == rhs->b)
        return (struct AstNode *)rhs;

    copy = ast_new(&g_ast_arena, rhs->kind);
    *copy = *rhs;
    copy->a = na;
    copy->b = nb;
    return copy;
}

void ast_plan_for_metadata(const struct AstNode *n)
{
    const char *ivar_name;
    const struct AstNode *lhs;
    const struct AstNode *member;
    const struct AstNode *rhs;
    const char *modified_name = NULL;
    struct AstNode *rewritten;
    int value_type;
    int planned = 0;
    char temp_name[24];

    if (n == NULL || n->kind != AST_FOR || n->sym != NULL)
        return;

    if (ast_for_hoist_lvalue_addr_supported(
            n, &ivar_name, &lhs, &value_type)) {
        sprintf(temp_name, "#licm%d", g_func_pass.licm_seq++);
        (void)add_local_alloc(
            temp_name, type_add_ptr(value_type), 2);
        planned = 1;
    } else if (ast_for_hoist_global_member_value_supported(
                   n, &member, &value_type)) {
        sprintf(temp_name, "#gmv%d", g_func_pass.licm_seq++);
        (void)add_local_alloc(temp_name, value_type, 2);
        planned = 1;
    }

    if (ast_for_rhs_hoist_scan_supported(n, &ivar_name, &rhs)) {
        const struct AstNode *body_assign =
            n->d != NULL && n->d->kind == AST_EXPR_STMT
                ? n->d->a : NULL;

        if (body_assign != NULL &&
            body_assign->kind == AST_ASSIGN &&
            body_assign->a != NULL &&
            body_assign->a->kind == AST_IDENT)
            modified_name = body_assign->a->sval;
        rewritten = ast_hoist_row_invariant_2d_reads(
            rhs, ivar_name, modified_name);
        if (rewritten != rhs)
            planned = 1;
    }

    if (!planned)
        (void)ast_licm_plan_invariants(n);
}

/* Build and size one nested statement without producing target code. */
int ast_scan_for_stmt(void)
{
    int s_scan_mode;
    struct AstNode *n;

    s_scan_mode = scan_mode;
    scan_mode = 1;

    n = ast_build_stmt(&g_ast_arena);
    if (n != NULL) {
        ast_support_cache_begin();
        if (ast_stmt_supported(n))
            ast_process_stmt_metadata(n);
    }

    ast_arena_reset(&g_ast_arena);
    scan_mode = s_scan_mode;
    return n != NULL;
}

int ast_stmt_has_reentry_label(const struct AstNode *n)
{
    int i;

    if (n == NULL)
        return 0;
    if (n->kind == AST_LABEL || n->kind == AST_CASE || n->kind == AST_DEFAULT)
        return 1;
    if (n->kind == AST_COMPOUND) {
        for (i = 0; i < n->list_len; ++i)
            if (ast_stmt_has_reentry_label(n->list[i]))
                return 1;
        return 0;
    }
    if (n->kind == AST_IF)
        return ast_stmt_has_reentry_label(n->b) || ast_stmt_has_reentry_label(n->c);
    if (n->kind == AST_WHILE || n->kind == AST_DOWHILE)
        return ast_stmt_has_reentry_label(n->b);
    if (n->kind == AST_FOR)
        return ast_stmt_has_reentry_label(n->d);
    if (n->kind == AST_SWITCH)
        return ast_stmt_has_reentry_label(n->b);
    return 0;
}

/* Is there a `break;` directly inside `n` that would target an enclosing
 * switch - i.e. not one belonging to a nested loop or switch, which has its
 * own break target? Used by ast_stmt_exits's AST_SWITCH case: a switch with
 * a default case and no such break can only fall through to whatever
 * statement is textually last in its body (nothing escapes early), so its
 * own exit behavior reduces to that last statement's. */
static int ast_stmt_has_direct_break(const struct AstNode *n)
{
    int i;

    if (n == NULL)
        return 0;
    if (n->kind == AST_BREAK)
        return 1;
    if (n->kind == AST_WHILE || n->kind == AST_FOR || n->kind == AST_DOWHILE ||
        n->kind == AST_SWITCH)
        return 0;                          /* own break scope */
    if (n->kind == AST_IF)
        return ast_stmt_has_direct_break(n->b) || ast_stmt_has_direct_break(n->c);
    if (n->kind == AST_LABEL || n->kind == AST_CASE || n->kind == AST_DEFAULT)
        return ast_stmt_has_direct_break(n->b);
    if (n->kind == AST_COMPOUND) {
        for (i = 0; i < n->list_len; ++i)
            if (ast_stmt_has_direct_break(n->list[i]))
                return 1;
        return 0;
    }
    return 0;
}

/* Collect the names of every label DEFINED anywhere within `n` into
 * labels[] (bounded by cap). Used to tell an intra-loop goto (whose target
 * label lives inside the loop) from one that jumps clear out of it. */
static void ast_collect_defined_labels(const struct AstNode *n,
                                       const char **labels, int *nlabels, int cap)
{
    int i;

    if (n == NULL)
        return;
    if (n->kind == AST_LABEL && n->sval[0] != 0 && *nlabels < cap)
        labels[(*nlabels)++] = n->sval;
    ast_collect_defined_labels(n->a, labels, nlabels, cap);
    ast_collect_defined_labels(n->b, labels, nlabels, cap);
    ast_collect_defined_labels(n->c, labels, nlabels, cap);
    ast_collect_defined_labels(n->d, labels, nlabels, cap);
    for (i = 0; i < n->list_len; ++i)
        ast_collect_defined_labels(n->list[i], labels, nlabels, cap);
}

/* Is there a `goto` within `n` whose target label is NOT among labels[] -
 * i.e. one that escapes the subtree those labels were collected from? */
static int ast_goto_escapes(const struct AstNode *n,
                            const char **labels, int nlabels)
{
    int i;
    int j;

    if (n == NULL)
        return 0;
    if (n->kind == AST_GOTO && n->sval[0] != 0) {
        for (j = 0; j < nlabels; ++j)
            if (strcmp(labels[j], n->sval) == 0)
                break;
        if (j == nlabels)
            return 1;
    }
    if (ast_goto_escapes(n->a, labels, nlabels) ||
        ast_goto_escapes(n->b, labels, nlabels) ||
        ast_goto_escapes(n->c, labels, nlabels) ||
        ast_goto_escapes(n->d, labels, nlabels))
        return 1;
    for (i = 0; i < n->list_len; ++i)
        if (ast_goto_escapes(n->list[i], labels, nlabels))
            return 1;
    return 0;
}

/* A loop body with a `goto` that jumps to a label defined outside the loop
 * can exit the loop that way, just like a `break` - so an otherwise
 * "infinite" loop containing one is NOT guaranteed to never fall through. A
 * goto to a label defined inside the body stays in the loop and is ignored. */
static int ast_stmt_has_escaping_goto(const struct AstNode *body)
{
    const char *labels[MAX_USER_LABELS];
    int nlabels = 0;

    ast_collect_defined_labels(body, labels, &nlabels, MAX_USER_LABELS);
    return ast_goto_escapes(body, labels, nlabels);
}

int ast_stmt_exits(const struct AstNode *n)
{
    int i;
    int exits;

    if (n == NULL)
        return 0;
    switch (n->kind) {
    case AST_RETURN:
    case AST_BREAK:
    case AST_CONTINUE:
    case AST_GOTO:
        return 1;
    case AST_SWITCH: {
        /* Exits only when every path through the body is forced to reach a
         * point that itself exits: a default case must exist (otherwise an
         * unmatched value skips the whole body), and nothing may `break`
         * out early - given both, control can only fall through case labels
         * until it reaches whatever's textually last, so that determines
         * the switch's own exit behavior, exactly like AST_COMPOUND. */
        const struct AstNode *body = n->b;
        const struct AstNode *peel;
        int has_default = 0;

        if (body == NULL || body->kind != AST_COMPOUND || body->list_len == 0)
            return 0;
        for (i = 0; i < body->list_len; ++i) {
            peel = body->list[i];
            while (peel != NULL &&
                   (peel->kind == AST_CASE || peel->kind == AST_DEFAULT || peel->kind == AST_LABEL)) {
                if (peel->kind == AST_DEFAULT)
                    has_default = 1;
                peel = peel->b;
            }
        }
        if (!has_default || ast_stmt_has_direct_break(body))
            return 0;
        return ast_stmt_exits(body->list[body->list_len - 1]);
    }
    case AST_LABEL:
    case AST_CASE:
    case AST_DEFAULT:
        return ast_stmt_exits(n->b);
    case AST_IF:
        if (ast_is_const_nonzero_condition(n->a))
            return ast_stmt_exits(n->b);
        if (ast_is_const_zero_condition(n->a))
            return n->c != NULL && ast_stmt_exits(n->c);
        return n->c != NULL && ast_stmt_exits(n->b) && ast_stmt_exits(n->c);
    case AST_COMPOUND:
        exits = 0;
        for (i = 0; i < n->list_len; ++i) {
            if (exits && !ast_stmt_has_reentry_label(n->list[i]))
                continue;
            exits = ast_stmt_exits(n->list[i]);
        }
        return exits;
    case AST_WHILE:
    case AST_DOWHILE:
        /* An infinite loop (constant nonzero condition) exits unless a
         * `break` directly inside its body can escape it - e.g.
         * `for (;;) { ...; if (done) return x; }` never falls through, but
         * `while (1) { if (x) break; }` does, right past the loop. A `goto`
         * to a label outside the loop escapes it the same way as a break. */
        return ast_is_const_nonzero_condition(n->a) &&
               !ast_stmt_has_direct_break(n->b) &&
               !ast_stmt_has_escaping_goto(n->b);
    case AST_FOR:
        /* An empty condition (`for (;;)`) is `true` by C's own rules - not
         * caught by ast_is_const_nonzero_condition, which treats a NULL
         * node as "not a constant" rather than "absent = true". */
        return (n->b == NULL || ast_is_const_nonzero_condition(n->b)) &&
               !ast_stmt_has_direct_break(n->d) &&
               !ast_stmt_has_escaping_goto(n->d);
    default:
        return 0;
    }
}

int ast_last_statement_exits(void)
{
    return g_ast_last_stmt_exits;
}

void ast_record_debug_location(const char *file, int line)
{
    const char *p;

    if (!DEBUG_METADATA_ENABLED || scan_mode || line <= 0)
        return;
    if (mir_capture_debug_location(file, line))
        return;

    fputs(";@dcc-line \"", g_emit_sink.stream);
    p = file ? file : (input_name ? input_name : "<input>");
    while (*p) {
        if (*p == '\\' || *p == '"')
            fputc('\\', g_emit_sink.stream);
        fputc(*p++, g_emit_sink.stream);
    }
    fprintf(g_emit_sink.stream, "\" %d\n", line);
}

static const char *ast_unsupported_statement_message(const struct AstNode *n)
{
    if (n == NULL)
        return "malformed statement";

    switch (n->kind) {
    case AST_BREAK:
        return "break statement outside loop or switch";
    case AST_CONTINUE:
        return "continue statement outside loop";
    case AST_CASE:
        return "case label outside switch";
    case AST_DEFAULT:
        return "default label outside switch";
    case AST_RETURN:
        return "unsupported return expression";
    case AST_EXPR_STMT:
        return "unsupported expression statement";
    case AST_IF:
        return "unsupported if condition or branch";
    case AST_WHILE:
        return "unsupported while condition or body";
    case AST_DOWHILE:
        return "unsupported do-while condition or body";
    case AST_FOR:
        return "unsupported for statement";
    case AST_SWITCH:
        return "unsupported switch expression or body";
    default:
        return "unsupported AST statement";
    }
}

int ast_process_statement(void)
{
    LexState _ls;
    LexState _le;
    int sv_for_seq;
    int sv_block_seq;
    struct AstNode *n;
    int report;

    g_ast_last_stmt_exits = 0;

    if (scan_mode)
        return 0;

    report = getenv("DCC_AST_REPORT") != NULL;

    _ls = lex_save();
    sv_for_seq = g_func_pass.for_seq;
    sv_block_seq = g_func_pass.block_seq;

    n = ast_build_stmt(&g_ast_arena);

    _le = lex_save();

    if (n != NULL)
        ast_support_cache_begin();

    if (n != NULL && ast_stmt_supported(n)) {
        g_func_pass.for_seq = sv_for_seq;
        g_func_pass.block_seq = sv_block_seq;
        if (g_ast_build_enabled == 2)
            ast_dump(n, 0);
        mir_capture_stmt(n);
        ast_process_stmt_metadata(n);
        g_ast_last_stmt_exits = ast_stmt_exits(n);
        ast_arena_reset(&g_ast_arena);
        return 1;
    }

    if (report) {
        if (n == NULL) {
                fprintf(stderr, "; AST-unsupported stmt build token=%d text='%s' line=%d\n",
                    _ls.tok.kind, _ls.tok.text, _ls.tok_line);
        } else {
            fprintf(stderr, "; AST-unsupported stmt gate kind=%s line=%d\n",
                    ast_kind_name(n->kind), _ls.tok_line);
        }
    }

    lex_restore(&_ls);
    error_here(ast_unsupported_statement_message(n));

    if (n != NULL) {
        lex_restore(&_le);
    } else {
        int paren_depth = 0;
        int bracket_depth = 0;
        int brace_depth = 0;

        while (g_lex.tok.kind != TOK_EOF) {
            if (g_lex.tok.kind == ';' && paren_depth == 0 && bracket_depth == 0 && brace_depth == 0) {
                next_token();
                break;
            }
            if (g_lex.tok.kind == '}' && paren_depth == 0 && bracket_depth == 0 && brace_depth == 0)
                break;
            if (g_lex.tok.kind == '(')
                paren_depth++;
            else if (g_lex.tok.kind == ')' && paren_depth > 0)
                paren_depth--;
            else if (g_lex.tok.kind == '[')
                bracket_depth++;
            else if (g_lex.tok.kind == ']' && bracket_depth > 0)
                bracket_depth--;
            else if (g_lex.tok.kind == '{')
                brace_depth++;
            else if (g_lex.tok.kind == '}' && brace_depth > 0)
                brace_depth--;
            next_token();
        }
    }

    g_func_pass.for_seq = sv_for_seq;
    g_func_pass.block_seq = sv_block_seq;
    ast_arena_reset(&g_ast_arena);
    return 1;
}
