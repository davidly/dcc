/*
 * dcc_licm.c - general loop-invariant code motion for `for` loop bodies.
 *
 * dcc_ast_gen_stmt.c already hoists three narrow, single-statement-body
 * shapes (a loop-invariant lvalue address, a row-invariant 2D array read,
 * and a global-member value proven invariant via dcc_global_scan.c). This
 * file generalizes the same idea - "the same value gets recomputed every
 * iteration even though it can't actually change" - to loop bodies of any
 * shape (multiple statements, nested ifs), for the narrower but still very
 * common case of a pure scalar-arithmetic subexpression (no memory access,
 * no calls, no side effects) that doesn't depend on anything the loop
 * itself modifies.
 *
 * Motivating case (tests/00040.c, an 8-queens solver profiled at 98.5% of
 * total runtime in one function):
 *
 *     int chk(int x, int y)
 *     {
 *         int i, r;
 *         for (r=i=0; i<8; i++) {
 *             r = r + t[x + 8*i];
 *             r = r + t[i + 8*y];   -- 8*y recomputed every iteration
 *             ...
 *         }
 *     }
 *
 * `y` is a parameter, never assigned anywhere in the loop, so `8*y` is the
 * same value on every one of the loop's iterations - and this shape (a
 * multi-statement body with several `if`s) doesn't match any of the three
 * existing narrow hoists at all.
 *
 * Design, deliberately conservative at every step (an unrecognized shape or
 * anything that could plausibly break the invariance argument is always a
 * decline, never a guess - matching dcc_array_narrow.c/dcc_global_scan.c):
 *
 *   1. Scan the loop's condition, increment, and body for every plain
 *      identifier that is assigned, incremented/decremented, or has its
 *      address taken anywhere - that's the set nothing invariant may depend
 *      on. Any function call anywhere, or any statement/expression shape
 *      this doesn't specifically recognize, marks the WHOLE set as
 *      "modified" (i.e. declines every candidate), since a call could modify
 *      anything through an escaped pointer or global that a purely lexical
 *      scan can't rule out.
 *   2. Walk the body looking for the largest (outermost) subexpressions that
 *      are pure scalar arithmetic (int-sized, no pointers, no memory access,
 *      no calls) and reference only locals/params outside that modified
 *      set - never descending further into an eligible subexpression once
 *      found, so `t[x + 8*i]`'s inner `8*i` is correctly left alone (`i` is
 *      modified) while `t[i + 8*y]`'s inner `8*y` is found and taken whole.
 *   3. For each distinct candidate (by structural equality - the same
 *      subexpression appearing more than once shares one temp), compute its
 *      value once, into a fresh compiler-temp local, before the loop.
 *   4. Return a rewritten copy of the body with every occurrence replaced by
 *      a reference to its temp; the original body node is never mutated,
 *      matching ast_hoist_row_invariant_2d_reads's sharing discipline
 *      (only allocate a fresh copy of a node when something beneath it
 *      actually changed).
 *
 * Runs independently of, and after, the three narrow hoists in
 * ast_gen_for_stmt - only when none of them already produced a hoist_body,
 * to keep this file's interaction with that existing code trivial.
 */
#include "dcc.h"
#include "dcc_ast.h"

#define MAX_LICM_CANDIDATES 8
#define LICM_MAX_INLINE_EXPAND_DEPTH 16

/* "Currently being expanded" stack for AST_CALL's inline-substitution
 * recursion below: guards against a self- or mutually-recursive chain of
 * inline-substitutable functions sending this scan into unbounded (and,
 * for a self-call, literally infinite) recursion. Safe to keep as file-
 * scope state rather than threading it through every call: licm_scan_
 * modified is a single synchronous recursive descent that always fully
 * unwinds (push immediately before recursing into a callee's captured
 * body, pop immediately after) before this file does anything else, and
 * dcc is single-threaded. Hitting the cap (or revisiting a function
 * already being expanded) just overflows - conservative, matching every
 * other "shape this doesn't specifically model" decision in this file. */
static struct Sym *g_licm_inline_expand_stack[LICM_MAX_INLINE_EXPAND_DEPTH];
static int g_licm_inline_expand_depth;

/* Runtime helpers whose implementations are pure and touch no memory visible
 * to the caller. LICM may therefore treat them like ordinary scalar
 * operations after recursively checking their argument expressions. */
static const char *g_licm_safe_runtime_calls[] = {
    "__mulu", "__udivmod", "__divu", "__modu", "__divs", "__mods", "__sdivmod",
    "__caa", "__can", "__csp", "__cdg", "__cup", "__clo",
    "__cxd", "__cpr", "__cct", "__cpu", "__ctu", "__ctl",
    NULL
};

static int asm_name_is_licm_safe_call(const char *name)
{
    int i;

    for (i = 0; g_licm_safe_runtime_calls[i] != NULL; ++i)
        if (strcmp(name, g_licm_safe_runtime_calls[i]) == 0)
            return 1;
    return 0;
}

struct LicmCandidateList {
    const struct AstNode *nodes[MAX_LICM_CANDIDATES];
    int count;
};

