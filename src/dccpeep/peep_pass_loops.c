/**
 * @file peep_pass_loops.c
 * @brief Promotes proven loop variables and bounds into registers.
 *
 * @par Role
 * Matches exact byte and word loop shapes, proves slot, escape, call, and
 * register-ownership safety, then promotes counters or bounds into BC, C, E,
 * or IYL for the loop's live interval.
 *
 * @par Key entry points
 * The pass_*_to_reg_*() loop passes declared in dccpeep_internal.h.
 *
 * @par Boundary
 * This is pattern-specific loop registerization, not general frame allocation.
 * dccpeep.c owns pass order and the opt-in gate for undocumented IYL forms.
 */
#include "dccpeep_internal.h"

static int loop_body_escapes_safe_for_offset(int lo, int hi,
                                             const char *pat_ix);

enum CounterZeroExtendUse {
    COUNTER_USE_NONE,
    COUNTER_USE_DE,
    COUNTER_USE_HL
};

static int find_straight_line_loop_back(int body_start, const char *label)
{
    int k;
    char target[128];

    for (k = body_start; k < nlines; ++k) {
        if (starts_label(lines[k]))
            break;
        if (is_uncond_jp(lines[k])) {
            if (jump_target(lines[k], target) && strcmp(target, label) == 0)
                return k;
            break;
        }
    }
    return -1;
}

static enum CounterZeroExtendUse match_counter_zero_extend(
    int line, const char *load_de, const char *load_hl)
{
    if (eq(line, load_de) && eq(line + 1, "ld d,0"))
        return COUNTER_USE_DE;
    if (eq(line, load_hl) && eq(line + 1, "ld h,0"))
        return COUNTER_USE_HL;
    return COUNTER_USE_NONE;
}

static int line_uses_iy_half_register(int line)
{
    return strncmp(lines[line], "db 0FDh,", 8) == 0;
}

/* Recognizes a byte-sized ix-local used purely as a self-guarding
 * decrementing loop counter - dcc_array_narrow.c's `while(--n)` idiom,
 * once narrowing has made the counter's own storage a single byte (see
 * try_narrow_register_scalar in dcc_func.c) - and promotes it to register
 * C for the loop's duration, eliminating the ix-frame reload on every use.
 *
 * Matches:
 *   LABEL:
 *   dec (ix+O)
 *   jp z, EXIT
 *   <body, ending in a bare "jp LABEL">
 * where every reference to (ix+O) inside the body is one of exactly two
 * whitelisted "zero-extend into a 16-bit register pair" shapes -
 *   ld e,(ix+O)        ld l,(ix+O)
 *   ld d,0              ld h,0
 * - and every call inside the body is to __mods or __divs specifically:
 * runtime helpers documented (see DCCRTL.MAC) to preserve BC across the
 * call, so C can stand in for the whole loop with no spill/reload at all.
 *
 * Declines (the safe default, missing the optimization but never
 * misapplying it) if any other reference to the counter's slot, any other
 * call, or any other label appears in the body - this pass does not try
 * to reason about what such a reference might mean. */
int pass_byte_loop_counter_to_reg_c(void)
{
    int i;
    int changed;
    int off;
    char label[128];
    char target[128];
    int loop_end;
    int k;
    int ok;
    char pat_ix[40];
    char pat_lde[40];
    char pat_lhl[40];
    char prime[40];
    char writeback[40];

    changed = 0;

    for (i = 0; i + 2 < nlines; ++i) {
        if (!starts_label(lines[i]))
            continue;
        if (!peep_parse_dec_ix_byte(lines[i + 1], &off))
            continue;
        if (!parse_jp_cond_label(lines[i + 2], "z", target))
            continue;

        strcpy(label, lines[i]);
        strip_label_colon(label);

        /* Find the matching loop-back jump to this same label, with no
         * other label in between (single-entry, single-exit body). */
        loop_end = find_straight_line_loop_back(i + 3, label);
        if (loop_end < 0)
            continue;

        sprintf(pat_ix, "(ix%+d)", off);
        sprintf(pat_lde, "ld e,(ix%+d)", off);
        sprintf(pat_lhl, "ld l,(ix%+d)", off);

        ok = 1;
        for (k = i + 3; k < loop_end && ok; ++k) {
            if (strncmp(lines[k], "call ", 5) == 0) {
                if (!eq(k, "call __mods") && !eq(k, "call __divs"))
                    ok = 0;
                continue;
            }
            if (strstr(lines[k], pat_ix) == NULL)
                continue;
            if (match_counter_zero_extend(k, pat_lde, pat_lhl) != COUNTER_USE_NONE) {
                ++k;
                continue;
            }
            ok = 0;
        }
        if (!ok)
            continue;

        /* This loop's own body never mentions B/C outside the whitelisted
         * shapes above, but that alone doesn't prove BC is actually free
         * here - dcc's own reg_alloc may already hold a whole-function or
         * earlier-loop candidate resident in BC across this exact point,
         * invisible to a scan confined to [i+3, loop_end) alone. The
         * counter this pass puts in C is live only for the loop, so the
         * span to ask about is exactly [i, loop_end) - a claim dcc has
         * already released before the loop no longer blocks it. */
        if (bc_regalloc_claimed_in_range(i, loop_end))
            continue;

        /* In-place replacements first, while every index computed above is
         * still valid (no lines inserted/deleted yet). */
        replace1_tagged(i + 1, "dec c", "byte_loop_counter_to_reg_c");
        for (k = i + 3; k < loop_end; ++k) {
            if (eq(k, pat_lde)) { replace1(k, "ld e,c"); continue; }
            if (eq(k, pat_lhl)) { replace1(k, "ld l,c"); continue; }
        }

        /* Write the counter back to its frame slot right after the
         * decrement (LD does not touch flags, so the Z flag "dec c" just
         * set is still valid two lines later at the exit branch) - makes
         * the transform safe regardless of whether anything after the
         * loop still reads the slot, without needing to prove it doesn't. */
        sprintf(writeback, "ld (ix%+d),c", off);
        insert_line(i + 2, writeback);

        /* Prime the register right before the loop label. */
        sprintf(prime, "ld c,(ix%+d)", off);
        insert_line_tagged(i, prime, "byte_loop_counter_to_reg_c");

        changed = 1;
    }

    return changed;
}

