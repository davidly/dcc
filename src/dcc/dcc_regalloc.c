/*
 * dcc_regalloc.c - speculative no-IX-frame and BC-register-allocation codegen.
 *
 * Speculative function-body generation passes that opportunistically emit a
 * tighter prologue/epilogue: omitting IX for eligible leaf functions and
 * keeping an eligible read-only word in BC (optionally with an E-register
 * counter). It also orchestrates the loop-scoped BC trial implemented in
 * dcc_loop_regalloc.c. Each pass generates into a verification sink, then
 * commits or restores parser/frame state and falls back to ordinary codegen.
 *
 * Carved out of dcc_func.c; entry points are called from
 * parse_function_or_global. find_bc_regalloc_candidate and
 * plain_static_body_can_be_buffered remain in dcc_func.c.
 *
 * MODULE: compiled as its own translation unit; optimizer-only contracts are
 * declared in dcc_regalloc_internal.h.
 */

#ifndef _WIN32
/* fileno()/ftruncate() (scratch-buffer management) are POSIX, so strict ISO C
 * mode hides their declarations unless a POSIX feature-test macro is
 * visible before <stdio.h>/<unistd.h> are first included via dcc.h below. */
#define _POSIX_C_SOURCE 200809L
#endif

#include "dcc.h"
#include "dcc_regalloc_internal.h"
#include "dcc_ast.h"
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

/* Cheap pre-filter for try_speculative_noix_function_body: is it even worth
 * attempting? A function that makes any call almost certainly pushes
 * arguments for it (current_function_has_call is set during the scan pass,
 * so it is already known here) - cheap to check and saves a wasted extra
 * generation pass on the common case. main() is excluded outright: it is
 * never a hot leaf, and the __mrun-shim emission right after this codegen
 * assumes the ordinary IX-framed epilogue shape.
 *
 * local_bytes must be exactly 0 (no declared locals AND no compiler-
 * generated temporaries - #itmp/#clit locals count too, since local_bytes is
 * just local_size after the scan). This is load-bearing, not a size
 * heuristic: the disabled no-IX-frame support only ever extended to
 * SC_PARAM addressing (frame_sp_offset_for_sym, and the current_omit_ix_
 * frame checks in emit_load_sym_value_direct/emit_load_frame_addr_hl are
 * both gated on `s->storage == SC_PARAM`). A local variable is still
 * addressed (ix+N) unconditionally - with no `push ix`/`ld ix,0` this reads
 * and writes through whatever garbage IX happens to hold at entry, silently
 * corrupting memory rather than crashing. A speculative generation pass
 * cannot catch this the way it catches an SP-shifting push (there is no
 * "did the buffer do something wrong" signal to check for - the emitted
 * instructions are individually valid, just relative to the wrong
 * register), so functions with any local storage must be excluded here,
 * before ever attempting it, rather than verified after the fact. */
int function_qualifies_for_speculative_noix(const char *name, int local_bytes)
{
    if (strcmp(name, "main") == 0)
        return 0;
    if (current_function_has_call)
        return 0;
    if (local_bytes != 0)
        return 0;
    return 1;
}

/* Does the buffered speculative body contain anything unsafe for a function
 * that never set up an IX frame? Two independent signals, both fatal:
 *
 *   - "push": the only way SP-relative (sp+N) addressing already computed
 *     earlier in the body can go wrong - a push shifts SP out from under it,
 *     and dcc has no live SP-delta tracking to compensate.
 *   - "(ix": IX was never loaded with anything meaningful in this function
 *     (no `push ix`/`ld ix,0`/`add ix,sp`), so ANY `(ix+N)`/`(ix-N)` memory
 *     reference anywhere in the body reads or writes through whatever
 *     garbage IX happens to hold at entry - silently corrupting memory
 *     rather than crashing. This is not hypothetical: the disabled no-IX
 *     support only ever touched a handful of load/store helpers
 *     (emit_load_sym_value_direct and friends in dcc_symbols.c) to check
 *     current_omit_ix_frame; other, unrelated fast paths elsewhere in the
 *     codegen (e.g. gen_return_ast's dedicated "return a 1-byte identifier"
 *     shortcut) emit `(ix+N)` addressing directly and were never taught
 *     about current_omit_ix_frame at all, so cannot be trusted to have
 *     skipped it just because this function has no locals. Rather than
 *     audit every such shortcut across the codebase (and every future one
 *     anyone adds), checking for the address mode itself is exact: a
 *     correctly-generated no-IX-frame body can never legitimately contain
 *     it, so any occurrence at all is proof something used it by mistake.
 *
 * Every push/(ix in dcc's own codegen is emitted as literal text (there are
 * no comments or other decoration at this stage - dccpeep is a separate
 * later pass), so a plain substring search is exact, not a heuristic. */
static int tmpfile_unsafe_for_noix(FILE *f)
{
    long size;
    char *buf;
    int found;

    buf = dcc_read_stream_text(f, &size,
                               "cannot read speculative no-ix-frame temp file");
    if (size <= 0) {
        free(buf);
        return 0;
    }
    found = strstr(buf, "push") != NULL || strstr(buf, "(ix") != NULL;
    free(buf);
    return found;
}

/*
 * Reset the per-function codegen sequence counters to their function-entry
 * state for a fresh body-generation pass over function `s`: label count,
 * for-scope / for-init-rename / block-scope-depth sequence counters, the
 * compound-literal and LICM sequence counters, and the static-local dedup
 * index. Allocates a fresh return label via new_label() (which advances the
 * global label counter - a discarded speculative attempt simply abandons the
 * label it allocated). Shared by the speculative body passes below, whose
 * setup and discard-rewind previously duplicated this block verbatim (six
 * copies); centralising it means a newly added piece of per-function codegen
 * state can't be forgotten at one of them.
 */
static void reset_function_codegen_state(struct Sym *s)
{
    nulabels = 0;
    current_return_label = new_label();
    g_func_pass.for_seq = 0;
    g_func_pass.forren_n = 0;
    g_func_pass.for_decl_seq = -1;
    g_func_pass.for_decl_rename_index = 0;
    g_func_pass.for_decl_recording = 0;
    g_func_pass.scope_depth = 0;
    g_func_pass.static_local_func_index = (int)(s - globals);
    g_func_pass.static_local_seq = 0;
    g_func_pass.compound_literal_seq = 0;
    g_func_pass.licm_seq = 0;
}

/*
 * Undo everything a discarded speculative body-generation attempt touched, so
 * the caller's normal codegen path runs exactly as if the attempt never
 * happened: rewind the lexer cursor and frame-layout counters to the captured
 * body-start snapshot, then reset the per-function codegen counters. Shared by
 * all three speculative passes' failure paths.
 */
static void speculative_body_discard_rewind(struct Sym *s,
                                            long body_start_pos,
                                            long body_start_tok_start,
                                            int body_start_line,
                                            int body_start_tok_line,
                                            struct Token body_start_tok,
                                            int body_start_nlocals,
                                            int body_start_local_size)
{
    g_lex.posi = body_start_pos;
    g_lex.tok_start_pos = body_start_tok_start;
    g_lex.line_no = body_start_line;
    g_lex.tok_line = body_start_tok_line;
    g_lex.tok = body_start_tok;
    g_frame.nlocals = body_start_nlocals;
    g_frame.local_size = body_start_local_size;
    reset_function_codegen_state(s);
}

/* Speculatively generate `name`'s already-scanned body without an IX frame
 * (params/locals addressed sp-relative - see current_function_safe_to_omit_ix,
 * which stays hard-disabled for the ordinary path below), and check whether
 * it ever pushed anything. A push is the only way that addressing can go
 * wrong: it shifts SP out from under every sp+N address already computed,
 * and dcc has no live SP-delta tracking to compensate (auditing every push
 * emission site to add that tracking would be a much larger, more invasive
 * change - see the long comment on current_function_safe_to_omit_ix). Rather
 * than prove push-freedom statically, generate into a scratch buffer and
 * inspect the result: no push anywhere means SP provably never moved, so
 * every address in the buffer is correct and it can be used as-is; a push
 * means discard it and let the caller regenerate normally with the IX frame,
 * via the exact same rewind this function performs on failure.
 *
 * Returns 1 if the no-IX version was kept and already written to g_emit_sink.stream; 0 if
 * the caller must still run the normal (IX-framed) codegen path itself, with
 * every relevant piece of parser/codegen state already rewound to the body
 * start as if this function had never been called - the same state the
 * scan-to-codegen reset just above this call already established. */