static void licm_mark_modified(struct LicmModifiedNames *mod, const char *name)
{
    int i;

    if (name == NULL)
        return;
    for (i = 0; i < mod->count; ++i)
        if (strcmp(mod->names[i], name) == 0)
            return;
    if (mod->count < LICM_MAX_MODIFIED_NAMES)
        mod->names[mod->count++] = name;
    else
        mod->overflowed = 1;
}

static int licm_name_is_modified(const struct LicmModifiedNames *mod, const char *name)
{
    int i;

    if (mod->overflowed)
        return 1;
    for (i = 0; i < mod->count; ++i)
        if (strcmp(mod->names[i], name) == 0)
            return 1;
    return 0;
}

static void licm_scan_modified_switch_body(const struct AstNode *n, struct LicmModifiedNames *mod);

/* Recursively record every name assigned, incremented/decremented, or
 * address-taken anywhere in `n`. Any call to a function that might actually
 * return, or any node kind not explicitly recognized below (nested loops,
 * goto/labels, compound literals, ...) sets mod->overflowed - safe (declines
 * every candidate) rather than risking a missed modification a purely
 * lexical scan can't see through.
 *
 * AST_SWITCH and AST_CONTINUE are both explicitly safe, not just "not
 * unrecognized": a `continue` anywhere never skips this loop's own eventual
 * exit (it jumps to the increment/condition test, still inside the loop's
 * normal flow), and a `switch` is scanned via licm_scan_modified_switch_
 * body below, which is break-aware - a `break` reached directly inside a
 * switch's own body targets that switch (via break_stack[nflow-1] at
 * whichever nesting level is innermost at runtime - see ast_gen_stmt's
 * AST_SWITCH/AST_BREAK cases, dcc_ast_gen_stmt.c), not this loop, so it
 * doesn't skip this loop's exit either. A `break` reached OUTSIDE any
 * switch still overflows here (the normal AST_BREAK case, declined via the
 * default case below) since THAT one does target this loop directly.
 *
 * AST_CALL to a function declared _Noreturn (Sym::is_noreturn) is also
 * explicitly safe: whatever it clobbers can't matter, because control never
 * returns to any point after it on this path for this candidate's
 * eligibility to be evaluated against - only its own argument expressions
 * (which do still execute) need scanning.
 *
 * AST_CALL to a function dcc's inliner has captured as substitutable
 * (is_inline_substitutable, dcc_func.c) is likewise not an automatic
 * overflow: dcc's real codegen-time substitution (try_gen_inline_call_ast,
 * invoked from gen_call_ast during ast_gen_expr) replaces the call with the
 * callee's own captured body at the call site, so by the time this AST_CALL
 * node reaches actual codegen it may never become a real "call" instruction
 * at all - declining every loop that merely calls an inlinable helper (this
 * file's motivating change: forint.c's eval_e, whose hot loop calls
 * get_sym_val/set_sym_val for every variable access) would be needlessly
 * conservative. After scanning the call's own arguments normally, this
 * recurses into the callee's captured inline_return_expr/inline_stmt_expr/
 * inline_stmt_body as if it were written at the call site - parameter names
 * inside that captured body are left as-is (real substitution onto this
 * call's actual arguments happens later, at codegen time) and scanned as
 * ordinary identifiers, which is correct for this purpose: this scan only
 * cares about what's modified/address-taken/called, and dcc's own scoping
 * already prevents a parameter name from colliding with an unrelated
 * caller-scope identifier of the same name. g_licm_inline_expand_stack
 * guards against a self- or mutually-recursive chain of such functions
 * (see its own comment). A call to anything else - an ordinary function, a
 * function pointer, or an unresolvable callee - still overflows, since it
 * could modify anything through an escaped pointer or global that this
 * lexical scan can't rule out. */
