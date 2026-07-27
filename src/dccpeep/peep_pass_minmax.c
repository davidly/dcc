/* peep_pass_minmax.c - application-specific board/game passes.
 *
 * These passes target patterns emitted by the bundled ttt/chess sample
 * programs (the _MinMax, _FindSolution, board, winner, and posn-function
 * idioms). They are opt-in by pattern match: each declines unless it finds
 * the exact application shape, so they are inert on ordinary programs.
 */
#include "dccpeep_internal.h"

static int peep_is_pos_func_label(const char *s)
{
    if (s[0] == '_' &&
        s[1] == 'p' &&
        s[2] == 'o' &&
        s[3] == 's' &&
        strstr(s, "func:") != NULL)
        return 1;

    if (strcmp(s, "_LookForWinner:") == 0)
        return 1;

    return 0;
}

int pass_posfunc_b_cache(void)
{
    int i, j, end;
    int changed;
    int has_call;
    int has_ix1_store_after_setup;
    int setup_ld_a;
    int setup_store;
    int setup_kind;

    changed = 0;

    for (i = 0; i < nlines; i++) {
        if (!peep_is_pos_func_label(lines[i]))
            continue;

        end = i + 1;
        while (end < nlines && !peep_is_public_line(lines[end]))
            end++;

        /* line_clobbers_bc (not a bare "call " scan) so "call __stchk" -
         * present in every function's prologue under the default
         * -fstack-check build - doesn't block this pass on its own; see
         * line_clobbers_bc's own comment for why that call specifically
         * never touches B/C. Without this exemption, this pass could
         * never fire on any stack-checked build at all. */
        has_call = 0;
        for (j = i + 1; j < end; j++) {
            if (line_clobbers_bc(lines[j])) {
                has_call = 1;
                break;
            }
        }
        if (has_call)
            continue;

        /* Two possible preambles land x = *(hl) into (ix-1): the classic
         * "ld a,(hl); ld (ix-1),a" (2 lines, setup_kind 1), or the
         * ix-direct declaration-initializer fast path's "ld l,(hl);
         * ld h,0; ld (ix-1),l" (3 lines, setup_kind 2 - dcc_decl.c emits
         * a 16-bit-typed load/store even for a byte-sized x, zero-
         * extending into h - see pass_posfunc_collapse_b_setup's own
         * comment on the same shape). Both just mean B ends up holding
         * x's byte value once collapsed. */
        setup_ld_a = -1;
        setup_store = -1;
        setup_kind = 0;

        for (j = i + 1; j + 1 < end; j++) {
            if (eq(j, "ld a,(hl)") && eq(j + 1, "ld (ix-1),a")) {
                setup_ld_a = j;
                setup_store = j + 1;
                setup_kind = 1;
                break;
            }
            if (j + 2 < end &&
                eq(j, "ld l,(hl)") && eq(j + 1, "ld h,0") &&
                eq(j + 2, "ld (ix-1),l")) {
                setup_ld_a = j;
                setup_store = j + 2;
                setup_kind = 2;
                break;
            }
        }

        if (setup_ld_a < 0)
            continue;

        /* "cp (ix-1)" is a pure read (Z80's CP never writes its operand),
         * exactly like "ld a,(ix-1)"/"ld l,(ix-1)" - x compared against a
         * board byte loaded into A is at least as common a source order
         * as x loaded into A first, once dcc's comparison codegen picks
         * which operand to load first. */
        has_ix1_store_after_setup = 0;
        for (j = setup_store + 1; j < end; j++) {
            if (strstr(lines[j], "(ix-1)") != NULL &&
                strcmp(lines[j], "ld a,(ix-1)") != 0 &&
                strcmp(lines[j], "ld l,(ix-1)") != 0 &&
                strcmp(lines[j], "cp (ix-1)") != 0) {
                has_ix1_store_after_setup = 1;
                break;
            }
        }
        if (has_ix1_store_after_setup)
            continue;

        /* Remove the one-byte local allocation if present in the prologue. */
        for (j = i + 1; j < setup_ld_a; j++) {
            if (eq(j, "dec sp")) {
                delete_n(j, 1);
                end--;
                setup_ld_a--;
                setup_store--;
                changed = 1;
                break;
            }
        }

        replace1(setup_ld_a, "ld b,(hl)");
        if (setup_kind == 1) {
            delete_n(setup_store, 1);
            end--;
        } else {
            delete_n(setup_ld_a + 1, 2);
            end -= 2;
        }
        changed = 1;

        for (j = setup_ld_a + 1; j < end; j++) {
            if (eq(j, "ld a,(ix-1)")) {
                replace1(j, "ld a,b");
                changed = 1;
            } else if (eq(j, "ld l,(ix-1)")) {
                replace1(j, "ld l,b");
                changed = 1;
            } else if (eq(j, "cp (ix-1)")) {
                replace1(j, "cp b");
                changed = 1;
            }
        }
    }

    return changed;
}

int peep_in_function_range(const char *func, int *startp, int *endp)
{
    int i, end;

    for (i = 0; i < nlines; i++) {
        if (strcmp(lines[i], func) == 0) {
            end = i + 1;
            while (end < nlines && !peep_is_public_line(lines[end]))
                end++;
            startp[0] = i;
            endp[0] = end;
            return 1;
        }
    }

    return 0;
}

/*
 * True if any line in [start,end) is a "-g" debug annotation
 * (";@dcc-line", ";@dcc-var", ";@dcc-var-end", etc: anything starting with
 * ";@dcc-").  Used as a blanket guard for the pass_minmax_ and
 * pass_shrink_minmax_ family (and any other pass built the same way): those passes
 * were written and validated only against release-mode (non -g) codegen,
 * by pattern-matching long runs of strictly adjacent instructions with no
 * regard for what might sit in between. -g's per-statement comments can
 * land inside those runs and either break a match outright (harmless,
 * just a missed optimization) or - worse, and what actually happened here -
 * silently change which instructions a still-succeeding match captures,
 * producing a real miscompile rather than just imprecise debug info. Making
 * each such pass provably safe under -g individually is exactly the kind of
 * open-ended, error-prone audit that isn't worth it for a handful of
 * program-specific passes: just decline all of them outright when debug
 * annotations are present in range, and fall back to their unoptimized (but
 * always correct) input for that one function.
 */
int peep_range_has_debug_annotations(int start, int end)
{
    int i;

    for (i = start; i < end; i++)
        if (strncmp(lines[i], ";@dcc-", 6) == 0)
            return 1;
    return 0;
}

