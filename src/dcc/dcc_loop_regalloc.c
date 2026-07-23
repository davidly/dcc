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
 * Verification of a read-only (Phase 1) candidate reuses dcc_func.c's own
 * regalloc_buffer_finalize unmodified: it scans a buffer of emitted
 * assembly text for anything that clobbers b/c/bc without dcc's own
 * recognized load/store idioms, tracking loop back-edges via label/jump
 * text so a linear single-pass scan still catches a hazard that would only
 * appear on a loop's second-and-later iteration. Nothing in that function
 * assumes its buffer is a whole function body rather than one loop's own
 * emitted span - "any call anywhere in this text" is exactly the right
 * question whether "this text" is a function or a loop.
 *
 * Phase 2 promotes candidates the loop also WRITES (assigns to, and/or
 * increments/decrements) - the actual replacement for dccpeep's half-dozen
 * near-duplicate loop-counter/accumulator-to-register passes. This needs
 * two things Phase 1 didn't: a place for a write to actually go (new
 * REG_BC branches in emit_store_hl_to_sym_direct/emit_incdec_sym_direct,
 * dcc_symbols.c - previously dead code, since the whole-function BC
 * candidate is read-only by construction) and a stricter verifier
 * (loop_regalloc_write_candidate_safe, below) than regalloc_buffer_
 * finalize's lenient one, because a write candidate's frame slot is
 * intentionally stale for the whole loop - reload-repairing from it, as
 * the lenient policy would, could silently substitute a stale value. See
 * that function's own header comment for the full argument. Eligible loops
 * are naturally single-exit (see loop_regalloc_find_bc_candidate's header
 * comment), so a single spill store right after the loop is always
 * sufficient to resync the frame slot before anything downstream reads it.
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
    case AST_SWITCH:
        loop_regalloc_count_idents(n->a, ic);
        loop_regalloc_count_idents(n->b, ic);
        return;
    case AST_CASE:
    case AST_DEFAULT:
        loop_regalloc_count_idents(n->b, ic);
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

/* True if `name` is referenced anywhere in `n` (any position - read, write,
 * or as part of a larger subexpression). Used only by loop_regalloc_used_
 * as_index below to check an AST_INDEX's index subtree, which can be an
 * arbitrary expression (`arr[i]`, `arr[i+1]`, ...), not just a bare
 * identifier. */
static int loop_regalloc_ident_in_subtree(const struct AstNode *n, const char *name)
{
    if (n == NULL)
        return 0;
    switch (n->kind) {
    case AST_IDENT:
        return n->sval != NULL && strcmp(n->sval, name) == 0;
    case AST_BINARY:
    case AST_LOGAND:
    case AST_LOGOR:
    case AST_COMMA:
    case AST_INDEX:
    case AST_ASSIGN:
        return loop_regalloc_ident_in_subtree(n->a, name) ||
               loop_regalloc_ident_in_subtree(n->b, name);
    case AST_UNARY:
    case AST_POSTFIX:
    case AST_MEMBER:
    case AST_CAST:
        return loop_regalloc_ident_in_subtree(n->a, name);
    case AST_COND:
        return loop_regalloc_ident_in_subtree(n->a, name) ||
               loop_regalloc_ident_in_subtree(n->b, name) ||
               loop_regalloc_ident_in_subtree(n->c, name);
    default:
        return 0;
    }
}

/* True if `name` is used as (part of) an array/pointer INDEX expression
 * anywhere in `n` - i.e. appears in an AST_INDEX node's `b` (the subscript),
 * as opposed to merely being the array/pointer being indexed (`n->a`) or
 * appearing elsewhere entirely. Write-candidate selection excludes any name
 * this returns true for - see the comment at its call site for why. Same
 * node-kind coverage/traversal shape as loop_regalloc_count_idents. */