/*
 * pass_word_loop_var_to_reg_bc:
 *
 * A 2-byte local variable (a negative ix offset, so a local rather than a
 * parameter) that is:
 *   - initialized once, immediately before a loop's own top label (a
 *     "ld (ix+O),l" / "ld (ix+O+1),h" pair right before the label),
 *   - that label is a real loop (something later jumps back to it - and,
 *     since a loop can have more than one back-edge, e.g. an early
 *     "continue"-style path, every reference up through the LAST one
 *     found counts as still being inside the loop, not just the first),
 *   - never has its address taken - checked two ways: no reference to
 *     its offset anywhere in the function outside one of eight
 *     whitelisted shapes (loaded into hl, de, or a's low/high byte, or
 *     stored back from hl as a pair - never a cp, never anything else),
 *     and no bare "ld de,<off>" / "ld hl,<off>" anywhere either, which is
 *     how dcc compiles "&local" (push ix / pop hl / ld de,<off> /
 *     add hl,de) - the offset there is a plain immediate, not "(ix<off>)"
 *     text, so the first check alone can't see it,
 *   - and B/C are completely unused anywhere else in the function (no
 *     calls at all - every user-function call in this ABI clobbers every
 *     register, so a live register cache can never safely cross one - no
 *     block-repeat instructions, no explicit b/c/bc token)
 * is kept in BC for the function's entire body instead of round-tripping
 * through its frame slot on every use. This is the same
 * "enumerate exactly what's recognized, decline on anything else" design
 * as pass_byte_loop_counter_to_reg_c, generalized from an 8-bit
 * decrementing counter to a 16-bit variable with an arbitrary update.
 *
 * An earlier version scoped the BC-freedom requirement to just the loop
 * (not the whole function), reasoning that code before the init-write
 * can't matter and code after the loop can't reference the variable
 * again anyway. That's true, but cost two real bugs to get right (an
 * incomplete loop-extent bound from only finding the first of several
 * back-edges let BC get silently clobbered mid-loop in tests/forint.c;
 * and it never even caught the &local case above, corrupting
 * tests/tforsco.c) while never actually paying for itself - its
 * motivating case, is_attacked's call-free knight-check loop ahead of
 * its calls to attacked_by_slider, turned out to be blocked anyway by an
 * unrelated pre-existing pass already claiming BC for that loop's own
 * array index. Reverted to the simpler, whole-function requirement.
 *
 * Does not attempt to reclaim the now-unused frame slot; leaving it
 * allocated but unreferenced wastes at most 2 bytes of stack per call,
 * and avoids needing to renumber every other frame offset in the
 * function.
 */
int pass_word_loop_var_to_reg_bc(void)
{
    int i, j, changed = 0;
    int off;
    int func_start, func_end;
    int backedge_line;
    char label[128];
    char pat_l[32], pat_h[32], pat_e[32], pat_d[32], pat_stl[32], pat_sth[32];
    char pat_al[32], pat_ah[32];
    int bad;
    int bc_used_elsewhere;

    for (i = 0; i + 2 < nlines; i++) {
        char t1[MAX_LINE], t2[MAX_LINE];

        strip_peep_comment_copy(t1, lines[i]);
        if (strncmp(t1, "ld (ix", 6) != 0)
            continue;
        if (sscanf(t1 + 6, "%d),l", &off) != 1)
            continue;
        if (off >= 0)
            continue;

        sprintf(pat_sth, "ld (ix%d),h", off + 1);
        strip_peep_comment_copy(t2, lines[i + 1]);
        if (strcmp(t2, pat_sth) != 0)
            continue;

        if (!starts_label(lines[i + 2]))
            continue;
        if (!label_name_at(i + 2, label))
            continue;

        /* A loop can have more than one jump back to its own top label -
         * an early "continue"-style path, for instance - and every one of
         * them is still part of the loop body. Take the LAST (highest
         * line number) back-edge, not the first: anything in between is
         * still inside the loop and must be covered by the bad-reference
         * and BC-freedom scans below. Using only the first one found
         * silently under-covers the loop whenever a later back-edge
         * exists, missing BC-clobbering code that's still part of it -
         * confirmed via a real corruption in tests/forint.c, where a
         * later back-edge sat well past the first one this used to stop
         * at. */
        backedge_line = -1;
        for (j = i + 3; j < nlines; j++) {
            char tgt[128];
            if (strncmp(lines[j], "public ", 7) == 0 ||
                strncmp(lines[j], "; static function ", 18) == 0)
                break;
            if (jump_target_any(lines[j], tgt) && !strcmp(tgt, label))
                backedge_line = j;
        }
        if (backedge_line < 0)
            continue;

        find_function_bounds_any(i, &func_start, &func_end);

        sprintf(pat_l,   "ld l,(ix%d)", off);
        sprintf(pat_h,   "ld h,(ix%d)", off + 1);
        sprintf(pat_e,   "ld e,(ix%d)", off);
        sprintf(pat_d,   "ld d,(ix%d)", off + 1);
        sprintf(pat_stl, "ld (ix%d),l", off);
        sprintf(pat_sth, "ld (ix%d),h", off + 1);
        sprintf(pat_al,  "ld a,(ix%d)", off);
        sprintf(pat_ah,  "ld a,(ix%d)", off + 1);

        /* A local's address starts with "push ix / pop hl", followed by
         * either "ld de,<off> / add hl,de" or repeated inc/dec hl for a
         * small offset. The offset appears as a BARE immediate or is only
         * implicit in the inc/dec count, not as "(ix<off>)" text, so the
         * substring scan below can never see it. Matching the complete
         * address sequence (not just a bare "ld de,<off>" anywhere, which an
         * earlier version of this check did) matters: an unrelated
         * variable's own address computation can legitimately use this
         * candidate's own offset as its bare immediate too, purely by
         * numeric coincidence (two different locals' offsets are never
         * equal, but variable X's *base address* arithmetic can still
         * involve variable Y's offset number, e.g. X = &array_at_dash_5
         * while Y separately happens to live at offset -6/-5) - confirmed
         * via a real false-positive in tests/tc99scpe.c that the loose
         * bare-number version of this check declined unnecessarily.
         * Still conservative (requires an exact, contiguous recognized
         * shape; a compiler change that computed this address some other
         * way would need a corresponding update here), but precise enough
         * not to trip over an unrelated variable's own address math. */
        {
            char pat_addr_de_l[32], pat_addr_de_h[32];
            int addr_taken;

            sprintf(pat_addr_de_l, "ld de,%d", off);
            sprintf(pat_addr_de_h, "ld de,%d", off + 1);

            addr_taken = 0;
            for (j = func_start; j + 3 < func_end; j++) {
                char t0[MAX_LINE], t1[MAX_LINE], t2[MAX_LINE], t3[MAX_LINE];
                int addr_off, k;

                strip_peep_comment_lower_copy(t0, lines[j]);
                if (strcmp(t0, "push ix") != 0)
                    continue;
                strip_peep_comment_lower_copy(t1, lines[j + 1]);
                if (strcmp(t1, "pop hl") != 0)
                    continue;
                addr_off = 0;
                for (k = j + 2; k < func_end; ++k) {
                    strip_peep_comment_lower_copy(t2, lines[k]);
                    if (strcmp(t2, "inc hl") == 0)
                        ++addr_off;
                    else if (strcmp(t2, "dec hl") == 0)
                        --addr_off;
                    else
                        break;
                    if (addr_off == off || addr_off == off + 1) {
                        addr_taken = 1;
                        break;
                    }
                }
                if (addr_taken)
                    break;
                strip_peep_comment_lower_copy(t3, lines[j + 3]);
                if (strcmp(t3, "add hl,de") != 0)
                    continue;
                strip_peep_comment_lower_copy(t2, lines[j + 2]);
                if (strcmp(t2, pat_addr_de_l) == 0 || strcmp(t2, pat_addr_de_h) == 0) {
                    addr_taken = 1;
                    break;
                }
            }
            if (addr_taken)
                continue;
        }

        bad = 0;
        for (j = func_start; j < func_end; j++) {
            char t[MAX_LINE];
            char off_l[32], off_h[32];

            strip_peep_comment_lower_copy(t, lines[j]);
            sprintf(off_l, "(ix%d)", off);
            sprintf(off_h, "(ix%d)", off + 1);
            if (strstr(t, off_l) == NULL && strstr(t, off_h) == NULL)
                continue;
            if (strcmp(t, pat_l) == 0 || strcmp(t, pat_h) == 0 ||
                strcmp(t, pat_e) == 0 || strcmp(t, pat_d) == 0 ||
                strcmp(t, pat_stl) == 0 || strcmp(t, pat_sth) == 0 ||
                strcmp(t, pat_al) == 0 || strcmp(t, pat_ah) == 0)
                continue;
            bad = 1;
            break;
        }
        if (bad)
            continue;

        /* BC must be free across the whole function, not just the loop:
         * an earlier version scoped this to just [loop label, back-edge]
         * (reasoning that nothing outside that range could still
         * reference the variable, since the "bad" scan above already
         * covers the whole function). That's true, but doesn't fully
         * pay for itself - it never actually unlocked its motivating
         * case (a call-free loop followed by calls later in the same
         * function, e.g. is_attacked's knight-check loop ahead of its
         * calls to attacked_by_slider - blocked instead by an unrelated
         * register conflict, a pre-existing pass already claiming BC for
         * that specific loop's own array index) while costing a real,
         * measurable regression in several other apps that used to have
         * this variable safely read again after the loop under the
         * whole-function requirement. Reverted to the simpler, strictly
         * safe whole-function scope. */
        bc_used_elsewhere = peep_reg_used_in_function(i, NULL, line_clobbers_bc);
        if (bc_used_elsewhere)
            continue;

        for (j = func_start; j < func_end; j++) {
            char t[MAX_LINE];

            strip_peep_comment_lower_copy(t, lines[j]);
            if (strcmp(t, pat_l) == 0) {
                replace1_tagged(j, "ld l,c", "word_loop_var_bc");
                changed = 1;
            } else if (strcmp(t, pat_h) == 0) {
                replace1_tagged(j, "ld h,b", "word_loop_var_bc");
                changed = 1;
            } else if (strcmp(t, pat_e) == 0) {
                replace1_tagged(j, "ld e,c", "word_loop_var_bc");
                changed = 1;
            } else if (strcmp(t, pat_d) == 0) {
                replace1_tagged(j, "ld d,b", "word_loop_var_bc");
                changed = 1;
            } else if (strcmp(t, pat_stl) == 0) {
                replace1_tagged(j, "ld c,l", "word_loop_var_bc");
                changed = 1;
            } else if (strcmp(t, pat_sth) == 0) {
                replace1_tagged(j, "ld b,h", "word_loop_var_bc");
                changed = 1;
            } else if (strcmp(t, pat_al) == 0) {
                replace1_tagged(j, "ld a,c", "word_loop_var_bc");
                changed = 1;
            } else if (strcmp(t, pat_ah) == 0) {
                replace1_tagged(j, "ld a,b", "word_loop_var_bc");
                changed = 1;
            }
        }
    }

    return changed;
}

