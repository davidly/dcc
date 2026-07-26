/* peep_pass_stubs.c - size-oriented shared-helper rewrites.
 *
 * These passes run after fixed-point optimization. Their order remains in
 * dccpeep.c because later passes consume forms produced by earlier ones.
 */
#include "dccpeep_internal.h"

static int count_exact_sequence(const char *const *pattern, int count)
{
    int i;
    int j;
    int matches = 0;

    for (i = 0; i + count <= nlines; ++i) {
        for (j = 0; j < count; ++j)
            if (!eq(i + j, pattern[j]))
                break;
        if (j == count)
            ++matches;
    }
    return matches;
}

int pass_shared_frame_stubs(void)
{
    int i, j;
    int changed = 0;
    int used_entr = 0, used_en0 = 0, used_lve = 0;

    for (i = 0; i + 2 < nlines; i++) {
        int next_func, epi, has_locals;
        char lsize[MAX_LINE];

        if (!eq(i, "push ix") || !eq(i+1, "ld ix,0") || !eq(i+2, "add ix,sp"))
            continue;

        /* Find next function boundary. */
        next_func = nlines;
        for (j = i + 3; j < nlines; j++) {
            if (strncmp(lines[j], "public ", 7) == 0 || is_global_asm_label_line(j)) {
                next_func = j;
                break;
            }
        }

        /* Check for local variable allocation immediately after prologue. */
        has_locals = 0;
        lsize[0] = '\0';
        if (i + 5 < next_func &&
            strncmp(lines[i+3], "ld hl,-", 7) == 0 &&
            eq(i+4, "add hl,sp") &&
            eq(i+5, "ld sp,hl")) {
            has_locals = 1;
            strncpy(lsize, lines[i+3], sizeof(lsize) - 1);
            lsize[sizeof(lsize) - 1] = '\0';
        }

        if (has_locals) {
            /* Find the matching epilogue: ld sp,ix / pop ix / ret */
            epi = -1;
            for (j = i + 6; j + 2 < next_func; j++) {
                if (eq(j, "ld sp,ix") && eq(j+1, "pop ix") && eq(j+2, "ret")) {
                    epi = j;
                    break;
                }
            }
            if (epi < 0)
                continue; /* no canonical epilogue found — leave as-is */

            /*
             * Replace prologue: remove push ix/ld ix,0/add ix,sp (3 lines),
             * then remove add hl,sp/ld sp,hl (2 lines), leaving ld hl,-N at
             * position i.  Insert call __entr after it.
             */
            delete_n(i, 3);
            epi -= 3;
            delete_n(i + 1, 2);
            epi -= 2;
            insert_line_tagged(i + 1, "call __entr", "shared_frame");
            epi += 1;
            used_entr = 1;

            /* Replace epilogue: ld sp,ix / pop ix / ret -> jp __lve */
            replace1_tagged(epi, "jp __lve", "shared_frame");
            delete_n(epi + 1, 2);
            used_lve = 1;
        } else {
            /* No-locals: push ix / ld ix,0 / add ix,sp -> call __en0.
             * Since dcc now always emits ld sp,ix in the epilogue,
             * also convert ld sp,ix / pop ix / ret -> jp __lve so that
             * no-locals functions remain as compact as before. */
            epi = -1;
            for (j = i + 3; j + 2 < next_func; j++) {
                if (eq(j, "ld sp,ix") && eq(j+1, "pop ix") && eq(j+2, "ret")) {
                    epi = j;
                    break;
                }
            }

            replace1_tagged(i, "call __en0", "shared_frame");
            delete_n(i + 1, 2);
            used_en0 = 1;

            if (epi >= 0) {
                epi -= 2; /* two lines removed from prolog above */
                replace1_tagged(epi, "jp __lve", "shared_frame");
                delete_n(epi + 1, 2);
                used_lve = 1;
            }
        }

        changed = 1;
        i--; /* re-examine same index after line deletions */
    }

    /*
     * Inject extrn declarations at the top of the file for each stub used.
     * dccrtlstrip uses these references to select which RTL blocks to include.
     * Insert in reverse order so the final sequence reads __entr/__en0/__lve.
     */
    if (used_entr || used_en0 || used_lve) {
        int pos = 0;
        if (used_lve)  insert_line(pos, "extrn __lve");
        if (used_en0)  insert_line(pos, "extrn __en0");
        if (used_entr) insert_line(pos, "extrn __entr");
    }

    return changed;
}