static int loop_regalloc_used_as_index(const struct AstNode *n, const char *name)
{
    int i;

    if (n == NULL)
        return 0;
    switch (n->kind) {
    case AST_COMPOUND:
        for (i = 0; i < n->list_len; ++i)
            if (loop_regalloc_used_as_index(n->list[i], name))
                return 1;
        return 0;
    case AST_EXPR_STMT:
        return loop_regalloc_used_as_index(n->a, name);
    case AST_IF:
        return loop_regalloc_used_as_index(n->a, name) ||
               loop_regalloc_used_as_index(n->b, name) ||
               loop_regalloc_used_as_index(n->c, name);
    case AST_SWITCH:
        return loop_regalloc_used_as_index(n->a, name) ||
               loop_regalloc_used_as_index(n->b, name);
    case AST_CASE:
    case AST_DEFAULT:
        return loop_regalloc_used_as_index(n->b, name);
    case AST_ASSIGN:
        return loop_regalloc_used_as_index(n->a, name) ||
               loop_regalloc_used_as_index(n->b, name);
    case AST_UNARY:
    case AST_POSTFIX:
        return loop_regalloc_used_as_index(n->a, name);
    case AST_BINARY:
    case AST_LOGAND:
    case AST_LOGOR:
    case AST_COMMA:
        return loop_regalloc_used_as_index(n->a, name) ||
               loop_regalloc_used_as_index(n->b, name);
    case AST_INDEX:
        if (loop_regalloc_ident_in_subtree(n->b, name))
            return 1;
        return loop_regalloc_used_as_index(n->a, name) ||
               loop_regalloc_used_as_index(n->b, name);
    case AST_MEMBER:
        return loop_regalloc_used_as_index(n->a, name);
    case AST_COND:
        return loop_regalloc_used_as_index(n->a, name) ||
               loop_regalloc_used_as_index(n->b, name) ||
               loop_regalloc_used_as_index(n->c, name);
    case AST_CAST:
        return loop_regalloc_used_as_index(n->a, name);
    default:
        return 0;
    }
}

/* Shared eligibility gate for both read-only (Phase 1) and write (Phase 2)
 * candidates: word-sized (2-byte, not struct/long/float) local or
 * parameter, not an array/VLA, not volatile, not already reg_alloc'd by an
 * outer claim, its address never taken ANYWHERE in the whole function (not
 * just within this loop - licm_scan_modified's own address-taken detection
 * is loop-scoped, so `int *alias = &value;` sitting BEFORE the loop is
 * completely invisible to it; a write to *alias inside the loop would then
 * silently target value's stale frame slot instead of its BC-resident
 * live value - found via tests/tpeepal.c's word_loop_alias, a real wrong-
 * answer bug, not just a missed optimization, since checked via
 * local_name_address_taken_in_function - dcc_func.c's own whole-function
 * lexical scan, already run once per function and safe to reuse here, the
 * same one find_bc_regalloc_candidate itself relies on for its own
 * parameter candidates), and its frame offset fits an IX displacement's
 * signed byte range (sym_can_ix_direct - see the priming-load comment in
 * try_loop_regalloc_bc for why this matters here specifically, unlike for
 * dcc_func.c's parameter-only whole-function candidate). */
static int loop_regalloc_sym_eligible(struct Sym *s)
{
    if (s == NULL)
        return 0;
    if (s->storage != SC_LOCAL && s->storage != SC_PARAM)
        return 0;
    if (local_name_address_taken_in_function(s->name))
        return 0;
    if (s->name[0] == '#')
        return 0;
    if (s->is_array || s->is_vla)
        return 0;
    if (s->is_volatile)
        return 0;
    if (s->reg_alloc != REG_NONE)
        return 0;
    if (type_is_struct_object(s->type) || type_is_long(s->type) || type_is_float(s->type))
        return 0;
    if (type_size(s->type) != 2)
        return 0;
    if (!sym_can_ix_direct(s))
        return 0;
    return 1;
}

