/*
 * dcc_array_narrow.c - detect int arrays whose every stored element value is
 * provably in [0,255], so their declared element type can be narrowed to
 * unsigned char before normal codegen runs.
 *
 * Byte arrays are already a completely ordinary, fully-supported C
 * construct in dcc - narrowing an array's type is the entire fix; no new
 * codegen is needed, since type_index_elem_size() and friends already
 * derive addressing arithmetic from the element type dynamically. The hard
 * part, and the only thing this file does, is proving the narrowing is
 * safe.
 *
 * Two independent questions have to be answered, both by lexical/AST
 * scanning rather than symbol-table-based analysis (matching
 * dcc_global_scan.c's approach for the analogous whole-file question):
 *
 *   1. Does this array's name ever escape as a bare pointer (passed to a
 *      function, assigned to a pointer, etc.)? If so, some other code this
 *      scan cannot see might store an unbounded value through it, so
 *      narrowing is declined outright. A bare occurrence of the name not
 *      immediately followed by '[' is treated as an escape.
 *
 *   2. Is every value ever stored into the array provably in [0,255]? This
 *      needs both an upper bound *and* non-negativity - storing a negative
 *      int into unsigned char wraps to a large positive value, which is not
 *      the same value read back, so an upper-bound-only proof is not
 *      sufic.ient. Values that are themselves expressions referencing other
 *      local scalars (not just literals) require bounding those scalars
 *      too, by tracing every place they are reassigned within the same
 *      scope - which can require mutual reasoning (variable X's bound
 *      depends on array A's values, and A's bound depends on X), handled
 *      by hypothesize-then-verify: assume every name in the dependency
 *      closure has the target property, then check every reassignment site
 *      for every name in the closure is consistent with that assumption.
 *      This is a coinductive safety argument, not an inductive one - it is
 *      valid because the property being checked (nonneg-and-bounded) is
 *      preserved by the recognized operators, not because it needs a base
 *      case to build up from.
 *
 * The rule set for "is this expression nonneg and bounded by K" is
 * deliberately small and conservative: literals, +, *, / (bound derived
 * from operands, with / requiring a positive divisor), % (bounded by the
 * divisor's own bound minus 1, and only when the dividend is separately
 * proven nonneg), a bare reference to another name in the assumption set,
 * an index into an array in the assumption set, and a call to a
 * no-argument function whose entire body is a single return statement
 * (recursively bounded the same way). Anything else - unrecognized
 * operators, a variable outside the same function, a function with
 * parameters or a body of more than one statement - is an unconditional
 * decline, never a guess: this can only under-narrow (miss an
 * optimization), never over-narrow (corrupt a value), which is the
 * required safety direction throughout this codebase's other lexical
 * scans (see local_name_used_ahead, local_name_address_taken_ahead,
 * scan_global_write_info).
 */

#include "dcc.h"
#include "dcc_ast.h"

#define MAX_NARROW_GROUP 16
#define NARROW_TARGET_BOUND 255
#define NARROW_FAIL(st) do { (st)->ok = 0; } while (0)

struct NarrowGroup {
    char names[MAX_NARROW_GROUP][64];
    int is_array[MAX_NARROW_GROUP];
    int n;
};

static void narrow_copy_name(char *dst, size_t dstsz, const char *src)
{
    size_t len = strlen(src);
    if (len > dstsz - 1)
        len = dstsz - 1;
    memcpy(dst, src, len);
    dst[len] = 0;
}

static int narrow_group_index(struct NarrowGroup *g, const char *name)
{
    int i;
    for (i = 0; i < g->n; ++i)
        if (!strcmp(g->names[i], name))
            return i;
    return -1;
}

static int narrow_group_add(struct NarrowGroup *g, const char *name, int is_array)
{
    int i;
    i = narrow_group_index(g, name);
    if (i >= 0)
        return i;
    if (g->n >= MAX_NARROW_GROUP)
        return -1;
    narrow_copy_name(g->names[g->n], sizeof(g->names[g->n]), name);
    g->is_array[g->n] = is_array;
    return g->n++;
}

/* A no-argument, single-return-statement function's body, for recursively
 * bounding a call like rndrm(). Mirrors the same "simple substitution body"
 * shape already recognized for static inline functions, but this walk is
 * independent of (and does not require) the inline machinery - it works for
 * any such function, static inline or not. */
static const struct AstNode *narrow_get_noarg_return_expr(const char *fname)
{
    struct Sym *fn;

    fn = find_global(fname);
    if (fn == NULL || fn->storage != SC_FUNC)
        return NULL;
    return fn->narrow_return_expr;
}