/*
 * pass_narrow_bc_loop_bound_to_reg_c:
 *
 * pass_word_loop_var_to_reg_bc (just above) is deliberately general: it
 * relocates an arbitrary 16-bit loop-carried variable from its frame slot
 * into BC, preserving the exact same wide arithmetic throughout - it has
 * no idea whether the variable's actual runtime range ever fits a byte.
 * When it is specifically a `for (i = 0; i < N; i++)` counter with
 * N < 256 - recognizable by the single-sided bias-compare MIR emits for
 * this exact idiom, where the constant side is pre-biased at compile
 * time and only the variable's high byte needs a runtime xor (see
 * pass_signed_cmp_const_bias_fold_mir's own comment for the sibling,
 * ex-de,hl-swapped member of this family) - i is provably byte-range for
 * the whole loop and B is always 0, so the comparison's whole eight-
 * instruction wide-arithmetic sequence collapses to a plain byte cp
 * against C's own low byte directly - no new register claim needed at
 * all, since pass_word_loop_var_to_reg_bc already guarantees C holds the
 * exact, always-current counter value everywhere in the function.
 *
 * An earlier version of this pass tried to shadow the counter into E
 * instead of reading C directly, on the theory that a narrower register
 * might read/compare marginally cheaper - that needed E free for the
 * whole loop body, which tests/tctresc.c's find_mismatch (this pass's
 * motivating case) fails outright: pass_ix_pair_load_to_de already uses
 * d/e for an unrelated address computation earlier in the same loop
 * body. Reading C directly needs no such freedom - it only ever adds a
 * "ld a,c" in front of the existing, already-safe C, so it carries none
 * of that risk and fires unconditionally wherever the shape matches.
 *
 * Same "enumerate exactly what's recognized, decline on anything else"
 * discipline as its siblings: requires the counter to start at the
 * literal constant 0 and to be incremented by exactly the standard
 * "inc hl" re-prime MIR emits for this idiom (proving it can never
 * exceed the compared bound, which this pass already requires to be
 * under 256) - both checked, but neither is rewritten, since nothing
 * about them needs to change once the comparison itself no longer cares
 * that C/B is a 16-bit pair.
 */
int pass_narrow_bc_loop_bound_to_reg_c(void)
{
    int i, k;
    int changed;
    char label[128];
    int label_line;
    int loop_end;
    int imm, bound;
    int increment_found;

    changed = 0;

    for (i = 0; i + 10 < nlines; ++i) {
        if (!eq(i, "ld c,l") || !eq(i + 1, "ld b,h"))
            continue;
        if (!starts_label(lines[i + 2]))
            continue;
        if (!label_name_at(i + 2, label))
            continue;
        label_line = i + 2;

        /* The init value must be exactly the constant 0 - "ld hl,0"
         * shortly before this priming, skipping only intervening,
         * unrelated stores (same bounded backward scan idiom
         * pass_byte_for_counter_to_reg_e uses for its own init line). */
        {
            int found_init = 0;
            int scan_lo = i - 8;
            if (scan_lo < 0)
                scan_lo = 0;
            for (k = i - 1; k >= scan_lo; --k) {
                if (starts_label(lines[k]))
                    break;
                if (eq(k, "ld hl,0")) {
                    found_init = 1;
                    break;
                }
            }
            if (!found_init)
                continue;
        }

        loop_end = find_last_loop_back(label_line + 1, label, 1);
        if (loop_end < label_line + 10)
            continue;

        /* Comparison must be the very next thing after the label - the
         * exact single-sided bias-compare shape MIR emits for a
         * byte-range constant bound. The swapped ex-de,hl variant needs
         * different arithmetic entirely and is not handled here (see
         * pass_signed_cmp_const_bias_fold_mir's own comment). */
        if (!eq(label_line + 1, "ld l,c") || !eq(label_line + 2, "ld h,b"))
            continue;
        if (!peep_parse_ld_de_signed(lines[label_line + 3], &imm))
            continue;
        if (imm < 0x8001 || imm > 0x80ff)
            continue;
        bound = imm - 0x8000;
        if (!eq(label_line + 4, "ld a,h"))
            continue;
        if (!eq(label_line + 5, "xor 128"))
            continue;
        if (!eq(label_line + 6, "ld h,a"))
            continue;
        if (!eq(label_line + 7, "sbc hl,de"))
            continue;
        {
            char t[MAX_LINE];
            strip_peep_comment_copy(t, lines[label_line + 8]);
            if (strncmp(t, "jp nc, ", 7) != 0 && strncmp(t, "jr nc,", 6) != 0)
                continue;
        }

        /* Somewhere in the loop body, the counter must be incremented by
         * exactly +1 via the standard reload-then-inc shape - proof that
         * it can never step past the bound just matched above, so C's
         * low byte alone (B always 0) is a safe stand-in for the whole
         * pair for the rest of this loop's life. Existence only: the
         * increment itself is not rewritten, since it stays correct
         * exactly as pass_word_loop_var_to_reg_bc left it. */
        increment_found = 0;
        for (k = label_line + 9; k + 2 <= loop_end; ++k) {
            if (eq(k, "ld l,c") && eq(k + 1, "ld h,b") && eq(k + 2, "inc hl")) {
                increment_found = 1;
                break;
            }
        }
        if (!increment_found)
            continue;

        /* Replace the whole eight-instruction bias-compare with a
         * three-instruction byte cp against c directly, preserving the
         * original branch (whichever exact jp/jr form it already is) at
         * its now-shifted position. */
        {
            char line[64];

            replace1_tagged(label_line + 1, "ld a,c",
                            "narrow_bc_loop_bound_to_reg_c");
            sprintf(line, "cp %d", bound);
            replace1(label_line + 2, line);
            delete_n(label_line + 3, 5);
        }

        changed = 1;
    }

    return changed;
}