/* Picks the best loop-scoped BC-promotion candidate for a loop whose
 * condition/increment/body are `cond`/`incr`/`body` (`incr` may be NULL -
 * AST_WHILE/AST_DOWHILE have no separate increment clause, only AST_FOR
 * does), or NULL if none qualifies - considering BOTH read-only candidates
 * (Phase 1: never assigned/incremented/address-taken in the loop, verified
 * via try_loop_regalloc_bc's lenient reload-repair) and write candidates
 * (Phase 2: assigned and/or incremented/decremented, verified via
 * try_loop_regalloc_bc_write's stricter decline-only policy - see that
 * function's header comment for why a write candidate can't safely use the
 * lenient policy). Both draw from the same reference-count ranking across
 * cond/incr/body, so whichever identifier is used the most wins regardless
 * of category - only one can occupy BC per loop either way. *out_is_write
 * reports which policy the winning candidate needs; the caller must route
 * to the matching driver.
 *
 * A call, nested loop, switch, or goto anywhere in the loop declines
 * everything outright (dcc_licm.c's licm_scan_modified overflows), exactly
 * matching find_bc_regalloc_candidate's own function-wide "any call
 * disqualifies everything" conservatism, just scoped to one loop - and,
 * for write candidates specifically, this is also what guarantees a single
 * fall-through exit: break/return/goto-out/continue are all unrecognized
 * node kinds to licm_scan_modified too, so any of them anywhere in the
 * loop already declines the whole loop before write-candidate selection
 * ever runs, leaving exactly one exit point (the label right after the
 * loop) for the spill store try_loop_regalloc_bc_write emits there. */