/* A guard fact recording that, at the current point in the walk, `name` is
 * known to be strictly greater than `min_exclusive` - e.g. {N, 9} inside
 * `while (N > 9) { ... }`. Every group member implicitly has min_exclusive
 * -1 (i.e. >= 0, the ordinary nonneg hypothesis) even with no facts in
 * scope; entries here only ever *tighten* that. */
struct NarrowFacts {
    char names[8][64];
    int min_exclusive[8];
    int n;
};

/* Searches most-recently-pushed first, so a later fact about the same name
 * (from a tighter loop guard, or invalidated by a subsequent reassignment -
 * see narrow_walk_seq's sequential literal-assignment tracking) always
 * shadows an earlier, now-stale one rather than being masked by it. */
static int narrow_facts_min(struct NarrowFacts *f, const char *name)
{
    int i;
    for (i = f->n - 1; i >= 0; --i)
        if (!strcmp(f->names[i], name))
            return f->min_exclusive[i];
    return -1;
}

static void narrow_facts_push(struct NarrowFacts *f, const char *name, int min_exclusive,
                              struct NarrowFacts *out_saved)
{
    *out_saved = *f;
    if (f->n < 8) {
        narrow_copy_name(f->names[f->n], sizeof(f->names[f->n]), name);
        f->min_exclusive[f->n] = min_exclusive;
        f->n++;
    }
}

/* Appends a fact with no save/restore - used for sequential, persistent
 * tracking of "the last thing this name was set to" as narrow_walk_seq
 * processes sibling statements in one block (see there), not for a
 * lexically-scoped guard. A later append for the same name naturally
 * shadows an earlier one via narrow_facts_min's most-recent-first search,
 * which is exactly how a literal-assignment fact gets invalidated once the
 * name is reassigned to something else. */
static void narrow_facts_append(struct NarrowFacts *f, const char *name, int min_exclusive)
{
    if (f->n < 8) {
        narrow_copy_name(f->names[f->n], sizeof(f->names[f->n]), name);
        f->min_exclusive[f->n] = min_exclusive;
        f->n++;
    }
}

/* Set once per verification call so narrow_expr_bound's '-' rule (the only
 * rule that needs a numeric fact rather than just group membership) can
 * consult the facts active at that exact point in the walk, without
 * threading a new parameter through the whole recursive rule engine. NULL
 * outside of a walk (e.g. nothing sets it before the top-level entry
 * point's own bookkeeping) makes '-' simply decline, the safe default. */
static struct NarrowFacts *g_narrow_active_facts;

static int narrow_expr_bound(const struct AstNode *n, struct NarrowGroup *g,
                             int *out_nonneg, int *out_bound);

static int narrow_expr_nonneg_only(const struct AstNode *n, struct NarrowGroup *g)
{
    int nonneg, bound;
    if (!narrow_expr_bound(n, g, &nonneg, &bound))
        return 0;
    return nonneg;
}