int pass_minmax_winner_result_no_temp(void)
{
    int start;
    int end;
    int i;
    int j;
    int changed;

    changed = 0;

    if (!peep_in_function_range("_MinMax:", &start, &end))
        return 0;
    if (peep_range_has_debug_annotations(start, end))
        return 0;

    /*
     * The winner-function result is returned in L.  The generated code stores
     * it into ix-3, tests it, then reloads ix-3 to compare with PieceX.
     * But ix-3 is later overwritten with the loop variable p=0, and the
     * zero/PieceX tests do not need the spill.
     *
     * Accept both old and new dispatch cleanup shapes:
     *
     *     call __call_hl
     *     ld (ix-3),l
     *
     * and:
     *
     *     call __call_hl
     *     pop bc
     *     ld (ix-3),l
     */
    for (i = start; i + 5 < end; ++i) {
        if (!eq(i, "call __call_hl"))
            continue;

        j = i + 1;
        while (j < end && eq(j, "pop bc"))
            ++j;

        if (j + 4 < end &&
            eq(j, "ld (ix-3),l") &&
            eq(j + 1, "ld a,l") &&
            eq(j + 2, "or a") &&
            strncmp(lines[j + 3], "jp z,", 5) == 0 &&
            eq(j + 4, "ld a,(ix-3)")) {
            delete_n(j, 1);
            end--;
            replace1_tagged(j + 3, "ld a,l", "winner_result_no_temp");
            changed = 1;
            if (i > start)
                --i;
        }
    }

    return changed;
}

int pass_minmax_score_b_cache(void)
{
    int start;
    int end;
    int i;
    int j;
    int changed;
    char tmp[MAX_LINE];

    changed = 0;

    if (!peep_in_function_range("_MinMax:", &start, &end))
        return 0;
    if (peep_range_has_debug_annotations(start, end))
        return 0;

    /*
     * In the uint8_t MinMax variant, score is stored in the byte local ix-4
     * immediately after the recursive call:
     *
     *     call _MinMax
     *     pop bc ...
     *     ld (ix-4),l
     *
     * From there until the next recursive call, it is only read as
     *     ld a,(ix-4)
     * and no calls occur.  Keep it in E instead (B is reserved for the loop
     * counter p in pass_minmax_loop_ctr_b).  This also lets the frame shrink
     * from 4 bytes to 3 bytes after all ix-4 references disappear.
     */
    for (i = start; i < end; ++i) {
        if (!eq(i, "ld (ix-4),l"))
            continue;

        /* Make sure this really follows the recursive call cleanup. */
        j = i - 1;
        while (j > start && eq(j, "pop bc"))
            --j;
        if (!eq(j, "call _MinMax"))
            continue;

        replace1_tagged(i, "ld e,l", "minmax_score_e");

        for (j = i + 1; j < end; ++j) {
            if (eq(j, "call _MinMax"))
                break;

            strip_peep_comment_copy(tmp, lines[j]);
            if (!strcmp(tmp, "ld a,(ix-4)")) {
                replace1_tagged(j, "ld a,e", "minmax_score_e");
                changed = 1;
                continue;
            }

            /*
             * Be conservative.  If some future code writes ix-4 or loads it
             * in a non-A form, stop caching for this region.
             */
            if (strstr(tmp, "(ix-4)") != NULL)
                break;
        }

        changed = 1;
    }

    return changed;
}

/*
 * pass_minmax_loop_ctr_b:
 *
 * Move the MinMax blank-cell loop counter p from the IX frame slot (ix-3)
 * into register B.  This drops the loop overhead from ~59T to ~25T per
 * iteration by replacing slow IX-relative loads/stores with register ops.
 *
 * Requires pass_minmax_score_e to have already moved score from B to E,
 * freeing B for the loop counter.
 *
 * Replacements within _MinMax:
 *   ld (ix-3),0  →  ld b,0          (init)
 *   ld e,(ix-3)  →  ld e,b          (address compute: 19T → 4T)
 *   ld l,(ix-3)  →  ld l,b          (push move arg: 19T → 4T)
 *   inc (ix-3)   →  inc b           (loop increment: 23T → 4T)
 *   ld a,(ix-3)  →  ld a,b          (loop test: 19T → 4T)
 *
 * After "call _MinMax; pop bc×N; ld e,l", the 4th pop has already left
 * p in C (the move argument was pushed as L=p, H=0, so pop bc gives C=p).
 * Insert "ld b,c" to restore the loop counter from C.
 */
int pass_minmax_loop_ctr_b(void)
{
    int start, end, i, changed = 0;
    char tmp[MAX_LINE];

    if (!peep_in_function_range("_MinMax:", &start, &end))
        return 0;
    if (peep_range_has_debug_annotations(start, end))
        return 0;

    /* Only run after:
     * (a) pass_minmax_score_e committed: ld e,l present, ld b,l absent.
     * (b) pass_minmax_winner_result_no_temp cleaned up: no ld (ix-3),l store.
     *     That store is the winner-result spill; until it is removed, some
     *     ld a,(ix-3) references belong to the winner check, not the loop
     *     counter, and must not be replaced with ld a,b. */
    {
        int has_score_e = 0, has_score_b = 0;
        for (i = start; i < end; i++) {
            strip_peep_comment_copy(tmp, lines[i]);
            if (!strcmp(tmp, "ld e,l"))      has_score_e = 1;
            if (!strcmp(tmp, "ld b,l"))      has_score_b = 1;
            if (!strcmp(tmp, "ld (ix-3),l")) return 0; /* winner spill still present */
        }
        if (!has_score_e || has_score_b) return 0;
    }

    /* _MinMax is an ordinary function like any other: dcc's own reg_alloc
     * could in principle have claimed BC for a whole-function candidate
     * here too, and this pass has no visibility into that. See
     * bc_regalloc_claimed_before's own comment - end, not start, since the
     * whole [start,end) range is being claimed for B, not just one loop. */
    if (bc_regalloc_claimed_before(end))
        return 0;

    /* Replace all (ix-3) loop-counter references with B.
     * All are 1-for-1 replacements so nlines and end are unchanged. */
    for (i = start; i < end; i++) {
        strip_peep_comment_copy(tmp, lines[i]);

        if (!strcmp(tmp, "ld (ix-3),0")) {
            replace1_tagged(i, "ld b,0", "minmax_loop_ctr_b");
            changed = 1;
        } else if (!strcmp(tmp, "ld e,(ix-3)")) {
            replace1_tagged(i, "ld e,b", "minmax_loop_ctr_b");
            changed = 1;
        } else if (!strcmp(tmp, "ld l,(ix-3)")) {
            replace1_tagged(i, "ld l,b", "minmax_loop_ctr_b");
            changed = 1;
        } else if (!strcmp(tmp, "inc (ix-3)")) {
            replace1_tagged(i, "inc b", "minmax_loop_ctr_b");
            changed = 1;
        } else if (!strcmp(tmp, "ld a,(ix-3)")) {
            replace1_tagged(i, "ld a,b", "minmax_loop_ctr_b");
            changed = 1;
        }
    }

    /* After "pop bc × N; ld e,l", insert "ld b,c" to recover the loop
     * counter from C.  The 4th pop bc left p in C because the move arg
     * was pushed as L=p, H=0, so pop bc gives B=0, C=p. */
    for (i = start; i < end - 1; i++) {
        strip_peep_comment_copy(tmp, lines[i]);
        if (strcmp(tmp, "ld e,l") != 0) continue;

        if (!eq(i - 1, "pop bc")) continue;   /* must follow a pop bc */

        strip_peep_comment_copy(tmp, lines[i + 1]);
        if (!strcmp(tmp, "ld b,c")) continue;  /* already inserted */
        if (!strcmp(tmp, "pop bc")) continue;  /* pass_minmax_value_c replaced it */

        insert_line_tagged(i + 1, "ld b,c", "minmax_loop_ctr_b");
        end++;
        changed = 1;
    }

    return changed;
}

