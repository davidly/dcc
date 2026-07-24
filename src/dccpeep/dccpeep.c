/*
    peephole optimizer for dcc C89 compiler targeting Z80 with M80 syntax
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINES 400000
#define MAX_LINE  512

static char *lines[MAX_LINES];
static int nlines;
static int opt_size = 0;  /* -Os: use RTL helper stubs; default -Ot: inline */

/* -fundocumented-z80: allow peephole passes that rely on undocumented Z80
 * opcodes (currently just the IYH/IYL half-register load/inc/dec forms
 * pass_byte_loop_counter_to_reg_iyl/pass_byte_incr_loop_counter_to_reg_iyl
 * use, wrapped in M80 macros since M80 has no native mnemonic for them -
 * see the macro prelude in main()). These opcodes are well-established
 * folklore on real NMOS Z80 silicon and its common clones, and verified
 * working under ntvcm, but are not part of the documented Z80 instruction
 * set, so they are opt-in and OFF by default. */
static int allow_undocumented_z80 = 0;

static char *xstrdup2(const char *s)
{
    char *p;
    p = (char *)malloc(strlen(s) + 1);
    if (!p) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    strcpy(p, s);
    return p;
}

static void trim(char *s)
{
    int i;
    int j;
    int n;

    n = (int)strlen(s);
    while (n > 0 &&
           (s[n - 1] == '\n' || s[n - 1] == '\r' ||
            s[n - 1] == ' '  || s[n - 1] == '\t'))
        s[--n] = 0;

    i = 0;
    while (s[i] == ' ' || s[i] == '\t')
        i++;

    if (i) {
        j = 0;
        while (s[i])
            s[j++] = s[i++];
        s[j] = 0;
    }
}

static int eq(int i, const char *s)
{
    char buf[MAX_LINE];
    char *semi;
    int n;

    if (i < 0 || i >= nlines)
        return 0;

    strncpy(buf, lines[i], sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

    semi = strchr(buf, ';');
    if (semi)
        *semi = 0;

    n = (int)strlen(buf);
    while (n > 0 && (buf[n - 1] == ' ' || buf[n - 1] == '\t'))
        buf[--n] = 0;

    return strcmp(buf, s) == 0;
}

static int starts_label(const char *s)
{
    int n;
    n = (int)strlen(s);
    return n > 0 && s[n - 1] == ':';
}

static int is_blank_or_comment(const char *s)
{
    return s[0] == 0 || s[0] == ';';
}


static void strip_peep_comment_copy(char *dst, const char *src)
{
    int i;
    int n;

    i = 0;
    while (src[i] && src[i] != ';' && i < MAX_LINE - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;

    n = (int)strlen(dst);
    while (n > 0 && (dst[n - 1] == ' ' || dst[n - 1] == '\t')) {
        dst[n - 1] = 0;
        n--;
    }
}

static void strip_peep_comment_lower_copy(char *dst, const char *src)
{
    char *p;

    strip_peep_comment_copy(dst, src);
    for (p = dst; *p; ++p)
        *p = (char)tolower((unsigned char)*p);
}

static void replace1(int i, const char *s)
{
    char *p;

    /*
     * Be careful when callers pass lines[i] as the replacement text.
     * The old version freed lines[i] before duplicating s, which is a
     * use-after-free if s == lines[i].  That happened in the signed_le_zero
     * peephole and produced platform-dependent garbage on Linux while often
     * appearing to work on Windows.
     */
    p = xstrdup2(s);
    free(lines[i]);
    lines[i] = p;
}

static char *make_tagged_line(const char *s, const char *tag)
{
    char *buf;
    size_t n;

    /*
     * Optimized lines can already be close to MAX_LINE bytes.  A fixed
     * snprintf buffer is safe at runtime but triggers -Wformat-truncation
     * under fortified libc because the diagnostic correctly sees that the
     * tag may not fit.  Allocate the exact size instead.
     */
    n = strlen(s) + strlen(tag) + strlen(" ; peep: ") + 1;
    buf = (char *)malloc(n);
    if (!buf) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    strcpy(buf, s);
    strcat(buf, " ; peep: ");
    strcat(buf, tag);
    return buf;
}

static void replace1_tagged(int i, const char *s, const char *tag)
{
    char *buf;

    buf = make_tagged_line(s, tag);
    replace1(i, buf);
    free(buf);
}

static void delete_n(int i, int count)
{
    int j;

    for (j = 0; j < count; j++)
        free(lines[i + j]);

    for (j = i; j + count < nlines; j++)
        lines[j] = lines[j + count];

    nlines -= count;
}

static void insert_line(int i, const char *s)
{
    int j;

    if (nlines >= MAX_LINES) {
        fprintf(stderr, "too many lines\n");
        exit(1);
    }

    for (j = nlines; j > i; j--)
        lines[j] = lines[j - 1];

    lines[i] = xstrdup2(s);
    nlines++;
}

static void insert_line_tagged(int i, const char *s, const char *tag)
{
    char *buf;

    buf = make_tagged_line(s, tag);
    insert_line(i, buf);
    free(buf);
}

static int parse_ld_hl_imm(const char *s, char *val)
{
    const char *p;
    char tmp[MAX_LINE];

    strip_peep_comment_copy(tmp, s);
    p = "ld hl,";
    if (strncmp(tmp, p, strlen(p)) != 0)
        return 0;

    strcpy(val, tmp + strlen(p));
    return 1;
}

static int parse_ld_de_imm(const char *s, char *val)
{
    const char *p;
    char tmp[MAX_LINE];

    strip_peep_comment_copy(tmp, s);
    p = "ld de,";
    if (strncmp(tmp, p, strlen(p)) != 0)
        return 0;

    strcpy(val, tmp + strlen(p));
    return 1;
}

static int parse_nonneg_int(const char *s, int *out)
{
    int v;

    if (*s < '0' || *s > '9')
        return 0;

    v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }

    if (*s != 0)
        return 0;

    *out = v;
    return 1;
}

static int jump_target(const char *s, char *out);

static int is_uncond_jp(const char *s)
{
    const char *p;

    if (strncmp(s, "jp ", 3) != 0)
        return 0;

    p = s + 3;

    /* Conditional forms are emitted as jp z, Lx / jp nc, Lx, etc. */
    while (*p) {
        if (*p == ',')
            return 0;
        p++;
    }

    return 1;
}

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


static int parse_jp_cond_label(const char *s, const char *cond, char *label)
{
    char pat[32];
    int n;

    sprintf(pat, "jp %s, ", cond);
    n = (int)strlen(pat);
    if (strncmp(s, pat, n) != 0)
        return 0;
    strcpy(label, s + n);
    return 1;
}

static int parse_jp_z_label(const char *s, char *label)
{
    return parse_jp_cond_label(s, "z", label);
}

static int parse_jp_nz_label(const char *s, char *label)
{
    return parse_jp_cond_label(s, "nz", label);
}

static int parse_jp_c_label(const char *s, char *label)
{
    return parse_jp_cond_label(s, "c", label);
}

static int parse_jp_nc_label(const char *s, char *label)
{
    return parse_jp_cond_label(s, "nc", label);
}

static int line_is_label_name(int i, const char *name)
{
    char tmp[MAX_LINE];
    if (i < 0 || i >= nlines)
        return 0;
    sprintf(tmp, "%s:", name);
    return strcmp(lines[i], tmp) == 0;
}



static int is_global_asm_label_line(int i)
{
    const char *s;
    int n;

    if (i < 0 || i >= nlines)
        return 0;
    s = lines[i];
    n = (int)strlen(s);
    if (n < 2 || s[n - 1] != ':')
        return 0;

    /* DCC emits global/static function and data labels at column 0 as
     * _name: or _Znnn:. Local control-flow labels are Lnnn:, so they
     * must not end a function range. */
    return s[0] == '_';
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


static int is_jump_line(const char *s)
{
    return strncmp(s, "jp ", 3) == 0;
}

static int jump_target(const char *s, char *out)
{
    const char *p;
    int i;

    if (!is_jump_line(s))
        return 0;

    p = s + 3;

    /* conditional form: jp z, L1 / jp nc, L1 */
    while (*p && *p != ',')
        p++;

    if (*p == ',') {
        p++;
        while (*p == ' ' || *p == '\t')
            p++;
    } else {
        p = s + 3;
        while (*p == ' ' || *p == '\t')
            p++;
    }

    if (*p == 0)
        return 0;

    i = 0;
    while (*p && *p != ' ' && *p != '\t' && i < 120)
        out[i++] = *p++;
    out[i] = 0;
    return i > 0;
}

static void rewrite_jump_target(int i, const char *newtarget)
{
    char out[256];
    char *comma;

    comma = strchr(lines[i], ',');
    if (comma) {
        int prefix_len;
        prefix_len = (int)(comma - lines[i]) + 1;
        if (prefix_len > 200) prefix_len = 200;
        memcpy(out, lines[i], (size_t)prefix_len);
        out[prefix_len] = 0;
        strcat(out, " ");
        strcat(out, newtarget);
    } else {
        strcpy(out, "jp ");
        strcat(out, newtarget);
    }

    replace1(i, out);
}

static int label_name_at(int i, char *out)
{
    int n;

    if (i < 0 || i >= nlines || !starts_label(lines[i]))
        return 0;

    n = (int)strlen(lines[i]);
    if (n <= 1 || n > 120)
        return 0;

    memcpy(out, lines[i], (size_t)(n - 1));
    out[n - 1] = 0;
    return 1;
}

static int is_label_referenced(const char *lab)
{
    int i;
    char tgt[128];
    int lablen;
    const char *found;
    char before;
    char after;

    lablen = (int)strlen(lab);

    for (i = 0; i < nlines; i++) {
        if (jump_target(lines[i], tgt) && strcmp(tgt, lab) == 0)
            return 1;

        /* Be conservative for data or non-jump references. */
        if (!starts_label(lines[i]) && !is_jump_line(lines[i])) {
            found = strstr(lines[i], lab);
            while (found != NULL) {
                /* Require word boundaries so "L1" does not match inside "L10". */
                before = (found > lines[i]) ? *(found - 1) : 0;
                after  = *(found + lablen);
                if (!isalnum((unsigned char)before) && before != '_' &&
                    !isalnum((unsigned char)after)  && after  != '_') {
                    return 1;
                }
                found = strstr(found + 1, lab);
            }
        }
    }

    return 0;
}

/*
 * Collapse adjacent-label chains:
 *
 *   L1:
 *   L2:
 *
 * redirects references to L1 to L2, then removes L1 if unreferenced.
 * This also handles:
 *
 *   jp L1
 *   L1:
 *   L2:
 *
 * after the redirect, the existing jump-to-next-label rule can remove
 * jp L2 / L2: on a later pass.
 */
static int pass_labels(void)
{
    int i, j;
    int changed;
    char oldlab[128];
    char newlab[128];
    char tgt[128];

    changed = 0;

    for (i = 0; i + 1 < nlines; i++) {
        if (!label_name_at(i, oldlab))
            continue;

        j = i + 1;
        while (j < nlines && is_blank_or_comment(lines[j]))
            j++;

        if (!label_name_at(j, newlab))
            continue;

        /* Rewrite all jumps to old label to the following label. */
        {
            int k;
            for (k = 0; k < nlines; k++) {
                if (jump_target(lines[k], tgt) && strcmp(tgt, oldlab) == 0) {
                    rewrite_jump_target(k, newlab);
                    changed = 1;
                }
            }
        }

        if (!is_label_referenced(oldlab)) {
            delete_n(i, 1);
            changed = 1;
            if (i > 0) i--;
        }
    }

    return changed;
}



static int parse_ld_de_positive_imm(const char *s, long *out)
{
    char *endp;
    long v;
    char tmp[MAX_LINE];

    strip_peep_comment_copy(tmp, s);

    if (strncmp(tmp, "ld de,", 6) != 0)
        return 0;

    v = strtol(tmp + 6, &endp, 0);
    while (*endp == ' ' || *endp == '\t')
        endp++;

    if (*endp != 0)
        return 0;

    if (v <= 0 || v > 32767)
        return 0;

    out[0] = v;
    return 1;
}




static int peep_parse_jp_cond_label(const char *s, const char *cond, char *lab)
{
    char prefix[32];
    const char *p;
    int i;

    sprintf(prefix, "jp %s,", cond);
    if (strncmp(s, prefix, strlen(prefix)) != 0)
        return 0;

    p = s + strlen(prefix);
    while (*p == ' ' || *p == '\t')
        p++;

    i = 0;
    while (*p && *p != ' ' && *p != '\t' && i < 120)
        lab[i++] = *p++;
    lab[i] = 0;
    return i > 0;
}

static int peep_parse_jp_uncond_label(const char *s, char *lab)
{
    const char *p;
    int i;

    if (strncmp(s, "jp ", 3) != 0)
        return 0;

    if (strchr(s + 3, ',') != NULL)
        return 0;

    p = s + 3;
    while (*p == ' ' || *p == '\t')
        p++;

    i = 0;
    while (*p && *p != ' ' && *p != '\t' && i < 120)
        lab[i++] = *p++;
    lab[i] = 0;
    return i > 0;
}

static void peep_make_cond_jump(char *out, size_t size, const char *cond, const char *lab)
{
    snprintf(out, size, "jp %s, %s", cond, lab);
}

static int peep_parse_any_cond_jump(const char *s, char *cond, char *lab)
{
    if (peep_parse_jp_cond_label(s, "z", lab)) { strcpy(cond, "z"); return 1; }
    if (peep_parse_jp_cond_label(s, "nz", lab)) { strcpy(cond, "nz"); return 1; }
    if (peep_parse_jp_cond_label(s, "c", lab)) { strcpy(cond, "c"); return 1; }
    if (peep_parse_jp_cond_label(s, "nc", lab)) { strcpy(cond, "nc"); return 1; }
    return 0;
}

static const char *peep_inverse_cond(const char *cond)
{
    if (!strcmp(cond, "z")) return "nz";
    if (!strcmp(cond, "nz")) return "z";
    if (!strcmp(cond, "c")) return "nc";
    if (!strcmp(cond, "nc")) return "c";
    return NULL;
}


/*
 * Collapse common address formation:
 *
 *     ld hl,_base
 *     push hl
 *     ld l,(ix+N)
 *     ld h,(ix+N+1)
 *     ex de,hl
 *     pop hl
 *     add hl,de
 *
 * into:
 *
 *     ld l,(ix+N)
 *     ld h,(ix+N+1)
 *     ld de,_base
 *     add hl,de
 *
 * This is a general DCC expression pattern for base + local/param index,
 * very common at hot string/memory call sites.  The expression result is HL;
 * DCC codegen should not depend on DE preserving the index after address
 * formation, so the shorter sequence is safe for normal expression code.
 */
static int peep_parse_ld_l_ix(const char *s, char *off);
static int peep_parse_ld_h_ix(const char *s, char *off);

static int pass_base_index_addr(void)
{
    int i;
    int changed;
    char base[128];
    char loff[32];
    char hoff[32];
    char out[256];

    changed = 0;

    for (i = 0; i + 6 < nlines; ++i) {
        if (!parse_ld_hl_imm(lines[i], base))
            continue;
        /* Only rewrite constants/labels that are also legal as ld de,<base>. */
        if (base[0] == '(')
            continue;
        if (!eq(i + 1, "push hl"))
            continue;
        if (!peep_parse_ld_l_ix(lines[i + 2], loff))
            continue;
        if (!peep_parse_ld_h_ix(lines[i + 3], hoff))
            continue;
        if (!eq(i + 4, "ex de,hl"))
            continue;
        if (!eq(i + 5, "pop hl"))
            continue;
        if (!eq(i + 6, "add hl,de"))
            continue;

        replace1_tagged(i, lines[i + 2], "base_index_addr");
        replace1(i + 1, lines[i + 3]);
        sprintf(out, "ld de,%s", base);
        replace1(i + 2, out);
        replace1(i + 3, "add hl,de");
        delete_n(i + 4, 3);
        changed = 1;
        if (i > 0)
            --i;
    }

    return changed;
}


static int pass_branch_over_jump(void)
{
    int i;
    int changed;
    char cond[16];
    char lbody[128];
    char lexit[128];
    const char *inv;
    char newline[160];

    changed = 0;

    for (i = 0; i + 2 < nlines; ++i) {
        if (peep_parse_any_cond_jump(lines[i], cond, lbody) &&
            peep_parse_jp_uncond_label(lines[i + 1], lexit) &&
            line_is_label_name(i + 2, lbody)) {
            inv = peep_inverse_cond(cond);
            if (!inv)
                continue;
            sprintf(newline, "jp %s, %s", inv, lexit);
            replace1_tagged(i, newline, "branch_over_jump");
            delete_n(i + 1, 1);
            changed = 1;
            if (i > 0)
                --i;
        }
    }

    return changed;
}

/*
 * pass_jump_thread:
 *
 * Replace a jump to a trampoline label (a label whose only content is an
 * unconditional jp) with a direct jump to the trampoline's target.
 *
 *   jp cc, Lx                       jp cc, Ly
 *   ...           becomes:          ...
 *   Lx:                             Lx: (now unreferenced; removed next pass)
 *     jp Ly                           jp Ly
 *
 * Applies to both conditional and unconditional source jumps.  Fires in the
 * pos*func winner-check functions where DCC emits intermediate boolean
 * accumulation labels that are pure trampolines, e.g.:
 *
 *   jp z, L10    ; L10 contains only "jp L18"
 * becomes:
 *   jp z, L18
 */
static int pass_jump_thread(void)
{
    int i, k, changed = 0;
    char cond[16], lx[128], ly[128];
    char newjump[256];
    char tmp[MAX_LINE];
    char target[160];

    for (i = 0; i < nlines; i++) {
        int is_cond;

        strip_peep_comment_copy(tmp, lines[i]);

        is_cond = peep_parse_any_cond_jump(tmp, cond, lx);
        if (!is_cond && !peep_parse_jp_uncond_label(tmp, lx))
            continue;

        /* Find the definition of lx */
        sprintf(target, "%s:", lx);
        for (k = 0; k < nlines; k++) {
            if (strcmp(lines[k], target) == 0)
                break;
        }
        if (k >= nlines)
            continue;

        /* The label must be followed immediately by an unconditional jump */
        if (k + 1 >= nlines)
            continue;
        strip_peep_comment_copy(tmp, lines[k + 1]);
        if (!peep_parse_jp_uncond_label(tmp, ly))
            continue;

        /* Don't thread to self */
        if (strcmp(ly, lx) == 0)
            continue;

        if (is_cond)
            sprintf(newjump, "jp %s, %s", cond, ly);
        else
            sprintf(newjump, "jp %s", ly);

        replace1_tagged(i, newjump, "jump_thread");
        changed = 1;
    }

    return changed;
}



static int peep_parse_ld_l_ix(const char *s, char *off)
{
    char tmp[MAX_LINE];
    const char *p;
    int i;

    strip_peep_comment_copy(tmp, s);

    if (strncmp(tmp, "ld l,(ix", 8) != 0)
        return 0;

    p = tmp + 8;
    i = 0;
    while (*p && *p != ')' && i < 31)
        off[i++] = *p++;
    off[i] = 0;

    if (*p != ')' || p[1] != 0)
        return 0;

    return i > 0;
}

static int peep_is_jp_z_or_nz(const char *s)
{
    return strncmp(s, "jp z,", 5) == 0 || strncmp(s, "jp nz,", 6) == 0;
}

static void peep_make_ld_a_ix(char *out, const char *off)
{
    sprintf(out, "ld a,(ix%s)", off);
}




/* Match exactly:
 *     ld (ix+N),a
 *     ld a,(ix+N)
 * allowing optimizer tags/comments after either instruction.  This is safe
 * only for adjacent instructions because the store does not alter A and no
 * intervening instruction can clobber it.
 */
static int peep_parse_ld_ix_a(const char *s, char *off)
{
    char tmp[MAX_LINE];
    const char *p;
    int i;

    strip_peep_comment_copy(tmp, s);

    if (strncmp(tmp, "ld (ix", 6) != 0)
        return 0;

    p = tmp + 6;
    i = 0;
    while (*p && *p != ')' && i < 31)
        off[i++] = *p++;
    off[i] = 0;

    if (*p != ')' || p[1] != ',' || p[2] != 'a' || p[3] != 0)
        return 0;

    return i > 0;
}

static int peep_parse_ld_a_ix(const char *s, char *off)
{
    char tmp[MAX_LINE];
    const char *p;
    int i;

    strip_peep_comment_copy(tmp, s);

    if (strncmp(tmp, "ld a,(ix", 8) != 0)
        return 0;

    p = tmp + 8;
    i = 0;
    while (*p && *p != ')' && i < 31)
        off[i++] = *p++;
    off[i] = 0;

    if (*p != ')' || p[1] != 0)
        return 0;

    return i > 0;
}

static int pass_remove_ix_store_reload_a(void)
{
    int i;
    int changed;
    char off1[32];
    char off2[32];

    changed = 0;

    for (i = 0; i + 1 < nlines; ++i) {
        if (peep_parse_ld_ix_a(lines[i], off1) &&
            peep_parse_ld_a_ix(lines[i + 1], off2) &&
            strcmp(off1, off2) == 0) {
            delete_n(i + 1, 1);
            changed = 1;
            if (i > 0)
                i--;
        }
    }

    return changed;
}


static int peep_parse_ld_de_0_to_255(const char *s, int *out)
{
    char *endp;
    long v;
    char tmp[MAX_LINE];

    strip_peep_comment_copy(tmp, s);

    if (strncmp(tmp, "ld de,", 6) != 0)
        return 0;

    v = strtol(tmp + 6, &endp, 0);
    while (*endp == ' ' || *endp == '\t')
        endp++;

    if (*endp != 0 || v < 0 || v > 255)
        return 0;

    out[0] = (int)v;
    return 1;
}

static int peep_parse_ld_de_signed(const char *s, int *out)
{
    char *endp;
    long v;
    char tmp[MAX_LINE];

    strip_peep_comment_copy(tmp, s);

    if (strncmp(tmp, "ld de,", 6) != 0)
        return 0;

    v = strtol(tmp + 6, &endp, 0);
    if (*endp != 0)
        return 0;

    *out = (int)v;
    return 1;
}

static void peep_format_ix_off(char *buf, int off)
{
    if (off >= 0)
        sprintf(buf, "+%d", off);
    else
        sprintf(buf, "%d", off);
}

static int peep_parse_ld_e_imm8(const char *s, int *out)
{
    char tmp[MAX_LINE];
    char *endp;
    long v;

    strip_peep_comment_copy(tmp, s);
    if (strncmp(tmp, "ld e,", 5) != 0)
        return 0;

    v = strtol(tmp + 5, &endp, 0);
    if (*endp != 0 || v < 0 || v > 255)
        return 0;

    *out = (int)v;
    return 1;
}

static int pass_ix_addr_byte_store_imm(void)
{
    int i;
    int j;
    int off;
    int add;
    int imm;
    int changed = 0;
    char line[MAX_LINE];
    char offbuf[32];

    for (i = 0; i + 5 < nlines; ++i) {
        if (!eq(i, "push ix")) continue;
        if (!eq(i + 1, "pop hl")) continue;
        if (!peep_parse_ld_de_signed(lines[i + 2], &off)) continue;
        if (!eq(i + 3, "add hl,de")) continue;

        j = i + 4;
        while (j < nlines && eq(j, "inc hl")) {
            off++;
            j++;
        }
        if (j + 1 < nlines && peep_parse_ld_de_signed(lines[j], &add) &&
            eq(j + 1, "add hl,de")) {
            off += add;
            j += 2;
            while (j < nlines && eq(j, "inc hl")) {
                off++;
                j++;
            }
        }

        if (off < -128 || off > 127) continue;
        if (j + 1 >= nlines) continue;
        if (!peep_parse_ld_e_imm8(lines[j], &imm)) continue;
        if (!eq(j + 1, "ld (hl),e")) continue;

        peep_format_ix_off(offbuf, off);
        sprintf(line, "ld (ix%s),%d", offbuf, imm);
        replace1_tagged(i, line, "ix_addr_byte_store_imm");
        delete_n(i + 1, j + 1 - i);
        changed = 1;
        if (i > 0) --i;
    }

    return changed;
}

/* Recognize HL = IX + constant sequences emitted for frame addresses. */
static int scan_ix_frame_addr(int i, long *lowest_offset)
{
    char cur[MAX_LINE], next[MAX_LINE];
    char *endp;
    long offset;
    long delta;
    long lowest;
    int j;
    int saw_offset;

    if (i + 1 >= nlines)
        return 0;
    strip_peep_comment_copy(cur, lines[i]);
    strip_peep_comment_copy(next, lines[i + 1]);
    if (strcmp(cur, "push ix") != 0 || strcmp(next, "pop hl") != 0)
        return 0;

    offset = 0;
    lowest = 0;
    saw_offset = 0;
    j = i + 2;
    while (j < nlines) {
        strip_peep_comment_copy(cur, lines[j]);
        if (j + 1 < nlines && strncmp(cur, "ld de,", 6) == 0) {
            strip_peep_comment_copy(next, lines[j + 1]);
            delta = strtol(cur + 6, &endp, 0);
            if (*endp == 0 && strcmp(next, "add hl,de") == 0) {
                offset += delta;
                if (!saw_offset || offset < lowest)
                    lowest = offset;
                saw_offset = 1;
                j += 2;
                continue;
            }
        }
        if (strcmp(cur, "inc hl") == 0 || strcmp(cur, "dec hl") == 0) {
            if (!saw_offset)
                lowest = 0;
            offset += strcmp(cur, "inc hl") == 0 ? 1 : -1;
            if (offset < lowest)
                lowest = offset;
            saw_offset = 1;
            j++;
            continue;
        }
        break;
    }
    if (!saw_offset)
        return 0;
    *lowest_offset = lowest;
    return 1;
}

static int may_access_escaped_frame(const char *s)
{
    if (strncmp(s, "call ", 5) == 0 || strncmp(s, "rst ", 4) == 0)
        return 1;
    return strchr(s, '(') != NULL && strstr(s, "(ix") == NULL;
}

/*
 * Dead IX-frame store elimination.
 *
 * Performs a forward-scan within each function/segment body.  A store to
 * (ix+N) is dead when it is overwritten by a later store to the same offset
 * before any read of that offset, or when the function returns (ret) without
 * ever reading the offset again.
 *
 * Conservative behaviour:
 *   - Internal labels (L\d+:) and forward/backward jumps flush all pending
 *     stores (we can no longer prove they are overwritten before read).
 *   - Segment boundaries (_Z...: labels etc.) also flush — the new segment's
 *     IX frame is different from the old one.
 *
 * Fires on crc_update_byte: ix-8..ix-5 (temp "t") are written but never read
 * (16 dead stores); earlier writes to ix-4..ix-1 (idx) and ix+4..ix+7 (crc)
 * are killed by subsequent writes without intervening reads (20 more).
 */
static int pass_elim_dead_ix_stores(void)
{
    static char is_dead[MAX_LINES];
    int last_store[256];  /* last_store[offset+128] = line index; -1 = none */
    int i, idx, changed;
    char tmp[MAX_LINE];
    const char *p;
    char *endp;
    long v;
    int n;
    int escaped_from;

    memset(is_dead, 0, sizeof(is_dead));
    memset(last_store, -1, sizeof(last_store));
    changed = 0;
    escaped_from = 128;

    for (i = 0; i < nlines; i++) {
        strip_peep_comment_copy(tmp, lines[i]);
        n = (int)strlen(tmp);

        /* Any label ending in ':' that is not an internal 'L<digits>:' label
           is a segment boundary (new function or data).  Reset all state. */
        if (n > 1 && tmp[n-1] == ':' && tmp[0] != ' ' && tmp[0] != '\t') {
            /* Check whether it is an internal local label L\d+: */
            int is_local = 0;
            if (tmp[0] == 'L') {
                int j;
                is_local = 1;
                for (j = 1; j < n-1; j++)
                    if (tmp[j] < '0' || tmp[j] > '9') { is_local = 0; break; }
            }
            if (!is_local) {
                /* Segment boundary: flush (remaining stores from previous
                   segment are addressed by the old IX — not our problem) */
                memset(last_store, -1, sizeof(last_store));
                escaped_from = 128;
            } else {
                /* Internal label: conservative flush — a branch from elsewhere
                   might rely on a pending store being present. Keep
                   escaped_from: an address saved in a pointer remains escaped
                   across control flow for the rest of this function. */
                memset(last_store, -1, sizeof(last_store));
            }
            continue;
        }

        /* ret: end of function — any pending store was never read → dead */
        if (strcmp(tmp, "ret") == 0) {
            for (idx = 0; idx < 256; idx++)
                if (last_store[idx] >= 0)
                    is_dead[last_store[idx]] = 1;
            memset(last_store, -1, sizeof(last_store));
            escaped_from = 128;
            continue;
        }

        /* Jump instructions: flush — can't prove overwrite on all paths */
        if (strncmp(tmp, "jp ", 3) == 0 || strncmp(tmp, "jr ", 3) == 0 ||
            strncmp(tmp, "djnz ", 5) == 0) {
            memset(last_store, -1, sizeof(last_store));
            continue;
        }

        /* Once HL holds a frame address, assembly text cannot prove whether
         * the pointer is used directly, saved, or retained by a call. Flush
         * pending stores and protect all later direct stores from the lowest
         * offset visited while constructing that address. */
        {
            long lowest_offset;
            if (scan_ix_frame_addr(i, &lowest_offset)) {
                memset(last_store, -1, sizeof(last_store));
                if ((int)lowest_offset < escaped_from)
                    escaped_from = (int)lowest_offset;
            }
        }

        /* A direct store to escaped frame storage remains removable until an
         * indirect access or call could observe it. */
        if (may_access_escaped_frame(tmp)) {
            int first_escaped = escaped_from < -128 ? 0 : escaped_from + 128;
            for (idx = first_escaped; idx < 256; ++idx)
                last_store[idx] = -1;
        }

        /* Check whether this instruction touches an IX-indexed address */
        if (strstr(tmp, "(ix") == NULL)
            continue;

        if (strncmp(tmp, "ld (ix", 6) == 0) {
            /* Store: ld (ix+N),something  — track as pending store */
            p = tmp + 6;
            v = strtol(p, &endp, 0);
            if (*endp == ')' && *(endp+1) == ',' && v >= -128 && v <= 127) {
                idx = (int)v + 128;
                if (last_store[idx] >= 0)
                    is_dead[last_store[idx]] = 1;  /* overwritten → dead */
                last_store[idx] = i;
            }
        } else {
            /* Any other use of (ix+N): mark the pending store as live */
            p = strstr(tmp, "(ix");
            if (p) {
                p += 3;
                if (*p == '+' || *p == '-' ||
                    (*p >= '0' && *p <= '9')) {
                    v = strtol(p, &endp, 0);
                    if (*endp == ')' && v >= -128 && v <= 127) {
                        idx = (int)v + 128;
                        last_store[idx] = -1;  /* store is live */
                    }
                }
            }
        }
    }

    /* Delete dead stores in reverse line order to preserve lower indices */
    for (i = nlines - 1; i >= 0; i--) {
        if (is_dead[i]) {
            delete_n(i, 1);
            changed = 1;
        }
    }

    return changed;
}

/*
 * Eliminate: store 32-bit DEHL to (ix+N)..(ix+N+3) immediately followed by
 * reload of the same four bytes back into l/h/e/d.  Since stores to memory do
 * not change the source registers, DEHL still holds the correct value after
 * the stores and the reload is redundant.
 *
 *   ld (ix+N),l      |  ld (ix+N),l
 *   ld (ix+N+1),h    |  ld (ix+N+1),h
 *   ld (ix+N+2),e    |  ld (ix+N+2),e
 *   ld (ix+N+3),d    |  ld (ix+N+3),d
 *   ld l,(ix+N)      |  (deleted)
 *   ld h,(ix+N+1)    |  (deleted)
 *   ld e,(ix+N+2)    |  (deleted)
 *   ld d,(ix+N+3)    |  (deleted)
 *
 * This fires heavily in 32-bit (long) expression code where the compiler
 * spills an intermediate to a local and then immediately reloads it.
 */
static int peep_is_harmless_between_store_reload(const char *s)
{
    char tmp[MAX_LINE];

    strip_peep_comment_copy(tmp, s);

    if (tmp[0] == 0 || tmp[0] == ';')
        return 1;

    /*
     * These stack-cleanup / assembler-directive lines do not alter DEHL and
     * do not read or write the stored local slots.  Keep this whitelist tiny:
     * the optimization is only intended to bridge the common "store; pop bc;
     * reload" shape after helper-call cleanup.
     */
    if (strcmp(tmp, "pop bc") == 0)
        return 1;
    if (strcmp(tmp, "push bc") == 0)
        return 1;
    if (strcmp(tmp, "inc sp") == 0)
        return 1;
    if (strncmp(tmp, "extrn ", 6) == 0)
        return 1;

    return 0;
}

static int peep_match_long_reload_at(int i,
                                     const char *off0,
                                     const char *off1,
                                     const char *off2,
                                     const char *off3)
{
    char expect[MAX_LINE];

    if (i + 3 >= nlines)
        return 0;

    sprintf(expect, "ld l,(ix%s)", off0);
    if (!eq(i, expect)) return 0;
    sprintf(expect, "ld h,(ix%s)", off1);
    if (!eq(i + 1, expect)) return 0;
    sprintf(expect, "ld e,(ix%s)", off2);
    if (!eq(i + 2, expect)) return 0;
    sprintf(expect, "ld d,(ix%s)", off3);
    if (!eq(i + 3, expect)) return 0;

    return 1;
}

/*
 * Eliminate: store 32-bit DEHL to (ix+N)..(ix+N+3) followed shortly by
 * reload of the same four bytes back into l/h/e/d.  Since stores to memory do
 * not change the source registers, DEHL still holds the correct value after
 * the stores and the reload is redundant.
 *
 * Original adjacent form:
 *
 *   ld (ix+N),l
 *   ld (ix+N+1),h
 *   ld (ix+N+2),e
 *   ld (ix+N+3),d
 *   ld l,(ix+N)       ; deleted
 *   ld h,(ix+N+1)     ; deleted
 *   ld e,(ix+N+2)     ; deleted
 *   ld d,(ix+N+3)     ; deleted
 *
 * Extended safe form allows only a tiny whitelist between the store and reload,
 * e.g. caller cleanup:
 *
 *   pop bc
 *
 * Anything else could clobber DEHL, change control flow, or touch the local
 * slots, so the pass refuses to fire.
 */
static int pass_elim_long_store_reload(void)
{
    int i, j, changed, ival, k;
    char tmp[MAX_LINE];
    char off0[32], off1[32], off2[32], off3[32];
    char expect[MAX_LINE];
    const char *p;
    int max_j;

    changed = 0;

    for (i = 0; i + 7 < nlines; i++) {
        /* Match first store: ld (ix+N),l */
        strip_peep_comment_copy(tmp, lines[i]);
        if (strncmp(tmp, "ld (ix", 6) != 0)
            continue;
        p = tmp + 6;
        k = 0;
        while (*p && *p != ')' && k < 30)
            off0[k++] = *p++;
        off0[k] = 0;
        if (*p != ')' || p[1] != ',' || p[2] != 'l' || p[3] != 0 || k == 0)
            continue;

        /* Compute adjacent offsets */
        ival = (int)strtol(off0, NULL, 0);
        peep_format_ix_off(off1, ival + 1);
        peep_format_ix_off(off2, ival + 2);
        peep_format_ix_off(off3, ival + 3);

        /* Check remaining 3 stores */
        sprintf(expect, "ld (ix%s),h", off1);
        if (!eq(i + 1, expect)) continue;
        sprintf(expect, "ld (ix%s),e", off2);
        if (!eq(i + 2, expect)) continue;
        sprintf(expect, "ld (ix%s),d", off3);
        if (!eq(i + 3, expect)) continue;

        /*
         * Look for the reload either immediately or after a few harmless lines.
         * Keep the search window deliberately small; if the compiler starts
         * emitting more complex code between store/reload, that should be
         * handled by a separate data-flow pass, not this peephole.
         */
        max_j = i + 10;
        if (max_j + 3 >= nlines)
            max_j = nlines - 4;

        for (j = i + 4; j <= max_j; j++) {
            if (peep_match_long_reload_at(j, off0, off1, off2, off3)) {
                delete_n(j, 4);
                changed = 1;
                break;
            }

            if (!peep_is_harmless_between_store_reload(lines[j]))
                break;
        }
    }

    return changed;
}

/*
 * pass_skip_ix_reload_across_label:
 *
 * A 32-bit DEHL store to (ix+N)..(ix+N+3) that falls straight into a label
 * which is immediately followed by the matching reload of those same four
 * bytes:
 *
 *   ld (ix+N),l
 *   ld (ix+N+1),h
 *   ld (ix+N+2),e
 *   ld (ix+N+3),d
 *   L1:
 *       ld l,(ix+N)
 *       ld h,(ix+N+1)
 *       ld e,(ix+N+2)
 *       ld d,(ix+N+3)
 *
 * pass_elim_long_store_reload refuses to touch this shape because L1 may be
 * a branch target: some other predecessor jumps straight to L1 without
 * having just stored DEHL, so the reload is genuinely needed on that path.
 * But DEHL still holds the stored value on the fall-through path (stores
 * don't change the source registers), so that path can jump straight past
 * the reload instead of redoing it:
 *
 *   ld (ix+N),l
 *   ld (ix+N+1),h
 *   ld (ix+N+2),e
 *   ld (ix+N+3),d
 *   jr Lskip            ; new
 *   L1:
 *       ld l,(ix+N)
 *       ld h,(ix+N+1)
 *       ld e,(ix+N+2)
 *       ld d,(ix+N+3)
 *   Lskip:               ; new
 *
 * The jump-in path is untouched. Saves four ix-relative loads on the
 * fall-through path at the cost of one always-taken jr.
 */
static int pass_skip_ix_reload_across_label(void)
{
    int i, ival, k, changed;
    char tmp[MAX_LINE], expect[MAX_LINE];
    char off0[32], off1[32], off2[32], off3[32];
    char lab[128];
    const char *p;

    changed = 0;

    for (i = 0; i + 8 < nlines; i++) {
        strip_peep_comment_copy(tmp, lines[i]);
        if (strncmp(tmp, "ld (ix", 6) != 0)
            continue;
        p = tmp + 6;
        k = 0;
        while (*p && *p != ')' && k < 30)
            off0[k++] = *p++;
        off0[k] = 0;
        if (*p != ')' || p[1] != ',' || p[2] != 'l' || p[3] != 0 || k == 0)
            continue;

        ival = (int)strtol(off0, NULL, 0);
        peep_format_ix_off(off1, ival + 1);
        peep_format_ix_off(off2, ival + 2);
        peep_format_ix_off(off3, ival + 3);

        sprintf(expect, "ld (ix%s),h", off1);
        if (!eq(i + 1, expect)) continue;
        sprintf(expect, "ld (ix%s),e", off2);
        if (!eq(i + 2, expect)) continue;
        sprintf(expect, "ld (ix%s),d", off3);
        if (!eq(i + 3, expect)) continue;

        /* Must fall straight into a label ... */
        if (!label_name_at(i + 4, lab))
            continue;

        /* ... immediately followed by the matching reload. */
        if (!peep_match_long_reload_at(i + 5, off0, off1, off2, off3))
            continue;

        {
            char skip_label[160], jr_line[192], skip_def[168];

            /* "Lskrl_", not "Lpeep_skiprl_": M80 truncates non-public
             * labels to 16 significant characters (confirmed empirically -
             * not documented anywhere in the M80/L80 manual we have), and
             * "Lpeep_skiprl_" alone is already 13 of those, leaving just 3
             * digits of headroom before two different line-index suffixes
             * (e.g. i=160 and i=1609) silently collide into the same
             * symbol. Real-world impact isn't just an assembler error: M80
             * still emits a .REL despite the "multiply defined" fatals, so
             * the "jr" can end up wired to whichever definition won the
             * collision - confirmed as a silent miscompile on tests/pihex.c
             * (hangs after 2 lines of output instead of completing) when
             * assembled with the real M80.COM/M80.EXE, though not with the
             * project's m80c, which doesn't enforce this limit and let it
             * through unnoticed. Same fix applied to every other
             * Lpeep_..._%d generator below - see the shared rationale here. */
            sprintf(skip_label, "Lskrl_%d", i);
            sprintf(jr_line, "jr %s", skip_label);
            sprintf(skip_def, "%s:", skip_label);

            insert_line_tagged(i + 4, jr_line, "skip_ix_reload_across_label");
            insert_line(i + 10, skip_def);
        }

        changed = 1;
    }

    return changed;
}

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

static int peep_is_public_line(const char *s)
{
    return strncmp(s, "public ", 7) == 0;
}

/*
 * In tiny posNfunc helpers, cache the selected board byte in B instead of
 * a one-byte stack local at ix-1.  These helpers make no calls, so B is safe.
 */



static int line_clobbers_bc(const char *line);

static int pass_posfunc_b_cache(void)
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

/* Is `line` unsafe to assume BC is free across it - i.e. could it clobber
 * B, C, or BC? Three independent hazards:
 *   - "call"/"rst": can clobber BC through whatever they invoke.
 *   - djnz, exx, and block instructions use or replace B/BC implicitly
 *     without spelling out "b" or "bc" in their operand text - a plain
 *     register-name text search would miss these.
 *   - an explicit "b"/"c"/"bc" register-name token anywhere else in the
 *     function body.
 *
 * "call __stchk" is explicitly exempted from the call check: -fstack-check
 * inserts it at the top of every function (the default build config, so
 * without this exemption no BC-caching pass could ever fire at all), and
 * DCCRTL.MAC shows it never touches B or C on any path that returns to the
 * caller - its fast/common path (no overflow) only uses HL/DE/AF, and its
 * overflow path, which does use C for a BDOS print call, ends in
 * "jp 0000h" (CP/M warm boot) without ever returning - so a clobbered C
 * there is never observed by anything. */
static int line_clobbers_bc(const char *line)
{
    char clean[MAX_LINE];
    const char *p;

    strip_peep_comment_lower_copy(clean, line);

    if ((strncmp(clean, "rst", 3) == 0 &&
         (clean[3] == ' ' || clean[3] == '\t')) ||
        strncmp(clean, "djnz", 4) == 0 || strcmp(clean, "exx") == 0 ||
        strcmp(clean, "ldi") == 0 || strcmp(clean, "ldd") == 0 ||
        strcmp(clean, "cpi") == 0 || strcmp(clean, "cpd") == 0 ||
        strcmp(clean, "ini") == 0 || strcmp(clean, "ind") == 0 ||
        strcmp(clean, "outi") == 0 || strcmp(clean, "outd") == 0 ||
        strcmp(clean, "ldir") == 0 || strcmp(clean, "lddr") == 0 ||
        strcmp(clean, "cpir") == 0 || strcmp(clean, "cpdr") == 0 ||
        strcmp(clean, "inir") == 0 || strcmp(clean, "indr") == 0 ||
        strcmp(clean, "otir") == 0 || strcmp(clean, "otdr") == 0)
        return 1;

    if (strncmp(clean, "call", 4) == 0 &&
        (clean[4] == ' ' || clean[4] == '\t') &&
        strcmp(clean, "call __stchk") != 0)
        return 1;

    p = clean;
    while (*p) {
        if (isalnum((unsigned char)*p) || *p == '_') {
            const char *start = p;
            int n = 0;
            while (*p && (isalnum((unsigned char)*p) || *p == '_')) { p++; n++; }
            if (n == 1 && (*start == 'b' || *start == 'c'))
                return 1;
            if (n == 2 && start[0] == 'b' && start[1] == 'c')
                return 1;
        } else {
            p++;
        }
    }
    return 0;
}

/* pass_cache_noix_byte_param_reload additionally needs SP to be stable (it
 * caches an SP-relative address, not just a value), so push/pop - which
 * don't clobber BC but do shift SP - are hazards there even though they
 * aren't for a plain register-value cache like
 * pass_cache_global_word_reload's. */
static int line_could_use_bc(const char *line)
{
    char clean[MAX_LINE];

    if (line_clobbers_bc(line))
        return 1;

    strip_peep_comment_copy(clean, line);
    return strncmp(clean, "push ", 5) == 0 || strncmp(clean, "pop ", 4) == 0;
}

/* Recognizes both known "read a stack parameter via SP-relative addressing"
 * shapes a no-IX-frame function emits, immediately following the common
 * "ld hl,OFFSET / add hl,sp" address computation:
 *   - byte parameter:  ld l,(hl)                                (1 line)
 *   - int  parameter:  ld a,(hl) / inc hl / ld h,(hl) / ld l,a  (4 lines)
 * Both leave the SAME (H,L) pair - the parameter's value - which is what
 * makes one cache-and-reuse strategy work for either shape. Returns the
 * total line count of "ld hl,OFFSET/add hl,sp" plus the matched
 * continuation (3 for byte, 6 for int), or 0 if neither matches. */
static int match_noix_param_read(int i, int fend)
{
    if (i + 1 >= fend || !eq(i + 1, "add hl,sp"))
        return 0;
    if (i + 2 < fend && eq(i + 2, "ld l,(hl)"))
        return 3;
    if (i + 5 < fend && eq(i + 2, "ld a,(hl)") && eq(i + 3, "inc hl") &&
        eq(i + 4, "ld h,(hl)") && eq(i + 5, "ld l,a"))
        return 6;
    return 0;
}

/* Every "ld hl,off" in [fstart,fend) must match the expected read shape
 * (len) - not just the ones this pass already found. A literal `off` used
 * any other way here - most importantly a *write* through the same
 * computed address, which would make a cached copy stale, but also just a
 * same-valued constant used for an unrelated purpose - means this
 * candidate cannot be trusted. */
static int offset_used_only_as_expected_read(int fstart, int fend, int off, int len)
{
    int i;
    char valbuf[128];
    int v;

    for (i = fstart; i < fend; i++) {
        if (!parse_ld_hl_imm(lines[i], valbuf)) continue;
        if (!parse_nonneg_int(valbuf, &v)) continue;
        if (v != off) continue;
        if (match_noix_param_read(i, fend) != len) return 0;
    }
    return 1;
}

/*
 * pass_cache_noix_byte_param_reload:
 *
 * A no-IX-frame function accesses a stack parameter via:
 *   ld hl,OFFSET
 *   add hl,sp
 *   <byte or int load - see match_noix_param_read>
 * recomputed from scratch at EVERY reference - even when the same
 * parameter is read more than once in the function body (e.g. tests/
 * tchess.c's piece_side: separate 'A'-'Z' and 'a'-'z' range checks each
 * reload the parameter; abs_i: sign check + negation both reload it).
 * Since SP is provably unchanged between two such sequences whenever the
 * function contains no push/pop/call anywhere - exactly the property that
 * makes SP-relative addressing sound in the first place, see dcc_func.c's
 * tmpfile_unsafe_for_noix - the (H,L) pair this sequence produces is
 * bit-for-bit identical every time it appears for the same OFFSET.
 *
 * Cache it in BC after the first occurrence (verified unused anywhere else
 * in the function, and that OFFSET is never used any other way - see the
 * two helpers above) and replace every later occurrence with a 2-
 * instruction register copy instead of a fresh recomputation.
 *
 * Scoped to one cached offset per function (only one spare register pair
 * is claimed): the offset with the most repeated occurrences wins if a
 * function has more than one multiply-referenced parameter.
 */
static int line_starts_function_marker(const char *line)
{
    return peep_is_public_line(line) || strncmp(line, "; static function ", 18) == 0;
}

/* Forward declarations: full definitions (and their shared header comment)
 * live near pass_cache_global_word_reload below, the pass that originally
 * motivated them - but bc_regalloc_claimed_before is needed by several
 * passes defined earlier in the file than that. */
static int line_is_regalloc_bc_priming(const char *line);
static int bc_regalloc_claimed_before(int at);

static int pass_cache_noix_byte_param_reload(void)
{
    int fstart, fend;
    int changed = 0;

    fstart = 0;
    while (fstart < nlines && !line_starts_function_marker(lines[fstart]))
        fstart++;

    while (fstart < nlines) {
        int i, j;
        int off, mlen;
        int best_off = -1, best_count = 0, best_len = 0;
        struct { int offset; int count; int len; } seen[32];
        int nseen = 0;

        fend = fstart + 1;
        while (fend < nlines && !line_starts_function_marker(lines[fend]))
            fend++;

        for (i = fstart; i < fend; i++) {
            char valbuf[128];
            if (!parse_ld_hl_imm(lines[i], valbuf)) continue;
            if (!parse_nonneg_int(valbuf, &off)) continue;
            mlen = match_noix_param_read(i, fend);
            if (mlen == 0) continue;

            for (j = 0; j < nseen; j++)
                if (seen[j].offset == off) break;
            if (j == nseen) {
                if (nseen < 32) {
                    seen[nseen].offset = off;
                    seen[nseen].count = 1;
                    seen[nseen].len = mlen;
                    nseen++;
                }
            } else {
                seen[j].count++;
                /* Same offset, different shape than before: one C variable
                 * can't have two types, so this should never happen - but
                 * if it somehow did, refuse the offset rather than guess. */
                if (seen[j].len != mlen)
                    seen[j].len = -1;
            }
        }

        for (j = 0; j < nseen; j++) {
            if (seen[j].len > 0 && seen[j].count > best_count) {
                best_count = seen[j].count;
                best_off = seen[j].offset;
                best_len = seen[j].len;
            }
        }

        if (best_count >= 2 && offset_used_only_as_expected_read(fstart, fend, best_off, best_len)) {
            int safe = 1;
            int occ[64];
            int noc = 0;
            int k;

            for (i = fstart; i < fend; i++) {
                if (line_could_use_bc(lines[i])) { safe = 0; break; }
            }

            if (safe) {
                for (i = fstart; i < fend; i++) {
                    char valbuf[128];
                    if (!parse_ld_hl_imm(lines[i], valbuf)) continue;
                    if (!parse_nonneg_int(valbuf, &off)) continue;
                    if (off != best_off) continue;
                    if (match_noix_param_read(i, fend) != best_len) continue;
                    if (noc < 64) occ[noc++] = i;
                }

                /* Textual order isn't execution order: a branch can reach a
                 * later occurrence without ever running through occ[0], the
                 * point the cache gets stored - e.g. tests/cint.c's
                 * store_op(sc,esz,arr) reads esz on two sibling branches
                 * (arr true vs arr false) of an early "if (arr)"; the first
                 * occurrence establishes the cache only on the arr-true
                 * side, and the arr-false side's own occurrence, reached by
                 * jumping past the store entirely via its branch label,
                 * loaded garbage from an never-written BC - a real
                 * miscompile (confirmed: cint.c interpreting sieve.c hung
                 * forever, load_op picking the wrong opcode from that
                 * garbage). A label anywhere between occ[0] and a later
                 * occurrence means some OTHER point in the function can
                 * jump directly into that range, bypassing the store, so
                 * refuse the whole optimization for this function rather
                 * than risk it - occ[0] is always the earliest occurrence,
                 * so this only needs one scan from occ[0] to the last one. */
                for (i = occ[0]; i < occ[noc - 1]; i++) {
                    if (starts_label(lines[i])) { safe = 0; break; }
                }

                if (safe) {
                /* Last occurrence first: delete_n only ever shifts indices
                 * strictly after the edit point, so earlier (not yet
                 * processed) entries in occ[], including occ[0], stay valid. */
                for (k = noc - 1; k >= 1; k--) {
                    replace1_tagged(occ[k], "ld l,c", "noix_param_cache_load");
                    replace1(occ[k] + 1, "ld h,b");
                    delete_n(occ[k] + 2, best_len - 2);
                    fend -= (best_len - 2);
                    changed = 1;
                }

                insert_line_tagged(occ[0] + best_len, "ld c,l", "noix_param_cache_store");
                insert_line(occ[0] + best_len + 1, "ld b,h");
                fend += 2;
                changed = 1;
                }
            }
        }

        fstart = fend;
    }

    return changed;
}




static int peep_parse_ld_hl_0_to_255(const char *s, int *out)
{
    char *endp;
    long v;
    char tmp[MAX_LINE];

    strip_peep_comment_copy(tmp, s);

    if (strncmp(tmp, "ld hl,", 6) != 0)
        return 0;

    v = strtol(tmp + 6, &endp, 0);
    while (*endp == ' ' || *endp == '\t')
        endp++;

    if (*endp != 0 || v < 0 || v > 255)
        return 0;

    out[0] = (int)v;
    return 1;
}

static int peep_parse_ld_h_ix(const char *s, char *off)
{
    char tmp[MAX_LINE];
    const char *p;
    int i;

    strip_peep_comment_copy(tmp, s);

    if (strncmp(tmp, "ld h,(ix", 8) != 0)
        return 0;

    p = tmp + 8;
    i = 0;
    while (*p && *p != ')' && i < 31)
        off[i++] = *p++;
    off[i] = 0;

    if (*p != ')' || p[1] != 0)
        return 0;

    return i > 0;
}





static int peep_parse_ld_e_ix(const char *s, char *off)
{
    char tmp[MAX_LINE];
    const char *p;
    int i;
    strip_peep_comment_copy(tmp, s);
    if (strncmp(tmp, "ld e,(ix", 8) != 0) return 0;
    p = tmp + 8; i = 0;
    while (*p && *p != ')' && i < 31) off[i++] = *p++;
    off[i] = 0;
    if (*p != ')' || p[1] != 0) return 0;
    return i > 0;
}

static int peep_parse_ld_d_ix(const char *s, char *off)
{
    char tmp[MAX_LINE];
    const char *p;
    int i;
    strip_peep_comment_copy(tmp, s);
    if (strncmp(tmp, "ld d,(ix", 8) != 0) return 0;
    p = tmp + 8; i = 0;
    while (*p && *p != ')' && i < 31) off[i++] = *p++;
    off[i] = 0;
    if (*p != ')' || p[1] != 0) return 0;
    return i > 0;
}

static int peep_parse_ld_ix_pair(const char *s1, const char *s2, int *off)
{
    char loff[32];
    char hoff[32];
    int lo;
    int hi;
    char *endp;

    if (!peep_parse_ld_l_ix(s1, loff))
        return 0;
    if (!peep_parse_ld_h_ix(s2, hoff))
        return 0;

    lo = (int)strtol(loff, &endp, 10);
    if (*endp != 0)
        return 0;
    hi = (int)strtol(hoff, &endp, 10);
    if (*endp != 0)
        return 0;
    if (hi != lo + 1)
        return 0;

    *off = lo;
    return 1;
}


static int peep_parse_st_ix_pair(const char *s1, const char *s2, int *off)
{
    char tmp1[MAX_LINE];
    char tmp2[MAX_LINE];
    char *p;
    char *endp;
    int lo;
    int hi;

    strip_peep_comment_copy(tmp1, s1);
    strip_peep_comment_copy(tmp2, s2);

    if (strncmp(tmp1, "ld (ix", 6) != 0)
        return 0;
    p = tmp1 + 6;
    lo = (int)strtol(p, &endp, 10);
    if (*endp != ')' || endp[1] != ',' || endp[2] != 'l' || endp[3] != 0)
        return 0;

    if (strncmp(tmp2, "ld (ix", 6) != 0)
        return 0;
    p = tmp2 + 6;
    hi = (int)strtol(p, &endp, 10);
    if (*endp != ')' || endp[1] != ',' || endp[2] != 'h' || endp[3] != 0)
        return 0;

    if (hi != lo + 1)
        return 0;

    *off = lo;
    return 1;
}

static int peep_parse_jp_same_z_c(int iz, int ic, char *lab)
{
    char lab2[128];

    if (!peep_parse_jp_cond_label(lines[iz], "z", lab))
        return 0;
    if (!peep_parse_jp_cond_label(lines[ic], "c", lab2))
        return 0;
    return strcmp(lab, lab2) == 0;
}

static int pass_e_signed_le_zero(void)
{
    int i;
    int changed;
    int off;
    char lab[128];
    char line[160];

    changed = 0;

    for (i = 0; i + 12 < nlines; ++i) {
        if (peep_parse_ld_ix_pair(lines[i], lines[i + 1], &off) &&
            eq(i + 2, "ld de,0") &&
            eq(i + 3, "ld a,h") &&
            eq(i + 4, "xor 80h") &&
            eq(i + 5, "ld h,a") &&
            eq(i + 6, "ld a,d") &&
            eq(i + 7, "xor 80h") &&
            eq(i + 8, "ld d,a") &&
            eq(i + 9, "or a") &&
            eq(i + 10, "sbc hl,de") &&
            peep_parse_jp_same_z_c(i + 11, i + 12, lab)) {
            replace1_tagged(i, lines[i], "signed_le_zero");
            replace1(i + 1, lines[i + 1]);
            replace1(i + 2, "ld a,h");
            replace1(i + 3, "or l");
            sprintf(line, "jp z, %s", lab);
            replace1(i + 4, line);
            replace1(i + 5, "bit 7,h");
            sprintf(line, "jp nz, %s", lab);
            replace1(i + 6, line);
            delete_n(i + 7, 6);
            changed = 1;
            if (i > 0) --i;
        }
    }

    return changed;
}

static int pass_ix_array_word_addr(void)
{
    int i;
    int changed;
    int baseoff;
    int idxoff;
    int step;
    int j;
    char line[160];

    changed = 0;

    for (i = 0; i + 10 < nlines; ++i) {
        if (eq(i, "push ix") &&
            eq(i + 1, "pop hl") &&
            peep_parse_ld_de_signed(lines[i + 2], &baseoff) &&
            eq(i + 3, "add hl,de") &&
            eq(i + 4, "push hl") &&
            peep_parse_ld_ix_pair(lines[i + 5], lines[i + 6], &idxoff)) {
            j = i + 7;
            step = 0;
            if (eq(j, "dec hl")) {
                step = -1;
                j++;
            } else if (eq(j, "inc hl")) {
                step = 1;
                j++;
            }

            if (eq(j, "add hl,hl") &&
                eq(j + 1, "ex de,hl") &&
                eq(j + 2, "pop hl") &&
                eq(j + 3, "add hl,de")) {
                replace1_tagged(i, lines[i + 5], "ix_array_word_addr");
                replace1(i + 1, lines[i + 6]);
                if (step < 0)
                    replace1(i + 2, "dec hl");
                else if (step > 0)
                    replace1(i + 2, "inc hl");
                else
                    replace1(i + 2, "add hl,hl");

                if (step != 0)
                    replace1(i + 3, "add hl,hl");
                else
                    replace1(i + 3, "push ix");
                if (step != 0)
                    replace1(i + 4, "push ix");
                else
                    replace1(i + 4, "pop de");
                if (step != 0)
                    replace1(i + 5, "pop de");
                else
                    replace1(i + 5, "add hl,de");
                if (step != 0)
                    replace1(i + 6, "add hl,de");
                else {
                    sprintf(line, "ld de,%d", baseoff);
                    replace1(i + 6, line);
                }
                if (step != 0) {
                    sprintf(line, "ld de,%d", baseoff);
                    replace1(i + 7, line);
                } else {
                    replace1(i + 7, "add hl,de");
                }
                if (step != 0)
                    replace1(i + 8, "add hl,de");

                if (step != 0)
                    delete_n(i + 9, (j + 4) - (i + 9));
                else
                    delete_n(i + 8, (j + 4) - (i + 8));

                changed = 1;
                if (i > 0) --i;
            }
        }
    }

    return changed;
}




/* Byte-array counterpart of pass_ix_array_word_addr: same canonical raw
 * shape, minus the "add hl,hl" doubling step (a byte array's stride is 1,
 * so the index needs no scaling before being added to the base address).
 * Matches:
 *   push ix
 *   pop hl
 *   ld de,BASEOFF
 *   add hl,de
 *   push hl
 *   ld l,(ix+O) / ld h,(ix+O+1)
 *   [dec hl | inc hl]        (optional index adjustment)
 *   ex de,hl
 *   pop hl
 *   add hl,de
 * and rewrites in place to the canonical block:
 *   ld l,(ix+O)
 *   ld h,(ix+O+1)
 *   [dec hl | inc hl]
 *   push ix
 *   pop de
 *   add hl,de
 *   ld de,ARROFF
 *   add hl,de */
static int pass_ix_array_byte_addr(void)
{
    int i;
    int changed;
    int baseoff;
    int idxoff;
    int step;
    int j;
    char line[160];

    changed = 0;

    for (i = 0; i + 9 < nlines; ++i) {
        if (eq(i, "push ix") &&
            eq(i + 1, "pop hl") &&
            peep_parse_ld_de_signed(lines[i + 2], &baseoff) &&
            eq(i + 3, "add hl,de") &&
            eq(i + 4, "push hl") &&
            peep_parse_ld_ix_pair(lines[i + 5], lines[i + 6], &idxoff)) {
            j = i + 7;
            step = 0;
            if (eq(j, "dec hl")) {
                step = -1;
                j++;
            } else if (eq(j, "inc hl")) {
                step = 1;
                j++;
            }

            if (eq(j, "ex de,hl") &&
                eq(j + 1, "pop hl") &&
                eq(j + 2, "add hl,de")) {
                replace1_tagged(i, lines[i + 5], "ix_array_byte_addr");
                replace1(i + 1, lines[i + 6]);
                if (step < 0)
                    replace1(i + 2, "dec hl");
                else if (step > 0)
                    replace1(i + 2, "inc hl");
                else
                    replace1(i + 2, "push ix");

                if (step != 0)
                    replace1(i + 3, "push ix");
                else
                    replace1(i + 3, "pop de");
                if (step != 0)
                    replace1(i + 4, "pop de");
                else
                    replace1(i + 4, "add hl,de");
                if (step != 0)
                    replace1(i + 5, "add hl,de");
                else {
                    sprintf(line, "ld de,%d", baseoff);
                    replace1(i + 5, line);
                }
                if (step != 0) {
                    sprintf(line, "ld de,%d", baseoff);
                    replace1(i + 6, line);
                } else {
                    replace1(i + 6, "add hl,de");
                }
                if (step != 0)
                    replace1(i + 7, "add hl,de");

                if (step != 0)
                    delete_n(i + 8, (j + 3) - (i + 8));
                else
                    delete_n(i + 7, (j + 3) - (i + 7));

                changed = 1;
                if (i > 0) --i;
            }
        }
    }

    return changed;
}


/* Byte-array counterpart of pass_reuse_array_word_addr. Two array-byte-
 * address blocks for the SAME array and SAME index variable, separated only
 * by a push hl / <straight-line gap that reads but never writes the index
 * variable, no label> / pop hl / ld (hl),R (single-byte store through the
 * address) - the second address is recomputed from scratch even though HL,
 * right after that single-byte store, is unchanged (still exactly
 * &array[idxA]+stepA, since - unlike the word-store tail's "inc hl" - a
 * one-byte store never moves HL). Common in the same e.c shape as the word
 * version: `a[n] = x % n; ... a[n-1] ...` once `a` has been narrowed from
 * int to unsigned char. */

static int peep_parse_dec_ix_byte(const char *s, int *off)
{
    char tmp[MAX_LINE];
    char *p;
    char *endp;

    strip_peep_comment_copy(tmp, s);
    if (strncmp(tmp, "dec (ix", 7) != 0)
        return 0;
    p = tmp + 7;
    *off = (int)strtol(p, &endp, 10);
    if (*endp != ')' || endp[1] != 0)
        return 0;
    return 1;
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
static int pass_byte_loop_counter_to_reg_c(void)
{
    int i;
    int changed;
    int off;
    char label[128];
    char target[128];
    char tgt[128];
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
        k = (int)strlen(label);
        if (k > 0 && label[k - 1] == ':')
            label[k - 1] = 0;

        /* Find the matching loop-back jump to this same label, with no
         * other label in between (single-entry, single-exit body). */
        loop_end = -1;
        for (k = i + 3; k < nlines; ++k) {
            if (starts_label(lines[k]))
                break;
            if (is_uncond_jp(lines[k])) {
                if (jump_target(lines[k], tgt) && strcmp(tgt, label) == 0)
                    loop_end = k;
                break;
            }
        }
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
            if (eq(k, pat_lde) && eq(k + 1, "ld d,0")) {
                ++k;
                continue;
            }
            if (eq(k, pat_lhl) && eq(k + 1, "ld h,0")) {
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
         * invisible to a scan confined to [i+3, loop_end) alone. See
         * bc_regalloc_claimed_before's own comment; this is the same
         * collision class pass_cache_global_word_reload was fixed for. */
        if (bc_regalloc_claimed_before(i))
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

static int peep_parse_ld_ix_byte_imm(const char *s, int *off, int *val)
{
    const char *p;
    int sign;
    int o;
    int v;

    if (strncmp(s, "ld (ix", 6) != 0)
        return 0;
    p = s + 6;
    if (*p == '+') { sign = 1; p++; }
    else if (*p == '-') { sign = -1; p++; }
    else return 0;
    if (*p < '0' || *p > '9') return 0;
    o = 0;
    while (*p >= '0' && *p <= '9')
        o = o * 10 + (*p++ - '0');
    if (strncmp(p, "),", 2) != 0) return 0;
    p += 2;
    if (*p < '0' || *p > '9') return 0;
    v = 0;
    while (*p >= '0' && *p <= '9')
        v = v * 10 + (*p++ - '0');
    if (*p != 0) return 0;
    *off = sign * o;
    *val = v;
    return 1;
}

static int peep_parse_inc_ix_byte(const char *s, int *off);
static int peep_parse_cp_const(const char *s, int *val);
static int line_touches_bc(const char *s);
static int line_touches_de(const char *s);
static int line_touches_hl(const char *s);

/* This function's own boundaries: the most recent "public NAME" at or
 * before `from`, and the next "public NAME" after it (or nlines if this is
 * the last function in the file). Used to bound the label-reachability
 * check below to the current function only, so it can never be fooled by
 * a same-numbered label belonging to a different function. */
static void find_function_bounds(int from, int *func_start, int *func_end)
{
    int k;

    *func_start = 0;
    for (k = from; k >= 0; --k) {
        if (strncmp(lines[k], "public ", 7) == 0) { *func_start = k; break; }
    }
    *func_end = nlines;
    for (k = from + 1; k < nlines; ++k) {
        if (strncmp(lines[k], "public ", 7) == 0) { *func_end = k; break; }
    }
}

/* Same as find_function_bounds, but also recognizes "; static function "
 * (see emit_function_prologue) as a function boundary - a static
 * function's definition never emits a public line, so find_function_bounds
 * alone treats its whole body as still belonging to whichever public
 * function happens to precede it in the file. */
static void find_function_bounds_any(int from, int *func_start, int *func_end)
{
    int k;

    *func_start = 0;
    for (k = from; k >= 0; --k) {
        if (strncmp(lines[k], "public ", 7) == 0 ||
            strncmp(lines[k], "; static function ", 18) == 0) {
            *func_start = k;
            break;
        }
    }
    *func_end = nlines;
    for (k = from + 1; k < nlines; ++k) {
        if (strncmp(lines[k], "public ", 7) == 0 ||
            strncmp(lines[k], "; static function ", 18) == 0) {
            *func_end = k;
            break;
        }
    }
}

static int jump_target_any(const char *s, char *out);

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
static int pass_word_loop_var_to_reg_bc(void)
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
static int pass_byte_loop_var_to_reg_c(void)
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

/* True iff every jump anywhere in [scan_lo, scan_hi) that targets
 * `label_name` has its OWN line number inside [range_lo, range_hi).  Used
 * to admit an internal label into a loop's scanned body only when it is
 * purely an intra-loop if/early-return merge point - never a re-entry
 * point some other, unrelated code elsewhere in the same function jumps
 * into - which a bare "ignore every internal label" scan cannot tell
 * apart (see pass_hoist_index_ptr_to_bc's own history: an earlier,
 * unconditional version of that relaxation let the scan run past one
 * loop's real body into unrelated code and corrupted tests/cint.c and
 * tests/fint.c; this reachability check is what makes it safe). */
static int label_targeted_only_within(const char *label_name,
                                      int scan_lo, int scan_hi,
                                      int range_lo, int range_hi)
{
    int k;
    char tgt[128];

    for (k = scan_lo; k < scan_hi; ++k) {
        if (jump_target(lines[k], tgt) && strcmp(tgt, label_name) == 0) {
            if (k < range_lo || k >= range_hi)
                return 0;
        }
    }
    return 1;
}

/* Validates every internal label within [lo, hi) via
 * label_targeted_only_within, bounded to the current function
 * (find_function_bounds). Returns 1 iff the whole range is safe to treat
 * as a single loop's straight-line-equivalent body. */
static int loop_body_internal_labels_safe(int lo, int hi)
{
    int func_start, func_end;
    int k;
    char inner[128];
    int n2;

    find_function_bounds(lo, &func_start, &func_end);
    for (k = lo; k < hi; ++k) {
        if (!starts_label(lines[k]))
            continue;
        strcpy(inner, lines[k]);
        n2 = (int)strlen(inner);
        if (n2 > 0 && inner[n2 - 1] == ':')
            inner[n2 - 1] = 0;
        if (!label_targeted_only_within(inner, func_start, func_end, lo, hi))
            return 0;
    }
    return 1;
}

/* Find the line index of the definition of label `name` within
 * [lo, hi), or -1 if not found. */
static int find_label_line(const char *name, int lo, int hi)
{
    int k;

    for (k = lo; k < hi; ++k)
        if (line_is_label_name(k, name))
            return k;
    return -1;
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
            pos = find_label_line(tgt, 0, func_end);
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
            int target_line = find_label_line(tgt, func_start, func_end);
            if (target_line >= lo && target_line < hi)
                continue;  /* jumps back into the loop's own range - fine */
            if (!escape_path_reaches_epilogue_safely(target_line, pat_ix, func_end))
                return 0;
        }
    }
    return 1;
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
static int pass_byte_for_counter_to_reg_c(void)
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
        k = (int)strlen(label);
        if (k > 0 && label[k - 1] == ':')
            label[k - 1] = 0;

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
            if (eq(k, pat_lde) && eq(k + 1, "ld d,0")) { ++k; continue; }
            if (eq(k, pat_lhl) && eq(k + 1, "ld h,0")) { ++k; continue; }
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
         * before this loop and still live here. See
         * bc_regalloc_claimed_before's own comment. */
        if (bc_regalloc_claimed_before(i))
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
static int pass_byte_for_counter_to_reg_e(void)
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
        k = (int)strlen(label);
        if (k > 0 && label[k - 1] == ':')
            label[k - 1] = 0;

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
        loop_end = -1;
        for (k = i + 1; k < nlines; ++k) {
            if (strncmp(lines[k], "public ", 7) == 0)
                break;
            if (jump_target(lines[k], tgt) && strcmp(tgt, label) == 0)
                loop_end = k;
        }
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
            if (eq(k, pat_lde) && eq(k + 1, "ld d,0")) {
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
            if (eq(k, pat_lhl) && eq(k + 1, "ld h,0")) { ++k; continue; }
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
 * IY is otherwise completely unused across dcc's own codegen and all of
 * DCCRTL.MAC (verified: zero occurrences), unlike BC which the codegen and
 * runtime use constantly. That makes it a second, near-unconditionally-safe
 * register slot for pass_byte_loop_counter_to_reg_iyl below - EXCEPT for
 * calls into another function in this SAME translation unit, which might
 * itself have one of its own loops promoted to IYL by this same pass and
 * would silently stomp this loop's live counter across the call. This scan
 * (run once, before the fixed-point pass loop) collects every function
 * entry-point label in the file so that pass can tell those calls apart
 * from RTL/library calls (which never touch IY).
 *
 * Matches the two shapes dcc_func.c's emit_function_prologue emits:
 *   public NAME       (non-static)      ; static function ORIGNAME (static)
 *   NAME:                               MANGLEDNAME:
 * In both cases the actual asm label used at call sites is on the line
 * immediately after the marker, so that's what gets collected - not the
 * original C name in the static-function comment.
 *
 * Residual gap: a multi-module ("-c -module") build where the callee lives
 * in a separately compiled-and-peepholed file is invisible to this per-file
 * scan. Every test in this repository's harness is single-module, so this
 * is a documented limitation, not a live bug against anything exercised
 * here.
 */
#define MAX_LOCAL_FUNC_LABELS 8192
static char local_func_labels[MAX_LOCAL_FUNC_LABELS][128];
static int n_local_func_labels;

static void scan_local_func_labels(void)
{
    int i;
    char name[128];
    int n;

    n_local_func_labels = 0;
    for (i = 0; i + 1 < nlines; ++i) {
        /* "; static function " is 18 characters, not 19 - an off-by-one
         * here meant this branch never matched (strncmp saw the real
         * function name's first character where the literal's implicit
         * NUL was, at n=19), so scan_local_func_labels only ever recorded
         * genuinely `public` functions, never `static` ones. That silently
         * defeated the whole cross-function IY-collision check for calls
         * between static functions - confirmed as the root cause of
         * tests/too.c's corrupted output under -fundocumented-z80:
         * gallery_init (static) calls hall_init (static) calls
         * exhibit_init (static), and all three independently claimed IYL
         * for their own loop, each stomping the others' live value. */
        if (strncmp(lines[i], "public ", 7) != 0 &&
            strncmp(lines[i], "; static function ", 18) != 0)
            continue;
        if (!starts_label(lines[i + 1]))
            continue;

        strncpy(name, lines[i + 1], sizeof(name) - 1);
        name[sizeof(name) - 1] = 0;
        n = (int)strlen(name);
        if (n > 0 && name[n - 1] == ':')
            name[n - 1] = 0;

        if (n_local_func_labels < MAX_LOCAL_FUNC_LABELS)
            strcpy(local_func_labels[n_local_func_labels++], name);
    }
}

static int is_local_func_label(const char *name)
{
    int i;
    for (i = 0; i < n_local_func_labels; ++i)
        if (!strcmp(local_func_labels[i], name))
            return 1;
    return 0;
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
 * Because nothing else touches IY (see scan_local_func_labels above), this
 * pass allows ANY call inside the loop body, not just __mods/__divs, except
 * one that is_local_func_label flags as another function in this same file
 * - declined exactly like pass_byte_loop_counter_to_reg_c declines a call
 * that isn't __mods/__divs.
 *
 * "ld l,(ix+off)" can't become a single "ld l,iyl": the FD prefix redirects
 * EVERY H/L reference in an instruction, so "ld l,iyl" would actually
 * encode "ld iyl,iyl" - there is no single-instruction undocumented form
 * that reads IYL into the real L register (E, unaffected by the H/L
 * substitution rule, has no such problem - "ld e,iyl" is a clean single
 * instruction). That whitelisted shape expands to two lines (the LD A,IYL
 * byte sequence, then "ld l,a") instead of a single-line replacement.
 */
static int pass_byte_loop_counter_to_reg_iyl(void)
{
    int i;
    int changed;
    int off;
    char label[128];
    char target[128];
    char tgt[128];
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

    for (i = 0; i + 2 < nlines; ++i) {
        if (!starts_label(lines[i]))
            continue;
        if (!peep_parse_dec_ix_byte(lines[i + 1], &off))
            continue;
        if (!parse_jp_cond_label(lines[i + 2], "z", target))
            continue;

        strcpy(label, lines[i]);
        k = (int)strlen(label);
        if (k > 0 && label[k - 1] == ':')
            label[k - 1] = 0;

        /* Find the matching loop-back jump to this same label, with no
         * other label in between (single-entry, single-exit body). */
        loop_end = -1;
        for (k = i + 3; k < nlines; ++k) {
            if (starts_label(lines[k]))
                break;
            if (is_uncond_jp(lines[k])) {
                if (jump_target(lines[k], tgt) && strcmp(tgt, label) == 0)
                    loop_end = k;
                break;
            }
        }
        if (loop_end < 0)
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
            if (strncmp(lines[k], "db 0FDh,", 8) == 0) {
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
            if (eq(k, pat_lde) && eq(k + 1, "ld d,0")) {
                ++k;
                continue;
            }
            if (eq(k, pat_lhl) && eq(k + 1, "ld h,0")) {
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

        changed = 1;
    }

    return changed;
}

static int peep_parse_inc_ix_byte(const char *s, int *off)
{
    char tmp[MAX_LINE];
    char *p;
    char *endp;

    strip_peep_comment_copy(tmp, s);
    if (strncmp(tmp, "inc (ix", 7) != 0)
        return 0;
    p = tmp + 7;
    *off = (int)strtol(p, &endp, 10);
    if (*endp != ')' || endp[1] != 0)
        return 0;
    return 1;
}

static int peep_parse_cp_const(const char *s, int *val)
{
    char tmp[MAX_LINE];
    char *endp;

    strip_peep_comment_copy(tmp, s);
    if (strncmp(tmp, "cp ", 3) != 0)
        return 0;
    *val = (int)strtol(tmp + 3, &endp, 10);
    if (*endp != 0)
        return 0;
    return 1;
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
 * Same IYL promotion and same call-safety rule (scan_local_func_labels/
 * is_local_func_label) as pass_byte_loop_counter_to_reg_iyl. The writeback
 * is nearly free here: IYLDA already has to reload the fresh value into A
 * for the "cp K" comparison, so one more "ld (ix+off),a" covers every
 * iteration's writeback at essentially no extra cost - unlike the
 * decrementing pass (and unlike the "ld l,(ix+off)" shape below, which
 * still needs its own dedicated two-line expansion for the same H/L-
 * substitution reason documented there).
 */
static int pass_byte_incr_loop_counter_to_reg_iyl(void)
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
            if (strncmp(lines[k], "db 0FDh,", 8) == 0) {
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
            if (eq(k, pat_lde) && eq(k + 1, "ld d,0")) {
                ++k;
                continue;
            }
            if (eq(k, pat_lhl) && eq(k + 1, "ld h,0")) {
                ++k;
                continue;
            }
            ok = 0;
        }
        if (!ok)
            continue;

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
        }

        changed = 1;
    }

    return changed;
}



/*
 * Return non-zero if a token equal to the d, e or de register appears in the
 * operand portion of an instruction line (comment already stripped).  Used to
 * detect reads of the DE register pair.  Mnemonic letters (e.g. the 'd' in
 * "dec" or the 'e' in "ex") are ignored because only operands are scanned.
 */
static int insn_mentions_de(const char *buf)
{
    const char *ops;
    const char *t;
    int len;

    /* Skip mnemonic (up to first space/tab). */
    ops = buf;
    while (*ops && *ops != ' ' && *ops != '\t')
        ops++;

    while (*ops) {
        /* Skip separators. */
        while (*ops && !(( *ops >= 'a' && *ops <= 'z') ||
                         (*ops >= 'A' && *ops <= 'Z')))
            ops++;
        if (!*ops)
            break;
        t = ops;
        while ((*ops >= 'a' && *ops <= 'z') ||
               (*ops >= 'A' && *ops <= 'Z') ||
               (*ops >= '0' && *ops <= '9'))
            ops++;
        len = (int)(ops - t);
        if ((len == 1 && (t[0] == 'd' || t[0] == 'e')) ||
            (len == 2 && t[0] == 'd' && t[1] == 'e'))
            return 1;
    }
    return 0;
}

/*
 * Return non-zero if the DE register pair is provably dead starting at line
 * `start`: i.e. it is fully redefined (ld de,.. / pop de) before any read of
 * D, E or DE, and before any control-flow or alternate-register boundary.
 * Conservative: anything uncertain (labels, branches, calls, exx) yields "not
 * dead" so callers refrain from deleting the instruction that set DE.
 */
static int peep_de_dead_at(int start)
{
    int k;
    char buf[MAX_LINE];

    for (k = start; k < nlines; ++k) {
        if (is_blank_or_comment(lines[k]))
            continue;

        strip_peep_comment_copy(buf, lines[k]);
        if (buf[0] == 0)
            continue;

        if (starts_label(buf))
            return 0;

        /* Full redefinition of DE without reading it => dead. */
        if (strncmp(buf, "ld de,", 6) == 0 || strcmp(buf, "pop de") == 0)
            return 1;

        /* Control-flow / alternate-register boundary: be conservative. */
        if (strncmp(buf, "jp", 2) == 0 || strncmp(buf, "jr", 2) == 0 ||
            strncmp(buf, "call", 4) == 0 || strncmp(buf, "ret", 3) == 0 ||
            strncmp(buf, "djnz", 4) == 0 || strncmp(buf, "rst", 3) == 0 ||
            strcmp(buf, "exx") == 0)
            return 0;

        /* Any read (or partial write) of D/E/DE keeps it live. */
        if (insn_mentions_de(buf))
            return 0;
    }

    return 1;
}

static int pass_store_word_const_hl(void)
{
    int i;
    int changed;
    int imm;
    char line[160];

    changed = 0;

    for (i = 0; i + 3 < nlines; ++i) {
        if (peep_parse_ld_de_0_to_255(lines[i], &imm) &&
            eq(i + 1, "ld (hl),e") &&
            eq(i + 2, "inc hl") &&
            eq(i + 3, "ld (hl),d") &&
            !(i + 5 < nlines && eq(i + 4, "pop hl") && eq(i + 5, "ld (hl),e")) &&
            peep_de_dead_at(i + 4)) {
            sprintf(line, "ld (hl),%d", imm & 255);
            replace1_tagged(i, line, "store_word_const");
            replace1(i + 1, "inc hl");
            replace1(i + 2, "ld (hl),0");
            delete_n(i + 3, 1);
            changed = 1;
            if (i > 0) --i;
        }
    }

    return changed;
}



static int pass_remove_unreferenced_labels(void)
{
    int i;
    int changed;
    char lab[128];

    changed = 0;

    for (i = 0; i < nlines; ++i) {
        if (label_name_at(i, lab) &&
            lab[0] == 'L' &&
            !is_label_referenced(lab)) {
            delete_n(i, 1);
            changed = 1;
            if (i > 0) --i;
        }
    }

    return changed;
}

static int peep_in_function_range(const char *func, int *startp, int *endp)
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
static int peep_range_has_debug_annotations(int start, int end)
{
    int i;

    for (i = start; i < end; i++)
        if (strncmp(lines[i], ";@dcc-", 6) == 0)
            return 1;
    return 0;
}

/*
 * Replace ld de,N; [extrn __mulu;] call __mulu with inline shift-add sequences
 * for small constants (10, 20, 40, 80, 160).  Uses DE as a scratch register to
 * save the original HL value; this matches what the caller already expects
 * (__mulu clobbers DE).
 *
 * Also constant-folds ld hl,C1; ld de,C2; call __mulu → ld hl,C1*C2 when
 * both operands are compile-time constants.
 */

/*
 * Replace integer-zero-to-float conversion followed by a 4-byte float store:
 *
 *   push <addr>
 *   ld hl,0
 *   call __fif
 *   ld b,d
 *   ld c,e
 *   pop de
 *   ex de,hl
 *   ld (hl),e
 *   inc hl
 *   ld (hl),d
 *   inc hl
 *   ld (hl),c
 *   inc hl
 *   ld (hl),b
 *
 * Since __fif(0) is exactly 0.0f, all four stored bytes are zero.  This is
 * common in mm.c's fillc()/ffillc() and avoids a runtime helper call inside
 * the clearing loops.
 */
static int pass_float_zero_store(void)
{
    int i;
    int changed;

    changed = 0;

    for (i = 0; i + 12 < nlines; ++i) {
        if (eq(i, "ld hl,0") &&
            eq(i + 1, "call __fif") &&
            eq(i + 2, "ld b,d") &&
            eq(i + 3, "ld c,e") &&
            eq(i + 4, "pop de") &&
            eq(i + 5, "ex de,hl") &&
            eq(i + 6, "ld (hl),e") &&
            eq(i + 7, "inc hl") &&
            eq(i + 8, "ld (hl),d") &&
            eq(i + 9, "inc hl") &&
            eq(i + 10, "ld (hl),c") &&
            eq(i + 11, "inc hl") &&
            eq(i + 12, "ld (hl),b")) {
            replace1_tagged(i, "pop hl", "float_zero_store");
            replace1(i + 1, "ld (hl),0");
            replace1(i + 2, "inc hl");
            replace1(i + 3, "ld (hl),0");
            replace1(i + 4, "inc hl");
            replace1(i + 5, "ld (hl),0");
            replace1(i + 6, "inc hl");
            replace1(i + 7, "ld (hl),0");
            delete_n(i + 8, 5);
            changed = 1;
            if (i > 0)
                --i;
        }
    }

    return changed;
}


static int pass_const_divmod_helpers(void)
{
    int i;
    int changed;

    changed = 0;

    for (i = 0; i + 1 < nlines; ++i) {
        long divv;
        int has_extrn;
        const char *oldname;
        const char *newname;
        char call_old[64];
        char extrn_old[64];
        char call_new[64];
        char extrn_new[64];

        if (!parse_ld_de_positive_imm(lines[i], &divv))
            continue;

        /* Leave divide-by-zero and unusual negative constants alone. */
        if (divv <= 0)
            continue;

        oldname = NULL;
        newname = NULL;

#define TRY_DIVMOD_HELPER(OLD, NEW) \
        do { \
            sprintf(call_old, "call %s", OLD); \
            sprintf(extrn_old, "extrn %s", OLD); \
            if (eq(i + 1, call_old) || \
                (i + 2 < nlines && eq(i + 1, extrn_old) && eq(i + 2, call_old))) { \
                oldname = OLD; \
                newname = NEW; \
            } \
        } while (0)

        TRY_DIVMOD_HELPER("__divu", "__q2u");
        /* A divisor that fits in a byte gets the even cheaper single-register
         * remainder helper (__r1u/__r1s take the divisor in E alone, with a
         * per-step 8-bit compare instead of __r2u/__r2s's 16-bit one). */
        if (!oldname && divv <= 255) TRY_DIVMOD_HELPER("__modu", "__r1u");
        if (!oldname) TRY_DIVMOD_HELPER("__modu", "__r2u");
        if (!oldname) TRY_DIVMOD_HELPER("__divs", "__q2s");
        if (!oldname && divv <= 255) TRY_DIVMOD_HELPER("__mods", "__r1s");
        if (!oldname) TRY_DIVMOD_HELPER("__mods", "__r2s");

#undef TRY_DIVMOD_HELPER

        if (!oldname)
            continue;

        sprintf(call_old, "call %s", oldname);
        sprintf(extrn_old, "extrn %s", oldname);
        sprintf(call_new, "call %s", newname);
        sprintf(extrn_new, "extrn %s", newname);

        has_extrn = (i + 2 < nlines && eq(i + 1, extrn_old) && eq(i + 2, call_old));
        if (has_extrn) {
            replace1_tagged(i + 1, extrn_new, "const_divmod_helper");
            replace1(i + 2, call_new);
        } else {
            replace1_tagged(i + 1, call_new, "const_divmod_helper");
        }

        /* __r1u/__r1s only read E, so shrink the 3-byte "ld de,N" divisor
         * load to the 2-byte "ld e,N" form to match. */
        if (!strcmp(newname, "__r1u") || !strcmp(newname, "__r1s")) {
            char l_e[32];
            sprintf(l_e, "ld e,%ld", divv);
            replace1(i, l_e);
        }

        changed = 1;
    }

    return changed;
}


static int peep_is_exact_extrn_for(const char *line, const char *name)
{
    char clean[MAX_LINE];
    char want[64];

    strip_peep_comment_copy(clean, line);
    sprintf(want, "extrn %s", name);
    return strcmp(clean, want) == 0;
}

static int peep_is_exact_call_for(const char *line, const char *name)
{
    char clean[MAX_LINE];
    char want[64];

    strip_peep_comment_copy(clean, line);
    sprintf(want, "call %s", name);
    return strcmp(clean, want) == 0;
}

static int peep_line_is_divmod_extrn(const char *line)
{
    static const char *names[] = {
        "__divu", "__modu", "__divs", "__mods",
        "__q2u", "__r2u", "__q2s", "__r2s",
        "__r1u", "__r1s",
        NULL
    };
    int i;

    for (i = 0; names[i]; ++i)
        if (peep_is_exact_extrn_for(line, names[i]))
            return 1;
    return 0;
}

/*
 * pass_fix_divmod_extrns:
 *
 * pass_const_divmod_helpers may rewrite the first call after an EXTRN from
 * __modu/__mods/etc. to the constant-divisor helper __r2u/__r2s/etc.  If the
 * same function later still contains variable-divisor calls to the old helper,
 * M80 reports them as undefined at assembly time because the only EXTRN was
 * consumed by the rewrite.  Conversely, leaving an unused EXTRN is bad for the
 * reduced-runtime link because dccrtlstrip/L80 can keep or require an unused
 * block.
 *
 * Normalize this small helper family at the very end: remove stale EXTRNs for
 * the div/mod helper names, scan the final calls, then insert exactly the EXTRNs
 * still required by the optimized module.
 */
static void pass_fix_divmod_extrns(void)
{
    static const char *names[] = {
        "__divu", "__modu", "__divs", "__mods",
        "__q2u", "__r2u", "__q2s", "__r2s",
        "__r1u", "__r1s",
        NULL
    };
    int used[10];
    int i, k;
    char line[64];

    for (k = 0; k < 10; ++k)
        used[k] = 0;

    /* Delete all existing EXTRNs for this helper family. */
    for (i = 0; i < nlines; ++i) {
        if (peep_line_is_divmod_extrn(lines[i])) {
            delete_n(i, 1);
            --i;
        }
    }

    /* Scan final code for calls that remain. */
    for (i = 0; i < nlines; ++i) {
        for (k = 0; names[k]; ++k) {
            if (peep_is_exact_call_for(lines[i], names[k]))
                used[k] = 1;
        }
    }

    /* Insert in reverse so final order matches names[]. */
    for (k = 9; k >= 0; --k) {
        if (used[k]) {
            sprintf(line, "extrn %s", names[k]);
            insert_line(0, line);
        }
    }
}


static int peep_line_is_mulu_extrn(const char *line)
{
    return peep_is_exact_extrn_for(line, "__mulu");
}

/*
 * pass_fix_mulu_extrn:
 *
 * pass_mulu_const may consume the one EXTRN __mulu line when it inlines or
 * folds the first constant multiply in a module.  If later variable or
 * unsupported-constant unsigned multiplies remain, M80 then reports those
 * call __mulu sites as undefined.
 *
 * Normalize __mulu exactly like the div/mod helper family: remove stale
 * EXTRNs, scan the final code, and insert one EXTRN if any call remains.
 */
static void pass_fix_mulu_extrn(void)
{
    int i;
    int used;

    used = 0;

    for (i = 0; i < nlines; ++i) {
        if (peep_line_is_mulu_extrn(lines[i])) {
            delete_n(i, 1);
            --i;
        }
    }

    for (i = 0; i < nlines; ++i) {
        if (peep_is_exact_call_for(lines[i], "__mulu")) {
            used = 1;
            break;
        }
    }

    if (used)
        insert_line(0, "extrn __mulu");
}

static int pass_mulu_const(void)
{
    int i, changed = 0;

    for (i = 0; i + 1 < nlines; i++) {
        long de_val;
        int has_extrn;
        int n_delete;
        int shift_count;
        char tmp[MAX_LINE];
        char *endp;

        if (!parse_ld_de_positive_imm(lines[i], &de_val))
            continue;

        has_extrn = (i + 2 < nlines &&
                     eq(i + 1, "extrn __mulu") &&
                     eq(i + 2, "call __mulu"));
        if (!has_extrn && !eq(i + 1, "call __mulu"))
            continue;

        n_delete = has_extrn ? 3 : 2;

        /* Constant fold: ld hl,C1 immediately before ld de,C2; call __mulu */
        if (i > 0) {
            strip_peep_comment_copy(tmp, lines[i - 1]);
            if (strncmp(tmp, "ld hl,", 6) == 0) {
                long hl_val = strtol(tmp + 6, &endp, 0);
                while (*endp == ' ' || *endp == '\t') endp++;
                if (*endp == '\0' && hl_val >= 0 && hl_val <= 32767) {
                    long prod = hl_val * de_val;
                    if (prod >= 0 && prod <= 65535) {
                        char buf[64];
                        sprintf(buf, "ld hl,%ld ; peep: mulu_const_fold", prod);
                        replace1(i - 1, buf);
                        delete_n(i, n_delete);
                        changed = 1;
                        if (i > 1) i -= 2;
                        continue;
                    }
                }
            }
        }

        if (de_val == 0 || de_val == 1 ||
            (de_val > 0 && de_val <= 32768 && (de_val & (de_val - 1)) == 0)) {
            delete_n(i, n_delete);

            if (de_val == 0) {
                insert_line_tagged(i, "ld hl,0", "mulu0");
            } else {
                shift_count = 0;
                while (de_val > 1) {
                    de_val >>= 1;
                    shift_count++;
                }
                while (shift_count-- > 0)
                    insert_line_tagged(i, "add hl,hl", "mulu_pow2");
            }

            changed = 1;
            if (i > 0) i--;
            continue;
        }

        /* Inline expansion for specific multiplier constants.
         * Strategy: save HL in DE (ld d,h; ld e,l), compute 4x, add original
         * to get 5x, then shift left to reach the target.
         * 5   = 5 * 1    :  ×1→DE, ×2, ×4, +DE(=5)
         * 10  = 5 * 2    :  ×1→DE, ×2, ×4, +DE(=5), ×2
         * 20  = 5 * 4    :  ×1→DE, ×2, ×4, +DE(=5), ×2, ×2
         * 40  = 5 * 8    :  ×1→DE, ×2, ×4, +DE(=5), ×2, ×2, ×2
         * 80  = 5 * 16   :  ×1→DE, ×2, ×4, +DE(=5), ×2, ×2, ×2, ×2
         * 160 = 5 * 32   :  ×1→DE, ×2, ×4, +DE(=5), ×2, ×2, ×2, ×2, ×2  */
        if (de_val != 5 && de_val != 10 && de_val != 20 &&
            de_val != 40 && de_val != 80 && de_val != 160)
            continue;

        delete_n(i, n_delete);

        /* Insert instructions in REVERSE order so position i holds the first. */
        /* Extra trailing shifts (160 needs one more than 80, etc.) */
        if (de_val >= 160) insert_line(i, "add hl,hl");   /* ×160 */
        if (de_val >=  80) insert_line(i, "add hl,hl");   /* ×80  */
        if (de_val >=  40) insert_line(i, "add hl,hl");   /* ×40  */
        if (de_val >=  20) insert_line(i, "add hl,hl");   /* ×20  */
        if (de_val >=  10) insert_line(i, "add hl,hl");   /* ×10  */
        /* Core 5× sequence: */
        insert_line(i, "add hl,de");                       /* ×5 = ×4 + ×1 */
        insert_line(i, "add hl,hl");                       /* ×4  */
        insert_line(i, "add hl,hl");                       /* ×2  */
        insert_line(i, "ld e,l");                          /* save ×1 low  */
        {
            char tag[32];
            sprintf(tag, "mulu%ld", de_val);
            insert_line_tagged(i, "ld d,h", tag);          /* save ×1 high */
        }

        changed = 1;
        if (i > 0) i--;
    }

    return changed;
}

static int stride_parse_ld_r_ix_neg(const char *s, char r, int *n); /* forward */

/* Is `line` inside the body of the function labeled `func`? Originally
 * always scanned forward from line 0 to `line` on every call - O(line)
 * every time, called once per line from the main peephole loop's per-line
 * pattern checks (profiled: the single largest self-time contributor when
 * compiling tests/cobint.c, the largest generated .mac in the suite - the
 * only real caller passes "_main:", whose body sits near the top of the
 * file with the next `public` boundary thousands of lines later, so almost
 * every call scans nearly the whole file just to conclude "no").
 *
 * A first attempt scanned backward from `line` instead, expecting to stop
 * at the nearest earlier `public` boundary - it didn't help, because that
 * boundary is exactly as far away going backward as going forward (same
 * gap, same iteration count), and unlike the forward version's short-
 * circuited "start<0 so skip the public check" fast path, the backward
 * version must check every line for "is this a public boundary" before it
 * even knows whether `func`'s label is nearer, so it did strictly more
 * work per iteration - a measured regression, not an improvement.
 *
 * The actual fix: `func`'s [start, end) range is the same answer for every
 * query until the line table itself changes shape (an insert/delete
 * shifting positions - replace1 rewrites a line's text in place without
 * moving anything, so it can't invalidate this), so compute it once and
 * reuse it - keyed on `nlines`, the cheapest-to-check proxy for "has
 * anything shifted", already maintained by every insert/delete site. */
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
 * Eliminate the IX frame-pointer prologue/epilogue from leaf functions where
 * the peephole optimizer has already removed all IX-relative memory accesses.
 *
 * Pattern to remove:
 *   push ix          ← prologue (3 lines deleted)
 *   ld ix,0
 *   add ix,sp
 *   ... body with no (ix+N)/(ix-N) references ...
 *   ld sp,ix         ← epilogue simplified: these 2 lines deleted, "ret" kept
 *   pop ix
 *   ret
 *
 * Safety checks: abort if any line in the body contains "(ix" (live IX usage)
 * or if an un-removed local-allocation sequence is present.
 */
static int pass_elim_ix_frame(void)
{
    int i, j;
    int changed;
    int next_func;
    int has_ix_use;
    int epi;

    changed = 0;

    for (i = 0; i + 4 < nlines; i++) {
        if (!eq(i, "push ix") || !eq(i+1, "ld ix,0") || !eq(i+2, "add ix,sp"))
            continue;

        /* Find the function boundary: next "public" directive or EOF */
        next_func = nlines;
        for (j = i + 3; j < nlines; j++) {
            if (strncmp(lines[j], "public ", 7) == 0 || is_global_asm_label_line(j)) {
                next_func = j;
                break;
            }
        }

        /* Scan the body for IX usage and locate the epilogue */
        has_ix_use = 0;
        epi = -1;
        for (j = i + 3; j < next_func; j++) {
            /* Locate epilogue first.  Its IX references are the only ones
             * allowed when deciding whether the frame pointer is dead. */
            if (eq(j, "ld sp,ix") && j + 2 < next_func &&
                eq(j + 1, "pop ix") && eq(j + 2, "ret")) {
                epi = j;
                j += 1;
                continue;
            }

            /* Any other remaining IX use means the frame pointer is still
             * live.  The old test only looked for "(ix" indexed memory
             * operands, but generated code also uses IX as a value via:
             *
             *     push ix
             *     pop hl
             *     ld de,4
             *     add hl,de
             *
             * to form parameter/local addresses.  Removing the prologue in
             * that case leaves IX uninitialised and breaks make_move(). */
            {
                char tmp_ix_scan[MAX_LINE];
                strip_peep_comment_copy(tmp_ix_scan, lines[j]);
                if (strstr(tmp_ix_scan, "ix") != NULL) {
                    has_ix_use = 1;
                    break;
                }
            }
            /* Detect an un-removed local allocation.  This pass runs
             * after other peepholes, so local allocation may be in either the
             * original form:
             *
             *     ld hl,-N / add hl,sp / ld sp,hl
             *
             * or the compact form:
             *
             *     dec sp
             *     dec sp
             *
             * Removing the IX prologue/epilogue while leaving either form
             * corrupts the return address.  tgoto's gt_block_label exposed
             * this when local_alloc_2 had already compacted the allocation
             * before pass_elim_ix_frame saw it.
             */
            if (eq(j, "dec sp")) {
                has_ix_use = 1;
                break;
            }
            if (j + 2 < next_func &&
                strncmp(lines[j], "ld hl,-", 7) == 0 &&
                eq(j+1, "add hl,sp") &&
                eq(j+2, "ld sp,hl")) {
                has_ix_use = 1;
                break;
            }
        }

        if (!has_ix_use && epi >= 0) {
            delete_n(i, 3);     /* remove push ix / ld ix,0 / add ix,sp */
            epi -= 3;
            delete_n(epi, 2);   /* remove ld sp,ix / pop ix; "ret" stays */
            changed = 1;
            i--;                /* re-examine same position after deletions */
        }
    }

    return changed;
}

/*
 * pass_shared_frame_stubs:
 *
 * Called after pass_elim_ix_frame (which already stripped IX frames from
 * functions that never touch IX).  This pass converts the remaining framed
 * prologues and epilogues to shared stub calls, saving ~5-7 bytes per
 * prologue and ~2 bytes per epilogue that has locals.
 *
 * With locals (13 inline bytes → 6):
 *   push ix / ld ix,0 / add ix,sp / ld hl,-N / add hl,sp / ld sp,hl
 *   → ld hl,-N / call __entr
 *
 * Without locals but with IX-accessed params (8 inline bytes -> 3):
 *   push ix / ld ix,0 / add ix,sp
 *   -> call __en0
 *
 * Epilogue -- with-locals only (5 inline bytes -> 3):
 *   ld sp,ix / pop ix / ret
 *   -> jp __lve
 *
 * The no-locals epilogue (pop ix / ret, 3 bytes after the dcc.c fix) is
 * already as compact as a jp __lve, so it is left inline.
 *
 * RTL stub names are <=6 chars so they stay distinct in L80's 6-character
 * symbol table.  extrn declarations are injected at the top of the file so
 * that dccrtlstrip includes only the blocks actually used.
 */
static int pass_shared_frame_stubs(void)
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

static int pass_byte_minmax_patterns(void)
{
    int i;
    int changed;
    int imm;
    char off[32];
    char newline[160];

    changed = 0;

    for (i = 0; i + 11 < nlines; ++i) {
        /*
         * Unsigned byte local/parameter compare against small constant:
         *
         *   ld hl,N
         *   push hl
         *   ld l,(ix+K)
         *   ld h,0
         *   ex de,hl
         *   pop hl
         *   or a
         *   sbc hl,de
         *   jp cc,L
         *
         * For byte values this is equivalent to:
         *
         *   ld a,(ix+K)
         *   cp N
         *   jp cc,L
         */
        if (peep_parse_ld_hl_0_to_255(lines[i], &imm) &&
            eq(i + 1, "push hl") &&
            peep_parse_ld_l_ix(lines[i + 2], off) &&
            eq(i + 3, "ld h,0") &&
            eq(i + 4, "ex de,hl") &&
            eq(i + 5, "pop hl") &&
            eq(i + 6, "or a") &&
            eq(i + 7, "sbc hl,de") &&
            (strncmp(lines[i + 8], "jp z,", 5) == 0 ||
             strncmp(lines[i + 8], "jp nz,", 6) == 0 ||
             strncmp(lines[i + 8], "jp c,", 5) == 0 ||
             strncmp(lines[i + 8], "jp nc,", 6) == 0)) {
            sprintf(newline, "ld a,(ix%s)", off);
            replace1_tagged(i, newline, "byte_const_cmp");
            sprintf(newline, "cp %d", imm);
            replace1(i + 1, newline);
            replace1(i + 2, lines[i + 8]);
            delete_n(i + 3, 6);
            changed = 1;
            if (i > 0) --i;
            continue;
        }

        /*
         * Same compare, but the constant is in DE because push_lde_pop
         * has already folded a push/pop pair:
         *
         *   ld l,(ix+K)
         *   ld h,0
         *   ld de,N
         *   or a
         *   sbc hl,de
         *   jp cc,L
         */
        if (peep_parse_ld_l_ix(lines[i], off) &&
            eq(i + 1, "ld h,0") &&
            peep_parse_ld_de_0_to_255(lines[i + 2], &imm) &&
            eq(i + 3, "or a") &&
            eq(i + 4, "sbc hl,de") &&
            (strncmp(lines[i + 5], "jp z,", 5) == 0 ||
             strncmp(lines[i + 5], "jp nz,", 6) == 0 ||
             strncmp(lines[i + 5], "jp c,", 5) == 0 ||
             strncmp(lines[i + 5], "jp nc,", 6) == 0)) {
            sprintf(newline, "ld a,(ix%s)", off);
            replace1_tagged(i, newline, "byte_de_cmp");
            sprintf(newline, "cp %d", imm);
            replace1(i + 1, newline);
            replace1(i + 2, lines[i + 5]);
            delete_n(i + 3, 3);
            changed = 1;
            if (i > 0) --i;
            continue;
        }

        /*
         * Byte local/parameter compare:
         *
         *   ld l,(ix+A)
         *   ld h,0
         *   push hl
         *   ld l,(ix+B)
         *   ld h,0
         *   ex de,hl
         *   pop hl
         *   or a
         *   sbc hl,de
         *   jp cc,L
         *
         * becomes:
         *   ld a,(ix+A)
         *   cp (ix+B)
         *   jp cc,L
         */
        {
            char off2[32];
            if (peep_parse_ld_l_ix(lines[i], off) &&
                eq(i + 1, "ld h,0") &&
                eq(i + 2, "push hl") &&
                peep_parse_ld_l_ix(lines[i + 3], off2) &&
                eq(i + 4, "ld h,0") &&
                eq(i + 5, "ex de,hl") &&
                eq(i + 6, "pop hl") &&
                eq(i + 7, "or a") &&
                eq(i + 8, "sbc hl,de") &&
                (strncmp(lines[i + 9], "jp z,", 5) == 0 ||
                 strncmp(lines[i + 9], "jp nz,", 6) == 0 ||
                 strncmp(lines[i + 9], "jp c,", 5) == 0 ||
                 strncmp(lines[i + 9], "jp nc,", 6) == 0)) {
                sprintf(newline, "ld a,(ix%s)", off);
                replace1_tagged(i, newline, "byte_ix_cmp");
                sprintf(newline, "cp (ix%s)", off2);
                replace1(i + 1, newline);
                replace1(i + 2, lines[i + 9]);
                delete_n(i + 3, 7);
                changed = 1;
                if (i > 0) --i;
                continue;
            }
        }

        /*
         * Byte "(x & 1)" branch:
         *
         *   ld l,(ix+K)
         *   ld h,0
         *   ld de,1
         *   ld a,h
         *   and d
         *   ld h,a
         *   ld a,l
         *   and e
         *   ld l,a
         *   ld a,h
         *   or l
         *   jp z/nz,L
         */
        if (peep_parse_ld_l_ix(lines[i], off) &&
            eq(i + 1, "ld h,0") &&
            peep_parse_ld_de_0_to_255(lines[i + 2], &imm) &&
            imm == 1 &&
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
            sprintf(newline, "ld a,(ix%s)", off);
            replace1_tagged(i, newline, "byte_and1_bool");
            replace1(i + 1, "and 1");
            replace1(i + 2, lines[i + 11]);
            delete_n(i + 3, 9);
            changed = 1;
            if (i > 0) --i;
            continue;
        }
    }

    return changed;
}






static int pass_dead_hl_load_before_ldhl(void)
{
    int i;
    int changed;
    char off[32], off2[32];
    char imm[128];

    changed = 0;

    for (i = 0; i + 2 < nlines; ++i) {
        if (peep_parse_ld_l_ix(lines[i], off) &&
            (eq(i + 1, "ld h,0") || peep_parse_ld_h_ix(lines[i + 1], off2)) &&
            parse_ld_hl_imm(lines[i + 2], imm)) {
            delete_n(i, 2);
            changed = 1;
            if (i > 0) --i;
        }
    }

    return changed;
}

static int peep_call_uses_stack_args_only(const char *s)
{
    char tmp[MAX_LINE];
    const char *name;

    strip_peep_comment_copy(tmp, s);
    if (strncmp(tmp, "call ", 5) != 0)
        return 0;

    name = tmp + 5;
    if (strncmp(name, "__", 2) == 0) {
        return strcmp(name, "__scmp") == 0 ||
               strcmp(name, "__ncmp") == 0 ||
               strcmp(name, "__mset") == 0;
    }

    return name[0] == '_';
}

static int peep_call_uses_long_stack_args(const char *s)
{
    char tmp[MAX_LINE];
    const char *name;

    strip_peep_comment_copy(tmp, s);
    if (strncmp(tmp, "call ", 5) != 0)
        return 0;

    name = tmp + 5;
    if (strncmp(name, "__", 2) == 0) {
        return strcmp(name, "__lts") == 0 ||
               strcmp(name, "__les") == 0 ||
               strcmp(name, "__lgs") == 0 ||
               strcmp(name, "__lks") == 0 ||
               strcmp(name, "__lds") == 0 ||
               strcmp(name, "__ltu") == 0 ||
               strcmp(name, "__lmu") == 0 ||
               strcmp(name, "__lms") == 0 ||
               strcmp(name, "__fgt") == 0 ||
               strcmp(name, "__fadd") == 0;
    }

    return name[0] == '_';
}

/*
 * A common by-reference load used as an immediate stack argument:
 *
 *   ld e,(hl)
 *   inc hl
 *   ld d,(hl)
 *   ex de,hl
 *   push hl
 *   call _func
 *
 * The loaded word is already in DE.  For ordinary stack-argument calls the
 * transient HL/DE register values are not part of the call ABI, so push DE
 * directly and avoid the exchange.  Do not apply to register-ABI helpers.
 */
static int pass_word_load_push_de_call(void)
{
    int i;
    int changed = 0;

    for (i = 0; i + 5 < nlines; ++i) {
        if (!eq(i,     "ld e,(hl)")) continue;
        if (!eq(i + 1, "inc hl")) continue;
        if (!eq(i + 2, "ld d,(hl)")) continue;
        if (!eq(i + 3, "ex de,hl")) continue;
        if (!eq(i + 4, "push hl")) continue;
        if (!peep_call_uses_stack_args_only(lines[i + 5])) continue;

        replace1_tagged(i + 3, "push de", "word_load_push_de_call");
        delete_n(i + 4, 1);
        changed = 1;
        if (i > 0) --i;
    }

    return changed;
}

/*
 * A 32-bit value loaded from memory is often pushed immediately as a long or
 * float stack argument:
 *
 *   ld e,(hl) / inc hl / ld d,(hl)          ; DE = low word
 *   inc hl / ld a,(hl) / inc hl / ld h,(hl)
 *   ld l,a                                  ; HL = high word
 *   ex de,hl
 *   push de
 *   push hl
 *   call __helper
 *
 * Before the exchange, HL is already the high word and DE the low word.  Push
 * them in that order and skip the exchange.  Constrain this to immediate
 * stack-argument calls; register-ABI helpers are deliberately excluded.
 */
static int pass_long_load_push_no_ex_call(void)
{
    int i;
    int call_line;
    int changed = 0;

    for (i = 0; i + 11 < nlines; ++i) {
        if (!eq(i,      "ld e,(hl)")) continue;
        if (!eq(i + 1,  "inc hl")) continue;
        if (!eq(i + 2,  "ld d,(hl)")) continue;
        if (!eq(i + 3,  "inc hl")) continue;
        if (!eq(i + 4,  "ld a,(hl)")) continue;
        if (!eq(i + 5,  "inc hl")) continue;
        if (!eq(i + 6,  "ld h,(hl)")) continue;
        if (!eq(i + 7,  "ld l,a")) continue;
        if (!eq(i + 8,  "ex de,hl")) continue;
        if (!eq(i + 9,  "push de")) continue;
        if (!eq(i + 10, "push hl")) continue;

        call_line = i + 11;
        if (call_line < nlines && strncmp(lines[call_line], "extrn ", 6) == 0)
            call_line++;
        if (call_line >= nlines || !peep_call_uses_long_stack_args(lines[call_line]))
            continue;

        replace1_tagged(i + 8, "push hl", "long_load_push_no_ex_call");
        replace1(i + 9, "push de");
        delete_n(i + 10, 1);
        changed = 1;
        if (i > 0) --i;
    }

    return changed;
}

static int parse_ix_off_numeric(const char *off, int *val); /* forward */

/*
 * Remove the 6-instruction signed-compare bias (xor 80h trick) from
 * for-loop back edges where the loop counter is provably non-negative.
 *
 * DCC emits for a signed 16-bit compare against a small positive constant:
 *
 *   ld a,h       ; }
 *   xor 80h      ; } bias both operands by flipping sign bit so that
 *   ld h,a       ; } signed subtraction with SBC gives the right carry
 *   ld a,d       ; }
 *   xor 80h      ; }
 *   ld d,a       ; }
 *   or a
 *   sbc hl,de
 *   jp c/nc, BODY
 *
 * When the full pattern immediately preceded by the loop back-edge increment
 * is recognised, both the counter (fresh from the 16-bit inc) and the limit
 * (CONST <= 32767) have sign bit 0, so the XOR 80h operations are no-ops.
 * Remove the 6-instruction block; the remaining unsigned SBC gives the same
 * carry as the signed comparison would.
 *
 * Required context (looking backward from "ld a,h" at position i):
 *   i-1: ld de,CONST         0 < CONST <= 32767
 *   i-2: ld h,(ix+HOFF)
 *   i-3: ld l,(ix+LOFF)
 *   i-4: LSKIP:
 *   i-5: inc (ix+HOFF)       HOFF == LOFF+1 (little-endian adjacent bytes)
 *   i-6: jp nz,LSKIP
 *   i-7: inc (ix+LOFF)
 */
static int pass_elim_loop_back_signed_bias(void)
{
    int i;
    int changed = 0;

    for (i = 7; i + 8 < nlines; ++i) {
        char loff[32], hoff[32], skip_lab[128], got_lab[128];
        long const_val;
        int lo_val, hi_val;
        char inc_lo[64], inc_hi[64];

        if (!eq(i,   "ld a,h"))    continue;
        if (!eq(i+1, "xor 80h"))   continue;
        if (!eq(i+2, "ld h,a"))    continue;
        if (!eq(i+3, "ld a,d"))    continue;
        if (!eq(i+4, "xor 80h"))   continue;
        if (!eq(i+5, "ld d,a"))    continue;
        if (!eq(i+6, "or a"))      continue;
        if (!eq(i+7, "sbc hl,de")) continue;
        if (strncmp(lines[i+8], "jp ", 3) != 0) continue;

        if (!parse_ld_de_positive_imm(lines[i-1], &const_val)) continue;
        if (!peep_parse_ld_h_ix(lines[i-2], hoff))             continue;
        if (!peep_parse_ld_l_ix(lines[i-3], loff))             continue;
        if (!parse_ix_off_numeric(loff, &lo_val))               continue;
        if (!parse_ix_off_numeric(hoff, &hi_val))               continue;
        if (hi_val != lo_val + 1)                               continue;
        if (!label_name_at(i-4, skip_lab))                      continue;

        sprintf(inc_hi, "inc (ix%s)", hoff);
        if (!eq(i-5, inc_hi))                                   continue;

        if (!parse_jp_nz_label(lines[i-6], got_lab))           continue;
        if (strcmp(got_lab, skip_lab) != 0)                     continue;

        sprintf(inc_lo, "inc (ix%s)", loff);
        if (!eq(i-7, inc_lo))                                   continue;

        delete_n(i, 6);
        changed = 1;
        if (i >= 7) i -= 7;
    }

    return changed;
}


/*
 * Replace a full 16-bit compare against zero used only for a Z/NZ branch:
 *
 *     ld de,0
 *     or a
 *     sbc hl,de
 *     jp z,L        ; or jp nz,L
 *
 * with the standard 16-bit zero test:
 *
 *     ld a,h
 *     or l
 *     jp z,L        ; or jp nz,L
 *
 * This is safe only for equality/non-equality branches.  Carry is different,
 * so relational branches must not be rewritten by this pass.
 */
static int pass_hl_cmp_zero_to_or_hl(void)
{
    int i;
    int changed;

    changed = 0;

    for (i = 0; i + 3 < nlines; ++i) {
        if (eq(i, "ld de,0") &&
            eq(i + 1, "or a") &&
            eq(i + 2, "sbc hl,de") &&
            (strncmp(lines[i + 3], "jp z,", 5) == 0 ||
             strncmp(lines[i + 3], "jp nz,", 6) == 0)) {
            replace1_tagged(i, "ld a,h", "cmp0_or_hl");
            replace1(i + 1, "or l");
            delete_n(i + 2, 1);
            changed = 1;
            if (i > 0)
                --i;
        }
    }

    return changed;
}

static int pass_cp_zero_to_or_a(void)
{
    int i;
    int changed;

    changed = 0;

    for (i = 0; i + 2 < nlines; ++i) {
        if (eq(i + 1, "cp 0") &&
            (strncmp(lines[i + 2], "jp z,", 5) == 0 ||
             strncmp(lines[i + 2], "jp nz,", 6) == 0)) {
            replace1_tagged(i + 1, "or a", "cp0_or_a");
            changed = 1;
        }
    }

    return changed;
}


/*
 * Narrow a byte value compared as a zero-extended 16-bit integer to a direct
 * 8-bit CP.  DCC often emits this after the usual integer promotions:
 *
 *     ld l,(ix+N)
 *     ld h,0
 *     ld de,K
 *     or a
 *     sbc hl,de
 *     jp cc,L
 *
 * or, for signed int comparisons, with the standard xor-80h bias before the
 * subtraction.  When the left operand was explicitly zero-extended from a
 * byte, both forms have the same Z/C result as:
 *
 *     ld a,(ix+N)
 *     cp K
 *     jp cc,L
 *
 * JP does not alter flags, so this is also safe when the compiler emits two
 * adjacent flag checks after the compare, such as jp z,L / jp c,L.
 */

/*
 * Optimize the common signed 16-bit compare against a positive constant whose
 * low byte is zero.  DCC emits signed compares by biasing both high bytes and
 * doing a full 16-bit subtract:
 *
 *     ld de,4096
 *     ld a,h
 *     xor 80h
 *     ld h,a
 *     ld a,d
 *     xor 80h
 *     ld d,a
 *     or a
 *     sbc hl,de
 *     jp nc,L          ; branch when HL >= 4096
 *
 * For constants K*256, the low byte cannot affect < or >=, so compare the
 * biased high byte directly.  This is useful for loops such as i < 4096.
 */
static int pass_signed_cmp_const_low0(void)
{
    int i;
    int changed;
    int imm;
    char line[128];

    changed = 0;

    for (i = 0; i + 8 < nlines; ++i) {
        if (!peep_parse_ld_de_signed(lines[i], &imm))
            continue;
        if (imm <= 0 || imm > 32767 || (imm & 255) != 0)
            continue;
        if (!eq(i + 1, "ld a,h"))
            continue;
        if (!eq(i + 2, "xor 80h"))
            continue;
        if (!eq(i + 3, "ld h,a"))
            continue;
        if (!eq(i + 4, "ld a,d"))
            continue;
        if (!eq(i + 5, "xor 80h"))
            continue;
        if (!eq(i + 6, "ld d,a"))
            continue;
        if (!eq(i + 7, "or a"))
            continue;
        if (!eq(i + 8, "sbc hl,de"))
            continue;
        if (i + 9 >= nlines)
            continue;
        if (strncmp(lines[i + 9], "jp nc,", 6) != 0 &&
            strncmp(lines[i + 9], "jp c,", 5) != 0)
            continue;

        replace1_tagged(i, "ld a,h", "signed_cmp_const_low0");
        replace1(i + 1, "xor 80h");
        sprintf(line, "cp %d", ((imm >> 8) ^ 0x80) & 255);
        replace1(i + 2, line);
        delete_n(i + 3, 6);
        changed = 1;
        if (i > 0)
            --i;
    }

    return changed;
}

static int pass_zeroext_byte_cmp_const(void)
{
    int i;
    int changed;
    int imm;
    char off[32];
    char newline[128];

    changed = 0;

    for (i = 0; i + 5 < nlines; ++i) {
        if (!peep_parse_ld_l_ix(lines[i], off))
            continue;
        if (!eq(i + 1, "ld h,0"))
            continue;
        if (!peep_parse_ld_de_0_to_255(lines[i + 2], &imm))
            continue;

        /* Plain unsigned 16-bit subtract compare. */
        if (eq(i + 3, "or a") &&
            eq(i + 4, "sbc hl,de") &&
            (strncmp(lines[i + 5], "jp z,", 5) == 0 ||
             strncmp(lines[i + 5], "jp nz,", 6) == 0 ||
             strncmp(lines[i + 5], "jp c,", 5) == 0 ||
             strncmp(lines[i + 5], "jp nc,", 6) == 0)) {
            sprintf(newline, "ld a,(ix%s)", off);
            replace1_tagged(i, newline, "zeroext_byte_cmp_const");
            if (imm == 0)
                replace1(i + 1, "or a");
            else {
                sprintf(newline, "cp %d", imm);
                replace1(i + 1, newline);
            }
            replace1(i + 2, lines[i + 5]);
            delete_n(i + 3, 3);
            changed = 1;
            if (i > 0) --i;
            continue;
        }

        /* Signed compare bias around the same zero-extended byte value. */
        if (i + 11 < nlines &&
            eq(i + 3, "ld a,h") &&
            eq(i + 4, "xor 80h") &&
            eq(i + 5, "ld h,a") &&
            eq(i + 6, "ld a,d") &&
            eq(i + 7, "xor 80h") &&
            eq(i + 8, "ld d,a") &&
            eq(i + 9, "or a") &&
            eq(i + 10, "sbc hl,de") &&
            (strncmp(lines[i + 11], "jp z,", 5) == 0 ||
             strncmp(lines[i + 11], "jp nz,", 6) == 0 ||
             strncmp(lines[i + 11], "jp c,", 5) == 0 ||
             strncmp(lines[i + 11], "jp nc,", 6) == 0)) {
            sprintf(newline, "ld a,(ix%s)", off);
            replace1_tagged(i, newline, "zeroext_byte_cmp_const");
            if (imm == 0)
                replace1(i + 1, "or a");
            else {
                sprintf(newline, "cp %d", imm);
                replace1(i + 1, newline);
            }
            replace1(i + 2, lines[i + 11]);
            delete_n(i + 3, 9);
            changed = 1;
            if (i > 0) --i;
            continue;
        }
    }

    return changed;
}

/*
 * When a zero-extended byte value is compared to a small constant (0..255),
 * DCC emits a push/sbc/pop sequence to preserve HL across the compare:
 *
 *   ld h,0
 *   push hl           ; save HL (byte in L)
 *   ld de,N           ; 0 <= N <= 255
 *   or a
 *   sbc hl,de         ; set Z/C flags, destroys HL
 *   pop hl            ; restore byte in HL
 *
 * Because H=0 and D=0, the 16-bit sbc produces the same Z and C flags as
 * an 8-bit cp on L.  We can use ld a,l; cp N directly and skip the
 * push/sbc/pop entirely, leaving HL untouched.
 */
static int pass_byte_cmp_push_pop_hl(void)
{
    int i, imm, changed = 0;
    char cp_line[32];

    for (i = 0; i + 5 < nlines; i++) {
        if (!eq(i, "ld h,0")) continue;
        if (!eq(i + 1, "push hl")) continue;
        if (!peep_parse_ld_de_0_to_255(lines[i + 2], &imm)) continue;
        if (!eq(i + 3, "or a")) continue;
        if (!eq(i + 4, "sbc hl,de")) continue;
        if (!eq(i + 5, "pop hl")) continue;

        replace1_tagged(i + 1, "ld a,l", "byte_cmp_push_pop_hl");
        if (imm == 0)
            replace1(i + 2, "or a");
        else {
            sprintf(cp_line, "cp %d", imm);
            replace1(i + 2, cp_line);
        }
        delete_n(i + 3, 3);

        changed = 1;
        if (i > 0) i--;
    }

    return changed;
}


/*
 * Byte-indexed array address through a global base pointer.
 *
 * When a float/int array is indexed by a uint8_t variable and the base comes
 * from a global (either a direct label or a dereferenced pointer), the
 * compiler emits:
 *
 *   ld hl,X             ; load base (address or pointer value) into HL
 *   push hl             ; save base
 *   ld l,(ix-K)         ; byte index
 *   ld h,0
 *   add hl,hl × S       ; scale (S doublings: ×2, ×4, ×8, ...)
 *   ex de,hl            ; DE = scaled index
 *   pop hl              ; HL = base (restored)
 *   add hl,de           ; HL = base + scaled_index
 *
 * The push/pop can be removed by computing the index first, then loading the
 * base — X is a constant expression so reordering is always safe:
 *
 *   ld l,(ix-K)
 *   ld h,0
 *   add hl,hl × S
 *   ex de,hl            ; DE = scaled index
 *   ld hl,X             ; HL = base
 *   add hl,de           ; HL = base + scaled_index
 *
 * Saves 2 instructions and ~21 T-states per array access.
 */
static int pass_byte_global_ptr_array_addr(void)
{
    int i, j, S, changed = 0;
    char base[MAX_LINE], off[32], ld_hl_buf[MAX_LINE];

    for (i = 0; i + 6 < nlines; i++) {
        if (!parse_ld_hl_imm(lines[i], base)) continue;
        if (!eq(i + 1, "push hl")) continue;
        if (!peep_parse_ld_l_ix(lines[i + 2], off)) continue;
        if (!eq(i + 3, "ld h,0")) continue;
        j = i + 4; S = 0;
        while (j < nlines && eq(j, "add hl,hl") && S < 8) { j++; S++; }
        if (S == 0) continue;
        if (j + 2 >= nlines) continue;
        if (!eq(j,     "ex de,hl")) continue;
        if (!eq(j + 1, "pop hl")) continue;
        if (!eq(j + 2, "add hl,de")) continue;

        sprintf(ld_hl_buf, "ld hl,%s", base);
        delete_n(i, j + 2 - i + 1);
        {
            char ld_l_buf[64]; int k;
            sprintf(ld_l_buf, "ld l,(ix%s)", off);
            insert_line_tagged(i,     ld_l_buf, "byte_global_ptr_array_addr");
            insert_line(i + 1,        "ld h,0");
            for (k = 0; k < S; k++)
                insert_line(i + 2 + k, "add hl,hl");
            insert_line(i + 2 + S, "ex de,hl");
            insert_line(i + 3 + S, ld_hl_buf);
            insert_line(i + 4 + S, "add hl,de");
        }
        changed = 1; if (i > 0) i--;
    }
    return changed;
}


/*
 * Byte variable pre-decrement with immediate zero/nonzero test.
 *
 * The dcc code generator emits this sequence for while (--n) when n is an
 * unsigned char local at (ix-K):
 *
 *   push ix
 *   pop hl
 *   [dec hl × K]        ; address form A: K consecutive "dec hl"
 *   push hl             ; or form B: ld de,-K; add hl,de
 *   ld l,(hl)
 *   ld h,0
 *   dec hl
 *   ld h,0              ; clamp H back to 0 after possible underflow
 *   ex de,hl
 *   pop hl
 *   ld (hl),e
 *   ex de,hl
 *   ld a,h
 *   or l
 *   jp z/nz, LABEL
 *
 * Becomes the optimal two-instruction form:
 *
 *   dec (ix-K)
 *   jp z/nz, LABEL
 */
static int pass_byte_ix_predec_zero_test(void)
{
    int i, j, K, lde, changed = 0;
    char cond[16], label[128], newdec[64], newjp[160], tmp[MAX_LINE];

    for (i = 0; i + 13 < nlines; i++) {
        if (!eq(i, "push ix")) continue;
        if (!eq(i + 1, "pop hl")) continue;

        j = i + 2;
        K = 0;

        /* Address form A: consecutive "dec hl" for small offsets */
        while (j < nlines && eq(j, "dec hl") && K < 128) {
            j++;
            K++;
        }

        /* Address form B: "ld de,-K; add hl,de" for any offset */
        if (K == 0) {
            if (!peep_parse_ld_de_signed(lines[j], &lde)) continue;
            if (lde >= 0 || lde < -128) continue;
            j++;
            if (!eq(j, "add hl,de")) continue;
            j++;
            K = -lde;
        }

        if (K <= 0 || K > 128) continue;
        if (j + 11 >= nlines) continue;

        if (!eq(j,      "push hl")) continue;
        if (!eq(j + 1,  "ld l,(hl)")) continue;
        if (!eq(j + 2,  "ld h,0")) continue;
        if (!eq(j + 3,  "dec hl")) continue;
        if (!eq(j + 4,  "ld h,0")) continue;
        if (!eq(j + 5,  "ex de,hl")) continue;
        if (!eq(j + 6,  "pop hl")) continue;
        if (!eq(j + 7,  "ld (hl),e")) continue;
        if (!eq(j + 8,  "ex de,hl")) continue;
        if (!eq(j + 9,  "ld a,h")) continue;
        if (!eq(j + 10, "or l")) continue;

        /* Must end with a conditional jump testing zero */
        strip_peep_comment_copy(tmp, lines[j + 11]);
        if (!peep_parse_any_cond_jump(tmp, cond, label)) continue;
        if (strcmp(cond, "z") != 0 && strcmp(cond, "nz") != 0) continue;

        sprintf(newdec, "dec (ix-%d)", K);
        sprintf(newjp, "jp %s, %s", cond, label);

        replace1_tagged(i, newdec, "byte_predec_zero");
        replace1(i + 1, newjp);
        delete_n(i + 2, j + 10 - i);

        changed = 1;
        if (i > 0) i--;
    }
    return changed;
}

/*
 * Zero-extended byte load into DE via HL push/pop roundtrip:
 *
 *   push hl
 *   ld l,(ix+N)
 *   ld h,0
 *   ex de,hl
 *   pop hl
 *
 * DE is always dead before the push, so we can load the byte directly
 * into DE without touching HL or the stack:
 *
 *   ld e,(ix+N)
 *   ld d,0
 */
static int pass_ix_byte_load_to_de(void)
{
    int i, changed = 0;
    char off[32], new_lo[64];

    for (i = 0; i + 4 < nlines; i++) {
        if (!eq(i, "push hl")) continue;
        if (!peep_parse_ld_l_ix(lines[i + 1], off)) continue;
        if (!eq(i + 2, "ld h,0")) continue;
        if (!eq(i + 3, "ex de,hl")) continue;
        if (!eq(i + 4, "pop hl")) continue;

        sprintf(new_lo, "ld e,(ix%s)", off);
        replace1_tagged(i, new_lo, "ix_byte_load_to_de");
        replace1(i + 1, "ld d,0");
        delete_n(i + 2, 3);

        changed = 1;
        if (i > 0) i--;
    }
    return changed;
}


/*
 * Binary operations (ADD, SUB, CMP, etc.) load the second operand via:
 *
 *   push hl             ; save HL (first operand 'a')
 *   ld l,(ix+N)         ; load second operand 'b' low
 *   ld h,(ix+N+1)       ; load second operand 'b' high
 *   ex de,hl            ; HL = old DE (stale), DE = b
 *   pop hl              ; HL = a (restored)
 *
 * DE is always dead before the push (it held a consumed frame pointer),
 * so we can load b directly into DE without touching the stack:
 *
 *   ld e,(ix+N)
 *   ld d,(ix+N+1)
 */
static int pass_ix_pair_load_to_de(void)
{
    int i, off, changed = 0;
    char new_lo[64], new_hi[64];

    for (i = 0; i + 4 < nlines; i++) {
        if (!eq(i, "push hl")) continue;
        if (!peep_parse_ld_ix_pair(lines[i + 1], lines[i + 2], &off)) continue;
        if (!eq(i + 3, "ex de,hl")) continue;
        if (!eq(i + 4, "pop hl")) continue;

        sprintf(new_lo, "ld e,(ix%+d)", off);
        sprintf(new_hi, "ld d,(ix%+d)", off + 1);
        replace1_tagged(i, new_lo, "ix_pair_load_to_de");
        replace1(i + 1, new_hi);
        delete_n(i + 2, 3);

        changed = 1;
        if (i > 0) i--;
    }

    return changed;
}

/*
 * BC-resident counterpart of pass_ix_pair_load_to_de above: a loop-scoped
 * register-allocation candidate (dcc_loop_regalloc.c) parked in BC loads
 * into DE via the same generic "push hl / load into hl / ex de,hl / pop hl"
 * scaffolding used for any expression operand, since dcc's codegen has no
 * AST-level knowledge, at a generic operand-evaluation call site, that the
 * operand happens to be a plain register-resident ident eligible for a
 * cheaper direct move - it just evaluates the operand into HL (whose REG_BC
 * branch is "ld l,c"/"ld h,b") like any other subexpression. As with the ix
 * case, DE is always dead before the push (it held a consumed frame
 * pointer), and BC itself is never disturbed by push hl/pop hl, so the
 * whole five-instruction dance collapses to a single register-to-register
 * move pair - cheaper even than the ix case, since there's no memory access
 * involved at all.
 */
static int pass_bc_pair_load_to_de(void)
{
    int i, changed = 0;

    for (i = 0; i + 4 < nlines; i++) {
        if (!eq(i, "push hl")) continue;
        if (!eq(i + 1, "ld l,c")) continue;
        if (!eq(i + 2, "ld h,b")) continue;
        if (!eq(i + 3, "ex de,hl")) continue;
        if (!eq(i + 4, "pop hl")) continue;

        replace1_tagged(i, "ld e,c", "bc_pair_load_to_de");
        replace1(i + 1, "ld d,b");
        delete_n(i + 2, 3);

        changed = 1;
        if (i > 0) i--;
    }
    return changed;
}

/* Returns 1 if instruction s is safe to skip over when checking whether a
 * reload of HL from (ix+off_lo)/(ix+off_hi) is redundant.  An instruction
 * is safe when it neither modifies L/H nor writes to the two stored slots. */
static int hl_store_reload_safe_intervening(const char *s, int off_lo, int off_hi)
{
    char tmp[MAX_LINE];
    char store_lo[64], store_hi[64];

    strip_peep_comment_copy(tmp, s);
    if (starts_label(tmp))          return 0; /* label: unknown incoming HL */
    /* Instructions that write to L or H */
    if (strncmp(tmp, "ld l,",   5) == 0) return 0;
    if (strncmp(tmp, "ld h,",   5) == 0) return 0;
    if (strncmp(tmp, "ld hl,",  6) == 0) return 0;
    if (strncmp(tmp, "add hl,", 7) == 0) return 0;
    if (strncmp(tmp, "sbc hl,", 7) == 0) return 0;
    if (strncmp(tmp, "adc hl,", 7) == 0) return 0;
    if (strcmp (tmp, "inc hl")      == 0) return 0;
    if (strcmp (tmp, "dec hl")      == 0) return 0;
    if (strcmp (tmp, "pop hl")      == 0) return 0;
    if (strcmp (tmp, "ex de,hl")    == 0) return 0;
    if (strcmp (tmp, "ex (sp),hl")  == 0) return 0;
    /* Jumps/calls: would need dataflow analysis */
    if (strncmp(tmp, "jp ",   3) == 0) return 0;
    if (strncmp(tmp, "jr ",   3) == 0) return 0;
    if (strncmp(tmp, "call ", 5) == 0) return 0;
    if (strcmp (tmp, "ret")       == 0) return 0;
    if (strncmp(tmp, "ret ",  4) == 0) return 0;
    /* Writes to the stored memory slots */
    sprintf(store_lo, "ld (ix%+d),", off_lo);
    sprintf(store_hi, "ld (ix%+d),", off_hi);
    if (strncmp(tmp, store_lo, strlen(store_lo)) == 0) return 0;
    if (strncmp(tmp, store_hi, strlen(store_hi)) == 0) return 0;
    return 1;
}

static int pass_remove_ix_store_reload_hl(void)
{
    int i, j, off, changed = 0;
    char expected_lo[64], expected_hi[64];

    for (i = 0; i + 3 < nlines; i++) {
        if (!peep_parse_st_ix_pair(lines[i], lines[i + 1], &off)) continue;
        sprintf(expected_lo, "ld l,(ix%+d)", off);
        sprintf(expected_hi, "ld h,(ix%+d)", off + 1);
        for (j = i + 2; j < nlines && j <= i + 10; j++) {
            if (eq(j, expected_lo)) {
                if (j + 1 < nlines && eq(j + 1, expected_hi)) {
                    delete_n(j, 2);
                    changed = 1;
                    if (i > 0) i--;
                }
                break;
            }
            if (!hl_store_reload_safe_intervening(lines[j], off, off + 1))
                break;
        }
    }
    return changed;
}

static int peep_parse_st_ix_de_pair(const char *s1, const char *s2, int *off)
{
    char tmp1[MAX_LINE], tmp2[MAX_LINE];
    char *p, *endp;
    int lo, hi;

    strip_peep_comment_copy(tmp1, s1);
    strip_peep_comment_copy(tmp2, s2);
    if (strncmp(tmp1, "ld (ix", 6) != 0)
        return 0;
    p = tmp1 + 6;
    lo = (int)strtol(p, &endp, 10);
    if (*endp != ')' || endp[1] != ',' || endp[2] != 'e' || endp[3] != 0)
        return 0;
    if (strncmp(tmp2, "ld (ix", 6) != 0)
        return 0;
    p = tmp2 + 6;
    hi = (int)strtol(p, &endp, 10);
    if (*endp != ')' || endp[1] != ',' || endp[2] != 'd' || endp[3] != 0 || hi != lo + 1)
        return 0;
    *off = lo;
    return 1;
}

static int inline_temp_line_preserves_de(const char *s)
{
    char tmp[MAX_LINE];

    strip_peep_comment_copy(tmp, s);
    if (strncmp(tmp, "call ", 5) == 0 || strncmp(tmp, "rst ", 4) == 0 ||
        strcmp(tmp, "ex de,hl") == 0 || strcmp(tmp, "exx") == 0 ||
        strcmp(tmp, "pop de") == 0 || strcmp(tmp, "inc de") == 0 ||
        strcmp(tmp, "dec de") == 0 || strcmp(tmp, "inc d") == 0 ||
        strcmp(tmp, "dec d") == 0 || strcmp(tmp, "inc e") == 0 ||
        strcmp(tmp, "dec e") == 0 || strcmp(tmp, "ldi") == 0 ||
        strcmp(tmp, "ldir") == 0 || strcmp(tmp, "ldd") == 0 ||
        strcmp(tmp, "lddr") == 0)
        return 0;
    if (strncmp(tmp, "ld de,", 6) == 0 || strncmp(tmp, "ld d,", 5) == 0 ||
        strncmp(tmp, "ld e,", 5) == 0 || strncmp(tmp, "rl d", 4) == 0 ||
        strncmp(tmp, "rl e", 4) == 0 || strncmp(tmp, "rr d", 4) == 0 ||
        strncmp(tmp, "rr e", 4) == 0 || strncmp(tmp, "sl d", 4) == 0 ||
        strncmp(tmp, "sl e", 4) == 0 || strncmp(tmp, "sr d", 4) == 0 ||
        strncmp(tmp, "sr e", 4) == 0 || strncmp(tmp, "set ", 4) == 0 ||
        strncmp(tmp, "res ", 4) == 0)
        return 0;
    return 1;
}

static int inline_temp_line_mentions_de(const char *s)
{
    char tmp[MAX_LINE];
    char token[32];
    int i, n;

    strip_peep_comment_copy(tmp, s);
    i = 0;
    while (tmp[i] != 0) {
        while (tmp[i] != 0 &&
               !isalnum((unsigned char)tmp[i]) && tmp[i] != '_')
            i++;
        n = 0;
        while (tmp[i] != 0 &&
               (isalnum((unsigned char)tmp[i]) || tmp[i] == '_')) {
            if (n + 1 < (int)sizeof(token))
                token[n++] = tmp[i];
            i++;
        }
        token[n] = 0;
        if (strcmp(token, "d") == 0 || strcmp(token, "e") == 0 ||
            strcmp(token, "de") == 0)
            return 1;
    }
    return 0;
}

static int inline_temp_byte_gap_preserves_a(const char *s)
{
    char tmp[MAX_LINE];
    size_t n;

    strip_peep_comment_copy(tmp, s);
    if (tmp[0] == 0 || strcmp(tmp, "push hl") == 0 ||
        strcmp(tmp, "inc hl") == 0 || strncmp(tmp, "ld hl,", 6) == 0)
        return 1;
    n = strlen(tmp);
    return strncmp(tmp, "ld (", 4) == 0 && n >= 4 &&
           strcmp(tmp + n - 3, ",hl") == 0;
}

static int inline_temp_line_mentions_bank_reg(const char *s)
{
    char tmp[MAX_LINE];
    char token[32];
    int i, n;

    strip_peep_comment_copy(tmp, s);
    i = 0;
    while (tmp[i] != 0) {
        while (tmp[i] != 0 &&
               !isalnum((unsigned char)tmp[i]) && tmp[i] != '_')
            i++;
        n = 0;
        while (tmp[i] != 0 &&
               (isalnum((unsigned char)tmp[i]) || tmp[i] == '_')) {
            if (n + 1 < (int)sizeof(token))
                token[n++] = tmp[i];
            i++;
        }
        token[n] = 0;
        if (strcmp(token, "h") == 0 || strcmp(token, "l") == 0 ||
            strcmp(token, "hl") == 0 || strcmp(token, "d") == 0 ||
            strcmp(token, "e") == 0 || strcmp(token, "de") == 0 ||
            strcmp(token, "b") == 0 || strcmp(token, "c") == 0 ||
            strcmp(token, "bc") == 0)
            return 1;
    }
    return 0;
}

static int inline_temp_exx_gap_safe(int first, int last)
{
    int i;
    int h = 0, l = 0, d = 0, e = 0, b = 0, c = 0;
    char tmp[MAX_LINE];

    for (i = first; i < last; ++i) {
        strip_peep_comment_copy(tmp, lines[i]);
        if (tmp[0] == 0)
            continue;
        if (starts_label(tmp) || strncmp(tmp, "call ", 5) == 0 ||
            strncmp(tmp, "rst ", 4) == 0 || strncmp(tmp, "jp ", 3) == 0 ||
            strncmp(tmp, "jr ", 3) == 0 || strncmp(tmp, "djnz ", 5) == 0 ||
            strcmp(tmp, "ret") == 0 || strncmp(tmp, "ret ", 4) == 0 ||
            strcmp(tmp, "exx") == 0 || strstr(tmp, "sp") != NULL)
            return 0;

        if (strncmp(tmp, "ld l,(ix", 8) == 0) { l = 1; continue; }
        if (strncmp(tmp, "ld h,(ix", 8) == 0) { h = 1; continue; }
        if (strncmp(tmp, "ld e,(ix", 8) == 0) { e = 1; continue; }
        if (strncmp(tmp, "ld d,(ix", 8) == 0) { d = 1; continue; }
        if (strncmp(tmp, "ld hl,", 6) == 0) { h = l = 1; continue; }
        if (strncmp(tmp, "ld de,", 6) == 0) { d = e = 1; continue; }
        if (strncmp(tmp, "ld bc,", 6) == 0) { b = c = 1; continue; }
        if (strcmp(tmp, "ld h,0") == 0) { h = 1; continue; }
        if (strcmp(tmp, "ld d,h") == 0) { if (!h) return 0; d = 1; continue; }
        if (strcmp(tmp, "ld e,l") == 0) { if (!l) return 0; e = 1; continue; }
        if (strcmp(tmp, "pop hl") == 0) { h = l = 1; continue; }
        if (strcmp(tmp, "pop de") == 0) { d = e = 1; continue; }
        if (strcmp(tmp, "pop bc") == 0) { b = c = 1; continue; }
        if (strcmp(tmp, "push ix") == 0 || strcmp(tmp, "pop ix") == 0)
            continue;
        if (strcmp(tmp, "push hl") == 0) { if (!h || !l) return 0; continue; }
        if (strcmp(tmp, "push de") == 0) { if (!d || !e) return 0; continue; }
        if (strcmp(tmp, "push bc") == 0) { if (!b || !c) return 0; continue; }
        if (strcmp(tmp, "inc hl") == 0 || strcmp(tmp, "dec hl") == 0) {
            if (!h || !l) return 0;
            continue;
        }
        if (strcmp(tmp, "add hl,hl") == 0) {
            if (!h || !l) return 0;
            continue;
        }
        if (strcmp(tmp, "add hl,de") == 0) {
            if (!h || !l || !d || !e) return 0;
            continue;
        }
        if (strncmp(tmp, "ld e,(hl)", 9) == 0) {
            if (!h || !l) return 0;
            e = 1;
            continue;
        }
        if (strncmp(tmp, "ld d,(hl)", 9) == 0) {
            if (!h || !l) return 0;
            d = 1;
            continue;
        }
        if (strcmp(tmp, "ex de,hl") == 0) {
            int old_h = h, old_l = l;
            h = d; l = e; d = old_h; e = old_l;
            continue;
        }
        if (strcmp(tmp, "ld a,h") == 0) { if (!h) return 0; continue; }
        if (strcmp(tmp, "ld a,l") == 0) { if (!l) return 0; continue; }
        if (strcmp(tmp, "ld a,d") == 0) { if (!d) return 0; continue; }
        if (strcmp(tmp, "ld a,e") == 0) { if (!e) return 0; continue; }
        if (strcmp(tmp, "ld d,a") == 0) { d = 1; continue; }
        if (strcmp(tmp, "ld e,a") == 0) { e = 1; continue; }
        if (strncmp(tmp, "ld (ix", 6) == 0) {
            if (strstr(tmp, ",h") != NULL && !h) return 0;
            if (strstr(tmp, ",l") != NULL && !l) return 0;
            if (strstr(tmp, ",d") != NULL && !d) return 0;
            if (strstr(tmp, ",e") != NULL && !e) return 0;
            continue;
        }
        if (inline_temp_line_mentions_bank_reg(tmp))
            return 0;
    }
    return 1;
}

/* Replace a straight-line, compiler-tagged single-use inline-argument spill
 * with a stack spill. The tag supplies the fact assembly cannot recover: no
 * later path can read this value from the frame slot. Control flow and direct
 * SP manipulation still decline the rewrite.
 *
 * The DE form accepts one outstanding `push hl` in the gap. It rotates that
 * saved HL value around the argument with pop hl/pop de/push hl, leaving the
 * stack and live registers exactly as the original reload/exchange did. */
static int pass_inline_temp_spill_to_stack(void)
{
    int i, j, off, changed = 0;
    int is_de, stack_depth, outstanding_push_hl;
    int de_preserved, de_untouched;
    int byte_a_safe, byte_hl_overwritten, byte_load_i;
    char expected_lo[64], expected_hi[64];
    char expected_de_lo[64], expected_de_hi[64], tmp[MAX_LINE];

    for (i = 0; i + 4 < nlines; ++i) {
        is_de = peep_parse_st_ix_de_pair(lines[i], lines[i + 1], &off);
        if (!is_de && !peep_parse_st_ix_pair(lines[i], lines[i + 1], &off))
            continue;
        if (strstr(lines[i + 2], ";@dcc-inline-temp-single-use") == NULL)
            continue;
        sprintf(expected_lo, "ld l,(ix%+d)", off);
        sprintf(expected_hi, "ld h,(ix%+d)", off + 1);
        sprintf(expected_de_lo, "ld e,(ix%+d)", off);
        sprintf(expected_de_hi, "ld d,(ix%+d)", off + 1);
        if (!is_de && i + 7 < nlines && eq(i + 3, "ld a,h") &&
            eq(i + 4, "rlca") && eq(i + 5, "sbc a,a") &&
            eq(i + 6, "ld d,a") && eq(i + 7, "ld e,a")) {
            replace1_tagged(i, "; inline temp remains in hl",
                            "inline_temp_hl_live");
            delete_n(i + 1, 2);
            changed = 1;
            if (i > 0) --i;
            continue;
        }
        stack_depth = 0;
        outstanding_push_hl = 0;
        de_preserved = 1;
        de_untouched = 1;
        byte_load_i = -1;
        if (!is_de && i >= 2 && eq(i - 2, "ld l,(hl)") && eq(i - 1, "ld h,0"))
            byte_load_i = i - 2;
        else if (!is_de && i >= 3 && eq(i - 3, "ld l,(hl)") &&
                 eq(i - 2, "ld h,0") && eq(i - 1, "ld h,0"))
            byte_load_i = i - 3;
        byte_a_safe = byte_load_i >= 0;
        byte_hl_overwritten = 0;

        for (j = i + 3; j < nlines && j <= i + 60; ++j) {
            strip_peep_comment_copy(tmp, lines[j]);
            if ((eq(j, expected_lo) && j + 1 < nlines && eq(j + 1, expected_hi)) ||
                (eq(j, expected_de_lo) && j + 1 < nlines && eq(j + 1, expected_de_hi))) {
                int reloads_de = eq(j, expected_de_lo);
                int followed_by_ex = j + 2 < nlines && eq(j + 2, "ex de,hl");
                if (!is_de && !reloads_de && j + 7 < nlines &&
                    eq(j + 2, "ld a,h") && eq(j + 3, "rlca") &&
                    eq(j + 4, "sbc a,a") && eq(j + 5, "ld d,a") &&
                    eq(j + 6, "ld e,a") && eq(j + 7, "pop bc") &&
                    inline_temp_exx_gap_safe(i + 3, j)) {
                    replace1_tagged(j, "exx", "inline_temp_exx");
                    delete_n(j + 1, 1);
                    replace1_tagged(i, "exx", "inline_temp_exx");
                    delete_n(i + 1, 2);
                    changed = 1;
                    if (i > 0) --i;
                } else if (reloads_de && is_de && de_preserved) {
                    delete_n(j, 2);
                    replace1_tagged(i, "; inline temp kept in de", "inline_temp_de_live");
                    delete_n(i + 1, 2);
                    changed = 1;
                    if (i > 0) --i;
                } else if (reloads_de && stack_depth == 0) {
                    replace1_tagged(i, is_de ? "push de" : "push hl",
                                    "inline_temp_spill_to_stack");
                    replace1(j, "pop de");
                    delete_n(j + 1, 1);
                    delete_n(i + 1, 2);
                    changed = 1;
                    if (i > 0) --i;
                } else if (byte_a_safe && followed_by_ex && j + 6 < nlines &&
                    eq(j + 3, "pop hl") && eq(j + 4, "ld (hl),e") &&
                    eq(j + 5, "inc hl") && eq(j + 6, "ld (hl),d")) {
                    replace1_tagged(byte_load_i, "ld a,(hl)", "inline_temp_byte_in_a");
                    replace1(j + 4, "ld (hl),a");
                    replace1(j + 6, "ld (hl),0");
                    delete_n(j, 3);
                    delete_n(byte_load_i + 1, i - byte_load_i + 2);
                    changed = 1;
                    i = byte_load_i > 0 ? byte_load_i - 1 : 0;
                } else if (is_de && followed_by_ex && de_preserved) {
                    delete_n(j, 3);
                    replace1_tagged(i, "; inline temp kept in de", "inline_temp_de_live");
                    delete_n(i + 1, 2);
                    changed = 1;
                    if (i > 0) --i;
                } else if (!is_de && followed_by_ex && de_untouched) {
                    delete_n(j, 3);
                    replace1_tagged(i, "ld d,h", "inline_temp_hl_to_de");
                    replace1(i + 1, "ld e,l");
                    delete_n(i + 2, 1);
                    changed = 1;
                    if (i > 0) --i;
                } else if (!is_de && stack_depth == 0 && !followed_by_ex) {
                    replace1_tagged(i, "push hl", "inline_temp_spill_to_stack");
                    replace1(j, "pop hl");
                    delete_n(j + 1, 1);
                    delete_n(i + 1, 1);
                    delete_n(i + 1, 1);
                    changed = 1;
                    if (i > 0) --i;
                } else if (followed_by_ex &&
                           (stack_depth == 0 ||
                            (stack_depth == 1 && outstanding_push_hl &&
                             j + 3 < nlines && eq(j + 3, "pop hl")))) {
                    replace1_tagged(i, is_de ? "push de" : "push hl",
                                    "inline_temp_spill_to_stack");
                    if (stack_depth == 0) {
                        replace1(j, "ex de,hl");
                        replace1(j + 1, "pop de");
                        delete_n(j + 2, 1);
                    } else {
                        replace1(j, "pop hl");
                        replace1(j + 1, "pop de");
                        replace1(j + 2, "push hl");
                    }
                    delete_n(i + 1, 1);
                    delete_n(i + 1, 1);
                    changed = 1;
                    if (i > 0) --i;
                }
                break;
            }
            if (starts_label(tmp) || strncmp(tmp, "jp ", 3) == 0 ||
                strncmp(tmp, "jr ", 3) == 0 || strncmp(tmp, "djnz ", 5) == 0 ||
                strcmp(tmp, "ret") == 0 || strncmp(tmp, "ret ", 4) == 0 ||
                strstr(tmp, "sp") != NULL || strcmp(tmp, "ex (sp),hl") == 0)
                break;
            if (!inline_temp_line_preserves_de(tmp))
                de_preserved = 0;
            if (inline_temp_line_mentions_de(tmp))
                de_untouched = 0;
            if (byte_a_safe && !byte_hl_overwritten && tmp[0] != 0) {
                if (strncmp(tmp, "ld hl,", 6) == 0)
                    byte_hl_overwritten = 1;
                else
                    byte_a_safe = 0;
            }
            if (!inline_temp_byte_gap_preserves_a(tmp))
                byte_a_safe = 0;
            if (strncmp(tmp, "push ", 5) == 0) {
                if (stack_depth == 0)
                    outstanding_push_hl = strcmp(tmp, "push hl") == 0;
                stack_depth++;
            } else if (strncmp(tmp, "pop ", 4) == 0) {
                if (stack_depth == 0)
                    break;
                stack_depth--;
                if (stack_depth == 0)
                    outstanding_push_hl = 0;
            }
        }
    }
    return changed;
}

static int pass_remove_inline_temp_markers(void)
{
    int i, changed = 0;

    for (i = nlines - 1; i >= 0; --i) {
        if (strstr(lines[i], ";@dcc-inline-temp-single-use") != NULL) {
            delete_n(i, 1);
            changed = 1;
        }
    }
    return changed;
}

static int count_jumps_to_label(const char *label)
{
    int k, count = 0;
    char lab[128], cond[16];
    for (k = 0; k < nlines; k++) {
        if (peep_parse_any_cond_jump(lines[k], cond, lab) && strcmp(lab, label) == 0)
            count++;
        else if (peep_parse_jp_uncond_label(lines[k], lab) && strcmp(lab, label) == 0)
            count++;
    }
    return count;
}

static int pass_bool_from_cmp(void)
{
    int i, changed = 0;
    char ltrue[128], lexit[128], cond[16], new_jp[256];
    const char *inv_cond;

    for (i = 0; i + 5 < nlines; i++) {
        if (!peep_parse_any_cond_jump(lines[i], cond, ltrue)) continue;
        if (!eq(i + 1, "ld hl,0")) continue;
        if (!peep_parse_jp_uncond_label(lines[i + 2], lexit)) continue;
        if (!line_is_label_name(i + 3, ltrue)) continue;
        if (!eq(i + 4, "ld hl,1")) continue;
        if (!line_is_label_name(i + 5, lexit)) continue;
        if (strcmp(ltrue, lexit) == 0) continue;
        /* Only safe to delete Ltrue: if no other jump targets it */
        if (count_jumps_to_label(ltrue) != 1) continue;
        inv_cond = peep_inverse_cond(cond);
        if (!inv_cond) continue;
        peep_make_cond_jump(new_jp, sizeof(new_jp), inv_cond, lexit);
        replace1_tagged(i, "ld hl,0", "bool_from_cmp");
        replace1(i + 1, new_jp);
        replace1(i + 2, "inc l");
        delete_n(i + 3, 2);
        changed = 1;
        if (i > 0) i--;
    }
    return changed;
}

/* Return 1 if the flags set by inc (ix+d) — Z, S, H, PV but NOT C — are
 * guaranteed dead at `start` (i.e. overwritten before any conditional branch
 * that reads them).  Returns 0 when uncertain (conservative). */
static int flags_dead_from(int start)
{
    int j;
    char tmp[MAX_LINE];
    for (j = start; j < start + 20 && j < nlines; j++) {
        strip_peep_comment_copy(tmp, lines[j]);
        /* Label: flags may arrive from a different path — stop scanning */
        if (starts_label(tmp)) return 0;
        /* Instructions that clobber Z/S/H/PV before any branch can read them */
        if (strncmp(tmp, "or ",   3) == 0) return 1;
        if (strcmp (tmp, "or a")    == 0)  return 1;
        if (strncmp(tmp, "and ",  4) == 0) return 1;
        if (strncmp(tmp, "xor ",  4) == 0) return 1;
        if (strncmp(tmp, "cp ",   3) == 0) return 1;
        if (strncmp(tmp, "add a,",6) == 0) return 1;
        if (strncmp(tmp, "sub ",  4) == 0) return 1;
        if (strncmp(tmp, "sbc a,",6) == 0) return 1;
        if (strncmp(tmp, "adc a,",6) == 0) return 1;
        if (strncmp(tmp, "sbc hl,",7)== 0) return 1;
        if (strncmp(tmp, "call ", 5) == 0) return 1;  /* callee overwrites flags */
        /* Conditional branches that read the flags inc modifies */
        if (strncmp(tmp, "jp z,",  5) == 0) return 0;
        if (strncmp(tmp, "jp nz,", 6) == 0) return 0;
        if (strncmp(tmp, "jp m,",  5) == 0) return 0;
        if (strncmp(tmp, "jp p,",  5) == 0) return 0;
        if (strncmp(tmp, "jp pe,", 6) == 0) return 0;
        if (strncmp(tmp, "jp po,", 6) == 0) return 0;
        if (strncmp(tmp, "jr z,",  5) == 0) return 0;
        if (strncmp(tmp, "jr nz,", 6) == 0) return 0;
        if (strncmp(tmp, "call z,",  7) == 0) return 0;
        if (strncmp(tmp, "call nz,", 8) == 0) return 0;
        if (strncmp(tmp, "ret z",  5) == 0) return 0;
        if (strncmp(tmp, "ret nz", 6) == 0) return 0;
        if (strncmp(tmp, "ret m",  5) == 0) return 0;
        if (strncmp(tmp, "ret p",  5) == 0) return 0;
        if (strncmp(tmp, "ret pe", 6) == 0) return 0;
        if (strncmp(tmp, "ret po", 6) == 0) return 0;
    }
    return 0;  /* conservative: scan limit exceeded or unknown */
}

/* Replace the 16-bit post-increment-through-stack pattern:
 *   push hl / ld l,(ix+N) / ld h,(ix+N+1) /
 *   push hl / inc hl / ld (ix+N),l / ld (ix+N+1),h / pop hl
 * with:
 *   push hl / ld l,(ix+N) / ld h,(ix+N+1) /
 *   inc (ix+N) / jr nz,Lskip / inc (ix+N+1) / Lskip:
 *
 * After the pattern HL still holds the old (pre-increment) value of (ix+N),
 * as required by the callers (e.g. "in = &code[pc++]").
 * Saves 35T in the common (no-carry) case.  Only applied when the flags
 * set by inc (ix+N) are dead before any conditional branch (flags_dead_from). */
static int pass_postinc_ix_word(void)
{
    int i, off, off_store, changed = 0;
    char inc_lo[64], inc_hi[64], jr_skip[96], skip_label[64], skip_def[72];

    for (i = 0; i + 7 < nlines; i++) {
        if (!eq(i, "push hl")) continue;
        if (!peep_parse_ld_ix_pair(lines[i + 1], lines[i + 2], &off)) continue;
        if (!eq(i + 3, "push hl")) continue;
        if (!eq(i + 4, "inc hl")) continue;
        if (!peep_parse_st_ix_pair(lines[i + 5], lines[i + 6], &off_store)) continue;
        if (off_store != off) continue;
        if (!eq(i + 7, "pop hl")) continue;
        if (!flags_dead_from(i + 8)) continue;

        sprintf(skip_label, "Lincw_%d", i); /* see Lskrl_'s rationale above */
        sprintf(inc_lo,    "inc (ix%+d)", off);
        sprintf(inc_hi,    "inc (ix%+d)", off + 1);
        sprintf(jr_skip,   "jr nz, %s",   skip_label);
        sprintf(skip_def,  "%s:",          skip_label);

        replace1_tagged(i + 3, inc_lo, "postinc_ix_word");
        replace1(i + 4, jr_skip);
        replace1(i + 5, inc_hi);
        replace1(i + 6, skip_def);
        delete_n(i + 7, 1);

        changed = 1;
        if (i > 0) i--;
    }
    return changed;
}

/* Fold `cp N; jp z, L1; jp nc, L2; L1:` into `cp N+1; jp nc, L2; L1:`.
 * Both forms mean "if A <= N, fall through to L1; else goto L2".
 * Eliminates one branch on the hot path; saves 10T.
 * Valid when N < 255 so N+1 stays in the byte range. */
static int pass_cp_jz_jpnc(void)
{
    int i, n, changed = 0;
    char cond1[16], cond2[16], label1[128], label2[128];
    char tmp[MAX_LINE], new_cp[32];
    char *endp;

    for (i = 0; i + 3 < nlines; i++) {
        strip_peep_comment_copy(tmp, lines[i]);
        if (strncmp(tmp, "cp ", 3) != 0) continue;
        n = (int)strtol(tmp + 3, &endp, 10);
        if (*endp != 0 || n < 0 || n > 254) continue;

        if (!peep_parse_any_cond_jump(lines[i + 1], cond1, label1)) continue;
        if (strcmp(cond1, "z") != 0) continue;

        if (!peep_parse_any_cond_jump(lines[i + 2], cond2, label2)) continue;
        if (strcmp(cond2, "nc") != 0) continue;

        if (!line_is_label_name(i + 3, label1)) continue;

        sprintf(new_cp, "cp %d", n + 1);
        replace1_tagged(i, new_cp, "cp_jz_jpnc");
        delete_n(i + 1, 1);

        changed = 1;
        if (i > 0) i--;
    }
    return changed;
}

/*
 * Fold unsigned less-than-or-equal compare:
 *
 *   cp N
 *   jp z, LABEL     ; jump if A == N
 *   jp c, LABEL     ; jump if A < N  (same label)
 *
 * Combined: jump when A <= N, i.e. A < N+1.  Becomes:
 *
 *   cp N+1
 *   jp c, LABEL
 */
static int pass_cp_jz_jpc(void)
{
    int i, n, changed = 0;
    char cond1[16], cond2[16], label1[128], label2[128];
    char tmp[MAX_LINE], new_cp[32];
    char *endp;

    for (i = 0; i + 2 < nlines; i++) {
        strip_peep_comment_copy(tmp, lines[i]);
        if (strncmp(tmp, "cp ", 3) != 0) continue;
        n = (int)strtol(tmp + 3, &endp, 10);
        if (*endp != 0 || n < 0 || n > 254) continue;

        if (!peep_parse_any_cond_jump(lines[i + 1], cond1, label1)) continue;
        if (strcmp(cond1, "z") != 0) continue;

        if (!peep_parse_any_cond_jump(lines[i + 2], cond2, label2)) continue;
        if (strcmp(cond2, "c") != 0) continue;

        if (strcmp(label1, label2) != 0) continue;

        sprintf(new_cp, "cp %d", n + 1);
        replace1_tagged(i, new_cp, "cp_jz_jpc");
        delete_n(i + 1, 1);

        changed = 1;
        if (i > 0) i--;
    }
    return changed;
}

/*
 * General signed 16-bit compare against a constant: fold the constant's half
 * of the sign bias at compile time.
 *
 * DCC implements a signed compare by flipping bit 15 (xor 8000h) of BOTH
 * operands so a plain unsigned SBC produces the correct signed carry:
 *
 *     ld de,CONST          ld de,(CONST XOR 8000h)
 *     ld a,h               ld a,h
 *     xor 80h              xor 80h
 *     ld h,a          ==>  ld h,a
 *     ld a,d               or a
 *     xor 80h              sbc hl,de
 *     ld d,a
 *     or a
 *     sbc hl,de
 *
 * When the right operand is a compile-time constant held in DE, its bias is a
 * constant too: biasing DE at run time (ld a,d / xor 80h / ld d,a) is identical
 * to loading the already-biased immediate.  The XOR only affects D (the high
 * byte = bit 15), so E is unchanged, matching ld de,(CONST XOR 8000h).
 *
 * The H bias stays because the left operand is not known here.  This is the
 * general fallback for signed const compares the more specific passes above do
 * not cover (loop-back non-negative counters, low-byte-zero constants,
 * zero-extended bytes).  Those collapse to smaller code and run first; this
 * only fires on what remains.  Saves 3 instructions (4 bytes, ~12 T-states)
 * per site.  Requiring the trailing "or a / sbc hl,de" guarantees the DE value
 * is consumed only by this comparison.
 */
static int pass_signed_cmp_const_bias_fold(void)
{
    int i;
    int changed;
    int imm;
    unsigned int biased;
    char line[128];

    changed = 0;

    for (i = 0; i + 8 < nlines; ++i) {
        if (!peep_parse_ld_de_signed(lines[i], &imm))
            continue;
        if (!eq(i + 1, "ld a,h"))
            continue;
        if (!eq(i + 2, "xor 80h"))
            continue;
        if (!eq(i + 3, "ld h,a"))
            continue;
        if (!eq(i + 4, "ld a,d"))
            continue;
        if (!eq(i + 5, "xor 80h"))
            continue;
        if (!eq(i + 6, "ld d,a"))
            continue;
        if (!eq(i + 7, "or a"))
            continue;
        if (!eq(i + 8, "sbc hl,de"))
            continue;

        biased = ((unsigned int)imm ^ 0x8000u) & 0xffffu;
        sprintf(line, "ld de,%u", biased);
        replace1_tagged(i, line, "signed_cmp_const_bias_fold");
        /* Keep the H bias at i+1..i+3; delete the D bias triple at i+4..i+6. */
        delete_n(i + 4, 3);
        changed = 1;
        if (i > 0)
            --i;
    }

    return changed;
}

static int signed_zero_branch_emit(char out[][160], int *nout,
                                   const char *cond, const char *label,
                                   int *last_test)
{
    char jump_cond[8];
    int test_kind;

    if (!strcmp(cond, "z") || !strcmp(cond, "nz")) {
        test_kind = 1;
        strcpy(jump_cond, cond);
    } else if (!strcmp(cond, "c")) {
        test_kind = 2;
        strcpy(jump_cond, "nz");
    } else if (!strcmp(cond, "nc")) {
        test_kind = 2;
        strcpy(jump_cond, "z");
    } else {
        return 0;
    }

    if (*last_test != test_kind) {
        if (test_kind == 1) {
            strcpy(out[(*nout)++], "ld a,h");
            strcpy(out[(*nout)++], "or l");
        } else {
            strcpy(out[(*nout)++], "bit 7,h");
        }
        *last_test = test_kind;
    }

    sprintf(out[(*nout)++], "jp %s, %s", jump_cond, label);
    return 1;
}

/*
 * After pass_signed_cmp_const_bias_fold, a signed comparison against zero is:
 *
 *   ld de,32768
 *   ld a,h
 *   xor 80h
 *   ld h,a
 *   or a
 *   sbc hl,de
 *   jp cc,L
 *
 * The subtract's useful facts are just: Z iff HL was zero, C iff signed HL
 * was negative.  Use a direct zero test for z/nz branches and a sign-bit test
 * for c/nc branches.  Handle up to two adjacent conditional jumps because DCC
 * commonly emits combined relation tests such as z+c for <=.
 */
static int pass_signed_zero_branch(void)
{
    int i;
    int changed;

    changed = 0;

    for (i = 0; i + 6 < nlines; ++i) {
        char cond1[16];
        char cond2[16];
        char lab1[128];
        char lab2[128];
        char unused_lab[128];
        char out[6][160];
        int ncond;
        int nout;
        int last_test;
        int k;

        if (!eq(i, "ld de,32768"))
            continue;
        if (!eq(i + 1, "ld a,h"))
            continue;
        if (!eq(i + 2, "xor 80h"))
            continue;
        if (!eq(i + 3, "ld h,a"))
            continue;
        if (!eq(i + 4, "or a"))
            continue;
        if (!eq(i + 5, "sbc hl,de"))
            continue;
        if (!peep_parse_any_cond_jump(lines[i + 6], cond1, lab1))
            continue;

        ncond = 1;
        if (i + 7 < nlines && peep_parse_any_cond_jump(lines[i + 7], cond2, lab2)) {
            ncond = 2;
            if (i + 8 < nlines && peep_parse_any_cond_jump(lines[i + 8], cond2, unused_lab))
                continue;
        }

        nout = 0;
        last_test = 0;
        if (!signed_zero_branch_emit(out, &nout, cond1, lab1, &last_test))
            continue;
        if (ncond == 2 && !signed_zero_branch_emit(out, &nout, cond2, lab2, &last_test))
            continue;

        replace1_tagged(i, out[0], "signed_zero_branch");
        for (k = 1; k < nout; ++k)
            replace1(i + k, out[k]);
        delete_n(i + nout, 6 + ncond - nout);

        changed = 1;
        if (i > 0)
            --i;
    }

    return changed;
}





static int pass_call_hl_stack_roundtrip(void)
{
    int i;
    int calli;
    int changed;

    changed = 0;

    /*
     * New direct function-pointer-array calls can generate:
     *
     *     ex de,hl        ; HL = function pointer
     *     push hl
     *     ld hl,0
     *     add hl,sp
     *     ld e,(hl)
     *     inc hl
     *     ld d,(hl)
     *     ex de,hl
     *     [extrn __call_hl]
     *     call __call_hl
     *     pop bc
     *
     * Since HL already contains the function pointer before the push, the
     * stack round-trip is pointless.  Keep the first ex de,hl and call
     * __call_hl directly.
     */
    for (i = 0; i + 9 < nlines; ++i) {
        if (!(eq(i,     "ex de,hl") &&
              eq(i + 1, "push hl") &&
              eq(i + 2, "ld hl,0") &&
              eq(i + 3, "add hl,sp") &&
              eq(i + 4, "ld e,(hl)") &&
              eq(i + 5, "inc hl") &&
              eq(i + 6, "ld d,(hl)") &&
              eq(i + 7, "ex de,hl")))
            continue;

        calli = i + 8;
        if (eq(calli, "extrn __call_hl"))
            ++calli;

        if (calli + 1 >= nlines)
            continue;
        if (!eq(calli, "call __call_hl") || !eq(calli + 1, "pop bc"))
            continue;

        /* Delete push/reload/second-ex, and delete the pop.  Leave optional extrn. */
        delete_n(i + 1, 7);
        calli -= 7;
        if (eq(calli, "extrn __call_hl"))
            ++calli;
        if (eq(calli + 1, "pop bc"))
            delete_n(calli + 1, 1);

        replace1_tagged(calli, "call __call_hl", "call_hl_stack_roundtrip");
        changed = 1;
        if (i > 0)
            --i;
    }

    return changed;
}

static int pass_minmax_winner_result_no_temp(void)
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

static int pass_minmax_score_b_cache(void)
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

static int pass_shrink_minmax_frame3_after_score_cache(void)
{
    int start;
    int end;
    int i;

    if (!peep_in_function_range("_MinMax:", &start, &end))
        return 0;
    if (peep_range_has_debug_annotations(start, end))
        return 0;

    for (i = start; i < end; ++i) {
        if (strstr(lines[i], "(ix-4)") != NULL)
            return 0;
    }

    for (i = start; i + 2 < end; ++i) {
        if (eq(i, "ld hl,-4") &&
            eq(i + 1, "add hl,sp") &&
            eq(i + 2, "ld sp,hl")) {
            replace1_tagged(i, "ld hl,-3", "shrink_minmax_frame3");
            return 1;
        }
    }

    return 0;
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
static int pass_minmax_loop_ctr_b(void)
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
 * pass_shrink_minmax_frame2_after_loop_ctr_b:
 *
 * After pass_minmax_loop_ctr_b removes all (ix-3) references, the MinMax
 * frame only needs 2 bytes: (ix-1) = value, (ix-2) = pieceMove.
 * Shrink the allocation from ld hl,-3 to ld hl,-2.
 */
static int pass_shrink_minmax_frame2_after_loop_ctr_b(void)
{
    int start, end, i;

    if (!peep_in_function_range("_MinMax:", &start, &end))
        return 0;
    if (peep_range_has_debug_annotations(start, end))
        return 0;

    for (i = start; i < end; ++i) {
        if (strstr(lines[i], "(ix-3)") != NULL)
            return 0;
    }

    for (i = start; i + 2 < end; ++i) {
        if (eq(i, "ld hl,-3") &&
            eq(i + 1, "add hl,sp") &&
            eq(i + 2, "ld sp,hl")) {
            replace1_tagged(i, "ld hl,-2", "shrink_minmax_frame2");
            return 1;
        }
    }

    return 0;
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
static int pass_minmax_value_c(void)
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
static int pass_minmax_board_ptr_loop(void)
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
static int pass_minmax_byte_returns(void)
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

/*
 * pass_minmax_pack_args:
 *
 * MinMax takes 4 ttt_t (uint8_t) parameters: alpha, beta, depth, move.
 * DCC passes each as a 16-bit word with H=0, using 4 pushes (8 bytes).
 * We can pack them into 2 words like ZCC does, saving ~46T per recursive call.
 *
 * New stack layout (low offset = top of stack = most-recently pushed):
 *   ix+4 = L of push hl = alpha (was ix+4)
 *   ix+5 = H of push hl = beta  (was ix+6)
 *   ix+6 = C of push bc = depth (was ix+8)
 *   ix+7 = B of push bc = move  (was ix+10)
 *
 * Phase 1: translate all frame accesses inside _MinMax (and _FindSolution).
 * Phase 2: transform the recursive self-call from 4 separate pushes to 2 packed.
 * Phase 3: transform FindSolution's call to MinMax to match the new convention.
 *
 * All three phases run in sequence within a single pass function.
 *
 * Guard: only runs while (ix+10) references still exist in _MinMax.
 * After phase 1 fires, (ix+10) is gone and the guard prevents re-firing.
 */

/* Helper: replace first occurrence of 'from' in 'buf' with 'to' (may differ in length). */
static void pack_str_replace(char *buf, const char *from, const char *to)
{
    char *p = strstr(buf, from);
    if (!p) return;
    size_t fl = strlen(from), tl = strlen(to);
    memmove(p + tl, p + fl, strlen(p + fl) + 1);
    memcpy(p, to, tl);
}

static int pass_minmax_pack_call(void);

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
static int pass_minmax_pack_frame(void)
{
    int start, end, i, changed = 0;
    char newline[MAX_LINE];
    char **backup;

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

    backup = (char **)malloc((size_t)(end - start) * sizeof(char *));
    for (i = start; i < end; i++)
        backup[i - start] = xstrdup2(lines[i]);

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
        for (i = start; i < end; i++)
            replace1(i, backup[i - start]);
        changed = 0;
    }

    for (i = start; i < end; i++)
        free(backup[i - start]);
    free(backup);

    return changed;
}

/* pass_minmax_pack_call: Phase 2 + Phase 3.
 * Transforms the recursive self-call from 4 separate 16-bit pushes to 2
 * packed word pushes, and updates FindSolution's call similarly.
 * Fires only after pass_minmax_pack_frame has run (ix+10 gone, ix+5 present). */
static int pass_minmax_pack_call(void)
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
}  /* pass_minmax_pack_call */

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
static int pass_minmax_save_board_addr(void)
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
        if (!parse_ld_hl_imm(lines[j], addr))               continue;
        if (strcmp(addr, "_g_board") != 0)                   continue; j++;
        if (!stride_parse_ld_r_ix_neg(lines[j], 'e', &k2))  continue;
        if (k2 != K)                                         continue; j++;
        if (!eq(j, "ld d,0"))                               continue; j++;
        if (!eq(j, "add hl,de"))                            continue; j++;

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

static int pass_store_l_reload_a(void)
{
    int i;
    int changed;
    int off1;
    char off2[32];
    char tmp[MAX_LINE];
    char *p;
    char *endp;

    changed = 0;

    for (i = 0; i + 1 < nlines; ++i) {
        strip_peep_comment_copy(tmp, lines[i]);
        if (strncmp(tmp, "ld (ix", 6) != 0)
            continue;
        p = tmp + 6;
        off1 = (int)strtol(p, &endp, 10);
        if (*endp != ')' || endp[1] != ',' || endp[2] != 'l' || endp[3] != 0)
            continue;
        if (!peep_parse_ld_a_ix(lines[i + 1], off2))
            continue;
        if (off1 != (int)strtol(off2, NULL, 10))
            continue;

        replace1_tagged(i + 1, "ld a,l", "store_l_reload_a");
        changed = 1;
    }

    return changed;
}

static int pass_reuse_board_addr_for_zero_store(void)
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

static int pass_array_base_push_to_de(void)
{
    int i;
    int changed;
    char base[128], index[128];

    changed = 0;

    for (i = 0; i + 7 < nlines; ++i) {
        if (parse_ld_hl_imm(lines[i], base) &&
            eq(i + 1, "push hl") &&
            peep_parse_ld_l_ix(lines[i + 2], base + 100) &&
            eq(i + 3, "ld h,0") &&
            eq(i + 4, "add hl,hl") &&
            eq(i + 5, "ex de,hl") &&
            eq(i + 6, "pop hl") &&
            eq(i + 7, "add hl,de")) {
            char line[180];
            replace1_tagged(i, lines[i + 2], "array_base_to_de");
            replace1(i + 1, "ld h,0");
            replace1(i + 2, "add hl,hl");
            sprintf(line, "ld de,%s", base);
            replace1(i + 3, line);
            replace1(i + 4, "add hl,de");
            delete_n(i + 5, 3);
            changed = 1;
            if (i > 0) --i;
        }

        if (i + 10 < nlines &&
            parse_ld_hl_imm(lines[i], base) && base[0] != '(' &&
            eq(i + 1, "push hl") &&
            parse_ld_hl_imm(lines[i + 2], index) && index[0] == '(' &&
            eq(i + 3, "push hl") &&
            eq(i + 4, "inc hl") &&
            eq(i + 6, "pop hl") &&
            eq(i + 7, "add hl,hl") &&
            eq(i + 8, "ex de,hl") &&
            eq(i + 9, "pop hl") &&
            eq(i + 10, "add hl,de") &&
            peep_de_dead_at(i + 11)) {
            char store[128], expected_store[132], line[180];

            strip_peep_comment_copy(store, lines[i + 5]);
            sprintf(expected_store, "ld %s,hl", index);
            if (strcmp(store, expected_store) != 0)
                continue;

            delete_n(i, 2);
            replace1_tagged(i, lines[i], "array_base_to_de");
            sprintf(line, "ld de,%s", base);
            replace1(i + 6, line);
            replace1(i + 7, "add hl,de");
            delete_n(i + 8, 1);
            changed = 1;
            if (i > 0) --i;
        }
    }

    return changed;
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
static int local_alloc_hl_result_dead(int start)
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

static int pass_once(void)
{
    int i;
    int changed;
    char v[128];
    char out[256];

    changed = 0;

    for (i = 0; i < nlines; i++) {
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
            changed = 1;
            if (i > 0) i--;
            continue;
        }

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

                changed = 1;
                if (i > 0) i--;
                continue;
            }
        }

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
            changed = 1;
            if (i > 0) i--;
            continue;
        }

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
            changed = 1;
            if (i > 0) i--;
            continue;
        }

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
                changed = 1;
                if (i > 0) i--;
                continue;
            }
        }

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

                changed = 1;
                if (i > 0) i--;
                continue;
            }
        }

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

                changed = 1;
                if (i > 0) i--;
                continue;
            }
        }

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
                changed = 1;
                if (i > 0) i--;
                continue;
            }
        }

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
                changed = 1;
                if (i > 0) i--;
                continue;
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
                changed = 1;
                if (i > 0) i--;
                continue;
            }
        }

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
         */
        if (eq(i, "ld hl,-1") &&
            eq(i + 1, "add hl,sp") &&
            eq(i + 2, "ld sp,hl") &&
            local_alloc_hl_result_dead(i + 3)) {
            replace1_tagged(i, "dec sp", "local_alloc_1");
            delete_n(i + 1, 2);
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        if (eq(i, "ld hl,-2") &&
            eq(i + 1, "add hl,sp") &&
            eq(i + 2, "ld sp,hl") &&
            local_alloc_hl_result_dead(i + 3)) {
            replace1_tagged(i, "dec sp", "local_alloc_2");
            replace1(i + 1, "dec sp");
            delete_n(i + 2, 1);
            changed = 1;
            if (i > 0) i--;
            continue;
        }

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
                    changed = 1;
                    if (i > 0) i--;
                    continue;
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
                    changed = 1;
                    if (i > 0) i--;
                    continue;
                }
            }
        }

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
                changed = 1;
                if (i > 0) i--;
                continue;
            }
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

        /* Small positive address offsets.  16-bit INC HL does not affect flags.
         * Only use where the next instruction is not a conditional branch. */
        if (eq(i, "ld de,1") && eq(i + 1, "add hl,de") &&
            i + 2 < nlines && strncmp(lines[i + 2], "jp ", 3) != 0) {
            replace1_tagged(i, "inc hl", "ld_de1_to_inc");
            delete_n(i + 1, 1);
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        if (eq(i, "ld de,2") && eq(i + 1, "add hl,de") &&
            i + 2 < nlines && strncmp(lines[i + 2], "jp ", 3) != 0) {
            replace1_tagged(i, "inc hl", "ld_de2_to_inc");
            replace1(i + 1, "inc hl");
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        if (eq(i, "ld de,3") && eq(i + 1, "add hl,de") &&
            i + 2 < nlines && strncmp(lines[i + 2], "jp ", 3) != 0) {
            replace1_tagged(i, "inc hl", "ld_de3_to_inc");
            replace1(i + 1, "inc hl");
            insert_line(i + 2, "inc hl");
            changed = 1;
            if (i > 0) i--;
            continue;
        }

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
         */
        if (eq(i, "ld de,1") &&
            eq(i + 1, "or a") &&
            eq(i + 2, "sbc hl,de") &&
            i + 3 < nlines &&
            strncmp(lines[i + 3], "jp ", 3) != 0) {
            replace1_tagged(i, "dec hl", "sbc_de1_to_dec");
            delete_n(i + 1, 2);
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        /* Same-register push/pop has no register or flag effect. */
        if ((eq(i, "push hl") && eq(i + 1, "pop hl")) ||
            (eq(i, "push de") && eq(i + 1, "pop de")) ||
            (eq(i, "push bc") && eq(i + 1, "pop bc")) ||
            (eq(i, "push af") && eq(i + 1, "pop af")) ||
            (eq(i, "push ix") && eq(i + 1, "pop ix"))) {
            delete_n(i, 2);
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        /* Two exchanges cancel exactly. */
        if (eq(i, "ex de,hl") && eq(i + 1, "ex de,hl")) {
            delete_n(i, 2);
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
            parse_ld_hl_imm(lines[i + 1], v) &&
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
            parse_ld_de_imm(lines[i + 1], v) &&
            eq(i + 2, "pop hl")) {
            sprintf(out, "ld de,%s", v);
            replace1_tagged(i, out, "push_lde_pop");
            delete_n(i + 1, 2);
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        /*
         * Caller cleanup that preserves HL return value:
         *   ex de,hl / ld hl,N / add hl,sp / ld sp,hl / ex de,hl
         * becomes N copies of inc sp for small even N.  This keeps HL
         * unchanged and adjusts SP by the same amount.  It intentionally
         * avoids changing condition flags.
         */
        if (eq(i, "ex de,hl") &&
            i + 4 < nlines &&
            parse_ld_hl_imm(lines[i + 1], v) &&
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
                changed = 1;
                if (i > 0) i--;
                continue;
            }
        }

        /*
         * Code after an unconditional jump is unreachable until the next
         * label.  Delete one non-label instruction at a time.
         */
        if (is_uncond_jp(lines[i]) &&
            i + 1 < nlines &&
            !starts_label(lines[i + 1]) &&
            !is_blank_or_comment(lines[i + 1])) {
            delete_n(i + 1, 1);
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        /* Unconditional jump to immediately following label. */
        if (is_jp_to_next_label(i)) {
            delete_n(i, 1);
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
                changed = 1;
                if (i > 0) i--;
                continue;
            }
        }

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

/*
 * pass_cond_skip_shortcut:
 *
 * Pattern:
 *   jp cc, LSKIP    ; conditional jump over one instruction
 *   INSTR           ; one non-label, non-jump instruction
 *   LSKIP:          ; skip target
 *   jp LDEST        ; unconditional jump to real destination
 *
 * Replace the conditional jump so it points directly at LDEST.
 * LSKIP becomes unreferenced and will be removed by pass_labels.
 *
 * This avoids the two-jump cost (10T + 10T) when the condition fires,
 * replacing it with a single direct jump (10T).
 */
static int pass_cond_skip_shortcut(void)
{
    int i;
    int changed = 0;

    for (i = 0; i + 3 < nlines; i++) {
        char lskip[128], ldest[128], new_jp[256];
        const char *cond = NULL;

        if      (parse_jp_nz_label(lines[i], lskip)) cond = "nz";
        else if (parse_jp_z_label (lines[i], lskip)) cond = "z";
        else if (parse_jp_c_label (lines[i], lskip)) cond = "c";
        else if (parse_jp_nc_label(lines[i], lskip)) cond = "nc";

        if (!cond)
            continue;
        if (starts_label(lines[i + 1]))
            continue;
        if (strncmp(lines[i + 1], "jp ", 3) == 0)
            continue;
        if (!line_is_label_name(i + 2, lskip))
            continue;
        if (!is_uncond_jp(lines[i + 3]))
            continue;
        if (!peep_parse_jp_uncond_label(lines[i + 3], ldest))
            continue;
        if (strcmp(lskip, ldest) == 0)
            continue;

        peep_make_cond_jump(new_jp, sizeof(new_jp), cond, ldest);
        replace1(i, new_jp);
        changed = 1;
        if (i > 0) i--;
    }

    return changed;
}

static void read_file(const char *name)
{
    FILE *f;
    char buf[MAX_LINE];

    f = fopen(name, "r");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", name);
        exit(1);
    }

    while (fgets(buf, sizeof(buf), f)) {
        trim(buf);
        if (nlines >= MAX_LINES) {
            fprintf(stderr, "too many lines\n");
            exit(1);
        }
        lines[nlines++] = xstrdup2(buf);
    }

    fclose(f);
}

static void write_file(const char *name)
{
    FILE *f;
    int i;

    f = fopen(name, "w");
    if (!f) {
        fprintf(stderr, "cannot create %s\n", name);
        exit(1);
    }

    for (i = 0; i < nlines; i++) {
        if (lines[i][0] == 0)
            fprintf(f, "\n");
        else if (starts_label(lines[i]) || lines[i][0] == ';')
            fprintf(f, "%s\n", lines[i]);
        else
            fprintf(f, "\t%s\n", lines[i]);
    }

    fclose(f);
}

/*
 * Detect a sequential byte-store loop that initialises a global array to a
 * constant value, and replace it with LDIR (block move).  The pattern is:
 *
 *   Lhead:
 *     ld l,(ix-A)  ld h,(ix-A-1)   ; index variable
 *     ld de,SIZE
 *     or a
 *     sbc hl,de
 *     jp z, Lbody
 *     jp c, Lbody
 *     jp Lexit
 *   Lbody:
 *     ld l,(ix-A)  ld h,(ix-A-1)
 *     ld de,SYM                     ; array base (global symbol)
 *     add hl,de
 *     ld (hl),CONST                 ; small non-negative constant
 *   [Linc:]
 *     inc (ix-A)
 *     jp nz, Lnext
 *     inc (ix-A-1)
 *   Lnext:
 *     jp Lhead
 *   Lexit:
 *
 * Replaced with (only valid when the index starts from 0):
 *
 *     ld hl,SYM
 *     ld (hl),CONST
 *     ld de,SYM+1
 *     ld bc,SIZE
 *     ldir
 *
 * To verify the index starts at 0 we look for the pattern
 * "ld hl,0 / ld (ix-A),l / ld (ix-A-1),h" immediately before Lhead.
 */
static int stride_parse_ld_r_ix_neg(const char *s, char r, int *n); /* forward */

/* Rotated-loop counterpart to pass_ldir_memset: recognises the identical
 * "loop stores a constant into every array element" idiom, but in the
 * bottom-tested shape ast_gen_for_stmt emits when it can prove the loop's
 * first iteration is certain (a plain `var = CONST; var OP CONST2; var++`
 * header where the entry test is statically known true) - body first, then
 * the increment, then the bound compare branching back to the body. This is
 * a cyclic rotation of the exact same instructions pass_ldir_memset matches
 * (store, increment, compare, in a different order around the back-edge),
 * not a new idiom, so it produces the identical LDIR replacement. */
static int pass_ldir_memset_rotated(void)
{
    int i;
    int changed = 0;

    for (i = 0; i + 20 < nlines; i++) {
        char lbody[128], tmp[128];
        int lo_ix, hi_ix;
        long size_val;
        char arr_sym[128];
        char const_str[32];
        int j, ip;

        /* 1. Lbody label */
        if (!label_name_at(i, lbody))
            continue;
        j = i + 1;

        /* 2. Body: reload index, compute address, store constant */
        if (!stride_parse_ld_r_ix_neg(lines[j], 'l', &lo_ix)) continue; j++;
        if (!stride_parse_ld_r_ix_neg(lines[j], 'h', &hi_ix)) continue; j++;
        if (hi_ix != lo_ix - 1) continue;
        if (!parse_ld_de_imm(lines[j], arr_sym) || arr_sym[0] != '_') continue; j++;
        if (!eq(j, "add hl,de")) continue; j++;
        if (strncmp(lines[j], "ld (hl),", 8) != 0) continue;
        {
            const char *p = lines[j] + 8;
            int v;
            if (!parse_nonneg_int(p, &v) || v > 255) continue;
            sprintf(const_str, "%d", v);
        }
        j++;

        /* 3. Optional Linc label */
        if (starts_label(lines[j]))
            j++;

        /* 4. Increment: inc (ix-lo); jp nz,Ltest; inc (ix-hi); Ltest: */
        {
            char stored_lo[32];
            sprintf(stored_lo, "inc (ix-%d)", lo_ix);
            if (!eq(j, stored_lo)) continue; j++;
        }
        if (!parse_jp_nz_label(lines[j], tmp)) continue; j++;
        {
            char stored_hi[32];
            sprintf(stored_hi, "inc (ix-%d)", hi_ix);
            if (!eq(j, stored_hi)) continue; j++;
        }
        if (!line_is_label_name(j, tmp)) continue; j++;

        /* 5. Comparison block: reload index, compare bound, branch back to Lbody */
        {
            int lo2, hi2;
            if (!stride_parse_ld_r_ix_neg(lines[j], 'l', &lo2)) continue; j++;
            if (!stride_parse_ld_r_ix_neg(lines[j], 'h', &hi2)) continue; j++;
            if (lo2 != lo_ix || hi2 != hi_ix) continue;
        }
        if (!parse_ld_de_positive_imm(lines[j], &size_val)) continue; j++;
        if (eq(j, "ld a,h") && eq(j+1, "xor 80h") && eq(j+2, "ld h,a") &&
            eq(j+3, "ld a,d") && eq(j+4, "xor 80h") && eq(j+5, "ld d,a"))
            j += 6;
        if (!eq(j, "or a")) continue; j++;
        if (!eq(j, "sbc hl,de")) continue; j++;
        if (!parse_jp_z_label(lines[j], tmp) || strcmp(tmp, lbody) != 0) continue; j++;
        if (!parse_jp_c_label(lines[j], tmp) || strcmp(tmp, lbody) != 0) continue;
        ip = j;

        /* 6. Verify the index was initialised to 0 immediately before Lbody.
         *    Look for:  ld hl,0 / ld (ix-lo),l / ld (ix-hi),h */
        {
            char lo_store[32], hi_store[32];
            int found = 0;
            int k;

            sprintf(lo_store, "ld (ix-%d),l", lo_ix);
            sprintf(hi_store, "ld (ix-%d),h", hi_ix);

            for (k = i - 1; k >= 0 && k >= i - 6; k--) {
                if (eq(k, "ld hl,0") &&
                    k + 1 < i && eq(k + 1, lo_store) &&
                    k + 2 < i && eq(k + 2, hi_store)) {
                    found = 1;
                    break;
                }
            }
            if (!found) continue;
        }

        /* The matched loop body never touches B/C (it's HL/DE/IX only), so
         * nothing above needed to check that - but the LDIR replacement
         * below claims BC fresh as a byte count, and dcc's own reg_alloc
         * may already have a whole-function or earlier-loop candidate live
         * in BC right through this exact point, invisible to a match that
         * never had any reason to look at B/C. See
         * bc_regalloc_claimed_before's own comment; same collision class
         * pass_cache_global_word_reload was fixed for. */
        if (bc_regalloc_claimed_before(i))
            continue;

        /* All checks passed.  Replace the rotated loop with LDIR. */
        {
            char ld_hl_sym[MAX_LINE], ld_const[MAX_LINE], ld_de_sym1[MAX_LINE], ld_bc[MAX_LINE];

            sprintf(ld_hl_sym,  "ld hl,%s",     arr_sym);
            sprintf(ld_const,   "ld (hl),%s",   const_str);
            sprintf(ld_de_sym1, "ld de,%s+1",   arr_sym);
            sprintf(ld_bc,      "ld bc,%ld",     size_val);

            delete_n(i, ip - i + 1);

            insert_line_tagged(i + 0, ld_hl_sym, "ldir_memset");
            insert_line(i + 1, ld_const);
            insert_line(i + 2, ld_de_sym1);
            insert_line(i + 3, ld_bc);
            insert_line(i + 4, "ldir");

            changed = 1;
        }
    }

    return changed;
}

/*
 * Parse "ld R,(ix-N)" extracting N (positive int). R is a single register
 * character ('l','h','e','d'). Returns 1 on success.
 */
static int stride_parse_ld_r_ix_neg(const char *s, char r, int *n)
{
    char prefix[16];
    const char *p;
    int v;

    sprintf(prefix, "ld %c,(ix-", r);
    if (strncmp(s, prefix, strlen(prefix)) != 0)
        return 0;
    p = s + strlen(prefix);
    if (*p < '0' || *p > '9')
        return 0;
    v = 0;
    while (*p >= '0' && *p <= '9')
        v = v * 10 + (*p++ - '0');
    if (*p != ')' || p[1] != 0 || v <= 0)
        return 0;
    *n = v;
    return 1;
}

/* Parse "ld (ix-N),R" extracting N. Returns 1 on success. */
static int stride_parse_ld_ix_neg_r(const char *s, char r, int *n)
{
    char suffix[8];
    const char *p;
    int v;

    if (strncmp(s, "ld (ix-", 7) != 0)
        return 0;
    p = s + 7;
    if (*p < '0' || *p > '9')
        return 0;
    v = 0;
    while (*p >= '0' && *p <= '9')
        v = v * 10 + (*p++ - '0');
    sprintf(suffix, "),%c", r);
    if (strcmp(p, suffix) != 0 || v <= 0)
        return 0;
    *n = v;
    return 1;
}

/*
 * Convert a stride-indexed inner loop into a pointer-walk loop.
 *
 * Detects the pattern:
 *
 *   LH:
 *     ld l,(ix-A)  ld h,(ix-A-1)    ; index variable (lo/hi)
 *     ld de,CONST                    ; upper bound
 *     or a
 *     sbc hl,de
 *     jp z, LB
 *     jp c, LB                       ; ≤ CONST → body
 *     jp LE                          ; > CONST → exit
 *   LB:
 *     ld l,(ix-A)  ld h,(ix-A-1)    ; reload index
 *     ld de,SYM                      ; array base (global symbol)
 *     add hl,de
 *     ld (hl),0                      ; array[k] = 0
 *   [LI:]
 *     ld l,(ix-A)  ld h,(ix-A-1)    ; reload index
 *     ld e,(ix-B)  ld d,(ix-B-1)    ; stride
 *     add hl,de
 *     ld (ix-A),l  ld (ix-A-1),h    ; store updated index
 *     jp LH
 *   LE:
 *
 * Replaces it with a pointer-walk that keeps ptr in HL, stride in DE,
 * and end address in BC — eliminating the three IX-relative reloads
 * per iteration:
 *
 *     ld e,(ix-B)  ld d,(ix-B-1)    ; DE = stride
 *     ld l,(ix-A)  ld h,(ix-A-1)    ; HL = initial index
 *     ld bc,SYM                      ; BC = array base
 *     add hl,bc                      ; HL = initial ptr
 *     ld bc,SYM+CONST+1              ; BC = end address
 *     push hl / or a / sbc hl,bc / pop hl
 *     jp nc,LE                       ; skip loop if ptr >= end
 *   LH:
 *     ld (hl),0
 *     add hl,de
 *     push hl / or a / sbc hl,bc / pop hl
 *     jp c,LH
 *   LE:
 */
static int pass_stride_loop_to_ptr(void)
{
    int i;
    int changed = 0;

    for (i = 0; i + 32 < nlines; i++) {
        char lh[128], lb[128], le[128], tmp[128];
        int lo_k, hi_k, lo_s, hi_s;
        long cmp_val;
        char arr_sym[128];
        int j, ip;

        /* 1. LH label */
        if (!label_name_at(i, lh))
            continue;
        j = i + 1;

        /* 2. Comparison block */
        if (!stride_parse_ld_r_ix_neg(lines[j], 'l', &lo_k)) continue; j++;
        if (!stride_parse_ld_r_ix_neg(lines[j], 'h', &hi_k)) continue; j++;
        if (hi_k != lo_k - 1) continue;
        if (!parse_ld_de_positive_imm(lines[j], &cmp_val)) continue; j++;
        /* Accept both unsigned (or a/sbc) and signed-biased (xor 80h/or a/sbc)
         * comparisons. The generated pointer walk uses unsigned pointer arithmetic,
         * which is semantically correct for non-negative array indices — the only
         * valid use case for this pattern (negative index would be UB in C). */
        if (eq(j, "ld a,h") && eq(j+1, "xor 80h") && eq(j+2, "ld h,a") &&
            eq(j+3, "ld a,d") && eq(j+4, "xor 80h") && eq(j+5, "ld d,a"))
            j += 6;
        if (!eq(j, "or a")) continue; j++;
        if (!eq(j, "sbc hl,de")) continue; j++;
        if (!parse_jp_z_label(lines[j], lb)) continue; j++;
        if (!parse_jp_c_label(lines[j], tmp) || strcmp(tmp, lb) != 0) continue; j++;
        if (!peep_parse_jp_uncond_label(lines[j], le)) continue; j++;

        /* 3. LB label */
        if (!line_is_label_name(j, lb)) continue; j++;

        /* 4. Body: reload index, compute address, store 0 */
        {
            int lo2, hi2;
            if (!stride_parse_ld_r_ix_neg(lines[j], 'l', &lo2)) continue; j++;
            if (!stride_parse_ld_r_ix_neg(lines[j], 'h', &hi2)) continue; j++;
            if (lo2 != lo_k || hi2 != hi_k) continue;
        }
        if (!parse_ld_de_imm(lines[j], arr_sym) || arr_sym[0] != '_') continue; j++;
        if (!eq(j, "add hl,de")) continue; j++;
        if (!eq(j, "ld (hl),0")) continue; j++;

        /* 5. Optional LI label (fall-through increment label) */
        if (starts_label(lines[j]))
            j++;

        /* 6. Increment block: reload index, load stride, update index */
        {
            int lo3, hi3;
            if (!stride_parse_ld_r_ix_neg(lines[j], 'l', &lo3)) continue; j++;
            if (!stride_parse_ld_r_ix_neg(lines[j], 'h', &hi3)) continue; j++;
            if (lo3 != lo_k || hi3 != hi_k) continue;
        }
        if (!stride_parse_ld_r_ix_neg(lines[j], 'e', &lo_s)) continue; j++;
        if (!stride_parse_ld_r_ix_neg(lines[j], 'd', &hi_s)) continue; j++;
        if (hi_s != lo_s - 1) continue;
        if (!eq(j, "add hl,de")) continue; j++;
        {
            int lo4;
            if (!stride_parse_ld_ix_neg_r(lines[j], 'l', &lo4) || lo4 != lo_k) continue;
        }
        j++;
        {
            int hi4;
            if (!stride_parse_ld_ix_neg_r(lines[j], 'h', &hi4) || hi4 != hi_k) continue;
        }
        j++;

        /* 7. jp back to LH */
        if (!peep_parse_jp_uncond_label(lines[j], tmp) || strcmp(tmp, lh) != 0) continue;
        ip = j;
        j++;

        /* 8. LE label immediately follows */
        if (!line_is_label_name(j, le)) continue;

        /* The matched loop body never touches B/C (HL/DE/IX only), so
         * nothing above needed to check that - but the replacement below
         * keeps BC live as the end-address for the loop's entire new
         * duration, and dcc's own reg_alloc may already have a
         * whole-function or earlier-loop candidate live in BC right
         * through this exact point. See bc_regalloc_claimed_before's own
         * comment; same collision class pass_cache_global_word_reload was
         * fixed for. */
        if (bc_regalloc_claimed_before(i))
            continue;

        /* Pattern matched. Delete old block and insert pointer-walk version.
         *
         * BC = SYM+SIZE+1 (16-bit relocatable — fine with M80).
         * B = endhi = high byte of end address.
         * C = endlo = low byte of end address.
         *
         * "ld a,h; cp b" computes H - endhi (set carry if H < endhi).
         * "ld a,l; cp c" computes L - endlo (set carry if L < endlo).
         * Neither ld(hl),0 nor add hl,de modifies A, B, or C.
         *
         * Common case (H < endhi): 39 T-states per iteration.
         * Previous push/sbc/pop approach:  71 T-states.
         *
         * Loop structure:
         *   LH:
         *     ld (hl),0
         *     add hl,de          ptr += stride
         *     ld a,h
         *     cp b               H - endhi: carry → H < endhi
         *     jp c,LH            H < endhi → continue (39T)
         *     jp nz,LE           H > endhi → exit
         *     ld a,l             H = endhi: check low byte
         *     cp c               L - endlo: carry → L < endlo
         *     jp c,LH            L < endlo → continue
         *                        fall through: L >= endlo → exit
         */
        {
            char l0[MAX_LINE], l1[MAX_LINE], l2[MAX_LINE], l3[MAX_LINE], l4[MAX_LINE], l6[MAX_LINE];
            char lh_label[MAX_LINE];
            char jp_c_lh[MAX_LINE], jp_nz_le[MAX_LINE], jp_nc_le[MAX_LINE];

            sprintf(l0,       "ld e,(ix-%d)", lo_s);
            sprintf(l1,       "ld d,(ix-%d)", hi_s);
            sprintf(l2,       "ld l,(ix-%d)", lo_k);
            sprintf(l3,       "ld h,(ix-%d)", hi_k);
            sprintf(l4,       "ld bc,%s", arr_sym);
            sprintf(l6,       "ld bc,%s+%ld", arr_sym, cmp_val + 1);
            sprintf(lh_label, "%s:", lh);
            sprintf(jp_c_lh,  "jp c,%s", lh);
            sprintf(jp_nz_le, "jp nz,%s", le);
            sprintf(jp_nc_le, "jp nc,%s", le);

            delete_n(i, ip - i + 1);

            /* Setup: stride in DE, initial ptr in HL, end addr in BC */
            insert_line_tagged(i +  0, l0, "stride_loop"); /* ld e,(ix-B) */
            insert_line(i +  1, l1);           /* ld d,(ix-B-1)      */
            insert_line(i +  2, l2);           /* ld l,(ix-A)        */
            insert_line(i +  3, l3);           /* ld h,(ix-A-1)      */
            insert_line(i +  4, l4);           /* ld bc,SYM          */
            insert_line(i +  5, "add hl,bc");  /* HL = SYM+k = ptr   */
            insert_line(i +  6, l6);           /* ld bc,SYM+SIZE+1   */
            /* One-shot pre-check: skip loop if initial ptr >= end */
            insert_line(i +  7, "push hl");
            insert_line(i +  8, "or a");
            insert_line(i +  9, "sbc hl,bc");
            insert_line(i + 10, "pop hl");
            insert_line(i + 11, jp_nc_le);     /* jp nc,LE           */
            /* Hot loop body */
            insert_line(i + 12, lh_label);     /* LH:                */
            insert_line(i + 13, "ld (hl),0");
            insert_line(i + 14, "add hl,de");
            insert_line(i + 15, "ld a,h");     /* A = ptr.hi         */
            insert_line(i + 16, "cp b");       /* H - endhi          */
            insert_line(i + 17, jp_c_lh);      /* jp c → H < endhi   */
            insert_line(i + 18, jp_nz_le);     /* jp nz → H > endhi  */
            insert_line(i + 19, "ld a,l");     /* H = endhi: check L */
            insert_line(i + 20, "cp c");       /* L - endlo          */
            insert_line(i + 21, jp_c_lh);      /* jp c → L < endlo   */
            /* fall through: L >= endlo → exit to LE                 */

            changed = 1;
        }
    }

    return changed;
}



/*
 * pass_reuse_sbc_result_for_flagcheck_rotated:
 *
 * Rotated-loop counterpart to pass_reuse_sbc_result_for_flagcheck above. In a
 * rotated loop the bound compare sits at the BOTTOM, branching back to the
 * body at the TOP - so unlike the non-rotated case, the compare does not
 * unconditionally precede every use of the body's address computation: the
 * very first iteration reaches the body by falling out of the loop's init,
 * never through the compare. Reusing the compare's leftover HL therefore
 * needs an extra one-time "prime" load right after the init, computing the
 * same HL value the compare would have produced, so every entry into the
 * body - first iteration included - sees consistent state. Restricted to a
 * zero literal loop index (the same restriction pass_ldir_memset_rotated
 * relies on), so the prime is always the constant -(N+1).
 *
 * Matches (compare block, found first, scanning backward from there):
 *   Lbody:
 *     ld l,(ix-K) / ld h,(ix-K-1)
 *     ld de,SYM (global symbol)
 *     add hl,de
 *     ld a,(hl)
 *     or a
 *     jp z/nz,LDEST
 *     ...
 *   ld l,(ix-K) / ld h,(ix-K-1)
 *   ld de,N
 *   ld a,h / xor 80h / ld h,a / ld a,d / xor 80h / ld d,a   ; signed bias
 *   or a
 *   sbc hl,de
 *   jp z,Lbody
 *   jp c,Lbody
 *
 * immediately preceded (within the loop init, found by backward scan from
 * Lbody) by:
 *   ld hl,0
 *   ld (ix-K),l
 *   ld (ix-K-1),h
 *
 * Rewrites all three regions: the compare drops its signed bias and jp z
 * branch (unsigned N+1 folds both into a single "sbc hl,de; jp c,Lbody"),
 * the body's index reload + array-base add becomes a single "ld de,SYM+N+1;
 * add hl,de" reusing HL, and the init gains one extra "ld hl,-(N+1)" line so
 * the first entry into the body sees the same HL the compare would have left.
 */
static int pass_reuse_sbc_result_for_flagcheck_rotated(void)
{
    int i;
    int changed = 0;

    for (i = 0; i + 7 <= nlines; i++) {
        int K, M, lo2, hi2;
        long N;
        char lbody[128], tmp[128], arr_sym[128], dest[128];
        int body_idx, init_idx, j, k, cmp_end;

        /* Compare block: reload index, compare, branch back. The signed
         * bias (xor 80h on both halves) is present when this pass runs
         * before pass_elim_loop_back_signed_bias has stripped it, and absent
         * when that pass has already run first in this same convergence
         * pass - accept both, mirroring pass_ldir_memset's own optional
         * bias check. Either way both "jp z,Lbody" and "jp c,Lbody" remain,
         * since that other pass only strips the bias lines, not the
         * branches. */
        if (!stride_parse_ld_r_ix_neg(lines[i + 0], 'l', &K)) continue;
        if (!stride_parse_ld_r_ix_neg(lines[i + 1], 'h', &M)) continue;
        if (M != K - 1) continue;
        j = i + 2;
        if (!parse_ld_de_positive_imm(lines[j], &N)) continue; j++;
        if (eq(j, "ld a,h") && eq(j+1, "xor 80h") && eq(j+2, "ld h,a") &&
            eq(j+3, "ld a,d") && eq(j+4, "xor 80h") && eq(j+5, "ld d,a"))
            j += 6;
        if (!eq(j, "or a")) continue; j++;
        if (!eq(j, "sbc hl,de")) continue; j++;
        if (!parse_jp_z_label(lines[j], lbody)) continue; j++;
        if (!parse_jp_c_label(lines[j], tmp) || strcmp(tmp, lbody) != 0) continue;
        cmp_end = j;

        /* Lbody must be reached only from the two branches just matched, plus
         * fall-through from the loop init - nothing else may enter it with a
         * different (or absent) HL value. */
        if (count_jumps_to_label(lbody) != 2) continue;

        /* Find Lbody's line index. */
        body_idx = -1;
        for (k = 0; k < nlines; k++) {
            if (label_name_at(k, tmp) && strcmp(tmp, lbody) == 0) {
                body_idx = k;
                break;
            }
        }
        if (body_idx < 0 || body_idx >= i)
            continue;

        /* Lbody's prefix: reload the same index, load the array base, add,
         * read the byte, test it, branch on the flag. */
        j = body_idx + 1;
        if (!stride_parse_ld_r_ix_neg(lines[j], 'l', &lo2) || lo2 != K) continue; j++;
        if (!stride_parse_ld_r_ix_neg(lines[j], 'h', &hi2) || hi2 != M) continue; j++;
        if (!parse_ld_de_imm(lines[j], arr_sym) || arr_sym[0] != '_') continue; j++;
        if (!eq(j, "add hl,de")) continue; j++;
        if (!eq(j, "ld a,(hl)")) continue; j++;
        if (!eq(j, "or a")) continue; j++;
        /* The flag test itself (jp z/nz,dest) is left untouched by the
         * rewrite below - only confirm it's there so we're not misreading
         * some other shape as this idiom. */
        if (!parse_jp_z_label(lines[j], dest) && !parse_jp_nz_label(lines[j], dest))
            continue;

        /* Loop init immediately preceding Lbody: ld hl,0 / ld (ix-K),l / ld (ix-M),h */
        {
            char lo_store[32], hi_store[32];
            int found = 0;
            sprintf(lo_store, "ld (ix-%d),l", K);
            sprintf(hi_store, "ld (ix-%d),h", M);
            init_idx = -1;
            for (k = body_idx - 1; k >= 0 && k >= body_idx - 6; k--) {
                if (eq(k, "ld hl,0") &&
                    k + 1 < body_idx && eq(k + 1, lo_store) &&
                    k + 2 < body_idx && eq(k + 2, hi_store)) {
                    found = 1;
                    init_idx = k;
                    break;
                }
            }
            if (!found) continue;
        }

        /* All checks passed.  Rewrite compare, body prefix, and init - in
         * descending line-index order so each edit's position stays valid
         * for the edits still to come. */
        {
            long np1 = N + 1;
            char l_de_np1[MAX_LINE], jp_c_lbody[MAX_LINE];
            char l_de_sym_np1[MAX_LINE], l_prime[MAX_LINE];

            /* 1. Compare block (highest index): drop the signed bias (if
             *    still present) and the "jp z" branch; unsigned N+1 needs
             *    only "sbc hl,de; jp c". */
            sprintf(l_de_np1, "ld de,%ld", np1);
            sprintf(jp_c_lbody, "jp c,%s", lbody);
            delete_n(i + 2, cmp_end - (i + 2) + 1); /* de,N .. jp c,lbody, inclusive */
            insert_line(i + 2, l_de_np1);
            insert_line(i + 3, "or a");
            insert_line(i + 4, "sbc hl,de");
            insert_line_tagged(i + 5, jp_c_lbody, "reuse_sbc_rotated");

            /* 2. Lbody prefix: replace the index reload + array-base add
             *    with a single de-load against SYM+N+1 that reuses HL. */
            sprintf(l_de_sym_np1, "ld de,%s+%ld", arr_sym, np1);
            delete_n(body_idx + 1, 4); /* the two reloads + ld de,SYM + add hl,de */
            insert_line(body_idx + 1, l_de_sym_np1);
            insert_line(body_idx + 2, "add hl,de");

            /* 3. Init: prime HL to -(N+1) so the first entry into Lbody sees
             *    the same HL the compare would have left behind. */
            sprintf(l_prime, "ld hl,%ld", (-np1) & 0xffffL);
            insert_line(init_idx + 3, l_prime);

            changed = 1;
        }
    }

    return changed;
}




static int peep_parse_ld_hl_paren_sym(const char *s, char *sym)
{
    char tmp[MAX_LINE];
    const char *p;
    int i;

    strip_peep_comment_copy(tmp, s);
    if (strncmp(tmp, "ld hl,(", 7) != 0)
        return 0;
    p = tmp + 7;
    i = 0;
    while (*p && *p != ')' && i < 120)
        sym[i++] = *p++;
    sym[i] = 0;
    return i > 0 && *p == ')' && p[1] == 0;
}

static int peep_parse_ld_paren_sym_hl(const char *s, char *sym)
{
    char tmp[MAX_LINE];
    const char *p;
    int i;

    strip_peep_comment_copy(tmp, s);
    if (strncmp(tmp, "ld (", 4) != 0)
        return 0;
    p = tmp + 4;
    i = 0;
    while (*p && *p != ')' && i < 120)
        sym[i++] = *p++;
    sym[i] = 0;
    return i > 0 && *p == ')' && p[1] == ',' &&
           p[2] == 'h' && p[3] == 'l' && p[4] == 0;
}


/* Whole-file store count for a word-sized (int/pointer) global - used to
 * prove a repeated "ld hl,(NAME)" reload's value can't have changed between
 * two occurrences in the same hazard-free segment (see
 * pass_cache_global_word_reload below): if NAME is stored to (via
 * "ld (NAME),hl", the only word-store shape this codegen emits) at most
 * once in the WHOLE file, nothing anywhere - including whatever a call in a
 * *different* segment might do - can ever reassign it again. This is the
 * same "write once, then read-only" assumption dcc_global_scan.c's own
 * whole-file write-once proof already relies on for its (much narrower)
 * global-hoist fast path, just re-derived here textually since dccpeep has
 * no access to that C-source-level analysis. */
/* True if `line` is dcc's own reg_alloc priming load for a loop-scoped or
 * whole-function BC candidate (dcc_loop_regalloc.c/dcc_func.c emit this
 * exact pair, with a leading tab in dcc's own output, right before the
 * candidate's live range begins: "\tld c,(ix%+d)\n" / "\tld b,(ix%+d)\n") -
 * confirmed by grep to be the ONLY place dcc's own codegen ever emits "ld
 * c,(ix" or "ld b,(ix" at all, so this text signature is unambiguous. The
 * comparison below has no leading tab because read_file's own trim() has
 * already stripped it from every line in lines[] by the time any pass sees
 * it - matching every other bare-mnemonic string this file compares
 * against (e.g. line_clobbers_bc's "rst"/"djnz" checks). Used by pass_
 * cache_global_word_reload (below) to recognize that BC is already spoken
 * for by dcc's own codegen from this point in the function onward - see
 * that pass's own use of this for why a purely per-segment view (line_
 * clobbers_bc) isn't enough here. */
static int line_is_regalloc_bc_priming(const char *line)
{
    char clean[MAX_LINE];

    strip_peep_comment_copy(clean, line);
    return strncmp(clean, "ld c,(ix", 8) == 0 || strncmp(clean, "ld b,(ix", 8) == 0;
}

/* Shared by every dccpeep pass that wants to write its own value into B, C,
 * or the BC pair (a loop counter, a cached pointer, a packed call argument,
 * ...): true if dcc's own reg_alloc priming line for a loop-scoped or
 * whole-function BC candidate appears anywhere between the start of the
 * function containing line `at` and `at` itself. Deliberately conservative,
 * matching pass_cache_global_word_reload's own reg_alloc_seen tracking
 * (this is in fact the same check, generalized to a single callable
 * primitive instead of that pass's own local forward-scan state - see this
 * function's own commit history for why the two were unified): once a
 * priming line has appeared anywhere earlier in the function, BC is treated
 * as spoken for through the rest of that function, not just until some
 * text-detected spill point - a real reg_alloc candidate's spill, if it has
 * one, would genuinely free BC back up before the function ends, but
 * reliably proving that from text alone isn't worth the complexity for what
 * stays a missed optimization either way, never a correctness risk.
 *
 * A caller with a candidate insertion point that isn't itself the very
 * start of a loop/segment (e.g. a whole-function pass like the _MinMax
 * family below) should pass the END of its own scan range instead of a
 * single point, so the backward scan still covers every line the pass is
 * about to touch, not just a prefix of it. */
static int bc_regalloc_claimed_before(int at)
{
    int i;

    for (i = at - 1; i >= 0; i--) {
        if (line_starts_function_marker(lines[i]))
            return 0;
        if (line_is_regalloc_bc_priming(lines[i]))
            return 1;
    }
    return 0;
}

static int global_write_count_in_file(const char *name)
{
    int i, n;
    char sym[128];

    n = 0;
    for (i = 0; i < nlines; i++) {
        if (peep_parse_ld_paren_sym_hl(lines[i], sym) && !strcmp(sym, name))
            n++;
    }
    return n;
}

/* Is `name` stored to (via "ld (name),hl") anywhere in [start,end)? Even a
 * symbol with at most one store in the WHOLE file (see
 * global_write_count_in_file) is not a safe cache candidate if that one
 * store falls INSIDE the very segment being cached - e.g.
 * tests/tforblk.c's static_shadows_auto: `x++; inner = x;` on a
 * function-static compiles to read/inc/store/read, and the store sits
 * between the two reads. global_write_count_in_file alone doesn't catch
 * this (it only proves nothing OUTSIDE this segment can have reassigned
 * the symbol) - this closes the gap by refusing any segment that contains
 * the write itself, mirroring dcc_global_scan.c's own
 * global_text_written_in_function check for its narrower C-source-level
 * version of the same proof. */
static int symbol_written_in_range(const char *name, int start, int end)
{
    int i;
    char sym[128];

    for (i = start; i < end; i++) {
        if (peep_parse_ld_paren_sym_hl(lines[i], sym) && !strcmp(sym, name))
            return 1;
    }
    return 0;
}

/*
 * pass_cache_global_word_reload:
 *
 * "ld hl,(NAME)" for a word-sized global reloads it from memory at every
 * reference, even when the same global is read more than once in a short
 * span with no intervening call/push/pop (e.g. tests/cobint.c's central
 * interpreter-state pointer G, referenced 365 times across the file for a
 * value written exactly once at startup - exec_stmt_tokens's own loop
 * condition alone rereads it three times back to back).
 *
 * Unlike pass_cache_noix_byte_param_reload, this doesn't require the whole
 * function to be call-free - it partitions each function into hazard-free
 * segments (split at whatever line_clobbers_bc flags: a call, djnz/block-
 * repeat, or an explicit B/C mention) and caches independently within each
 * segment. Deliberately does NOT split on push/pop the way the no-IX-frame
 * pass does - those don't clobber BC and this pass isn't caching an SP-
 * relative address, so they're not a hazard here. That's what makes it
 * apply far more broadly than the no-IX-frame case: most ordinary
 * functions call other functions somewhere, but a short run of field
 * accesses (a loop condition, a handful of comparisons) with no call in
 * between is common. Requires the symbol be stored to at most once in the
 * WHOLE file (see global_write_count_in_file), not just within the
 * segment, since a call inside a *different* segment could otherwise have
 * reassigned it in between.
 */
static int pass_cache_global_word_reload(void)
{
    int i;
    int changed = 0;
    int segstart;

    segstart = 0;
    for (i = 0; i <= nlines; i++) {
        int j, k;
        char sym[128], best_sym[128];
        int best_count;
        struct { char name[128]; int count; } seen[32];
        int nseen;
        int occ[64];
        int noc;
        int delta;

        /* dcc's own reg_alloc mechanism (dcc_loop_regalloc.c/dcc_func.c)
         * keeps a loop-scoped or whole-function candidate resident in BC
         * across a span this pass cannot see in the surrounding text at
         * all: the priming load ("ld c,(ix+d)"/"ld b,(ix+d+1)") and the
         * candidate's own later reads/writes are the ONLY textual b/c
         * mentions, with everything genuinely hazard-free (by line_
         * clobbers_bc's own definition) in between - exactly the shape
         * this pass looks for to justify caching something else there.
         * Once BC's real owner is dcc's own reg_alloc, this pass storing a
         * completely unrelated global into it there silently overwrites
         * the live candidate - confirmed as a real miscompile: forint.c's
         * eval_e, where this pass cached g_syms's address in BC across a
         * span that, unknown to it, already held a live write candidate,
         * corrupting an array index computed several calls later. Guarded
         * below via the shared bc_regalloc_claimed_before (see its own
         * comment for the conservative "seen once, spoken for through the
         * rest of the function" rule this pass originally established and
         * every other BC-writing pass in this file now shares). */

        /* A segment must never cross a label or a function boundary in
         * addition to never crossing a BC-clobbering line: a label can be
         * a jump target reached from some other point in the function (or,
         * for a function marker, from an entirely unrelated call site)
         * where BC does not hold whatever this segment cached - there is
         * no reaching-definitions analysis here to prove otherwise, so
         * treat every label as an unconditional hazard, exactly like a
         * call. This is what an earlier version of this pass got wrong: it
         * only checked line_clobbers_bc, so a segment could span from one
         * function's tail straight into the next function's prologue
         * whenever nothing in between happened to look like a call/djnz/
         * register mention - confirmed as a real miscompile (cobint fails
         * with "too many statements" instead of computing correctly)
         * before this fix, not just a theoretical concern. */
        if (i < nlines && !line_clobbers_bc(lines[i]) &&
            !starts_label(lines[i]) && !line_starts_function_marker(lines[i]))
            continue;

        /* [segstart, i) is one hazard-free segment (i itself is the
         * hazard, or end of file). Find the best repeated global-word
         * reload within it. */
        nseen = 0;
        for (j = segstart; j < i; j++) {
            if (!peep_parse_ld_hl_paren_sym(lines[j], sym))
                continue;
            for (k = 0; k < nseen; k++)
                if (!strcmp(seen[k].name, sym)) break;
            if (k == nseen) {
                if (nseen < 32) { strcpy(seen[nseen].name, sym); seen[nseen].count = 1; nseen++; }
            } else {
                seen[k].count++;
            }
        }

        best_count = 0;
        best_sym[0] = 0;
        for (k = 0; k < nseen; k++) {
            if (seen[k].count > best_count) {
                best_count = seen[k].count;
                strcpy(best_sym, seen[k].name);
            }
        }

        /* If the hazard ending this segment is itself an EXISTING
         * "global_word_cache_load" (from a symbol this pass already cached
         * in a completely separate, earlier-processed segment - possibly
         * from a prior call to this same pass, on an earlier dccpeep
         * fixed-point iteration), that load's correctness depends on BC
         * being untouched all the way back to that OTHER symbol's cache
         * store, which lives further back than segstart, outside this
         * segment's own view. line_clobbers_bc correctly stops THIS
         * segment right before that load (so we never overwrite the load
         * instruction itself), but a NEW cache store anywhere in
         * [segstart, i) would still sit between that earlier store and
         * this pending load, clobbering BC before the load reads it.
         * Confirmed as a real miscompile: tests/tptrlhs.c cached gpwrap in
         * exactly such a segment, silently corrupting an unrelated
         * gpleaf cache read sitting immediately after it. Refuse to start
         * a brand new cache anywhere in a segment bounded by someone
         * else's still-pending load - safe to leave as plain reloads
         * either way, just forgoes a second, smaller-payoff cache in the
         * same neighborhood. */
        if (i < nlines && strstr(lines[i], "global_word_cache_load"))
            best_count = 0;

        /* >= 3, not >= 2: caching costs a fixed 8 T-states (ld c,l/ld b,h),
         * and each avoided reload saves exactly 8 T-states (ld hl,(nn) is
         * 16 T-states; the ld l,c/ld h,b replacement is 8) - so 2
         * occurrences is a wash, not a win, and rewriting the pattern also
         * risks defeating some other, more specific existing pass that
         * would otherwise have matched "ld hl,(NAME)" in its original form
         * (confirmed as a real, measured regression on several small,
         * otherwise-unrelated tests before this threshold was raised). */
        if (best_count >= 3 && global_write_count_in_file(best_sym) <= 1 &&
            !symbol_written_in_range(best_sym, segstart, i) &&
            !bc_regalloc_claimed_before(i)) {
            noc = 0;
            for (j = segstart; j < i; j++) {
                if (!peep_parse_ld_hl_paren_sym(lines[j], sym)) continue;
                if (strcmp(sym, best_sym) != 0) continue;
                if (noc < 64) occ[noc++] = j;
            }

            delta = 0;
            /* Last occurrence first: insert_line only ever shifts indices
             * at or after the edit point, so earlier (not yet processed)
             * entries in occ[], including occ[0], stay valid. */
            for (k = noc - 1; k >= 1; k--) {
                replace1_tagged(occ[k], "ld l,c", "global_word_cache_load");
                insert_line(occ[k] + 1, "ld h,b");
                delta += 1;
                changed = 1;
            }

            /* occ[0] itself is left as the real load, keeping the cache
             * fresh right after it. */
            insert_line_tagged(occ[0] + 1, "ld c,l", "global_word_cache_store");
            insert_line(occ[0] + 2, "ld b,h");
            delta += 2;
            changed = 1;

            i += delta;
        }

        segstart = i + 1;
    }

    return changed;
}

/*
 * Collapse DCC's generic code for *(p = p - 1), where p is an int * global.
 * This is the hot pint popv() workaround shape.  The following dereference
 * still sees HL equal to the updated pointer.
 */
static int pass_global_ptr_word_predec_load(void)
{
    int i;
    int changed;
    char sym1[128];
    char sym2[128];
    char line[192];

    changed = 0;
    for (i = 0; i + 8 < nlines; i++) {
        if (!peep_parse_ld_hl_paren_sym(lines[i], sym1)) continue;
        if (!eq(i + 1, "push hl")) continue;
        if (!eq(i + 2, "ld hl,1")) continue;
        if (!eq(i + 3, "add hl,hl")) continue;
        if (!eq(i + 4, "ex de,hl")) continue;
        if (!eq(i + 5, "pop hl")) continue;
        if (!eq(i + 6, "or a")) continue;
        if (!eq(i + 7, "sbc hl,de")) continue;
        if (!peep_parse_ld_paren_sym_hl(lines[i + 8], sym2)) continue;
        if (strcmp(sym1, sym2) != 0) continue;

        sprintf(line, "ld hl,(%s)", sym1);
        replace1_tagged(i, line, "global_ptr_word_predec_load");
        replace1(i + 1, "dec hl");
        replace1(i + 2, "dec hl");
        sprintf(line, "ld (%s),hl", sym1);
        replace1(i + 3, line);
        delete_n(i + 4, 5);
        changed = 1;
        if (i > 0)
            i--;
    }

    return changed;
}

/*
 * pass_elim_ex_de_hl_before_ix_store:
 *
 * After pass_global_ptr_word_predec_load the loaded value is in DE and HL
 * points one past the last byte read.  DCC then generates:
 *
 *   ex de,hl            ; HL = value, DE = stale stp pointer
 *   ld (ix-N),l         ; store value lo byte
 *   ld (ix-N+1),h       ; store value hi byte
 *   ld hl,(sym)         ; HL immediately overwritten by next instruction
 *
 * Because the store only needs the value (which is still in DE), the
 * exchange is unnecessary.  Store from E/D directly:
 *
 *   ld (ix-N),e
 *   ld (ix-N+1),d
 *   ld hl,(sym)
 *
 * Safe because the immediately following instruction (guarded to start with
 * "ld hl,") clobbers HL before it is read, so leaving HL as the stale
 * stp-pointer rather than the value does not matter.
 */
static int pass_elim_ex_de_hl_before_ix_store(void)
{
    int i, next_i, off, changed = 0;
    char next[MAX_LINE];
    char new_lo[64], new_hi[64];

    for (i = 0; i + 3 < nlines; i++) {
        if (!eq(i, "ex de,hl")) continue;
        if (!peep_parse_st_ix_pair(lines[i + 1], lines[i + 2], &off)) continue;

        next_i = i + 3;
        if (next_i < nlines &&
            strstr(lines[next_i], ";@dcc-inline-temp-single-use") != NULL)
            next_i++;
        if (next_i >= nlines)
            continue;
        strip_peep_comment_copy(next, lines[next_i]);

        if (strncmp(next, "ld hl,", 6) != 0) continue;

        sprintf(new_lo, "ld (ix%+d),e", off);
        sprintf(new_hi, "ld (ix%+d),d", off + 1);

        replace1_tagged(i, new_lo, "ex_de_hl_elim");
        replace1(i + 1, new_hi);
        delete_n(i + 2, 1);

        changed = 1;
        if (i > 0) i--;
    }

    return changed;
}


/*
 * pass_elim_redundant_pop_push:
 *
 * After pass_global_ptr_word_postinc_store_setup the generated sequence ends
 * with:
 *
 *   ld hl,(stp)       ; peep: global_ptr_word_postinc_setup
 *   push hl           ; [A] save old stp for the later store
 *   inc hl
 *   inc hl
 *   ld (stp),hl       ; advance stp
 *   pop hl            ; [B] restore old stp into HL ← remove
 *   push hl           ; [C] save it again           ← remove
 *   [instruction that immediately clobbers HL]
 *   ...
 *   pop hl            ; [D] pops [A]'s saved old stp for the store
 *
 * [B]+[C] together are a no-op on both the stack and HL: the same old-stp
 * value pushed at [A] is still on the stack after [C], and the first
 * instruction after [C] always clobbers HL (either ld hl,(x) or
 * ld l,(ix+N)).  Removing [B]+[C] leaves [D] still popping the [A] push.
 *
 * Note: pop de / push de looks similar but is NOT always removable — the
 * pop may set DE for use in the following code while also saving the value
 * back for a later pop into a different register (e.g. the xstrdup strcpy
 * loop).  Only hl is handled here.
 */
/* Returns 1 if HL is written (all of HL, or at least L with H following) before
 * it is read, scanning forward from line `start`.  Conservative: returns 0 if
 * uncertain.  Used to guard pop hl; push hl removal. */
static int hl_is_written_before_read_from(int start)
{
    int j;
    char tmp[MAX_LINE];
    char lo_off[32];

    for (j = start; j < start + 4 && j < nlines; j++) {
        strip_peep_comment_copy(tmp, lines[j]);
        /* Instructions that fully write HL without first reading it */
        if (strncmp(tmp, "ld hl,", 6) == 0) return 1;   /* ld hl,N  or  ld hl,(x) */
        if (peep_parse_ld_l_ix(lines[j], lo_off)) return 1; /* ld l,(ix+N) — H follows */
        if (strcmp(tmp, "pop hl") == 0) return 1;
        /* Instructions that read HL */
        if (strncmp(tmp, "ld ", 3) == 0 && strstr(tmp, "(hl)") != NULL) return 0;
        if (strcmp(tmp, "inc hl") == 0) return 0;
        if (strcmp(tmp, "dec hl") == 0) return 0;
        if (strncmp(tmp, "add hl,", 7) == 0) return 0;
        if (strcmp(tmp, "push hl") == 0) return 0;
        if (strcmp(tmp, "ex de,hl") == 0) return 0;
        /* Otherwise neutral (push de, push bc, push ix, ld de,N, etc.) — keep scanning */
    }
    return 0; /* conservative: don't remove if undetermined */
}

static int pass_elim_redundant_pop_push(void)
{
    int i, changed = 0;

    for (i = 0; i + 1 < nlines; i++) {
        if (eq(i, "pop hl") && eq(i + 1, "push hl") &&
            hl_is_written_before_read_from(i + 2)) {
            delete_n(i, 2);
            changed = 1;
            if (i > 0) i--;
        }
    }

    return changed;
}

/*
 * pass_double_de_before_add:
 *
 * DCC often forms word-array addresses as:
 *
 *     ...             ; DE = index, HL/base saved on stack
 *     ex de,hl
 *     add hl,hl
 *     ex de,hl
 *     pop hl
 *     add hl,de
 *
 * The three middle instructions only double DE while preserving HL for the
 * following pop.  Replace them with a direct 16-bit shift of DE.  This helps
 * pint's run() loop after loading in->a/in->b and using it as an int-array
 * index, and is conservative because it only fires immediately before
 * pop hl / add hl,de where the arithmetic flags from the doubling are dead.
 */
static int pass_double_de_before_add(void)
{
    int i;
    int changed;

    changed = 0;
    for (i = 0; i + 4 < nlines; i++) {
        if (!eq(i, "ex de,hl")) continue;
        if (!eq(i + 1, "add hl,hl")) continue;
        if (!eq(i + 2, "ex de,hl")) continue;
        if (!eq(i + 3, "pop hl")) continue;
        if (!eq(i + 4, "add hl,de")) continue;

        replace1_tagged(i, "sla e", "double_de_before_add");
        replace1(i + 1, "rl d");
        delete_n(i + 2, 1);
        changed = 1;
        if (i > 0)
            i--;
    }

    return changed;
}

/*
 * Fold a constant left shift emitted as repeated HL doublings:
 *
 *     ld hl,N
 *     add hl,hl
 *     ...
 *     add hl,hl
 *     push hl      ; or ex de,hl
 *
 * The folded load has the same 16-bit value.  Restrict the rewrite to the two
 * immediate successors DCC uses for argument/address setup, neither of which
 * consumes the carry/half-carry flags produced by ADD HL,HL.
 */
static int pass_const_hl_doubles(void)
{
    int i;
    int changed;

    changed = 0;
    for (i = 0; i + 2 < nlines; i++) {
        char imm_text[64];
        char line[64];
        int value;
        int count;
        unsigned int folded;

        if (!parse_ld_hl_imm(lines[i], imm_text))
            continue;
        if (!parse_nonneg_int(imm_text, &value))
            continue;
        if (!eq(i + 1, "add hl,hl"))
            continue;

        folded = (unsigned int)value & 0xffffu;
        count = 0;
        while (i + 1 + count < nlines && eq(i + 1 + count, "add hl,hl")) {
            folded = (folded << 1) & 0xffffu;
            count++;
        }
        if (i + 1 + count >= nlines)
            continue;
        if (!eq(i + 1 + count, "push hl") && !eq(i + 1 + count, "ex de,hl"))
            continue;

        sprintf(line, "ld hl,%u", folded);
        replace1_tagged(i, line, "const_hl_doubles");
        delete_n(i + 1, count);
        changed = 1;
        if (i > 0)
            i--;
    }

    return changed;
}

/*
 * pass_ix_frame_ptr_load:
 *
 * Collapse the indirect-via-IX idiom for loading a 16-bit local variable
 * (a pointer stored in the IX frame) into HL via push/pop/dec:
 *
 *   push ix
 *   pop hl
 *   [dec hl] × N        ; N >= 2: HL → IX-N (lo-byte address)
 *   ld e,(hl)           ; E = lo byte at IX-N
 *   inc hl              ; HL → IX-(N-1) (hi-byte address)
 *   ld d,(hl)           ; D = hi byte at IX-(N-1)
 *   ex de,hl            ; HL = 16-bit local pointer value
 *
 * Replaced with two instructions:
 *
 *   ld l,(ix-N)
 *   ld h,(ix-(N-1))
 *
 * Valid because the push/pop cancel on the stack, dec×N offsets HL from IX,
 * and the ld e/ld d/ex sequence is exactly what the IX-indexed loads do.
 * Requires N >= 2 so that both the lo byte (ix-N) and hi byte (ix-(N-1))
 * have strictly negative IX displacements (valid local-variable slots).
 */
static int pass_ix_frame_ptr_load(void)
{
    int i;
    int changed = 0;

    for (i = 0; i + 6 < nlines; i++) {
        int n_dec, j;
        char ld_l[64], ld_h[64];

        if (!eq(i,     "push ix")) continue;
        if (!eq(i + 1, "pop hl"))  continue;

        /* Count consecutive dec hl; require at least 2 */
        j = i + 2;
        n_dec = 0;
        while (j < nlines && eq(j, "dec hl") && n_dec < 7) {
            n_dec++;
            j++;
        }
        if (n_dec < 2) continue;

        /* Mandatory tail: ld e,(hl) / inc hl / ld d,(hl) / ex de,hl */
        if (!eq(j,     "ld e,(hl)")) continue;
        if (!eq(j + 1, "inc hl"))    continue;
        if (!eq(j + 2, "ld d,(hl)")) continue;
        if (!eq(j + 3, "ex de,hl"))  continue;

        /* Total lines consumed: 2 (push/pop) + n_dec + 4 */
        {
            int total = 2 + n_dec + 4;

            sprintf(ld_l, "ld l,(ix-%d)", n_dec);
            sprintf(ld_h, "ld h,(ix-%d)", n_dec - 1);

            delete_n(i, total);
            insert_line_tagged(i, ld_l, "ix_frame_ptr");
            insert_line(i + 1, ld_h);

            changed = 1;
        }
    }

    return changed;
}

/*
 * pass_deref_byte_cmp:
 *
 * After pass_ix_frame_ptr_load simplifies the pointer load, recognise and
 * collapse the pattern of dereferencing a byte through a local pointer and
 * comparing that byte (zero-extended to 16 bits) against another IX local:
 *
 *   ld l,(ix-N)              ; pointer lo  (N >= 2)
 *   ld h,(ix-M)              ; pointer hi  (M == N-1)
 *   ld l,(hl)                ; L = *ptr  (byte dereference)
 *   ld h,0                   ; H = 0     (zero-extend to 16-bit)
 *   push hl
 *   ld l,(ix+P) or (ix-P)    ; L = compare value
 *   ld h,0
 *   ex de,hl                 ; DE = compare value
 *   pop hl                   ; HL = *ptr
 *   or a
 *   sbc hl,de                ; HL = *ptr - cmp
 *   jp z/nz, LABEL
 *
 * Replaced with (6 instructions):
 *
 *   ld l,(ix-N)
 *   ld h,(ix-M)              ; HL = ptr
 *   ld a,(hl)                ; A = *ptr
 *   ld l,(ix+P) or (ix-P)    ; L = compare value
 *   cp l                     ; Z set iff A == L  (same as sbc hl,de for z/nz)
 *   jp z/nz, LABEL
 *
 * Safe for z/nz conditions: sbc hl,de with HL=(0,A) and DE=(0,L) sets Z
 * iff A==L, which is exactly what "cp l" tests.
 */

/*
 * pass_ix_frame_ptr_load_deadd:
 *
 * Collapse the generic IX+constant local load form used for wider negative
 * frame offsets into direct indexed byte loads:
 *
 *   push ix
 *   pop hl
 *   ld de,-12
 *   add hl,de
 *   ld e,(hl)
 *   inc hl
 *   ld d,(hl)
 *   ex de,hl
 *
 * to:
 *
 *   ld l,(ix-12)
 *   ld h,(ix-11)
 */
static int pass_ix_frame_ptr_load_deadd(void)
{
    int i;
    int changed;

    changed = 0;
    for (i = 0; i + 7 < nlines; i++) {
        int off;
        char ld_l[64];
        char ld_h[64];

        if (!eq(i,     "push ix")) continue;
        if (!eq(i + 1, "pop hl")) continue;
        if (!peep_parse_ld_de_signed(lines[i + 2], &off)) continue;
        if (!eq(i + 3, "add hl,de")) continue;
        if (!eq(i + 4, "ld e,(hl)")) continue;
        if (!eq(i + 5, "inc hl")) continue;
        if (!eq(i + 6, "ld d,(hl)")) continue;
        if (!eq(i + 7, "ex de,hl")) continue;

        if (off < -128 || off + 1 > 127)
            continue;

        sprintf(ld_l, "ld l,(ix%+d)", off);
        sprintf(ld_h, "ld h,(ix%+d)", off + 1);
        delete_n(i, 8);
        insert_line_tagged(i, ld_l, "ix_frame_ptr_deadd");
        insert_line(i + 1, ld_h);
        changed = 1;
        if (i > 0)
            i--;
    }

    return changed;
}

static int hoistbc_parse_ld_l_ix_off(const char *s, int *off)
{
    char buf[MAX_LINE];
    char *semi;
    int n;
    const char *p;
    int sign;
    int v;

    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    semi = strchr(buf, ';');
    if (semi) *semi = 0;
    n = (int)strlen(buf);
    while (n > 0 && (buf[n - 1] == ' ' || buf[n - 1] == '\t'))
        buf[--n] = 0;

    if (strncmp(buf, "ld l,(ix", 8) != 0)
        return 0;
    p = buf + 8;
    if (*p == '+') { sign = 1; p++; }
    else if (*p == '-') { sign = -1; p++; }
    else return 0;
    if (*p < '0' || *p > '9') return 0;
    v = 0;
    while (*p >= '0' && *p <= '9')
        v = v * 10 + (*p++ - '0');
    if (strcmp(p, ")") != 0) return 0;
    *off = sign * v;
    return 1;
}

/* Same as hoistbc_parse_ld_l_ix_off, for "ld c,(ix+N)" - the compiler's own
 * BC-regalloc load shape (see function_has_bc_regalloc_entry) rather than
 * this pass's own hoist-candidate shape. */
static int hoistbc_parse_ld_c_ix_off(const char *s, int *off)
{
    char buf[MAX_LINE];
    char *semi;
    int n;
    const char *p;
    int sign;
    int v;

    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    semi = strchr(buf, ';');
    if (semi) *semi = 0;
    n = (int)strlen(buf);
    while (n > 0 && (buf[n - 1] == ' ' || buf[n - 1] == '\t'))
        buf[--n] = 0;

    if (strncmp(buf, "ld c,(ix", 8) != 0)
        return 0;
    p = buf + 8;
    if (*p == '+') { sign = 1; p++; }
    else if (*p == '-') { sign = -1; p++; }
    else return 0;
    if (*p < '0' || *p > '9') return 0;
    v = 0;
    while (*p >= '0' && *p <= '9')
        v = v * 10 + (*p++ - '0');
    if (strcmp(p, ")") != 0) return 0;
    *off = sign * v;
    return 1;
}

/* Conservative check: does this line reference register `lo`, `hi`, or
 * the `pair` register pair (e.g. "b"/"c"/"bc") in any way? Guards a pass's
 * exclusive claim on a register pair for the whole loop body. A false
 * positive just declines the optimization; a false negative could
 * silently corrupt a live value, so this errs deliberately broad - every
 * Z80 mnemonic that touches a register pair implicitly (the block/repeat
 * instructions all use BC as a counter and DE/HL as pointers) is
 * included, not just explicit register-name operands. */
static int line_touches_reg_pair(const char *s, const char *lo, const char *hi,
                                 const char *pair)
{
    static const char *implicit_pair_mnemonics[] = {
        "djnz ", "ldir", "lddr", "cpir", "cpdr",
        "otir", "otdr", "inir", "indr",
        "ldi", "ldd", "cpi", "cpd", "ini", "ind", "outi", "outd",
        NULL
    };
    const char *p;
    char tok[16];
    char paren[8];
    int ti;
    int i;

    for (i = 0; implicit_pair_mnemonics[i] != NULL; ++i)
        if (strncmp(s, implicit_pair_mnemonics[i], strlen(implicit_pair_mnemonics[i])) == 0)
            return 1;

    sprintf(paren, "(%s)", pair);
    if (strstr(s, paren) != NULL)
        return 1;

    p = s;
    while (*p) {
        if (isalpha((unsigned char)*p) || *p == '_') {
            ti = 0;
            while ((isalnum((unsigned char)*p) || *p == '_') && ti < 15)
                tok[ti++] = *p++;
            tok[ti] = 0;
            if (strcmp(tok, lo) == 0 || strcmp(tok, hi) == 0 || strcmp(tok, pair) == 0)
                return 1;
        } else {
            p++;
        }
    }
    return 0;
}

static int line_touches_bc(const char *s)
{
    return line_touches_reg_pair(s, "b", "c", "bc");
}

static int line_touches_de(const char *s)
{
    return line_touches_reg_pair(s, "d", "e", "de");
}

static int line_touches_hl(const char *s)
{
    return line_touches_reg_pair(s, "l", "h", "hl");
}

/* True iff [func_start, func_end) contains the compiler's own BC-regalloc
 * load shape: "ld c,(ix+N)" immediately followed by "ld b,(ix+N+1)" for the
 * same N, wherever it appears (the function's initial load of the cached
 * parameter, or any later on-demand reload dcc_func.c's regalloc_buffer_
 * finalize inserted at a loop header). That exact two-line pair is only
 * ever emitted for a bc-resident parameter (dcc_symbols.c's REG_BC
 * candidate) - nothing else in codegen produces it - so its presence means
 * BC is already claimed for the life of this function, for a value this
 * pass has no way to know is safe to clobber.
 *
 * Without this check, pass_hoist_index_ptr_to_bc and this pass raced for
 * the same register: dcc reserves BC for a read-only parameter for the
 * whole function, but this peephole pass runs later and has no visibility
 * into that reservation, so it could just as easily decide to hoist an
 * unrelated loop counter/pointer into BC too - silently overwriting the
 * cached parameter the moment the loop's own header (or any reload dcc
 * inserted) next reads it. Found via tests/tvla.c and tests/tautolcs.c
 * after tightening dcc_func.c's own reload insertion to be conditional on
 * an actual clobber: without that context, dcc's conditional reload logic
 * cannot see a hoist THIS pass performs after dcc has already finished, so
 * the only place left that can reliably avoid the collision is here. */
static int function_has_bc_regalloc_entry(int func_start, int func_end)
{
    int k;
    int off;

    for (k = func_start; k + 1 < func_end; ++k) {
        if (hoistbc_parse_ld_c_ix_off(lines[k], &off)) {
            char exp_b[40];
            sprintf(exp_b, "ld b,(ix%+d)", off + 1);
            if (eq(k + 1, exp_b))
                return 1;
        }
    }
    return 0;
}

/*
 * pass_hoist_index_ptr_to_bc:
 *
 * Hoists a loop-invariant pointer parameter/local used as an array/pointer
 * index base into BC for the duration of a straight-line for-loop body,
 * eliminating a redundant frame reload every iteration:
 *
 *   LABEL:
 *     ld l,(ix+P)
 *     ld h,(ix+P+1)
 *     ...
 *     jp COND, LABEL          (closing branch, no internal label)
 *
 * becomes:
 *
 *     ld c,(ix+P)
 *     ld b,(ix+P+1)
 *   LABEL:
 *     ld l,c
 *     ld h,b
 *     ...
 *     jp COND, LABEL
 *
 * Declines (never misapplies) unless, across the WHOLE loop body:
 *   - every reference to offset P or P+1 is exactly one of the two lines
 *     above (so the frame slot is never written, and never read any other
 *     way this pass doesn't already account for);
 *   - B, C, and BC are never referenced by anything else (so hoisting the
 *     pointer into BC can't clobber or be clobbered by anything else the
 *     loop does); and
 *   - every call in the body is on the small whitelist already used by
 *     pass_byte_loop_counter_to_reg_c (__mods, __divs - documented to
 *     preserve BC).
 *
 * No write-back is needed: the transform only ever READS (ix+P)/(ix+P+1),
 * so the original frame slot is untouched and still correct for any use
 * after the loop - including an early-return/if-guarded exit inside the
 * loop body, as long as loop_body_internal_labels_safe proves every
 * internal label the loop contains is only ever reached from within the
 * loop itself (see that function's own comment for why this check exists
 * and what it protects against).
 */
static int pass_hoist_index_ptr_to_bc(void)
{
    int i, k;
    int changed;
    char label[128];
    char tgt[128];
    int loop_end;
    int off;
    char pat_l[40];
    char pat_h[40];
    char probe_p[40];
    char probe_p1[40];
    int ok;
    int found_pair;
    char prime_c[40];
    char prime_b[40];

    changed = 0;

    for (i = 0; i < nlines; ++i) {
        if (!starts_label(lines[i]))
            continue;

        strcpy(label, lines[i]);
        k = (int)strlen(label);
        if (k > 0 && label[k - 1] == ':')
            label[k - 1] = 0;

        /* Find this loop's own closing branch back to LABEL - the LAST
         * line in the function that jumps back to it, not the first: an
         * if/else that both continue the same loop compiles to two
         * separate backward jumps (e.g. "jp nz, LABEL" then, a couple of
         * lines later, an unconditional "jp LABEL"), and stopping at the
         * first one would silently exclude the rest of the loop's own body
         * from every check below. An internal label (an if/early-return
         * inside the loop body, e.g. tests/tbig.c's check_record) is fine
         * PROVIDED loop_body_internal_labels_safe proves every such label
         * is only ever targeted from within this same range - never a
         * re-entry point some unrelated code elsewhere in the function
         * jumps into. An earlier, unconditional version of this relaxation
         * (ignoring every internal label outright) skipped that proof and
         * let the scan run past what was actually a single loop's own body
         * into unrelated code reusing the same frame offset for a
         * different, non-overlapping-scope variable, corrupting
         * tests/cint.c and tests/fint.c; this reachability check is what
         * makes the relaxed scan safe. Bounded by the next function's own
         * "public NAME" so a candidate that never loops back can't run
         * past this function's own code. */
        loop_end = -1;
        for (k = i + 1; k < nlines; ++k) {
            if (strncmp(lines[k], "public ", 7) == 0)
                break;
            if (jump_target(lines[k], tgt) && strcmp(tgt, label) == 0)
                loop_end = k;
        }
        if (loop_end < 0)
            continue;
        if (!loop_body_internal_labels_safe(i + 1, loop_end))
            continue;

        /* The priming "ld c,(ix+P) / ld b,(ix+P+1)" is inserted textually
         * immediately before LABEL, so LABEL's only fall-through
         * predecessor becomes the priming itself: any path that falls into
         * the header necessarily executes the priming first. The one way to
         * reach the header WITHOUT passing through the priming is a jump
         * whose target is LABEL and that originates OUTSIDE this loop's own
         * body - it lands on the header AFTER the priming and bypasses it,
         * leaving BC uninitialised so the rewritten "ld l,c / ld h,b" reads
         * garbage. (A back-edge from within the body is fine: the priming
         * already ran on first entry and BC is loop-invariant across the
         * body, which the guards below enforce.) Decline if any such
         * external branch to the header exists.
         *
         * Without this, tests/thoistbc.c - two while-loops sharing the frame
         * variable "head" - was silently miscompiled: the second loop's
         * header is reached by branches from the first loop, so the priming
         * placed before it never ran. Note it is the HEADER label that must
         * have no external entry; an external branch to a DIFFERENT label
         * stacked ABOVE the priming (e.g. tests/tautolcs.c, where an outer
         * "jp z, Lx" lands on a label sitting above the inserted priming and
         * still flows through it) stays correct and must remain optimised. */
        {
            int func_start, func_end;
            int external_entry = 0;
            find_function_bounds(i, &func_start, &func_end);
            /* dcc itself may already have BC claimed for the whole function
             * (a read-only parameter cached across every call site - see
             * function_has_bc_regalloc_entry). This pass has no visibility
             * into that reservation and no way to safely coexist with it -
             * the "no other line touches bc" check below only covers this
             * one loop's body, not the reload dcc may have planted at a
             * DIFFERENT loop's header elsewhere in the same function. */
            if (function_has_bc_regalloc_entry(func_start, func_end))
                continue;
            for (k = func_start; k < func_end; ++k) {
                if (jump_target(lines[k], tgt) && strcmp(tgt, label) == 0) {
                    if (k <= i || k > loop_end) { external_entry = 1; break; }
                }
            }
            if (external_entry)
                continue;
        }

        /* Pick a candidate offset P from the first "ld l,(ix+P)" / "ld
         * h,(ix+P+1)" consecutive pair found in the body. */
        off = 0;
        found_pair = 0;
        for (k = i + 1; k < loop_end - 1 && !found_pair; ++k) {
            char exp_h[40];
            if (!hoistbc_parse_ld_l_ix_off(lines[k], &off))
                continue;
            sprintf(exp_h, "ld h,(ix%+d)", off + 1);
            if (eq(k + 1, exp_h))
                found_pair = 1;
        }
        if (!found_pair)
            continue;

        sprintf(pat_l, "ld l,(ix%+d)", off);
        sprintf(pat_h, "ld h,(ix%+d)", off + 1);
        sprintf(probe_p, "(ix%+d)", off);
        sprintf(probe_p1, "(ix%+d)", off + 1);

        ok = 1;
        for (k = i + 1; k < loop_end && ok; ++k) {
            if (strncmp(lines[k], "call ", 5) == 0) {
                if (!eq(k, "call __mods") && !eq(k, "call __divs")) { ok = 0; break; }
                continue;
            }
            if (eq(k, pat_l) || eq(k, pat_h))
                continue;
            if (strstr(lines[k], probe_p) != NULL || strstr(lines[k], probe_p1) != NULL) { ok = 0; break; }
            if (line_touches_bc(lines[k])) { ok = 0; break; }
        }
        if (!ok)
            continue;

        for (k = i + 1; k < loop_end; ++k) {
            if (eq(k, pat_l)) { replace1_tagged(k, "ld l,c", "hoist_index_ptr_to_bc"); continue; }
            if (eq(k, pat_h)) { replace1_tagged(k, "ld h,b", "hoist_index_ptr_to_bc"); continue; }
        }

        sprintf(prime_c, "ld c,(ix%+d)", off);
        sprintf(prime_b, "ld b,(ix%+d)", off + 1);
        insert_line_tagged(i, prime_b, "hoist_index_ptr_to_bc");
        insert_line_tagged(i, prime_c, "hoist_index_ptr_to_bc");

        changed = 1;
    }

    return changed;
}

/* Same target-label parse as jump_target above, but also accepts "jr "
 * forms - jump_target alone only recognises "jp ", which is all
 * pass_hoist_index_ptr_to_bc and pass_byte_for_counter_to_reg_e need since
 * both run early enough in the fixed-point loop to see the closing branch
 * while it is still a "jp"; by the time pass_walk_hoisted_index_ptr below
 * runs (after both, consuming their output), an earlier same-iteration or
 * prior-iteration jp_to_jr pass may already have shrunk that same branch to
 * "jr", so this pass needs to recognise either spelling to keep finding the
 * loop's own bounds regardless of exactly when in the fixed-point sequence
 * it happens to run. */
static int jump_target_any(const char *s, char *out)
{
    const char *p;
    int i;

    if (strncmp(s, "jp ", 3) == 0 || strncmp(s, "jr ", 3) == 0)
        p = s + 3;
    else
        return 0;

    while (*p && *p != ',')
        p++;

    if (*p == ',') {
        p++;
        while (*p == ' ' || *p == '\t')
            p++;
    } else {
        p = s + 3;
        while (*p == ' ' || *p == '\t')
            p++;
    }

    if (*p == 0)
        return 0;

    i = 0;
    while (*p && *p != ' ' && *p != '\t' && i < 120)
        out[i++] = *p++;
    out[i] = 0;
    return i > 0;
}

static int zero_cond_jump_target_any(const char *s, char *out)
{
    char tmp[MAX_LINE];

    strip_peep_comment_copy(tmp, s);
    if (strncmp(tmp, "jp z,", 5) != 0 &&
        strncmp(tmp, "jp nz,", 6) != 0 &&
        strncmp(tmp, "jr z,", 5) != 0 &&
        strncmp(tmp, "jr nz,", 6) != 0)
        return 0;
    return jump_target_any(tmp, out);
}

/* True iff "a" or "af" appears as a whole token anywhere in `s` (comment
 * stripped first). Used only by a_dead_or_overwritten_from's conservative
 * liveness proof below - unlike line_touches_reg_pair's b/c/d/e callers,
 * there is no flag-condition mnemonic spelled "a" to special-case. */
static int line_touches_a(const char *s)
{
    char tmp[MAX_LINE];
    const char *p;
    char tok[16];
    int ti;

    strip_peep_comment_copy(tmp, s);
    p = tmp;
    while (*p) {
        if (isalpha((unsigned char)*p) || *p == '_') {
            ti = 0;
            while ((isalnum((unsigned char)*p) || *p == '_') && ti < 15)
                tok[ti++] = *p++;
            tok[ti] = 0;
            if (strcmp(tok, "a") == 0 || strcmp(tok, "af") == 0)
                return 1;
        } else {
            p++;
        }
    }
    return 0;
}

static int is_uncond_jr(const char *s)
{
    const char *p;

    if (strncmp(s, "jr ", 3) != 0)
        return 0;
    p = s + 3;
    while (*p) {
        if (*p == ',')
            return 0;
        p++;
    }
    return 1;
}

/* Trace forward from `start`, following only unconditional control flow
 * (label fall-through, unconditional jp/jr), for up to a bounded number of
 * hops: true iff A is provably overwritten by a fresh "ld a,X" - so nothing
 * later on this path can observe whatever this pass just left in A - or
 * execution reaches the function's own epilogue ("ld sp,ix") with A never
 * referenced at all. A conditional jump (ambiguous which way execution
 * goes), a call (could return a value in A), or running out of hops without
 * resolving either way is treated as unprovable - a decline, not a
 * misapplication. Modelled directly on escape_path_reaches_epilogue_safely
 * above, checking A-liveness instead of a frame-offset pattern. */
static int a_dead_or_overwritten_from(int start, int func_end)
{
    int pos;
    int hops;
    char tmp[MAX_LINE];
    char tgt[128];

    pos = start;
    for (hops = 0; hops < 60; ++hops) {
        if (pos < 0 || pos >= func_end)
            return 0;
        if (eq(pos, "ld sp,ix"))
            return 1;
        strip_peep_comment_copy(tmp, lines[pos]);
        if (strncmp(tmp, "ld a,", 5) == 0)
            return 1;
        if (line_touches_a(lines[pos]))
            return 0;
        if (starts_label(lines[pos])) { ++pos; continue; }
        if (strncmp(lines[pos], "call ", 5) == 0)
            return 0;
        if (jump_target_any(lines[pos], tgt)) {
            if (!is_uncond_jp(lines[pos]) && !is_uncond_jr(lines[pos]))
                return 0;  /* a conditional jump - which way is ambiguous */
            pos = find_label_line(tgt, 0, func_end);
            continue;
        }
        ++pos;
    }
    return 0;
}

/*
 * pass_walk_hoisted_index_ptr:
 *
 * pass_hoist_index_ptr_to_bc hoists a loop-invariant array/pointer base into
 * BC; pass_byte_for_counter_to_reg_e (its counterpart, triggered precisely
 * because that hoist already claimed C/BC) promotes the loop's own byte
 * counter into E. Between them the per-iteration element address is cheap
 * to each build, but still gets recombined into HL from scratch every single
 * iteration:
 *
 *   ld l,c
 *   ld h,b
 *   ld d,0
 *   add hl,de
 *   ld (hl),a          ; or: cp (hl)
 *
 * Since BC's cached value and E's counter both only ever advance by exactly
 * 1 in lockstep every iteration (proved below, not assumed), BC itself can
 * walk forward by one byte per iteration instead of being recombined with E
 * from scratch each time. A store becomes a direct write through BC:
 *
 *   ld (bc),a
 *   inc bc
 *
 * A compare (tests/tbig.c's check_record: `if (b[i] != rhs) ...`) is
 * trickier - Z80 has no "cp (bc)" - so the rhs already sitting in A is
 * parked in D first, the array byte is fetched into A instead, and the two
 * are compared the other way around (equivalent: (a==b) == (b==a)):
 *
 *   ld d,a
 *   ld a,(bc)
 *   cp d
 *   inc bc
 *
 * unlike "cp (hl)", this leaves A holding the array byte afterward rather
 * than the original rhs - harmless for a flags-only compare (both forms set
 * the same Z flag), but only when A's old value is never read again, which
 * a_dead_or_overwritten_from proves separately for the compare case before
 * this pass ever touches such a loop.
 *
 * BC's original (ix+P)/(ix+P+1) frame slot is never written by
 * pass_hoist_index_ptr_to_bc - only ever read, once, to prime BC before the
 * loop - so nothing outside the loop can observe BC walking away from that
 * value; the frame slot remains authoritative for any later use of the
 * pointer, and no write-back is needed here either, mirroring that pass's
 * own reasoning.
 *
 * This does not trust pass_hoist_index_ptr_to_bc's/pass_byte_for_counter_to_
 * reg_e's own comment tags as a safety proof (nothing else in this file
 * gates correctness on another pass's tag, and this should not be the first
 * exception) - every precondition is independently reverified here:
 *   - exactly one "ld l,c / ld h,b / ld d,0 / add hl,de" occurs in the loop
 *     body, eventually followed by "ld (hl),a" or "cp (hl)" with nothing
 *     touching H or L in between (the rhs value is computed after the
 *     address, so the access is not necessarily the very next line - but
 *     nothing may disturb HL before it runs); more than one candidate
 *     occurrence declines outright rather than guessing which, if any, is
 *     safe to walk;
 *   - for a compare specifically, the line right after "cp (hl)" is a z/nz
 *     jump (operand reversal preserves equality, but not ordered flags), and
 *     a_dead_or_overwritten_from proves A is dead (or freshly overwritten)
 *     on BOTH the fall-through path and the jump's own target before this
 *     pass commits to leaving the array byte in A instead of the original
 *     rhs;
 *   - B, C, and BC are referenced nowhere else in the loop body (so this
 *     really is a loop-invariant pointer with nothing else relying on BC
 *     holding its original, unwalked value mid-loop);
 *   - D and E are referenced nowhere else in the loop body except exactly
 *     one "inc e" (so E - and hence the recombined address - provably
 *     advances by exactly 1 every iteration, matching the +1 pointer walk
 *     this transform performs).
 * Declining (0) is always safe: the loop keeps recomputing its address the
 * ordinary way.
 */
static int pass_walk_hoisted_index_ptr(void)
{
    int i, k;
    int changed;
    char label[128];
    char tgt[128];
    int loop_end;
    int match_k;
    int access_k;
    int access_is_cmp;
    int match_count;
    int inc_e_count;
    int bc_ok;
    int de_ok;
    int func_start, func_end;

    changed = 0;

    for (i = 0; i < nlines; ++i) {
        if (!starts_label(lines[i]))
            continue;

        strcpy(label, lines[i]);
        k = (int)strlen(label);
        if (k > 0 && label[k - 1] == ':')
            label[k - 1] = 0;

        loop_end = -1;
        for (k = i + 1; k < nlines; ++k) {
            if (strncmp(lines[k], "public ", 7) == 0)
                break;
            if (jump_target_any(lines[k], tgt) && strcmp(tgt, label) == 0)
                loop_end = k;
        }
        if (loop_end < i + 5)
            continue;
        if (!loop_body_internal_labels_safe(i + 1, loop_end))
            continue;

        match_k = -1;
        match_count = 0;
        for (k = i + 1; k + 4 < loop_end; ++k) {
            if (eq(k, "ld l,c") && eq(k + 1, "ld h,b") &&
                eq(k + 2, "ld d,0") && eq(k + 3, "add hl,de")) {
                if (match_count == 0)
                    match_k = k;
                match_count++;
            }
        }
        if (match_count != 1)
            continue;

        /* The rhs value (e.g. "ld a,(ix+4) / add a,e") is computed AFTER
         * the address, so the access is not necessarily the very next line -
         * scan forward for it, requiring every intervening line to leave HL
         * alone (a-only/e-only arithmetic is fine; anything touching H or L
         * is not, since it would corrupt the very address just built). */
        access_k = -1;
        access_is_cmp = 0;
        for (k = match_k + 4; k < loop_end; ++k) {
            if (eq(k, "ld (hl),a")) {
                access_k = k;
                access_is_cmp = 0;
                break;
            }
            if (eq(k, "cp (hl)")) {
                access_k = k;
                access_is_cmp = 1;
                break;
            }
            if (line_touches_hl(lines[k]))
                break;
        }
        if (access_k < 0)
            continue;

        /* A compare leaves the array byte in A afterward instead of the
         * original rhs (see the pass's own doc comment) - only safe when
         * nothing downstream ever reads that stale rhs value again. The
         * compare's own result is only ever consulted via flags, through
         * the conditional jump immediately following it - anything else
         * there is a shape this pass does not understand, so decline. */
        if (access_is_cmp) {
            char jtgt[128];

            if (access_k + 1 >= loop_end)
                continue;
            if (!zero_cond_jump_target_any(lines[access_k + 1], jtgt))
                continue;

            find_function_bounds(i, &func_start, &func_end);
            if (!a_dead_or_overwritten_from(access_k + 2, func_end))
                continue;
            {
                int jtgt_line = find_label_line(jtgt, func_start, func_end);
                if (jtgt_line < 0 ||
                    !a_dead_or_overwritten_from(jtgt_line, func_end))
                    continue;
            }
        }

        bc_ok = 1;
        for (k = i + 1; k < loop_end && bc_ok; ++k) {
            if (k == match_k || k == match_k + 1)
                continue;
            if (line_touches_bc(lines[k]))
                bc_ok = 0;
        }
        if (!bc_ok)
            continue;

        /* D must never be written except the matched "ld d,0", and E never
         * written except the one "inc e" - together proving the recombined
         * address advances by exactly 1 every iteration. Reading e (e.g.
         * "add a,e" for the rhs arithmetic, the normal shape once the
         * counter is e-resident) is not a hazard and is explicitly
         * whitelisted rather than caught by the blanket line_touches_de
         * check below, same narrow-whitelist style pass_byte_for_counter_
         * to_reg_e itself uses for the pre-promotion "add a,(ix+off)" shape
         * this becomes once e holds the counter. */
        de_ok = 1;
        inc_e_count = 0;
        for (k = i + 1; k < loop_end && de_ok; ++k) {
            if (k == match_k + 2 || k == match_k + 3)
                continue;
            if (eq(k, "inc e")) {
                inc_e_count++;
                continue;
            }
            if (eq(k, "add a,e") || eq(k, "cp e") || eq(k, "ld a,e"))
                continue;
            if (line_touches_de(lines[k]))
                de_ok = 0;
        }
        if (!de_ok || inc_e_count != 1)
            continue;

        /* BC is primed with the pointer's raw base (element 0), but the
         * loop's first iteration needs to store at base+INIT - in the
         * original code that offset came from e's own initial value
         * (primed by pass_byte_for_counter_to_reg_e as "ld e,INIT"), folded
         * in by the first iteration's own "add hl,de". Walking bc directly
         * skips that fold entirely, so it must be added once, up front, to
         * bc's own priming instead - missing this exact adjustment first
         * showed up as fill_record silently writing every byte 4 positions
         * too early (confirmed via tests/tbig.c: record 0's stamp read back
         * as 0x04030201 instead of 0, i.e. bytes 4..7's values landing in
         * bytes 0..3). Scan backward a bounded distance for that priming
         * line, matching pass_byte_for_counter_to_reg_e's own backward-scan
         * distance for the same line when it first inserted it. */
        {
            int init_val;
            int found_init;
            int scan_limit;

            found_init = 0;
            init_val = 0;
            scan_limit = i - 8;
            if (scan_limit < 0) scan_limit = 0;
            for (k = i - 1; k >= scan_limit; --k) {
                if (starts_label(lines[k]))
                    break;
                if (peep_parse_ld_e_imm8(lines[k], &init_val)) {
                    found_init = 1;
                    break;
                }
            }
            if (!found_init)
                continue;

            if (init_val > 0) {
                char ld_hl_init[40];
                sprintf(ld_hl_init, "ld hl,%d", init_val);
                insert_line_tagged(i, "ld c,l", "walk_hoisted_index_ptr");
                insert_line_tagged(i, "ld b,h", "walk_hoisted_index_ptr");
                insert_line_tagged(i, "add hl,bc", "walk_hoisted_index_ptr");
                insert_line_tagged(i, ld_hl_init, "walk_hoisted_index_ptr");
                i += 4;
                loop_end += 4;
                match_k += 4;
                access_k += 4;
            }
        }

        {
            int access_after_delete = access_k - 4;
            delete_n(match_k, 4);
            if (access_is_cmp) {
                replace1_tagged(access_after_delete, "ld d,a", "walk_hoisted_index_ptr");
                insert_line_tagged(access_after_delete + 1, "ld a,(bc)", "walk_hoisted_index_ptr");
                insert_line_tagged(access_after_delete + 2, "cp d", "walk_hoisted_index_ptr");
                insert_line_tagged(access_after_delete + 3, "inc bc", "walk_hoisted_index_ptr");
            } else {
                replace1_tagged(access_after_delete, "ld (bc),a", "walk_hoisted_index_ptr");
                insert_line_tagged(access_after_delete + 1, "inc bc", "walk_hoisted_index_ptr");
            }
        }

        changed = 1;
    }

    return changed;
}

/*
 * pass_walk_row_cached_float_index:
 *
 * A float 2D array indexed as ROW[k] inside a k-loop, where ROW is itself a
 * loop-invariant row-base pointer already cached in a frame slot (by the
 * compiler's own row-invariant-2D-read hoist) - tests/mm.c's matmult()/
 * fmatmult(): A[i][k] inside the k-loop, with A[i]'s row address cached once
 * per (i,j) pair - still recomputes the element address from that cached
 * row base plus k*4 (the float stride) from scratch every iteration:
 *
 *   ld l,(ix-R)
 *   ld h,(ix-R-1)
 *   push hl
 *   ld l,(ix-K)
 *   ld h,0
 *   add hl,hl
 *   add hl,hl
 *   ex de,hl
 *   pop hl
 *   add hl,de          ; hl = row_base + k*4
 *   ld e,(hl)           ; 4-byte float read follows...
 *   inc hl
 *   ld d,(hl)
 *   inc hl
 *   ld a,(hl)
 *   inc hl
 *   ld h,(hl)
 *   ld l,a
 *   ex de,hl
 *   push de
 *   push hl             ; ...packed as a stack argument for a call
 *
 * IY is otherwise free here (see pass_byte_loop_counter_to_reg_iyl's own
 * comment: nothing else in dcc's codegen or DCCRTL.MAC ever touches it) -
 * primed once, before the loop, to exactly this element's address, IY can
 * then just walk forward by the float stride (4) every iteration instead of
 * rebuilding the address from the cached row base and k from scratch:
 *
 *   push iy              ; copy the walking pointer into hl for the read
 *   pop hl
 *   ld e,(hl)             ; unchanged 4-byte read
 *   ...
 *   ld l,a
 *   ex de,hl
 *   push de
 *   push hl
 *   ld de,4               ; borrowed here: de is dead from just after this
 *   add iy,de              ; "push hl" until the next statement's own fresh
 *                           ; "ld d,.." write (verified below)
 *
 * Unlike a direct memory read, IY's own indexed addressing mode carries a
 * real per-access tax (19T vs 7T for the same read through HL) that would
 * eat the whole saving if the 4-byte float read were done directly through
 * IY - so IY only ever holds the address; the read itself still goes
 * through HL, via a one-instruction-pair copy ("push iy"/"pop hl") that is
 * far cheaper than rebuilding the address from the row base and k.
 *
 * Every precondition is verified from the generated text, not assumed:
 *   - exactly one occurrence of the full address+read+pack shape above
 *     (more than one, or none, declines outright);
 *   - the row-base frame slot (ix-R)/(ix-R-1) is referenced nowhere else in
 *     the loop body (so it truly is loop-invariant, matching what the
 *     upstream row-invariant-2D-read hoist already proved when it created
 *     that slot - reverified here rather than trusted);
 *   - the counter's own frame slot (ix-K) is incremented by exactly one
 *     "inc (ix-K)" per iteration, matching this loop's own k++;
 *   - immediately after the argument-packing "push hl", D and E are free
 *     until the very next fresh "ld d,X" write, with no read of the stale
 *     value in between (verified by a bounded forward scan) - that gap is
 *     where the once-per-iteration "+4" borrows DE without needing its own
 *     push/pop.
 * Declining (0) is always safe: the loop keeps recomputing the address the
 * ordinary way.
 */
static int pass_walk_row_cached_float_index(void)
{
    int i, k;
    int changed;
    char label[128];
    char tgt[128];
    int loop_end;
    int match_k;
    int row_off;
    int k_off;
    int match_count;
    int row_ok;
    int inc_count;
    int de_gap;
    int init_off, init_val;
    int found_init;
    int scan_limit;

    changed = 0;

    for (i = 0; i < nlines; ++i) {
        if (!starts_label(lines[i]))
            continue;

        strcpy(label, lines[i]);
        k = (int)strlen(label);
        if (k > 0 && label[k - 1] == ':')
            label[k - 1] = 0;

        loop_end = -1;
        for (k = i + 1; k < nlines; ++k) {
            if (strncmp(lines[k], "public ", 7) == 0)
                break;
            if (jump_target_any(lines[k], tgt) && strcmp(tgt, label) == 0)
                loop_end = k;
        }
        if (loop_end < i + 22)
            continue;
        if (!loop_body_internal_labels_safe(i + 1, loop_end))
            continue;

        /* IY is a single register: a call anywhere in this loop's body to
         * another function defined in this same file, which might itself
         * have a loop promoted to IY (by this same pass or pass_byte_loop_
         * counter_to_reg_iyl), would silently clobber this loop's live
         * walking pointer across the call - the exact hazard scan_local_
         * func_labels/is_local_func_label exist to catch (see
         * pass_byte_loop_counter_to_reg_iyl's own identical check). An RTL
         * call (e.g. __fmaf) is fine - DCCRTL.MAC never touches IY. */
        {
            int call_ok = 1;
            for (k = i + 1; k < loop_end && call_ok; ++k) {
                char callee[128];
                const char *p;
                /* A nested loop already promoted to IYL (undocumented-Z80
                 * mode only) inside this loop's own body is the same
                 * collision one level down - same declines-outright
                 * treatment pass_byte_loop_counter_to_reg_iyl gives it. */
                if (strncmp(lines[k], "db 0FDh,", 8) == 0) {
                    call_ok = 0;
                    continue;
                }
                if (strncmp(lines[k], "call ", 5) != 0)
                    continue;
                strip_peep_comment_copy(callee, lines[k]);
                p = callee + 5;
                while (*p == ' ' || *p == '\t')
                    p++;
                if (is_local_func_label(p))
                    call_ok = 0;
            }
            if (!call_ok)
                continue;
        }

        match_k = -1;
        match_count = 0;
        row_off = 0;
        k_off = 0;
        for (k = i + 1; k + 20 < loop_end; ++k) {
            int r, kc;
            char exp_h[40];

            if (!stride_parse_ld_r_ix_neg(lines[k], 'l', &r))
                continue;
            sprintf(exp_h, "ld h,(ix-%d)", r - 1);
            if (!eq(k + 1, exp_h))
                continue;
            if (!eq(k + 2, "push hl"))
                continue;
            if (!stride_parse_ld_r_ix_neg(lines[k + 3], 'l', &kc))
                continue;
            if (!eq(k + 4, "ld h,0")) continue;
            if (!eq(k + 5, "add hl,hl")) continue;
            if (!eq(k + 6, "add hl,hl")) continue;
            if (!eq(k + 7, "ex de,hl")) continue;
            if (!eq(k + 8, "pop hl")) continue;
            if (!eq(k + 9, "add hl,de")) continue;
            if (!eq(k + 10, "ld e,(hl)")) continue;
            if (!eq(k + 11, "inc hl")) continue;
            if (!eq(k + 12, "ld d,(hl)")) continue;
            if (!eq(k + 13, "inc hl")) continue;
            if (!eq(k + 14, "ld a,(hl)")) continue;
            if (!eq(k + 15, "inc hl")) continue;
            if (!eq(k + 16, "ld h,(hl)")) continue;
            if (!eq(k + 17, "ld l,a")) continue;
            if (!eq(k + 18, "ex de,hl")) continue;
            if (!eq(k + 19, "push de")) continue;
            if (!eq(k + 20, "push hl")) continue;

            if (match_count == 0) {
                match_k = k;
                row_off = r;
                k_off = kc;
            }
            match_count++;
        }
        if (match_count != 1)
            continue;

        row_ok = 1;
        {
            char pat_row_lo[40], pat_row_hi[40];
            sprintf(pat_row_lo, "(ix-%d)", row_off);
            sprintf(pat_row_hi, "(ix-%d)", row_off - 1);
            for (k = i + 1; k < loop_end && row_ok; ++k) {
                if (k == match_k || k == match_k + 1)
                    continue;
                if (strstr(lines[k], pat_row_lo) != NULL ||
                    strstr(lines[k], pat_row_hi) != NULL)
                    row_ok = 0;
            }
        }
        if (!row_ok)
            continue;

        inc_count = 0;
        {
            char pat_inc[40];
            sprintf(pat_inc, "inc (ix-%d)", k_off);
            for (k = i + 1; k < loop_end; ++k)
                if (eq(k, pat_inc))
                    inc_count++;
        }
        if (inc_count != 1)
            continue;

        de_gap = -1;
        for (k = match_k + 21; k < loop_end; ++k) {
            char tmp[MAX_LINE];
            strip_peep_comment_copy(tmp, lines[k]);
            if (strncmp(tmp, "ld d,", 5) == 0) {
                de_gap = k;
                break;
            }
            if (line_touches_de(lines[k]))
                break;
        }
        if (de_gap < 0)
            continue;

        /* Unlike pass_byte_for_counter_to_reg_e's own backward scan for the
         * same shape of line (bounded to 8 lines back, since that pass's
         * loops are typically entered directly), this loop's counter init
         * sits right after the ENCLOSING loop's own label, with the row-
         * base and other per-outer-iteration address setup in between
         * (tests/mm.c: ~38 lines from k's own "ld (ix-K),0" to the k-loop's
         * label) - so this scan is bounded much further back, relying on
         * the starts_label() stop below as the real, correct boundary (an
         * intervening label means the init is not simply upstream in the
         * same basic block, which is unprovable here and correctly
         * declines) rather than an arbitrary nearby distance. */
        found_init = 0;
        init_val = 0;
        scan_limit = i - 200;
        if (scan_limit < 0) scan_limit = 0;
        for (k = i - 1; k >= scan_limit; --k) {
            if (starts_label(lines[k]))
                break;
            if (peep_parse_ld_ix_byte_imm(lines[k], &init_off, &init_val) &&
                init_off == -k_off) {
                found_init = 1;
                break;
            }
        }
        if (!found_init)
            continue;

        /* Commit back-to-front (highest index first) so earlier indices
         * stay valid for later edits: de_gap, then match_k, then i. */
        insert_line_tagged(de_gap, "ld de,4", "walk_row_cached_float_index");
        insert_line_tagged(de_gap + 1, "add iy,de", "walk_row_cached_float_index");

        delete_n(match_k, 10);
        insert_line_tagged(match_k, "push iy", "walk_row_cached_float_index");
        insert_line_tagged(match_k + 1, "pop hl", "walk_row_cached_float_index");

        /* insert_line_tagged(i, ...) always pushes whatever is currently at
         * i - including an earlier insertion made at this same i - one
         * further down, so building up a multi-line block in the desired
         * execution order means inserting the LAST line first and working
         * backward (as pass_walk_hoisted_index_ptr's own analogous offset
         * priming above does). */
        {
            char ld_row_lo[40], ld_row_hi[40];
            sprintf(ld_row_lo, "ld l,(ix-%d)", row_off);
            sprintf(ld_row_hi, "ld h,(ix-%d)", row_off - 1);
            insert_line_tagged(i, "pop iy", "walk_row_cached_float_index");
            insert_line_tagged(i, "push hl", "walk_row_cached_float_index");
            if (init_val > 0) {
                char ld_de_off[40];
                sprintf(ld_de_off, "ld de,%d", init_val * 4);
                insert_line_tagged(i, "add hl,de", "walk_row_cached_float_index");
                insert_line_tagged(i, ld_de_off, "walk_row_cached_float_index");
            }
            insert_line_tagged(i, ld_row_hi, "walk_row_cached_float_index");
            insert_line_tagged(i, ld_row_lo, "walk_row_cached_float_index");
        }

        changed = 1;
    }

    return changed;
}

static int pass_deref_byte_cmp(void)
{
    int i;
    int changed = 0;

    for (i = 0; i + 11 < nlines; i++) {
        char lo_ix[64], hi_ix[64];
        char label[128];
        const char *cond;
        int N, M;

        /* ld l,(ix-N) — pointer lo byte, strictly negative offset */
        if (!peep_parse_ld_l_ix(lines[i], lo_ix)) continue;
        if (lo_ix[0] != '-') continue;
        N = atoi(lo_ix + 1);
        if (N < 2) continue;

        /* ld h,(ix-M) — pointer hi byte, M must equal N-1 */
        if (!peep_parse_ld_h_ix(lines[i + 1], hi_ix)) continue;
        if (hi_ix[0] != '-') continue;
        M = atoi(hi_ix + 1);
        if (M != N - 1) continue;

        /* ld l,(hl) / ld h,0 — byte dereference, zero-extend */
        if (!eq(i + 2, "ld l,(hl)")) continue;
        if (!eq(i + 3, "ld h,0"))    continue;

        /* push hl / ld l,(ix...) / ld h,0 / ex de,hl / pop hl */
        if (!eq(i + 4, "push hl"))   continue;
        if (strncmp(lines[i + 5], "ld l,(ix", 8) != 0) continue;
        if (!eq(i + 6, "ld h,0"))    continue;
        if (!eq(i + 7, "ex de,hl"))  continue;
        if (!eq(i + 8, "pop hl"))    continue;

        /* or a / sbc hl,de / jp z or jp nz */
        if (!eq(i + 9,  "or a"))      continue;
        if (!eq(i + 10, "sbc hl,de")) continue;
        if (parse_jp_z_label(lines[i + 11], label))
            cond = "z";
        else if (parse_jp_nz_label(lines[i + 11], label))
            cond = "nz";
        else
            continue;

        /* Pattern matched (12 lines). Emit 6 instructions. */
        {
            char ld_l[64], ld_h[64], cmp_l[MAX_LINE], jp_line[MAX_LINE];

            sprintf(ld_l, "ld l,(ix-%d)", N);
            sprintf(ld_h, "ld h,(ix-%d)", M);
            strcpy(cmp_l, lines[i + 5]);   /* preserve the "ld l,(ix...)" line */
            sprintf(jp_line, "jp %s, %s", cond, label);

            delete_n(i, 12);
            insert_line_tagged(i + 0, ld_l, "deref_byte_cmp");
            insert_line(i + 1, ld_h);
            insert_line(i + 2, "ld a,(hl)");
            insert_line(i + 3, cmp_l);
            insert_line(i + 4, "cp l");
            insert_line(i + 5, jp_line);

            changed = 1;
        }
    }

    return changed;
}




/*
 * pass_lvar_stubs — replace ld l,(ix-N) / ld h,(ix-N+1) with call __lv1..8.
 * pass_svar_stubs — replace ld (ix-N),l / ld (ix-N+1),h with call __sv1..6.
 *
 * Mirror of pass_larg_stubs but for local variables (negative IX offsets).
 * Stubs __lv1..__lv8 load the 1st..8th local word into HL.
 * Stubs __sv1..__sv6 store HL into the 1st..6th local word.
 * Each stub is 7 bytes; the inline pair is 6 bytes.  Break-even is 3 calls.
 */
static int pass_lvar_stubs(void)
{
    static const char * const names[] = {
        "__lv1","__lv2","__lv3","__lv4","__lv5","__lv6","__lv7","__lv8"
    };
    static char low[8][20], high[8][20];
    static int inited = 0;
    int i, k, changed;
    int used[8];

    if (!inited) {
        for (k = 0; k < 8; k++) {
            sprintf(low[k],  "ld l,(ix-%d)", (k+1)*2);
            sprintf(high[k], "ld h,(ix-%d)", (k+1)*2-1);
        }
        inited = 1;
    }

    for (k = 0; k < 8; k++) used[k] = 0;
    changed = 0;

    for (i = 0; i + 1 < nlines; i++) {
        for (k = 0; k < 8; k++) {
            if (eq(i, low[k]) && eq(i+1, high[k])) {
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

static int pass_svar_stubs(void)
{
    static const char * const names[] = {
        "__sv1","__sv2","__sv3","__sv4","__sv5","__sv6"
    };
    static char low[6][24], high[6][24];
    static int inited = 0;
    int i, k, changed;
    int used[6];

    if (!inited) {
        for (k = 0; k < 6; k++) {
            sprintf(low[k],  "ld (ix-%d),l", (k+1)*2);
            sprintf(high[k], "ld (ix-%d),h", (k+1)*2-1);
        }
        inited = 1;
    }

    for (k = 0; k < 6; k++) used[k] = 0;
    changed = 0;

    for (i = 0; i + 1 < nlines; i++) {
        for (k = 0; k < 6; k++) {
            if (eq(i, low[k]) && eq(i+1, high[k])) {
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

/*
 * pass_larg_stubs — replace ld l,(ix+N) / ld h,(ix+N+1) with call __la1/2/3.
 *
 * The shared RTL stubs are 7 bytes each; the inline pair is 6 bytes.  With
 * three or more uses of the same stub the stub cost is recovered and every
 * additional use saves 3 bytes.  Runs after pass_elim_ix_frame so that
 * the "ix" text is still visible during frame-elimination scanning.
 */
static int pass_larg_stubs(void)
{
    int i, changed;
    int used_la1 = 0, used_la2 = 0, used_la3 = 0;

    changed = 0;

    for (i = 0; i + 1 < nlines; i++) {
        if (eq(i, "ld l,(ix+4)") && eq(i+1, "ld h,(ix+5)")) {
            replace1_tagged(i, "call __la1", "larg");
            delete_n(i+1, 1);
            used_la1 = 1; changed = 1;
        } else if (eq(i, "ld l,(ix+6)") && eq(i+1, "ld h,(ix+7)")) {
            replace1_tagged(i, "call __la2", "larg");
            delete_n(i+1, 1);
            used_la2 = 1; changed = 1;
        } else if (eq(i, "ld l,(ix+8)") && eq(i+1, "ld h,(ix+9)")) {
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

/*
 * pass_phix_stub — replace push hl / push ix / pop hl with call __phix.
 *
 * The pattern saves HL on the stack and copies IX into HL (for frame-pointer
 * arithmetic).  The stub is 6 bytes; the inline sequence is 4 bytes (1 byte
 * push hl + 2 bytes push ix + 1 byte pop hl).  Break-even is 7 calls.
 * Runs after pass_shared_frame_stubs so prologue push-ix has been converted.
 */
static int pass_phix_stub(void)
{
    int i, changed;
    int used = 0;

    changed = 0;

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

/*
 * pass_larg_direct_store — fold "ld hl,ADDR / push hl / call __la[123] or __lv[1-8] /
 *   ex de,hl / pop hl / ld (hl),e / inc hl / ld (hl),d" into
 *   "call __la[123]/__lv[1-8] / ld (ADDR),hl".
 *
 * Pattern generated for C like:  global_var = argN;
 * The destination is always a fixed global address (_Z label = BSS equ),
 * so ld (ADDR),hl (Z80 direct store) is valid.
 *
 * 8 instructions / 12 bytes -> 2 instructions / 6 bytes: saves 6 bytes per site.
 */
static int pass_larg_direct_store(void)
{
    int i, changed = 0;
    char addr[MAX_LINE], newline[MAX_LINE];

    for (i = 0; i + 7 < nlines; i++) {
        char tmp[MAX_LINE];
        const char *stub;

        if (!parse_ld_hl_imm(lines[i], addr))
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

/*
 * pass_ldwl_stub — replace ld e,(hl)/inc hl/ld d,(hl)/ex de,hl with call __ldwl.
 * Dereferences a 16-bit pointer in HL into HL.  4 inline bytes -> 3.
 * Stub is 5 bytes; break-even at 5 uses.
 */
static int pass_ldwl_stub(void)
{
    int i, changed = 0, used = 0;

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

/*
 * pass_wand_stub — replace ld a,h/and d/ld h,a/ld a,l/and e/ld l,a with call __wand.
 * 16-bit HL &= DE.  6 inline bytes -> 3.  Stub is 7 bytes; break-even at 3 uses.
 */
static int pass_wand_stub(void)
{
    int i, changed = 0, used = 0;

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

/*
 * pass_icmp_stub — replace 8-byte signed 16-bit compare sequence with call __icmp.
 * ld a,h/xor 80h/ld h,a/ld a,d/xor 80h/ld d,a/or a/sbc hl,de -> call __icmp.
 * 8 inline bytes -> 3.  Stub is 9 bytes; break-even at 2 uses.
 * Runs after the main loop so pass_e_signed_le_zero and pass_signed_cmp_small_const
 * (which recognise the same sub-sequence) have already fired.
 */
static int pass_icmp_stub(void)
{
    int i, changed = 0, used = 0;

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

/*
 * pass_sxde_stub — replace ld a,h/rlca/sbc a,a/ld d,a/ld e,a with call __sxde.
 * Sign-extends HL to DEHL by producing DE = 0FFFFh if HL<0, 0 otherwise.
 * 5 inline bytes -> 3.  Stub is 6 bytes; break-even at 3 uses.
 */
static int pass_sxde_stub(void)
{
    int i, changed = 0, used = 0;

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

/*
 * pass_sxhl_stub — replace ld a,l/rlca/sbc a,a/ld h,a with call __sxhl.
 * Sign-extends 8-bit L into H so HL = (int16_t)(int8_t)L.
 * 4 inline bytes -> 3.  Stub is 5 bytes; break-even at 5 uses.
 */
static int pass_sxhl_stub(void)
{
    int i, changed = 0, used = 0;

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

/*
 * pass_a_tracks_ix_byte:
 *
 * When A is loaded from (ix+N) and the value at (ix+N) is not modified
 * before a subsequent load of the same offset into another register, replace
 * the second load with a faster register-to-register copy.
 *
 *   ld a,(ix-3)          ; A = (ix-3)  [19 T-states, 3 bytes]
 *   cp 9                 ; A unchanged
 *   jp nc, L216          ; A unchanged
 *   ld hl,_g_board       ; A unchanged
 *   ld e,(ix-3)          ; redundant memory read → ld e,a  [4 T-states, 1 byte]
 *
 * Tracking resets at labels, calls, ret, any A-writing instruction, and any
 * store to the tracked (ix+N) offset.
 */
static int pass_a_tracks_ix_byte(void)
{
    int i, changed = 0;
    int a_valid = 0;
    int a_off = 0;
    char tmp[MAX_LINE];

    for (i = 0; i < nlines; i++) {
        if (starts_label(lines[i])) {
            a_valid = 0;
            continue;
        }

        strip_peep_comment_copy(tmp, lines[i]);

        if (strncmp(tmp, "call ", 5) == 0 || strcmp(tmp, "ret") == 0) {
            a_valid = 0;
            continue;
        }

        /* ld a,(ix+N): if A already tracks the same offset, the reload is redundant */
        if (strncmp(tmp, "ld a,(ix", 8) == 0) {
            char *endp;
            long v = strtol(tmp + 8, &endp, 0);
            if (*endp == ')' && endp[1] == 0 && v >= -128 && v <= 127) {
                if (a_valid && a_off == (int)v) {
                    delete_n(i, 1);
                    changed = 1;
                    if (i > 0) i--;
                    continue;
                }
                a_valid = 1;
                a_off = (int)v;
            } else {
                a_valid = 0;
            }
            continue;
        }

        /* ld r,(ix+N) where r != a and same offset: replace with ld r,a */
        if (a_valid &&
            strncmp(tmp, "ld ", 3) == 0 &&
            tmp[4] == ',' &&
            strncmp(tmp + 5, "(ix", 3) == 0) {
            char r = tmp[3];
            if (r != 'a') {
                char *endp;
                long v = strtol(tmp + 8, &endp, 0);
                if (*endp == ')' && endp[1] == 0 && v == a_off) {
                    char newline[MAX_LINE];
                    sprintf(newline, "ld %c,a", r);
                    replace1_tagged(i, newline, "a_tracks_ix");
                    changed = 1;
                    continue;
                }
            }
        }

        /* ld (ix+N),a  → A and (ix+N) now hold the same value; establish tracking.
         * ld (ix+N),X  → if tracked slot written with non-A, invalidate.
         * Note: ld (ix+N),a with a_valid && a_off==N preserves (not clears) tracking. */
        if (strncmp(tmp, "ld (ix", 6) == 0) {
            char *endp;
            long v = strtol(tmp + 6, &endp, 0);
            if (*endp == ')' && v >= -128 && v <= 127) {
                if (endp[1] == ',' && endp[2] == 'a' && endp[3] == 0) {
                    a_valid = 1;
                    a_off = (int)v;
                } else if (a_valid && (int)v == a_off) {
                    a_valid = 0;
                }
            }
            continue;
        }

        /* Instructions that write A (cp, push, ld r/m for r!=a do not) */
        if (strncmp(tmp, "ld a,", 5) == 0 ||
            strncmp(tmp, "add a,", 6) == 0 ||
            strncmp(tmp, "adc a,", 6) == 0 ||
            strncmp(tmp, "sub ", 4) == 0 ||
            strncmp(tmp, "sbc a,", 6) == 0 ||
            strncmp(tmp, "and ", 4) == 0 ||
            (strncmp(tmp, "or ", 3) == 0 && strcmp(tmp, "or a") != 0) ||
            strncmp(tmp, "xor ", 4) == 0 ||
            strcmp(tmp, "inc a") == 0 ||
            strcmp(tmp, "dec a") == 0 ||
            strcmp(tmp, "rlca") == 0 ||
            strcmp(tmp, "rrca") == 0 ||
            strcmp(tmp, "rla") == 0 ||
            strcmp(tmp, "rra") == 0 ||
            strcmp(tmp, "daa") == 0 ||
            strcmp(tmp, "cpl") == 0 ||
            strcmp(tmp, "neg") == 0 ||
            strcmp(tmp, "pop af") == 0 ||
            strncmp(tmp, "in a,", 5) == 0) {
            a_valid = 0;
        }
    }

    return changed;
}

/*
 * pass_elim_redundant_ld_a_reg:
 *
 * Remove a "ld a,r" that is redundant because A already equals r.
 *
 * After "ld a,r", instructions that only affect flags (cp, jp) leave A
 * unchanged.  A subsequent "ld a,r" for the same r is therefore dead.
 *
 * This fires twice in the MinMax inner loop after pass_minmax_score_b_cache
 * promotes score into B.  The score-update branch pattern is:
 *
 *   ld a,b        ; score
 *   cp (ix-1)     ; compare — does not touch A
 *   jp z, L       ; conditional — does not touch A
 *   jp c, L       ; conditional — does not touch A
 *   ld a,b        ; ← redundant: A still equals B
 *   ld (ix-1),a
 *
 * The window (12 lines) is kept small and only cp/jp are treated as
 * A-transparent, so the rule is conservative and correct.
 */
static int pass_elim_redundant_ld_a_reg(void)
{
    int i, j, changed = 0;
    char tmp[MAX_LINE], tmp2[MAX_LINE];
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

        for (j = i + 1; j < nlines && j < i + 12; j++) {
            if (starts_label(lines[j]))
                break;

            strip_peep_comment_copy(tmp2, lines[j]);

            if (strcmp(tmp2, tmp) == 0) {
                delete_n(j, 1);
                changed = 1;
                break;
            }

            if (strncmp(tmp2, "cp ", 3) == 0)
                continue;
            if (strncmp(tmp2, "jp ", 3) == 0)
                continue;
            /* or a: A = A|A = A, value unchanged — transparent */
            if (strcmp(tmp2, "or a") == 0)
                continue;

            break;
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
static int pass_minmax_elim_label_reload(void)
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
 * pass_elim_c_reload_after_store:
 *
 * After "ld c,a", registers A and C hold the same value.  A subsequent
 * "ld a,c" is therefore redundant if A and C have not been modified between.
 *
 * Handles the dead-code crossing for the MinMax value-update pattern:
 *   ld c,a          ; value = score; A = C = score
 *   cp (ix+6)       ; A unchanged
 *   jp c, L227      ; conditional (taken = score < beta)
 *   ld l,a          ; success path (not taken)
 *   ld h,0
 *   jp L202         ; → enters dead code
 *   L227:           ; label after dead code → safe to cross
 *   ld a,c          ; ← redundant: A = C = score still
 */
static int pass_elim_c_reload_after_store(void)
{
    int i, j, changed = 0;
    char tmp2[MAX_LINE];
    int in_dead;

    for (i = 0; i + 1 < nlines; i++) {
        if (!eq(i, "ld c,a"))
            continue;

        in_dead = 0;
        for (j = i + 1; j < nlines && j < i + 20; j++) {
            if (starts_label(lines[j])) {
                if (in_dead) { in_dead = 0; continue; }
                break;
            }

            strip_peep_comment_copy(tmp2, lines[j]);

            if (strcmp(tmp2, "ld a,c") == 0 && !in_dead) {
                delete_n(j, 1);
                changed = 1;
                break;
            }

            if (in_dead)
                continue;

            if (strncmp(tmp2, "cp ", 3) == 0) continue;
            if (strncmp(tmp2, "jp ", 3) == 0) {
                /* Unconditional jp → dead code starts after it */
                if (strchr(tmp2 + 3, ',') == NULL)
                    in_dead = 1;
                continue;
            }
            if (strcmp(tmp2, "or a") == 0) continue;
            if (strcmp(tmp2, "ld l,a") == 0) continue;
            if (strcmp(tmp2, "ld h,a") == 0) continue;
            if (strcmp(tmp2, "ld h,0") == 0) continue;
            if (strcmp(tmp2, "ld l,0") == 0) continue;

            break;
        }
    }

    return changed;
}

/*
 * pass_and1_ix_to_bit:
 *
 * Replace "ld a,(ix+K); and 1; jp z/nz, L" with "bit 0,(ix+K); jp z/nz, L".
 *
 * "bit 0,(ix+K)" is 20T vs "ld a,(ix+K); and 1" = 19+7 = 26T: saves 6T.
 * Safe when A is dead on both targets, which holds in MinMax where the
 * depth&1 branch is always followed by "ld a,e" or similar.
 */
static int pass_and1_ix_to_bit(void)
{
    int i, changed = 0;
    const char *p;
    char kstr[32], new_bit[64], tmp[MAX_LINE];
    int ki;
    char lab[128];

    for (i = 0; i + 2 < nlines; i++) {
        strip_peep_comment_copy(tmp, lines[i]);
        if (strncmp(tmp, "ld a,(ix", 8) != 0)
            continue;
        p = tmp + 8;
        ki = 0;
        while (*p && *p != ')' && ki < 30)
            kstr[ki++] = *p++;
        kstr[ki] = 0;
        if (*p != ')' || p[1] != 0)
            continue;

        /* "ld a,(ix+K); and 1; jp z/nz" → "bit 0,(ix+K); jp z/nz" */
        if (eq(i + 1, "and 1") &&
            (parse_jp_z_label(lines[i + 2], lab) ||
             parse_jp_nz_label(lines[i + 2], lab))) {
            sprintf(new_bit, "bit 0,(ix%s)", kstr);
            replace1_tagged(i, new_bit, "and1_to_bit");
            delete_n(i + 1, 1);
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        /* "ld a,(ix+K); cp 8; jp nz, L" → "bit 3,(ix+K); jp z, L"
         * Valid for depth values 0-8: bit3 is set only at depth=8.
         * jp nz (jump if depth!=8) ≡ jp z after bit 3 (jump if bit3=0).
         * A is dead on both branch targets (next insn loads it fresh). */
        if (eq(i + 1, "cp 8") &&
            parse_jp_nz_label(lines[i + 2], lab)) {
            char newjp[MAX_LINE];
            sprintf(new_bit, "bit 3,(ix%s)", kstr);
            sprintf(newjp, "jp z, %s", lab);
            replace1_tagged(i, new_bit, "cp8_to_bit3");
            replace1(i + 1, newjp);
            delete_n(i + 2, 1);
            changed = 1;
            if (i > 0) i--;
            continue;
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
static int pass_winner_check_dec_a(void)
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
            if (!parse_ld_hl_imm(lines[i + 4], lab) &&
                (strncmp(t4, "ld l,", 5) != 0))
                continue;
        }

        replace1_tagged(i + 2, "dec a", "winner_dec_a");
        changed = 1;
    }

    return changed;
}

/*
 * pass_elim_redundant_ld_h_zero:
 *
 * Eliminate redundant "ld h,0" when H is already known to be zero.  DCC
 * zero-extends byte values to 16-bit HL with:
 *
 *   ld l,(ix-3)
 *   ld h,0
 *   push hl
 *   ld a,(ix+8)
 *   add a,1
 *   ld l,a
 *   ld h,0       ← H still 0 from above (push/ld a/add a/ld l don't touch H)
 *   push hl
 *   ld l,(ix+6)
 *   ld h,0       ← still redundant
 *   push hl
 *
 * Tracking resets at labels, calls, ret, and any H-clobbering instruction
 * (ld h/hl, pop hl, add/adc/sbc hl, inc/dec hl, ex de/sp,hl).
 */
static int pass_elim_redundant_ld_h_zero(void)
{
    int i, changed = 0;
    int h_is_zero = 0;
    char tmp[MAX_LINE];

    for (i = 0; i < nlines; i++) {
        if (starts_label(lines[i])) {
            h_is_zero = 0;
            continue;
        }

        strip_peep_comment_copy(tmp, lines[i]);

        if (strcmp(tmp, "ld h,0") == 0) {
            if (h_is_zero) {
                delete_n(i, 1);
                changed = 1;
                if (i > 0) i--;
            } else {
                h_is_zero = 1;
            }
            continue;
        }

        /* "ld hl,0" sets H=0 too, exactly like "ld h,0" does - it just
         * isn't itself a redundant/removable instruction the way a
         * standalone "ld h,0" can be (it's also setting L, still needed).
         * Recognizing that lets a *later* "ld h,0" in the same block -
         * e.g. dcc's own return-path pairing, "ld hl,0" for the 0 result
         * followed by another zero-extend on a different path that
         * happens to merge here - be caught as truly redundant instead of
         * this pass conservatively assuming "ld hl,0" clobbers H to an
         * unknown value the way any other "ld hl,<nonzero>" genuinely
         * does. */
        if (strcmp(tmp, "ld hl,0") == 0) {
            h_is_zero = 1;
            continue;
        }

        if (strncmp(tmp, "ld h,", 5) == 0 ||
            strncmp(tmp, "ld hl,", 6) == 0 ||
            strcmp(tmp, "pop hl") == 0 ||
            strncmp(tmp, "add hl,", 7) == 0 ||
            strncmp(tmp, "adc hl,", 7) == 0 ||
            strncmp(tmp, "sbc hl,", 7) == 0 ||
            strcmp(tmp, "inc hl") == 0 ||
            strcmp(tmp, "dec hl") == 0 ||
            strcmp(tmp, "ex de,hl") == 0 ||
            strcmp(tmp, "ex (sp),hl") == 0 ||
            strncmp(tmp, "call ", 5) == 0 ||
            strcmp(tmp, "ret") == 0) {
            h_is_zero = 0;
        }
    }

    return changed;
}

static int pass_global_board_const_offsets(void)
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


/* Parse an IX offset string (e.g. "+8", "-2") to an integer. */
static int parse_ix_off_numeric(const char *off, int *val)
{
    char *endp;
    if (off[0] == 0) return 0;
    *val = (int)strtol(off, &endp, 10);
    return *endp == 0;
}



/*
 * pass_cpir: Replace a byte-scan equality loop with a Z80 CPI-based loop.
 *
 * Detects the pattern produced by pass_deref_byte_cmp for loops of the form
 *   for (i = 0; i < c; i++) { if (*ptr != val) { fail-and-exit; } ptr++; }
 * where i is a local 16-bit counter, ptr is a local byte pointer, val is a
 * local byte, and c is a parameter or local count.
 *
 * NOTE: this must NOT be replaced with the auto-repeating CPIR instruction.
 * CPIR stops at the FIRST byte that matches A (or when BC reaches 0),
 * unconditionally - it has no way to keep scanning past a match. Since val
 * is the fill byte, byte 0 of a correctly-filled buffer already matches, so
 * a CPIR-based version would check exactly one byte and silently treat that
 * as "all c bytes verified" - a real, confirmed miscompile (a corruption at
 * any offset other than 0 goes completely undetected). The transformation
 * below issues one CPI per byte with explicit branches instead, which is
 * slower than (broken) CPIR but still several times faster than the
 * original multi-instruction stack-relative loop, and - unlike CPIR -
 * actually checks every byte.
 *
 * The pattern (in the peepholed output):
 *
 *   Lhead:
 *     ld l,(ix-A)  ld h,(ix-B)            ; loop counter i (B = A-1)
 *     push hl
 *     ld l,(ix+C)  ld h,(ix+D)            ; limit c (D = C+1, any sign)
 *     ex de,hl  pop hl  or a  sbc hl,de
 *     jp nc, Lexit                         ; i >= c → done
 *     ld l,(ix+P)  ld h,(ix+Q)            ; pointer ptr (Q = P+1, any sign)
 *     ld a,(hl)                            ; A = *ptr
 *     ld l,(ix+V)  cp l                   ; compare with val
 *     jp z, Lok                            ; equal → keep going
 *     [fail code containing call _exit]
 *   Lok:
 *     ld l,(ix+P)  ld h,(ix+Q)  inc hl
 *     ld (ix+P),l  ld (ix+Q),h            ; ptr++
 *     inc (ix-A)   jp nz, Lhead
 *     inc (ix-B)   jp Lhead               ; i++
 *   Lexit:
 *
 * Replaced with:
 *
 *     ld l,(ix+C)  ld h,(ix+D)            ; HL = c (count)
 *     ld a,h  or l                        ; guard: c == 0 is vacuously true
 *     jp z, Lexit
 *     push hl
 *     ld l,(ix+P)  ld h,(ix+Q)            ; HL = starting ptr
 *     pop bc                              ; BC = c
 *     ld a,(ix+V)                         ; A = byte to match
 *   Lhead:                                ; reuses the original loop-head label
 *     cpi                                 ; A-(HL); HL++; BC--; Z set if matched
 *     jp nz, Lmis                         ; mismatch → go fix up ptr, then fail
 *     jp pe, Lhead                        ; BC != 0 → more bytes to check
 *     jp Lexit                            ; BC == 0, last byte matched → success
 *   Lmis:
 *     dec hl                              ; HL now addresses the mismatched byte
 *     ld (ix+P),l  ld (ix+Q),h            ; write back so the fail code's own
 *                                         ; re-read of ptr reports the right byte
 *     [original fail code falls through]
 *   Lexit:
 *
 * Requirement: i must be initialised to 0 before Lhead (verified by finding
 * "ld de,-A" in the pre-loop code, which DCC emits to address the counter).
 */
static int pass_cpir(void)
{
    int i, j, k, ip;
    int changed = 0;

    for (i = 0; i + 40 < nlines; i++) {
        char lhead[128], lexit[128], lok[128], tmp[128];
        int cnt_lo, cnt_hi;
        char lim_lo_off[32], lim_hi_off[32];
        char ptr_lo_off[32], ptr_hi_off[32];
        char val_off[32];
        int lim_lo_val, ptr_lo_val, val_val;
        int lok_pos, fail_has_exit;
        int fail_start;
        char inc_cnt_lo[32], inc_cnt_hi[32];
        char store_ptr_lo[64], store_ptr_hi[64];

        /* 1. Loop header label */
        if (!label_name_at(i, lhead)) continue;
        j = i + 1;

        /* 2. Loop condition: counter in HL, limit in DE, then sbc+jp.
         * Accept original push/load/ex/pop form, or the ix_pair_load_to_de
         * form (ld e,(ix+N); ld d,(ix+N+1)) if that pass ran first. */
        if (!stride_parse_ld_r_ix_neg(lines[j], 'l', &cnt_lo)) continue; j++;
        if (!stride_parse_ld_r_ix_neg(lines[j], 'h', &cnt_hi)) continue; j++;
        if (cnt_hi != cnt_lo - 1) continue;
        if (eq(j, "push hl")) {
            j++;
            if (!peep_parse_ld_l_ix(lines[j], lim_lo_off)) continue; j++;
            if (!peep_parse_ld_h_ix(lines[j], lim_hi_off)) continue; j++;
            if (!parse_ix_off_numeric(lim_lo_off, &lim_lo_val)) continue;
            { int v; if (!parse_ix_off_numeric(lim_hi_off, &v)) continue;
              if (v != lim_lo_val + 1) continue; }
            if (!eq(j, "ex de,hl")) continue; j++;
            if (!eq(j, "pop hl"))   continue; j++;
        } else {
            /* ix_pair_load_to_de form: ld e,(ix+N); ld d,(ix+N+1) */
            if (!peep_parse_ld_e_ix(lines[j], lim_lo_off)) continue; j++;
            if (!peep_parse_ld_d_ix(lines[j], lim_hi_off)) continue; j++;
            if (!parse_ix_off_numeric(lim_lo_off, &lim_lo_val)) continue;
            { int v; if (!parse_ix_off_numeric(lim_hi_off, &v)) continue;
              if (v != lim_lo_val + 1) continue; }
        }
        if (!eq(j, "or a"))      continue; j++;
        if (!eq(j, "sbc hl,de")) continue; j++;
        if (!parse_jp_nc_label(lines[j], lexit)) continue; j++;

        /* 3. Byte deref and compare. Two shapes reach here: the classic
         * "ld l,(ix+V); cp l" (6 lines total), or dcc_cmp.c's byte-operand
         * kind-4 fast path (ast_byte_operand/emit_cp_byte_operand), which
         * compares directly against the ix-relative memory operand without
         * first loading it into L - "cp (ix+V)" (5 lines total). */
        if (!peep_parse_ld_l_ix(lines[j], ptr_lo_off)) continue; j++;
        if (!peep_parse_ld_h_ix(lines[j], ptr_hi_off)) continue; j++;
        if (!parse_ix_off_numeric(ptr_lo_off, &ptr_lo_val)) continue;
        { int v; if (!parse_ix_off_numeric(ptr_hi_off, &v)) continue;
          if (v != ptr_lo_val + 1) continue; }
        if (!eq(j, "ld a,(hl)")) continue; j++;
        {
            char cptmp[MAX_LINE];
            strip_peep_comment_copy(cptmp, lines[j]);
            if (strncmp(cptmp, "cp (ix", 6) == 0) {
                const char *p2 = cptmp + 6;
                int oi = 0;
                while (*p2 && *p2 != ')' && oi < 31)
                    val_off[oi++] = *p2++;
                val_off[oi] = 0;
                if (*p2 != ')' || p2[1] != 0) continue;
                if (!parse_ix_off_numeric(val_off, &val_val)) continue;
                j++;
            } else {
                if (!peep_parse_ld_l_ix(lines[j], val_off)) continue; j++;
                if (!parse_ix_off_numeric(val_off, &val_val)) continue;
                if (!eq(j, "cp l")) continue; j++;
            }
        }
        if (!parse_jp_z_label(lines[j], lok)) continue; j++;
        fail_start = j;  /* first line of fail code */

        /* Reject if counter, pointer, val, or limit share IX slots */
        if (-cnt_lo == ptr_lo_val) continue;
        if (-cnt_lo == val_val)    continue;
        if (ptr_lo_val == val_val) continue;
        if (-cnt_lo == lim_lo_val) continue;

        /* 4. Scan fail code for call _exit and Lok label */
        fail_has_exit = 0;
        lok_pos = -1;
        for (k = j; k < nlines && k < j + 60; k++) {
            if (line_is_label_name(k, lok))  { lok_pos = k; break; }
            if (eq(k, "call _exit"))          fail_has_exit = 1;
            if (is_global_asm_label_line(k))  break;
        }
        if (lok_pos < 0 || !fail_has_exit) continue;

        /* 5. After Lok: ptr++ (5 lines) */
        k = lok_pos + 1;
        { char lo2[32], hi2[32];
          if (!peep_parse_ld_l_ix(lines[k], lo2) || strcmp(lo2, ptr_lo_off)) continue; k++;
          if (!peep_parse_ld_h_ix(lines[k], hi2) || strcmp(hi2, ptr_hi_off)) continue; k++; }
        if (!eq(k, "inc hl")) continue; k++;
        sprintf(store_ptr_lo, "ld (ix%s),l", ptr_lo_off);
        sprintf(store_ptr_hi, "ld (ix%s),h", ptr_hi_off);
        if (!eq(k, store_ptr_lo)) continue; k++;
        if (!eq(k, store_ptr_hi)) continue; k++;

        /* 6. Counter increment (4 lines): inc(ix-A); jp nz,Lhead; inc(ix-B); jp Lhead */
        sprintf(inc_cnt_lo, "inc (ix-%d)", cnt_lo);
        sprintf(inc_cnt_hi, "inc (ix-%d)", cnt_hi);
        if (!eq(k, inc_cnt_lo)) continue; k++;
        if (!parse_jp_nz_label(lines[k], tmp) || strcmp(tmp, lhead)) continue; k++;
        if (!eq(k, inc_cnt_hi)) continue; k++;
        if (!peep_parse_jp_uncond_label(lines[k], tmp) || strcmp(tmp, lhead)) continue;
        ip = k; k++;

        /* 7. Lexit label must follow */
        if (!line_is_label_name(k, lexit)) continue;

        /* 8. Counter must start at 0: look back up to 20 lines for evidence
         * of zero-initialization. DCC used to always compute the counter's
         * frame address via "ld de,-A / add hl,de" as part of storing its
         * initializer, leaving a distinctive "ld de,-A" text marker to grep
         * for. The ix-direct declaration-initializer fast path (dcc_decl.c)
         * skips that address computation entirely, so a zero-initialized
         * counter can now also appear as
         *   ld hl,0 / ld (ix-A),l / ld (ix-B),h          (expression path)
         * or
         *   ld (ix-A),0 / ld (ix-B),0                    (immediate-const path)
         * with no "ld de,-A" anywhere. Recognizing only the first shape
         * silently stopped this whole pass from ever firing again on a
         * loop whose counter takes either of the newer, faster
         * initialization shapes - a real regression this project hit once
         * already (tm.c/ttt.c both use `for (size_t i = 0; ...)`). */
        { char de_init[32], ix_lo0[32], ix_hi0[32], ix_lo_l[32], ix_hi_h[32];
          int found = 0;
          sprintf(de_init, "ld de,-%d", cnt_lo);
          sprintf(ix_lo0, "ld (ix-%d),0", cnt_lo);
          sprintf(ix_hi0, "ld (ix-%d),0", cnt_hi);
          sprintf(ix_lo_l, "ld (ix-%d),l", cnt_lo);
          sprintf(ix_hi_h, "ld (ix-%d),h", cnt_hi);
          for (k = i - 1; k >= 0 && k >= i - 20; k--) {
              if (eq(k, de_init)) { found = 1; break; }
              if (eq(k, ix_lo0) && eq(k + 1, ix_hi0)) { found = 1; break; }
              if (eq(k, "ld hl,0") && eq(k + 1, ix_lo_l) && eq(k + 2, ix_hi_h)) {
                  found = 1; break;
              }
              if (is_global_asm_label_line(k)) break;
          }
          if (!found) continue; }

        /* All checks passed — apply the CPI-loop transformation (see the
         * header comment for why this must not be a single CPIR). */
        {
            static int mis_counter = 0;
            char s_lim_lo[160], s_lim_hi[160], s_ptr_lo[160], s_ptr_hi[160];
            char s_val[160], s_jp_z_exit[160], s_lhead[160];
            char s_jp_nz_mis[160], s_jp_pe_lhead[160], s_mis_label[160];
            char s_store_ptr_lo[160], s_store_ptr_hi[160];
            char mis[32];

            sprintf(s_lim_lo,      "ld l,(ix%s)", lim_lo_off);
            sprintf(s_lim_hi,      "ld h,(ix%s)", lim_hi_off);
            sprintf(s_ptr_lo,      "ld l,(ix%s)", ptr_lo_off);
            sprintf(s_ptr_hi,      "ld h,(ix%s)", ptr_hi_off);
            sprintf(s_val,         "ld a,(ix%s)", val_off);
            sprintf(s_jp_z_exit,   "jp z, %s", lexit);
            sprintf(s_lhead,       "%s:", lhead);
            sprintf(mis,           "PCM%d", mis_counter++);
            sprintf(s_jp_nz_mis,   "jp nz, %s", mis);
            sprintf(s_jp_pe_lhead, "jp pe, %s", lhead);
            sprintf(s_mis_label,   "%s:", mis);
            sprintf(s_store_ptr_lo,"ld (ix%s),l", ptr_lo_off);
            sprintf(s_store_ptr_hi,"ld (ix%s),h", ptr_hi_off);

            /* Delete end block first (lok label + ptr++ + counter++) so that
             * positions i..fail_start-1 are unchanged. */
            delete_n(lok_pos, ip - lok_pos + 1);

            /* Delete head block (L4 label + condition + deref + jp z,Lok). */
            delete_n(i, fail_start - i);

            /* Insert the CPI-loop at i (now the first line of the fail code).
             * Lhead is reintroduced as the loop-back target (its original
             * definition was just deleted above, freeing the name). */
            insert_line_tagged(i,      s_lim_lo,    "cpiloop");
            insert_line(i +  1,        s_lim_hi);
            insert_line(i +  2,        "ld a,h");
            insert_line(i +  3,        "or l");
            insert_line(i +  4,        s_jp_z_exit);    /* zero count: vacuously true */
            insert_line(i +  5,        "push hl");
            insert_line(i +  6,        s_ptr_lo);
            insert_line(i +  7,        s_ptr_hi);
            insert_line(i +  8,        "pop bc");
            insert_line(i +  9,        s_val);
            insert_line(i + 10,        s_lhead);
            insert_line(i + 11,        "cpi");
            insert_line(i + 12,        s_jp_nz_mis);    /* mismatch: fix up ptr, then fail */
            insert_line(i + 13,        s_jp_pe_lhead);  /* more bytes remain: loop */
            insert_line(i + 14,        s_jp_z_exit);    /* BC==0 and last byte matched: success */
            insert_line(i + 15,        s_mis_label);
            insert_line(i + 16,        "dec hl");
            insert_line(i + 17,        s_store_ptr_lo);
            insert_line(i + 18,        s_store_ptr_hi);
            /* original fail code falls through unchanged from here */

            changed = 1;
        }
    }

    return changed;
}


/*
 * Replace an unconditional jump to a label whose body is just RET with RET.
 *
 * DCC commonly emits byte-return helpers as:
 *     ld l,b
 *     jp Lret
 *   Lret:
 *     ret
 *
 * If the target really is a plain return label, the jump has no semantic
 * purpose.  This pass deliberately does not fire for framed epilogues such as
 * "ld sp,ix / pop ix / ret"; those labels are not immediately followed by ret.
 */
static int pass_jp_to_plain_ret(void)
{
    int i;
    int k;
    int changed;
    char lab[128];
    char def[160];

    changed = 0;

    for (i = 0; i < nlines; ++i) {
        char tmp[MAX_LINE];

        strip_peep_comment_copy(tmp, lines[i]);
        if (!peep_parse_jp_uncond_label(tmp, lab))
            continue;

        sprintf(def, "%s:", lab);
        for (k = 0; k + 1 < nlines; ++k) {
            if (strcmp(lines[k], def) == 0)
                break;
        }
        if (k + 1 >= nlines)
            continue;

        strip_peep_comment_copy(tmp, lines[k + 1]);
        if (strcmp(tmp, "ret") != 0)
            continue;

        replace1_tagged(i, "ret", "jp_to_plain_ret");
        changed = 1;
    }

    return changed;
}

/* ------------------------------------------------------------------------- *
 * jp -> jr relaxation.
 *
 * The compiler only ever emits 3-byte absolute jumps (jp).  Where the target
 * is within the signed 8-bit relative range, a 2-byte jr is both smaller and
 * (on the not-taken path) faster.  This pass converts
 *
 *     jp LABEL            -> jr LABEL
 *     jp z,LABEL          -> jr z,LABEL      (and nz / c / nc)
 *
 * It is deliberately conservative:
 *   - Only the four conditions jr can encode (z, nz, c, nc) plus the
 *     unconditional form are touched.  `jp m,` (no jr form) and the indirect
 *     `jp (hl)` are left alone (their targets are not plain labels).
 *   - Byte addresses are modelled with an *upper bound* per line (see
 *     instr_size_upper).  Over-estimating sizes can only make the pass decide
 *     a branch is out of range when it is actually in range; it can never
 *     turn a truly-out-of-range branch into an emitted jr.  So every jr this
 *     pass produces is guaranteed assemblable by M80.
 *   - A fixpoint loop re-runs the scan: shrinking one jp to jr reduces
 *     addresses, which may bring further branches into range.  Shrinking never
 *     grows anything, so the loop is monotonic and terminates.
 * ------------------------------------------------------------------------- */

/* Upper bound on the encoded byte size of one (already-trimmed) line. */
static int instr_size_upper(const char *s)
{
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

/* If line i is a jp that jr can encode, copy its label to out and return the
 * length of the mnemonic+condition prefix kept before the label
 * (3 for "jp ", or the comma form length).  Returns 0 if not convertible. */
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

/* Rewrite a convertible jp at line i into the equivalent jr. */
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

static int pass_jp_to_jr(void)
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
            pc += instr_size_upper(lines[i]);
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

/* ------------------------------------------------------------------------- *
 * Replace a redundant DE reload with register copies from HL.
 *
 *     ld l,(ix+N)          ld l,(ix+N)
 *     ld h,(ix+N+1)   ->   ld h,(ix+N+1)
 *     ld e,(ix+N)          ld e,l
 *     ld d,(ix+N+1)        ld d,h
 *
 * When DE is loaded from exactly the same frame slot that HL was just loaded
 * from (the canonical "x + x" / "x op x" shape, e.g. sieve's i + i), the two
 * 3-byte indexed loads become two 1-byte register copies.  DE ends up holding
 * the identical value, so this is safe no matter how DE is used afterwards
 * (unlike folding straight to add hl,hl, which would require DE to be dead).
 * Saves 4 bytes and two (ix+d) memory accesses per occurrence.
 * ------------------------------------------------------------------------- */
static int pass_dup_ix_load_to_reg_copy(void)
{
    int i;
    int changed = 0;

    for (i = 0; i + 3 < nlines; ++i) {
        int hl_off, de_off;

        if (!peep_parse_ld_ix_pair(lines[i], lines[i + 1], &hl_off))
            continue;
        if (!peep_parse_ld_ix_pair(lines[i + 2], lines[i + 3], &de_off)) {
            /* lines[i+2]/[i+3] are ld e,(ix)/ld d,(ix), not ld l/ld h, so the
             * generic pair parser does not match; check the e/d forms. */
            char eoff[32], doff[32];
            char *endp;
            int elo, dhi;

            if (!peep_parse_ld_e_ix(lines[i + 2], eoff))
                continue;
            if (!peep_parse_ld_d_ix(lines[i + 3], doff))
                continue;
            elo = (int)strtol(eoff, &endp, 10);
            if (*endp != 0)
                continue;
            dhi = (int)strtol(doff, &endp, 10);
            if (*endp != 0)
                continue;
            if (dhi != elo + 1)
                continue;
            if (elo != hl_off)
                continue;   /* DE must come from the same slot as HL */

            replace1_tagged(i + 2, "ld e,l", "dup_ix_load_reg_copy");
            replace1(i + 3, "ld d,h");
            changed = 1;
        }
    }

    return changed;
}

/* ------------------------------------------------------------------------- *
 * Fold the runtime sign-extension of a 16-bit CONSTANT into a direct load.
 *
 *     ld hl,CONST          ld hl,CONST
 *     ld a,h          ->   ld de,0       (when CONST's bit 15 is clear)
 *     rlca                 ld de,65535   (when CONST's bit 15 is set)
 *     sbc a,a
 *     ld d,a
 *     ld e,a
 *
 * The five-instruction tail computes DE = (HL < 0) ? 0FFFFh : 0 and destroys
 * A.  When HL was just loaded with a compile-time *numeric* constant the
 * result is known, so we emit it directly: 5 bytes -> 3 bytes, and A is left
 * intact (strictly safer than the original, which clobbered it).  Only plain
 * decimal constants are folded; symbol/address loads (ld hl,_x) have unknown
 * high bits and are skipped.
 * ------------------------------------------------------------------------- */

/* Parse a pure decimal (optionally negative) "ld hl,N" immediate.  Returns 1
 * and stores the 16-bit-reduced value's high bit (1 = set) when the operand is
 * a bare integer; returns 0 for symbols or any non-decimal operand. */
static int ld_hl_const_high_bit_set(const char *s, int *bit15)
{
    char val[MAX_LINE];
    const char *p;
    long n;
    int neg = 0;

    if (!parse_ld_hl_imm(s, val))
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

static int pass_fold_const_sign_extend(void)
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

/* ------------------------------------------------------------------------- *
 * Dead 16-bit register reload elimination.
 *
 * Two adjacent full-width loads to the same 16-bit register pair, e.g.
 *
 *     ld hl,0
 *     ld hl,_flags
 *
 * make the first load dead: the second overwrites the whole pair and none of
 * the `ld rr,nn` / `ld rr,(nn)` forms the compiler emits read the old value.
 * Such leftovers are produced by other rewrites (e.g. the ldir_memset idiom).
 * Only hl/de/bc are considered; the loads must be exactly adjacent so nothing
 * can read the register in between.
 * ------------------------------------------------------------------------- */

/* If s is "ld RR,..." with RR one of hl/de/bc, write RR to out[3] and ret 1. */
static int parse_ld_reg16_dest(const char *s, char *out)
{
    const char *p;

    if (strncmp(s, "ld ", 3) != 0)
        return 0;
    p = s + 3;
    while (*p == ' ' || *p == '\t')
        p++;
    if (((p[0] == 'h' && p[1] == 'l') ||
         (p[0] == 'd' && p[1] == 'e') ||
         (p[0] == 'b' && p[1] == 'c')) &&
        p[2] == ',') {
        out[0] = p[0];
        out[1] = p[1];
        out[2] = 0;
        return 1;
    }
    return 0;
}

static int pass_elim_dead_reg16_reload(void)
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

int main(int argc, char **argv)
{
    int changed;
    int passes;
    const char *infile = NULL;
    const char *outfile = NULL;
    int ai;

    for (ai = 1; ai < argc; ++ai) {
        if (strcmp(argv[ai], "-Os") == 0) {
            opt_size = 1;
        } else if (strcmp(argv[ai], "-Ot") == 0) {
            opt_size = 0;
        } else if (strcmp(argv[ai], "-fundocumented-z80") == 0) {
            allow_undocumented_z80 = 1;
        } else if (infile == NULL) {
            infile = argv[ai];
        } else if (outfile == NULL) {
            outfile = argv[ai];
        } else {
            infile = NULL;
            break;
        }
    }
    if (infile == NULL || outfile == NULL) {
        fprintf(stderr,
                "usage: dccpeep [-Ot|-Os] [-fundocumented-z80] input.mac output.mac\n");
        return 1;
    }

    read_file(infile);

    /* Needed by both pass_byte_loop_counter_to_reg_iyl (undocumented-Z80
     * only, gated below) and pass_walk_row_cached_float_index (always on -
     * it uses only standard, documented IY opcodes) - either way, a call to
     * another function in this same file that itself gets a loop promoted
     * to IY would silently stomp this one's live value if that collision
     * were not checked; see scan_local_func_labels's own comment for the
     * tests/too.c regression this exact check exists to prevent. */
    scan_local_func_labels();

    passes = 0;
    do {
        changed = 0;
        if (pass_once()) changed = 1;
        if (pass_byte_minmax_patterns()) changed = 1;
        if (pass_dead_hl_load_before_ldhl()) changed = 1;
        if (pass_word_load_push_de_call()) changed = 1;
        if (pass_long_load_push_no_ex_call()) changed = 1;
        if (pass_elim_loop_back_signed_bias()) changed = 1;
        if (pass_cp_zero_to_or_a()) changed = 1;
        if (pass_hl_cmp_zero_to_or_hl()) changed = 1;
        if (pass_signed_cmp_const_low0()) changed = 1;
        if (pass_zeroext_byte_cmp_const()) changed = 1;
        if (pass_byte_cmp_push_pop_hl()) changed = 1;
        if (pass_call_hl_stack_roundtrip()) changed = 1;
        if (pass_minmax_winner_result_no_temp()) changed = 1;
        if (pass_minmax_score_b_cache()) changed = 1;
        if (pass_minmax_save_board_addr()) changed = 1;
        if (pass_elim_redundant_ld_a_reg()) changed = 1;
        if (pass_minmax_elim_label_reload()) changed = 1;
        if (pass_elim_c_reload_after_store()) changed = 1;
        if (pass_and1_ix_to_bit()) changed = 1;
        if (pass_winner_check_dec_a()) changed = 1;
        if (pass_shrink_minmax_frame3_after_score_cache()) changed = 1;
        if (pass_minmax_loop_ctr_b()) changed = 1;
        if (pass_shrink_minmax_frame2_after_loop_ctr_b()) changed = 1;
        if (pass_minmax_value_c()) changed = 1;
        if (pass_minmax_board_ptr_loop()) changed = 1;
        if (pass_minmax_byte_returns()) changed = 1;
        if (pass_minmax_pack_frame()) changed = 1;
        if (pass_minmax_pack_call()) changed = 1;
        if (pass_store_l_reload_a()) changed = 1;
        if (pass_reuse_board_addr_for_zero_store()) changed = 1;
        if (pass_array_base_push_to_de()) changed = 1;
        if (pass_base_index_addr()) changed = 1;
        if (pass_e_signed_le_zero()) changed = 1;
        if (pass_ix_array_word_addr()) changed = 1;
        if (pass_ix_array_byte_addr()) changed = 1;
        if (pass_byte_loop_counter_to_reg_c()) changed = 1;
        if (pass_byte_for_counter_to_reg_c()) changed = 1;
        if (pass_byte_for_counter_to_reg_e()) changed = 1;
        if (pass_store_word_const_hl()) changed = 1;
        if (pass_float_zero_store()) changed = 1;
        if (pass_remove_unreferenced_labels()) changed = 1;
        if (pass_ldir_memset_rotated()) changed = 1;
        if (pass_reuse_sbc_result_for_flagcheck_rotated()) changed = 1;
        if (pass_cond_skip_shortcut()) changed = 1;
        if (pass_stride_loop_to_ptr()) changed = 1;
        if (pass_ix_frame_ptr_load()) changed = 1;
        if (pass_ix_frame_ptr_load_deadd()) changed = 1;
        if (pass_hoist_index_ptr_to_bc()) changed = 1;
        if (pass_walk_hoisted_index_ptr()) changed = 1;
        if (pass_walk_row_cached_float_index()) changed = 1;
        if (pass_global_ptr_word_predec_load()) changed = 1;
        if (pass_elim_ex_de_hl_before_ix_store()) changed = 1;
        if (pass_elim_redundant_pop_push()) changed = 1;
        if (pass_double_de_before_add()) changed = 1;
        if (pass_const_hl_doubles()) changed = 1;
        if (pass_deref_byte_cmp()) changed = 1;
        if (pass_cpir()) changed = 1;
        if (pass_byte_global_ptr_array_addr()) changed = 1;
        if (pass_byte_ix_predec_zero_test()) changed = 1;
        /* Deliberately placed after the specialized array-addressing passes
         * above (pass_byte_global_ptr_array_addr in particular): both of
         * these IYL-promotion passes match a plain "ld e,(ix+off)"/"ld
         * l,(ix+off)" read, which is also part of several of those passes'
         * own, more specific (and cheaper, since they need no IY prefix
         * tax) fused address-computation patterns. Running first would let
         * IYL promotion win a pattern an existing, already-optimal peephole
         * would otherwise have claimed - measured as a real (if tiny)
         * regression on attnc99 before this ordering fix. */
        if (allow_undocumented_z80) {
            if (pass_byte_loop_counter_to_reg_iyl()) changed = 1;
            if (pass_byte_incr_loop_counter_to_reg_iyl()) changed = 1;
        }
        if (pass_ix_pair_load_to_de()) changed = 1;
        if (pass_bc_pair_load_to_de()) changed = 1;
        if (pass_ix_byte_load_to_de()) changed = 1;
        if (pass_remove_ix_store_reload_hl()) changed = 1;
        if (pass_inline_temp_spill_to_stack()) changed = 1;
        if (pass_remove_inline_temp_markers()) changed = 1;
        if (pass_postinc_ix_word()) changed = 1;
        if (pass_cp_jz_jpnc()) changed = 1;
        if (pass_cp_jz_jpc()) changed = 1;
        if (pass_bool_from_cmp()) changed = 1;
        if (pass_elim_dead_ix_stores()) changed = 1;
        if (pass_ix_addr_byte_store_imm()) changed = 1;
        if (pass_remove_ix_store_reload_a()) changed = 1;
        if (pass_a_tracks_ix_byte()) changed = 1;
        if (pass_elim_redundant_ld_h_zero()) changed = 1;
        if (pass_elim_long_store_reload()) changed = 1;
        if (pass_skip_ix_reload_across_label()) changed = 1;
        if (pass_branch_over_jump()) changed = 1;
        if (pass_jump_thread()) changed = 1;
        if (pass_global_board_const_offsets()) changed = 1;
        if (pass_posfunc_b_cache()) changed = 1;
        if (pass_jp_to_plain_ret()) changed = 1;
        if (pass_const_divmod_helpers()) changed = 1;
        if (pass_mulu_const()) changed = 1;
        if (pass_cache_noix_byte_param_reload()) changed = 1;
        if (pass_cache_global_word_reload()) changed = 1;
        if (pass_word_loop_var_to_reg_bc()) changed = 1;
        if (pass_byte_loop_var_to_reg_c()) changed = 1;
/*        if (pass_replace_tstr_fake_strstr()) changed = 1; */
        if (pass_labels()) changed = 1;
        passes++;
    } while (changed && passes < 30);

    /* General signed-compare constant-bias fold runs once after the main loop
     * converges.  It rewrites a signed 16-bit compare against a constant
     * (ld de,CONST + a 6-instruction xor-80h bias) into the already-biased
     * immediate plus a 3-instruction bias, deleting the constant's runtime
     * bias.  It MUST run after convergence: structural passes such as
     * pass_ldir_memset and pass_stride_loop_to_ptr recognise loops by their
     * canonical biased-compare shape, so folding earlier would hide those
     * loops and block far larger wins.  The fold is purely local (no control
     * flow change), so a single pass suffices; pass_labels tidies up.
     *
     * Time-mode only: under -Os the opt_size stub passes below factor the
     * whole 6-line bias sequence into a shared "call" stub, which is smaller
     * than this inline fold.  Folding here would defeat that, so restrict the
     * fold to -Ot where trading shared code size for fewer inline instructions
     * is the goal. */
    if (!opt_size && pass_signed_cmp_const_bias_fold())
        pass_labels();
    if (!opt_size && pass_signed_zero_branch())
        pass_labels();

    /* Run frame elimination after all other passes have converged, then
     * clean up any newly unreferenced labels created by the removal.
     *
     * Important: pass_jp_to_plain_ret() must also run after frame elimination.
     * Before this point, a return label in a frameless helper may still look like:
     *
     *     Lret:
     *         ld sp,ix
     *         pop ix
     *         ret
     *
     * so the earlier main-loop pass correctly refuses to replace jp Lret with
     * ret.  pass_elim_ix_frame() can then collapse that label to a plain ret,
     * creating exactly the pattern jp_to_plain_ret is meant to remove. */
    if (pass_elim_ix_frame()) {
        pass_jp_to_plain_ret();
        pass_labels();
    }

    /* Convert remaining framed prologues/epilogues to shared stub calls.
     * Runs after frame elimination so only functions that genuinely need IX
     * are transformed.  A follow-up branch/label pass collapses any return
     * labels that now just contain "jp __lve" into direct jumps. */
    if (opt_size && pass_shared_frame_stubs()) {
        pass_branch_over_jump();
        pass_labels();
    }

    /* Load-arg stubs and frame-pointer copy stub run last: they remove "ix"
     * text from lines, so they must not run before pass_elim_ix_frame (which
     * uses that text to detect live frame usage). */
    if (opt_size) {
        pass_larg_stubs();
        pass_phix_stub();
        pass_lvar_stubs();
        pass_svar_stubs();
        /* Generic sequence stubs: run after all other passes so that more
         * specific transforms (pass_e_signed_le_zero, pass_signed_cmp_small_const,
         * pass_once "and1_bool", etc.) have already fired on sub-patterns.
         *
         * Perf/size tradeoffs measured on lzpack with lzcost_t change applied
         * (baseline: 31360 bytes, 3017M cycles on wumpus.com).
         * Each stub adds 27 T-states call/ret overhead per dynamically executed site.
         *
         *   pass_icmp_stub:  -251 bytes, +1.8% perf (52 sites, hot: ~86k exec/site)
         *   pass_sxde_stub:  -130 bytes, ~0%   perf (68 sites, cold)
         *   pass_sxhl_stub:  - 46 bytes, ~0%   perf (51 sites, cold)
         *   pass_wand_stub:  -200 bytes, +5%   perf (69 sites, hot: ~85k exec/site)
         *   pass_ldwl_stub:  -231 bytes, +8%   perf (236 sites, warm: ~40k exec/site)
         */
        /* Fold "ld hl,ADDR / push hl / call __laX/__lvX / ex de,hl / pop hl / store"
         * into "call __laX/__lvX / ld (ADDR),hl" using Z80 direct-store.
         * Must run after larg/lvar stubs have produced the "call __laX" form.
         * Saves 6 bytes per site, no perf cost.  ~10 sites on lzpack. */
        pass_larg_direct_store();
        pass_icmp_stub();
        pass_sxde_stub();
        pass_sxhl_stub();
        /* Enable for more size at some perf cost: */
#if 1
        pass_wand_stub();    /* -200 bytes, +5% perf */
        pass_ldwl_stub();    /* -231 bytes, +8% perf */
#endif
    }

    pass_fix_divmod_extrns();
    pass_fix_mulu_extrn();

    /* Final cleanup: drop dead 16-bit reloads, then relax in-range absolute
     * jumps to relative jumps.  Both run after every structural pass so they
     * only tidy the settled instruction stream; dead-load removal first since
     * it shrinks code and can bring more branches into jr range. */
    pass_dup_ix_load_to_reg_copy();
    pass_fold_const_sign_extend();
    pass_elim_dead_reg16_reload();
    pass_jp_to_jr();

    write_file(outfile);
    return 0;
}