static int narrow_expr_bound(const struct AstNode *n, struct NarrowGroup *g,
                             int *out_nonneg, int *out_bound)
{
    int an, ab, bn, bb;
    int idx;

    if (n == NULL)
        return 0;

    switch (n->kind) {
    case AST_INT_LIT:
        if (n->ival < 0)
            return 0;
        *out_nonneg = 1;
        *out_bound = (int)n->ival;
        return 1;

    case AST_IDENT:
        idx = narrow_group_index(g, n->sval);
        if (idx < 0)
            return 0;
        /* Hypothesized (or already-proven) member of the dependency group:
         * trust the target property we are trying to establish for the
         * whole group. */
        *out_nonneg = 1;
        *out_bound = NARROW_TARGET_BOUND;
        return 1;

    case AST_INDEX:
        /* name[idx]: only trusted when the base is a plain identifier that
         * is itself a member of the group (an array already hypothesized
         * nonneg-and-bounded); the index expression itself does not need
         * to be bounded to read from it. */
        if (n->a == NULL || n->a->kind != AST_IDENT)
            return 0;
        idx = narrow_group_index(g, n->a->sval);
        if (idx < 0 || !g->is_array[idx])
            return 0;
        *out_nonneg = 1;
        *out_bound = NARROW_TARGET_BOUND;
        return 1;

    case AST_BINARY:
        if (n->op == '+') {
            if (!narrow_expr_bound(n->a, g, &an, &ab) ||
                !narrow_expr_bound(n->b, g, &bn, &bb))
                return 0;
            *out_nonneg = 1;
            *out_bound = ab + bb;
            return 1;
        }
        if (n->op == '*') {
            if (!narrow_expr_bound(n->a, g, &an, &ab) ||
                !narrow_expr_bound(n->b, g, &bn, &bb))
                return 0;
            *out_nonneg = 1;
            *out_bound = ab * bb;
            return 1;
        }
        if (n->op == '/') {
            /* Integer division only shrinks a nonneg dividend toward zero;
             * the dividend's own bound is always a safe (if loose) bound
             * for the quotient. The divisor just needs to be positive. */
            if (!narrow_expr_bound(n->a, g, &an, &ab))
                return 0;
            if (n->b == NULL || n->b->kind != AST_IDENT)
                return 0;
            idx = narrow_group_index(g, n->b->sval);
            if (idx < 0)
                return 0;
            *out_nonneg = 1;
            *out_bound = ab;
            return 1;
        }
        if (n->op == '%') {
            /* Bounded by the divisor's own bound minus one, but only valid
             * (per C's truncating division) when the dividend is nonneg -
             * a negative dividend can make the result negative regardless
             * of the divisor. */
            if (!narrow_expr_nonneg_only(n->a, g))
                return 0;
            if (!narrow_expr_bound(n->b, g, &bn, &bb) || !bn || bb <= 0)
                return 0;
            *out_nonneg = 1;
            *out_bound = bb - 1;
            return 1;
        }
        if (n->op == '-' && n->a != NULL && n->a->kind == AST_IDENT &&
            n->b != NULL && n->b->kind == AST_INT_LIT) {
            /* A group member's own bound/nonneg hypothesis alone is too
             * weak to bound a subtraction (it could be exactly 0). Only
             * recognized when g_narrow_active_facts has a numeric fact for
             * the identifier - e.g. a sequential "just assigned from this
             * literal" fact (see narrow_walk_seq) or a loop guard - strong
             * enough that subtracting the literal cannot go negative. */
            int fmin;
            if (g_narrow_active_facts == NULL)
                return 0;
            fmin = narrow_facts_min(g_narrow_active_facts, n->a->sval);
            if (fmin < 0 || n->b->ival < 0 || n->b->ival > fmin + 1)
                return 0;
            *out_nonneg = 1;
            *out_bound = (fmin + 1) - (int)n->b->ival;
            return 1;
        }
        return 0;

    case AST_CALL: {
        const struct AstNode *ret_expr;
        if (n->a == NULL || n->a->kind != AST_IDENT || n->list_len != 0)
            return 0;
        ret_expr = narrow_get_noarg_return_expr(n->a->sval);
        if (ret_expr == NULL)
            return 0;
        /* The callee's own body is analyzed with an EMPTY group of its
         * own - it has no parameters, so nothing in the caller's group
         * could leak in incorrectly, and the callee's return expression
         * must stand on its own (its own literals/operators/further
         * no-arg calls). */
        {
            struct NarrowGroup empty;
            empty.n = 0;
            return narrow_expr_bound(ret_expr, &empty, out_nonneg, out_bound);
        }
    }

    default:
        return 0;
    }
}

/* Does `cond` recognizably establish `name > K` for some nonneg literal K,
 * for the duration of whatever it guards (a while/for body, or a for's own
 * increment clause)? Only the two textual orderings `name > K` / `K < name`
 * are recognized. */
static int narrow_cond_lower_bounds(const struct AstNode *cond, const char *name, int *out_min)
{
    if (cond == NULL || cond->kind != AST_BINARY)
        return 0;
    if (cond->op == '>' && cond->a != NULL && cond->a->kind == AST_IDENT &&
        !strcmp(cond->a->sval, name) && cond->b != NULL && cond->b->kind == AST_INT_LIT &&
        cond->b->ival >= 0) {
        *out_min = (int)cond->b->ival;
        return 1;
    }
    if (cond->op == '<' && cond->b != NULL && cond->b->kind == AST_IDENT &&
        !strcmp(cond->b->sval, name) && cond->a != NULL && cond->a->kind == AST_INT_LIT &&
        cond->a->ival >= 0) {
        *out_min = (int)cond->a->ival;
        return 1;
    }
    return 0;
}

/* Is `n` a decrement (`--X`/`X--`) of a plain identifier, and if so, which
 * name? Returns NULL if not a decrement of a bare identifier. */
static const char *narrow_decrement_target(const struct AstNode *n)
{
    if (n == NULL)
        return NULL;
    if (n->kind == AST_UNARY && n->op == TOK_DEC && n->a != NULL && n->a->kind == AST_IDENT)
        return n->a->sval;
    if (n->kind == AST_POSTFIX && n->op == TOK_DEC && n->a != NULL && n->a->kind == AST_IDENT)
        return n->a->sval;
    return NULL;
}

