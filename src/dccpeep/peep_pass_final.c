/* peep_pass_final.c - terminal relaxation and cleanup passes.
 *
 * These rewrites run after fixed-point and size-mode passes so their address
 * estimates and dead-load decisions see the final instruction stream.
 */
#include "dccpeep_internal.h"

static int instr_size_upper(int line_index)
{
    const char *s = user_asm_original[line_index] != NULL
        ? user_asm_original[line_index] : lines[line_index];
    /* Labels, comments, blank lines and assembler directives emit no code. */
    if (s[0] == 0 || s[0] == ';' || starts_label(s))
        return 0;
    if (strncmp(s, "cseg", 4) == 0 || strncmp(s, "dseg", 4) == 0 ||
        strncmp(s, "public", 6) == 0 || strncmp(s, "extrn", 5) == 0 ||
        strncmp(s, "end", 3) == 0 || strchr(s, '='))   /* "X equ N", "_x equ" */
        return 0;

    /* db N,N,...  -> one byte per comma-separated item. */
    if (strncmp(s, "db ", 3) == 0 || strncmp(s, "dw ", 3) == 0) {
        int items = 1;
        const char *p;
        int per = (s[1] == 'w') ? 2 : 1;
        for (p = s; *p; ++p)
            if (*p == ',')
                items++;
        return items * per;
    }

    /* jr/djnz are 2 bytes; a jr we have already produced must stay sized 2. */
    if (strncmp(s, "jr ", 3) == 0 || strncmp(s, "djnz", 4) == 0)
        return 2;

    /* Any IX/IY-relative instruction is at most 4 bytes (DD/FD prefix). */
    if (strstr(s, "ix") || strstr(s, "iy"))
        return 4;

    /* Everything else dcc emits (jp/call/ld rr,nn/arith/stack/ret/...) is at
     * most 3 bytes.  Using 3 as the universal upper bound is safe. */
    return 3;
}

static int jr_convertible(int i, char *labelout)
{
    const char *s = lines[i];
    char lab[128];

    if (strncmp(s, "jp ", 3) != 0)
        return 0;
    /* Reject indirect jp (hl) / jp (ix) etc. and the m condition. */
    if (strchr(s, '('))
        return 0;
    if (parse_jp_z_label(s, lab) || parse_jp_nz_label(s, lab) ||
        parse_jp_c_label(s, lab) || parse_jp_nc_label(s, lab)) {
        /* parse_jp_cond_label copies the remainder of the line, which may
         * include a trailing "; peep: ..." comment or whitespace.  Trim the
         * label to its first token. */
        int k = 0;
        while (lab[k] && lab[k] != ' ' && lab[k] != '\t' && lab[k] != ';')
            k++;
        lab[k] = 0;
        if (lab[0] == 0)
            return 0;
        strcpy(labelout, lab);
        return 1;
    }
    /* Unconditional jp LABEL: no comma, target is a bare label. */
    if (!strchr(s, ',')) {
        if (jump_target(s, lab) && lab[0] != '(') {
            strcpy(labelout, lab);
            return 1;
        }
    }
    return 0;
}

static void make_jr(int i)
{
    const char *s = lines[i];
    char lab[128];
    char out[160];
    int k;

    if (parse_jp_z_label(s, lab))
        sprintf(out, "jr z,%s", lab);
    else if (parse_jp_nz_label(s, lab))
        sprintf(out, "jr nz,%s", lab);
    else if (parse_jp_c_label(s, lab))
        sprintf(out, "jr c,%s", lab);
    else if (parse_jp_nc_label(s, lab))
        sprintf(out, "jr nc,%s", lab);
    else {
        jump_target(s, lab);
        sprintf(out, "jr %s", lab);
        replace1_tagged(i, out, "jp_to_jr");
        return;
    }
    /* Conditional parse may have captured a trailing comment; trim it. */
    k = 0;
    while (out[k])
        k++;
    while (k > 0 && (out[k - 1] == ' ' || out[k - 1] == '\t'))
        k--;
    out[k] = 0;
    {
        char *semi = strchr(out, ';');
        if (semi) {
            while (semi > out && (semi[-1] == ' ' || semi[-1] == '\t'))
                semi--;
            *semi = 0;
        }
    }
    replace1_tagged(i, out, "jp_to_jr");
}

