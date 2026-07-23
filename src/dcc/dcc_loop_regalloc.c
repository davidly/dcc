/*
 * dcc_loop_regalloc.c - loop-scoped BC register promotion.
 *
 * dcc_func.c already promotes at most one read-only parameter into BC for a
 * whole function's lifetime (find_bc_regalloc_candidate,
 * try_speculative_bc_regalloc_function_body). This file generalizes the
 * same underlying trick - "this scalar's stack slot never changes across
 * the span we care about, so keep its value in BC instead of reloading it
 * on every reference" - from whole-function scope down to a single loop, so
 * a function that has a call elsewhere (disqualifying it from the whole-
 * function scheme entirely) can still benefit if its hot loop itself has
 * none, and so body-local candidates (excluded from the whole-function
 * scheme because their Sym isn't stable across dcc_func.c's separate scan/
 * codegen passes) become eligible too, since this mechanism never leaves
 * the one codegen pass that already has a live, stable Sym* for them.
 *
 * Candidate eligibility reuses dcc_licm.c's own conservative modified-name
 * scan (licm_scan_modified) verbatim: a candidate must be referenced in the
 * loop's condition/increment/body but never appear in that scan's result
 * set (assigned, incremented/decremented, or address-taken), and the scan
 * overflowing (a call, nested loop, switch, or goto anywhere in the loop)
 * declines promoting anything in this loop at all - exactly the same bar
 * LICM itself already holds its own hoists to, reused rather than
 * reinvented for a second time in this file.
 *
 * Verification of a chosen candidate reuses dcc_func.c's own
 * regalloc_buffer_finalize unmodified: it scans a buffer of emitted
 * assembly text for anything that clobbers b/c/bc without dcc's own
 * recognized load/store idioms, tracking loop back-edges via label/jump
 * text so a linear single-pass scan still catches a hazard that would only
 * appear on a loop's second-and-later iteration. Nothing in that function
 * assumes its buffer is a whole function body rather than one loop's own
 * emitted span - "any call anywhere in this text" is exactly the right
 * question whether "this text" is a function or a loop.
 */
#include "dcc.h"
#include "dcc_ast.h"

#define LOOP_REGALLOC_MIN_REFS 2
#define MAX_LOOP_IDENT_COUNTS 32

struct LoopIdentCount {
    const char *name;
    int count;
};

struct LoopIdentCounts {
    struct LoopIdentCount items[MAX_LOOP_IDENT_COUNTS];
    int n;
};

static void loop_ident_bump(struct LoopIdentCounts *ic, const char *name)
{
    int i;

    if (name == NULL)
        return;
    for (i = 0; i < ic->n; ++i) {
        if (strcmp(ic->items[i].name, name) == 0) {
            ic->items[i].count++;
            return;
        }
    }
    if (ic->n < MAX_LOOP_IDENT_COUNTS) {
        ic->items[ic->n].name = name;
        ic->items[ic->n].count = 1;
        ic->n++;
    }
    /* Overflow: this specific name just never becomes a counted candidate.
     * Always safe to under-count - it can only lose a candidate, never
     * promote one that shouldn't be. */
}

/* Counts every AST_IDENT reference (read or written - a written one is
 * filtered out separately via the modified-names set) anywhere in `n`.
 * Same node-kind coverage as dcc_licm.c's licm_scan_modified on purpose:
 * the caller only ever invokes this after licm_scan_modified has already
 * proven (mod.overflowed == 0) that this exact subtree contains nothing
 * outside this case list, so there is no unrecognized shape to fall through
 * to here - the default case is unreachable in practice and, if it were
 * ever reached, would simply under-count rather than misbehave. */
static void loop_regalloc_count_idents(const struct AstNode *n, struct LoopIdentCounts *ic)
{
    int i;

    if (n == NULL)
        return;

    switch (n->kind) {
    case AST_COMPOUND:
        for (i = 0; i < n->list_len; ++i)
            loop_regalloc_count_idents(n->list[i], ic);
        return;
    case AST_EXPR_STMT:
        loop_regalloc_count_idents(n->a, ic);
        return;
    case AST_IF:
        loop_regalloc_count_idents(n->a, ic);
        loop_regalloc_count_idents(n->b, ic);
        loop_regalloc_count_idents(n->c, ic);
        return;
    case AST_ASSIGN:
        loop_regalloc_count_idents(n->a, ic);
        loop_regalloc_count_idents(n->b, ic);
        return;
    case AST_UNARY:
    case AST_POSTFIX:
        loop_regalloc_count_idents(n->a, ic);
        return;
    case AST_BINARY:
    case AST_LOGAND:
    case AST_LOGOR:
    case AST_COMMA:
    case AST_INDEX:
        loop_regalloc_count_idents(n->a, ic);
        loop_regalloc_count_idents(n->b, ic);
        return;
    case AST_MEMBER:
        loop_regalloc_count_idents(n->a, ic);
        return;
    case AST_COND:
        loop_regalloc_count_idents(n->a, ic);
        loop_regalloc_count_idents(n->b, ic);
        loop_regalloc_count_idents(n->c, ic);
        return;
    case AST_CAST:
        loop_regalloc_count_idents(n->a, ic);
        return;
    case AST_IDENT:
        loop_ident_bump(ic, n->sval);
        return;
    default:
        return;
    }
}