/*
 * pass_minmax_value_c:
 *
 * Move the MinMax "value" variable from the IX frame slot (ix-1) into
 * register C.  Requires pass_minmax_loop_ctr_b to have already moved the
 * loop counter to B and score to E, freeing C.
 *
 * Replacements within _MinMax:
 *   ld (ix-1),2/9  →  ld c,2/9       (init before loop)
 *   cp (ix-1)      →  cp c            (score vs value: 15T → 4T)
 *   ld (ix-1),a    →  ld c,a          (value = score: 19T → 4T)
 *   ld a,(ix-1)    →  ld a,c          (load value: 19T → 4T)
 *   ld l,(ix-1)    →  ld l,c          (return value: 19T → 4T)
 *
 * Call save/restore: B=p and C=value must survive the recursive call.
 * Insert "push bc" after the board-address push (before arg pushes).
 * Replace the existing "ld b,c" (loop counter recovery) with "pop bc"
 * which simultaneously restores both B=p and C=value.
 *
 * Also shrinks the frame from 2 to 1 byte (only ix-2 = pieceMove remains):
 * pass_shrink_minmax_frame1_after_value_c handles that.
 */
int pass_minmax_value_c(void)
{
    int start, end, i, changed = 0;
    char tmp[MAX_LINE];

    if (!peep_in_function_range("_MinMax:", &start, &end))
        return 0;
    if (peep_range_has_debug_annotations(start, end))
        return 0;

    /* Guard: pass_minmax_loop_ctr_b must have committed (ld b,c present,
     * no (ix-3) remaining).  If (ix-1) is already gone, nothing to do. */
    {
        int has_bc = 0, has_ix1 = 0;
        for (i = start; i < end; i++) {
            strip_peep_comment_copy(tmp, lines[i]);
            if (!strcmp(tmp, "ld b,c"))        has_bc  = 1;
            if (strstr(lines[i], "(ix-3)"))    return 0; /* loop_ctr_b not done */
            if (strstr(lines[i], "(ix-1)"))    has_ix1 = 1;
        }
        if (!has_bc || !has_ix1) return 0;
    }

    /* Transitively covered by pass_minmax_loop_ctr_b's own guard today (the
     * "ld b,c" this pass requires above only exists if that pass already
     * committed), but checked explicitly anyway rather than relying on that
     * chain never changing - see bc_regalloc_claimed_before's own comment. */
    if (bc_regalloc_claimed_before(end))
        return 0;

    /* Replace (ix-1) value references with C. */
    for (i = start; i < end; i++) {
        strip_peep_comment_copy(tmp, lines[i]);

        if (!strcmp(tmp, "ld (ix-1),2")) {
            replace1_tagged(i, "ld c,2", "minmax_value_c"); changed = 1;
        } else if (!strcmp(tmp, "ld (ix-1),9")) {
            replace1_tagged(i, "ld c,9", "minmax_value_c"); changed = 1;
        } else if (!strcmp(tmp, "cp (ix-1)")) {
            replace1_tagged(i, "cp c",   "minmax_value_c"); changed = 1;
        } else if (!strcmp(tmp, "ld (ix-1),a")) {
            replace1_tagged(i, "ld c,a", "minmax_value_c"); changed = 1;
        } else if (!strcmp(tmp, "ld a,(ix-1)")) {
            replace1_tagged(i, "ld a,c", "minmax_value_c"); changed = 1;
        } else if (!strcmp(tmp, "ld l,(ix-1)")) {
            replace1_tagged(i, "ld l,c", "minmax_value_c"); changed = 1;
        }
    }

    /* Insert "push bc" (save B=p, C=value) after the board-address push
     * and before the move-arg setup.  The board-address push is the "push hl"
     * that is immediately followed by "ld l,b" (move arg setup). */
    for (i = start; i < end - 1; i++) {
        strip_peep_comment_copy(tmp, lines[i]);
        if (strcmp(tmp, "push hl") != 0) continue;

        strip_peep_comment_copy(tmp, lines[i + 1]);
        if (strcmp(tmp, "ld l,b") != 0) continue;

        insert_line_tagged(i + 1, "push bc", "minmax_value_c");
        end++;
        changed = 1;
        i++;
    }

    /* Replace "ld b,c" (old loop counter recovery) with "pop bc" which
     * now restores both B=p and C=value from the "push bc" inserted above.
     * Pattern: "ld e,l" immediately followed by "ld b,c". */
    for (i = start; i < end - 1; i++) {
        strip_peep_comment_copy(tmp, lines[i]);
        if (strcmp(tmp, "ld e,l") != 0) continue;

        strip_peep_comment_copy(tmp, lines[i + 1]);
        if (strcmp(tmp, "ld b,c") != 0) continue;

        replace1_tagged(i + 1, "pop bc", "minmax_value_c");
        changed = 1;
    }

    return changed;
}