struct Sym *loop_regalloc_find_bc_candidate(const struct AstNode *cond,
                                            const struct AstNode *incr,
                                            const struct AstNode *body,
                                            int *out_is_write)
{
    struct LicmModifiedNames mod;
    struct LoopIdentCounts ic;
    struct Sym *best_ro;
    int best_ro_count;
    struct Sym *best_write;
    int best_write_count;
    int i;

    if (body == NULL)
        return NULL;
    /* BC already spoken for by dcc_func.c's whole-function candidate (for
     * this whole function, not just this loop) - never double-claim it. */
    if (g_bc_regalloc_sym != NULL)
        return NULL;

    memset(&mod, 0, sizeof(mod));
    licm_scan_modified(cond, &mod);
    licm_scan_modified(incr, &mod);
    licm_scan_modified(body, &mod);
    if (mod.overflowed)
        return NULL;

    memset(&ic, 0, sizeof(ic));
    loop_regalloc_count_idents(cond, &ic);
    loop_regalloc_count_idents(incr, &ic);
    loop_regalloc_count_idents(body, &ic);

    /* If ANY identifier referenced in this loop was declared `register`
     * (regardless of whether it's even eligible as a candidate itself),
     * decline promoting anything in this loop at all. dccpeep has its own,
     * completely independent register-allocation-shaped passes that
     * specifically target register-qualified locals (e.g.
     * pass_byte_loop_counter_to_reg_c, reactively promoting a narrowed
     * `register`-qualified self-testing decrement counter into C) - dccpeep
     * runs on dcc's output as pure text, with zero visibility into any
     * reg_alloc claim this mechanism already made, so it can and does
     * clobber a register this mechanism is already using for a DIFFERENT
     * symbol across the very same loop. Found via tests/tregnarw.c's
     * lres(): `register int n; ... while (--n) total = total + n;` -
     * `total` (word, unrelated to `n`) got promoted into BC here, then
     * dccpeep's pass_byte_loop_counter_to_reg_c independently promoted the
     * narrowed byte counter `n` into C - the low half of the SAME
     * register - silently overwriting `total`'s low byte and hanging the
     * program in an infinite loop. `register` is a strong, cheap, already-
     * available (Sym::is_register, set from the `register` keyword at
     * declaration - dcc_decl.c's gen_local_decl_after_type) signal that
     * some OTHER mechanism may want this loop's registers too; ceding the
     * whole loop is far simpler and safer than trying to model dccpeep's
     * exact pattern set here. */
    {
        int k;
        for (k = 0; k < ic.n; ++k) {
            struct Sym *rs = find_sym(ic.items[k].name);
            if (rs != NULL && rs->is_register)
                return NULL;
        }
    }

    /* Read-only and write candidates are ranked SEPARATELY, then combined
     * with a bias toward read-only: a write candidate only wins if its
     * count beats the best read-only candidate's by more than one
     * reference, not merely ties or edges it out. Both categories draw
     * from the same loop, so they aren't independent - promoting whichever
     * one wins means the OTHER doesn't get promoted at all, and a write
     * candidate is strictly more expensive in fixed overhead (it pays a
     * spill after the loop; a read-only candidate doesn't) with no
     * guarantee its per-access savings are actually larger. Found via
     * tests/00040b.c's chk(): its accumulator `r` (write, ~6 references)
     * edged out its parameter `y` (read-only, referenced almost as often,
     * including inside several comparisons) by a small margin under pure
     * reference-count ranking, and promoting `r` instead of `y` measurably
     * regressed this extremely hot function - not from any fast-path
     * defeat (see the condition-clause exclusion above), just because `y`
     * was the more valuable candidate to have in BC here and a small
     * count edge isn't a reliable signal of that. */
    best_ro = NULL;
    best_ro_count = 0;
    best_write = NULL;
    best_write_count = 0;
    for (i = 0; i < ic.n; ++i) {
        struct Sym *s;
        int j, is_mod;

        if (ic.items[i].count < LOOP_REGALLOC_MIN_REFS)
            continue;
        if (ic.items[i].count <= best_ro_count && ic.items[i].count <= best_write_count)
            continue;

        is_mod = 0;
        for (j = 0; j < mod.count; ++j) {
            if (strcmp(mod.names[j], ic.items[i].name) == 0) {
                is_mod = 1;
                break;
            }
        }

        /* A write candidate that also appears in the loop's own condition
         * (almost always the induction variable itself, compared against a
         * bound every iteration) USED to be excluded outright here: dcc's
         * fast direct-branch comparison path required reg_alloc == REG_NONE
         * (ast_cmp_operand_ok, dcc_ast_gen_cond.c), so a BC-resident operand
         * fell back to a much more expensive generic "materialize a 0/1
         * boolean, then test it" sequence, paid every iteration. That gate
         * now accepts a REG_BC-resident operand directly (its own emitter,
         * ast_gen_cmp_branch, was already reg_alloc-transparent - the gate
         * was the only thing declining it), so the blanket condition-clause
         * exclusion is gone.
         *
         * A NARROWER exclusion replaces it: a write candidate used as an
         * array/pointer INDEX anywhere in the loop is still excluded. Not a
         * fast-path defeat this time - dccpeep has whole-LOOP structural
         * passes (pass_ldir_memset_rotated, pass_stride_loop_to_ptr) that
         * recognize an array-indexed loop's canonical (ix+d)-based shape
         * and replace the ENTIRE loop with something algorithmically
         * better (a single Z80 `ldir` block-copy for a constant-fill loop;
         * incremental pointer striding instead of recomputing base+index
         * every iteration for a strided-store loop) - categorically bigger
         * wins than a register cache can ever provide. Promoting the index
         * changes the text shape enough that neither pass recognizes the
         * loop anymore, losing that larger win for the smaller one this
         * mechanism provides. Found via tests/sieve.c: promoting its outer
         * fill-loop's `i` (matches pass_ldir_memset_rotated's shape) and
         * inner sieve loop's `k` (matches pass_stride_loop_to_ptr's shape)
         * more than doubled total cycles even with the comparison fast-path
         * fix in place - confirmed via dccprof profiling and a direct diff
         * against dccpeep's un-promoted output, which collapses the whole
         * fill loop into one `ldir`. A candidate that never indexes an
         * array (a plain accumulator, or a comparison-only participant)
         * is unaffected. */
        if (is_mod &&
            (loop_regalloc_used_as_index(cond, ic.items[i].name) ||
             loop_regalloc_used_as_index(incr, ic.items[i].name) ||
             loop_regalloc_used_as_index(body, ic.items[i].name)))
            continue;

        s = find_sym(ic.items[i].name);
        if (!loop_regalloc_sym_eligible(s))
            continue;

        if (is_mod) {
            if (ic.items[i].count > best_write_count) {
                best_write = s;
                best_write_count = ic.items[i].count;
            }
        } else {
            if (ic.items[i].count > best_ro_count) {
                best_ro = s;
                best_ro_count = ic.items[i].count;
            }
        }
    }

    /* Combine with a bias toward read-only - see the comment above this
     * loop for why. Absolute, not margin-based: a read-only candidate's
     * raw reference count isn't comparable to a write candidate's at all,
     * since every read-modify-write occurrence (the overwhelmingly common
     * write shape - `x = x op y`, `x op= y`, `x++`) counts the SAME name
     * twice (once read, once written) where a read-only use counts it
     * once, so a write candidate's count is inflated by its very nature
     * and no fixed margin reliably corrects for that - tests/00040b.c's
     * `r` (write, 12 occurrences: 6 statements x 2) still beat `y` (read-
     * only, single digits) under a margin of 1. Any qualifying read-only
     * candidate wins outright instead. */
    if (best_ro != NULL) {
        if (out_is_write != NULL)
            *out_is_write = 0;
        return best_ro;
    }
    if (best_write != NULL) {
        if (out_is_write != NULL)
            *out_is_write = 1;
        return best_write;
    }
    if (out_is_write != NULL)
        *out_is_write = 0;
    return NULL;
}