/* Picks the best loop-scoped BC-promotion candidate for `for_node` (an
 * AST_FOR node), or NULL if none qualifies. Eligibility mirrors dcc_func.c's
 * find_bc_regalloc_candidate: a plain word-sized (2-byte, not struct/long/
 * float) local or parameter, not an array/VLA, not volatile, not already
 * reg_alloc'd by an outer claim, referenced at least LOOP_REGALLOC_MIN_REFS
 * times in the loop's condition/increment/body, and never assigned,
 * incremented/decremented, or address-taken anywhere in that same span
 * (checked via dcc_licm.c's licm_scan_modified, which also declines the
 * whole loop - by overflowing - if it contains a call, nested loop, switch,
 * or goto anywhere, matching find_bc_regalloc_candidate's own function-wide
 * "any call disqualifies everything" conservatism, just scoped to one
 * loop). Unlike find_bc_regalloc_candidate, body-local symbols are eligible
 * too, not just parameters - see this file's header comment for why that's
 * safe here specifically. */
struct Sym *loop_regalloc_find_bc_candidate(const struct AstNode *for_node)
{
    struct LicmModifiedNames mod;
    struct LoopIdentCounts ic;
    struct Sym *best;
    int best_count;
    int i;

    if (for_node == NULL)
        return NULL;
    /* BC already spoken for by dcc_func.c's whole-function candidate (for
     * this whole function, not just this loop) - never double-claim it. */
    if (g_bc_regalloc_sym != NULL)
        return NULL;

    memset(&mod, 0, sizeof(mod));
    licm_scan_modified(for_node->b, &mod);
    licm_scan_modified(for_node->c, &mod);
    licm_scan_modified(for_node->d, &mod);
    if (mod.overflowed)
        return NULL;

    memset(&ic, 0, sizeof(ic));
    loop_regalloc_count_idents(for_node->b, &ic);
    loop_regalloc_count_idents(for_node->c, &ic);
    loop_regalloc_count_idents(for_node->d, &ic);

    best = NULL;
    best_count = 0;
    for (i = 0; i < ic.n; ++i) {
        struct Sym *s;
        int j, is_mod;

        if (ic.items[i].count < LOOP_REGALLOC_MIN_REFS)
            continue;
        if (ic.items[i].count <= best_count)
            continue;

        is_mod = 0;
        for (j = 0; j < mod.count; ++j) {
            if (strcmp(mod.names[j], ic.items[i].name) == 0) {
                is_mod = 1;
                break;
            }
        }
        if (is_mod)
            continue;

        s = find_sym(ic.items[i].name);
        if (s == NULL)
            continue;
        if (s->storage != SC_LOCAL && s->storage != SC_PARAM)
            continue;
        if (s->name[0] == '#')
            continue;
        if (s->is_array || s->is_vla)
            continue;
        if (s->is_volatile)
            continue;
        if (s->reg_alloc != REG_NONE)
            continue;
        if (type_is_struct_object(s->type) || type_is_long(s->type) || type_is_float(s->type))
            continue;
        if (type_size(s->type) != 2)
            continue;
        /* The priming load emits a direct "ld c,(ix+off)"/"ld b,(ix+off+1)"
         * pair (see try_loop_regalloc_bc) - off must fit an IX
         * displacement's signed byte range. dcc_func.c's own whole-function
         * candidate never needs this check: it's restricted to parameters,
         * which always sit at small, in-range positive offsets - but this
         * mechanism also considers locals, which can sit arbitrarily deep
         * in a large frame (found via tests/tautolcs.c: a local at ix-164,
         * well outside [-128,127], produced a "ld c,(ix-164)" instruction
         * with a displacement byte too large to encode). sym_can_ix_direct
         * is the existing, already-correct check for exactly this. */
        if (!sym_can_ix_direct(s))
            continue;

        best = s;
        best_count = ic.items[i].count;
    }
    return best;
}