/*
 * pass_minmax_board_ptr_loop:
 *
 * In _MinMax, after the loop counter has been moved to B, the hot blank-cell
 * scan still recomputes &_g_board[B] at every iteration:
 *
 *   ld b,0
 * Lloop:
 *   ld hl,_g_board
 *   ld e,b
 *   ld d,0
 *   add hl,de
 *   ld a,(hl)
 *   or a
 *   jp nz,Ltail
 *   ... recursive call, with HL saved/restored as the board-cell pointer ...
 * Ltail:
 *   inc b
 *   ld a,b
 *   cp 9
 *   jp c,Lloop
 *
 * HL is the current board-cell pointer on every path reaching Ltail: the
 * occupied-cell path never changes it, and the recursive path restores it via
 * pass_minmax_save_board_addr.  Initialize HL once and walk it with inc hl.
 */
int pass_minmax_board_ptr_loop(void)
{
    int start, end, i, j;
    char loop_lab[128], tail_lab[128], got_lab[128];
    char cond[16];

    if (!peep_in_function_range("_MinMax:", &start, &end))
        return 0;
    if (peep_range_has_debug_annotations(start, end))
        return 0;

    for (i = start; i + 8 < end; i++) {
        if (!eq(i, "ld b,0")) continue;
        if (!label_name_at(i + 1, loop_lab)) continue;
        if (!eq(i + 2, "ld hl,_g_board")) continue;
        if (!eq(i + 3, "ld e,b")) continue;
        if (!eq(i + 4, "ld d,0")) continue;
        if (!eq(i + 5, "add hl,de")) continue;
        if (!eq(i + 6, "ld a,(hl)")) continue;
        if (!eq(i + 7, "or a")) continue;
        if (!peep_parse_any_cond_jump(lines[i + 8], cond, tail_lab)) continue;
        if (strcmp(cond, "nz") != 0) continue;

        for (j = i + 9; j + 4 < end; j++) {
            if (!label_name_at(j, got_lab) || strcmp(got_lab, tail_lab) != 0)
                continue;
            if (!eq(j + 1, "inc b")) continue;
            if (!eq(j + 2, "ld a,b")) continue;
            if (!eq(j + 3, "cp 9")) continue;
            if (!peep_parse_any_cond_jump(lines[j + 4], cond, got_lab)) continue;
            if (strcmp(cond, "c") != 0 || strcmp(got_lab, loop_lab) != 0) continue;

            insert_line_tagged(j + 1, "inc hl", "minmax_board_ptr_loop");
            insert_line_tagged(i + 1, "ld hl,_g_board", "minmax_board_ptr_loop");
            delete_n(i + 3, 4);
            return 1;
        }
    }

    return 0;
}

/*
 * pass_minmax_byte_returns:
 *
 * MinMax is declared as returning int, but every value it returns fits in a
 * byte (SCORE_WIN=6, SCORE_LOSE=4, SCORE_TIE=5, value=2..9).  Every caller
 * either discards the result (FindSolution) or reads only the low byte L:
 *
 *   ld e,l  ; peep: minmax_score_e
 *
 * so H is never read.  Within _MinMax, eliminate all "ld h,0" that exist
 * purely to zero-extend the return value, and shrink "ld hl,N; jp Lret" to
 * "ld l,N; jp Lret" for the constant-score returns (saves 3T each).
 *
 * The exit point is the label L immediately before "ld sp,ix; pop ix; ret".
 * All return paths "jp L" or fall through to L.
 */
int pass_minmax_byte_returns(void)
{
    int start, end, i, changed = 0;
    char exit_label[128];
    char tmp[MAX_LINE];
    int exit_label_line = -1;

    if (!peep_in_function_range("_MinMax:", &start, &end))
        return 0;
    if (peep_range_has_debug_annotations(start, end))
        return 0;

    /* Find the exit label: the last label before "ld sp,ix; pop ix; ret". */
    for (i = end - 1; i >= start; i--) {
        strip_peep_comment_copy(tmp, lines[i]);
        if (strcmp(tmp, "ld sp,ix") == 0 && i > start) {
            /* Walk back over pop ix (and any other trailing insns) to find label */
            int k = i - 1;
            while (k >= start) {
                strip_peep_comment_copy(tmp, lines[k]);
                if (starts_label(lines[k])) {
                    if (label_name_at(k, exit_label)) {
                        exit_label_line = k;
                    }
                    break;
                }
                k--;
            }
            break;
        }
    }
    if (exit_label_line < 0)
        return 0;

    /* 1. Remove "ld h,0" immediately followed by "jp {exit_label}". */
    for (i = start; i + 1 < end; i++) {
        if (!eq(i, "ld h,0"))
            continue;
        strip_peep_comment_copy(tmp, lines[i + 1]);
        {
            char lab[128];
            if (peep_parse_jp_uncond_label(tmp, lab) &&
                strcmp(lab, exit_label) == 0) {
                delete_n(i, 1);
                end--;
                replace1_tagged(i, lines[i], "minmax_byte_ret");
                changed = 1;
                if (i > start) i--;
            }
        }
    }

    /* 2. Remove "ld h,0" that immediately precedes the exit label itself
     *    (the fall-through path at end of loop). */
    for (i = start; i + 1 < end; i++) {
        if (!eq(i, "ld h,0"))
            continue;
        if (line_is_label_name(i + 1, exit_label)) {
            delete_n(i, 1);
            end--;
            exit_label_line--;
            changed = 1;
            if (i > start) i--;
        }
    }

    /* 3. Replace "ld hl,N; jp {exit_label}" with "ld l,N; jp {exit_label}"
     *    for constant score returns (N fits in a byte). */
    for (i = start; i + 1 < end; i++) {
        int imm;
        char lab[128];
        if (!peep_parse_ld_hl_0_to_255(lines[i], &imm))
            continue;
        strip_peep_comment_copy(tmp, lines[i + 1]);
        if (!peep_parse_jp_uncond_label(tmp, lab))
            continue;
        if (strcmp(lab, exit_label) != 0)
            continue;
        sprintf(tmp, "ld l,%d", imm);
        replace1_tagged(i, tmp, "minmax_byte_ret");
        changed = 1;
    }

    return changed;
}

/* Helper: replace first occurrence of 'from' in 'buf' with 'to' (may differ in length). */
static void pack_str_replace(char *buf, const char *from, const char *to)
{
    char *p = strstr(buf, from);
    if (!p) return;
    size_t fl = strlen(from), tl = strlen(to);
    memmove(p + tl, p + fl, strlen(p + fl) + 1);
    memcpy(p, to, tl);
}