/*
 * pass_byte_loop_var_to_reg_c:
 *
 * A 1-byte local variable (a negative ix offset) initialized once,
 * immediately before a loop's own top label (a single "ld (ix+O),l"
 * right before the label - the same shape pass_word_loop_var_to_reg_bc
 * looks for, just one instruction instead of a pair), referenced
 * elsewhere in the function only via one of four whitelisted shapes -
 * loaded into l or a, or stored back from l or a - is kept in register C
 * for the function's entire body instead of round-tripping through its
 * frame slot on every use, under the exact same conditions as that pass
 * (a real loop, possibly with more than one back-edge; never has its
 * address taken, checked the same two ways; C completely unused
 * anywhere else in the function).
 *
 * Deliberately narrower in what it rewrites than the word version: it
 * only ever replaces the memory fetch/store itself ("ld l,(ix+O)" ->
 * "ld l,c", not anything downstream of it). For a signed byte used in a
 * 16-bit context, dcc's codegen sign-extends after every load ("ld a,l /
 * rlca / sbc a,a / ld h,a") - this pass leaves that sequence completely
 * untouched and just feeds it from a register instead of memory. That
 * still saves the full memory-load cost (a direct (ix+d) byte fetch is
 * comparable to the pair fetch pass_word_loop_var_to_reg_bc eliminates),
 * without needing to reason about every way dcc might phrase "produce
 * this byte's sign-extended 16-bit value" - including the existing
 * a_tracks_ix fusion, which already elides a redundant reload into A
 * right before this pass would apply and needs no awareness of this
 * pass at all, since it never touches "(ix+O)" text itself.
 */
int pass_byte_loop_var_to_reg_c(void)
{
    int i, j, changed = 0;
    int off;
    int func_start, func_end;
    int backedge_line;
    char label[128];
    char pat_l[32], pat_a[32], pat_stl[32], pat_sta[32];
    int bad;
    int bc_used_elsewhere;

    for (i = 0; i < nlines; i++) {
        char t1[MAX_LINE];

        strip_peep_comment_copy(t1, lines[i]);
        if (strncmp(t1, "ld (ix", 6) != 0)
            continue;
        if (sscanf(t1 + 6, "%d),l", &off) != 1)
            continue;
        if (off >= 0)
            continue;

        if (!starts_label(lines[i + 1]))
            continue;
        if (!label_name_at(i + 1, label))
            continue;

        backedge_line = -1;
        for (j = i + 2; j < nlines; j++) {
            char tgt[128];
            if (strncmp(lines[j], "public ", 7) == 0 ||
                strncmp(lines[j], "; static function ", 18) == 0)
                break;
            if (jump_target_any(lines[j], tgt) && !strcmp(tgt, label))
                backedge_line = j;
        }
        if (backedge_line < 0)
            continue;

        find_function_bounds_any(i, &func_start, &func_end);

        /* Same &local detection as pass_word_loop_var_to_reg_bc: after
         * "push ix / pop hl", dcc uses either a bare offset plus add hl,de
         * or repeated inc/dec hl, neither visible to the IX substring scan. */
        {
            char pat_addr_de[32];
            int addr_taken;

            sprintf(pat_addr_de, "ld de,%d", off);
            addr_taken = 0;
            for (j = func_start; j + 3 < func_end; j++) {
                char t0[MAX_LINE], t1b[MAX_LINE], t2[MAX_LINE], t3[MAX_LINE];
                int addr_off, k;

                strip_peep_comment_lower_copy(t0, lines[j]);
                if (strcmp(t0, "push ix") != 0)
                    continue;
                strip_peep_comment_lower_copy(t1b, lines[j + 1]);
                if (strcmp(t1b, "pop hl") != 0)
                    continue;
                addr_off = 0;
                for (k = j + 2; k < func_end; ++k) {
                    strip_peep_comment_lower_copy(t2, lines[k]);
                    if (strcmp(t2, "inc hl") == 0)
                        ++addr_off;
                    else if (strcmp(t2, "dec hl") == 0)
                        --addr_off;
                    else
                        break;
                    if (addr_off == off) {
                        addr_taken = 1;
                        break;
                    }
                }
                if (addr_taken)
                    break;
                strip_peep_comment_lower_copy(t3, lines[j + 3]);
                if (strcmp(t3, "add hl,de") != 0)
                    continue;
                strip_peep_comment_lower_copy(t2, lines[j + 2]);
                if (strcmp(t2, pat_addr_de) == 0) {
                    addr_taken = 1;
                    break;
                }
            }
            if (addr_taken)
                continue;
        }

        sprintf(pat_l,   "ld l,(ix%d)", off);
        sprintf(pat_a,   "ld a,(ix%d)", off);
        sprintf(pat_stl, "ld (ix%d),l", off);
        sprintf(pat_sta, "ld (ix%d),a", off);

        bad = 0;
        for (j = func_start; j < func_end; j++) {
            char t[MAX_LINE];
            char off_txt[32];

            strip_peep_comment_lower_copy(t, lines[j]);
            sprintf(off_txt, "(ix%d)", off);
            if (strstr(t, off_txt) == NULL)
                continue;
            if (strcmp(t, pat_l) == 0 || strcmp(t, pat_a) == 0 ||
                strcmp(t, pat_stl) == 0 || strcmp(t, pat_sta) == 0)
                continue;
            bad = 1;
            break;
        }
        if (bad)
            continue;

        bc_used_elsewhere = 0;
        for (j = func_start; j < func_end; j++) {
            if (line_clobbers_bc(lines[j])) {
                bc_used_elsewhere = 1;
                break;
            }
        }
        if (bc_used_elsewhere)
            continue;

        for (j = func_start; j < func_end; j++) {
            char t[MAX_LINE];

            strip_peep_comment_lower_copy(t, lines[j]);
            if (strcmp(t, pat_l) == 0) {
                replace1_tagged(j, "ld l,c", "byte_loop_var_c");
                changed = 1;
            } else if (strcmp(t, pat_a) == 0) {
                replace1_tagged(j, "ld a,c", "byte_loop_var_c");
                changed = 1;
            } else if (strcmp(t, pat_stl) == 0) {
                replace1_tagged(j, "ld c,l", "byte_loop_var_c");
                changed = 1;
            } else if (strcmp(t, pat_sta) == 0) {
                replace1_tagged(j, "ld c,a", "byte_loop_var_c");
                changed = 1;
            }
        }
    }

    return changed;
}