/* Speculatively generates `for_node` (via `gen_for_impl`, dcc_ast_gen_stmt.c's
 * ast_gen_for_stmt_impl - the renamed original body of ast_gen_for_stmt)
 * with `cand` primed into BC right before the loop instead of occupying its
 * normal frame slot, and verifies/finalizes via regalloc_buffer_finalize.
 * Modeled directly on dcc_func.c's try_speculative_bc_regalloc_function_body:
 * same tmpfile-redirect-generate-verify-commit-or-discard shape. Lighter in
 * one respect: no token-stream rewind is needed on a discarded attempt,
 * because for_node is an already-built AST subtree that this whole call
 * re-walks without reparsing anything - unlike dcc_func.c's whole-function
 * retry, which must reparse from source because gen_compound() is a fused
 * parse-and-emit loop with no persisted whole-function tree to re-walk (see
 * this file's header comment). Only frame/label bookkeeping a discarded
 * attempt could have touched needs rewinding: nlocals/local_size (a
 * declined loop-invariant hoist inside the loop may have allocated a
 * compiler temp), g_licm_seq (that temp's name counter), and g_for_seq
 * (defense in depth - unreachable in practice, since licm_scan_modified
 * already declines any loop containing a nested loop).
 *
 * Returns 1 if the promoted version was committed to outf (cand->reg_alloc
 * is REG_NONE again by the time this returns either way - the caller must
 * NOT also call gen_for_impl itself when this returns 1). Returns 0 if
 * declined; the caller must then generate the loop normally. */
int try_loop_regalloc_bc(const struct AstNode *for_node, struct Sym *cand,
                          void (*gen_for_impl)(const struct AstNode *))
{
    FILE *scratch;
    FILE *saved_outf;
    int saved_nlocals;
    int saved_local_size;
    int saved_for_seq;
    int saved_licm_seq;
    int c;
    int errors_before;

    scratch = tmpfile();
    if (scratch == NULL)
        fatal("cannot create speculative loop-regalloc temp file");

    saved_nlocals = nlocals;
    saved_local_size = local_size;
    saved_for_seq = g_for_seq;
    saved_licm_seq = g_licm_seq;

    saved_outf = outf;
    outf = scratch;
    cand->reg_alloc = REG_BC;
    g_regalloc_address_escaped = 0;
    /* This buffer may be discarded entirely (see regalloc_buffer_finalize
     * below) - a runtime helper (e.g. __mulu) first referenced only inside
     * it must not be marked "extrn already emitted" in the persistent
     * dedup cache, or the real fallback generation would silently skip
     * re-declaring it. Same guard dcc_func.c's own speculative attempts
     * use, for the identical reason - see emit_extrn_if_needed in
     * dcc_symbols.c. */
    g_inline_body_buffering++;
    g_buffering_epoch++;
    fprintf(outf, "\tld c,(ix%+d)\n", cand->offset);
    fprintf(outf, "\tld b,(ix%+d)\n", cand->offset + 1);

    errors_before = g_diag_error_count;
    asm_suppress_depth++;
    gen_for_impl(for_node);
    asm_suppress_depth--;
    g_inline_body_buffering--;

    cand->reg_alloc = REG_NONE;
    outf = saved_outf;

    if (g_diag_error_count == errors_before && !g_regalloc_address_escaped) {
        FILE *finalized = NULL;
        if (regalloc_buffer_finalize(scratch, cand, NULL, &finalized)) {
            fclose(scratch);
            while ((c = fgetc(finalized)) != EOF)
                fputc(c, outf);
            fclose(finalized);
            return 1;
        }
    }

    fclose(scratch);
    /* Bump the epoch again, even though g_inline_body_buffering may already
     * be back to 0: if this attempt is nested inside dcc_func.c's own
     * whole-function speculative buffering, g_inline_body_buffering is only
     * back to THAT outer level (not 0), and the fallback generation below
     * still runs under it. Without this, emit_runtime_extrn_if_needed's
     * static buf_epoch (last set to the epoch bumped above, during this
     * now-discarded attempt) would still equal g_buffering_epoch, so it
     * would keep reusing this attempt's buf_emitted[] list - wrongly
     * "remembering" a runtime helper's extrn as already emitted when that
     * declaration only ever existed in the scratch buffer just discarded.
     * Found via tests/tcrcfix.c: a `call __mulu` with no matching `extrn
     * __mulu` anywhere in the output, from exactly this nesting. */
    g_buffering_epoch++;
    nlocals = saved_nlocals;
    local_size = saved_local_size;
    g_for_seq = saved_for_seq;
    g_licm_seq = saved_licm_seq;
    return 0;
}