int try_speculative_noix_function_body(const char *name, int type,
                                                     int local_bytes, struct Sym *s,
                                                     long body_start_pos,
                                                     long body_start_tok_start,
                                                     int body_start_line,
                                                     int body_start_tok_line,
                                                     struct Token body_start_tok,
                                                     int body_start_nlocals,
                                                     int body_start_local_size)
{
    FILE *scratch;
    EmitSink saved_sink;
    int saved_stack_check;
    int generated_stack_check;
    int implicit_zero_return;
    int c;
    int errors_before;

    implicit_zero_return = strcmp(name, "main") == 0 &&
                            (type & 15) == TYPE_INT && type_ptr_depth(type) == 0;

    scratch = tmpfile();
    if (scratch == NULL)
        fatal("cannot create speculative no-ix-frame temp file");

    saved_stack_check = opt_stack_check;
    saved_sink = emit_sink_push(scratch, EMIT_SINK_VERIFY);
    opt_stack_check = s->stack_check_enabled;
    /* emit_runtime_extrn_if_needed (dcc_symbols.c) caches which runtime-
     * helper EXTRNs have already been emitted in a *persistent*,
     * compilation-wide table, so a helper's declaration is normally only
     * ever written once no matter how many call sites reference it. That
     * cache has no idea this generation is speculative and may be thrown
     * away: without g_inline_body_buffering set, a call emitted here (e.g.
     * -fstack-check's `call __stchk` in every prologue) marks the helper as
     * "already declared" globally, and if this attempt is then discarded,
     * the real fallback generation's identical call skips its own EXTRN
     * line - producing a `call` with no matching declaration anywhere in
     * the actual output. g_inline_body_buffering is the existing mechanism
     * for exactly this hazard (see emit_runtime_extrn_if_needed and the
     * static-inline-body-buffering branch above): each buffered attempt gets
     * its own EXTRN dedup scope (reset via g_buffering_epoch, bumped here),
     * so a self-contained attempt's EXTRNs are complete and correct whether
     * it's kept or discarded, instead of relying on the global cache. */
    g_inline_body_buffering++;
    g_buffering_epoch++;
    reset_function_codegen_state(s);
    /* Suppress diagnostics for the duration of this possibly-discarded
     * attempt (asm_suppress_depth, checked by dcc_error_at) so a genuine
     * source error isn't shown to the user before we know whether the real
     * fallback pass will re-encounter and correctly report it exactly once
     * - but g_diag_error_count still counts every call regardless of
     * suppression, so a real error occurring here can still force a decline
     * below rather than being silently swallowed if this attempt would
     * otherwise have been kept. */
    errors_before = g_diag_error_count;
    asm_suppress_depth++;
    emit_function_prologue(name, local_bytes, 1);
    gen_compound();
    emit_function_epilogue(implicit_zero_return);
    asm_suppress_depth--;
    g_inline_body_buffering--;
    generated_stack_check = opt_stack_check;
    opt_stack_check = saved_stack_check;
    emit_sink_restore(&saved_sink);

    /* check_undefined_user_labels() is deliberately not called above: if
     * this attempt is about to be discarded, calling it here would both
     * double-report a genuine undefined-label error (the caller's normal
     * codegen path below already calls it once) and, more subtly, leave
     * ulabel_defined[]/nulabels populated from this attempt's goto/label
     * bookkeeping for the caller's fresh gen_compound() run to collide
     * with - exactly the "duplicate goto label" false positive this
     * function's first version produced by forgetting to reset nulabels
     * (and the rest of the per-function codegen state below) before
     * falling back. */
    if (g_diag_error_count == errors_before && !tmpfile_unsafe_for_noix(scratch)) {
        check_undefined_user_labels();
        rewind(scratch);
        while ((c = fgetc(scratch)) != EOF)
            fputc(c, g_emit_sink.stream);
        fclose(scratch);
        opt_stack_check = generated_stack_check;
        return 1;
    }

    fclose(scratch);

    /* Undo every bit of per-function codegen state this discarded attempt
     * touched - the same set the scan-to-codegen transition above this
     * function resets - so the caller's normal codegen path runs exactly as
     * if this function had never been called. */
    speculative_body_discard_rewind(s, body_start_pos, body_start_tok_start,
                                    body_start_line, body_start_tok_line,
                                    body_start_tok, body_start_nlocals,
                                    body_start_local_size);
    return 0;
}

/* Cheap pre-filter for try_speculative_bc_regalloc_function_body: main() is
 * excluded (never a hot leaf; the __mrun-shim emission after this codegen
 * assumes the ordinary frame shape), and current_function_has_call must be
 * false - this only detects an explicit C call syntactically present in the
 * source (see the scan pass, dcc_func.c ~line 2295), NOT an implicit
 * runtime-helper call (e.g. `call __mulu`) codegen may still insert for a
 * `*`, `/`, `%`, or long/float operation with no visible call syntax at all -
 * so it is only a pre-filter, never the actual safety proof. That proof is
 * regalloc_buffer_finalize below, which also rejects any "call" found in
 * the generated buffer that isn't to one of a small set of DCCRTL.MAC-
 * contracted runtime helpers known to preserve BC (buf_has_unsafe_call),
 * regardless of what this pre-filter guessed.
 *
 * Returns 1 whenever it's worth attempting speculative generation at all -
 * regardless of whether find_bc_regalloc_candidate finds a BC pointer
 * candidate, since an E-counter candidate (find_bc_regalloc_candidate's
 * counterpart in dcc_decl.c's gen_local_decl_after_type, gated on g_e_
 * regalloc_claim_active) is only discovered during the speculative
 * gen_compound() walk itself, not knowable in advance the way a parameter
 * candidate is. */
int function_qualifies_for_speculative_regalloc(const char *name)
{
    if (strcmp(name, "main") == 0)
        return 0;
    if (current_function_has_call)
        return 0;
    return 1;
}

/* The IY counterpart, and deliberately the near-inverse of the test above.
 *
 * A call is a DISQUALIFIER for BC and a PRECONDITION for IY. In a call-free
 * function BC is available and strictly cheaper on every count (30 T-states
 * saved per reference against 13, no callee-save pair, no priming through
 * HL), so the BC path will already have taken it and IY would only be a
 * pessimisation. IY's entire purpose is to reach the functions BC cannot.
 *
 * Requiring the flag rather than relying on ordering alone was measured, not
 * assumed. Dropping it - on the reasoning that IY is the last attempt in the
 * chain and so only ever gets what BC declined - admits IY into bodies where
 * BC declined for reasons that have nothing to do with call safety, and the
 * corpus rejected that decisively: 3 apps regressed (00040b +1.1M, pint
 * +354K, tvlax +341K), two tests failed outright, and net cycles went from
 * -10.30M to -9.35M. The flag is doing real work as a proxy for "a register
 * that survives calls is the only kind that helps here".
 *
 * main() is excluded for the same reason it is excluded from the other
 * speculative paths: the CRT's __mrun shim depends on its exact frame shape,
 * and the callee-save push would change it. */
int function_qualifies_for_speculative_iy_regalloc(const char *name)
{
    const char *off;

    /* Kill switch, for bisecting a suspected IY-related miscompile against an
     * otherwise identical build. Set to "1" to disable IY promotion entirely,
     * or to a comma-separated list of function names to disable it for just
     * those. */
    off = getenv("DCC_NO_IY_REGALLOC");
    if (off != NULL) {
        if (strcmp(off, "1") == 0)
            return 0;
        if (strstr(off, name) != NULL)
            return 0;
    }
    if (strcmp(name, "main") == 0)
        return 0;
    if (!current_function_had_call_at_scan)
        return 0;
    return 1;
}

/* Ported from dccpeep.c's line_touches_reg_pair (proven correct there across
 * today's dccpeep pass work) rather than reinvented: true if `s` references
 * register B, C, or the BC pair as an operand anywhere in the line - as
 * opposed to merely containing the letter 'b' or 'c' as part of some
 * unrelated identifier or label, which a plain strstr would false-positive
 * on. Z80 instructions that touch BC only implicitly (block/repeat opcodes)
 * are matched by mnemonic; dcc's own codegen does not currently emit any of
 * these, but the check costs nothing and avoids silently trusting that fact
 * to remain true forever. */
/* Generalized from a BC-only original (see git history for the fill_record
 * "jp c,LABEL" false-positive this flag-condition exclusion fixes): true if
 * `s` references register `lo`, `hi`, or the pair `pair` as an operand
 * anywhere in the line - as opposed to merely containing that letter as
 * part of some unrelated identifier/label. Z80 instructions that touch a
 * pair only implicitly (block/repeat opcodes) are matched by mnemonic;
 * dcc's own codegen does not currently emit any of these for BC, but the
 * check costs nothing. jp/jr/call's optional leading condition, or ret's
 * sole operand, can be exactly "c"/"nc" meaning the carry flag rather than
 * register C - excluded by tracking token position; harmless (never
 * matches) when checking d/e/de, since Z80 has no letter-named flag
 * condition using those letters. */
static int line_touches_reg_pair(const char *s, const char *lo, const char *hi, const char *pair)
{
    static const char *implicit_pair_mnemonics[] = {
        "djnz ", "ldir", "lddr", "cpir", "cpdr",
        "otir", "otdr", "inir", "indr",
        "ldi", "ldd", "cpi", "cpd", "ini", "ind", "outi", "outd",
        NULL
    };
    static const char *cond_jump_mnemonics[] = { "jp", "jr", "call", "ret", NULL };
    const char *p;
    char tokbuf[16];
    char paren[8];
    int ti, i;
    int tok_index;
    int first_is_cond_mnemonic;

    for (i = 0; implicit_pair_mnemonics[i] != NULL; ++i)
        if (strncmp(s, implicit_pair_mnemonics[i], strlen(implicit_pair_mnemonics[i])) == 0)
            return 1;

    sprintf(paren, "(%s)", pair);
    if (strstr(s, paren) != NULL)
        return 1;

    p = s;
    tok_index = 0;
    first_is_cond_mnemonic = 0;
    while (*p) {
        if (isalpha((unsigned char)*p) || *p == '_') {
            ti = 0;
            while ((isalnum((unsigned char)*p) || *p == '_') && ti < 15)
                tokbuf[ti++] = *p++;
            tokbuf[ti] = 0;
            if (tok_index == 0) {
                for (i = 0; cond_jump_mnemonics[i] != NULL; ++i)
                    if (strcmp(tokbuf, cond_jump_mnemonics[i]) == 0)
                        first_is_cond_mnemonic = 1;
            } else if (tok_index == 1 && first_is_cond_mnemonic &&
                       (strcmp(tokbuf, "c") == 0 || strcmp(tokbuf, "nc") == 0)) {
                /* flag condition, not register C - not a touch */
            } else if (strcmp(tokbuf, lo) == 0 || strcmp(tokbuf, hi) == 0 || strcmp(tokbuf, pair) == 0) {
                return 1;
            }
            tok_index++;
        } else {
            p++;
        }
    }
    return 0;
}