#define MAX_NARROW_WRITES 48

struct NarrowWrite {
    char name[64];
    int is_array;
    const struct AstNode *rhs; /* NULL for a plain self-decrement (X-- / --X) */
    struct NarrowFacts facts;  /* snapshot of facts active when recorded, for
                                * narrow_expr_bound's '-' rule during final
                                * verification (see g_narrow_active_facts) */
};

struct NarrowWalkState {
    struct NarrowGroup *group;
    struct NarrowWrite writes[MAX_NARROW_WRITES];
    int nwrites;
    int ok;
    /* Discovery-phase walks (the group is still growing) must not fail
     * just because some name isn't a group member yet or lacks a guard
     * fact yet - the same statement is re-walked, with a larger group and
     * fresh facts, once fixpoint discovery adds it. Only structurally
     * unsupported shapes (compound assignment, wrong array-ness, an
     * unrecognized preceding-statement shape) fail regardless of this
     * flag. The final verification walk (once the group is stable) runs
     * with this cleared, so anything still unresolved at that point is a
     * genuine decline. */
    int discovery;
};

static void narrow_record_write(struct NarrowWalkState *st, const char *name, int is_array,
                                const struct AstNode *rhs, const struct NarrowFacts *facts)
{
    if (st->nwrites >= MAX_NARROW_WRITES) {
        NARROW_FAIL(st);
        return;
    }
    narrow_copy_name(st->writes[st->nwrites].name, sizeof(st->writes[st->nwrites].name), name);
    st->writes[st->nwrites].is_array = is_array;
    st->writes[st->nwrites].rhs = rhs;
    st->writes[st->nwrites].facts = *facts;
    st->nwrites++;
}

static void narrow_walk_seq(struct NarrowWalkState *st, const struct AstNode *body,
                            struct NarrowFacts *facts);

/* Is fmin (a min_exclusive fact, or -1 if none) strong enough to guarantee
 * strictly positive (> 0), as opposed to merely nonneg (>= 0)? Only used to
 * validate the self-guarding while(--X)/while(X--) idiom, which needs the
 * stronger guarantee - see the AST_WHILE case below. */
static int narrow_is_strictly_positive_fact(int fmin)
{
    return fmin >= 0;
}

/* A bare expression that is not itself a statement - a for-loop's init or
 * post clause, or an ordinary expression-statement's expression. Handles
 * exactly three shapes for a group member:
 *   - `X--` / `--X`: a guarded decrement (needs a lower-bound fact in scope
 *     for X already, from an enclosing while/for condition).
 *   - `X = Y--;` / `X = --Y;`: X takes Y's value (pre- or post-decrement
 *     respectively - both are bounded the same way, via Y's own group
 *     membership) while Y is separately decremented (same guard
 *     requirement as the bare-decrement case above).
 *   - `X = EXPR;` (any other RHS): recorded for later verification via
 *     narrow_expr_bound.
 * Anything else touching a group member is declined. */
static void narrow_walk_bare_expr(struct NarrowWalkState *st, const struct AstNode *e,
                                  struct NarrowFacts *facts)
{
    const char *dec_name;
    int idx;

    if (e == NULL || !st->ok)
        return;

    dec_name = narrow_decrement_target(e);
    if (dec_name != NULL) {
        idx = narrow_group_index(st->group, dec_name);
        if (idx < 0 || st->group->is_array[idx])
            return; /* decrementing something outside our group: irrelevant */
        if (narrow_facts_min(facts, dec_name) < 0) {
            /* No guard in scope proving dec_name > (some K >= 0): cannot
             * show this decrement stays nonneg. */
            NARROW_FAIL(st);
        }
        return;
    }

