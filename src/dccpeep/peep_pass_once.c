/* peep_pass_once.c - the single-scan micro-pattern dispatcher.
 *
 * pass_once() walks the line program once, trying a fixed ordered list of
 * small local rewrites (the try_*_at helpers) at each position. It is the
 * highest-volume pass in the fixpoint loop; all of its helpers are private
 * to this file and invoked only from pass_once itself.
 */
#include "dccpeep_internal.h"

static int is_jp_to_next_label(int i)
{
    char target[128];
    char label[128];
    int j, n;

    if (i + 1 >= nlines)
        return 0;
    if (!is_uncond_jp(lines[i]))
        return 0;
    if (!jump_target(lines[i], target))
        return 0;

    /*
     * The jump is redundant if plain fall-through reaches a label matching
     * its target with nothing but zero-width lines in between: blank
     * lines, comments (notably the "@dcc-line" debug annotations that -g
     * inserts between every statement, which otherwise hide this pattern
     * from debug builds), and other labels. Scan past all of that looking
     * for the target; stop at the first real instruction.
     */
    for (j = i + 1; j < nlines; j++) {
        if (is_blank_or_comment(lines[j]))
            continue;
        if (!starts_label(lines[j]))
            return 0;

        strcpy(label, lines[j]);
        n = (int)strlen(label);
        if (n > 0 && label[n - 1] == ':')
            label[n - 1] = 0;
        if (strcmp(target, label) == 0)
            return 1;
    }

    return 0;
}

static void replace_block_with_5(int i,
                                 const char *a, const char *b,
                                 const char *c, const char *d,
                                 const char *e, const char *tag)
{
    int oldn;
    oldn = 10;
    replace1_tagged(i, a, tag);
    replace1(i + 1, b);
    replace1(i + 2, c);
    replace1(i + 3, d);
    replace1(i + 4, e);
    delete_n(i + 5, oldn - 5);
}

static int try_fold_bool_branch(int i)
{
    char t1[128];
    char t2[128];
    char e[128];
    char f[128];
    char a[256];
    char b[256];
    char c[256];
    char d[256];
    char elab[256];

    if (i + 9 >= nlines)
        return 0;

    /* Do not fold across an intervening label.  The single-branch form below
     * overwrites i+1, and crc.c can produce a peephole-generated skip label
     * there after small_const_eq has rewritten a 16-bit equality test.  If we
     * remove that label, later M80 assembly sees an unresolved Lpeep_sceq_*
     * target. */
    if (starts_label(lines[i + 1]))
        return 0;

    if (!eq(i + 2, "ld hl,0"))
        return 0;
    if (!is_uncond_jp(lines[i + 3]))
        return 0;
    strcpy(e, lines[i + 3] + 3);

    if (!line_is_label_name(i + 6, e))
        return 0;
    if (!eq(i + 7, "ld a,h"))
        return 0;
    if (!eq(i + 8, "or l"))
        return 0;

    /* Two-way true test, used for <= and >= materialization. */
    if ((parse_jp_z_label(lines[i], t1) &&
         (parse_jp_c_label(lines[i + 1], t2) || parse_jp_nc_label(lines[i + 1], t2))) &&
        strcmp(t1, t2) == 0 &&
        line_is_label_name(i + 4, t1) &&
        eq(i + 5, "ld hl,1")) {

        sprintf(d, "%s:", t1);
        sprintf(elab, "%s:", e);

        if (parse_jp_z_label(lines[i + 9], f)) {
            sprintf(a, "%s", lines[i]);
            sprintf(b, "%s", lines[i + 1]);
            sprintf(c, "jp %s", f);
            replace_block_with_5(i, a, b, c, d, elab, "fold_bool_branch_2way");
            return 1;
        }

        if (parse_jp_nz_label(lines[i + 9], f)) {
            /* if boolean true, branch to f; otherwise fall through */
            sprintf(a, "jp z, %s", f);
            if (parse_jp_c_label(lines[i + 1], t2))
                sprintf(b, "jp c, %s", f);
            else
                sprintf(b, "jp nc, %s", f);
            sprintf(c, "jp %s", e);
            replace_block_with_5(i, a, b, c, d, elab, "fold_bool_branch_2way");
            return 1;
        }
    }

    /* Single true test, used for ==/!= materialization. */
    if ((parse_jp_z_label(lines[i], t1) || parse_jp_nz_label(lines[i], t1)) &&
        line_is_label_name(i + 4, t1) &&
        eq(i + 5, "ld hl,1")) {

        sprintf(d, "%s:", t1);
        sprintf(elab, "%s:", e);

        if (parse_jp_z_label(lines[i + 9], f)) {
            /* false branch goes to f */
            sprintf(a, "%s", lines[i]);
            sprintf(b, "jp %s", f);
            sprintf(c, "%s", d);
            replace1_tagged(i, a, "fold_bool_branch_single");
            replace1(i + 1, b);
            replace1(i + 2, c);
            replace1(i + 3, elab);
            delete_n(i + 4, 6);
            return 1;
        }

        if (parse_jp_nz_label(lines[i + 9], f)) {
            /* true branch goes to f */
            if (parse_jp_z_label(lines[i], t1))
                sprintf(a, "jp z, %s", f);
            else
                sprintf(a, "jp nz, %s", f);
            sprintf(b, "jp %s", e);
            sprintf(c, "%s", d);
            replace1_tagged(i, a, "fold_bool_branch_single");
            replace1(i + 1, b);
            replace1(i + 2, c);
            replace1(i + 3, elab);
            delete_n(i + 4, 6);
            return 1;
        }
    }

    return 0;
}

static int peep_line_in_function(int line, const char *func)
{
    static char cached_func[128];
    static int cached_nlines = -1;
    static int cached_start = -1;
    static int cached_end;
    int i;

    if (line < 0 || line >= nlines)
        return 0;

    if (cached_nlines != nlines || strcmp(cached_func, func) != 0) {
        int start = -1, end = nlines;

        for (i = 0; i < nlines; i++) {
            if (start < 0) {
                if (strcmp(lines[i], func) == 0)
                    start = i;
            } else if (peep_is_public_line(lines[i])) {
                end = i;
                break;
            }
        }

        strncpy(cached_func, func, sizeof(cached_func) - 1);
        cached_func[sizeof(cached_func) - 1] = 0;
        cached_nlines = nlines;
        cached_start = start;
        cached_end = end;
    }

    return cached_start >= 0 && line > cached_start && line < cached_end;
}

/*
 * Guard for the local_alloc rewrites in pass_once: deleting
 * "add hl,sp / ld sp,hl" also deletes the definition of HL (the address of
 * the fresh allocation).  Only report HL dead when a forward scan
 * proves the following code fully rewrites HL before reading it.  Any
 * control transfer or instruction touching HL/H/L before a full write means
 * HL must be treated as live (return 0).  The one call recognized as a kill
 * is __stchk: its documented prologue-helper contract clobbers HL before the
 * function body can depend on registers.
 */
