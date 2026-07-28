/*
 * dcc_ast_gen_stmt.c - switch/for/statement emitters, ast_try_emit_statement.
 *
 * Split from dcc_ast_gen.c; part of the AST codegen module.  Shared
 * prototypes live in dcc_ast_gen_internal.h.
 */
#include <string.h>
#include "dcc_ast_gen_internal.h"


/* Emit `arr[E] = lo; arr[E+1] = hi;` (mem_set_word-shaped code, matched by
 * ast_byte_pair_word_write_match) sharing one address computation instead
 * of two: HL = &arr[E] once, store the low byte, `inc hl`, store the high
 * byte. See ast_byte_pair_word_read_match's read-side counterpart
 * (dcc_ast_gen_expr.c) for the same idiom on the read path. */
static void gen_byte_pair_word_write_ast(const struct AstNode *lo_index,
                                         const struct AstNode *s1_assign,
                                         const struct AstNode *s2_assign)
{
    int val_type;
    int saved_dead;

    gen_index_addr_ast(lo_index, &val_type);   /* HL = &arr[E] */
    emit("\tpush hl\n");
    saved_dead = expr_result_dead;
    expr_result_dead = 1;
    ast_gen_expr(s1_assign->b);                /* HL = low-byte value */
    expr_result_dead = saved_dead;
    emit("\tex de,hl\n\tpop hl\n\tld (hl),e\n\tinc hl\n");
    emit("\tpush hl\n");
    saved_dead = expr_result_dead;
    expr_result_dead = 1;
    ast_gen_expr(s2_assign->b);                /* HL = high-byte value */
    expr_result_dead = saved_dead;
    emit("\tex de,hl\n\tpop hl\n\tld (hl),e\n");
}

void ast_switch_collect_stmt(const struct AstNode *n, int *case_vals,
                                    int *ncasep, int *have_defaultp)
{
    int i;

    if (n == NULL || n->kind == AST_SWITCH)
        return;
    if (n->kind == AST_CASE) {
        case_vals[(*ncasep)++] = (int)n->ival;
        ast_switch_collect_stmt(n->b, case_vals, ncasep, have_defaultp);
        return;
    }
    if (n->kind == AST_DEFAULT) {
        *have_defaultp = 1;
        ast_switch_collect_stmt(n->b, case_vals, ncasep, have_defaultp);
        return;
    }
    if (n->kind == AST_COMPOUND) {
        for (i = 0; i < n->list_len; ++i)
            ast_switch_collect_stmt(n->list[i], case_vals, ncasep, have_defaultp);
        return;
    }
    if (n->kind == AST_IF) {
        ast_switch_collect_stmt(n->b, case_vals, ncasep, have_defaultp);
        ast_switch_collect_stmt(n->c, case_vals, ncasep, have_defaultp);
        return;
    }
    if (n->kind == AST_WHILE || n->kind == AST_DOWHILE) {
        ast_switch_collect_stmt(n->b, case_vals, ncasep, have_defaultp);
        return;
    }
    if (n->kind == AST_FOR) {
        ast_switch_collect_stmt(n->d, case_vals, ncasep, have_defaultp);
        return;
    }
    if (n->kind == AST_LABEL)
        ast_switch_collect_stmt(n->b, case_vals, ncasep, have_defaultp);
}

void ast_switch_collect(const struct AstNode *n, int *case_vals,
                               int *ncasep, int *have_defaultp)
{
    *ncasep = 0;
    *have_defaultp = 0;
    ast_switch_collect_stmt(n->b, case_vals, ncasep, have_defaultp);
}

void ast_switch_consume_scan_labels_stmt(const struct AstNode *n)
{
    int i;

    if (n == NULL || n->kind == AST_SWITCH)
        return;
    if (n->kind == AST_CASE || n->kind == AST_DEFAULT) {
        (void)new_label();
        ast_switch_consume_scan_labels_stmt(n->b);
        return;
    }
    if (n->kind == AST_COMPOUND) {
        for (i = 0; i < n->list_len; ++i)
            ast_switch_consume_scan_labels_stmt(n->list[i]);
        return;
    }
    if (n->kind == AST_IF) {
        ast_switch_consume_scan_labels_stmt(n->b);
        ast_switch_consume_scan_labels_stmt(n->c);
        return;
    }
    if (n->kind == AST_WHILE || n->kind == AST_DOWHILE) {
        ast_switch_consume_scan_labels_stmt(n->b);
        return;
    }
    if (n->kind == AST_FOR) {
        ast_switch_consume_scan_labels_stmt(n->d);
        return;
    }
    if (n->kind == AST_LABEL)
        ast_switch_consume_scan_labels_stmt(n->b);
}

void ast_switch_consume_scan_labels(const struct AstNode *n)
{
    ast_switch_consume_scan_labels_stmt(n->b);
}

void ast_switch_assign_labels_stmt(const struct AstNode *n, int *case_vals,
                                          int *case_labs, int ncase,
                                          int *default_labp)
{
    int i;

    if (n == NULL || n->kind == AST_SWITCH)
        return;
    if (n->kind == AST_CASE) {
        i = ast_switch_find_case((int)n->ival, case_vals, ncase);
        if (i >= 0)
            case_labs[i] = new_label();
        ast_switch_assign_labels_stmt(n->b, case_vals, case_labs, ncase, default_labp);
        return;
    }
    if (n->kind == AST_DEFAULT) {
        *default_labp = new_label();
        ast_switch_assign_labels_stmt(n->b, case_vals, case_labs, ncase, default_labp);
        return;
    }
    if (n->kind == AST_COMPOUND) {
        for (i = 0; i < n->list_len; ++i)
            ast_switch_assign_labels_stmt(n->list[i], case_vals, case_labs,
                                          ncase, default_labp);
        return;
    }
    if (n->kind == AST_IF) {
        ast_switch_assign_labels_stmt(n->b, case_vals, case_labs, ncase, default_labp);
        ast_switch_assign_labels_stmt(n->c, case_vals, case_labs, ncase, default_labp);
        return;
    }
    if (n->kind == AST_WHILE || n->kind == AST_DOWHILE) {
        ast_switch_assign_labels_stmt(n->b, case_vals, case_labs, ncase, default_labp);
        return;
    }
    if (n->kind == AST_FOR) {
        ast_switch_assign_labels_stmt(n->d, case_vals, case_labs, ncase, default_labp);
        return;
    }
    if (n->kind == AST_LABEL)
        ast_switch_assign_labels_stmt(n->b, case_vals, case_labs, ncase, default_labp);
}