void licm_scan_modified(const struct AstNode *n, struct LicmModifiedNames *mod)
{
    int i;

    if (n == NULL || mod->overflowed)
        return;

    switch (n->kind) {
    case AST_COMPOUND:
        for (i = 0; i < n->list_len && !mod->overflowed; ++i)
            licm_scan_modified(n->list[i], mod);
        return;
    case AST_EXPR_STMT:
        licm_scan_modified(n->a, mod);
        return;
    case AST_SWITCH:
        licm_scan_modified(n->a, mod);           /* control expression */
        licm_scan_modified_switch_body(n->b, mod);
        return;
    case AST_CONTINUE:
        return;
    case AST_IF:
        licm_scan_modified(n->a, mod);
        licm_scan_modified(n->b, mod);
        licm_scan_modified(n->c, mod);
        return;
    case AST_ASSIGN:
        if (n->a != NULL && n->a->kind == AST_IDENT)
            licm_mark_modified(mod, n->a->sval);
        licm_scan_modified(n->a, mod);
        licm_scan_modified(n->b, mod);
        return;
    case AST_UNARY:
        if ((n->op == TOK_INC || n->op == TOK_DEC || n->op == '&') &&
            n->a != NULL && n->a->kind == AST_IDENT)
            licm_mark_modified(mod, n->a->sval);
        licm_scan_modified(n->a, mod);
        return;
    case AST_POSTFIX:
        if (n->a != NULL && n->a->kind == AST_IDENT)
            licm_mark_modified(mod, n->a->sval);
        licm_scan_modified(n->a, mod);
        return;
    case AST_BINARY:
    case AST_LOGAND:
    case AST_LOGOR:
    case AST_COMMA:
        licm_scan_modified(n->a, mod);
        licm_scan_modified(n->b, mod);
        return;
    case AST_INDEX:
        licm_scan_modified(n->a, mod);
        licm_scan_modified(n->b, mod);
        return;
    case AST_MEMBER:
        licm_scan_modified(n->a, mod);
        return;
    case AST_COND:
        licm_scan_modified(n->a, mod);
        licm_scan_modified(n->b, mod);
        licm_scan_modified(n->c, mod);
        return;
    case AST_CAST:
        licm_scan_modified(n->a, mod);
        return;
    case AST_CALL: {
        struct Sym *fn_sym;
        const struct AstNode *inline_body;
        int di, already_expanding;

        fn_sym = (n->a != NULL && n->a->kind == AST_IDENT) ? find_global(n->a->sval) : NULL;
        if (fn_sym != NULL && fn_sym->is_noreturn) {
            for (i = 0; i < n->list_len && !mod->overflowed; ++i)
                licm_scan_modified(n->list[i], mod);
            return;
        }
        /* Calls to the verified-pure runtime helpers above have no writes or
         * further side effects, so only their argument expressions can
         * invalidate a loop-invariant candidate. */
        if (fn_sym != NULL &&
            asm_name_is_licm_safe_call(asm_name_for(sym_asm_name(fn_sym)))) {
            for (i = 0; i < n->list_len && !mod->overflowed; ++i)
                licm_scan_modified(n->list[i], mod);
            return;
        }
        if (fn_sym != NULL && is_inline_substitutable(fn_sym)) {
            for (i = 0; i < n->list_len && !mod->overflowed; ++i)
                licm_scan_modified(n->list[i], mod);
            if (mod->overflowed)
                return;

            already_expanding = 0;
            for (di = 0; di < g_licm_inline_expand_depth; ++di)
                if (g_licm_inline_expand_stack[di] == fn_sym)
                    already_expanding = 1;
            if (already_expanding || g_licm_inline_expand_depth >= LICM_MAX_INLINE_EXPAND_DEPTH) {
                mod->overflowed = 1;
                return;
            }

            inline_body = fn_sym->inline_return_expr != NULL ? fn_sym->inline_return_expr :
                          fn_sym->inline_stmt_expr != NULL ? fn_sym->inline_stmt_expr :
                          fn_sym->inline_stmt_body;
            g_licm_inline_expand_stack[g_licm_inline_expand_depth++] = fn_sym;
            licm_scan_modified(inline_body, mod);
            g_licm_inline_expand_depth--;
            return;
        }
        mod->overflowed = 1;
        return;
    }
    case AST_IDENT:
    case AST_INT_LIT:
    case AST_FLOAT_LIT:
    case AST_STR_LIT:
    case AST_SIZEOF_EXPR:
    case AST_SIZEOF_TYPE:
    case AST_EMPTY:
        return;
    default:
        /* AST_WHILE, AST_FOR, AST_DOWHILE, AST_RETURN, AST_GOTO, AST_LABEL,
         * AST_BREAK, AST_DECL, AST_COMPOUND_LITERAL, or anything else not
         * explicitly handled above (AST_CALL has its own case): these change
         * control flow in ways this simple walk does not model (AST_BREAK
         * here specifically means one reached OUTSIDE any switch, since
         * AST_SWITCH routes its own body through licm_scan_modified_switch_
         * body instead, which handles a switch-scoped break itself - see
         * that function). */
        mod->overflowed = 1;
        return;
    }
}

/* Companion to licm_scan_modified, used only for a switch statement's own
 * body (dispatched from its AST_SWITCH case): same job, but a `break`
 * reached here is safe (it targets this switch's own end, not the
 * enclosing loop scanned by the caller) rather than triggering overflow.
 * AST_CASE/AST_DEFAULT are pure statement labels (dcc_ast.h: `b` = the
 * following statement) - unwrap and keep walking in switch-body mode, so a
 * break directly inside a `case` block still resolves as switch-scoped.
 * Anything that isn't a label, a break, or a nested compound is real
 * statement content, delegated back to the ordinary licm_scan_modified -
 * including a NESTED switch or loop within this one's cases, which get
 * exactly the same handling (a nested switch recurses back into this same
 * function for its own body; a nested loop still overflows via licm_scan_
 * modified's default case, unchanged). */
static void licm_scan_modified_switch_body(const struct AstNode *n, struct LicmModifiedNames *mod)
{
    int i;

    if (n == NULL || mod->overflowed)
        return;

    switch (n->kind) {
    case AST_COMPOUND:
        for (i = 0; i < n->list_len && !mod->overflowed; ++i)
            licm_scan_modified_switch_body(n->list[i], mod);
        return;
    case AST_CASE:
    case AST_DEFAULT:
        licm_scan_modified_switch_body(n->b, mod);
        return;
    case AST_BREAK:
        return;
    default:
        licm_scan_modified(n, mod);
        return;
    }
}

/* True if `n` is built entirely from integer literals and int-sized (not
 * long, not pointer, not float) local/parameter scalars, combined only with
 * plain arithmetic/bitwise/shift operators or unary -/~. This is the
 * "eligible to hoist at all" purity gate: no memory access, no calls, no
 * side effects anywhere, so its value depends only on the named locals/
 * params it reads. */