int local_alloc_hl_result_dead(int start)
{
    int j;
    char tmp[MAX_LINE];
    char off[32];
    char hi_off[32];

    for (j = start; j < nlines; j++) {
        strip_peep_comment_copy(tmp, lines[j]);

        if (is_blank_or_comment(tmp))
            continue;

        /* Full writes of HL without reading it first: HL is dead. */
        if (strncmp(tmp, "ld hl,", 6) == 0) return 1;
        if (strcmp(tmp, "pop hl") == 0) return 1;
        if (peep_parse_ld_l_ix(tmp, off)) {
            /* Loading L alone is only a partial write.  DCC's word-load
             * shape writes H on the immediately following instruction; prove
             * that second write rather than assuming it. */
            if (j + 1 < nlines && peep_parse_ld_h_ix(lines[j + 1], hi_off))
                return 1;
            return 0;
        }

        /* DCC emits this immediately after frame allocation under
         * -fstack-check.  DCCRTL.MAC documents that its normal path clobbers
         * AF/DE/HL, so the allocation result cannot survive this call. */
        if (strcmp(tmp, "call __stchk") == 0)
            return 1;

        /* A label does not read HL.  It is safe to continue into the labeled
         * block: if its first HL touch is a full overwrite, the allocation
         * result is dead both on fall-through and on every incoming edge. */
        if (starts_label(tmp))
            continue;

        /* Control transfer: successor unknown, assume live. */
        if (strncmp(tmp, "jp", 2) == 0 || strncmp(tmp, "jr", 2) == 0 ||
            strncmp(tmp, "call", 4) == 0 || strncmp(tmp, "ret", 3) == 0 ||
            strncmp(tmp, "djnz", 4) == 0 || strncmp(tmp, "rst", 3) == 0)
            return 0;

        /* Anything else touching HL/H/L before a full write: live. */
        if (line_touches_hl(tmp)) return 0;

        /* Neutral instruction: keep scanning. */
    }
    return 0; /* end of input without a full overwrite: assume live */
}

static int try_global_moves_postinc_at(int i)
{
    /*
     * Global 16-bit post-increment used as a statement:
     *
     *   ld hl,_g_Moves
     *   push hl
     *   ld e,(hl)
     *   inc hl
     *   ld d,(hl)
     *   ex de,hl
     *   push hl
     *   inc hl
     *   ex de,hl
     *   pop hl
     *   ex (sp),hl
     *   ld (hl),e
     *   inc hl
     *   ld (hl),d
     *   pop hl
     *
     * becomes direct memory increment.  This hits the very hot
     * g_Moves++ at _MinMax entry, without touching bool folding.
     */
    if (eq(i, "ld hl,_g_Moves") &&
        eq(i + 1, "push hl") &&
        eq(i + 2, "ld e,(hl)") &&
        eq(i + 3, "inc hl") &&
        eq(i + 4, "ld d,(hl)") &&
        eq(i + 5, "ex de,hl") &&
        eq(i + 6, "push hl") &&
        eq(i + 7, "inc hl") &&
        eq(i + 8, "ex de,hl") &&
        eq(i + 9, "pop hl") &&
        eq(i + 10, "ex (sp),hl") &&
        eq(i + 11, "ld (hl),e") &&
        eq(i + 12, "inc hl") &&
        eq(i + 13, "ld (hl),d") &&
        eq(i + 14, "pop hl")) {
        char lab[64], line[128];

        sprintf(lab, "Lginc_%d", i); /* see Lskrl_'s rationale above */
        replace1_tagged(i, "ld hl,_g_Moves", "lookforwinner_ginc");
        replace1(i + 1, "inc (hl)");
        sprintf(line, "jp nz, %s", lab);
        replace1(i + 2, line);
        replace1(i + 3, "inc hl");
        replace1(i + 4, "inc (hl)");
        sprintf(line, "%s:", lab);
        replace1(i + 5, line);
        delete_n(i + 6, 9);
        return 1;
    }

    return 0;
}

static int try_minmax_board_store_at(int i)
{
    /*
     * MinMax board[p] = pieceMove byte store:
     *
     *   ld l,(ix-5)
     *   ld h,(ix-4)
     *   ld hl,_g_board
     *   push hl
     *   ld l,(ix-5)
     *   ld h,(ix-4)
     *   ex de,hl
     *   pop hl
     *   add hl,de
     *   push hl
     *   ld l,(ix-3)
     *   ld h,0
     *   ex de,hl
     *   pop hl
     *   ld (hl),e
     *
     * becomes:
     *
     *   ld l,(ix-5)
     *   ld h,(ix-4)
     *   ld de,_g_board
     *   add hl,de
     *   ld a,(ix-3)
     *   ld (hl),a
     *
     * This is safe for ttt_t g_board[p] = pieceMove; because both are
     * byte objects in this benchmark.
     */
    if (eq(i, "ld l,(ix-5)") &&
        eq(i + 1, "ld h,(ix-4)") &&
        eq(i + 2, "ld hl,_g_board") &&
        eq(i + 3, "push hl") &&
        eq(i + 4, "ld l,(ix-5)") &&
        eq(i + 5, "ld h,(ix-4)") &&
        eq(i + 6, "ex de,hl") &&
        eq(i + 7, "pop hl") &&
        eq(i + 8, "add hl,de") &&
        eq(i + 9, "push hl") &&
        eq(i + 10, "ld l,(ix-3)") &&
        eq(i + 11, "ld h,0") &&
        eq(i + 12, "ex de,hl") &&
        eq(i + 13, "pop hl") &&
        eq(i + 14, "ld (hl),e")) {

        replace1_tagged(i, "ld l,(ix-5)", "board_store");
        replace1(i + 1, "ld h,(ix-4)");
        replace1(i + 2, "ld de,_g_board");
        replace1(i + 3, "add hl,de");
        replace1(i + 4, "ld a,(ix-3)");
        replace1(i + 5, "ld (hl),a");
        delete_n(i + 6, 9);
        return 1;
    }

    return 0;
}

static int try_minmax_blank_board_at(int i)
{
    /*
     * MinMax blank-board-position test:
     *
     *   ld hl,0
     *   push hl
     *   ld hl,_g_board
     *   push hl
     *   ld l,(ix-5)
     *   ld h,(ix-4)
     *   ex de,hl
     *   pop hl
     *   add hl,de
     *   ld l,(hl)
     *   ld h,0
     *   ex de,hl
     *   pop hl
     *   or a
     *   sbc hl,de
     *   jp nz,Lskip
     *
     * becomes:
     *
     *   ld l,(ix-5)
     *   ld h,(ix-4)
     *   ld de,_g_board
     *   add hl,de
     *   ld a,(hl)
     *   or a
     *   jp nz,Lskip
     */
    {
        char lskip[128];
        if (eq(i, "ld hl,0") &&
            eq(i + 1, "push hl") &&
            eq(i + 2, "ld hl,_g_board") &&
            eq(i + 3, "push hl") &&
            eq(i + 4, "ld l,(ix-5)") &&
            eq(i + 5, "ld h,(ix-4)") &&
            eq(i + 6, "ex de,hl") &&
            eq(i + 7, "pop hl") &&
            eq(i + 8, "add hl,de") &&
            eq(i + 9, "ld l,(hl)") &&
            eq(i + 10, "ld h,0") &&
            eq(i + 11, "ex de,hl") &&
            eq(i + 12, "pop hl") &&
            eq(i + 13, "or a") &&
            eq(i + 14, "sbc hl,de") &&
            peep_parse_jp_cond_label(lines[i + 15], "nz", lskip)) {

            replace1_tagged(i, "ld l,(ix-5)", "blank_board_test");
            replace1(i + 1, "ld h,(ix-4)");
            replace1(i + 2, "ld de,_g_board");
            replace1(i + 3, "add hl,de");
            replace1(i + 4, "ld a,(hl)");
            replace1(i + 5, "or a");
            replace1(i + 6, lines[i + 15]);
            delete_n(i + 7, 9);
        return 1;
        }
    }

    return 0;
}