    if (e->kind == AST_ASSIGN) {
        const char *target_name;
        int is_array;

        if (e->op != '=') {
            if (e->a != NULL && (e->a->kind == AST_IDENT || e->a->kind == AST_INDEX)) {
                const struct AstNode *base = (e->a->kind == AST_INDEX) ? e->a->a : e->a;
                if (base != NULL && base->kind == AST_IDENT &&
                    narrow_group_index(st->group, base->sval) >= 0)
                    NARROW_FAIL(st); /* compound assignment: not a recognized rule */
            }
            return;
        }

        if (e->a != NULL && e->a->kind == AST_IDENT) {
            target_name = e->a->sval;
            is_array = 0;
        } else if (e->a != NULL && e->a->kind == AST_INDEX &&
                   e->a->a != NULL && e->a->a->kind == AST_IDENT) {
            target_name = e->a->a->sval;
            is_array = 1;
        } else {
            return;
        }

        idx = narrow_group_index(st->group, target_name);
        if (idx < 0)
            return; /* assignment to something outside our group */
        if (st->group->is_array[idx] != is_array) {
            NARROW_FAIL(st);
            return;
        }

        {
            const char *rhs_dec = narrow_decrement_target(e->b);
            if (rhs_dec != NULL) {
                /* `target = OTHER--;` / `target = --OTHER;`: two effects -
                 * target takes OTHER's value (pre- or post-, both bounded
                 * identically via OTHER's own group membership), and OTHER
                 * itself is decremented (same guard requirement as a bare
                 * decrement). If OTHER is not yet a discovered group
                 * member, do not fail here - record the write anyway (as
                 * OTHER's own identifier, so the dependency-discovery step
                 * finds it) and defer the guard check to a later fixpoint
                 * pass, once OTHER has been added and this same statement
                 * is walked again with it recognized. */
                int ridx = narrow_group_index(st->group, rhs_dec);
                if (ridx >= 0) {
                    if (st->group->is_array[ridx]) {
                        NARROW_FAIL(st);
                        return;
                    }
                    if (narrow_facts_min(facts, rhs_dec) < 0) {
                        NARROW_FAIL(st);
                        return;
                    }
                }
                narrow_record_write(st, target_name, is_array, e->b->a, facts);
                return;
            }
        }

        narrow_record_write(st, target_name, is_array, e->b, facts);
        return;
    }

    /* Any other bare expression: only a concern if it references a group
     * member's address; the escape scan (run separately, see
     * try_narrow_int_array) already covers that across the whole scope. */
}

/* A single statement, with access to its immediate predecessor in the same
 * sequence (prev may be NULL) - needed only for the self-guarding
 * while(--X)/while(X--) case, which must look at "what was X set to just
 * before this loop". */
static void narrow_walk_stmt(struct NarrowWalkState *st, const struct AstNode *stmt,
                             const struct AstNode *prev, struct NarrowFacts *facts)
{
    if (stmt == NULL || !st->ok)
        return;

    switch (stmt->kind) {
    case AST_COMPOUND:
        narrow_walk_seq(st, stmt, facts);
        return;

    case AST_IF:
        narrow_walk_stmt(st, stmt->b, NULL, facts);
        narrow_walk_stmt(st, stmt->c, NULL, facts);
        return;

    case AST_WHILE: {
        const char *dec_name;
        int min_ex;
        struct NarrowFacts saved;

        dec_name = narrow_decrement_target(stmt->a);
        if (dec_name != NULL && narrow_group_index(st->group, dec_name) >= 0) {
            /* Self-guarding count-down loop: safe only if dec_name is
             * already proven strictly positive by the statement right
             * before this loop (its own value entering the loop). The
             * shape check (is the preceding statement even the right
             * form?) is structural and never becomes true or false later,
             * so it always fails hard; the bound/fact checks depend on
             * OTHER group members being discovered yet, so during
             * discovery they only skip re-verification (still walking the
             * body, so nested writes are found) rather than failing the
             * whole analysis - the final strict pass enforces them. */
            int nonneg, bound;
            int prev_min;
            const struct AstNode *rhs_for_bound;
            const char *prev_dec;
            int guard_ok;

            if (prev == NULL || prev->kind != AST_EXPR_STMT || prev->a == NULL ||
                prev->a->kind != AST_ASSIGN || prev->a->op != '=' ||
                prev->a->a == NULL || prev->a->a->kind != AST_IDENT ||
                strcmp(prev->a->a->sval, dec_name) != 0) {
                NARROW_FAIL(st);
                return;
            }

            /* prev->a->b (the preceding assignment's RHS) may itself be a
             * bundled decrement (`n = N--;`) rather than a plain
             * identifier - narrow_expr_bound has no AST_POSTFIX/AST_UNARY
             * case (it is not a value-bearing shape in general), so bound
             * the decremented identifier directly in that case, exactly
             * as narrow_walk_bare_expr already records it. */
            prev_dec = narrow_decrement_target(prev->a->b);
            rhs_for_bound = (prev_dec != NULL) ? prev->a->b->a : prev->a->b;

            guard_ok = narrow_expr_bound(rhs_for_bound, st->group, &nonneg, &bound) && nonneg;
            if (guard_ok) {
                /* The preceding assignment's RHS must itself be known
                 * strictly positive, not merely nonneg - a bare
                 * group-member hypothesis alone only gives nonneg. Both a
                 * plain copy (`n = N;`) and the bundled `n = N--;` shape
                 * reduce to "look up N's own fact" here. */
                if (prev->a->b->kind == AST_IDENT)
                    prev_min = narrow_facts_min(facts, prev->a->b->sval);
                else if (prev_dec != NULL)
                    prev_min = narrow_facts_min(facts, prev_dec);
                else
                    prev_min = -1;
                guard_ok = narrow_is_strictly_positive_fact(prev_min);
            }
            if (!guard_ok && !st->discovery) {
                NARROW_FAIL(st);
                return;
            }
            /* Either validated, or still discovering (dependencies not
             * fully known this pass) - walk the body regardless, so
             * writes/dependencies inside it still get found. */
            narrow_walk_stmt(st, stmt->b, NULL, facts);
            return;
        }

        {
            int handled = 0;
            int i;
            for (i = 0; i < st->group->n && !handled; ++i) {
                if (narrow_cond_lower_bounds(stmt->a, st->group->names[i], &min_ex)) {
                    narrow_facts_push(facts, st->group->names[i], min_ex, &saved);
                    narrow_walk_stmt(st, stmt->b, NULL, facts);
                    *facts = saved;
                    handled = 1;
                }
            }
            if (!handled)
                narrow_walk_stmt(st, stmt->b, NULL, facts);
        }
        return;
    }

    case AST_FOR: {
        struct NarrowFacts saved;
        int min_ex;
        int handled;
        int i;

        narrow_walk_bare_expr(st, stmt->a, facts);

        handled = 0;
        for (i = 0; i < st->group->n && !handled; ++i) {
            if (narrow_cond_lower_bounds(stmt->b, st->group->names[i], &min_ex)) {
                narrow_facts_push(facts, st->group->names[i], min_ex, &saved);
                narrow_walk_stmt(st, stmt->d, NULL, facts);
                narrow_walk_bare_expr(st, stmt->c, facts);
                *facts = saved;
                handled = 1;
            }
        }
        if (!handled) {
            narrow_walk_stmt(st, stmt->d, NULL, facts);
            narrow_walk_bare_expr(st, stmt->c, facts);
        }
        return;
    }

    case AST_EXPR_STMT:
        narrow_walk_bare_expr(st, stmt->a, facts);
        return;

    default:
        return;
    }
}