int line_touches_bc_reg(const char *s)
{
    return line_touches_reg_pair(s, "b", "c", "bc");
}

static int line_touches_de_reg(const char *s)
{
    return line_touches_reg_pair(s, "d", "e", "de");
}

/* Runtime helpers DCCRTL.MAC documents (see the CONTRACT comment just above
 * __divs there) as preserving BC across the call: their fast paths never
 * touch b/c/bc at all, and their slow paths explicitly push/pop it. This is
 * the same trust dccpeep's pass_byte_loop_counter_to_reg_c already relies on
 * for __mods/__divs specifically; extended here to the full set DCCRTL.MAC's
 * comment names, since dcc's own codegen (dcc_ops.c, dcc_ast_gen_stmt.c) can
 * emit a call to any of the seven for a plain `*`, `/`, or `%` on int - none
 * of which appear as an AST_CALL node, so no AST-level scan can ever see
 * them; this text-level check is the only place they're visible at all.
 *
 * The twelve ctype.h entries after them (isalpha through tolower) are a
 * second, independently verified group: unlike the arithmetic seven, these
 * DO appear as ordinary AST_CALL nodes (see dcc_licm.c's licm_scan_modified,
 * whose AST_CALL case checks asm_name_is_bc_safe_call below before deciding
 * whether to decline a loop containing one at all - this array alone only
 * gates the text-level re-check once such a loop's speculative attempt has
 * already been allowed to proceed). Confirmed by direct inspection of every
 * one of DCCRTL.MAC's "ctype helpers with short external names" (the block
 * starting at its own "Character classification and conversion" comment):
 * every single one reads its argument through IX/SP-relative addressing and
 * classifies it using only A, HL, and flags - none reference B, C, or BC in
 * any form, so none need the push/pop-around-a-clobber escape the arithmetic
 * seven's slow paths use. Any OTHER runtime helper (float conversions,
 * BDOS/BIOS calls, __stchk, __call_hl, long-math variants, string/memory
 * functions - strcmp and strlen in particular were checked and DO use BC,
 * as a stack-argument/CPIR scratch register respectively - ...) carries no
 * such verified contract and is deliberately left out - a bare call to any
 * of those still fails the whole attempt, exactly as before this whitelist
 * existed. */
static const char *g_safe_runtime_calls[] = {
    "__mulu", "__udivmod", "__divu", "__modu", "__divs", "__mods", "__sdivmod",
    "__caa", "__can", "__csp", "__cdg", "__cup", "__clo",
    "__cxd", "__cpr", "__cct", "__cpu", "__ctu", "__ctl",
    NULL
};

/* True if `name` (an asm-level call target, e.g. "__csp" for isspace - see
 * asm_name_for(sym_asm_name(s))) is on g_safe_runtime_calls above. Exposed
 * non-static for dcc_licm.c's licm_scan_modified, which needs this same
 * verified-safe set at the AST level (by the callee's real asm name, since
 * the DCCRTL.MAC short-name remapping only happens at that point - the C
 * name "isspace" itself is not what appears in g_safe_runtime_calls) to
 * decide whether a loop containing such a call is even eligible to attempt
 * BC promotion in the first place - see that function's own comment. */
int asm_name_is_bc_safe_call(const char *name)
{
    int i;

    for (i = 0; g_safe_runtime_calls[i] != NULL; i++) {
        if (strcmp(name, g_safe_runtime_calls[i]) == 0)
            return 1;
    }
    return 0;
}

/* True if `name` (an asm-level call target, e.g. "_Z0001" for a static
 * function or "_foo" for a public one - see sym_asm_name) is a function
 * declared _Noreturn. Checked in addition to g_safe_runtime_calls: a call
 * to such a function is trustworthy for a completely different reason than
 * the DCCRTL.MAC contract above - not because it preserves bc/de, but
 * because control never returns to any point after it, so whatever it
 * clobbers can never be read back through this speculative attempt's
 * promoted candidate on that path. This is the text-level counterpart to
 * dcc_licm.c's AST-level tolerance for the same case (licm_scan_modified's
 * AST_CALL handling) - that scan makes the loop eligible to ATTEMPT
 * promotion in the first place; this is what makes the attempt actually
 * verify once the call's real asm name is visible in the generated text
 * (found via forint.c's eval_e: get_sym_val inlines cleanly, but its own
 * callee cell_at still has a genuine, non-inlined `call` to die() for its
 * bounds check, which is _Noreturn but not itself inline-substitutable). */
int asm_name_is_noreturn_call(const char *name)
{
    int i;

    for (i = 0; i < nglobals; i++) {
        if (globals[i].storage == SC_FUNC && globals[i].is_noreturn &&
            strcmp(asm_name_for(globals[i].name), name) == 0)
            return 1;
    }
    return 0;
}

/* True if `buf` contains a "\tcall NAME" line whose NAME is neither on
 * g_safe_runtime_calls above nor a call to a _Noreturn function (see
 * asm_name_is_noreturn_call) - i.e. true if there is at least one call this
 * speculative attempt cannot trust. NAME is taken as running from just after
 * "\tcall " to end of line (or a trailing comment/condition would break this,
 * but dcc's own codegen never emits either after a call's target). */
int buf_has_unsafe_call(const char *buf)
{
    static const char prefix[] = "\tcall ";
    const size_t prefix_len = sizeof(prefix) - 1;
    const char *p;

    p = buf;
    for (;;) {
        const char *hit = strstr(p, prefix);
        const char *name_start, *name_end;
        char namebuf[32];
        size_t namelen;
        int whitelisted;

        if (hit == NULL)
            return 0;
        name_start = hit + prefix_len;
        name_end = name_start;
        while (*name_end != '\0' && *name_end != '\n' && *name_end != ' ' &&
               *name_end != '\t' && *name_end != ';')
            name_end++;
        namelen = (size_t)(name_end - name_start);
        if (namelen >= sizeof(namebuf))
            namelen = sizeof(namebuf) - 1;
        memcpy(namebuf, name_start, namelen);
        namebuf[namelen] = 0;

        whitelisted = asm_name_is_bc_safe_call(namebuf) || asm_name_is_noreturn_call(namebuf);
        if (!whitelisted)
            return 1;

        p = name_end;
    }
}

#define MAX_BC_LOOP_LABELS 512

static int bc_label_name_index(char names[][16], int n, const char *name)
{
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(names[i], name))
            return i;
    return -1;
}

/* regalloc_buffer_finalize's bc_trusted tracking below is a single linear
 * scan of the generated text with no notion of control flow: it is sound for
 * straight-line code and for if/else (each branch is its own straight-line
 * span, visited once, correctly reflecting that branch's own history up to
 * that point), but not for a loop. A label that is both fallen into once
 * AND reached again via a backward jump (the loop's back-edge) is only
 * visited ONCE by the linear scan, in file order - any bc-clobbering that
 * happens later in the loop body, between that label and the jump back to
 * it, is invisible to the scan on every visit after the first, because there
 * is no second textual visit to react to. Found via tests/tlongidx.c: `long
 * i` incremented as an array index (`in[i++]`) inside a while loop clobbers
 * bc (used as scratch for the 32-bit increment's address), gets correctly
 * reloaded before the loop's own later, first-encountered use, but the scan
 * has already permanently marked bc "trusted" by the time it reaches the
 * loop header text again on paper - so the second real iteration silently
 * reused stale bc instead of the reloaded value.
 *
 * Scans the whole buffer once up front for every "LNN:" label that is the
 * target of a "jp"/"jr" (conditional or not) appearing AFTER that label's
 * own definition in the text - i.e. a genuine backward jump, not a forward
 * skip - and returns the set of such labels. The caller forces bc_trusted
 * false at every one of them, which costs at most one possibly-unneeded
 * reload on the label's first (fall-through) visit, in exchange for
 * correctness on every subsequent (looped) visit - the same trade-off this
 * whole pass already makes for "push bc ... pop bc" (see the comment above
 * regalloc_buffer_finalize). */