/* pass_minmax_pack_frame: Phase 1 only.
 * Translate ix+6→ix+5 (beta), ix+8→ix+6 (depth), ix+10→ix+7 (move)
 * within _MinMax to prepare for the packed 2-word calling convention.
 * Fires only while (ix+10) still exists.
 *
 * This guard alone is NOT sufficient to fire safely: it says nothing
 * about whether pass_minmax_pack_call can actually collapse either call
 * site (the recursive self-call, or FindSolution's call into _MinMax) to
 * match the new packed offsets this pass is about to commit the frame
 * to. If this pass translates the frame while pass_minmax_pack_call
 * can't complete both call sites, the callee ends up reading parameters
 * at the new packed offsets while a caller still pushes the old unpacked
 * layout - a real caller/callee ABI mismatch (every parameter read
 * inside MinMax then comes from the wrong stack slot). Confirmed via a
 * corrupted tests/ttt.c run (radically wrong move counts) caused by an
 * unrelated, individually-correct codegen change that merely inserted
 * one harmless instruction between the recursive call and its cleanup -
 * enough to break pass_minmax_pack_call's exact-adjacency match while
 * this pass's own looser guard still fired.
 *
 * Rather than duplicating pass_minmax_pack_call's shape-matching logic
 * here (which would just create a second place to keep in sync - the
 * same mistake that let this happen in the first place), this pass
 * applies its translation, then immediately tries pass_minmax_pack_call
 * for real. If that fails to change anything, the translation is
 * reverted line-for-line and this pass declines entirely, so the two
 * always commit or decline together. */
int pass_minmax_pack_frame(void)
{
    int start, end, i, changed = 0;
    char newline[MAX_LINE];
    PeepEditTransaction transaction;

    if (!peep_in_function_range("_MinMax:", &start, &end))
        return 0;
    if (peep_range_has_debug_annotations(start, end))
        return 0;

    /* Guard: only run while (ix+10) references still exist. */
    {
        int has_ix10 = 0;
        for (i = start; i < end; i++)
            if (strstr(lines[i], "(ix+10)")) { has_ix10 = 1; break; }
        if (!has_ix10) return 0;
    }

    peep_edit_begin(&transaction);

    /* Process in order: ix+6→ix+5 first (old beta), then ix+8→ix+6 (depth),
     * then ix+10→ix+7 (move).  Ordering prevents double-translation. */
    for (i = start; i < end; i++) {
        int any = 0;
        strcpy(newline, lines[i]);

        if (strstr(newline, "(ix+6)")) {
            pack_str_replace(newline, "(ix+6)", "(ix+5)"); any = 1;
        }
        if (strstr(newline, "(ix+8)")) {
            pack_str_replace(newline, "(ix+8)", "(ix+6)"); any = 1;
        }
        if (strstr(newline, "(ix+10)")) {
            pack_str_replace(newline, "(ix+10)", "(ix+7)"); any = 1;
        }
        if (any) { replace1(i, newline); changed = 1; }
    }

    if (changed && !pass_minmax_pack_call()) {
        peep_edit_rollback(&transaction);
        changed = 0;
    } else {
        peep_edit_commit(&transaction);
    }

    return changed;
}

/* pass_minmax_pack_call: Phase 2 + Phase 3.
 * Transforms the recursive self-call from 4 separate 16-bit pushes to 2
 * packed word pushes, and updates FindSolution's call similarly.
 * Fires only after pass_minmax_pack_frame has run (ix+10 gone, ix+5 present). */