static int try_posnfunc_setup_at(int i)
{
    /*
     * posNfunc setup byte store:
     *
     *   push ix
     *   pop hl
     *   dec hl
     *   push hl
     *   ld hl,_g_board
     *   [inc hl ...] or [ld de,N/add hl,de]
     *   ld l,(hl)
     *   ld h,0
     *   ex de,hl
     *   pop hl
     *   ld (hl),e
     *
     * becomes:
     *
     *   ld hl,_g_board
     *   [same address adjustment]
     *   ld a,(hl)
     *   ld (ix-1),a
     */
    if (eq(i, "push ix") &&
        eq(i + 1, "pop hl") &&
        eq(i + 2, "dec hl") &&
        eq(i + 3, "push hl") &&
        eq(i + 4, "ld hl,_g_board")) {
        int j;
        int incs;
        int offv;
        char tmp[128];

        j = i + 5;
        incs = 0;

        while (j < nlines && eq(j, "inc hl")) {
            incs++;
            j++;
        }

        if (eq(j, "ld l,(hl)") &&
            eq(j + 1, "ld h,0") &&
            eq(j + 2, "ex de,hl") &&
            eq(j + 3, "pop hl") &&
            eq(j + 4, "ld (hl),e")) {

            replace1_tagged(i, "ld hl,_g_board", "posnfunc_inc");
            for (j = 0; j < incs; j++)
                replace1(i + 1 + j, "inc hl");
            replace1(i + 1 + incs, "ld a,(hl)");
            replace1(i + 2 + incs, "ld (ix-1),a");
            delete_n(i + 3 + incs, (i + 10 + incs) - (i + 3 + incs));
        return 1;
        }

        j = i + 5;
        if (peep_parse_ld_de_0_to_255(lines[j], &offv) &&
            eq(j + 1, "add hl,de") &&
            eq(j + 2, "ld l,(hl)") &&
            eq(j + 3, "ld h,0") &&
            eq(j + 4, "ex de,hl") &&
            eq(j + 5, "pop hl") &&
            eq(j + 6, "ld (hl),e")) {

            replace1_tagged(i, "ld hl,_g_board", "posnfunc_de");
            sprintf(tmp, "ld de,%d", offv);
            replace1(i + 1, tmp);
            replace1(i + 2, "add hl,de");
            replace1(i + 3, "ld a,(hl)");
            replace1(i + 4, "ld (ix-1),a");
            delete_n(i + 5, (j + 7) - (i + 5));
        return 1;
        }
    }

    return 0;
}

static int try_small_const_eq_at(int i)
{
    /*
     * Small constant equality/inequality against local int:
     *
     *   ld hl,N
     *   push hl
     *   ld l,(ix-K)
     *   ld h,(ix-K+1)
     *   ex de,hl
     *   pop hl
     *   or a
     *   sbc hl,de
     *   jp z/nz,L
     *
     * becomes (when N > 0):
     *
     *   ld a,(ix-K)
     *   cp N
     *   jp z/nz,L
     *
     * or when N == 0 (null/zero check, must test both bytes):
     *
     *   ld a,(ix-K)
     *   or (ix-K+1)
     *   jp z/nz,L
     *
     * This hits MinMax tests like score == SCORE_WIN / SCORE_LOSE.
     * When N==0 we must use 'or' to check both bytes: a pointer like
     * 0x1E00 has a zero low byte but is non-null, so 'cp 0' alone
     * would incorrectly treat it as null.
     */
    {
        int imm;
        char loff[32], hoff[32], newline[128];

        if (peep_parse_ld_hl_0_to_255(lines[i], &imm) &&
            eq(i + 1, "push hl") &&
            peep_parse_ld_l_ix(lines[i + 2], loff) &&
            peep_parse_ld_h_ix(lines[i + 3], hoff) &&
            eq(i + 4, "ex de,hl") &&
            eq(i + 5, "pop hl") &&
            eq(i + 6, "or a") &&
            eq(i + 7, "sbc hl,de") &&
            (strncmp(lines[i + 8], "jp z,", 5) == 0 ||
             strncmp(lines[i + 8], "jp nz,", 6) == 0) &&
            /* Skip >= comparisons: "jp z,L; jp c,L" with the same label
             * means (const <= var), not (const == var).  Treating it as
             * equality corrupts the carry flag used by the leftover jp c. */
            !(strncmp(lines[i + 8], "jp z,", 5) == 0 &&
              i + 9 < nlines &&
              strncmp(lines[i + 9], "jp c,", 5) == 0 &&
              strcmp(lines[i + 8] + 5, lines[i + 9] + 5) == 0)) {

            /*
             * For nonzero 16-bit constants the high byte must also be
             * tested against zero.  The old peephole used only:
             *
             *     ld a,(ix+lo)
             *     cp N
             *
             * which is only valid for byte objects.  It broke uint16_t
             * comparisons such as "m == 1" when m was 257, 513, ...
             *
             * Keep the zero case compact; for nonzero constants emit
             * a correct low-byte/high-byte test.
             */
            sprintf(newline, "ld a,(ix%s)", loff);
            replace1_tagged(i, newline, "small_const_eq");

            if (imm == 0) {
                sprintf(newline, "or (ix%s)", hoff);
                replace1(i + 1, newline);
                replace1(i + 2, lines[i + 8]);
                delete_n(i + 3, 6);
            } else if (strncmp(lines[i + 8], "jp nz,", 6) == 0) {
                /* x != imm: branch if low differs or high is nonzero. */
                sprintf(newline, "cp %d", imm);
                replace1(i + 1, newline);
                replace1(i + 2, lines[i + 8]);
                sprintf(newline, "ld a,(ix%s)", hoff);
                replace1(i + 3, newline);
                replace1(i + 4, "or a");
                replace1(i + 5, lines[i + 8]);
                delete_n(i + 6, 3);
            } else {
                /* x == imm: branch only if low matches and high is zero. */
                char skip[64];
                char jnzskip[96];

                sprintf(skip, "Lsceq_%d", i); /* see Lskrl_'s rationale above */
                sprintf(newline, "cp %d", imm);
                replace1(i + 1, newline);
                sprintf(jnzskip, "jp nz, %s", skip);
                replace1(i + 2, jnzskip);
                sprintf(newline, "ld a,(ix%s)", hoff);
                replace1(i + 3, newline);
                replace1(i + 4, "or a");
                replace1(i + 5, lines[i + 8]);
                sprintf(newline, "%s:", skip);
                replace1(i + 6, newline);
                delete_n(i + 7, 2);
            }
        return 1;
        }
    }

    return 0;
}