int pass_jp_to_jr(void)
{
    static int addr[MAX_LINES];   /* upper-bound byte address of each line */
    int i;
    int any = 0;
    int changed;

    do {
        int pc = 0;
        changed = 0;

        /* Assign an upper-bound address to every line. */
        for (i = 0; i < nlines; i++) {
            addr[i] = pc;
            pc += instr_size_upper(i);
        }

        for (i = 0; i < nlines; i++) {
            char lab[128];
            char def[130];
            int j;
            int target = -1;
            int from, to, disp;

            if (!jr_convertible(i, lab))
                continue;

            /* Find the target label's line. */
            sprintf(def, "%s:", lab);
            for (j = 0; j < nlines; j++) {
                if (starts_label(lines[j]) && strcmp(lines[j], def) == 0) {
                    target = j;
                    break;
                }
            }
            if (target < 0)
                continue;

            /* Opaque user assembly may contain directives or macros whose
             * encoded size this text-level estimator cannot bound. */
            {
                int lo = (i < target) ? i : target;
                int hi = (i < target) ? target : i;
                int k;
                int opaque = 0;
                for (k = lo; k <= hi; ++k)
                    if (user_asm_original[k] != NULL) {
                        opaque = 1;
                        break;
                    }
                if (opaque)
                    continue;
            }

            /* Displacement is measured from the address *after* the 2-byte jr
             * to the target address.  Using the current (jp, size<=3) address
             * for line i is safe: the real jr is shorter, so the real
             * displacement magnitude is no larger than what we compute when
             * the branch points forward, and for backward branches the +2
             * end-of-instruction offset is exact for jr.  We bound both ways
             * by the conservative window below. */
            from = addr[i] + 2;     /* end of the would-be jr */
            to   = addr[target];
            disp = to - from;

            if (disp >= -128 && disp <= 127) {
                make_jr(i);
                changed = 1;
                any = 1;
            }
        }
    } while (changed);

    return any;
}

static int ld_hl_const_high_bit_set(const char *s, int *bit15)
{
    char val[MAX_LINE];
    const char *p;
    long n;
    int neg = 0;

    if (!parse_ld_hl_imm(s, val, sizeof(val)))
        return 0;

    p = val;
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p == '-') { neg = 1; p++; }
    else if (*p == '+') { p++; }
    if (*p == 0 || !isdigit((unsigned char)*p))
        return 0;                 /* not a bare decimal integer */
    n = 0;
    while (isdigit((unsigned char)*p)) {
        n = n * 10 + (*p - '0');
        p++;
    }
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p != 0)
        return 0;                 /* trailing garbage / symbol arithmetic */

    if (neg)
        n = -n;
    *bit15 = ((n & 0x8000L) != 0) ? 1 : 0;
    return 1;
}

int pass_fold_const_sign_extend(void)
{
    int i;
    int changed = 0;
    int bit15;

    for (i = 0; i + 5 < nlines; ++i) {
        if (!ld_hl_const_high_bit_set(lines[i], &bit15))
            continue;
        if (!eq(i + 1, "ld a,h") || !eq(i + 2, "rlca") ||
            !eq(i + 3, "sbc a,a") || !eq(i + 4, "ld d,a") ||
            !eq(i + 5, "ld e,a"))
            continue;

        /* Replace the 5-instruction extend with one immediate DE load. */
        delete_n(i + 1, 5);
        insert_line_tagged(i + 1, bit15 ? "ld de,65535" : "ld de,0",
                           "fold_const_sxt");
        changed = 1;
    }

    return changed;
}

int pass_elim_dead_reg16_reload(void)
{
    int i;
    int changed = 0;
    char r1[4];
    char r2[4];

    for (i = 0; i + 1 < nlines; ++i) {
        if (parse_ld_reg16_dest(lines[i], r1) &&
            parse_ld_reg16_dest(lines[i + 1], r2) &&
            strcmp(r1, r2) == 0) {
            delete_n(i, 1);   /* the first load is dead */
            changed = 1;
            if (i > 0)
                --i;          /* re-check against the new predecessor */
        }
    }

    return changed;
}