/* Speculatively generates `loop_node` (via `gen_loop_impl`, dcc_ast_gen_stmt.c's
 * ast_gen_for_stmt_impl - the renamed original body of ast_gen_for_stmt)
 * with `cand` primed into BC right before the loop instead of occupying its
 * normal frame slot, and verifies/finalizes via regalloc_buffer_finalize.
 * Modeled directly on dcc_func.c's try_speculative_bc_regalloc_function_body:
 * same tmpfile-redirect-generate-verify-commit-or-discard shape. Lighter in
 * one respect: no token-stream rewind is needed on a discarded attempt,
 * because loop_node is an already-built AST subtree that this whole call
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
 * NOT also call gen_loop_impl itself when this returns 1). Returns 0 if
 * declined; the caller must then generate the loop normally. */
int try_loop_regalloc_bc(const struct AstNode *loop_node, struct Sym *cand,
                          void (*gen_loop_impl)(const struct AstNode *))
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
    gen_loop_impl(loop_node);
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

/* Strict verifier for a write candidate (Phase 2), scanning `f` (left
 * rewound, unmodified, for the caller to copy on success - unlike
 * regalloc_buffer_finalize's bc_cand path, nothing here is ever rewritten).
 *
 * regalloc_buffer_finalize's BC handling is deliberately lenient: since a
 * read-only candidate's original frame slot never changes for the life of
 * the span being verified, any untrusted point can always be repaired by
 * reloading from it, so an unrelated scratch use of b/c/bc elsewhere (e.g.
 * borrowed transiently for long-arithmetic scratch) just costs a reload
 * rather than failing the whole attempt. A write candidate has no such
 * shadow: its frame slot is intentionally stale for the entire loop -
 * nothing keeps it in sync until try_loop_regalloc_bc_write's spill store,
 * emitted once, after the loop - so there is nothing safe to reload from
 * at any point *during* it. Anything that touches b/c/bc other than this
 * candidate's own recognized read (emit_load_sym_value_direct's REG_BC
 * pair), write (emit_store_hl_to_sym_direct's REG_BC pair), incdec
 * (emit_incdec_sym_direct's REG_BC form), or the priming/spill lines this
 * file itself emits, must decline the WHOLE attempt outright - matching
 * regalloc_buffer_finalize's own e_cand policy (word pair instead of e's
 * single byte). This also means none of regalloc_buffer_finalize's loop-
 * header/self-consistency machinery is needed: that exists purely to
 * decide when a reload-repair is worth it (a linear scan sees a loop body
 * once but it runs N times), which never applies here - every recognized
 * pattern is safe on every iteration purely because it's the same fixed
 * instruction wherever it appears in the text, independent of any
 * "trust" history, so nothing about iteration count matters. */