static int licm_is_pure_scalar_arith(const struct AstNode *n)
{
    if (n == NULL)
        return 0;

    switch (n->kind) {
    case AST_INT_LIT:
        return 1;
    case AST_IDENT:
        {
            struct Sym *s = find_sym(n->sval);
            if (s == NULL)
                return 0;
            if (s->storage != SC_LOCAL && s->storage != SC_PARAM)
                return 0;
            if (s->is_array || s->is_vla)
                return 0;
            if (type_ptr_depth(s->type) != 0)
                return 0;
            if (type_is_float(s->type))
                return 0;
            if (type_size(s->type) > 2)
                return 0;
            return 1;
        }
    case AST_BINARY:
        switch (n->op) {
        case '+': case '-': case '*': case '/': case '%':
        case '&': case '|': case '^': case TOK_SHL: case TOK_SHR:
            return licm_is_pure_scalar_arith(n->a) && licm_is_pure_scalar_arith(n->b);
        default:
            return 0;
        }
    case AST_UNARY:
        if (n->op == '-' || n->op == '~')
            return licm_is_pure_scalar_arith(n->a);
        return 0;
    default:
        return 0;
    }
}

/* True if any identifier referenced in `n` is in `mod`. Only ever called on
 * nodes licm_is_pure_scalar_arith already approved, so the shapes here are
 * exactly the ones that function allows. */
static int licm_expr_depends_on_modified(const struct AstNode *n, const struct LicmModifiedNames *mod)
{
    if (n == NULL)
        return 0;
    if (n->kind == AST_IDENT)
        return licm_name_is_modified(mod, n->sval);
    if (n->kind == AST_BINARY)
        return licm_expr_depends_on_modified(n->a, mod) || licm_expr_depends_on_modified(n->b, mod);
    if (n->kind == AST_UNARY)
        return licm_expr_depends_on_modified(n->a, mod);
    return 0;
}

/* Structural equality over exactly the shapes licm_is_pure_scalar_arith
 * allows - used to dedupe repeated occurrences of the same candidate onto
 * one temp, and to recognize occurrences during the rewrite pass. */
static int licm_expr_equal(const struct AstNode *a, const struct AstNode *b)
{
    if (a == b)
        return 1;
    if (a == NULL || b == NULL)
        return 0;
    if (a->kind != b->kind || a->op != b->op)
        return 0;
    switch (a->kind) {
    case AST_INT_LIT:
        return a->ival == b->ival;
    case AST_IDENT:
        return a->sval != NULL && b->sval != NULL && strcmp(a->sval, b->sval) == 0;
    case AST_BINARY:
        return licm_expr_equal(a->a, b->a) && licm_expr_equal(a->b, b->b);
    case AST_UNARY:
        return licm_expr_equal(a->a, b->a);
    default:
        return 0;
    }
}

static void licm_add_candidate(struct LicmCandidateList *out, const struct AstNode *n)
{
    int i;

    for (i = 0; i < out->count; ++i)
        if (licm_expr_equal(out->nodes[i], n))
            return;
    if (out->count < MAX_LICM_CANDIDATES)
        out->nodes[out->count++] = n;
}

/* Walk `n` looking for the largest (outermost) pure-scalar-arithmetic
 * subexpressions that don't depend on `mod`. Once such a node is found it is
 * recorded whole and NOT descended into further - it is already maximal, and
 * descending further would only find smaller, less valuable candidates
 * nested inside a value already being hoisted in full. A node that fails
 * either test is never itself hoistable, so recursion continues into its
 * children looking for a smaller eligible piece (e.g. `i + 8*y`: the whole
 * expression depends on the modified `i`, so it's not taken, but `8*y`
 * beneath it is). Same statement-shape coverage as licm_scan_modified;
 * anything else is simply left alone (always safe, just less thorough). */
static void licm_collect_candidates(const struct AstNode *n, const struct LicmModifiedNames *mod,
                                     struct LicmCandidateList *out)
{
    if (n == NULL || out->count >= MAX_LICM_CANDIDATES)
        return;

    switch (n->kind) {
    case AST_COMPOUND:
        {
            int i;
            for (i = 0; i < n->list_len; ++i)
                licm_collect_candidates(n->list[i], mod, out);
        }
        return;
    case AST_EXPR_STMT:
        licm_collect_candidates(n->a, mod, out);
        return;
    case AST_IF:
        licm_collect_candidates(n->a, mod, out);
        licm_collect_candidates(n->b, mod, out);
        licm_collect_candidates(n->c, mod, out);
        return;
    case AST_ASSIGN:
        licm_collect_candidates(n->b, mod, out);
        if (n->a != NULL && (n->a->kind == AST_INDEX || n->a->kind == AST_MEMBER || n->a->kind == AST_UNARY))
            licm_collect_candidates(n->a, mod, out);
        return;
    case AST_MEMBER:
        licm_collect_candidates(n->a, mod, out);
        return;
    case AST_COND:
        licm_collect_candidates(n->a, mod, out);
        licm_collect_candidates(n->b, mod, out);
        licm_collect_candidates(n->c, mod, out);
        return;
    case AST_CAST:
        licm_collect_candidates(n->a, mod, out);
        return;
    case AST_COMMA:
        licm_collect_candidates(n->a, mod, out);
        licm_collect_candidates(n->b, mod, out);
        return;
    default:
        break;
    }