int pass_minmax_pack_call(void)
{
    int start, end, i, changed = 0;
    char newline[MAX_LINE];

    if (!peep_in_function_range("_MinMax:", &start, &end))
        return 0;
    if (peep_range_has_debug_annotations(start, end))
        return 0;

    /* Guard: only run after frame translation (ix+10 gone, ix+5 present). */
    {
        int has_ix10 = 0, has_ix5 = 0;
        for (i = start; i < end; i++) {
            if (strstr(lines[i], "(ix+10)")) { has_ix10 = 1; break; }
            if (strstr(lines[i], "(ix+5)"))   has_ix5 = 1;
        }
        if (has_ix10 || !has_ix5) return 0;
    }

    /* ---- Phase 2: transform the recursive call inside _MinMax ----
     *
     * Pattern (after phase 1 has updated offsets):
     *   push hl               ; board-addr save (from pass_minmax_save_board_addr)
     *   push bc               ; {B=p, C=value} save (from pass_minmax_value_c)
     *   ld l,b                ; L = p (move arg)
     *   ld h,0
     *   push hl               ; push {move, 0}
     *   ld a,(ix+6)           ; depth  (ix+8 → ix+6 after phase 1)
     *   add a,1
     *   ld l,a
     *   push hl               ; push {depth+1, 0}
     *   ld l,(ix+5)           ; beta   (ix+6 → ix+5 after phase 1)
     *   push hl               ; push {beta, 0}
     *   ld l,(ix+4)           ; alpha
     *   push hl               ; push {alpha, 0}
     *   call _MinMax
     *   pop bc (×4)
     *
     * Replaced with:
     *   push hl
     *   push bc
     *   ld a,(ix+6)           ; depth
     *   inc a                 ; depth+1 (was add a,1)
     *   ld c,a                ; C = depth+1
     *   push bc               ; packed {B=p=move, C=depth+1}
     *   ld h,(ix+5)           ; H = beta
     *   ld l,(ix+4)           ; L = alpha
     *   push hl               ; packed {alpha, beta}
     *   call _MinMax
     *   pop af                ; clear {alpha, beta}
     *   pop af                ; clear {move, depth+1}
     */
    for (i = start; i + 14 < end; i++) {
        int j, npopcnt;

        if (!eq(i,     "push hl"))        continue;
        if (!eq(i + 1, "push bc"))        continue;
        if (!eq(i + 2, "ld l,b"))         continue;
        if (!eq(i + 3, "ld h,0"))         continue;
        if (!eq(i + 4, "push hl"))        continue;
        if (!eq(i + 5, "ld a,(ix+6)"))    continue; /* depth */
        if (!eq(i + 6, "add a,1"))        continue;
        if (!eq(i + 7, "ld l,a"))         continue;
        if (!eq(i + 8, "push hl"))        continue;
        if (!eq(i + 9, "ld l,(ix+5)"))    continue; /* beta */
        if (!eq(i + 10, "push hl"))       continue;
        if (!eq(i + 11, "ld l,(ix+4)"))   continue; /* alpha */
        if (!eq(i + 12, "push hl"))       continue;
        if (!eq(i + 13, "call _MinMax"))  continue;
        j = i + 14;
        npopcnt = 0;
        while (j < end && eq(j, "pop bc")) { j++; npopcnt++; }
        if (npopcnt != 4) continue;

        /* Transform in-place (9 lines → shrinks by 3: remove 3 old lines, no insertion) */
        replace1_tagged(i,      "push hl",         "pack_args");
        replace1(i + 1,         "push bc");
        replace1(i + 2,         "ld a,(ix+6)");    /* depth */
        replace1(i + 3,         "inc a");           /* depth+1 (was add a,1 + ld l,a) */
        replace1(i + 4,         "ld c,a");
        replace1(i + 5,         "push bc");         /* packed {p=move, depth+1} */
        replace1(i + 6,         "ld h,(ix+5)");     /* beta */
        replace1(i + 7,         "ld l,(ix+4)");     /* alpha */
        replace1(i + 8,         "push hl");         /* packed {alpha, beta} */
        replace1(i + 9,         "call _MinMax");
        replace1(i + 10,        "pop af");          /* clear {alpha,beta} */
        replace1(i + 11,        "pop af");          /* clear {move,depth+1} */
        delete_n(i + 12, j - (i + 12));            /* remove extra pop bc lines */
        changed = 1;
    }

    /* Same recursive-call packing after the newer byte+constant code path.
     * The unsigned-long promotion fix makes depth+1 appear as:
     *
     *   ld l,(ix+6)
     *   ld h,0
     *   ld de,1
     *   add hl,de
     *
     * or, after the generic +1 peephole:
     *
     *   ld l,(ix+6)
     *   inc hl
     *
     * instead of the older ld a,(ix+6)/add a,1/ld l,a shape.  If this
     * pattern is not packed, _MinMax's frame has already been translated to
     * packed byte arguments, but the recursive call still pushes four words;
     * then beta/depth/move are read from the wrong stack bytes.
     */
    for (i = start; i + 13 < end; i++) {
        int j, npopcnt;
        int depth_shape;

        if (!eq(i,     "push hl"))        continue;
        if (!eq(i + 1, "push bc"))        continue;
        if (!eq(i + 2, "ld l,b"))         continue;
        if (!eq(i + 3, "ld h,0"))         continue;
        if (!eq(i + 4, "push hl"))        continue;

        depth_shape = 0;
        if (eq(i + 5, "ld l,(ix+6)") &&
            eq(i + 6, "inc hl") &&
            eq(i + 7, "push hl") &&
            eq(i + 8, "ld l,(ix+5)") &&
            eq(i + 9, "ld h,0") &&
            eq(i + 10, "push hl") &&
            eq(i + 11, "ld l,(ix+4)") &&
            eq(i + 12, "push hl") &&
            eq(i + 13, "call _MinMax")) {
            depth_shape = 1;
            j = i + 14;
        } else if (i + 15 < end &&
            eq(i + 5, "ld l,(ix+6)") &&
            eq(i + 6, "ld h,0") &&
            eq(i + 7, "ld de,1") &&
            eq(i + 8, "add hl,de") &&
            eq(i + 9, "push hl") &&
            eq(i + 10, "ld l,(ix+5)") &&
            eq(i + 11, "ld h,0") &&
            eq(i + 12, "push hl") &&
            eq(i + 13, "ld l,(ix+4)") &&
            eq(i + 14, "push hl") &&
            eq(i + 15, "call _MinMax")) {
            depth_shape = 2;
            j = i + 16;
        } else {
            continue;
        }

        npopcnt = 0;
        while (j < end && eq(j, "pop bc")) { j++; npopcnt++; }
        if (npopcnt != 4) continue;

        replace1_tagged(i,      "push hl",         "pack_args");
        replace1(i + 1,         "push bc");
        replace1(i + 2,         "ld a,(ix+6)");    /* depth */
        replace1(i + 3,         "inc a");           /* depth+1 */
        replace1(i + 4,         "ld c,a");
        replace1(i + 5,         "push bc");         /* packed {p=move, depth+1} */
        replace1(i + 6,         "ld h,(ix+5)");     /* beta */
        replace1(i + 7,         "ld l,(ix+4)");     /* alpha */
        replace1(i + 8,         "push hl");         /* packed {alpha, beta} */
        replace1(i + 9,         "call _MinMax");
        replace1(i + 10,        "pop af");
        replace1(i + 11,        "pop af");
        delete_n(i + 12, j - (i + 12));
        changed = 1;
        (void)depth_shape;
    }

    /* ---- Phase 3: transform FindSolution's call to _MinMax ----
     *
     * Pattern:
     *   ld l,(ix+4)           ; position (move)
     *   ld h,0
     *   push hl               ; push {move, 0}
     *   ld hl,0               ; depth = 0
     *   push hl
     *   ld hl,9               ; beta = SCORE_MAX
     *   push hl
     *   ld hl,2               ; alpha = SCORE_MIN
     *   push hl
     *   call _MinMax
     *   pop bc (×4)
     *
     * Replaced with:
     *   ld h,9                ; beta = SCORE_MAX
     *   ld l,2                ; alpha = SCORE_MIN
     *   push hl               ; packed {alpha=2, beta=9}
     *   ld b,(ix+4)           ; B = move = position
     *   ld c,0                ; C = depth = 0
     *   push bc               ; packed {move, depth}
     *   call _MinMax
     *   pop af                ; clear {alpha, beta}
     *   pop af                ; clear {move, depth}
     */
    {
        int fs_start, fs_end;
        if (peep_in_function_range("_FindSolution:", &fs_start, &fs_end) &&
            !peep_range_has_debug_annotations(fs_start, fs_end) &&
            !bc_regalloc_claimed_before(fs_end)) {
            for (i = fs_start; i + 12 < fs_end; i++) {
                int j, npopcnt;
                char off[32];

                if (!peep_parse_ld_l_ix(lines[i], off)) continue;
                if (!eq(i + 1, "ld h,0"))         continue;
                if (!eq(i + 2, "push hl"))         continue;
                if (!eq(i + 3, "ld hl,0"))         continue;
                if (!eq(i + 4, "push hl"))         continue;
                if (!eq(i + 5, "ld hl,9"))         continue;
                if (!eq(i + 6, "push hl"))         continue;
                if (!eq(i + 7, "ld hl,2"))         continue;
                if (!eq(i + 8, "push hl"))         continue;
                if (!eq(i + 9, "call _MinMax"))    continue;
                j = i + 10;
                npopcnt = 0;
                while (j < fs_end && eq(j, "pop bc")) { j++; npopcnt++; }
                if (npopcnt != 4) continue;

                /* Transform: push {move,depth} FIRST, {alpha,beta} LAST
                 * so callee sees ix+4=alpha, ix+5=beta, ix+6=depth, ix+7=move */
                sprintf(newline, "ld b,(ix%s)", off);
                replace1_tagged(i,     newline,             "pack_args_fs");  /* B=move */
                replace1(i + 1,        "ld c,0");                              /* C=depth */
                replace1(i + 2,        "push bc");           /* {move,depth} → ix+6,ix+7 */
                replace1(i + 3,        "ld h,9");            /* H=beta */
                replace1(i + 4,        "ld l,2");            /* L=alpha */
                replace1(i + 5,        "push hl");           /* {alpha,beta} → ix+4,ix+5 */
                replace1(i + 6,        "call _MinMax");
                replace1(i + 7,        "pop af");
                replace1(i + 8,        "pop af");
                delete_n(i + 9, j - (i + 9));
                changed = 1;
            }
        }
    }

    return changed;
}