/*
 * pass_byte_for_counter_to_reg_c:
 *
 * The incrementing counterpart of pass_byte_loop_counter_to_reg_c above:
 * promotes a byte-sized for-loop counter into Z80 register C, for the
 * shape:
 *
 *   ld (ix+O),LOW              ; init, immediately before the loop label
 * LABEL:
 *   <body, every reference to (ix+O) one of exactly the two whitelisted
 *    "zero-extend into a 16-bit register pair" shapes - ld e,(ix+O)/ld d,0
 *    or ld l,(ix+O)/ld h,0 - and every call to __mods/__divs specifically>
 *   inc (ix+O)
 *   ld a,(ix+O)
 *   cp HIGH
 *   jp c, LABEL
 *
 * to:
 *
 *   ld c,LOW
 * LABEL:
 *   <body, with (ix+O) references rewritten to use c directly>
 *   inc c
 *   ld a,c
 *   cp HIGH
 *   jp c, LABEL
 *   ld (ix+O),c                ; write back once, right after the loop -
 *                                anything after the loop that still reads
 *                                the slot sees the correct final value,
 *                                without needing to prove it doesn't
 *
 * Declines (the safe default, missing the optimization but never
 * misapplying it) if any other reference to the counter's slot, any other
 * call, or any other label appears in the body - matches
 * pass_byte_loop_counter_to_reg_c's own restriction to a single-entry,
 * straight-line loop body.
 */
int pass_byte_for_counter_to_reg_c(void)
{
    int i;
    int changed;
    int off;
    int low_val;
    int high_val;
    char label[128];
    char tgt[128];
    int loop_end;
    int k;
    int ok;
    char pat_ix[40];
    char pat_lde[40];
    char pat_lhl[40];
    char pat_adda[40];
    char prime[16];
    char writeback[40];
    char exp_lda[40];

    changed = 0;

    for (i = 1; i + 1 < nlines; ++i) {
        if (!starts_label(lines[i]))
            continue;
        if (!peep_parse_ld_ix_byte_imm(lines[i - 1], &off, &low_val))
            continue;

        strcpy(label, lines[i]);
        strip_label_colon(label);

        /* Find this loop's own closing conditional jump back to the
         * label, with no other label in between (single-entry,
         * straight-line body). */
        loop_end = -1;
        for (k = i + 1; k < nlines; ++k) {
            if (starts_label(lines[k]))
                break;
            if (jump_target(lines[k], tgt) && strcmp(tgt, label) == 0) {
                loop_end = k;
                break;
            }
        }
        if (loop_end < i + 4)
            continue;

        /* The three lines immediately before the closing branch must be
         * the increment/compare/test sequence for this same offset. */
        {
            int inc_off;
            if (!peep_parse_inc_ix_byte(lines[loop_end - 3], &inc_off) || inc_off != off)
                continue;
        }
        sprintf(exp_lda, "ld a,(ix%+d)", off);
        if (!eq(loop_end - 2, exp_lda))
            continue;
        if (!peep_parse_cp_const(lines[loop_end - 1], &high_val))
            continue;
        if (low_val < 0 || low_val > 255 || high_val < 0 || high_val > 255)
            continue;

        sprintf(pat_ix, "(ix%+d)", off);
        sprintf(pat_lde, "ld e,(ix%+d)", off);
        sprintf(pat_lhl, "ld l,(ix%+d)", off);
        sprintf(pat_adda, "add a,(ix%+d)", off);

        ok = 1;
        for (k = i + 1; k < loop_end - 3 && ok; ++k) {
            if (strncmp(lines[k], "call ", 5) == 0) {
                if (!eq(k, "call __mods") && !eq(k, "call __divs")) { ok = 0; break; }
                continue;
            }
            if (match_counter_zero_extend(k, pat_lde, pat_lhl) != COUNTER_USE_NONE) {
                ++k;
                continue;
            }
            /* The counter's own byte value used directly in arithmetic
             * (e.g. `(rec + i) & 0xff`, added to another byte in A) - safe
             * to read from c instead, same as the index-load shapes above. */
            if (eq(k, pat_adda)) continue;
            if (strstr(lines[k], pat_ix) != NULL) { ok = 0; break; }
            /* B/C/BC must be free for the whole loop body except the exact
             * shapes above - guards against another pass (e.g.
             * pass_hoist_index_ptr_to_bc) having already claimed BC for
             * something else in this same loop. */
            if (line_touches_bc(lines[k])) { ok = 0; break; }
        }
        if (!ok)
            continue;

        /* line_touches_bc above only covers this loop's own body - it can't
         * see a whole-function or earlier-loop reg_alloc candidate primed
         * before this loop and still live here. Asked over the loop's own
         * span, since that is exactly how long this pass needs C. */
        if (bc_regalloc_claimed_in_range(i, loop_end))
            continue;

        /* In-place replacements first, while every index computed above is
         * still valid (no lines inserted/deleted yet). */
        for (k = i + 1; k < loop_end - 3; ++k) {
            if (eq(k, pat_lde)) { replace1(k, "ld e,c"); continue; }
            if (eq(k, pat_lhl)) { replace1(k, "ld l,c"); continue; }
            if (eq(k, pat_adda)) { replace1(k, "add a,c"); continue; }
        }
        replace1_tagged(loop_end - 3, "inc c", "byte_for_counter_to_reg_c");
        replace1(loop_end - 2, "ld a,c");

        /* Write the counter back to its frame slot once, right after the
         * loop exits (the very next line, whatever it is) - covers any use
         * of the slot after the loop without needing to prove there isn't
         * one. Farther from the label than the init replacement below, so
         * do it first while index loop_end is still valid. */
        sprintf(writeback, "ld (ix%+d),c", off);
        insert_line(loop_end + 1, writeback);

        /* Prime the register in place of the old init store. */
        sprintf(prime, "ld c,%d", low_val);
        replace1_tagged(i - 1, prime, "byte_for_counter_to_reg_c");

        changed = 1;
    }

    return changed;
}

/*
 * pass_byte_for_counter_to_reg_e:
 *
 * Like pass_byte_for_counter_to_reg_c above, but targets register E
 * instead of C - for when C/BC is already claimed by something else in
 * the same loop (most commonly pass_hoist_index_ptr_to_bc's pointer,
 * which is exactly why this exists: tests/tbig.c's fill_record/
 * check_record hoist their pointer into BC, which then blocks the C
 * version of this pass outright). D/E are free in the same situation,
 * since the whole family of these passes centers on a "zero-extend the
 * counter into a 16-bit pair" shape that never involves B/C on its own.
 * This matches z88dk's own zsdcc output for this exact loop shape: it
 * keeps the counter in E for the whole loop and never touches the frame
 * slot at all until (if ever) it's needed after the loop.
 *
 * One meaningful difference from the C version: since E is the SAME
 * register the zero-extend shape already loads the counter into, "ld
 * e,(ix+O)" is not just safe to redirect - once e IS the counter, that
 * load is entirely redundant and is deleted rather than rewritten (one
 * fewer instruction per occurrence than the C version manages).
 *
 * Same restrictions as pass_byte_for_counter_to_reg_c: single-entry,
 * straight-line body (no internal label - this pass has not been proven
 * safe with one the way pass_hoist_index_ptr_to_bc was), every reference
 * to the counter's slot is one of the three whitelisted shapes, and every
 * call is __mods/__divs.
 */