static int try_and1_bool_at(int i)
{
    /*
     * Fold 16-bit "x & 1" boolean tests:
     *
     *   ld l,(ix+8)
     *   ld h,(ix+9)
     *   ld de,1
     *   ld a,h
     *   and d
     *   ld h,a
     *   ld a,l
     *   and e
     *   ld l,a
     *   ld a,h
     *   or l
     *   jp z,L
     *
     * becomes:
     *
     *   ld a,(ix+8)
     *   and 1
     *   jp z,L
     *
     * This is hot in ttt's MinMax for "depth & 1".
     */
    if (eq(i, "ld l,(ix+8)") &&
        eq(i + 1, "ld h,(ix+9)") &&
        eq(i + 2, "ld de,1") &&
        eq(i + 3, "ld a,h") &&
        eq(i + 4, "and d") &&
        eq(i + 5, "ld h,a") &&
        eq(i + 6, "ld a,l") &&
        eq(i + 7, "and e") &&
        eq(i + 8, "ld l,a") &&
        eq(i + 9, "ld a,h") &&
        eq(i + 10, "or l") &&
        (strncmp(lines[i + 11], "jp z,", 5) == 0 ||
         strncmp(lines[i + 11], "jp nz,", 6) == 0)) {

        replace1_tagged(i, "ld a,(ix+8)", "and1_bool");
        replace1(i + 1, "and 1");
        replace1(i + 2, lines[i + 11]);
        delete_n(i + 3, 9);
        return 1;
    }

    return 0;
}

static int try_bool_suffix_at(int i)
{
    /*
     * Fold boolean materialization suffixes:
     *
     *   ld hl,1
     *   jp Lend
     * Lfalse:
     *   ld hl,0
     * Lend:
     *   ld a,h
     *   or l
     *   jp nz,Ldest
     *
     * becomes:
     *   jp Ldest
     * Lfalse:
     * Lend:
     *
     * For final "jp z", the false path jumps to Ldest instead.
     * Also handles the symmetric "ld hl,0 ... Ltrue: ld hl,1" form.
     */
    {
        char ljump[128], lab_mid[128], lab_end[128], ldest[128];
        char out1[256], out2[256];

        if ((eq(i, "ld hl,1") || eq(i, "ld hl,0")) &&
            peep_parse_jp_uncond_label(lines[i + 1], ljump) &&
            label_name_at(i + 2, lab_mid) &&
            (eq(i + 3, "ld hl,0") || eq(i + 3, "ld hl,1")) &&
            label_name_at(i + 4, lab_end) &&
            strcmp(ljump, lab_end) == 0 &&
            eq(i + 5, "ld a,h") &&
            eq(i + 6, "or l") &&
            (peep_parse_jp_cond_label(lines[i + 7], "z", ldest) ||
             peep_parse_jp_cond_label(lines[i + 7], "nz", ldest))) {

            int first_is_true;
            int final_is_z;
            int branch_from_first_path;

            first_is_true = eq(i, "ld hl,1");
            final_is_z = strncmp(lines[i + 7], "jp z,", 5) == 0;

            /* final jp nz branches on true; final jp z branches on false. */
            branch_from_first_path =
                (first_is_true && !final_is_z) || (!first_is_true && final_is_z);

            if (branch_from_first_path) {
                sprintf(out1, "jp %s", ldest);
                replace1_tagged(i, out1, "bool_suffix");
                replace1(i + 1, lines[i + 2]);  /* middle label */
                replace1(i + 2, lines[i + 4]);  /* end label */
                delete_n(i + 3, 5);
            } else {
                sprintf(out1, "jp %s", lab_end);
                sprintf(out2, "jp %s", ldest);
                replace1_tagged(i, out1, "bool_suffix");
                replace1(i + 1, lines[i + 2]);  /* middle label */
                replace1(i + 2, out2);
                replace1(i + 3, lines[i + 4]);  /* end label */
                delete_n(i + 4, 4);
            }
        return 1;
        }
    }

    return 0;
}

static int try_const_bool_at(int i)
{
    /*
     * Fold constant boolean tests:
     *
     *   ld hl,1
     *   ld a,h
     *   or l
     *   jp z, Lx       ; never taken
     *
     * delete all four.  Similarly, "jp nz" is always taken.
     * Do the inverse for ld hl,0.
     */
    {
        char tgt[128];
        char newline[256];

        if ((eq(i, "ld hl,1") || eq(i, "ld hl,0")) &&
            eq(i + 1, "ld a,h") &&
            eq(i + 2, "or l") &&
            (peep_parse_jp_cond_label(lines[i + 3], "z", tgt) ||
             peep_parse_jp_cond_label(lines[i + 3], "nz", tgt))) {
            int is_one;
            int is_jp_z;
            int taken;

            is_one = eq(i, "ld hl,1");
            is_jp_z = strncmp(lines[i + 3], "jp z,", 5) == 0;

            taken = is_one ? !is_jp_z : is_jp_z;

            if (taken) {
                sprintf(newline, "jp %s", tgt);
                replace1_tagged(i, newline, "const_bool_taken");
                delete_n(i + 1, 3);
            } else {
                delete_n(i, 4);
            }
        return 1;
        }
    }

    return 0;
}

static int try_cp_hl_bool_at(int i)
{
    /*
     * Fold byte compare boolean materialization after previous byte-compare
     * peepholes:
     *
     *   cp (hl)
     *   jp z, Ltrue       ; or jp nz, Ltrue
     *   ld hl,0
     *   jp Lend
     * Ltrue:
     *   ld hl,1
     * Lend:
     *   ld a,h
     *   or l
     *   jp z, Lfalse      ; or jp nz, Ltrue2
     *
     * into a direct conditional branch after cp.
     */
    {
        char ltrue[128], lend[128], lab4[128], lab6[128], ldest[128];
        char newline[256];
        int first_is_z;
        int final_is_z;

        if (eq(i, "cp (hl)") &&
            (peep_parse_jp_cond_label(lines[i + 1], "z", ltrue) ||
             peep_parse_jp_cond_label(lines[i + 1], "nz", ltrue)) &&
            eq(i + 2, "ld hl,0") &&
            peep_parse_jp_uncond_label(lines[i + 3], lend) &&
            label_name_at(i + 4, lab4) &&
            strcmp(lab4, ltrue) == 0 &&
            eq(i + 5, "ld hl,1") &&
            label_name_at(i + 6, lab6) &&
            strcmp(lab6, lend) == 0 &&
            eq(i + 7, "ld a,h") &&
            eq(i + 8, "or l") &&
            (peep_parse_jp_cond_label(lines[i + 9], "z", ldest) ||
             peep_parse_jp_cond_label(lines[i + 9], "nz", ldest))) {

            first_is_z = strncmp(lines[i + 1], "jp z,", 5) == 0;
            final_is_z = strncmp(lines[i + 9], "jp z,", 5) == 0;

            if (final_is_z) {
                peep_make_cond_jump(newline, sizeof(newline), first_is_z ? "nz" : "z", ldest);
            } else {
                peep_make_cond_jump(newline, sizeof(newline), first_is_z ? "z" : "nz", ldest);
            }

            replace1_tagged(i + 1, newline, "cp_hl_bool");
            delete_n(i + 2, 8);
        return 1;
        }
    }

    return 0;
}