static void bc_regalloc_find_loop_headers(const char *buf, long size,
                                           char headers[][16],
                                           long header_offs[], long body_end_offs[],
                                           int *n_headers)
{
    char seen_names[MAX_BC_LOOP_LABELS][16];
    long seen_offs[MAX_BC_LOOP_LABELS];
    int n_seen;
    const char *p;
    const char *nl;
    char linebuf[64];
    size_t ll;
    long line_off;

    n_seen = 0;
    *n_headers = 0;
    p = buf;
    while (p < buf + size) {
        nl = memchr(p, '\n', (size_t)(buf + size - p));
        ll = nl ? (size_t)(nl - p) : (size_t)(buf + size - p);
        if (ll >= sizeof(linebuf)) ll = sizeof(linebuf) - 1;
        memcpy(linebuf, p, ll);
        linebuf[ll] = 0;
        line_off = (long)(p - buf);

        if (linebuf[0] == 'L' && isdigit((unsigned char)linebuf[1])) {
            char *colon = strchr(linebuf, ':');
            if (colon != NULL && n_seen < MAX_BC_LOOP_LABELS) {
                *colon = 0;
                dcc_copy_str(seen_names[n_seen], sizeof(seen_names[0]), linebuf);
                seen_offs[n_seen] = line_off;
                n_seen++;
            }
        } else if (strncmp(linebuf, "\tjp ", 4) == 0 || strncmp(linebuf, "\tjr ", 4) == 0) {
            char *comma = strrchr(linebuf, ',');
            char *tok = comma ? comma + 1 : linebuf + 4;
            while (*tok == ' ') tok++;
            if (tok[0] == 'L' && isdigit((unsigned char)tok[1])) {
                int si = bc_label_name_index(seen_names, n_seen, tok);
                if (si >= 0) {
                    long end_off = (long)((nl ? nl + 1 : buf + size) - buf);
                    int hi = bc_label_name_index(headers, *n_headers, tok);
                    if (hi < 0 && *n_headers < MAX_BC_LOOP_LABELS) {
                        dcc_copy_str(headers[*n_headers], sizeof(headers[0]), tok);
                        header_offs[*n_headers] = seen_offs[si];
                        body_end_offs[*n_headers] = end_off;
                        (*n_headers)++;
                    } else if (hi >= 0 && end_off > body_end_offs[hi]) {
                        /* a second (e.g. "continue"-style) back-edge to the
                         * same header - widen the body span to cover it too */
                        body_end_offs[hi] = end_off;
                    }
                }
            }
        }

        p = nl ? nl + 1 : buf + size;
    }
}

/* Machine-readable register-claim directives, the explicit replacement for
 * the text signature dccpeep used to have to infer a claim from ("ld c,(ix"
 * / "ld b,(ix" - see line_is_regalloc_bc_priming, dccpeep.c). A claim states
 * WHICH register is reserved, over WHAT scope, for WHICH symbol, and - once
 * the cost model lands - what the claim is WORTH, so dccpeep can decide
 * whether its own candidate for that register is the better one rather than
 * simply always yielding.
 *
 * The pairing rule is deliberately simple, because dccpeep consumes these by
 * a linear text walk: a claim stays live until the matching free directive,
 * or until the end of the enclosing function if there is none. A whole-
 * function claim therefore emits no free at all; a loop-scoped one emits its
 * free immediately after the loop (read-only) or after the spill store
 * (write), at which point BC really is dead and dccpeep is free to use it
 * for the rest of the function - the single largest source of missed
 * optimisation under the old "claimed once, claimed forever" rule.
 *
 * Emitted as comments so they are inert to the assembler, and skipped by
 * every speculative verifier's scan (regalloc_buffer_finalize,
 * bc_loop_body_self_consistent, loop_regalloc_write_candidate_safe) on the
 * shared "comment-only lines execute nothing" guard - which also stops the
 * bare "bc" token in the directive itself from tripping line_touches_bc_reg. */
void emit_regalloc_claim(const char *reg, const char *scope,
                         struct Sym *cand, const char *kind, long value)
{
    fprintf(g_emit_sink.stream, ";@dcc.reg claim=%s scope=%s sym=%s kind=%s val=%ld\n",
            reg, scope, cand->name, kind, value);
}

void emit_regalloc_free(const char *reg)
{
    fprintf(g_emit_sink.stream, ";@dcc.reg free=%s\n", reg);
}

/* Estimated dynamic cycles saved by holding `cand` in a 16-bit register
 * across `refs` references sitting at loop nesting depth `depth`.
 *
 * The per-reference figures are exact for this target, not guesses. A
 * local/param word lives in a frame slot, so a read is "ld l,(ix+d)" plus
 * "ld h,(ix+d+1)" = 19+19 = 38 T-states, against "ld l,c"/"ld h,b" = 4+4 =
 * 8 from BC: 30 saved per read, and the one-off prime that establishes it
 * costs that same 38. A global word reads as "ld hl,(nn)" = 16 T-states
 * against the same 8, so only 8 is saved per read, and its prime is the
 * 16-T load plus an 8-T transfer into the pair = 24. That asymmetry matters:
 * a global needs nearly four times as many references as a local before it
 * is worth the same register.
 *
 * Depth is weighted by the conventional factor of 8 per nesting level -
 * dcc has the exact nesting depth available from the AST for free, and
 * without it a reference inside a doubly-nested loop would be ranked
 * identically to one in straight-line code, which is precisely the ranking
 * failure that made every previous cost gate collapse into "whoever asked
 * first wins". Clamped so a deeply nested candidate cannot overflow into a
 * nonsense value. */
long regalloc_estimate_value(struct Sym *cand, int refs, int depth)
{
    long per_ref, prime, weight;
    int is_global = (cand->storage == SC_GLOBAL || cand->storage == SC_EXTERN);

    per_ref = is_global ? 8 : 30;
    prime = is_global ? 24 : 38;
    weight = 1;
    while (depth-- > 0 && weight <= 4096)
        weight *= 8;
    return (long)refs * per_ref * weight - prime;
}

/* Computes a BC-resident candidate's own fixed priming-instruction text, as
 * up to 3 lines (no trailing newline on any of them - callers add their
 * own). Shared by bc_loop_body_self_consistent and regalloc_buffer_finalize
 * (this file) and loop_regalloc_write_candidate_safe (dcc_loop_regalloc.c)
 * so all three recognize/reinsert exactly the same text a real prime emits -
 * see try_loop_regalloc_bc/try_loop_regalloc_bc_write (dcc_loop_regalloc.c)
 * for the actual emission this must stay in lockstep with.
 *
 * A local/param candidate's frame slot supports the ordinary 2-instruction
 * "ld c,(ix+d)"/"ld b,(ix+d+1)" pair. A global has no such direct absolute
 * BC load on Z80 (there is no "ld bc,(nn)" opcode this codebase's target
 * assembler recognizes for this form) - its prime is a 3-instruction
 * sequence instead, mirroring emit_load_global_word_direct (dcc_symbols.c)
 * plus a transfer into bc. Returns the line count (2 or 3). */
int bc_regalloc_entry_lines(struct Sym *cand, char lines[3][40])
{
    if (cand->storage == SC_GLOBAL || cand->storage == SC_EXTERN) {
        sprintf(lines[0], "\tld hl,(%s)", asm_name_for(sym_asm_name(cand)));
        strcpy(lines[1], "\tld c,l");
        strcpy(lines[2], "\tld b,h");
        return 3;
    }
    sprintf(lines[0], "\tld c,(ix%+d)", cand->offset);
    sprintf(lines[1], "\tld b,(ix%+d)", cand->offset + 1);
    return 2;
}

/* Spill-side counterpart of bc_regalloc_entry_lines, used only by a write
 * candidate's own verifier (loop_regalloc_write_candidate_safe, dcc_loop_
 * regalloc.c) to recognize try_loop_regalloc_bc_write's own spill text.
 * Local/param: "ld (ix+d),c"/"ld (ix+d+1),b" (2 lines). Global: transfer bc
 * into hl then store it absolutely, mirroring emit_store_global_word_direct
 * (dcc_symbols.c) - "ld l,c"/"ld h,b"/"ld (name),hl" (3 lines; the first two
 * are also already part of the generic recognized-line set every write
 * candidate's verifier accepts, so only the final store is genuinely new
 * text, but returning all 3 keeps this and bc_regalloc_entry_lines
 * symmetric and equally simple for callers to use). Returns the line count
 * (2 or 3). */
int bc_regalloc_exit_lines(struct Sym *cand, char lines[3][40])
{
    if (cand->storage == SC_GLOBAL || cand->storage == SC_EXTERN) {
        strcpy(lines[0], "\tld l,c");
        strcpy(lines[1], "\tld h,b");
        sprintf(lines[2], "\tld (%s),hl", asm_name_for(sym_asm_name(cand)));
        return 3;
    }
    sprintf(lines[0], "\tld (ix%+d),c", cand->offset);
    sprintf(lines[1], "\tld (ix%+d),b", cand->offset + 1);
    return 2;
}

/* Precise per-loop refinement of the above: a label only truly NEEDS bc
 * forced untrusted if its own body is not internally self-consistent - i.e.
 * simulating the exact same trust-transition rules regalloc_buffer_finalize
 * uses below, starting from bc_trusted=1 (the state the label is in by the
 * time of its second and later visits, after any reload the real scan
 * already inserts on the first, fall-through visit), the body does NOT end
 * back at bc_trusted=1 by its own back-edge. If it does, every iteration is
 * identical to the first, and forcing a reload at the header is pure waste
 * (found via tests/tbig.c's fill_record: a leaf loop that reads the bc-
 * resident pointer every iteration and never writes b/c/bc at all, yet paid
 * for a fresh two-byte reload on all 124 iterations under the older,
 * unconditional version of this pass).
 *
 * This mirrors, rather than reimplements, regalloc_buffer_finalize's own
 * is_bc_value_read_start / is_bc_recognized_other / line_touches_bc_reg
 * predicates on purpose - a hand-rolled second classifier could silently
 * diverge from what the real scan actually does and reintroduce exactly the
 * kind of blind spot this whole mechanism exists to avoid. */