int pass_byte_for_counter_to_reg_e(void)
{
    int i;
    int changed;
    int off;
    int low_val;
    int high_val;
    char label[128];
    int loop_end;
    int k;
    int ok;
    char pat_ix[40];
    char pat_lde[40];
    char pat_lhl[40];
    char pat_adda[40];
    char prime[16];
    char writeback[40];
    char exp_lda[40];

    changed = 0;

    for (i = 1; i + 1 < nlines; ++i) {
        int init_line;
        int scan_limit;

        if (!starts_label(lines[i]))
            continue;

        /* The counter's own init store is usually right before the label,
         * but pass_hoist_index_ptr_to_bc may have inserted its own
         * pointer-prime lines in between (it runs earlier in the fixed-
         * point pass list) - scan backward a bounded distance to find it,
         * stopping at the first label (a different construct entirely). */
        init_line = -1;
        scan_limit = i - 8;
        if (scan_limit < 0) scan_limit = 0;
        for (k = i - 1; k >= scan_limit; --k) {
            if (starts_label(lines[k]))
                break;
            if (peep_parse_ld_ix_byte_imm(lines[k], &off, &low_val)) {
                init_line = k;
                break;
            }
        }
        if (init_line < 0)
            continue;

        strcpy(label, lines[i]);
        strip_label_colon(label);

        /* Find the loop's own closing branch - the LAST line in the
         * function that jumps back to the label (see
         * pass_hoist_index_ptr_to_bc's own history for why "last", not
         * "first"). An internal label (an if/early-return inside the loop
         * body, e.g. tests/tbig.c's check_record) is fine PROVIDED
         * loop_body_internal_labels_safe proves it's purely an intra-loop
         * merge point, and loop_body_escapes_safe_for_offset proves every
         * early-exit path out of the loop never reads the counter's frame
         * slot before reaching the function's own epilogue - the register
         * holds the authoritative value for the whole loop, and unlike
         * pass_hoist_index_ptr_to_bc (which never writes the frame slot at
         * all, so any read of it anywhere is always correct), this pass's
         * write-back only runs once, at the loop's own NORMAL exit, never
         * on an early-exit path. */
        loop_end = find_last_loop_back(i + 1, label, 0);
        if (loop_end < i + 4)
            continue;
        if (!loop_body_internal_labels_safe(i + 1, loop_end))
            continue;

        {
            int inc_off;
            if (!peep_parse_inc_ix_byte(lines[loop_end - 3], &inc_off) || inc_off != off)
                continue;
        }
        sprintf(exp_lda, "ld a,(ix%+d)", off);
        if (!eq(loop_end - 2, exp_lda))
            continue;
        if (!peep_parse_cp_const(lines[loop_end - 1], &high_val))
            continue;
        if (low_val < 0 || low_val > 255 || high_val < 0 || high_val > 255)
            continue;

        sprintf(pat_ix, "(ix%+d)", off);
        if (!loop_body_escapes_safe_for_offset(i + 1, loop_end, pat_ix))
            continue;
        /* Anything skipped between the init line and the label must not
         * itself reference our counter's offset - it should only be an
         * unrelated pass's own prime lines for some other variable. */
        {
            int bad_gap = 0;
            for (k = init_line + 1; k < i; ++k) {
                if (strstr(lines[k], pat_ix) != NULL) { bad_gap = 1; break; }
            }
            if (bad_gap)
                continue;
        }
        sprintf(pat_lde, "ld e,(ix%+d)", off);
        sprintf(pat_lhl, "ld l,(ix%+d)", off);
        sprintf(pat_adda, "add a,(ix%+d)", off);

        ok = 1;
        for (k = i + 1; k < loop_end - 3 && ok; ++k) {
            if (strncmp(lines[k], "call ", 5) == 0) {
                if (!eq(k, "call __mods") && !eq(k, "call __divs")) { ok = 0; break; }
                continue;
            }
            if (match_counter_zero_extend(k, pat_lde, pat_lhl) == COUNTER_USE_DE) {
                /* The zero-extend is almost always immediately consumed by
                 * "add hl,de" (the address computation this whole family of
                 * passes exists to speed up) - that's the expected, safe
                 * use of the value just zero-extended into d/e, not some
                 * other conflicting use of the pair. */
                ++k;
                if (eq(k + 1, "add hl,de"))
                    ++k;
                continue;
            }
            if (match_counter_zero_extend(k, pat_lde, pat_lhl) == COUNTER_USE_HL) {
                ++k;
                continue;
            }
            if (eq(k, pat_adda)) continue;
            if (strstr(lines[k], pat_ix) != NULL) { ok = 0; break; }
            /* D/E must be free for the whole loop body except the exact
             * shapes above - the same guard pass_hoist_index_ptr_to_bc
             * uses for B/C, parameterized for D/E instead. */
            if (line_touches_de(lines[k])) { ok = 0; break; }
        }
        if (!ok)
            continue;

        /* Transform back-to-front: the "ld e,(ix+O)" deletion shifts every
         * later line up by one, so process from the end of the range
         * backward, exactly like pass_ix_frame_ptr_load_deadd's own
         * delete+insert combo - each deletion then only affects indices
         * already handled, never ones still to be checked. */
        for (k = loop_end - 4; k >= i + 1; --k) {
            if (eq(k, pat_lhl)) { replace1(k, "ld l,e"); continue; }
            if (eq(k, pat_adda)) { replace1(k, "add a,e"); continue; }
            if (eq(k, pat_lde) && eq(k + 1, "ld d,0")) {
                delete_n(k, 1);
                loop_end--;
                continue;
            }
        }
        replace1_tagged(loop_end - 3, "inc e", "byte_for_counter_to_reg_e");
        replace1(loop_end - 2, "ld a,e");

        /* Write the counter back to its frame slot once, right after the
         * loop exits, same rationale as pass_byte_for_counter_to_reg_c. */
        sprintf(writeback, "ld (ix%+d),e", off);
        insert_line(loop_end + 1, writeback);

        /* Prime the register in place of the old init store. */
        sprintf(prime, "ld e,%d", low_val);
        replace1_tagged(init_line, prime, "byte_for_counter_to_reg_e");

        changed = 1;
    }

    return changed;
}

/*
 * IYL counterpart of pass_byte_loop_counter_to_reg_c: the identical self-
 * guarding decrementing-loop-counter shape (see that pass's comment), but
 * promoted into IY's low byte via undocumented FD-prefixed opcodes instead
 * of register C, since M80 (and m80c) don't recognize the "iyl"/"iyh"
 * mnemonic spellings directly: each use site emits the raw opcode bytes as
 * "db 0FDh,xx" instead (DEC IYL=2Dh, LD A,IYL=7Dh, LD IYL,A=6Fh, LD E,IYL=
 * 5Dh, INC IYL=2Ch). These used to be M80 MACRO/ENDM definitions invoked by
 * name (IYDECL/IYLDA/IYSTA/IYLDE), but m80c - the native assembler that
 * later became the default toolchain - never implemented MACRO/ENDM, so
 * that indirection was replaced with the equivalent literal bytes at each
 * site; the nested-loop collision check below now recognizes the "db
 * 0FDh," prefix instead of an "IY" name prefix.
 *
 * Incoming IY is saved before the single-entry loop and restored at its
 * unique exit. Ordinary calls may therefore occur inside the loop because
 * the dcc ABI requires every callee to preserve IY; same-file IY claims
 * still decline the optimization to avoid overlapping a compiler home.
 *
 * "ld l,(ix+off)" can't become a single "ld l,iyl": the FD prefix redirects
 * EVERY H/L reference in an instruction, so "ld l,iyl" would actually
 * encode "ld iyl,iyl" - there is no single-instruction undocumented form
 * that reads IYL into the real L register (E, unaffected by the H/L
 * substitution rule, has no such problem - "ld e,iyl" is a clean single
 * instruction). That whitelisted shape expands to two lines (the LD A,IYL
 * byte sequence, then "ld l,a") instead of a single-line replacement.
 */