/* After walking a statement, record (or invalidate) a sequential fact about
 * a plain assignment to a group member: `X = LITERAL;` establishes "X is
 * exactly LITERAL" (as a min_exclusive fact, LITERAL - 1) for the
 * remainder of this same block, until X is next reassigned to anything
 * else (a plain non-literal assignment, or a decrement) - appending a
 * fresh, weaker (-1) fact for X at that point, which narrow_facts_min's
 * most-recent-first search then correctly prefers over the stale one. This
 * is what lets `N = DIGITS_TO_FIND; ... n = N - 1;` prove N - 1 is nonneg
 * without a general (and generally unsound) subtraction rule. */
static void narrow_seq_update_literal_fact(struct NarrowWalkState *st, const struct AstNode *stmt,
                                           struct NarrowFacts *facts)
{
    const struct AstNode *e;
    const char *dec_name;

    if (stmt == NULL || stmt->kind != AST_EXPR_STMT || stmt->a == NULL)
        return;
    e = stmt->a;

    dec_name = narrow_decrement_target(e);
    if (dec_name != NULL) {
        if (narrow_group_index(st->group, dec_name) >= 0)
            narrow_facts_append(facts, dec_name, -1);
        return;
    }

    if (e->kind == AST_ASSIGN && e->a != NULL && e->a->kind == AST_IDENT &&
        narrow_group_index(st->group, e->a->sval) >= 0) {
        if (e->op == '=' && e->b != NULL && e->b->kind == AST_INT_LIT && e->b->ival >= 0)
            narrow_facts_append(facts, e->a->sval, (int)e->b->ival - 1);
        else
            narrow_facts_append(facts, e->a->sval, -1);
    }
}

static void narrow_walk_seq(struct NarrowWalkState *st, const struct AstNode *body,
                            struct NarrowFacts *facts)
{
    int i;
    if (body == NULL || body->kind != AST_COMPOUND)
        return;
    for (i = 0; i < body->list_len && st->ok; ++i) {
        narrow_walk_stmt(st, body->list[i], (i > 0) ? body->list[i - 1] : NULL, facts);
        narrow_seq_update_literal_fact(st, body->list[i], facts);
    }
}