void ast_switch_assign_labels(const struct AstNode *n, int *case_vals,
                                     int *case_labs, int ncase,
                                     int *default_labp)
{
    ast_switch_assign_labels_stmt(n->b, case_vals, case_labs, ncase, default_labp);
}

void ast_gen_switch_stmt(const struct AstNode *n)
{
    int case_vals[MAX_SWITCH_CASES];
    int case_labs[MAX_SWITCH_CASES];
    int ncase;
    int have_default;
    int default_lab;
    int minv;
    int maxv;
    int table_ok;
    int lend;
    int i;

    ast_switch_collect(n, case_vals, &ncase, &have_default);
    table_ok = ast_switch_table_ok(case_vals, ncase, &minv, &maxv);

    /* Preserve the historical label allocation order: consume scan labels for
     * every top-level case/default before expression code can allocate labels.
     * The scan allocates in source order, so `default:` before later cases
     * matters. */
    ast_switch_consume_scan_labels(n);

    default_lab = -1;
    if (!table_ok)
        ast_switch_assign_labels(n, case_vals, case_labs, ncase, &default_lab);

    ast_gen_expr(n->a);

    if (table_ok)
        ast_switch_assign_labels(n, case_vals, case_labs, ncase, &default_lab);

    lend = new_label();
    if (table_ok) {
        emit_switch_jump_table(minv, maxv, case_vals, case_labs, ncase,
                               default_lab, lend);
    } else {
        emit("\tex de,hl\n");
        for (i = 0; i < ncase; ++i) {
            fprintf(g_emit_sink.stream, "\tld hl,%ld\n", (long)(case_vals[i] & 0xffff));
            emit("\tor a\n\tsbc hl,de\n");
            emit_jp_label("jp z,", case_labs[i]);
        }
        emit_jp_label("jp", default_lab >= 0 ? default_lab : lend);
    }

    enter_scope();
    break_stack[nflow] = lend;
    cont_stack[nflow] = (nflow > 0) ? cont_stack[nflow - 1] : lend;
    flow_scope_depth[nflow] = g_func_pass.scope_depth;
    nflow++;

    if (ast_sw_depth < AST_MAX_SW_NEST) {
        ast_sw_ctx[ast_sw_depth].vals    = case_vals;
        ast_sw_ctx[ast_sw_depth].labs    = case_labs;
        ast_sw_ctx[ast_sw_depth].n       = ncase;
        ast_sw_ctx[ast_sw_depth].def_lab = default_lab;
        ast_sw_depth++;
    }

    ast_gen_stmt(n->b);

    if (ast_sw_depth > 0)
        ast_sw_depth--;
    nflow--;
    leave_scope();
    emit_label(lend);
}

/* Is `n`'s for-header provably certain to run its first iteration, so the
 * upfront entry test can be skipped and the condition checked only at the
 * bottom (do-while style, one fewer branch per call than a top-tested loop
 * pays across the whole run)? Restricted to the simple, common shape this can
 * be decided for purely from literal values, with no evaluation: a plain
 * (non-C99-declaration) `var = CONST` init, whose SAME variable is compared
 * against a CONST bound in the condition with a plain relational operator.
 * Both operand orders (`var OP const` and `const OP var`) are accepted. Since
 * both init and cond are parsed in the for-header's own scope, matching by
 * identifier text is sound - there is no way for the init and cond names to
 * resolve to different symbols here. Declines (returns 0) for anything else,
 * which is always safe: the caller falls back to the ordinary top-tested
 * loop. This only decides whether the entry test is skippable; it does not
 * change the loop's shape, body, or exit condition in any other way. */
static int ast_for_first_iter_certain(const struct AstNode *n)
{
    const struct AstNode *cond;
    const char *varname;
    long init_val;
    long bound_val;
    int op;

    if (n->a == NULL || n->a->kind != AST_ASSIGN || n->a->op != '=')
        return 0;
    if (n->a->a == NULL || n->a->a->kind != AST_IDENT)
        return 0;
    if (n->a->b == NULL || n->a->b->kind != AST_INT_LIT)
        return 0;
    varname = n->a->a->sval;
    init_val = n->a->b->ival;

    cond = n->b;
    if (cond == NULL || cond->kind != AST_BINARY || !is_cmp_op(cond->op))
        return 0;

    if (cond->a != NULL && cond->a->kind == AST_IDENT &&
        !strcmp(cond->a->sval, varname) &&
        cond->b != NULL && cond->b->kind == AST_INT_LIT) {
        op = cond->op;
        bound_val = cond->b->ival;
    } else if (cond->b != NULL && cond->b->kind == AST_IDENT &&
               !strcmp(cond->b->sval, varname) &&
               cond->a != NULL && cond->a->kind == AST_INT_LIT) {
        op = invert_relop_for_swap(cond->op);
        bound_val = cond->a->ival;
    } else {
        return 0;
    }

    switch (op) {
    case '<':    return init_val < bound_val;
    case TOK_LE: return init_val <= bound_val;
    case '>':    return init_val > bound_val;
    case TOK_GE: return init_val >= bound_val;
    case TOK_EQ: return init_val == bound_val;
    case TOK_NE: return init_val != bound_val;
    default:     return 0;
    }
}

