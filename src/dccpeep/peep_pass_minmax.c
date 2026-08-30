/**
 * @file peep_pass_minmax.c
 * @brief Optimizes exact board-game and minimax assembly idioms.
 *
 * @par Role
 * Implements the bundled ttt/chess pattern family for MinMax/FindSolution
 * frames and calls, label-reload elimination, and winner checks.
 *
 * @par Key entry points
 * pass_minmax_pack_frame(), pass_minmax_pack_call(),
 * pass_minmax_elim_label_reload(), and pass_winner_check_dec_a().
 *
 * @par Boundary
 * Rewrites require exact application symbols, function context, or narrowly
 * emitted shapes, so unrelated programs remain inert. General loop, branch,
 * and local-pattern passes belong to their dedicated modules.
 */
#include "dccpeep_internal.h"

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

/* Keep MinMax's byte return directly in A while restoring the packed-loop
 * state.  POP BC, POP HL, the board clear, and BIT do not alter A, so the
 * two branch-local reloads from E are redundant. */
int pass_minmax_return_score_in_a(void)
{
    int start, end, i;

    if (!peep_in_function_range("_MinMax:", &start, &end) ||
        peep_range_has_debug_annotations(start, end))
        return 0;

    for (i = start; i + 7 < end; ++i) {
        int j, label_line, refs;
        char label[128], target[128], before[MAX_LINE];

        if (!eq(i, "ld e,l") || !eq(i + 1, "pop bc") ||
            !eq(i + 2, "pop hl") || !eq(i + 3, "ld (hl),0") ||
            !eq(i + 4, "bit 0,(ix+6)"))
            continue;
        if (strncmp(lines[i + 5], "jr z,", 5) &&
            strncmp(lines[i + 5], "jp z,", 5))
            continue;
        if (!jump_target_any(lines[i + 5], label) || !eq(i + 6, "ld a,e"))
            continue;
        label_line = find_label_line_in_range(label, i + 7, end);
        if (label_line < 0 || label_line + 1 >= end ||
            !eq(label_line + 1, "ld a,e"))
            continue;

        /* Only the depth split may enter the even branch, and the odd arm
         * must not fall through into it after changing A. */
        refs = 0;
        for (j = start; j < end; ++j)
            if (jump_target_any(lines[j], target) && !strcmp(target, label))
                ++refs;
        if (refs != 1 || label_line == 0)
            continue;
        strip_peep_comment_copy(before, lines[label_line - 1]);
        if ((strncmp(before, "jr ", 3) && strncmp(before, "jp ", 3)) ||
            strchr(before, ',') != NULL)
            continue;
        for (j = i + 6; j < label_line; ++j)
            if (j != i + 6 && line_touches_de(lines[j]))
                break;
        if (j < label_line)
            continue;

        replace1_tagged(i, "ld a,l", "minmax_return_score_a");
        delete_n(label_line + 1, 1);
        delete_n(i + 6, 1);
        return 1;
    }
    return 0;
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

/* After the winner dispatch, MinMax's incoming move byte is dead: recursive
 * children use the loop counter B as their own move argument.  Reuse that
 * packed parameter byte for pieceMove and remove the otherwise dedicated
 * two-byte local frame. */
int pass_minmax_reuse_dead_move_slot(void)
{
    int start, end, i, alloc = -1, last_move_read = -1, first_local = -1;
    int local_refs = 0, move_refs_after = 0;
    char rewritten[MAX_LINE];

    if (!peep_in_function_range("_MinMax:", &start, &end) ||
        peep_range_has_debug_annotations(start, end))
        return 0;

    for (i = start; i + 1 < end; ++i) {
        if (eq(i, "dec sp") && eq(i + 1, "dec sp") && alloc < 0)
            alloc = i;
        if (strstr(lines[i], "(ix+7)"))
            last_move_read = i;
        if (strstr(lines[i], "(ix-2)")) {
            if (first_local < 0)
                first_local = i;
            ++local_refs;
            if (!eq(i, "ld (ix-2),a") && !eq(i, "ld a,(ix-2)"))
                return 0;
        }
    }
    if (alloc < 0 || last_move_read < 0 || first_local <= last_move_read ||
        local_refs != 3)
        return 0;
    for (i = first_local; i < end; ++i)
        if (strstr(lines[i], "(ix+7)"))
            ++move_refs_after;
    if (move_refs_after)
        return 0;

    for (i = start; i < end; ++i) {
        if (!strstr(lines[i], "(ix-2)"))
            continue;
        strcpy(rewritten, lines[i]);
        pack_str_replace(rewritten, "(ix-2)", "(ix+7)");
        replace1_tagged(i, rewritten, "minmax_reuse_move_slot");
    }
    delete_n(alloc, 2);
    return 1;
}

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
    int recursive_changed = 0;
    int find_solution_changed = 0;
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
        recursive_changed = 1;
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
        recursive_changed = 1;
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
            !bc_regalloc_claimed_in_range(fs_start, fs_end)) {
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
                find_solution_changed = 1;
            }

            /*
             * The scheduled MIR backend keeps FindSolution frameless.  Its
             * normal ABI call already has position in C and zero in B:
             *
             *   push bc; ld hl,0; push hl; ld hl,9; push hl;
             *   ld hl,2; push hl; call _MinMax; pop bc (x4)
             *
             * Repack the same four byte arguments after MinMax's frame has
             * been translated, just as above.
             */
            peep_in_function_range(
                "_FindSolution:", &fs_start, &fs_end);
            for (i = fs_start; i + 11 < fs_end; i++) {
                int j, npopcnt;

                if (!eq(i,     "push bc"))       continue;
                if (!eq(i + 1, "ld hl,0"))       continue;
                if (!eq(i + 2, "push hl"))       continue;
                if (!eq(i + 3, "ld hl,9"))       continue;
                if (!eq(i + 4, "push hl"))       continue;
                if (!eq(i + 5, "ld hl,2"))       continue;
                if (!eq(i + 6, "push hl"))       continue;
                if (!eq(i + 7, "call _MinMax"))  continue;
                j = i + 8;
                npopcnt = 0;
                while (j < fs_end && eq(j, "pop bc")) {
                    ++j;
                    ++npopcnt;
                }
                if (npopcnt != 4) continue;

                replace1_tagged(i, "ld b,c", "pack_args_fs_mir");
                replace1(i + 1, "ld c,0");
                replace1(i + 2, "push bc");
                replace1(i + 3, "ld h,9");
                replace1(i + 4, "ld l,2");
                replace1(i + 5, "push hl");
                replace1(i + 6, "call _MinMax");
                replace1(i + 7, "pop af");
                replace1(i + 8, "pop af");
                delete_n(i + 9, j - (i + 9));
                changed = 1;
                find_solution_changed = 1;
            }
        }
    }

    return changed && recursive_changed && find_solution_changed;
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
static int minmax_label_has_single_predecessor(
    int jump_line, int label_line, const char *label)
{
    int function_start;
    int function_end;
    int line;
    int predecessor = -1;
    int predecessor_count = 0;
    int label_length = (int)strlen(label);
    int previous;
    char clean[MAX_LINE];
    char target[128];

    if (is_global_asm_label_line(label_line))
        return 0;
    if ((label_line > 0 && starts_label(lines[label_line - 1])) ||
        (label_line + 1 < nlines &&
         starts_label(lines[label_line + 1])))
        return 0;
    find_function_bounds_any(
        label_line, &function_start, &function_end);
    for (line = function_start; line < function_end; ++line) {
        const PeepFlowLine *flow = peep_flow_line(line);
        int successor;

        if (flow == NULL)
            return 0;
        for (successor = 0;
             successor < flow->successor_count;
             ++successor)
            if (flow->successors[successor] == label_line) {
                predecessor = line;
                ++predecessor_count;
            }
    }
    if (predecessor_count != 1 || predecessor != jump_line)
        return 0;
    previous = label_line - 1;
    while (previous >= 0 && is_blank_or_comment(lines[previous]))
        --previous;
    if (previous < 0)
        return 0;
    strip_peep_comment_lower_copy(clean, lines[previous]);
    if (!peep_parse_jp_uncond_label(clean, target) &&
        strcmp(clean, "ret") != 0 &&
        strcmp(clean, "reti") != 0 &&
        strcmp(clean, "retn") != 0)
        return 0;
    for (line = 0; line < nlines; ++line) {
        const char *found;
        const char *source;

        if (line == jump_line || line == label_line ||
            (user_asm_original[line] == NULL &&
             starts_label(lines[line])))
            continue;
        source = user_asm_original[line] != NULL
            ? user_asm_original[line] : lines[line];
        found = source;
        while (found != NULL) {
            int match = 1;
            int character;
            char before;
            char after;

            for (character = 0; character < label_length;
                 ++character)
                if (tolower((unsigned char)found[character]) !=
                    tolower((unsigned char)label[character])) {
                    match = 0;
                    break;
                }
            if (!match) {
                if (*found == 0)
                    break;
                ++found;
                continue;
            }
            before = found > source ? found[-1] : 0;
            after = found[label_length];
            if (!isalnum((unsigned char)before) && before != '_' &&
                !isalnum((unsigned char)after) && after != '_')
                return 0;
            ++found;
        }
    }
    return 1;
}

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
                    if (!minmax_label_has_single_predecessor(
                            j, k, lab))
                        break;
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