    if ((n->kind == AST_BINARY || n->kind == AST_UNARY) &&
        licm_is_pure_scalar_arith(n) &&
        !licm_expr_depends_on_modified(n, mod)) {
        licm_add_candidate(out, n);
        return;
    }

    if (n->kind == AST_BINARY || n->kind == AST_LOGAND || n->kind == AST_LOGOR) {
        licm_collect_candidates(n->a, mod, out);
        licm_collect_candidates(n->b, mod, out);
    } else if (n->kind == AST_UNARY) {
        licm_collect_candidates(n->a, mod, out);
    } else if (n->kind == AST_INDEX) {
        licm_collect_candidates(n->a, mod, out);
        licm_collect_candidates(n->b, mod, out);
    }
    /* AST_CALL args, AST_POSTFIX operand, and anything else: a candidate
     * reachable only through one of those positions is left un-hoisted -
     * always safe, just less thorough. */
}

/* Rebuild `n`, replacing every occurrence of a candidate (by structural
 * equality) with an AST_IDENT reference to its hoisted temp, sharing any
 * subtree that needed no rewrite (mirrors ast_hoist_row_invariant_2d_reads).
 * Same statement-shape coverage as the two walkers above. */
static struct AstNode *licm_rewrite(const struct AstNode *n, const struct LicmCandidateList *cands,
                                    struct Sym **temps)
{
    struct AstNode *na, *nb, *nc, *copy;
    int i;

    if (n == NULL)
        return NULL;

    for (i = 0; i < cands->count; ++i) {
        if (licm_expr_equal(n, cands->nodes[i])) {
            struct AstNode *ident = ast_new(&g_ast_arena, AST_IDENT);
            ident->sval = ast_arena_strdup(&g_ast_arena, temps[i]->name);
            ident->type = temps[i]->type;
            return ident;
        }
    }

    switch (n->kind) {
    case AST_COMPOUND:
        {
            int changed = 0;
            struct AstNode **newlist = (struct AstNode **)ast_arena_alloc(&g_ast_arena,
                sizeof(struct AstNode *) * (size_t)(n->list_len > 0 ? n->list_len : 1));
            for (i = 0; i < n->list_len; ++i) {
                newlist[i] = licm_rewrite(n->list[i], cands, temps);
                if (newlist[i] != n->list[i])
                    changed = 1;
            }
            if (!changed)
                return (struct AstNode *)n;
            copy = ast_new(&g_ast_arena, AST_COMPOUND);
            *copy = *n;
            copy->list = newlist;
            return copy;
        }
    case AST_EXPR_STMT:
        na = licm_rewrite(n->a, cands, temps);
        if (na == n->a)
            return (struct AstNode *)n;
        copy = ast_new(&g_ast_arena, AST_EXPR_STMT);
        *copy = *n;
        copy->a = na;
        return copy;
    case AST_IF:
        na = licm_rewrite(n->a, cands, temps);
        nb = licm_rewrite(n->b, cands, temps);
        nc = licm_rewrite(n->c, cands, temps);
        if (na == n->a && nb == n->b && nc == n->c)
            return (struct AstNode *)n;
        copy = ast_new(&g_ast_arena, AST_IF);
        *copy = *n;
        copy->a = na; copy->b = nb; copy->c = nc;
        return copy;
    case AST_ASSIGN:
        na = licm_rewrite(n->a, cands, temps);
        nb = licm_rewrite(n->b, cands, temps);
        if (na == n->a && nb == n->b)
            return (struct AstNode *)n;
        copy = ast_new(&g_ast_arena, AST_ASSIGN);
        *copy = *n;
        copy->a = na; copy->b = nb;
        return copy;
    case AST_BINARY:
    case AST_LOGAND:
    case AST_LOGOR:
    case AST_COMMA:
        na = licm_rewrite(n->a, cands, temps);
        nb = licm_rewrite(n->b, cands, temps);
        if (na == n->a && nb == n->b)
            return (struct AstNode *)n;
        copy = ast_new(&g_ast_arena, n->kind);
        *copy = *n;
        copy->a = na; copy->b = nb;
        return copy;
    case AST_UNARY:
        na = licm_rewrite(n->a, cands, temps);
        if (na == n->a)
            return (struct AstNode *)n;
        copy = ast_new(&g_ast_arena, AST_UNARY);
        *copy = *n;
        copy->a = na;
        return copy;
    case AST_INDEX:
        na = licm_rewrite(n->a, cands, temps);
        nb = licm_rewrite(n->b, cands, temps);
        if (na == n->a && nb == n->b)
            return (struct AstNode *)n;
        copy = ast_new(&g_ast_arena, AST_INDEX);
        *copy = *n;
        copy->a = na; copy->b = nb;
        return copy;
    case AST_MEMBER:
        na = licm_rewrite(n->a, cands, temps);
        if (na == n->a)
            return (struct AstNode *)n;
        copy = ast_new(&g_ast_arena, AST_MEMBER);
        *copy = *n;
        copy->a = na;
        return copy;
    case AST_COND:
        na = licm_rewrite(n->a, cands, temps);
        nb = licm_rewrite(n->b, cands, temps);
        nc = licm_rewrite(n->c, cands, temps);
        if (na == n->a && nb == n->b && nc == n->c)
            return (struct AstNode *)n;
        copy = ast_new(&g_ast_arena, AST_COND);
        *copy = *n;
        copy->a = na; copy->b = nb; copy->c = nc;
        return copy;
    case AST_CAST:
        na = licm_rewrite(n->a, cands, temps);
        if (na == n->a)
            return (struct AstNode *)n;
        copy = ast_new(&g_ast_arena, AST_CAST);
        *copy = *n;
        copy->a = na;
        return copy;
    default:
        return (struct AstNode *)n;
    }
}