/*
 * pass_minmax_save_board_addr:
 *
 * In MinMax's blank-cell loop, &g_board[p] is computed twice: once before
 * storing pieceMove and once after the recursive call to restore the cell.
 * The address is in HL right after the first store, but HL is immediately
 * clobbered by the arg setup for the recursive call.
 *
 * Before:
 *   ld (hl),a                    ; g_board[p] = pieceMove  — HL = &g_board[p]
 *   ld l,(ix-K)                  ; arg setup clobbers HL
 *   ... (push 4 args)
 *   call _MinMax
 *   pop bc (×N)
 *   ld e,l                       ; save score (E, not B — B is loop counter)
 *   ld hl,_g_board               ; recompute &g_board[p]
 *   ld e,(ix-K)
 *   ld d,0
 *   add hl,de
 *   ld (hl),0                    ; g_board[p] = 0
 *
 * After:
 *   ld (hl),a
 *   push hl                      ; save address before arg clobber
 *   ld l,(ix-K)
 *   ... (push 4 args)
 *   call _MinMax
 *   pop bc (×N)
 *   ld b,l
 *   pop hl                       ; restore address — replaces 4-insn recompute
 *   ld (hl),0
 *
 * Saves 47T (recompute) − 21T (push hl + pop hl) = 26T per blank cell visited.
 */
int pass_minmax_save_board_addr(void)
{
    int start, end, i, j, changed = 0;
    int K, k2, npopcnt;
    char addr[128], tmp[MAX_LINE];

    if (!peep_in_function_range("_MinMax:", &start, &end))
        return 0;
    if (peep_range_has_debug_annotations(start, end))
        return 0;

    for (i = start; i + 12 < end; i++) {
        /* ld (hl),a — store pieceMove; HL = &g_board[p] */
        if (!eq(i, "ld (hl),a")) continue;

        /* Next must be ld l,(ix-K) — arg setup about to clobber HL */
        if (!stride_parse_ld_r_ix_neg(lines[i + 1], 'l', &K)) continue;

        /* Scan forward for call _MinMax (within 20 lines) */
        for (j = i + 2; j < end && j < i + 20; j++)
            if (eq(j, "call _MinMax")) break;
        if (!eq(j, "call _MinMax")) continue;
        j++;

        /* Count consecutive pop bc */
        npopcnt = 0;
        while (j < end && eq(j, "pop bc")) { j++; npopcnt++; }
        if (npopcnt == 0) continue;

        /* ld e,l — score save (possibly tagged; E used by pass_minmax_score_e) */
        strip_peep_comment_copy(tmp, lines[j]);
        if (strcmp(tmp, "ld e,l") != 0) continue;
        j++;

        /* Recompute block: ld hl,_g_board; ld e,(ix-K); ld d,0; add hl,de */
        if (!parse_ld_hl_imm(lines[j], addr, sizeof(addr))) continue;
        if (strcmp(addr, "_g_board") != 0)                   continue;
        j++;
        if (!stride_parse_ld_r_ix_neg(lines[j], 'e', &k2))  continue;
        if (k2 != K)                                         continue;
        j++;
        if (!eq(j, "ld d,0"))                               continue;
        j++;
        if (!eq(j, "add hl,de"))                            continue;
        j++;

        /* ld (hl),0 — restore board cell */
        if (!eq(j, "ld (hl),0")) continue;

        /* Pattern matched. Transform:
         * - delete the 4-line recompute (at j-4 .. j-1)
         * - insert pop hl before ld (hl),0
         * - insert push hl after ld (hl),a (at i+1)
         * Apply end-to-start to keep earlier indices valid. */
        delete_n(j - 4, 4);
        insert_line_tagged(j - 4, "pop hl", "minmax_save_board_addr");
        insert_line(i + 1, "push hl");
        changed = 1;
    }

    return changed;
}

int pass_reuse_board_addr_for_zero_store(void)
{
    int i;
    int changed;
    char lab[128];

    changed = 0;

    for (i = 0; i + 13 < nlines; ++i) {
        if (eq(i, "ld hl,_g_board") &&
            eq(i + 1, "ld e,(ix-3)") &&
            eq(i + 2, "ld d,0") &&
            eq(i + 3, "add hl,de") &&
            eq(i + 4, "ld a,(hl)") &&
            (eq(i + 5, "or a") || eq(i + 5, "cp 0")) &&
            peep_parse_jp_cond_label(lines[i + 6], "nz", lab) &&
            eq(i + 7, "ld hl,_g_board") &&
            eq(i + 8, "ld e,(ix-3)") &&
            eq(i + 9, "ld d,0") &&
            eq(i + 10, "add hl,de")) {
            delete_n(i + 7, 4);
            changed = 1;
            if (i > 0) --i;
        }
    }

    return changed;
}

/*
 * pass_minmax_elim_label_reload:
 *
 * Eliminate a redundant "ld a,r" that appears immediately after a label
 * when A already holds the value of r from before the conditional jump
 * that targets that label.
 *
 * Pattern:
 *   ld a,r          ; A = r
 *   [cp/jp seq]
 *   jp cond, Lx     ; A unchanged on taken path
 *   [don't care]
 *   Lx:
 *   ld a,r          ; ← redundant: A is still r on the taken path
 *
 * This fires on the MinMax score-vs-SCORE_WIN check:
 *   ld a,e
 *   cp 6
 *   jp nz, L221
 *   ld hl,6
 *   jp L202
 *   L221:
 *   ld a,e          ; ← eliminated (A=E from before jp nz)
 */