void ast_gen_dead_expr(const struct AstNode *n)
{
    if (n->kind == AST_COMMA) {
        ast_gen_dead_expr(n->a);
        ast_gen_dead_expr(n->b);
    } else if (n->kind == AST_CAST && (n->type & 15) == TYPE_VOID) {
        ast_gen_dead_expr(n->a);
    } else if ((n->kind == AST_UNARY || n->kind == AST_POSTFIX) &&
               (n->op == TOK_INC || n->op == TOK_DEC)) {
        struct Sym *s = ast_deadincdec_sym_direct(n);
        if (s != NULL) {
            emit_incdec_sym_direct(s, n->op);
        } else {
            int vt;
            gen_deadincdec_addr_lvalue_ast(n, &vt);
            if (current_field_bit_width > 0)
                emit_pre_incdec_lvalue(vt, n->op);
            else
                emit_incdec_addr(vt, n->op);
        }
    } else if (ast_is_local_self_add_stmt(n)) {
        ast_emit_local_self_add_stmt(n);
    } else if (n->kind == AST_IDENT) {
        /* A dead bare-identifier read (most commonly `(void)param;` to
         * silence an unused-variable warning) has no observable effect and
         * can be skipped entirely, UNLESS the object is volatile - a
         * volatile read's side effect (the memory access itself) must
         * still happen even though its value is discarded. */
        struct Sym *s = find_sym(n->sval);
        if (s == NULL || s->is_volatile)
            ast_gen_expr(n);
    } else {
        ast_gen_expr(n);
    }
}

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
            struct AstNode *zero_lit;
            struct AstNode *row_addr;
            struct AstNode *ident;
            struct AstNode *replaced;
            char tmp_name[24];
            int addr_val_type;

            zero_lit = ast_new(&g_ast_arena, AST_INT_LIT);
            zero_lit->ival = 0;
            zero_lit->type = TYPE_INT;
            row_addr = ast_new(&g_ast_arena, AST_INDEX);
            row_addr->a = (struct AstNode *)outer;
            row_addr->b = zero_lit;

            sprintf(tmp_name, "#licm%d", g_func_pass.licm_seq++);
            addr_tmp = add_local_alloc(tmp_name, type_add_ptr(elem_val_type), 2);
            gen_index_addr_ast(row_addr, &addr_val_type);  /* HL = &ARR[row][0], computed once */
            emit_store_hl_to_sym_direct(addr_tmp);

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

    na = ast_hoist_row_invariant_2d_reads(rhs->a, ivar_name, modified_name);
    nb = ast_hoist_row_invariant_2d_reads(rhs->b, ivar_name, modified_name);
    if (na == rhs->a && nb == rhs->b)
        return (struct AstNode *)rhs;

    copy = ast_new(&g_ast_arena, rhs->kind);
    *copy = *rhs;
    copy->a = na;
    copy->b = nb;
    return copy;
}

/* Renamed original body of ast_gen_for_stmt - see that function (just below)
 * for the loop-scoped BC register-promotion wrapper now placed around it. */