/* ------------------------------------------------------------------------- *
 * Cross-statement CSE: the same pure subexpression appearing more than once
 * among the loop body's top-level statements (e.g. `x+i` tested in two
 * different `if` conditions - `if (x+i < 8 & y+i < 8)` and
 * `if (x+i < 8 & y-i >= 0)`). This is a fundamentally different concern from
 * the invariant hoist above - `x+i` depends on the loop's own induction
 * variable `i`, so it is NOT loop-invariant and correctly never reaches
 * licm_collect_candidates - but it is still computed twice on every
 * iteration for no reason, since nothing between the two uses can change it.
 *
 * An earlier version of this pass also counted (and hoisted based on) a
 * subexpression repeated between an if's own condition and its own
 * then/else body. That was measured to be a net regression on
 * tests/00040.c (+15% cycles): the temp is stored unconditionally, every
 * iteration, to serve a use inside the guarded body that may never actually
 * run (chk()'s if-conditions are pruning checks, false most of the time) -
 * so the store/reload overhead is paid far more often than the recompute it
 * was meant to avoid. Profitability here is therefore judged ONLY by
 * occurrences that are themselves unconditional - a top-level statement not
 * guarded by any if, or an if's own condition (which always runs, whichever
 * way it goes) - never an if's then/else. Once a candidate qualifies on
 * that basis, every occurrence (guarded body included) is still rewritten
 * to use the temp, as a free bonus.
 *
 * Unlike the invariant hoist, this cannot compute the shared value once via
 * a direct codegen call at rewrite time (that would only run once, before
 * the loop, which is wrong - the value depends on `i` and must be
 * recomputed every iteration). Instead this builds an ordinary `temp =
 * expr;` assignment AST node and splices it into the body's top-level
 * statement list once, ahead of every statement, so it gets normal
 * per-iteration codegen exactly like any other statement in the body.
 * ------------------------------------------------------------------------- */

struct LicmCseCandidate {
    const struct AstNode *node;
    int count;
};

struct LicmCseCandidateList {
    struct LicmCseCandidate items[MAX_LICM_CANDIDATES];
    int count;
};

/* mod must be the set of names modified ANYWHERE in this loop's own body
 * (not just between two occurrences of `n`): the shared temp this candidate
 * may become is computed once, at the very top of the body, before every
 * original statement runs, so its value can only stand in for every later
 * occurrence if none of those occurrences' operands changes anywhere before
 * them - the same bar licm_collect_candidates already holds the separate
 * cross-iteration hoist to, just applied within a single iteration instead
 * of across iterations. Without this check, `total = total + factor; total
 * = total + factor;` (factor unmodified, but total reassigned by the very
 * statement containing the first occurrence) was wrongly recognized as the
 * same value twice and computed only once, silently dropping the second
 * addition - found via tests/tbcint.c, a register-allocation regression
 * test that happened to also contain this shape, unrelated to registers at
 * all. */
static void licm_cse_bump(struct LicmCseCandidateList *out, const struct AstNode *n,
                          const struct LicmModifiedNames *mod)
{
    int i;

    if (licm_expr_depends_on_modified(n, mod))
        return;

    for (i = 0; i < out->count; ++i) {
        if (licm_expr_equal(out->items[i].node, n)) {
            out->items[i].count++;
            return;
        }
    }
    if (out->count < MAX_LICM_CANDIDATES) {
        out->items[out->count].node = n;
        out->items[out->count].count = 1;
        out->count++;
    }
}

/* Same maximality rule and statement-shape coverage as
 * licm_collect_candidates, but with no CROSS-ITERATION invariance
 * requirement - this counts every distinct maximal pure-scalar-arith
 * subexpression found anywhere in `n` whose operands aren't modified
 * anywhere in the loop body (see licm_cse_bump), regardless of whether it
 * also depends on the loop's induction variable. Used within a single
 * if-statement's {cond, then, else}, so a count of 2+ means the same value
 * is computed twice for that one statement. */
static void licm_cse_collect(const struct AstNode *n, const struct LicmModifiedNames *mod,
                             struct LicmCseCandidateList *out)
{
    if (n == NULL)
        return;