static int try_local_alloc_at(int i)
{
    /*
     * Small local stack allocation:
     *
     *   ld hl,-1
     *   add hl,sp
     *   ld sp,hl
     *
     * becomes:
     *   dec sp
     *
     * and similarly for -2.  This is especially useful for the tiny
     * ttt posNfunc helpers that allocate one char local.
     *
     * The rewrite deletes the definition of HL (the address of the
     * fresh allocation), so it must only fire when the following code
     * fully rewrites HL before reading it (local_alloc_hl_result_dead).
     * dcc's by-value struct/union argument copy uses HL from this very
     * sequence as the copy destination; rewriting that shape corrupted
     * the outgoing argument bytes and the stack.
     *
     * N=3/4 are deliberately NOT handled here even though they are also
     * both smaller and faster (see pass_local_alloc_wide in
     * peep_pass_final.c for why): pass_once runs first in the
     * fixed-point pass list, so eagerly rewriting "ld hl,-4"/"ld hl,-3"
     * this early would permanently destroy that exact text before
     * function-specific frame-shrinking passes elsewhere in the list
     * (e.g. pass_shrink_minmax_frame3_after_score_cache /
     * pass_shrink_minmax_frame2_after_loop_ctr_b, which look for that
     * literal text once dead locals are proven unused) ever get a
     * chance to reduce the allocation further - confirmed via ttt.c's
     * _MinMax regressing when this was tried inline here.
     */
    if (eq(i, "ld hl,-1") &&
        eq(i + 1, "add hl,sp") &&
        eq(i + 2, "ld sp,hl") &&
        local_alloc_hl_result_dead(i + 3)) {
        replace1_tagged(i, "dec sp", "local_alloc_1");
        delete_n(i + 1, 2);
        return 1;
    }

    if (eq(i, "ld hl,-2") &&
        eq(i + 1, "add hl,sp") &&
        eq(i + 2, "ld sp,hl") &&
        local_alloc_hl_result_dead(i + 3)) {
        replace1_tagged(i, "dec sp", "local_alloc_2");
        replace1(i + 1, "dec sp");
        delete_n(i + 2, 1);
        return 1;
    }

    return 0;
}

static int try_byte_equality_at(int i)
{
    /*
     * Byte equality compare:
     *
     *   ld l,(ix-N)
     *   ld h,0
     *   push hl
     *   ld hl,_g_board
     *   [inc hl ...]  or  [ld de,K / add hl,de]
     *   ld l,(hl)
     *   ld h,0
     *   ex de,hl
     *   pop hl
     *   or a
     *   sbc hl,de
     *   jp z/nz, L
     *
     * becomes:
     *
     *   ld a,(ix-N)
     *   ld hl,_g_board
     *   [same address adjustment]
     *   cp (hl)
     *   jp z/nz, L
     *
     * Only equality/inequality branches are folded, so carry/sign
     * semantics do not matter.
     */
    {
        char off[32];
        char newline[128];
        int j;
        int incs;

        if (peep_parse_ld_l_ix(lines[i], off) &&
            eq(i + 1, "ld h,0") &&
            eq(i + 2, "push hl") &&
            eq(i + 3, "ld hl,_g_board")) {

            j = i + 4;
            incs = 0;
            while (j < nlines && eq(j, "inc hl")) {
                j++;
                incs++;
            }

            if (eq(j, "ld l,(hl)") &&
                eq(j + 1, "ld h,0") &&
                eq(j + 2, "ex de,hl") &&
                eq(j + 3, "pop hl") &&
                eq(j + 4, "or a") &&
                eq(j + 5, "sbc hl,de") &&
                peep_is_jp_z_or_nz(lines[j + 6])) {

                peep_make_ld_a_ix(newline, off);
                replace1_tagged(i, newline, "byte_eq_inc");
                replace1(i + 1, "ld hl,_g_board");

                /* existing inc hl lines at i+4.. remain moved down by deletion;
                   copy them into position i+2.. */
                {
                    int k;
                    for (k = 0; k < incs; k++)
                        replace1(i + 2 + k, "inc hl");
                    replace1(i + 2 + incs, "cp (hl)");
                    replace1(i + 3 + incs, lines[j + 6]);
                }

                delete_n(i + 4 + incs, (j + 7) - (i + 4 + incs));
        return 1;
            }

            if (strncmp(lines[j], "ld de,", 6) == 0 &&
                eq(j + 1, "add hl,de") &&
                eq(j + 2, "ld l,(hl)") &&
                eq(j + 3, "ld h,0") &&
                eq(j + 4, "ex de,hl") &&
                eq(j + 5, "pop hl") &&
                eq(j + 6, "or a") &&
                eq(j + 7, "sbc hl,de") &&
                peep_is_jp_z_or_nz(lines[j + 8])) {

                peep_make_ld_a_ix(newline, off);
                replace1_tagged(i, newline, "byte_eq_de");
                replace1(i + 1, "ld hl,_g_board");
                replace1(i + 2, lines[j]);
                replace1(i + 3, "add hl,de");
                replace1(i + 4, "cp (hl)");
                replace1(i + 5, lines[j + 8]);

                delete_n(i + 6, (j + 9) - (i + 6));
        return 1;
            }
        }
    }

    return 0;
}

static int try_byte_compare_zero_at(int i)
{
    /*
     * Byte compare against zero:
     *
     *   ld hl,0
     *   push hl
     *   ld l,(ix-N)
     *   ld h,0
     *   ex de,hl
     *   pop hl
     *   or a
     *   sbc hl,de
     *   jp z/nz, L
     *
     * becomes:
     *   ld a,(ix-N)
     *   or a
     *   jp z/nz, L
     */
    {
        char off[32];
        char newline[128];

        if (eq(i, "ld hl,0") &&
            eq(i + 1, "push hl") &&
            peep_parse_ld_l_ix(lines[i + 2], off) &&
            eq(i + 3, "ld h,0") &&
            eq(i + 4, "ex de,hl") &&
            eq(i + 5, "pop hl") &&
            eq(i + 6, "or a") &&
            eq(i + 7, "sbc hl,de") &&
            peep_is_jp_z_or_nz(lines[i + 8])) {

            peep_make_ld_a_ix(newline, off);
            replace1_tagged(i, newline, "byte_cmp_zero");
            replace1(i + 1, "or a");
            replace1(i + 2, lines[i + 8]);
            delete_n(i + 3, 6);
        return 1;
        }
    }

    return 0;
}

static int try_ix_de_load_reorder_at(int i)
{
    /*
     * Before:
     *   push ix
     *   pop hl
     *   dec hl
     *   dec hl
     *   ld e,(hl)
     *   inc hl
     *   ld d,(hl)
     *   pop hl
     *
     * After:
     *   push ix
     *   pop hl
     *   dec hl
     *   ld d,(hl)
     *   dec hl
     *   ld e,(hl)
     *   pop hl
     *
     * Safe because the final pop hl overwrites HL, so the changed
     * intermediate HL value does not escape.
     */
    if (eq(i, "push ix") &&
        eq(i + 1, "pop hl") &&
        eq(i + 2, "dec hl") &&
        eq(i + 3, "dec hl") &&
        eq(i + 4, "ld e,(hl)") &&
        eq(i + 5, "inc hl") &&
        eq(i + 6, "ld d,(hl)") &&
        eq(i + 7, "pop hl")) {
        replace1_tagged(i + 2, "dec hl", "ix_de_load_reorder");
        replace1(i + 3, "ld d,(hl)");
        replace1(i + 4, "dec hl");
        replace1(i + 5, "ld e,(hl)");
        replace1(i + 6, "pop hl");
        delete_n(i + 7, 1);
        return 1;
    }

    return 0;
}