static void ast_gen_for_stmt_impl(const struct AstNode *n)
{
    int ltop;
    int linc;
    int lend;
    int for_seq;
    int rename_count;
    int rotate;
    const char *hoist_ivar_name;
    const struct AstNode *hoist_lhs;
    int hoist_val_type;
    struct AstNode *hoist_body;

    for_seq = g_func_pass.for_seq++;
    if (for_seq >= MAX_FOR_SCOPES)
        fatal("too many for statements");
    /* g_for_rename_count[] is indexed by for_seq, which is reused across
     * functions and across the (up to three) passes over one function's
     * body (frame-sizing scan x2, then the real pass) - g_func_pass.for_seq itself
     * resets to 0 for each pass, but this array does not reset on its own.
     * dcc_func.c's old hand-written frame-sizing scanner used to zero this
     * slot unconditionally for every for-loop it walked past, decl-init or
     * not, before that scanner was replaced by ast_scan_for_stmt driving
     * this same AST builder/emitter. Match that here: reset before deciding
     * which branch below applies, so a plain expression init reliably sees
     * "no renames" instead of whatever a decl-init loop that previously
     * owned this for_seq slot left behind. */
    g_for_rename_count[for_seq] = 0;
    rename_count = g_for_rename_count[for_seq];

    ltop = new_label();
    linc = new_label();
    lend = new_label();

    if (n->a != NULL && n->a->kind == AST_DECL) {
        /* C99 for-init declaration: drive declaration codegen through the
         * captured span with the for-scope rename context set, so the loop
         * variable is renamed to its unique internal slot and pushed onto
         * the active rename stack for the body/cond/inc to resolve.
         *
         * g_for_rename_count[for_seq] used to be populated ahead of time by
         * dcc_func.c's old hand-written frame-sizing scanner, which recorded
         * each for-init declarator's rename via g_func_pass.for_decl_recording before
         * any AST codegen ever ran for this loop. That scanner is gone, so
         * nothing else ever sets g_func_pass.for_decl_recording=1 any more - relying
         * on a pre-existing count here would always see 0 (just reset above)
         * and fail every C99 for-init loop. Record fresh instead: this same
         * call self-records via g_func_pass.for_decl_recording=1, so each invocation
         * (both scan-time replays and the real pass) is self-contained and
         * does not depend on a prior pass having run. */
        int old_for_decl_seq = g_func_pass.for_decl_seq;
        int old_for_decl_rename_index = g_func_pass.for_decl_rename_index;
        int old_for_decl_recording = g_func_pass.for_decl_recording;
        g_func_pass.for_decl_seq = for_seq;
        g_func_pass.for_decl_rename_index = 0;
        g_func_pass.for_decl_recording = 1;
        ast_emit_decl_span(n->a);
        rename_count = g_func_pass.for_decl_rename_index;
        g_func_pass.for_decl_seq = old_for_decl_seq;
        g_func_pass.for_decl_rename_index = old_for_decl_rename_index;
        g_func_pass.for_decl_recording = old_for_decl_recording;
    } else {
        if (rename_count != 0)
            fatal("unsupported AST for-init scope");
        if (n->a != NULL) {
            /* The init expression's value is never used - match the dead-result
             * context ast_for_init_expr_supported gates under (see that function),
             * so an out-of-ix-range long/float scalar init routes to the same
             * address-computed store fast path the gate already approved. */
            int saved_dead = expr_result_dead;
            expr_result_dead = 1;
            ast_gen_dead_expr(n->a);
            expr_result_dead = saved_dead;
        }
    }

    if (n->sym != NULL) {
        long init_val, base_val, mod_val;
        ast_for_mod_fill_supported(n, NULL, &init_val, &base_val, &mod_val, NULL);
        fprintf(g_emit_sink.stream, "\tld a,%ld\n", base_val + (init_val % mod_val));
        fprintf(g_emit_sink.stream, "\tld (ix%+d),a\n", n->sym->offset);
    }

    rotate = ast_for_first_iter_certain(n);

    /* Loop-invariant lvalue address hoist (see ast_for_hoist_lvalue_addr_
     * supported for the full shape/rationale): if the whole body is one
     * assignment to ARR[...] whose address never depends on this loop's
     * induction variable, compute that address once here, before the loop,
     * into a fresh compiler-temp pointer local - then rewrite the body (only
     * for codegen purposes; n->d itself is untouched) to store/combine
     * through *that pointer instead of recomputing ARR[...]'s address on
     * every iteration. Declines (hoist_body stays NULL) for anything not
     * matching that exact narrow shape, in which case the loop runs exactly
     * as before. */
    hoist_body = NULL;
    if (n->sym == NULL &&
        ast_for_hoist_lvalue_addr_supported(n, &hoist_ivar_name, &hoist_lhs, &hoist_val_type)) {
        struct Sym *addr_tmp;
        struct AstNode *ident;
        struct AstNode *deref;
        struct AstNode *assign;
        char tmp_name[24];
        int addr_val_type;

        sprintf(tmp_name, "#licm%d", g_func_pass.licm_seq++);
        addr_tmp = add_local_alloc(tmp_name, type_add_ptr(hoist_val_type), 2);

        gen_index_addr_ast(hoist_lhs, &addr_val_type);  /* HL = the hoisted address, computed once */
        emit_store_hl_to_sym_direct(addr_tmp);

        ident = ast_new(&g_ast_arena, AST_IDENT);
        ident->sval = ast_arena_strdup(&g_ast_arena, addr_tmp->name);
        deref = ast_new(&g_ast_arena, AST_UNARY);
        deref->op = '*';
        deref->a = ident;
        assign = ast_new(&g_ast_arena, AST_ASSIGN);
        assign->op = n->d->a->op;
        assign->a = deref;
        assign->b = n->d->a->b;
        hoist_body = ast_new(&g_ast_arena, AST_EXPR_STMT);
        hoist_body->a = assign;
        (void)hoist_ivar_name;
    } else if (n->sym == NULL) {
        const struct AstNode *gmv_member;
        int gmv_val_type;

        /* Loop-invariant global-member VALUE hoist (see
         * ast_for_hoist_global_member_value_supported for the full shape/
         * rationale/soundness argument): if the loop body's first statement
         * reads BASE->FIELD's value - provably invariant for this whole
         * function's execution via the whole-file write scan, even though
         * the rest of the body is full of calls - cache that value once
         * here, before the loop, into a fresh compiler-temp local, and
         * rewrite ONLY the first statement (n->d itself is untouched;
         * nothing else in the body is inspected or rewritten) to read
         * through that temp instead of re-fetching BASE->FIELD on every
         * iteration. */
        if (ast_for_hoist_global_member_value_supported(n, &gmv_member, &gmv_val_type)) {
            struct Sym *val_tmp;
            struct AstNode *ident;
            struct AstNode *new_index;
            struct AstNode *new_addrof;
            struct AstNode *new_assign;
            struct AstNode *new_body;
            const struct AstNode *stmt0 = n->d->list[0];
            const struct AstNode *orig_index = stmt0->a->b->a;
            char tmp_name[24];
            int i;

            sprintf(tmp_name, "#gmv%d", g_func_pass.licm_seq++);
            val_tmp = add_local_alloc(tmp_name, gmv_val_type, 2);

            gen_member_ast(gmv_member);  /* HL = BASE->FIELD's value, computed once */
            emit_store_hl_to_sym_direct(val_tmp);

            ident = ast_new(&g_ast_arena, AST_IDENT);
            ident->sval = ast_arena_strdup(&g_ast_arena, val_tmp->name);

            new_index = ast_new(&g_ast_arena, AST_INDEX);
            new_index->a = ident;
            new_index->b = orig_index->b;

            new_addrof = ast_new(&g_ast_arena, AST_UNARY);
            new_addrof->op = '&';
            new_addrof->a = new_index;

            new_assign = ast_new(&g_ast_arena, AST_ASSIGN);
            new_assign->op = '=';
            new_assign->a = stmt0->a->a;
            new_assign->b = new_addrof;

            new_body = ast_new(&g_ast_arena, AST_COMPOUND);
            new_body->list = (struct AstNode **)ast_arena_alloc(&g_ast_arena,
                sizeof(struct AstNode *) * (size_t)n->d->list_len);
            new_body->list_len = n->d->list_len;
            new_body->list_cap = n->d->list_len;
            new_body->list[0] = ast_new(&g_ast_arena, AST_EXPR_STMT);
            new_body->list[0]->a = new_assign;
            for (i = 1; i < n->d->list_len; ++i)
                new_body->list[i] = n->d->list[i];

            hoist_body = new_body;
        }
    }

    /* Loop-invariant rhs 2D-array-read row hoist (see
     * ast_hoist_row_invariant_2d_reads above for the full shape/rationale).
     * Runs independently of the lhs hoist above: fires whether or not
     * hoist_body is already set, either editing that hoist_body's assign in
     * place (when the lhs hoist above already built one, as in mm.c's
     * matmult(), where both this and the lhs hoist apply to the same
     * statement) or building a fresh hoist_body of its own (when the lhs
     * hoist declined, e.g. because the lhs itself depends on the induction
     * variable). Declines (leaves hoist_body/n->d exactly as decided above)
     * for anything not matching that exact narrow shape. */
    {
        const char *rhs_ivar_name;
        const struct AstNode *rhs_orig;
        const char *rhs_modified_name = NULL;

        if (n->sym == NULL &&
            ast_for_rhs_hoist_scan_supported(n, &rhs_ivar_name, &rhs_orig)) {
            struct AstNode *body_assign = hoist_body != NULL ? hoist_body->a : n->d->a;
            struct AstNode *rhs_rewritten;

            if (body_assign != NULL && body_assign->kind == AST_ASSIGN &&
                body_assign->a != NULL && body_assign->a->kind == AST_IDENT)
                rhs_modified_name = body_assign->a->sval;
            rhs_rewritten = ast_hoist_row_invariant_2d_reads(
                rhs_orig, rhs_ivar_name, rhs_modified_name);

            if (rhs_rewritten != rhs_orig) {
                if (hoist_body != NULL) {
                    hoist_body->a->b = rhs_rewritten;
                } else {
                    struct AstNode *assign = ast_new(&g_ast_arena, AST_ASSIGN);
                    assign->op = n->d->a->op;
                    assign->a = n->d->a->a;
                    assign->b = rhs_rewritten;
                    hoist_body = ast_new(&g_ast_arena, AST_EXPR_STMT);
                    hoist_body->a = assign;
                }
            }
        }
    }

    /* General loop-invariant code motion (dcc_licm.c): only attempted when
     * none of the three narrow hoists above already produced a rewritten
     * body, to keep this new pass's interaction with them trivial - it
     * covers loop bodies of any shape (multiple statements, nested ifs),
     * unlike those three, which are all limited to a single-statement
     * body. */
    if (n->sym == NULL && hoist_body == NULL) {
        struct AstNode *licm_rewritten = ast_licm_hoist_invariants(n);
        if (licm_rewritten != NULL)
            hoist_body = licm_rewritten;
    }

    emit_label(ltop);
    if (!rotate && n->b != NULL)
        ast_gen_cond_branch(n->b, lend, 0);

    break_stack[nflow] = lend;
    cont_stack[nflow] = linc;
    flow_scope_depth[nflow] = g_func_pass.scope_depth;
    nflow++;
    if (n->sym != NULL) {
        /* Cyclic-byte-fill fast path (see ast_for_mod_fill_supported): the
         * loop's own test/increment above and below are untouched - only
         * this one statement's BASE+(ivar%MOD) computation is replaced, by a
         * frame-resident byte counter (n->sym, reserved at build time)
         * primed to BASE+(INIT%MOD) once before the loop and then simply
         * incremented (wrapping at BASE+MOD back to BASE) each iteration,
         * avoiding a division call in the hot path. */
        long base_val, mod_val;
        int val_type;
        int lwrap;

        ast_for_mod_fill_supported(n, NULL, NULL, &base_val, &mod_val, NULL);
        gen_index_addr_ast(n->d->a->a, &val_type);   /* HL = &ARR[ivar] */
        fprintf(g_emit_sink.stream, "\tld a,(ix%+d)\n", n->sym->offset);
        emit("\tld (hl),a\n");
        emit("\tinc a\n");
        fprintf(g_emit_sink.stream, "\tcp %ld\n", base_val + mod_val);
        lwrap = new_label();
        emit_jp_label("jr nz,", lwrap);
        fprintf(g_emit_sink.stream, "\tld a,%ld\n", base_val);
        emit_label(lwrap);
        fprintf(g_emit_sink.stream, "\tld (ix%+d),a\n", n->sym->offset);
    } else if (hoist_body != NULL) {
        ast_gen_stmt(hoist_body);
    } else {
        ast_gen_stmt(n->d);
    }
    nflow--;

    emit_label(linc);
    if (n->c != NULL) {
        int old_dead = expr_result_dead;
        expr_result_dead = 1;
        ast_gen_dead_expr(n->c);
        expr_result_dead = old_dead;
    }
    if (rotate)
        ast_gen_cond_branch(n->b, ltop, 1);
    else
        emit_jp_label("jp", ltop);
    emit_label(lend);

    /* Close the for-init scope so source names resolve to outer symbols. */
    while (rename_count > 0) {
        pop_for_rename();
        rename_count--;
    }
}