    switch (n->kind) {
    case AST_IF:
        licm_cse_collect(n->a, mod, out);
        licm_cse_collect(n->b, mod, out);
        licm_cse_collect(n->c, mod, out);
        return;
    case AST_EXPR_STMT:
        licm_cse_collect(n->a, mod, out);
        return;
    case AST_ASSIGN:
        licm_cse_collect(n->b, mod, out);
        if (n->a != NULL && (n->a->kind == AST_INDEX || n->a->kind == AST_MEMBER || n->a->kind == AST_UNARY))
            licm_cse_collect(n->a, mod, out);
        return;
    case AST_MEMBER:
        licm_cse_collect(n->a, mod, out);
        return;
    case AST_COND:
        licm_cse_collect(n->a, mod, out);
        licm_cse_collect(n->b, mod, out);
        licm_cse_collect(n->c, mod, out);
        return;
    case AST_CAST:
        licm_cse_collect(n->a, mod, out);
        return;
    case AST_COMMA:
        licm_cse_collect(n->a, mod, out);
        licm_cse_collect(n->b, mod, out);
        return;
    default:
        break;
    }

    /* Unlike licm_collect_candidates, record a pure match but keep recursing
     * into its children instead of stopping: a larger containing expression
     * can be unique (count 1) while a smaller piece nested inside it repeats
     * elsewhere - e.g. `x+i + 8*(y+i)` appears only once as a whole, but its
     * `x+i` sub-piece also appears separately in this same if's condition.
     * Stopping at the first pure match (right for the invariant hoist, which
     * wants the single largest hoistable chunk) would hide that repeat. */
    if ((n->kind == AST_BINARY || n->kind == AST_UNARY) && licm_is_pure_scalar_arith(n))
        licm_cse_bump(out, n, mod);

    if (n->kind == AST_BINARY || n->kind == AST_LOGAND || n->kind == AST_LOGOR) {
        licm_cse_collect(n->a, mod, out);
        licm_cse_collect(n->b, mod, out);
    } else if (n->kind == AST_UNARY) {
        licm_cse_collect(n->a, mod, out);
    } else if (n->kind == AST_INDEX) {
        licm_cse_collect(n->a, mod, out);
        licm_cse_collect(n->b, mod, out);
    }
}

/* Build `temp = expr;` as an AST_EXPR_STMT/AST_ASSIGN pair - an ordinary
 * statement, not a direct codegen call, since (unlike the invariant hoist)
 * this needs to run every iteration. */
static struct AstNode *licm_cse_make_temp_assign(const struct AstNode *expr, struct Sym *temp)
{
    struct AstNode *ident;
    struct AstNode *assign;
    struct AstNode *stmt;

    ident = ast_new(&g_ast_arena, AST_IDENT);
    ident->sval = ast_arena_strdup(&g_ast_arena, temp->name);
    ident->type = temp->type;

    assign = ast_new(&g_ast_arena, AST_ASSIGN);
    assign->op = '=';
    assign->a = ident;
    assign->b = (struct AstNode *)expr;
    assign->type = temp->type;

    stmt = ast_new(&g_ast_arena, AST_EXPR_STMT);
    stmt->a = assign;
    return stmt;
}

#define MAX_LICM_CSE_SCRATCH 128

/* True if `stmt` runs unconditionally on every iteration it's reached at all
 * - i.e. not inside an if's then/else. An if statement's own condition
 * qualifies (it always runs, whichever way it goes) but its guarded body
 * does not, so only the condition subtree is scanned for an if; anything
 * else is scanned in full. Used only to decide what counts toward CSE
 * profitability - see the design note above. */
static void licm_cse_collect_unconditional_stmt(const struct AstNode *stmt, const struct LicmModifiedNames *mod,
                                                struct LicmCseCandidateList *out)
{
    if (stmt == NULL)
        return;
    if (stmt->kind == AST_IF) {
        licm_cse_collect(stmt->a, mod, out);
        return;
    }
    licm_cse_collect(stmt, mod, out);
}

/* Walk the loop body's top-level statement list (or treat a single bare
 * statement body as a one-entry list) looking for a pure subexpression
 * repeated across two or more unconditionally-executed positions (see the
 * design note above for why guarded if-bodies don't count toward this).
 * Each qualifying candidate gets one compiler-temp, computed via a `temp =
 * expr;` statement spliced in once ahead of every original statement; every
 * occurrence of the candidate anywhere in the body (guarded bodies
 * included) is then rewritten to reference it. Returns `body` unchanged if
 * nothing qualifies. */