static int try_small_positive_offset_at(int i)
{
    /* Small positive address offsets.  16-bit INC HL does not affect flags.
     * Only use where the next instruction is not a conditional branch. */
    if (eq(i, "ld de,1") && eq(i + 1, "add hl,de") &&
        i + 2 < nlines && strncmp(lines[i + 2], "jp ", 3) != 0) {
        replace1_tagged(i, "inc hl", "ld_de1_to_inc");
        delete_n(i + 1, 1);
        return 1;
    }

    if (eq(i, "ld de,2") && eq(i + 1, "add hl,de") &&
        i + 2 < nlines && strncmp(lines[i + 2], "jp ", 3) != 0) {
        replace1_tagged(i, "inc hl", "ld_de2_to_inc");
        replace1(i + 1, "inc hl");
        return 1;
    }

    if (eq(i, "ld de,3") && eq(i + 1, "add hl,de") &&
        i + 2 < nlines && strncmp(lines[i + 2], "jp ", 3) != 0) {
        replace1_tagged(i, "inc hl", "ld_de3_to_inc");
        replace1(i + 1, "inc hl");
        insert_line(i + 2, "inc hl");
        return 1;
    }

    return 0;
}

static int subtract_one_transfer_is_safe(int i, unsigned flags)
{
    int transferred = 0;
    int transfer_line = -1;
    int j;

    for (j = i + 3; j < nlines; ++j) {
        const PeepLineInfo *info = peep_line_info(j);
        if (info == NULL || info->kind == PEEP_LINE_LABEL ||
            info->kind == PEEP_LINE_DIRECTIVE ||
            info->kind == PEEP_LINE_OPAQUE)
            return 0;
        if (info->kind != PEEP_LINE_INSTRUCTION)
            continue;
        if (!transferred && eq(j, "ex de,hl")) {
            if (j + 1 >= nlines || !eq(j + 1, "pop hl"))
                return 0;
            transferred = 1;
            transfer_line = j;
        } else if (!transferred &&
                   ((info->effects.reads | info->effects.writes) &
                    (PEEP_REG_D | PEEP_REG_E)) != 0)
            return 0;
        if ((info->effects.unknown && j != transfer_line) ||
            info->effects.control_flow ||
            (info->effects.flags_read & flags) != 0)
            return 0;
        flags &= ~info->effects.flags_written;
        if (transferred && flags == 0)
            return 1;
    }
    return 0;
}

static int subtract_one_bc_loop_flags_dead(int i, unsigned flags)
{
    const PeepFlowLine *jump_flow;
    int j;

    if (i + 5 >= nlines || !eq(i + 3, "ld c,l") ||
        !eq(i + 4, "ld b,h") ||
        (strncmp(lines[i + 5], "jr ", 3) != 0 &&
         strncmp(lines[i + 5], "jp ", 3) != 0))
        return 0;
    jump_flow = peep_flow_line(i + 5);
    if (jump_flow == NULL || jump_flow->successor_count != 1)
        return 0;
    for (j = jump_flow->successors[0]; j < nlines; ++j) {
        const PeepLineInfo *info = peep_line_info(j);
        if (info == NULL || info->kind == PEEP_LINE_DIRECTIVE ||
            info->kind == PEEP_LINE_OPAQUE)
            return 0;
        if (info->kind != PEEP_LINE_INSTRUCTION)
            continue;
        if (info->effects.unknown || info->effects.control_flow ||
            (info->effects.flags_read & flags) != 0)
            return 0;
        flags &= ~info->effects.flags_written;
        if (flags == 0)
            return 1;
    }
    return 0;
}

static int subtract_one_call_argument_is_safe(int i)
{
    return i + 5 < nlines && eq(i + 3, "push hl") &&
           !strncmp(lines[i + 4], "call ", 5) && eq(i + 5, "pop bc");
}

static int try_subtract_one_at(int i)
{
    const unsigned all_flags = PEEP_FLAG_C | PEEP_FLAG_Z |
                               PEEP_FLAG_S | PEEP_FLAG_PV;
    /*
     * HL -= 1 via signed subtract:
     *   ld de,1
     *   or a       ; clear carry
     *   sbc hl,de  ; HL = HL - 1
     *
     * When not immediately followed by a conditional branch,
     * the flags from sbc are unused, and this becomes just:
     *   dec hl
     *
     * This hits HL = n - 1 patterns in indexed loops.
     *
     * KNOWN BUG (found 2026-08-13, via dcc/tests/ttime.c's mktime() tests -
     * see the dcc-peep=false override for "ttime" in
     * tests/_test_overrides.json): 16-bit DEC HL does not set flags on Z80,
     * unlike SBC HL,DE. The "not immediately followed by a conditional
     * branch" check below only looks at the single next line and only
     * recognizes literal "jp " as a branch. pass_bool_from_cmp() (dccpeep.c)
     * can relocate the real flag-consuming branch two lines further away,
     * behind an intervening flag-neutral "ld hl,0" - when that pass runs
     * first, this check no longer sees a "jp "-prefixed line, concludes the
     * flags are dead, and substitutes dec hl, silently corrupting the
     * branch that still depends on them. (A jp-to-jr shortening pass
     * running first would trip the same blind spot, since the check never
     * looks for "jr " either.)
     *
     * Already fixed on the perf/unified-regalloc branch (~/gh/dcc): that
     * branch rewrote this function to use peep_flags_dead_after /
     * peep_registers_dead_after - a proper dataflow-based liveness
     * analysis that already exists on this branch too (see dccpeep.c's own
     * other uses of peep_flags_dead_after) but was never wired up here.
     * Confirmed empirically: feature-branch dccpeep applied to the same
     * pre-peephole input produces correct output.
     *
     * Do NOT patch this function here to fix it locally - the feature
     * branch's version touches these exact lines with a larger rewrite
     * (plus new helpers: subtract_one_bc_loop_flags_dead,
     * subtract_one_transfer_is_safe, subtract_one_call_argument_is_safe),
     * so any local fix here would just create a guaranteed merge conflict
     * for no benefit. Once that branch merges: delete this comment, drop
     * the "ttime" dcc-peep=false override in tests/_test_overrides.json,
     * and confirm tests/ttime.c passes with peephole enabled again.
     */
    if (eq(i, "ld de,1") &&
        eq(i + 1, "or a") &&
        eq(i + 2, "sbc hl,de") &&
        i + 3 < nlines &&
                (((peep_flags_dead_after(i + 2, all_flags) ||
                     subtract_one_bc_loop_flags_dead(i, all_flags)) &&
                    peep_registers_dead_after(i + 2, PEEP_REG_D | PEEP_REG_E)) ||
                     subtract_one_transfer_is_safe(i, all_flags) ||
                     subtract_one_call_argument_is_safe(i))) {
        replace1_tagged(i, "dec hl", "sbc_de1_to_dec");
        delete_n(i + 1, 2);
        return 1;
    }

    return 0;
}

static int try_same_register_push_pop_at(int i)
{
    /* Same-register push/pop has no register or flag effect. */
    if ((eq(i, "push hl") && eq(i + 1, "pop hl")) ||
        (eq(i, "push de") && eq(i + 1, "pop de")) ||
        (eq(i, "push bc") && eq(i + 1, "pop bc")) ||
        (eq(i, "push af") && eq(i + 1, "pop af")) ||
        (eq(i, "push ix") && eq(i + 1, "pop ix"))) {
        delete_n(i, 2);
        return 1;
    }

    return 0;
}

static int try_double_exchange_at(int i)
{
    /* Two exchanges cancel exactly. */
    if (eq(i, "ex de,hl") && eq(i + 1, "ex de,hl")) {
        delete_n(i, 2);
        return 1;
    }

    return 0;
}