int pass_byte_loop_counter_to_reg_iyl(void)
{
    int i;
    int changed;
    int off;
    char label[128];
    char target[128];
    int loop_end;
    int k;
    int ok;
    int needs_iyl;
    char pat_ix[40];
    char pat_lde[40];
    char pat_lhl[40];
    char prime[40];
    char writeback[40];
    char callee[128];
    const char *p;

    changed = 0;
    if (dcc_iy_claimed_in_file())
        return 0;

    for (i = 0; i + 2 < nlines; ++i) {
        if (!starts_label(lines[i]))
            continue;
        if (!peep_parse_dec_ix_byte(lines[i + 1], &off))
            continue;
        if (!parse_jp_cond_label(lines[i + 2], "z", target))
            continue;

        strcpy(label, lines[i]);
        strip_label_colon(label);

        /* Find the matching loop-back jump to this same label, with no
         * other label in between (single-entry, single-exit body). */
        loop_end = find_straight_line_loop_back(i + 3, label);
        if (loop_end < 0)
            continue;
        if (!iy_loop_borrow_safe(i, loop_end, label, target))
            continue;

        sprintf(pat_ix, "(ix%+d)", off);
        sprintf(pat_lde, "ld e,(ix%+d)", off);
        sprintf(pat_lhl, "ld l,(ix%+d)", off);

        ok = 1;
        needs_iyl = 0;
        for (k = i + 3; k < loop_end && ok; ++k) {
            /* IY is a single register: a NESTED loop (this candidate's body
             * contains another loop already promoted to IYL by an earlier
             * match in this same scan - inner loops are found first, since
             * their tail text appears before an enclosing loop's own tail)
             * would silently clobber it. Decline outright - a real bug
             * here corrupted mm.c's matrix multiply (all three nested
             * i/j/k counters tried to claim IYL at once) before this check
             * existed. Detected by the literal "db 0FDh," prefix every
             * IYDECL/IYLDA/IYSTA/IYLDE/IYINCL emission site below produces
             * (was a "IY" prefix check back when these were M80 macro
             * invocations by name; m80c has no MACRO/ENDM support at all,
             * so they're emitted as raw opcode bytes directly now). */
            if (line_uses_iy_half_register(k)) {
                ok = 0;
                continue;
            }
            if (strncmp(lines[k], "call ", 5) == 0) {
                strip_peep_comment_copy(callee, lines[k]);
                p = callee + 5;
                while (*p == ' ' || *p == '\t')
                    p++;
                if (is_local_func_label(p))
                    ok = 0;
                else if (strcmp(p, "__mods") != 0 && strcmp(p, "__divs") != 0)
                    needs_iyl = 1;
                continue;
            }
            if (strstr(lines[k], pat_ix) == NULL)
                continue;
            if (match_counter_zero_extend(k, pat_lde, pat_lhl) != COUNTER_USE_NONE) {
                ++k;
                continue;
            }
            ok = 0;
        }
        /* If every call in the body is __mods/__divs (or there are none),
         * pass_byte_loop_counter_to_reg_c's own whitelist already covers
         * this loop - defer to it rather than racing it for the same
         * pattern. Whichever of the two runs first within a given
         * fixed-point pass depends on how many other passes' preconditions
         * this exact loop still needs to satisfy first, which is NOT the
         * same thing as their static order in the pass list; a real
         * regression on e.c (whose loop is fully __mods/__divs-eligible)
         * showed IYL winning that race after an unrelated pass reordering,
         * which is strictly worse than register C here (no FD-prefix tax,
         * and no forced two-line expansion for the "ld l,(ix+off)" shape).
         * IYL should only ever be used for what C structurally cannot
         * handle at all. */
        if (!needs_iyl)
            ok = 0;
        if (!ok)
            continue;

        /* Restore at the unique exit first, while its index is still stable. */
        insert_line_tagged(loop_end + 2, "pop iy",
                           "byte_loop_counter_to_reg_iyl_abi");

        /* In-place replacements first, while every index computed above is
         * still valid. The "ld l,(ix+off)" shape is the one exception -
         * expanding to two lines shifts everything after it, so loop_end
         * and k are bumped in lockstep right there. */
        replace1_tagged(i + 1, "db 0FDh,02Dh", "byte_loop_counter_to_reg_iyl");
        for (k = i + 3; k < loop_end; ++k) {
            if (eq(k, pat_lde)) {
                replace1_tagged(k, "db 0FDh,05Dh", "byte_loop_counter_to_reg_iyl");
                continue;
            }
            if (eq(k, pat_lhl)) {
                replace1_tagged(k, "db 0FDh,07Dh", "byte_loop_counter_to_reg_iyl");
                insert_line(k + 1, "ld l,a");
                loop_end++;
                ++k;
                continue;
            }
        }

        /* Write the counter back to its frame slot right after the
         * decrement, same rationale as pass_byte_loop_counter_to_reg_c
         * (safe regardless of whether anything after the loop still reads
         * the slot). IYLDA/writeback don't touch flags, so the Z flag
         * IYDECL just set is still valid at the exit branch. */
        insert_line_tagged(i + 2, "db 0FDh,07Dh", "byte_loop_counter_to_reg_iyl");
        sprintf(writeback, "ld (ix%+d),a", off);
        insert_line(i + 3, writeback);

        /* Prime the register right before the loop label. */
        sprintf(prime, "ld a,(ix%+d)", off);
        insert_line(i, prime);
        insert_line_tagged(i + 1, "db 0FDh,06Fh", "byte_loop_counter_to_reg_iyl");
        insert_line_tagged(i, "push iy",
                           "byte_loop_counter_to_reg_iyl_abi");

        changed = 1;
    }

    return changed;
}

/*
 * Increasing-loop counterpart of pass_byte_loop_counter_to_reg_iyl: dcc's
 * codegen for a byte-narrowed `for (i = 0; i < K; i++) BODY` (see
 * dcc_array_narrow.c's narrow_cond_upper_bounds_lt) tests at the BOTTOM of
 * the loop rather than the top:
 *
 *   LOOP:
 *     <body>
 *     inc (ix+off)
 *     ld a,(ix+off)
 *     cp K
 *     jp c, LOOP
 *
 * Same IYL promotion and ABI preservation rule as
 * pass_byte_loop_counter_to_reg_iyl. The writeback is nearly free here:
 * IYLDA already has to reload the fresh value into A for the "cp K"
 * comparison, so one more "ld (ix+off),a" covers every iteration's
 * writeback at essentially no extra cost.
 */