static int bc_loop_body_self_consistent(const char *buf, long start, long end,
                                         const char entry_lines[3][40], int n_entry_lines)
{
    const char *p, *nl;
    char linebuf[64], prev1[64];
    size_t ll;
    int bc_trusted;
    int is_bc_value_read_start, is_bc_recognized_other;
    int k;

    bc_trusted = 1;
    prev1[0] = 0;
    p = buf + start;
    while (p < buf + end) {
        nl = memchr(p, '\n', (size_t)(buf + end - p));
        ll = nl ? (size_t)(nl - p) : (size_t)(buf + end - p);
        if (ll >= sizeof(linebuf)) ll = sizeof(linebuf) - 1;
        memcpy(linebuf, p, ll);
        linebuf[ll] = 0;

        /* Comment-only lines execute nothing and must not affect trust
         * tracking or the prev1 adjacency state - see the identical guard in
         * regalloc_buffer_finalize's main scan (a ";@dcc-regalloc-bc-prime"
         * marker's bare "bc" token would otherwise trip line_touches_bc_reg).
         * This scan produces no output, so it just advances past the line. */
        {
            const char *cq = linebuf;
            while (*cq == ' ' || *cq == '\t') cq++;
            if (*cq == ';') {
                p = nl ? nl + 1 : buf + end;
                continue;
            }
        }

        is_bc_value_read_start =
            ((strcmp(linebuf, "\tld l,c") == 0 && strcmp(prev1, "\tld h,b") != 0) ||
             (strcmp(linebuf, "\tld e,c") == 0 && strcmp(prev1, "\tld d,b") != 0));
        is_bc_recognized_other =
            (strcmp(linebuf, "\tld h,b") == 0 || strcmp(linebuf, "\tld d,b") == 0 ||
             (strcmp(linebuf, "\tld l,c") == 0 && strcmp(prev1, "\tld h,b") == 0) ||
             (strcmp(linebuf, "\tld e,c") == 0 && strcmp(prev1, "\tld d,b") == 0));
        if (!is_bc_recognized_other) {
            for (k = 0; k < n_entry_lines; k++) {
                if (strcmp(linebuf, entry_lines[k]) == 0) {
                    is_bc_recognized_other = 1;
                    break;
                }
            }
        }

        if (is_bc_value_read_start)
            bc_trusted = 1;
        else if (!is_bc_recognized_other && line_touches_bc_reg(linebuf))
            bc_trusted = 0;

        dcc_copy_str(prev1, sizeof(prev1), linebuf);
        p = nl ? nl + 1 : buf + end;
    }
    return bc_trusted;
}

/* Exact safety verification and, for BC, on-demand-reload REWRITE, for
 * try_speculative_bc_regalloc_function_body.
 *
 * E is unchanged from before: strict decline-only. Every line touching d/e/
 * de (once e_cand's own value is live) must be one of the small recognized
 * shapes emit_store_hl_to_sym_direct/emit_incdec_sym_direct/emit_load_sym_
 * value_direct/ast_byte_operand's kind-1 hooks produce; anything else
 * discards the whole attempt. E has no shadow to fall back on (its frame
 * slot is never kept in sync - see gen_local_decl_after_type), so there is
 * nothing to reload from; a real clobber here is unrecoverable, not just
 * inconvenient.
 *
 * BC is different, and gets a genuinely more permissive treatment: bc_cand
 * is read-only by construction (find_bc_regalloc_candidate excludes any
 * candidate ever written to), so its ORIGINAL incoming-parameter stack slot
 * (ix+off / ix+off+1) never changes for the life of the function - it is
 * already a perfect, always-valid shadow, for free, with no bookkeeping
 * needed to keep it in sync. So instead of declining outright the moment
 * anything else touches b/c/bc (e.g. tbig.c's get_stamp parking a scratch
 * value via push bc/pop bc for unrelated long arithmetic), this pass tracks
 * whether bc is currently "trusted" (untouched by anything but a recognized
 * line since the last known-good point) as it walks forward, and - the
 * moment it's asked to trust bc again at a recognized value-read site while
 * untrusted - REWRITES the buffer, inserting a fresh reload from that
 * always-correct original slot right there, before continuing. This is
 * deliberately conservative in one direction: it does not attempt to prove
 * a "push bc ... pop bc" pair actually restores the original value (which
 * it usually does) and skip the reload in that case - every untrusted point
 * gets a reload whether or not one was strictly needed, trading a few extra
 * instructions for staying exact rather than tracking real stack-balance
 * semantics from flat text.
 *
 * A "call" to anything other than one of g_safe_runtime_calls' seven
 * DCCRTL.MAC-contracted helpers (see buf_has_unsafe_call above) still fails
 * the whole attempt outright, for both candidates: current_function_has_call
 * only detects an explicit C call syntactically present in the source, not
 * an implicit runtime-helper call (e.g. `call __mulu`) codegen may still
 * insert for a `*`, `/`, `%`, or long/float operation with no call syntax
 * visible at all - this feature's leaf-only gate otherwise depends on there
 * being truly zero calls of any kind, and an arbitrary call's effect on
 * bc/de is not something a reload can safely paper over (unlike a same-
 * function scratch use, it's not visible in this text at all); the seven
 * whitelisted helpers are the sole exception, trusted by documented contract
 * rather than by anything this scan itself can verify. Neither candidate's
 * own address may ever be taken either - see g_regalloc_address_escaped
 * (dcc_symbols.c), checked separately by the caller.
 *
 * On success, *out_f is a rewound tmpfile holding the (possibly BC-
 * reload-rewritten) content to commit - the caller must fclose it. On
 * failure, *out_f is untouched.
 *
 * Not static: dcc_loop_regalloc.c calls this directly to verify a single
 * loop's speculatively-generated body, passing e_cand=NULL (loop-scoped
 * promotion only ever targets BC, matching find_bc_regalloc_candidate's own
 * word-sized/read-only/never-address-taken candidate shape - see that
 * file). Nothing about this scan assumes its buffer covers a whole function
 * body rather than one loop's own emitted span - the "any call anywhere"
 * check and the loop-header self-consistency check are exactly as sound,
 * and exactly as needed, scoped to just a loop's own text. */