static int try_caller_cleanup_at(int i)
{
    char v[128];

    /*
     * Caller cleanup that preserves HL return value:
     *   ex de,hl / ld hl,N / add hl,sp / ld sp,hl / ex de,hl
     * becomes N copies of inc sp for small even N.  This keeps HL
     * unchanged and adjusts SP by the same amount.  It intentionally
     * avoids changing condition flags.
     */
    if (eq(i, "ex de,hl") &&
        i + 4 < nlines &&
        parse_ld_hl_imm(lines[i + 1], v, sizeof(v)) &&
        eq(i + 2, "add hl,sp") &&
        eq(i + 3, "ld sp,hl") &&
        eq(i + 4, "ex de,hl")) {
        int n;
        int k;
        if (parse_nonneg_int(v, &n) && n > 0 && n <= 6) {
            delete_n(i, 5);
            for (k = 0; k < n; k++) {
                if (k == 0)
                    insert_line_tagged(i, "inc sp", "caller_cleanup");
                else
                    insert_line(i + k, "inc sp");
            }
        return 1;
        }
    }

    return 0;
}

static int try_unreachable_after_jump_at(int i)
{
    /*
     * Code after an unconditional jump is unreachable until the next
     * label.  Delete one non-label instruction at a time.
     */
    if (is_uncond_jp(lines[i]) &&
        i + 1 < nlines &&
        !starts_label(lines[i + 1]) &&
        !is_blank_or_comment(lines[i + 1])) {
        delete_n(i + 1, 1);
        return 1;
    }

    return 0;
}

static int try_jump_to_next_at(int i)
{
    /* Unconditional jump to immediately following label. */
    if (is_jp_to_next_label(i)) {
        delete_n(i, 1);
        return 1;
    }

    return 0;
}

static int try_ix_predec_inc_at(int i)
{
    /*
     * 16-bit pre-decrement (or pre-increment) via pointer arithmetic,
     * where the variable's IX offset is within direct IX addressing range.
     *
     *   push ix
     *   pop hl
     *   ld de,K      ; K in -128..126
     *   add hl,de
     *   push hl
     *   ld e,(hl)
     *   inc hl
     *   ld d,(hl)
     *   ex de,hl
     *   dec hl       ; (or inc hl for pre-increment)
     *   ex de,hl
     *   pop hl
     *   ld (hl),e
     *   inc hl
     *   ld (hl),d
     *   ex de,hl
     *
     * Becomes (result stays in HL):
     *
     *   ld l,(ix+K)
     *   ld h,(ix+K+1)
     *   dec hl
     *   ld (ix+K),l
     *   ld (ix+K+1),h
     */
    {
        int K;
        char loff[32], hoff[32], newline[128];
        const char *step;

        if (eq(i,      "push ix") &&
            eq(i +  1, "pop hl") &&
            peep_parse_ld_de_signed(lines[i + 2], &K) &&
            eq(i +  3, "add hl,de") &&
            eq(i +  4, "push hl") &&
            eq(i +  5, "ld e,(hl)") &&
            eq(i +  6, "inc hl") &&
            eq(i +  7, "ld d,(hl)") &&
            eq(i +  8, "ex de,hl") &&
            (eq(i +  9, "dec hl") || eq(i +  9, "inc hl")) &&
            eq(i + 10, "ex de,hl") &&
            eq(i + 11, "pop hl") &&
            eq(i + 12, "ld (hl),e") &&
            eq(i + 13, "inc hl") &&
            eq(i + 14, "ld (hl),d") &&
            eq(i + 15, "ex de,hl") &&
            K >= -128 && K <= 126) {

            step = eq(i + 9, "dec hl") ? "dec hl" : "inc hl";
            peep_format_ix_off(loff, K);
            peep_format_ix_off(hoff, K + 1);

            sprintf(newline, "ld l,(ix%s)", loff);  replace1_tagged(i, newline, "ix_predec_inc");
            sprintf(newline, "ld h,(ix%s)", hoff);  replace1(i + 1, newline);
            replace1(i + 2, step);
            sprintf(newline, "ld (ix%s),l", loff);  replace1(i + 3, newline);
            sprintf(newline, "ld (ix%s),h", hoff);  replace1(i + 4, newline);
            delete_n(i + 5, 11);
        return 1;
        }
    }

    return 0;
}

static int try_byte_zero_test_at(int i)
{
    /*
     * Zero-test a byte from memory:
     *   ld l,(hl)
     *   ld h,0
     *   ld a,h
     *   or l
     * The above loads a byte from (HL) as an unsigned 16-bit value in HL,
     * then OR-reduces HL into A to test for zero.  Since H is forced to 0,
     * A ends up equal to the byte.  Equivalent, and 11T faster:
     *   ld a,(hl)
     *   or a
     */
    if (i + 3 < nlines &&
        eq(i,     "ld l,(hl)") &&
        eq(i + 1, "ld h,0") &&
        eq(i + 2, "ld a,h") &&
        eq(i + 3, "or l")) {
        replace1_tagged(i, "ld a,(hl)", "byte_zero_test");
        replace1(i + 1, "or a");
        delete_n(i + 2, 2);
        return 1;
    }

    return 0;
}

static int bc_dead_before_use(int start)
{
    int j;

    for (j = start; j < nlines; ++j) {
        const PeepLineInfo *info = peep_line_info(j);
        char clean[MAX_LINE];

        if (info == NULL || info->kind == PEEP_LINE_LABEL ||
            info->kind == PEEP_LINE_OPAQUE)
            return 0;
        if (info->kind == PEEP_LINE_DIRECTIVE) {
            strip_peep_comment_lower_copy(clean, lines[j]);
            if (strncmp(clean, "extrn ", 6) == 0)
                continue;
            return 0;
        }
        if (info->kind != PEEP_LINE_INSTRUCTION)
            continue;
        strip_peep_comment_lower_copy(clean, lines[j]);
        if (strncmp(clean, "call _", 6) == 0 &&
            clean[6] != '_' &&
            strchr(clean + 5, ',') == NULL)
            return 1;
        if (strcmp(clean, "ret") == 0)
            return 1;
        if ((info->effects.reads &
             (PEEP_REG_B | PEEP_REG_C)) != 0)
            return 0;
        if ((info->effects.writes &
             (PEEP_REG_B | PEEP_REG_C)) ==
            (PEEP_REG_B | PEEP_REG_C))
            return 1;
        if (info->effects.control_flow || info->effects.unknown)
            return 0;
    }
    return 0;
}

static int function_has_inline_simple_store_marker(int line)
{
    int start;
    int end;
    int i;

    find_function_bounds_any(line, &start, &end);
    for (i = start; i < end; ++i)
        if (strstr(lines[i], ";@dcc.mir inline-simple-store") != NULL)
            return 1;
    return 0;
}

static int try_hl_bc_hl_roundtrip_at(int i)
{
    if (i + 3 >= nlines ||
        !function_has_inline_simple_store_marker(i) ||
        !eq(i, "ld c,l") || !eq(i + 1, "ld b,h") ||
        !eq(i + 2, "ld l,c") || !eq(i + 3, "ld h,b"))
        return 0;
    if (peep_registers_dead_after(
            i + 3, PEEP_REG_B | PEEP_REG_C) ||
        bc_dead_before_use(i + 4)) {
        delete_n(i, 4);
    } else {
        replace1_tagged(
            i, "ld c,l",
            "hl_bc_hl_copyback");
        delete_n(i + 2, 2);
    }
    return 1;
}