int pass_minmax_elim_label_reload(void)
{
    int i, j, k, changed = 0;
    char tmp[MAX_LINE], tmp2[MAX_LINE], cond[16], lab[128];
    char r;

    for (i = 0; i + 1 < nlines; i++) {
        strip_peep_comment_copy(tmp, lines[i]);
        if (strncmp(tmp, "ld a,", 5) != 0)
            continue;
        r = tmp[5];
        if (tmp[6] != 0)
            continue;
        if (r != 'b' && r != 'c' && r != 'd' &&
            r != 'e' && r != 'h' && r != 'l')
            continue;

        /* Scan forward through transparent instructions for a conditional jump */
        for (j = i + 1; j < nlines && j < i + 8; j++) {
            strip_peep_comment_copy(tmp2, lines[j]);

            /* Found a conditional jp — check its target label */
            if (peep_parse_any_cond_jump(tmp2, cond, lab)) {
                /* Search for the label within a reasonable window */
                for (k = j + 1; k < nlines && k < j + 25; k++) {
                    if (!line_is_label_name(k, lab))
                        continue;
                    /* Skip any consecutive labels */
                    while (k + 1 < nlines && starts_label(lines[k + 1]))
                        k++;
                    /* If the next instruction after the label is ld a,r (same r) */
                    if (k + 1 < nlines) {
                        strip_peep_comment_copy(tmp2, lines[k + 1]);
                        if (strcmp(tmp2, tmp) == 0) {
                            delete_n(k + 1, 1);
                            changed = 1;
                        }
                    }
                    break;
                }
                break;
            }

            /* Transparent: cp, or a */
            if (strncmp(tmp2, "cp ", 3) == 0)
                continue;
            if (strcmp(tmp2, "or a") == 0)
                continue;
            break;
        }
    }

    return changed;
}

/*
 * pass_winner_check_dec_a:
 *
 * After the blank-cell test in MinMax's winner check, the piece identity
 * test is: cp 1; jp nz, L_lose.  Since A holds the winner value (1 or 2)
 * and is not live after the branch on either path, "dec a" is equivalent
 * to "cp 1" for the NZ test but costs 4T instead of 7T (saves 3T).
 *
 * Detects (after pass_elim_redundant_ld_a_reg removes the intervening reload):
 *
 *   or a               ; Z if blank — A unchanged
 *   jp z, L_blank
 *   cp 1               ; ← replaced by dec a
 *   jp nz, L_lose
 *   ld hl,N            ; A not live on fall-through: safe to clobber with dec
 */
int pass_winner_check_dec_a(void)
{
    int i, changed = 0;
    char tmp[MAX_LINE], lab[128];

    for (i = 0; i + 4 < nlines; i++) {
        strip_peep_comment_copy(tmp, lines[i]);
        if (strcmp(tmp, "or a") != 0) continue;

        if (!parse_jp_z_label(lines[i + 1], lab)) continue;

        strip_peep_comment_copy(tmp, lines[i + 2]);
        if (strcmp(tmp, "cp 1") != 0) continue;

        if (!parse_jp_nz_label(lines[i + 3], lab)) continue;

        /* ld hl,N or ld l,N immediately follows — confirms A is dead on fall-through */
        {
            char t4[MAX_LINE];
            strip_peep_comment_copy(t4, lines[i + 4]);
            if (!parse_ld_hl_imm(lines[i + 4], lab, sizeof(lab)) &&
                (strncmp(t4, "ld l,", 5) != 0))
                continue;
        }

        replace1_tagged(i + 2, "dec a", "winner_dec_a");
        changed = 1;
    }

    return changed;
}

int pass_global_board_const_offsets(void)
{
    int i;
    int changed;
    int incs;
    int k;
    int imm;
    char line[160];

    changed = 0;

    for (i = 0; i < nlines; ++i) {
        /*
         * Collapse constant-index global board addressing:
         *
         *     ld hl,_g_board
         *     inc hl
         *     inc hl
         *     cp (hl)
         *
         * into:
         *
         *     ld hl,_g_board+2
         *     cp (hl)
         *
         * and:
         *
         *     ld hl,_g_board
         *     ld de,5
         *     add hl,de
         *
         * into:
         *
         *     ld hl,_g_board+5
         *
         * This is safe because it only changes address formation; HL still
         * contains the same address before the following memory operation.
         */
        if (eq(i, "ld hl,_g_board")) {
            incs = 0;
            k = i + 1;
            while (k < nlines && eq(k, "inc hl")) {
                ++incs;
                ++k;
            }

            if (incs > 0) {
                sprintf(line, "ld hl,_g_board+%d", incs);
                replace1_tagged(i, line, "global_const_offset");
                delete_n(i + 1, incs);
                changed = 1;
                if (i > 0)
                    --i;
                continue;
            }

            if (i + 2 < nlines &&
                peep_parse_ld_de_0_to_255(lines[i + 1], &imm) &&
                eq(i + 2, "add hl,de")) {
                if (imm == 0)
                    sprintf(line, "ld hl,_g_board");
                else
                    sprintf(line, "ld hl,_g_board+%d", imm);
                replace1_tagged(i, line, "global_const_offset");
                delete_n(i + 1, 2);
                changed = 1;
                if (i > 0)
                    --i;
                continue;
            }
        }

        /*
         * Same collapse, mirrored operand order - the constant index
         * loaded into HL first, the board base into DE second:
         *
         *     ld hl,6
         *     ld de,_g_board
         *     add hl,de
         *
         * into:
         *
         *     ld hl,_g_board+6
         *
         * "add hl,de" is commutative (hl+de == de+hl), so this forms the
         * identical address; only which operand the front-end happened to
         * evaluate first differs. This is the shape a constant array
         * index typically compiles to (index in HL, base loaded after and
         * added) - the base-first form above is comparatively rare, so
         * every occurrence of this mirrored form was previously left as
         * the full 3-instruction computation instead of the one-line
         * direct-address form.
         */
        if (peep_parse_ld_hl_0_to_255(lines[i], &imm) &&
            i + 2 < nlines &&
            eq(i + 1, "ld de,_g_board") &&
            eq(i + 2, "add hl,de")) {
            if (imm == 0)
                sprintf(line, "ld hl,_g_board");
            else
                sprintf(line, "ld hl,_g_board+%d", imm);
            replace1_tagged(i, line, "global_const_offset");
            delete_n(i + 1, 2);
            changed = 1;
            if (i > 0)
                --i;
            continue;
        }
    }

    return changed;
}