static int ast_try_loop_regalloc(const struct AstNode *loop_node,
                                 const struct AstNode *cond,
                                 const struct AstNode *inc,
                                 const struct AstNode *body,
                                 void (*gen_loop_impl)(const struct AstNode *))
{
    int is_write;
    struct Sym *cand;

    /* Nesting depth of the loop about to be generated, for the cost model
     * behind the emitted claim directive. nflow counts the enclosing
     * breakable scopes and has not yet been incremented for THIS loop, so
     * it is exactly the number of loops/switches wrapped around it. A
     * candidate inside a nested loop is worth far more per reference than
     * the same candidate in straight-line code, and publishing that is what
     * lets dccpeep tell a valuable claim from a marginal one instead of
     * unconditionally yielding BC to whichever claim it happens to see. */
    loop_regalloc_last_depth = nflow;

    cand = loop_regalloc_find_bc_candidate(cond, inc, body, &is_write);
    if (cand == NULL)
        return 0;
    if (is_write)
        return try_loop_regalloc_bc_write(loop_node, cand, gen_loop_impl);
    return try_loop_regalloc_bc(loop_node, cand, gen_loop_impl);
}

/* Loop-scoped BC register promotion (dcc_loop_regalloc.c): if this loop has
 * an eligible candidate (see loop_regalloc_find_bc_candidate), try
 * generating the whole loop with it primed into BC instead of its normal
 * frame slot - via try_loop_regalloc_bc for a read-only candidate (Phase
 * 1) or try_loop_regalloc_bc_write for one the loop also writes (Phase 2,
 * which additionally spills BC back to the frame slot once, after the
 * loop) - verify the result is safe, and commit it. Falls back to plain,
 * unpromoted generation (the untouched ast_gen_for_stmt_impl above) if no
 * candidate exists or the speculative attempt is declined. */