int regalloc_buffer_finalize(FILE *f, struct Sym *bc_cand, struct Sym *e_cand,
                              FILE **out_f)
{
    long size;
    char *buf;
    char *line, *nl;
    char entry_lines[3][40];
    int n_entry_lines = 0;
    int safe;
    int e_live;
    int bc_trusted;
    char prev1[32], prev2[32];
    FILE *rewritten;
    char loop_headers[MAX_BC_LOOP_LABELS][16];
    int n_loop_headers;
    char fwd_untrusted[MAX_BC_LOOP_LABELS][16];
    int n_fwd_untrusted;

    rewritten = tmpfile();
    if (rewritten == NULL)
        fatal("cannot create speculative regalloc rewrite temp file");

    buf = dcc_read_stream_text(f, &size,
                               "cannot read speculative regalloc temp file");
    if (size <= 0) {
        free(buf);
        rewind(rewritten);
        *out_f = rewritten;
        return 1;
    }

    if (buf_has_unsafe_call(buf)) {
        free(buf);
        fclose(rewritten);
        return 0;
    }

    if (bc_cand != NULL) {
        char all_headers[MAX_BC_LOOP_LABELS][16];
        long header_offs[MAX_BC_LOOP_LABELS];
        long body_end_offs[MAX_BC_LOOP_LABELS];
        int n_all, hi;

        n_entry_lines = bc_regalloc_entry_lines(bc_cand, entry_lines);
        bc_regalloc_find_loop_headers(buf, size, all_headers, header_offs, body_end_offs, &n_all);

        n_loop_headers = 0;
        for (hi = 0; hi < n_all; hi++) {
            if (!bc_loop_body_self_consistent(buf, header_offs[hi], body_end_offs[hi],
                                               entry_lines, n_entry_lines)) {
                dcc_copy_str(loop_headers[n_loop_headers], sizeof(loop_headers[0]), all_headers[hi]);
                n_loop_headers++;
            }
        }
    } else {
        n_loop_headers = 0;
    }

    safe = 1;
    e_live = 0;
    bc_trusted = 1;
    n_fwd_untrusted = 0;
    prev1[0] = 0;
    prev2[0] = 0;
    line = buf;
    while (safe && line < buf + size) {
        int is_bc_value_read_start;
        int is_bc_recognized_other;
        int is_recognized_e_line;
        int is_recognized_e_index_swap;
        int is_universally_safe_de_line;

        nl = strchr(line, '\n');
        if (nl) *nl = 0;

        /* A comment-only line (first non-blank char ';') executes nothing -
         * in particular the ";@dcc-regalloc-bc-prime" marker emit_function_
         * prologue and the loop primers plant purely so dccpeep's own
         * bc_regalloc_claimed_before can tell dcc's global BC prime apart
         * from its identical-looking global_word_cache_store. Such a line
         * must be emitted verbatim but take no part in trust tracking: the
         * token scan line_touches_bc_reg uses would otherwise see the bare
         * "bc" in "regalloc-bc-prime" as register BC and wrongly clear
         * bc_trusted right after the prime, splicing a redundant reload in
         * before the candidate's first real use. It must also leave prev1/
         * prev2 untouched, since those record the last real instruction for
         * the value-read adjacency test below. */
        {
            const char *cq = line;
            while (*cq == ' ' || *cq == '\t') cq++;
            if (*cq == ';') {
                fprintf(rewritten, "%s\n", line);
                line = nl ? nl + 1 : buf + size;
                continue;
            }
        }

        /* The two-line "ld l,c"/"ld h,b" or "ld e,c"/"ld d,b" pairs (emit_
         * load_sym_value_direct/emit_load_sym_de_direct's REG_BC branches)
         * are always emitted back-to-back with nothing in between, so the
         * FIRST line of either pair is the one decision point: if bc is
         * currently untrusted, insert a fresh reload from the candidate's
         * own never-written original parameter slot right before it, and
         * treat bc as trusted again from here on - the second line of the
         * pair, and the two entry-load lines themselves, never need their
         * own check.
         *
         * But "ld l,c"/"ld e,c" are not unique to that pair: gen_post_
         * update_from_addr and several long/pointer helpers (dcc_expr.c,
         * dcc_ast_gen_expr.c, dcc_ast_gen.c, dcc_ops.c) save an unrelated
         * address in BC as scratch and later restore it with "ld h,b"/"ld
         * l,c" (or "ld d,b"/"ld e,c") - textually the SAME two lines as the
         * value-read pair, but in the OPPOSITE order and for a completely
         * different purpose (BC already holds a scratch address there, not
         * the cached parameter). Misreading that restore's second line as a
         * fresh value-read-start inserted a reload mid-restore, splicing the
         * parameter's low byte into what should have been the scratch
         * address's low byte - found via tests/tlongidx.c hanging (long i;
         * ... in[i++] inside a while loop, with `in` the sole candidate
         * parameter). Disambiguate by checking the immediately preceding
         * line: "ld l,c"/"ld e,c" only starts a value-read when it is NOT
         * immediately preceded by "ld h,b"/"ld d,b" respectively - in that
         * case it is the restore pair's own second line, already covered by
         * is_bc_recognized_other so it does not disturb bc_trusted (which
         * was correctly cleared when the scratch address was first loaded
         * into bc). */
        /* Loop back-edge target: force bc untrusted here (see
         * bc_regalloc_find_loop_headers) regardless of how trusted the
         * single linear scan thinks bc is on this, its only textual visit -
         * a later iteration reaching this same label at runtime may not
         * share that history. */
        if ((n_loop_headers > 0 || n_fwd_untrusted > 0) &&
            line[0] == 'L' && isdigit((unsigned char)line[1])) {
            size_t llen = strlen(line);
            if (llen > 0 && line[llen - 1] == ':') {
                char labelbuf[16];
                size_t nlen = llen - 1;
                if (nlen >= sizeof(labelbuf)) nlen = sizeof(labelbuf) - 1;
                memcpy(labelbuf, line, nlen);
                labelbuf[nlen] = 0;
                /* Force bc untrusted at this label if it is a non-self-
                 * consistent loop back-edge target (loop_headers) OR the
                 * target of any forward jump taken while bc was untrusted
                 * (fwd_untrusted).  The latter closes the branch-join hole:
                 * the linear scan reflects only the textually-preceding
                 * (fall-through) edge, so at an if/else join whose then-arm
                 * clobbers bc and jumps here while the else-arm falls through
                 * trusted, the scan would otherwise leave bc "trusted" and
                 * skip the reload the then-arm path needs. */
                if (bc_label_name_index(loop_headers, n_loop_headers, labelbuf) >= 0 ||
                    bc_label_name_index(fwd_untrusted, n_fwd_untrusted, labelbuf) >= 0)
                    bc_trusted = 0;
            }
        }

        is_bc_value_read_start = bc_cand != NULL &&
            ((strcmp(line, "\tld l,c") == 0 && strcmp(prev1, "\tld h,b") != 0) ||
             (strcmp(line, "\tld e,c") == 0 && strcmp(prev1, "\tld d,b") != 0));
        is_bc_recognized_other = bc_cand != NULL &&
            (strcmp(line, "\tld h,b") == 0 || strcmp(line, "\tld d,b") == 0 ||
             (strcmp(line, "\tld l,c") == 0 && strcmp(prev1, "\tld h,b") == 0) ||
             (strcmp(line, "\tld e,c") == 0 && strcmp(prev1, "\tld d,b") == 0));
        if (bc_cand != NULL && !is_bc_recognized_other) {
            int k;
            for (k = 0; k < n_entry_lines; k++) {
                if (strcmp(line, entry_lines[k]) == 0) {
                    is_bc_recognized_other = 1;
                    break;
                }
            }
        }

        if (is_bc_value_read_start) {
            if (!bc_trusted) {
                int k;
                for (k = 0; k < n_entry_lines; k++)
                    fprintf(rewritten, "%s\n", entry_lines[k]);
                bc_trusted = 1;
            }
        } else if (!is_bc_recognized_other && bc_cand != NULL && line_touches_bc_reg(line)) {
            bc_trusted = 0;
        }
        fprintf(rewritten, "%s\n", line);

        /* Forward-branch path-sensitivity (companion to the label barrier
         * above): record the target of any jp/jr taken while bc is
         * untrusted, so when the scan later reaches that label it forces a
         * reload regardless of the (possibly trusted) fall-through edge.
         * bc_trusted is unchanged by a jump line itself - jp/jr never write
         * b/c/bc, and a "c"/"nc" flag condition is excluded by
         * line_touches_bc_reg - so it still reflects the taken edge's trust
         * here.  Backward/loop edges are covered by loop_headers +
         * bc_loop_body_self_consistent, not this. */
        if (bc_cand != NULL && !bc_trusted &&
            (strncmp(line, "\tjp ", 4) == 0 || strncmp(line, "\tjr ", 4) == 0)) {
            const char *comma = strrchr(line, ',');
            const char *jt = comma ? comma + 1 : line + 4;
            while (*jt == ' ') jt++;
            if (jt[0] == 'L' && isdigit((unsigned char)jt[1])) {
                char jname[16];
                int ji = 0;
                while (jt[ji] && jt[ji] != ' ' && jt[ji] != '\t' &&
                       ji < (int)sizeof(jname) - 1) {
                    jname[ji] = jt[ji];
                    ji++;
                }
                jname[ji] = 0;
                if (bc_label_name_index(fwd_untrusted, n_fwd_untrusted, jname) < 0) {
                    if (n_fwd_untrusted >= MAX_BC_LOOP_LABELS) {
                        safe = 0;
                    } else {
                        dcc_copy_str(fwd_untrusted[n_fwd_untrusted++],
                                     sizeof(fwd_untrusted[0]), jname);
                    }
                }
            }
        }

        is_recognized_e_line =
            strcmp(line, "\tld e,l") == 0 || strcmp(line, "\tld l,e") == 0 ||
            strcmp(line, "\tld d,0") == 0 ||
            strcmp(line, "\tinc e") == 0 || strcmp(line, "\tdec e") == 0 ||
            strcmp(line, "\tld a,e") == 0 || strcmp(line, "\tcp e") == 0;

        /* Z80 semantics guarantee these three never write d/e/de - only HL
         * (and flags) - so they can never clobber a live counter regardless
         * of context, and (unlike is_recognized_e_line) must NOT set e_live:
         * add hl,de is the universal last step of every base-plus-index
         * address computation (gen_index_addr_ast) for ANY index, including
         * ones with nothing to do with this counter - counting it as "the
         * counter's own line" let it flip e_live on early, from a completely
         * unrelated b[1]/b[2]/b[3] address computation that runs before the
         * counter is even initialized, which then wrongly flagged the very
         * next unrelated "ld de,2" as a violation. */
        is_universally_safe_de_line =
            strcmp(line, "\tadd hl,de") == 0 || strcmp(line, "\tadc hl,de") == 0 ||
            strcmp(line, "\tsbc hl,de") == 0;

        /* gen_index_addr_ast's generic non-constant-index path (dcc_ast_gen_
         * expr.c) always follows a bare-identifier index load with "ex
         * de,hl" to move the index from HL into DE for the base-plus-index
         * add - including when that index is our own reg_alloc'd counter,
         * whose value was JUST loaded by "ld l,e"/"ld h,0" the two lines
         * before. Swapping HL and DE at that exact point only relocates the
         * value this feature's own emit_load_sym_value_direct put in L (now
         * H:L = 0:e) into D:E - numerically the same value, so e's live
         * content survives, and the line is exactly identifiable as this
         * specific idiom, not a coincidence to gloss over: it is only
         * excluded when immediately preceded by exactly "ld l,e"/"ld h,0" in
         * that order, nothing else. Found the same way as the "ld de,1"
         * false positive above, one line later in the same fill_record
         * dump. */
        is_recognized_e_index_swap =
            strcmp(line, "\tex de,hl") == 0 &&
            strcmp(prev1, "\tld h,0") == 0 && strcmp(prev2, "\tld l,e") == 0;

        /* Before the counter's own first recognized line, it holds no live
         * value yet (try_narrow_for_counter's proof requires it be declared
         * with no initializer - its real first write is exactly one of
         * these lines), so any earlier, unrelated d/e scratch use (e.g. a
         * literal array-offset constant like "ld de,1" for some other
         * variable's b[1]) is harmless and must not be flagged - found via
         * tbig.c's fill_record, whose four explicit b[0..3] stores (using
         * de purely as address-offset scratch) all run before `i`'s own
         * init. Once e_live is set it stays set for the rest of the
         * function - a later unrelated de use IS a real hazard (the
         * counter may still be read again, e.g. after the loop it belongs
         * to, since this scan has no notion of the loop's own extent) -
         * conservative by construction, not by accident. */
        if (safe && e_cand != NULL && e_live &&
            !is_recognized_e_line && !is_recognized_e_index_swap &&
            !is_universally_safe_de_line &&
            line_touches_de_reg(line))
            safe = 0;
        if (is_recognized_e_line)
            e_live = 1;

        memcpy(prev2, prev1, sizeof(prev2));
        {
            size_t ll = strlen(line);
            if (ll > sizeof(prev1) - 1) ll = sizeof(prev1) - 1;
            memcpy(prev1, line, ll);
            prev1[ll] = 0;
        }

        line = nl ? nl + 1 : buf + size;
    }

    free(buf);
    if (!safe) {
        fclose(rewritten);
        return 0;
    }
    rewind(rewritten);
    *out_f = rewritten;
    return 1;
}