int pass_lvar_stubs(void)
{
    static const char * const names[] = {
        "__lv1","__lv2","__lv3","__lv4","__lv5","__lv6","__lv7","__lv8"
    };
    static char low[8][20], high[8][20];
    static int inited = 0;
    int i, k, changed;
    int matches[8];
    int used[8];

    if (!inited) {
        for (k = 0; k < 8; k++) {
            sprintf(low[k],  "ld l,(ix-%d)", (k+1)*2);
            sprintf(high[k], "ld h,(ix-%d)", (k+1)*2-1);
        }
        inited = 1;
    }

    for (k = 0; k < 8; k++) {
        const char *pattern[] = { low[k], high[k] };
        matches[k] = count_exact_sequence(pattern, 2);
        used[k] = 0;
    }
    changed = 0;

    for (i = 0; i + 1 < nlines; i++) {
        for (k = 0; k < 8; k++) {
            if (matches[k] >= 3 && eq(i, low[k]) && eq(i+1, high[k])) {
                char stub[24];
                sprintf(stub, "call %s", names[k]);
                replace1_tagged(i, stub, "lvar");
                delete_n(i+1, 1);
                used[k] = 1; changed = 1;
                break;
            }
        }
    }

    if (changed) {
        for (k = 7; k >= 0; k--) {
            if (used[k]) {
                char extrn[24];
                sprintf(extrn, "extrn %s", names[k]);
                insert_line(0, extrn);
            }
        }
    }
    return changed;
}

int pass_svar_stubs(void)
{
    static const char * const names[] = {
        "__sv1","__sv2","__sv3","__sv4","__sv5","__sv6"
    };
    static char low[6][24], high[6][24];
    static int inited = 0;
    int i, k, changed;
    int matches[6];
    int used[6];

    if (!inited) {
        for (k = 0; k < 6; k++) {
            sprintf(low[k],  "ld (ix-%d),l", (k+1)*2);
            sprintf(high[k], "ld (ix-%d),h", (k+1)*2-1);
        }
        inited = 1;
    }

    for (k = 0; k < 6; k++) {
        const char *pattern[] = { low[k], high[k] };
        matches[k] = count_exact_sequence(pattern, 2);
        used[k] = 0;
    }
    changed = 0;

    for (i = 0; i + 1 < nlines; i++) {
        for (k = 0; k < 6; k++) {
            if (matches[k] >= 3 && eq(i, low[k]) && eq(i+1, high[k])) {
                char stub[24];
                sprintf(stub, "call %s", names[k]);
                replace1_tagged(i, stub, "svar");
                delete_n(i+1, 1);
                used[k] = 1; changed = 1;
                break;
            }
        }
    }

    if (changed) {
        for (k = 5; k >= 0; k--) {
            if (used[k]) {
                char extrn[24];
                sprintf(extrn, "extrn %s", names[k]);
                insert_line(0, extrn);
            }
        }
    }
    return changed;
}

int pass_larg_stubs(void)
{
    int i, changed;
    int count_la1 = 0, count_la2 = 0, count_la3 = 0;
    int used_la1 = 0, used_la2 = 0, used_la3 = 0;

    changed = 0;

    for (i = 0; i + 1 < nlines; ++i) {
        if (eq(i, "ld l,(ix+4)") && eq(i+1, "ld h,(ix+5)")) ++count_la1;
        if (eq(i, "ld l,(ix+6)") && eq(i+1, "ld h,(ix+7)")) ++count_la2;
        if (eq(i, "ld l,(ix+8)") && eq(i+1, "ld h,(ix+9)")) ++count_la3;
    }

    for (i = 0; i + 1 < nlines; i++) {
        if (count_la1 >= 3 && eq(i, "ld l,(ix+4)") && eq(i+1, "ld h,(ix+5)")) {
            replace1_tagged(i, "call __la1", "larg");
            delete_n(i+1, 1);
            used_la1 = 1; changed = 1;
        } else if (count_la2 >= 3 && eq(i, "ld l,(ix+6)") && eq(i+1, "ld h,(ix+7)")) {
            replace1_tagged(i, "call __la2", "larg");
            delete_n(i+1, 1);
            used_la2 = 1; changed = 1;
        } else if (count_la3 >= 3 && eq(i, "ld l,(ix+8)") && eq(i+1, "ld h,(ix+9)")) {
            replace1_tagged(i, "call __la3", "larg");
            delete_n(i+1, 1);
            used_la3 = 1; changed = 1;
        }
    }

    if (used_la1 || used_la2 || used_la3) {
        int pos = 0;
        if (used_la3) insert_line(pos, "extrn __la3");
        if (used_la2) insert_line(pos, "extrn __la2");
        if (used_la1) insert_line(pos, "extrn __la1");
    }

    return changed;
}

int pass_phix_stub(void)
{
    int i, changed;
    int used = 0;
    const char *pattern[] = { "push hl", "push ix", "pop hl" };

    changed = 0;

    if (count_exact_sequence(pattern, 3) < 7)
        return 0;

    for (i = 0; i + 2 < nlines; i++) {
        if (eq(i, "push hl") && eq(i+1, "push ix") && eq(i+2, "pop hl")) {
            replace1_tagged(i, "call __phix", "phix");
            delete_n(i+1, 2);
            used = 1; changed = 1;
        }
    }

    if (used)
        insert_line(0, "extrn __phix");

    return changed;
}