void ast_gen_for_stmt(const struct AstNode *n)
{
    if (ast_try_loop_regalloc(n, n->b, n->c, n->d, ast_gen_for_stmt_impl))
        return;
    ast_gen_for_stmt_impl(n);
}

/* Renamed original body of the AST_WHILE case (formerly inline in
 * ast_gen_stmt's switch) - see ast_gen_while_stmt just below for the
 * loop-scoped BC register-promotion wrapper now placed around it. */
static void ast_gen_while_stmt_impl(const struct AstNode *n)
{
    /* Generic while shape: ltop, lend allocated up front;
     * label(ltop); test condition -> lend; body inside nflow scope;
     * jp ltop; label(lend). */
    int ltop = new_label();
    int lend = new_label();
    emit_label(ltop);
    if (ast_is_const_nonzero_condition(n->a)) {
        ast_gen_expr(n->a);
        emit_test_expr_nonzero(g_expr.type, lend, 0);
    } else {
        ast_gen_cond_branch(n->a, lend, 0);
    }
    break_stack[nflow] = lend;
    cont_stack[nflow] = ltop;
    flow_scope_depth[nflow] = g_func_pass.scope_depth;
    nflow++;
    ast_gen_stmt(n->b);
    nflow--;
    emit_jp_label("jp", ltop);
    emit_label(lend);
}

/* Loop-scoped BC register promotion (dcc_loop_regalloc.c) for AST_WHILE -
 * same wrapper shape as ast_gen_for_stmt, just with no separate increment
 * clause (AST_WHILE's cond is n->a, body is n->b; there is no n->c). */
void ast_gen_while_stmt(const struct AstNode *n)
{
    if (ast_try_loop_regalloc(n, n->a, NULL, n->b, ast_gen_while_stmt_impl))
        return;
    ast_gen_while_stmt_impl(n);
}

/* Renamed original body of the AST_DOWHILE case (formerly inline in
 * ast_gen_stmt's switch) - see ast_gen_dowhile_stmt just below. */
static void ast_gen_dowhile_stmt_impl(const struct AstNode *n)
{
    /* Generic do-while shape: ltop, lcont, lend
     * allocated up front; label(ltop); body inside nflow scope (break->
     * lend, continue->lcont); label(lcont); test condition -> ltop when
     * TRUE (branch sense 1); label(lend).  For `while(0)`, keep the labels
     * but omit the test/back-edge. */
    int ltop = new_label();
    int lcont = new_label();
    int lend = new_label();
    emit_label(ltop);
    break_stack[nflow] = lend;
    cont_stack[nflow] = lcont;
    flow_scope_depth[nflow] = g_func_pass.scope_depth;
    nflow++;
    ast_gen_stmt(n->b);
    nflow--;
    emit_label(lcont);
    if (ast_is_const_nonzero_condition(n->a))
        emit_jp_label("jp", ltop);
    else if (!ast_is_const_zero_condition(n->a))
        ast_gen_cond_branch(n->a, ltop, 1);
    emit_label(lend);
}

/* Loop-scoped BC register promotion (dcc_loop_regalloc.c) for AST_DOWHILE -
 * same wrapper shape as ast_gen_for_stmt/ast_gen_while_stmt. */
void ast_gen_dowhile_stmt(const struct AstNode *n)
{
    if (ast_try_loop_regalloc(n, n->a, NULL, n->b, ast_gen_dowhile_stmt_impl))
        return;
    ast_gen_dowhile_stmt_impl(n);
}

/* See dcc_ast.h for the rationale: scan_function_body's frame-sizing scan
 * (dcc_func.c) calls this instead of hand-walking a for-statement's tokens,
 * so any local allocation the real codegen pass would make (declarations in
 * the body, ast_for_mod_fill_supported's rolling-counter slot, etc.) is
 * automatically accounted for here too, since this runs the exact same
 * builder+emitter. Output is redirected to a throwaway sink for the
 * duration - this must never write to the real output file. scan_mode is
 * also set, matching the existing C99 for-init declaration probe in
 * ast_gen_cond_branch: some emit-adjacent bookkeeping (e.g.
 * emit_runtime_extrn_if_needed's "already emitted" cache) is keyed on it
 * specifically, not on where g_emit_sink.stream points, and must see this replay as
 * suppressed or a runtime helper first used here would be marked emitted
 * while its EXTRN line went nowhere, leaving it undefined for the real
 * pass. */
static int g_ast_last_stmt_exits;