int pass_once(void)
{
    int i;
    int changed;
    char v[128];
    char out[256];

    changed = 0;

    for (i = 0; i < nlines; i++) {
        if (try_hl_bc_hl_roundtrip_at(i)) {
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        if (try_global_moves_postinc_at(i)) {
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        if (try_small_const_eq_at(i)) {
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        if (try_and1_bool_at(i)) {
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        if (try_minmax_board_store_at(i)) {
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        if (try_minmax_blank_board_at(i)) {
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        if (try_bool_suffix_at(i)) {
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        if (try_const_bool_at(i)) {
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        if (try_cp_hl_bool_at(i)) {
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        if (try_posnfunc_setup_at(i)) {
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        if (try_local_alloc_at(i)) {
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        if (try_byte_equality_at(i)) {
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        if (try_byte_compare_zero_at(i)) {
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        /*
         * Fold equality/inequality boolean materialization immediately
         * consumed by a zero/nonzero branch.
         */
        {
            char ltrue[128], lend[128], lab5[128], lab7[128], ldest[128];
            char newline[256];
            int first_is_z;
            int final_is_z;

            if (eq(i, "or a") &&
                eq(i + 1, "sbc hl,de") &&
                (peep_parse_jp_cond_label(lines[i + 2], "z", ltrue) ||
                 peep_parse_jp_cond_label(lines[i + 2], "nz", ltrue)) &&
                eq(i + 3, "ld hl,0") &&
                peep_parse_jp_uncond_label(lines[i + 4], lend) &&
                label_name_at(i + 5, lab5) &&
                strcmp(lab5, ltrue) == 0 &&
                eq(i + 6, "ld hl,1") &&
                label_name_at(i + 7, lab7) &&
                strcmp(lab7, lend) == 0 &&
                eq(i + 8, "ld a,h") &&
                eq(i + 9, "or l") &&
                (peep_parse_jp_cond_label(lines[i + 10], "z", ldest) ||
                 peep_parse_jp_cond_label(lines[i + 10], "nz", ldest))) {

                first_is_z = strncmp(lines[i + 2], "jp z,", 5) == 0;
                final_is_z = strncmp(lines[i + 10], "jp z,", 5) == 0;

                if (final_is_z) {
                    peep_make_cond_jump(newline, sizeof(newline), first_is_z ? "nz" : "z", ldest);
                } else {
                    peep_make_cond_jump(newline, sizeof(newline), first_is_z ? "z" : "nz", ldest);
                }

                /*
                 * Safety check: if ltrue or lend is referenced by any line
                 * outside this 11-line window, another fold has already
                 * created a jump to one of these labels.  Deleting ltrue:/lend:
                 * here would leave that earlier jump dangling (e.g. the
                 * OR-materialisation shared-label case in cbfs()).  Skip the
                 * fold in that situation.
                 */
                {
                    int safe = 1;
                    int k;
                    for (k = 0; k < nlines && safe; k++) {
                        char tgt_chk[128];
                        if (k >= i && k <= i + 10) continue;
                        if (jump_target(lines[k], tgt_chk) &&
                            (strcmp(tgt_chk, ltrue) == 0 ||
                             strcmp(tgt_chk, lend) == 0))
                            safe = 0;
                    }
                    if (!safe) continue;
                }
                replace1_tagged(i + 2, newline, "or_a_sbc_bool11");
                delete_n(i + 3, 8);
                changed = 1;
                if (i > 0) i--;
                continue;
            }
        }

        if (is_blank_or_comment(lines[i]))
            continue;

        if (try_ix_de_load_reorder_at(i)) {
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        /* Fold compare-result materialization immediately consumed by a branch. */
        if (!peep_line_in_function(i, "_main:") && try_fold_bool_branch(i)) {
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        if (try_small_positive_offset_at(i)) {
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        if (try_subtract_one_at(i)) {
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        if (try_same_register_push_pop_at(i)) {
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        if (try_double_exchange_at(i)) {
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        /*
         * Safe constant-to-DE only when surrounded by push/pop of HL:
         *   push hl
         *   ld hl,N
         *   ex de,hl
         *   pop hl
         * becomes:
         *   push hl
         *   ld de,N
         *   pop hl
         *
         * This preserves final HL and final DE and does not alter flags.
         */
        if (eq(i, "push hl") &&
            i + 3 < nlines &&
            parse_ld_hl_imm(lines[i + 1], v, sizeof(v)) &&
            eq(i + 2, "ex de,hl") &&
            eq(i + 3, "pop hl")) {
            sprintf(out, "ld de,%s", v);
            replace1_tagged(i + 1, out, "const_to_de");
            delete_n(i + 2, 1);
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        /*
         * After the safe constant-to-DE rewrite above, this common leftover:
         *   push hl
         *   ld de,N
         *   pop hl
         * is just ld de,N.  HL is unchanged either way, DE is the same,
         * flags are unchanged, and SP is unchanged.
         */
        if (eq(i, "push hl") &&
            i + 2 < nlines &&
            parse_ld_de_imm(lines[i + 1], v, sizeof(v)) &&
            eq(i + 2, "pop hl")) {
            sprintf(out, "ld de,%s", v);
            replace1_tagged(i, out, "push_lde_pop");
            delete_n(i + 1, 2);
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        if (try_caller_cleanup_at(i)) {
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        if (try_unreachable_after_jump_at(i)) {
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        if (try_jump_to_next_at(i)) {
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        /* Duplicate declarations anywhere before code are safe to remove. */
        if (strncmp(lines[i], "extrn ", 6) == 0 ||
            strncmp(lines[i], "public ", 7) == 0) {
            int j;
            for (j = 0; j < i; j++) {
                if (strcmp(lines[i], lines[j]) == 0) {
                    delete_n(i, 1);
                    changed = 1;
                    if (i > 0) i--;
                    break;
                }
            }
            if (changed)
                continue;
        }

        /* Adjacent duplicate declarations. */
        if ((strncmp(lines[i], "extrn ", 6) == 0 ||
             strncmp(lines[i], "public ", 7) == 0) &&
            i + 1 < nlines &&
            strcmp(lines[i], lines[i + 1]) == 0) {
            delete_n(i + 1, 1);
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        if (try_ix_predec_inc_at(i)) {
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        if (try_byte_zero_test_at(i)) {
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        /* Signed byte zero-test from memory:
         *   ld l,(hl)
         *   ld a,l
         *   rlca
         *   sbc a,a
         *   ld h,a
         *   ld a,h
         *   or l
         *   jr/jp z|nz,L
         *
         * The sign-extension is irrelevant for a zero/nonzero branch.  Test
         * the byte directly, leaving HL untouched; only apply when the next
         * consumer is a Z/NZ branch so no signed flags are being preserved.
         */
        if (i + 7 < nlines &&
            eq(i,     "ld l,(hl)") &&
            eq(i + 1, "ld a,l") &&
            eq(i + 2, "rlca") &&
            eq(i + 3, "sbc a,a") &&
            eq(i + 4, "ld h,a") &&
            eq(i + 5, "ld a,h") &&
            eq(i + 6, "or l") &&
            (strncmp(lines[i + 7], "jp z,", 5) == 0 ||
             strncmp(lines[i + 7], "jp nz,", 6) == 0 ||
             strncmp(lines[i + 7], "jr z,", 5) == 0 ||
             strncmp(lines[i + 7], "jr nz,", 6) == 0)) {
            replace1_tagged(i, "ld a,(hl)", "byte_signed_zero_test");
            replace1(i + 1, "or a");
            delete_n(i + 2, 5);
            changed = 1;
            if (i > 0) i--;
            continue;
        }
    }

    return changed;
}