int pass_larg_direct_store(void)
{
    int i, changed = 0;
    char addr[MAX_LINE], newline[MAX_LINE];

    for (i = 0; i + 7 < nlines; i++) {
        char tmp[MAX_LINE];
        const char *stub;

        if (!parse_ld_hl_imm(lines[i], addr, sizeof(addr)))
            continue;
        /* addr must be a label/symbol (not a register or computed value) */
        if (addr[0] == '(' || (addr[0] >= '0' && addr[0] <= '9'))
            continue;
        if (!eq(i+1, "push hl"))
            continue;

        strip_peep_comment_copy(tmp, lines[i+2]);
        if (strncmp(tmp, "call __la", 9) == 0 || strncmp(tmp, "call __lv", 9) == 0)
            stub = tmp + 5; /* "call " is 5 chars, stub = "__la1" etc. */
        else
            continue;

        if (!eq(i+3, "ex de,hl") ||
            !eq(i+4, "pop hl") ||
            !eq(i+5, "ld (hl),e") ||
            !eq(i+6, "inc hl") ||
            !eq(i+7, "ld (hl),d"))
            continue;

        /* Replace: keep stub call at i, replace i+1 with ld (ADDR),hl */
        sprintf(newline, "call %s", stub);
        replace1_tagged(i, newline, "larg_dstore");
        sprintf(newline, "ld (%s),hl", addr);
        replace1(i+1, newline);
        delete_n(i+2, 6);
        changed = 1;
    }

    return changed;
}

int pass_ldwl_stub(void)
{
    int i, changed = 0, used = 0;
    const char *pattern[] = { "ld e,(hl)", "inc hl", "ld d,(hl)", "ex de,hl" };

    if (count_exact_sequence(pattern, 4) < 5)
        return 0;

    for (i = 0; i + 3 < nlines; i++) {
        if (eq(i,   "ld e,(hl)") &&
            eq(i+1, "inc hl") &&
            eq(i+2, "ld d,(hl)") &&
            eq(i+3, "ex de,hl")) {
            replace1_tagged(i, "call __ldwl", "ldwl");
            delete_n(i+1, 3);
            used = 1; changed = 1;
        }
    }

    if (used)
        insert_line(0, "extrn __ldwl");

    return changed;
}

int pass_wand_stub(void)
{
    int i, changed = 0, used = 0;
    const char *pattern[] = {
        "ld a,h", "and d", "ld h,a", "ld a,l", "and e", "ld l,a"
    };

    if (count_exact_sequence(pattern, 6) < 3)
        return 0;

    for (i = 0; i + 5 < nlines; i++) {
        if (eq(i,   "ld a,h") &&
            eq(i+1, "and d") &&
            eq(i+2, "ld h,a") &&
            eq(i+3, "ld a,l") &&
            eq(i+4, "and e") &&
            eq(i+5, "ld l,a")) {
            replace1_tagged(i, "call __wand", "wand");
            delete_n(i+1, 5);
            used = 1; changed = 1;
        }
    }

    if (used)
        insert_line(0, "extrn __wand");

    return changed;
}

int pass_icmp_stub(void)
{
    int i, changed = 0, used = 0;
    const char *pattern[] = {
        "ld a,h", "xor 80h", "ld h,a", "ld a,d", "xor 80h", "ld d,a",
        "or a", "sbc hl,de"
    };

    if (count_exact_sequence(pattern, 8) < 2)
        return 0;

    for (i = 0; i + 7 < nlines; i++) {
        if (eq(i,   "ld a,h") &&
            eq(i+1, "xor 80h") &&
            eq(i+2, "ld h,a") &&
            eq(i+3, "ld a,d") &&
            eq(i+4, "xor 80h") &&
            eq(i+5, "ld d,a") &&
            eq(i+6, "or a") &&
            eq(i+7, "sbc hl,de")) {
            replace1_tagged(i, "call __icmp", "icmp");
            delete_n(i+1, 7);
            used = 1; changed = 1;
        }
    }

    if (used)
        insert_line(0, "extrn __icmp");

    return changed;
}

int pass_sxde_stub(void)
{
    int i, changed = 0, used = 0;
    const char *pattern[] = { "ld a,h", "rlca", "sbc a,a", "ld d,a", "ld e,a" };

    if (count_exact_sequence(pattern, 5) < 3)
        return 0;

    for (i = 0; i + 4 < nlines; i++) {
        if (eq(i,   "ld a,h") &&
            eq(i+1, "rlca") &&
            eq(i+2, "sbc a,a") &&
            eq(i+3, "ld d,a") &&
            eq(i+4, "ld e,a")) {
            replace1_tagged(i, "call __sxde", "sxde");
            delete_n(i+1, 4);
            used = 1; changed = 1;
        }
    }

    if (used)
        insert_line(0, "extrn __sxde");

    return changed;
}

int pass_sxhl_stub(void)
{
    int i, changed = 0, used = 0;
    const char *pattern[] = { "ld a,l", "rlca", "sbc a,a", "ld h,a" };

    if (count_exact_sequence(pattern, 4) < 5)
        return 0;

    for (i = 0; i + 3 < nlines; i++) {
        if (eq(i,   "ld a,l") &&
            eq(i+1, "rlca") &&
            eq(i+2, "sbc a,a") &&
            eq(i+3, "ld h,a")) {
            replace1_tagged(i, "call __sxhl", "sxhl");
            delete_n(i+1, 3);
            used = 1; changed = 1;
        }
    }

    if (used)
        insert_line(0, "extrn __sxhl");

    return changed;
}