int pass_byte_incr_loop_counter_to_reg_iyl(void)
{
    int i;
    int changed;
    int off;
    int bound;
    char label[128];
    char tmp[128];
    int loop_start;
    int k;
    int ok;
    char pat_ix[40];
    char pat_lde[40];
    char pat_lhl[40];
    char pat_lda[40];
    char callee[128];
    const char *p;

    changed = 0;
    if (dcc_iy_claimed_in_file())
        return 0;

    for (i = 0; i + 3 < nlines; ++i) {
        if (!peep_parse_inc_ix_byte(lines[i], &off))
            continue;
        sprintf(pat_lda, "ld a,(ix%+d)", off);
        if (!eq(i + 1, pat_lda))
            continue;
        if (!peep_parse_cp_const(lines[i + 2], &bound))
            continue;
        if (!parse_jp_cond_label(lines[i + 3], "c", label))
            continue;

        /* Find the loop's own start label (the jp c,LABEL target),
         * searching backward - it must precede this tail. */
        loop_start = -1;
        for (k = i; k >= 0; --k) {
            if (!starts_label(lines[k]))
                continue;
            strcpy(tmp, lines[k]);
            {
                int n = (int)strlen(tmp);
                if (n > 0 && tmp[n - 1] == ':')
                    tmp[n - 1] = 0;
            }
            if (!strcmp(tmp, label)) {
                loop_start = k;
                break;
            }
        }
        if (loop_start < 0)
            continue;
        if (!iy_loop_borrow_safe(loop_start, i + 3, label, NULL))
            continue;

        sprintf(pat_ix, "(ix%+d)", off);
        sprintf(pat_lde, "ld e,(ix%+d)", off);
        sprintf(pat_lhl, "ld l,(ix%+d)", off);

        ok = 1;
        for (k = loop_start + 1; k < i && ok; ++k) {
            /* IY is a single register: a NESTED loop already promoted to
             * IYL by an earlier match in this same scan would silently
             * clobber it - see pass_byte_loop_counter_to_reg_iyl's comment
             * on the exact bug this caused in mm.c before this check
             * existed (i/j/k all tried to claim IYL simultaneously), and on
             * why this checks for the literal "db 0FDh," prefix rather than
             * an "IY" macro-name prefix. */
            if (line_uses_iy_half_register(k)) {
                ok = 0;
                continue;
            }
            if (strncmp(lines[k], "call ", 5) == 0) {
                strip_peep_comment_copy(callee, lines[k]);
                p = callee + 5;
                while (*p == ' ' || *p == '\t')
                    p++;
                if (is_local_func_label(p))
                    ok = 0;
                continue;
            }
            if (strstr(lines[k], pat_ix) == NULL)
                continue;
            if (match_counter_zero_extend(k, pat_lde, pat_lhl) != COUNTER_USE_NONE) {
                ++k;
                continue;
            }
            ok = 0;
        }
        if (!ok)
            continue;

        /* The conditional backedge falls through to this unique restore. */
        insert_line_tagged(i + 4, "pop iy",
                           "byte_incr_loop_counter_to_reg_iyl_abi");

        /* In-place replacements first, bumping i in lockstep with the one
         * two-line expansion (mirrors pass_byte_loop_counter_to_reg_iyl). */
        for (k = loop_start + 1; k < i; ++k) {
            if (eq(k, pat_lde)) {
                replace1_tagged(k, "db 0FDh,05Dh", "byte_incr_loop_counter_to_reg_iyl");
                continue;
            }
            if (eq(k, pat_lhl)) {
                replace1_tagged(k, "db 0FDh,07Dh", "byte_incr_loop_counter_to_reg_iyl");
                insert_line(k + 1, "ld l,a");
                ++i;
                ++k;
                continue;
            }
        }

        replace1_tagged(i, "db 0FDh,02Ch", "byte_incr_loop_counter_to_reg_iyl");
        replace1_tagged(i + 1, "db 0FDh,07Dh", "byte_incr_loop_counter_to_reg_iyl");
        {
            char storeback[40];
            sprintf(storeback, "ld (ix%+d),a", off);
            insert_line(i + 2, storeback);
        }

        /* Prime IYL right before the loop's own start label - inserted
         * last, since it shifts everything from loop_start onward (the
         * body/tail edits above are already done). */
        {
            char primeload[40];
            sprintf(primeload, "ld a,(ix%+d)", off);
            insert_line(loop_start, primeload);
            insert_line_tagged(loop_start + 1, "db 0FDh,06Fh", "byte_incr_loop_counter_to_reg_iyl");
            insert_line_tagged(loop_start, "push iy",
                               "byte_incr_loop_counter_to_reg_iyl_abi");
        }

        changed = 1;
    }

    return changed;
}


/* Trace forward from `start` following only unconditional control flow
 * (label fall-through and unconditional jumps), for up to a bounded
 * number of hops, to see whether this path reaches the function's own
 * epilogue ("ld sp,ix") before referencing `pat_ix` anywhere. Used by a
 * counter-registerization pass to prove that an early-exit path out of
 * its loop (e.g. an `if (...) return X;` inside the loop body) never
 * reads the counter's frame slot while it's stale - the register holds
 * the authoritative value for the whole loop, and the write-back that
 * resyncs the frame slot only runs once, at the loop's own normal exit,
 * never on an early-exit path. A conditional jump (ambiguous which way
 * execution goes), a call (could do anything), or running out of hops
 * without reaching "ld sp,ix" is treated as unprovable - a decline, not a
 * misapplication. */
static int escape_path_reaches_epilogue_safely(int start, const char *pat_ix,
                                               int func_end)
{
    int pos;
    int hops;
    char tgt[128];

    pos = start;
    for (hops = 0; hops < 60; ++hops) {
        if (pos < 0 || pos >= func_end)
            return 0;
        if (eq(pos, "ld sp,ix"))
            return 1;
        if (strstr(lines[pos], pat_ix) != NULL)
            return 0;
        if (starts_label(lines[pos])) { ++pos; continue; }
        if (is_uncond_jp(lines[pos])) {
            if (!jump_target(lines[pos], tgt))
                return 0;
            pos = find_label_line_in_range(tgt, 0, func_end);
            continue;
        }
        if (strncmp(lines[pos], "call ", 5) == 0)
            return 0;
        if (jump_target(lines[pos], tgt))
            return 0;  /* a conditional jump - which way is ambiguous */
        ++pos;
    }
    return 0;
}

/* For every jump within [lo, hi) whose target is OUTSIDE [lo, hi) (an
 * early-exit path out of the loop), verify via
 * escape_path_reaches_epilogue_safely that it never reads `pat_ix` before
 * reaching the function's own epilogue. Returns 1 iff every such escape
 * is safe (or there are none). */
static int loop_body_escapes_safe_for_offset(int lo, int hi, const char *pat_ix)
{
    int func_start, func_end;
    int k;
    char tgt[128];

    find_function_bounds(lo, &func_start, &func_end);
    for (k = lo; k < hi; ++k) {
        if (!jump_target(lines[k], tgt))
            continue;
        {
            int target_line = find_label_line_in_range(tgt, func_start, func_end);
            if (target_line >= lo && target_line < hi)
                continue;  /* jumps back into the loop's own range - fine */
            if (!escape_path_reaches_epilogue_safely(target_line, pat_ix, func_end))
                return 0;
        }
    }
    return 1;
}