/* Does `name` appear as the divisor (b operand) of a '%' anywhere in this
 * subtree? A '%' result is bounded by its divisor, so a group member used
 * this way needs its OWN value kept within the target bound - unlike a
 * member used only as a '%' dividend (needs just nonneg) or in further
 * arithmetic that never itself gets stored into a group array (e.g. e.c's
 * `x` in `x = 10 * a[n-1] + x / n;` - x's own magnitude is never read
 * into an array, so it never needs to be byte-sized, only nonneg). */
static int narrow_name_used_as_percent_divisor(const struct AstNode *n, const char *name)
{
    int i;
    if (n == NULL)
        return 0;
    if (n->kind == AST_BINARY && n->op == '%' && n->b != NULL && n->b->kind == AST_IDENT &&
        !strcmp(n->b->sval, name))
        return 1;
    if (narrow_name_used_as_percent_divisor(n->a, name) ||
        narrow_name_used_as_percent_divisor(n->b, name) ||
        narrow_name_used_as_percent_divisor(n->c, name) ||
        narrow_name_used_as_percent_divisor(n->d, name))
        return 1;
    for (i = 0; i < n->list_len; ++i)
        if (narrow_name_used_as_percent_divisor(n->list[i], name))
            return 1;
    return 0;
}

/* Does this group member's own value need to stay within the target
 * bound, as opposed to merely nonneg? Always true for an array (that is
 * the entire point); for a scalar, true only if it is ever used as a '%'
 * divisor (whose result's bound depends on the divisor's own bound) or
 * stored directly (unwrapped) into a group array - the two ways a
 * scalar's own magnitude, not just its sign, can flow into an array
 * element's value. */
static int narrow_member_needs_bound(struct NarrowGroup *g, struct NarrowWalkState *st, int idx)
{
    int i;
    if (g->is_array[idx])
        return 1;
    for (i = 0; i < st->nwrites; ++i) {
        if (st->writes[i].rhs == NULL)
            continue;
        if (narrow_name_used_as_percent_divisor(st->writes[i].rhs, g->names[idx]))
            return 1;
        if (st->writes[i].is_array && st->writes[i].rhs->kind == AST_IDENT &&
            !strcmp(st->writes[i].rhs->sval, g->names[idx]))
            return 1;
    }
    return 0;
}

/* Does `name` appear anywhere at all in this subtree? Used only to check
 * under a `&` - any reference there is an escape regardless of shape. */
static int narrow_tree_references_name(const struct AstNode *n, const char *name)
{
    int i;
    if (n == NULL)
        return 0;
    if (n->kind == AST_IDENT && !strcmp(n->sval, name))
        return 1;
    if (narrow_tree_references_name(n->a, name) || narrow_tree_references_name(n->b, name) ||
        narrow_tree_references_name(n->c, name) || narrow_tree_references_name(n->d, name))
        return 1;
    for (i = 0; i < n->list_len; ++i)
        if (narrow_tree_references_name(n->list[i], name))
            return 1;
    return 0;
}

/* Does `name` ever escape as a writable alias this analysis cannot see -
 * `&name` anywhere (scalars and arrays alike), or (arrays only) a bare
 * occurrence not immediately indexed, which decays to a pointer? */
static int narrow_name_escapes(const struct AstNode *n, const char *name, int is_array)
{
    int i;
    if (n == NULL)
        return 0;

    if (n->kind == AST_UNARY && n->op == '&' && narrow_tree_references_name(n->a, name))
        return 1;

    if (n->kind == AST_IDENT && !strcmp(n->sval, name))
        return is_array;

    if (is_array && n->kind == AST_INDEX && n->a != NULL && n->a->kind == AST_IDENT &&
        !strcmp(n->a->sval, name))
        return narrow_name_escapes(n->b, name, is_array);

    if (narrow_name_escapes(n->a, name, is_array) || narrow_name_escapes(n->b, name, is_array) ||
        narrow_name_escapes(n->c, name, is_array) || narrow_name_escapes(n->d, name, is_array))
        return 1;
    for (i = 0; i < n->list_len; ++i)
        if (narrow_name_escapes(n->list[i], name, is_array))
            return 1;
    return 0;
}

/* Collects identifiers referenced by `n` that are not yet in `known`, for
 * worklist-style dependency discovery. An AST_INDEX's base is recorded as
 * an array dependency; any other identifier as a scalar. A no-argument
 * call's own body is analyzed in isolation by narrow_expr_bound, so it
 * contributes no dependency here. */