/* Tried before find_bc_regalloc_candidate's own whole-function BC candidate
 * gets a chance: dcc_loop_regalloc.c's loop-scoped mechanism finds and
 * ranks a per-loop candidate far more precisely than find_bc_regalloc_
 * candidate's crude "first read-only parameter referenced twice, in
 * declaration order" token-scan does - and, whenever it succeeds, a loop-
 * scoped candidate is essentially always more valuable, since it's
 * referenced inside a loop that (unless proven otherwise) runs more than
 * once, versus a flat whole-function reference count. But the two
 * mechanisms don't know each other's value in advance, and whole-function's
 * own commit happens by wrapping the ENTIRE body in one speculative
 * generate-verify-commit BEFORE any loop inside it is even reached - so
 * whichever claims BC first has always won unconditionally, regardless of
 * actual value (see tests/forint.c's eval_e for a real example: its own
 * best loop-scoped candidate, referenced 30 times inside its hot loop, was
 * losing outright to a parameter referenced 3 times, all outside any
 * loop).
 *
 * Rather than predict which side would win (a lexical heuristic risks
 * declining find_bc_regalloc_candidate's candidate for nothing if the
 * loop-scoped one then fails for a reason the heuristic can't see - e.g. a
 * text-level verifier decline), this generates the body once with NOTHING
 * pre-claimed - exactly like the plain-buffered/final-fallback branches
 * below, where dcc_loop_regalloc.c already gets a fully fair, unimpeded
 * shot today - and checks g_loop_regalloc_bc_claimed (set by dcc_loop_
 * regalloc.c's try_loop_regalloc_bc/try_loop_regalloc_bc_write right where
 * each commits) afterward to learn, empirically, whether any loop actually
 * claimed BC. If one did, this body is kept outright and the whole-function
 * mechanism never runs at all for this function. If not, this attempt is
 * discarded and rewound exactly like a failed speculative attempt anywhere
 * else in this file, and the existing chain proceeds unchanged - no loss
 * relative to today's behavior.
 *
 * Modeled directly on try_speculative_bc_regalloc_function_body's own
 * commit-or-full-rewind shape, just with nothing pre-claimed going in and a
 * different (empirical, not text-scanned) success condition. */
int try_loop_scoped_regalloc_first(const char *name, int type,
                                           int local_bytes, struct Sym *s,
                                           long body_start_pos,
                                           long body_start_tok_start,
                                           int body_start_line,
                                           int body_start_tok_line,
                                           struct Token body_start_tok,
                                           int body_start_nlocals,
                                           int body_start_local_size)
{
    FILE *scratch;
    EmitSink saved_sink;
    int saved_stack_check;
    int c;
    int errors_before;

    if (strcmp(name, "main") == 0)
        return 0;

    scratch = tmpfile();
    if (scratch == NULL)
        fatal("cannot create speculative loop-scoped-first temp file");

    saved_stack_check = opt_stack_check;
    saved_sink = emit_sink_push(scratch, EMIT_SINK_VERIFY);
    opt_stack_check = s->stack_check_enabled;
    g_inline_body_buffering++;
    g_buffering_epoch++;
    reset_function_codegen_state(s);
    g_loop_regalloc_bc_claimed = 0;

    errors_before = g_diag_error_count;
    asm_suppress_depth++;
    emit_function_prologue(name, local_bytes, current_function_safe_to_omit_ix(type, local_bytes));
    gen_compound();
    emit_function_epilogue(strcmp(name, "main") == 0 &&
                            (type & 15) == TYPE_INT && type_ptr_depth(type) == 0);
    asm_suppress_depth--;
    g_inline_body_buffering--;
    opt_stack_check = saved_stack_check;
    emit_sink_restore(&saved_sink);

    if (g_diag_error_count == errors_before && g_loop_regalloc_bc_claimed) {
        check_undefined_user_labels();
        if (plain_static_body_can_be_buffered(s, name)) {
            s->deferred_body_file = scratch;
        } else {
            rewind(scratch);
            while ((c = fgetc(scratch)) != EOF)
                fputc(c, g_emit_sink.stream);
            fclose(scratch);
        }
        return 1;
    }

    fclose(scratch);

    /* Undo every bit of per-function codegen state this discarded attempt
     * touched, exactly like try_speculative_bc_regalloc_function_body's own
     * rewind. */
    speculative_body_discard_rewind(s, body_start_pos, body_start_tok_start,
                                    body_start_line, body_start_tok_line,
                                    body_start_tok, body_start_nlocals,
                                    body_start_local_size);
    return 0;
}

/* Speculatively generate `name`'s already-scanned body with `bc_cand` (a
 * read-only pointer parameter, chosen ahead of time by find_bc_regalloc_
 * candidate - may be NULL) BC-resident, and/or a loop-counter local claimed
 * during the walk itself into E (via g_e_regalloc_claim_active, set here;
 * see gen_local_decl_after_type in dcc_decl.c), instead of occupying a frame
 * slot - and verifies/finalizes both via regalloc_buffer_finalize. Modeled directly on
 * try_speculative_noix_function_body: same tmpfile redirection, same g_
 * inline_body_buffering guard (required for the same EXTRN-dedup-cache-
 * desync reason), same commit-or-full-rewind discipline on failure. Unlike
 * the no-IX-frame optimization this stacks with a normal IX frame - only
 * the claimed candidates' own storage is affected, every other local/param
 * is addressed exactly as before. */
int try_speculative_bc_regalloc_function_body(const char *name, int type,
                                                       int local_bytes, struct Sym *s,
                                                       struct Sym *bc_cand,
                                                       int attempt_e,
                                                       long body_start_pos,
                                                       long body_start_tok_start,
                                                       int body_start_line,
                                                       int body_start_tok_line,
                                                       struct Token body_start_tok,
                                                       int body_start_nlocals,
                                                       int body_start_local_size)
{
    FILE *scratch;
    EmitSink saved_sink;
    int saved_stack_check;
    int c;
    int errors_before;

    scratch = tmpfile();
    if (scratch == NULL)
        fatal("cannot create speculative bc-regalloc temp file");

    saved_stack_check = opt_stack_check;
    saved_sink = emit_sink_push(scratch, EMIT_SINK_VERIFY);
    opt_stack_check = s->stack_check_enabled;
    g_inline_body_buffering++;
    g_buffering_epoch++;
    reset_function_codegen_state(s);
    if (bc_cand != NULL) {
        bc_cand->reg_alloc = REG_BC;
        g_bc_regalloc_sym = bc_cand;
    }
    g_e_regalloc_claim_active = attempt_e ? 1 : 0;
    g_e_regalloc_claimed = 0;
    g_e_regalloc_sym = NULL;
    g_regalloc_address_escaped = 0;
    /* Same suppress-but-count discipline as try_speculative_noix_function_
     * body, and for the identical reason: a genuine source error must never
     * be silently swallowed just because this specific attempt happens to
     * pass the regalloc-only safety checks below. */
    errors_before = g_diag_error_count;
    asm_suppress_depth++;
    emit_function_prologue(name, local_bytes, current_function_safe_to_omit_ix(type, local_bytes));
    gen_compound();
    emit_function_epilogue(strcmp(name, "main") == 0 &&
                            (type & 15) == TYPE_INT && type_ptr_depth(type) == 0);
    asm_suppress_depth--;
    g_bc_regalloc_sym = NULL;
    /* Reset unconditionally here, right after codegen finishes, regardless
     * of whether this attempt goes on to succeed or get discarded below -
     * matching try_loop_regalloc_bc/_write's (dcc_loop_regalloc.c) own,
     * already-correct pattern. bc_cand's own reg_alloc field is only ever
     * consulted DURING gen_compound() above; leaving it set to REG_BC past
     * this point serves no purpose and is actively dangerous for a global
     * candidate specifically - unlike a local/param's Sym, which is
     * effectively discarded once this function's compilation moves on, a
     * global's Sym is the SAME, persistent object referenced by every other
     * function in the file. A stale REG_BC left here after a successful
     * commit (the old code only reset it on the discard path, taking the
     * `return 1` below without ever reaching it) meant every later,
     * unrelated function referencing that same global got silently
     * compiled as if it had its own live BC prime, reading whatever
     * leftover garbage happened to be in BC instead of the global's real
     * value - a real, confirmed miscompile (tests/pint.c interpreting
     * TTT.PAS: curproc read as garbage, then appearing to change value
     * across an unrelated call, purely from stale reg_alloc state left on
     * its Sym by an earlier, different function's successful whole-
     * function promotion of it). */
    if (bc_cand != NULL)
        bc_cand->reg_alloc = REG_NONE;
    g_e_regalloc_claim_active = 0;
    g_inline_body_buffering--;
    opt_stack_check = saved_stack_check;
    emit_sink_restore(&saved_sink);

    /* Same rationale as try_speculative_noix_function_body's identical
     * comment: skip check_undefined_user_labels() here on a path that might
     * be discarded, to avoid double-reporting and stale nulabels state. */
    if (g_diag_error_count == errors_before &&
        !g_regalloc_address_escaped &&
        (bc_cand != NULL || g_e_regalloc_claimed)) {
        FILE *finalized = NULL;
        if (regalloc_buffer_finalize(scratch, bc_cand, g_e_regalloc_claimed ? g_e_regalloc_sym : NULL,
                                      &finalized)) {
            check_undefined_user_labels();
            fclose(scratch);
            while ((c = fgetc(finalized)) != EOF)
                fputc(c, g_emit_sink.stream);
            fclose(finalized);
            return 1;
        }
    }

    fclose(scratch);
    if (g_e_regalloc_claimed && g_e_regalloc_sym != NULL)
        g_e_regalloc_sym->reg_alloc = REG_NONE;
    g_e_regalloc_claimed = 0;
    g_e_regalloc_sym = NULL;

    /* Undo every bit of per-function codegen state this discarded attempt
     * touched, exactly like try_speculative_noix_function_body's own rewind. */
    speculative_body_discard_rewind(s, body_start_pos, body_start_tok_start,
                                    body_start_line, body_start_tok_line,
                                    body_start_tok, body_start_nlocals,
                                    body_start_local_size);
    return 0;
}