int ast_scan_for_stmt(void)
{
    static FILE *sink = NULL;
    EmitSink saved_sink;
    int s_scan_mode;
    struct AstNode *n;

    saved_sink = g_emit_sink;
    s_scan_mode = scan_mode;
    if (sink == NULL)
        sink = fopen(DCC_NULL_DEVICE, "w");
    if (sink != NULL)
        saved_sink = emit_sink_push(sink, EMIT_SINK_DISCARD);
    scan_mode = 1;

    n = ast_build_stmt(&g_ast_arena);
    if (n != NULL) {
        ast_support_cache_begin();
        if (ast_stmt_supported(n))
            ast_gen_stmt(n);
    }

    ast_arena_reset(&g_ast_arena);
    scan_mode = s_scan_mode;
    emit_sink_restore(&saved_sink);
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

void ast_emit_debug_location(const char *file, int line)
{
    const char *p;

    if (!opt_debug || scan_mode || line <= 0)
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

static void ast_emit_debug_line(const struct AstNode *n)
{
    ast_emit_debug_location(n->file, n->line);
}

/* Emit statement node `n` (gated by ast_stmt_supported). */
void ast_gen_stmt(const struct AstNode *n)
{
    ast_emit_debug_line(n);

    switch (n->kind) {
    case AST_EMPTY:
        break;                            /* empty statement: emit nothing */
    case AST_DIVMOD_CALL: {
        /* Compiler-synthesized only (see dcc_ast.h and ast_divmod_fuse_
         * compound in dcc_ast_gen_support.c): a and b are both bare int
         * identifiers by construction of the pass that built this node, so
         * evaluating them needs none of gen_binary_ast's general-purpose
         * machinery (promotion, pointer arithmetic, float paths, constant
         * folding) - just the standard "lhs into HL, push, rhs into HL,
         * ex de,hl/pop hl to land lhs in HL and rhs in DE" sequence used
         * throughout this file for a plain two-operand evaluation. */
        struct Sym *rem_sym = find_sym(n->sval);
        ast_gen_expr(n->a);
        emit("\tpush hl\n");
        ast_gen_expr(n->b);
        emit("\tex de,hl\n");
        emit("\tpop hl\n");
        emit_runtime_call(n->ival ? "__sdivmod" : "__udivmod");
        /* Stash the remainder (DE) on the stack before storing the quotient:
         * emit_store_hl_to_sym_direct's out-of-range-(ix+d) fallback (see
         * dcc_symbols.c) uses DE as scratch and only guarantees HL holds the
         * stored value on return, not that DE survives untouched - true for
         * e.c's own #dmq/#dmr temps once its frame grows past IX's +-127
         * range. Do not rely on DE surviving across that call. */
        emit("\tpush de\n");
        emit_store_hl_to_sym_direct(n->sym);   /* HL = quotient */
        emit("\tpop hl\n");                     /* HL = remainder */
        emit_store_hl_to_sym_direct(rem_sym);
        break;
    }
    case AST_DECL:
        ast_emit_decl_span(n);            /* declaration codegen replay */
        break;
    case AST_EXPR_STMT: {
        /* Expression statement results are dead, so emit with
         * expr_result_dead set. */
        int old_dead = expr_result_dead;
        expr_result_dead = 1;
        ast_gen_dead_expr(n->a);
        expr_result_dead = old_dead;
        break;
    }
    case AST_RETURN:
        gen_return_ast(n);
        break;
    case AST_BREAK:
        emit_vla_restore_for_flow(flow_scope_depth[nflow - 1]);
        emit_jp_label("jp", break_stack[nflow - 1]);
        break;
    case AST_CONTINUE:
        emit_vla_restore_for_flow(flow_scope_depth[nflow - 1]);
        emit_jp_label("jp", cont_stack[nflow - 1]);
        break;
    case AST_GOTO:
        {
            int li;
            int active_depth;
            li = find_or_alloc_user_label_index(n->sval);
            ulabel_referenced[li] = 1;
            active_depth = vla_active_scope_depth();
            if (ulabel_defined[li]) {
                /* Backward goto: the label's scope is already known. */
                if (vla_jump_enters_label_scope(li)) {
                    error_here("goto into a variable-length array scope is not supported");
                } else if (active_depth != 0) {
                    emit_vla_restore_to_label_scope(li);
                }
                emit_jp_label("jp", ulabel_ids[li]);
            } else if (active_depth != 0) {
                /* Forward goto from within a VLA scope: the target label has not
                 * been emitted yet, so route the jump through a deferred fixup
                 * stub that vla_resolve_fwd_gotos() completes once the label's
                 * own scope (and hence the exact SP to restore) is known. */
                int fixup = vla_record_fwd_goto(li, n->line);
                emit_jp_label("jp", fixup);
            } else {
                /* Forward goto from outside every VLA scope.  Nothing to
                 * reclaim; if the label proves to be inside a VLA scope the
                 * label site rejects it as a jump into that scope. */
                ulabel_shallow_fwd_ref[li] = 1;
                emit_jp_label("jp", ulabel_ids[li]);
            }
        }
        break;
    case AST_LABEL:
        {
            int li;
            li = find_or_alloc_user_label_index(n->sval);
            if (vla_active_scope_depth() != 0 && ulabel_shallow_fwd_ref[li])
                error_here("goto into a variable-length array scope is not supported");
            /* Emit any deferred forward-goto fixup stubs before the real label
             * (they jump to it and fall-through is guarded). */
            vla_resolve_fwd_gotos(li, ulabel_ids[li]);
            emit_label(define_user_label(n->sval));
        }
        ast_gen_stmt(n->b);
        break;
    case AST_CASE: {
        long cv;
        int i;
        int lab;
        struct AstSwCtx *sw;

        if (ast_sw_depth <= 0)
            fatal("case label outside switch");
        if (vla_active_scope_depth() != 0)
            error_here("case label inside a variable-length array scope is not supported");
        cv = n->ival;
        lab = -1;
        sw = &ast_sw_ctx[ast_sw_depth - 1];
        for (i = 0; i < sw->n; ++i) {
            if ((sw->vals[i] & 0xffff) == ((int)cv & 0xffff)) {
                lab = sw->labs[i];
                break;
            }
        }
        if (lab >= 0)
            emit_label(lab);
        ast_gen_stmt(n->b);
        break;
    }
    case AST_DEFAULT: {
        struct AstSwCtx *sw;

        if (ast_sw_depth <= 0)
            fatal("default label outside switch");
        if (vla_active_scope_depth() != 0)
            error_here("default label inside a variable-length array scope is not supported");
        sw = &ast_sw_ctx[ast_sw_depth - 1];
        if (sw->def_lab >= 0)
            emit_label(sw->def_lab);
        ast_gen_stmt(n->b);
        break;
    }
    case AST_IF: {
        if (n->c == NULL) {
            /* No else: branch straight to the join point on a false
             * condition instead of routing through a separate empty-else
             * label. Avoids emitting an unconditional jump that would just
             * land on the very next instruction (skipping a zero-width
             * else), which was left for the peephole optimizer to clean up
             * and therefore visible verbatim in -g builds, which skip
             * peephole entirely for debug-stepping fidelity. */
            int lend = new_label();
            ast_gen_cond_branch(n->a, lend, 0);
            ast_gen_stmt(n->b);
            emit_label(lend);
            break;
        }

        /* Generic if/else condition shape, including the label allocation
         * order (lelse, lend before the condition). */
        {
            int lelse = new_label();
            int lend = new_label();
            ast_gen_cond_branch(n->a, lelse, 0);
            ast_gen_stmt(n->b);
            emit_jp_label("jp", lend);
            emit_label(lelse);
            ast_gen_stmt(n->c);
            emit_label(lend);
        }
        break;
    }
    case AST_WHILE:
        ast_gen_while_stmt(n);
        break;
    case AST_DOWHILE:
        ast_gen_dowhile_stmt(n);
        break;
    case AST_FOR:
        ast_gen_for_stmt(n);
        break;
    case AST_SWITCH:
        ast_gen_switch_stmt(n);
        break;
    case AST_COMPOUND: {
        /* enter_scope(); emit each child (statements and AST_DECL declaration
         * spans, the latter running the declaration codegen); leave_scope().
         * enter/leave emit nothing. */
        int i;
        int dead;
        const struct AstNode *fused;
        const struct AstNode *body;
        /* ast_divmod_fuse_compound (see dcc_ast_gen_support.c) is the one
         * hoist in this file NOT anchored to ast_gen_for_stmt/dcc_licm.c -
         * its target shape (two adjacent statements sharing a %/ operand
         * pair) is not loop-specific at all, so it hooks in generically
         * here instead, where every compound block in the program - a
         * function body, a while/if/for body, any nested block - already
         * passes through uniformly. Only ever rewrites `body`'s own list
         * for the walk below; n itself is untouched, exactly like every
         * other hoist's "n->d itself is untouched" discipline. */
        fused = ast_divmod_fuse_compound(n);
        body = fused != NULL ? fused : n;
        enter_scope();
        dead = 0;
        for (i = 0; i < body->list_len; ++i) {
            if (dead && !ast_stmt_has_reentry_label(body->list[i])) {
                int j;
                if (body->list[i]->kind == AST_DECL) {
                    for (j = i + 1; j < body->list_len; ++j) {
                        if (ast_stmt_has_reentry_label(body->list[j])) {
                            asm_suppress_depth++;
                            ast_gen_stmt(body->list[i]);
                            asm_suppress_depth--;
                            break;
                        }
                    }
                }
                continue;
            }
            if (i + 1 < body->list_len) {
                const struct AstNode *bp_lo;
                const struct AstNode *bp_s2_assign;
                if (ast_byte_pair_word_write_match(body->list[i], body->list[i + 1],
                                                   &bp_lo, &bp_s2_assign)) {
                    gen_byte_pair_word_write_ast(bp_lo, body->list[i]->a, bp_s2_assign);
                    dead = 0;
                    ++i;
                    continue;
                }
            }
            ast_gen_stmt(body->list[i]);
            dead = ast_stmt_exits(body->list[i]);
        }
        /* Reclaim this scope's VLAs on fall-through exit (unreachable when the
         * block always exits; break/continue/return reclaim on their own). */
        if (!dead && g_func_pass.scope_depth < MAX_SCOPE_DEPTH &&
            g_vla_scope_off[g_func_pass.scope_depth] != 0)
            emit_vla_restore_sp(g_vla_scope_off[g_func_pass.scope_depth]);
        if (!dead)
            ast_emit_debug_location(n->end_file, n->end_line);
        leave_scope();
        break;
    }
    default:
        fatal("ast_gen_stmt: unsupported node");
    }
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

int ast_try_emit_statement(void)
{
    LexState _ls;
    LexState _le;
    int sv_for_seq;
    struct AstNode *n;
    int report;

    g_ast_last_stmt_exits = 0;

    if (scan_mode)
        return 0;

    report = getenv("DCC_AST_REPORT") != NULL;

    _ls = lex_save();
    sv_for_seq = g_func_pass.for_seq;

    n = ast_build_stmt(&g_ast_arena);

    _le = lex_save();

    if (n != NULL)
        ast_support_cache_begin();

    if (n != NULL && ast_stmt_supported(n)) {
        g_func_pass.for_seq = sv_for_seq;
        if (g_ast_build_enabled == 2)
            ast_dump(n, 0);
        ast_gen_stmt(n);
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
    ast_arena_reset(&g_ast_arena);
    return 1;
}