static int loop_regalloc_write_candidate_safe(FILE *f, struct Sym *cand)
{
    long size;
    char *buf;
    char *line, *nl;
    char entry_c[32], entry_b[32], exit_c[32], exit_b[32];
    int safe;

    fseek(f, 0, SEEK_END);
    size = ftell(f);
    rewind(f);
    if (size <= 0)
        return 1;
    buf = (char *)xmalloc((size_t)size + 1);
    if (fread(buf, 1, (size_t)size, f) != (size_t)size)
        fatal("cannot read loop-regalloc write-candidate verify temp file");
    buf[size] = 0;
    rewind(f);

    if (strstr(buf, "\tcall ") != NULL) {
        free(buf);
        return 0;
    }

    sprintf(entry_c, "\tld c,(ix%+d)", cand->offset);
    sprintf(entry_b, "\tld b,(ix%+d)", cand->offset + 1);
    sprintf(exit_c, "\tld (ix%+d),c", cand->offset);
    sprintf(exit_b, "\tld (ix%+d),b", cand->offset + 1);

    safe = 1;
    line = buf;
    while (safe && line < buf + size) {
        int recognized;

        nl = strchr(line, '\n');
        if (nl) *nl = 0;

        recognized =
            strcmp(line, entry_c) == 0 || strcmp(line, entry_b) == 0 ||
            strcmp(line, exit_c) == 0 || strcmp(line, exit_b) == 0 ||
            strcmp(line, "\tld l,c") == 0 || strcmp(line, "\tld h,b") == 0 ||
            strcmp(line, "\tld c,l") == 0 || strcmp(line, "\tld b,h") == 0 ||
            strcmp(line, "\tinc bc") == 0 || strcmp(line, "\tdec bc") == 0;

        if (!recognized && line_touches_bc_reg(line))
            safe = 0;

        line = nl ? nl + 1 : buf + size;
    }

    free(buf);
    return safe;
}

/* Write-candidate counterpart to try_loop_regalloc_bc (see that function's
 * header comment for the shared speculate/verify/commit-or-discard shape
 * and state-rewind rationale - identical here). Differences specific to a
 * write candidate: the loop's own generated writes/incdecs now redirect
 * through BC too (emit_store_hl_to_sym_direct/emit_incdec_sym_direct's
 * REG_BC branches, dcc_symbols.c), a spill store is emitted once right
 * after the loop to resync the candidate's frame slot with BC's final
 * value (nothing else keeps it in sync - see loop_regalloc_write_
 * candidate_safe), and verification uses that stricter, decline-only
 * checker instead of regalloc_buffer_finalize's lenient one. */
int try_loop_regalloc_bc_write(const struct AstNode *loop_node, struct Sym *cand,
                                void (*gen_loop_impl)(const struct AstNode *))
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
        fatal("cannot create speculative loop-regalloc write temp file");

    saved_nlocals = nlocals;
    saved_local_size = local_size;
    saved_for_seq = g_for_seq;
    saved_licm_seq = g_licm_seq;

    saved_outf = outf;
    outf = scratch;
    cand->reg_alloc = REG_BC;
    g_regalloc_address_escaped = 0;
    g_inline_body_buffering++;
    g_buffering_epoch++;
    fprintf(outf, "\tld c,(ix%+d)\n", cand->offset);
    fprintf(outf, "\tld b,(ix%+d)\n", cand->offset + 1);

    errors_before = g_diag_error_count;
    asm_suppress_depth++;
    gen_loop_impl(loop_node);
    /* Spill BC's final value back to the candidate's frame slot before
     * anything after the loop (still generated under reg_alloc == REG_NONE,
     * set below) can read it via the normal (ix+d) path. Still inside the
     * suppressed/buffered span - this is unconditionally part of what gets
     * verified and either committed whole or discarded whole. */
    fprintf(outf, "\tld (ix%+d),c\n", cand->offset);
    fprintf(outf, "\tld (ix%+d),b\n", cand->offset + 1);
    asm_suppress_depth--;
    g_inline_body_buffering--;

    cand->reg_alloc = REG_NONE;
    outf = saved_outf;

    if (g_diag_error_count == errors_before && !g_regalloc_address_escaped &&
        loop_regalloc_write_candidate_safe(scratch, cand)) {
        rewind(scratch);
        while ((c = fgetc(scratch)) != EOF)
            fputc(c, outf);
        fclose(scratch);
        return 1;
    }

    fclose(scratch);
    /* See try_loop_regalloc_bc's identical comment on this bump. */
    g_buffering_epoch++;
    nlocals = saved_nlocals;
    local_size = saved_local_size;
    g_for_seq = saved_for_seq;
    g_licm_seq = saved_licm_seq;
    return 0;
}