static void narrow_collect_deps(const struct AstNode *n, struct NarrowGroup *known,
                                char deps[][64], int *dep_is_array, int *n_deps, int max_deps)
{
    int i;
    if (n == NULL || *n_deps >= max_deps)
        return;

    if (n->kind == AST_IDENT) {
        if (narrow_group_index(known, n->sval) < 0) {
            for (i = 0; i < *n_deps; ++i)
                if (!strcmp(deps[i], n->sval))
                    return;
            narrow_copy_name(deps[*n_deps], 64, n->sval);
            dep_is_array[*n_deps] = 0;
            (*n_deps)++;
        }
        return;
    }

    if (n->kind == AST_INDEX) {
        if (n->a != NULL && n->a->kind == AST_IDENT && narrow_group_index(known, n->a->sval) < 0) {
            int dup = 0;
            for (i = 0; i < *n_deps; ++i)
                if (!strcmp(deps[i], n->a->sval)) { dup = 1; break; }
            if (!dup && *n_deps < max_deps) {
                narrow_copy_name(deps[*n_deps], 64, n->a->sval);
                dep_is_array[*n_deps] = 1;
                (*n_deps)++;
            }
        }
        narrow_collect_deps(n->b, known, deps, dep_is_array, n_deps, max_deps);
        return;
    }

    if (n->kind == AST_CALL)
        return;

    narrow_collect_deps(n->a, known, deps, dep_is_array, n_deps, max_deps);
    narrow_collect_deps(n->b, known, deps, dep_is_array, n_deps, max_deps);
}

/* Top-level entry: attempt to prove every value ever stored into
 * `arr_name` (an int array) within `scope` (a compound statement - the
 * enclosing function's body) is in [0,255]. Returns 1 if safe to narrow
 * its element type to unsigned char, 0 if declined. */
int narrow_array_is_byte_safe(const struct AstNode *scope, const char *arr_name)
{
    struct NarrowGroup group;
    struct NarrowWalkState st;
    struct NarrowFacts facts;
    int pass;
    int i;

    if (scope == NULL || scope->kind != AST_COMPOUND)
        return 0;

    group.n = 0;
    if (narrow_group_add(&group, arr_name, 1) < 0)
        return 0;

    for (pass = 0; pass < MAX_NARROW_GROUP + 1; ++pass) {
        char deps[MAX_NARROW_GROUP][64];
        int dep_is_array[MAX_NARROW_GROUP];
        int n_deps;
        int j;
        int group_grew;

        st.group = &group;
        st.nwrites = 0;
        st.ok = 1;
        st.discovery = 1;
        facts.n = 0;
        narrow_walk_seq(&st, scope, &facts);
        if (!st.ok)
            return 0;

        n_deps = 0;
        for (i = 0; i < st.nwrites; ++i)
            if (st.writes[i].rhs != NULL)
                narrow_collect_deps(st.writes[i].rhs, &group, deps, dep_is_array, &n_deps,
                                    MAX_NARROW_GROUP);

        group_grew = 0;
        for (j = 0; j < n_deps; ++j) {
            int before = group.n;
            if (narrow_group_add(&group, deps[j], dep_is_array[j]) < 0)
                return 0;
            if (group.n > before)
                group_grew = 1;
        }
        if (!group_grew)
            break;
    }

    for (i = 0; i < group.n; ++i)
        if (narrow_name_escapes(scope, group.names[i], group.is_array[i]))
            return 0;

    st.group = &group;
    st.nwrites = 0;
    st.ok = 1;
    st.discovery = 0;
    facts.n = 0;
    narrow_walk_seq(&st, scope, &facts);
    if (!st.ok)
        return 0;

    for (i = 0; i < st.nwrites; ++i) {
        int nonneg, bound;
        int member_idx;
        if (st.writes[i].rhs == NULL)
            continue; /* pure guarded decrement, already validated during the walk */
        g_narrow_active_facts = &st.writes[i].facts;
        if (!narrow_expr_bound(st.writes[i].rhs, &group, &nonneg, &bound)) {
            g_narrow_active_facts = NULL;
            return 0;
        }
        if (!nonneg) {
            g_narrow_active_facts = NULL;
            return 0;
        }
        member_idx = narrow_group_index(&group, st.writes[i].name);
        if (member_idx >= 0 && narrow_member_needs_bound(&group, &st, member_idx) &&
            bound > NARROW_TARGET_BOUND) {
            g_narrow_active_facts = NULL;
            return 0;
        }
    }
    g_narrow_active_facts = NULL;

    return 1;
}