/* True if the generated text in `buf` references IY anywhere. dcc's own
 * codegen never emits an IY instruction except the prologue/epilogue
 * callee-save pair and the priming sequence this attempt itself inserts, so
 * anything else found here came from user inline assembly - the one thing
 * that can invalidate the "nothing else in the image writes IY" invariant
 * REG_IY rests on. Verification for IY is this short precisely because the
 * invariant does the work that regalloc_buffer_finalize's whole trust-
 * tracking machinery has to do for BC: BC is contended by the code generator
 * itself, IY is not contended by anything. */
static int buf_has_foreign_iy_use(const char *buf, int n_own_lines,
                                  const char own[6][40])
{
    const char *p;
    char linebuf[256];

    p = buf;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t ll = nl ? (size_t)(nl - p) : strlen(p);
        const char *q;
        int k, own_line;

        if (ll >= sizeof(linebuf)) ll = sizeof(linebuf) - 1;
        memcpy(linebuf, p, ll);
        linebuf[ll] = 0;

        q = linebuf;
        while (*q == ' ' || *q == '\t') q++;
        if (*q != ';' && strstr(linebuf, "iy") != NULL) {
            own_line = 0;
            for (k = 0; k < n_own_lines; k++) {
                if (strcmp(linebuf, own[k]) == 0) {
                    own_line = 1;
                    break;
                }
            }
            if (!own_line)
                return 1;
        }
        if (!nl) break;
        p = nl + 1;
    }
    return 0;
}

/* Speculative whole-function IY promotion.
 *
 * Structurally the same speculate/verify/commit-or-rewind shape as the BC
 * attempt above, but with two substantive differences, both stemming from IY
 * being callee-saved rather than contended:
 *
 *  - There is no call whitelist and no equivalent of buf_has_unsafe_call.
 *    Every call preserves IY, so a function full of calls - precisely the
 *    population function_qualifies_for_speculative_regalloc rejects outright,
 *    and which therefore gets no register allocation at all today - is
 *    exactly the population this serves.
 *
 *  - Verification reduces to "did anything other than our own emitted lines
 *    mention IY", i.e. user inline assembly, plus the shared address-escape
 *    check every promotion needs.
 *
 * The candidate is primed once, at function entry, after the frame is
 * established: entry dominates every use, so no reload repair or loop-header
 * analysis is needed either. */
int try_speculative_iy_regalloc_function_body(const char *name, int type,
                                              int local_bytes, struct Sym *s,
                                              struct Sym *iy_cand,
                                              long body_start_pos,
                                              long body_start_tok_start,
                                              int body_start_line,
                                              int body_start_tok_line,
                                              struct Token body_start_tok,
                                              int body_start_nlocals,
                                              int body_start_local_size)
{
    FILE *scratch;
    EmitSink saved_sink;
    int saved_stack_check;
    int c;
    int errors_before;
    char own[6][40];
    int n_own;
    char *buf;
    long size;
    int ok;

    scratch = tmpfile();
    if (scratch == NULL)
        fatal("cannot create speculative iy-regalloc temp file");

    saved_stack_check = opt_stack_check;
    saved_sink = emit_sink_push(scratch, EMIT_SINK_VERIFY);
    opt_stack_check = s->stack_check_enabled;
    g_inline_body_buffering++;
    g_buffering_epoch++;
    reset_function_codegen_state(s);

    /* Set BEFORE the prologue is emitted: emit_function_prologue keys the
     * "push iy" off this, and recompute_param_offsets keys the +2 parameter
     * shift off it too, so both must see it. */
    g_iy_regalloc_sym = iy_cand;
    recompute_param_offsets();
    iy_cand->reg_alloc = REG_IY;
    g_regalloc_address_escaped = 0;

    errors_before = g_diag_error_count;
    asm_suppress_depth++;
    emit_function_prologue(name, local_bytes, 0);
    /* Prime IY from the parameter's frame slot. There is no "ld iy,(ix+d)"
     * on Z80, so the value goes through HL. 63 T-states, paid once. */
    emit_regalloc_claim("iy", "func", iy_cand, "ro",
                        regalloc_estimate_value(iy_cand, g_iy_regalloc_last_ref_count, 0));
    fprintf(g_emit_sink.stream, "\tld l,(ix%+d)\n", iy_cand->offset);
    fprintf(g_emit_sink.stream, "\tld h,(ix%+d)\n", iy_cand->offset + 1);
    emit("\tpush hl\n");
    emit("\tpop iy\n");
    gen_compound();
    emit_function_epilogue(strcmp(name, "main") == 0 &&
                           (type & 15) == TYPE_INT && type_ptr_depth(type) == 0);
    asm_suppress_depth--;

    n_own = 0;
    strcpy(own[n_own++], "\tpush iy");
    strcpy(own[n_own++], "\tpop iy");
    strcpy(own[n_own++], "\tpush hl");
    strcpy(own[n_own++], "\tpop hl");

    iy_cand->reg_alloc = REG_NONE;
    g_inline_body_buffering--;
    opt_stack_check = saved_stack_check;
    emit_sink_restore(&saved_sink);

    ok = 0;
    if (g_diag_error_count == errors_before && !g_regalloc_address_escaped) {
        buf = dcc_read_stream_text(scratch, &size,
                                   "cannot read speculative iy-regalloc temp file");
        ok = !buf_has_foreign_iy_use(buf, n_own, own);
        free(buf);
    }
    if (getenv("DCC_TRACE_IY") != NULL)
        fprintf(stderr, "[iy] %s: errors=%d escaped=%d accepted=%d\n", name,
                g_diag_error_count - errors_before, g_regalloc_address_escaped, ok);

    if (ok) {
        check_undefined_user_labels();
        rewind(scratch);
        while ((c = fgetc(scratch)) != EOF)
            fputc(c, g_emit_sink.stream);
        fclose(scratch);
        /* Left set through the caller's own bookkeeping is wrong for the same
         * reason the BC path documents at length: the next function must not
         * inherit this one's frame shape. */
        g_iy_regalloc_sym = NULL;
        recompute_param_offsets();
        return 1;
    }

    fclose(scratch);
    g_iy_regalloc_sym = NULL;
    recompute_param_offsets();
    speculative_body_discard_rewind(s, body_start_pos, body_start_tok_start,
                                    body_start_line, body_start_tok_line,
                                    body_start_tok, body_start_nlocals,
                                    body_start_local_size);
    return 0;
}

/* Wrapper around try_speculative_bc_regalloc_function_body that tries BC+E
 * together first, then falls back to BC-only if that combined attempt is
 * declined and a BC candidate exists - so a genuinely unsafe E-counter
 * candidate (e.g. tbig.c's fill_record: `i` used both as an array index
 * and, separately, as a long-arithmetic operand, with an intervening
 * long-load that clobbers e in between the two uses) never regresses the
 * already-safe BC win back to nothing. Each attempt is a fully independent
 * speculative generation with its own commit-or-rewind, so the retry is
 * exactly as safe as either attempt alone - the second one starts from the
 * identical rewound state the first one's failure already restored. */
int try_speculative_bc_regalloc_with_e_fallback(const char *name, int type,
                                                        int local_bytes, struct Sym *s,
                                                        struct Sym *bc_cand,
                                                        long body_start_pos,
                                                        long body_start_tok_start,
                                                        int body_start_line,
                                                        int body_start_tok_line,
                                                        struct Token body_start_tok,
                                                        int body_start_nlocals,
                                                        int body_start_local_size)
{
    if (try_speculative_bc_regalloc_function_body(name, type, local_bytes, s, bc_cand, 1,
                                                   body_start_pos, body_start_tok_start,
                                                   body_start_line, body_start_tok_line,
                                                   body_start_tok, body_start_nlocals,
                                                   body_start_local_size))
        return 1;
    if (bc_cand == NULL)
        return 0;
    return try_speculative_bc_regalloc_function_body(name, type, local_bytes, s, bc_cand, 0,
                                                      body_start_pos, body_start_tok_start,
                                                      body_start_line, body_start_tok_line,
                                                      body_start_tok, body_start_nlocals,
                                                      body_start_local_size);
}