static struct AstNode *licm_apply_cse_to_stmt_list(const struct AstNode *body,
                                                   const struct LicmModifiedNames *mod)
{
    const struct AstNode *const *orig_list;
    const struct AstNode *single[1];
    int orig_len;
    struct AstNode *scratch[MAX_LICM_CSE_SCRATCH];
    int scratch_n;
    int i, j;
    struct AstNode *wrapper;
    struct LicmCseCandidateList cands;
    struct LicmCandidateList filtered;
    struct Sym *temps[MAX_LICM_CANDIDATES];

    if (body == NULL)
        return NULL;

    if (body->kind == AST_COMPOUND) {
        orig_list = (const struct AstNode *const *)body->list;
        orig_len = body->list_len;
    } else {
        single[0] = body;
        orig_list = single;
        orig_len = 1;
    }

    memset(&cands, 0, sizeof(cands));
    for (i = 0; i < orig_len; ++i)
        licm_cse_collect_unconditional_stmt(orig_list[i], mod, &cands);

    memset(&filtered, 0, sizeof(filtered));
    for (j = 0; j < cands.count; ++j)
        if (cands.items[j].count >= 2 && filtered.count < MAX_LICM_CANDIDATES)
            filtered.nodes[filtered.count++] = cands.items[j].node;

    if (filtered.count == 0 || filtered.count + orig_len > MAX_LICM_CSE_SCRATCH)
        return (struct AstNode *)body;

    scratch_n = 0;
    for (j = 0; j < filtered.count; ++j) {
        char tmp_name[24];
        /* Use static type inference, not the candidate node's own ->type
         * field directly: that field is reliably populated for a top-level
         * expression, but not necessarily for an arbitrary nested
         * subexpression - e.g. `y+i` used as a comparison operand
         * (`y+i < 8`) was observed to have ->type left at 0, even though
         * `x+i` used as an addend elsewhere in the same statement was fine.
         * A wrong/zero type here silently breaks unrelated fast paths
         * further downstream (the temp's declared type feeds
         * ast_value_is_plain_int, which gated a constant-multiply fast path
         * that fell back to a full __mulu call once the temp's type read as
         * 0). */
        sprintf(tmp_name, "#cse%d", g_func_pass.licm_seq++);
        temps[j] = add_local_alloc(tmp_name, ast_expr_type_for_sizeof(filtered.nodes[j]), 2);
        scratch[scratch_n++] = licm_cse_make_temp_assign(filtered.nodes[j], temps[j]);
    }

    for (i = 0; i < orig_len; ++i)
        scratch[scratch_n++] = licm_rewrite(orig_list[i], &filtered, temps);

    wrapper = ast_new(&g_ast_arena, AST_COMPOUND);
    wrapper->list = (struct AstNode **)ast_arena_alloc(&g_ast_arena,
        sizeof(struct AstNode *) * (size_t)(scratch_n > 0 ? scratch_n : 1));
    memcpy(wrapper->list, scratch, sizeof(struct AstNode *) * (size_t)scratch_n);
    wrapper->list_len = scratch_n;
    wrapper->list_cap = scratch_n;
    return wrapper;
}

/* Entry point: `for_node` is the AST_FOR node itself. First scans the
 * ORIGINAL condition/increment/body for modified names - so the
 * within-statement CSE pass can decline any candidate whose operands are
 * reassigned by an earlier statement in the very same iteration (see
 * licm_cse_bump; this is a distinct concern from the cross-iteration
 * invariance the pre-CSE candidates also need, and must be checked against
 * the body as the user wrote it, not the CSE-rewritten one, since the CSE
 * rewrite itself hasn't happened yet at this point) - then applies
 * within-statement CSE to the body's top-level if-statements (see above),
 * then re-scans the (now CSE-rewritten) condition/increment/body for
 * modified names - so the CSE pass's own inserted temp assignments are
 * correctly seen as "modified every iteration", never mistaken for loop-
 * invariant - and finds every distinct loop-invariant candidate remaining,
 * emitting each one's value once (side effect of this call - HL is
 * clobbered, matching every other hoist in this file) before the loop.
 * Returns a rewritten copy of the body to use in place of for_node->d, or
 * NULL if neither pass found anything, in which case the caller should keep
 * using the original body unchanged. */
struct AstNode *ast_licm_hoist_invariants(const struct AstNode *for_node)
{
    struct AstNode *cse_body;
    struct LicmModifiedNames pre_cse_mod;
    struct LicmModifiedNames mod;
    struct LicmCandidateList cands;
    struct Sym *temps[MAX_LICM_CANDIDATES];
    int i;

    if (for_node == NULL || for_node->d == NULL)
        return NULL;

    memset(&pre_cse_mod, 0, sizeof(pre_cse_mod));
    licm_scan_modified(for_node->b, &pre_cse_mod);
    licm_scan_modified(for_node->c, &pre_cse_mod);
    licm_scan_modified(for_node->d, &pre_cse_mod);
    cse_body = licm_apply_cse_to_stmt_list(for_node->d, &pre_cse_mod);

    memset(&mod, 0, sizeof(mod));
    licm_scan_modified(for_node->b, &mod);
    licm_scan_modified(for_node->c, &mod);
    licm_scan_modified(cse_body, &mod);
    if (mod.overflowed)
        return cse_body != for_node->d ? cse_body : NULL;

    memset(&cands, 0, sizeof(cands));
    licm_collect_candidates(cse_body, &mod, &cands);
    if (cands.count == 0)
        return cse_body != for_node->d ? cse_body : NULL;

    for (i = 0; i < cands.count; ++i) {
        char tmp_name[24];
        /* ast_expr_type_for_sizeof, not cands.nodes[i]->type directly - see
         * the identical comment in licm_apply_cse_to_stmt_list, where this
         * was found to matter in practice. */
        sprintf(tmp_name, "#licm%d", g_func_pass.licm_seq++);
        temps[i] = add_local_alloc(tmp_name, ast_expr_type_for_sizeof(cands.nodes[i]), 2);
        ast_gen_expr(cands.nodes[i]);   /* HL = the hoisted value, computed once */
        emit_store_hl_to_sym_direct(temps[i]);
    }

    return licm_rewrite(cse_body, &cands, temps);
}
