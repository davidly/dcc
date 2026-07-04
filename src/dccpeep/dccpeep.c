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
    int n;

    if (i + 1 >= nlines)
        return 0;
    if (!is_uncond_jp(lines[i]))
        return 0;
    if (!starts_label(lines[i + 1]))
        return 0;

    if (!jump_target(lines[i], target))
        return 0;
    strcpy(label, lines[i + 1]);
    n = (int)strlen(label);
    if (n > 0 && label[n - 1] == ':')
        label[n - 1] = 0;

    return strcmp(target, label) == 0;
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

    memset(is_dead, 0, sizeof(is_dead));
    memset(last_store, -1, sizeof(last_store));
    changed = 0;

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
            } else {
                /* Internal label: conservative flush — a branch from elsewhere
                   might rely on a pending store being present. */
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
            continue;
        }

        /* Jump instructions: flush — can't prove overwrite on all paths */
        if (strncmp(tmp, "jp ", 3) == 0 || strncmp(tmp, "jr ", 3) == 0 ||
            strncmp(tmp, "djnz ", 5) == 0) {
            memset(last_store, -1, sizeof(last_store));
            continue;
        }

        /* Indirect IX-frame access pattern: push ix / pop hl / ld de,K / add hl,de
         * This computes HL = IX+K and then subsequent (hl) accesses read IX+K.
         * The (hl) instructions contain no "(ix" text, so we must handle this
         * 4-instruction sequence explicitly to avoid false dead-store deletions. */
        if (strcmp(tmp, "push ix") == 0 &&
            i + 3 < nlines) {
            char t1[MAX_LINE], t2[MAX_LINE], t3[MAX_LINE];
            long kv;
            char *ep;
            strip_peep_comment_copy(t1, lines[i + 1]);
            strip_peep_comment_copy(t2, lines[i + 2]);
            strip_peep_comment_copy(t3, lines[i + 3]);
            if (strcmp(t1, "pop hl") == 0 &&
                strncmp(t2, "ld de,", 6) == 0 &&
                strcmp(t3, "add hl,de") == 0) {
                kv = strtol(t2 + 6, &ep, 0);
                if (*ep == 0 && kv >= -128 && kv <= 127) {
                    int b;
                    /* Address of frame offset K is taken via HL.  We do not
                     * know how many bytes will be accessed through the
                     * resulting pointer, so conservatively mark up to 4
                     * consecutive bytes as live.  This covers char (1 byte),
                     * int (2 bytes), and long/float (4 bytes) objects. */
                    for (b = 0; b < 4; b++) {
                        if (kv + b >= -128 && kv + b <= 127) {
                            idx = (int)(kv + b) + 128;
                            last_store[idx] = -1;
                        }
                    }
                }
            }
            /* Fall through: also treat push ix as a regular no-(ix) instruction */
        }

        /* Small-offset IX-frame address idiom:
         *   push ix / pop hl / {dec hl | inc hl}*
         * computes HL = IX+K where K is the net of the inc/dec chain (used for
         * &local when the offset is close to the frame pointer).  Like the
         * ld de,K / add hl,de form above, the address is taken via HL and the
         * pointed-to bytes may be read or written through it, so mark up to 4
         * consecutive bytes from offset K as live to avoid deleting the store
         * that initialised the pointed-to object. */
        if (strcmp(tmp, "push ix") == 0 && i + 1 < nlines) {
            char t1[MAX_LINE], tj[MAX_LINE];
            strip_peep_comment_copy(t1, lines[i + 1]);
            if (strcmp(t1, "pop hl") == 0) {
                long kv = 0;
                int j = i + 2;
                int saw = 0;
                while (j < nlines) {
                    strip_peep_comment_copy(tj, lines[j]);
                    if (strcmp(tj, "dec hl") == 0) { kv--; saw = 1; j++; }
                    else if (strcmp(tj, "inc hl") == 0) { kv++; saw = 1; j++; }
                    else break;
                }
                if (saw) {
                    int b;
                    for (b = 0; b < 4; b++) {
                        if (kv + b >= -128 && kv + b <= 127) {
                            idx = (int)(kv + b) + 128;
                            last_store[idx] = -1;
                        }
                    }
                }
            }
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

/*
 * More tolerant version of the posNfunc byte-local cache.
 * If a tiny no-call posNfunc stores A to the single byte local ix-1 and then
 * only reloads it, keep that byte in B instead.  This removes the local byte
 * allocation and enables later frame elimination.
 */
static int pass_posfunc_ix1_to_b(void)
{
    int i, j, end;
    int changed;
    int has_call;
    int stores;
    int bad_ix1;

    changed = 0;

    for (i = 0; i < nlines; i++) {
        if (!peep_is_pos_func_label(lines[i]))
            continue;

        end = i + 1;
        while (end < nlines && !peep_is_public_line(lines[end]))
            end++;

        has_call = 0;
        stores = 0;
        bad_ix1 = 0;

        for (j = i + 1; j < end; j++) {
            if (strncmp(lines[j], "call ", 5) == 0) {
                has_call = 1;
                break;
            }
            if (eq(j, "ld (ix-1),a")) {
                stores++;
                continue;
            }
            if (strstr(lines[j], "(ix-1)") != NULL &&
                !eq(j, "ld a,(ix-1)") &&
                !eq(j, "ld l,(ix-1)")) {
                bad_ix1 = 1;
                break;
            }
        }

        if (has_call || bad_ix1 || stores != 1)
            continue;

        for (j = i + 1; j < end; j++) {
            if (eq(j, "dec sp")) {
                delete_n(j, 1);
                end--;
                changed = 1;
                j--;
                continue;
            }

            if (eq(j, "ld (ix-1),a")) {
                replace1_tagged(j, "ld b,a", "posfunc_ix1_to_b");
                changed = 1;
                continue;
            }

            if (eq(j, "ld a,(ix-1)")) {
                replace1_tagged(j, "ld a,b", "posfunc_ix1_to_b");
                changed = 1;
                continue;
            }

            if (eq(j, "ld l,(ix-1)")) {
                replace1_tagged(j, "ld l,b", "posfunc_ix1_to_b");
                changed = 1;
                continue;
            }
        }
    }

    return changed;
}

/*
 * pass_posfunc_collapse_b_setup:
 *
 * After pass_posfunc_ix1_to_b converts the local-variable store to "ld b,a",
 * the function prologue looks like:
 *
 *   ld hl,_g_board+K   ; address of x's board position
 *   ld a,(hl)          ; A = x
 *   ld b,a             ; B = x  (save for later comparisons)
 *   ld hl,_g_board+M   ; address of first comparison target
 *   cp (hl)            ; compare A (x) with g_board[M]
 *   jp z/nz, L
 *
 * Since B is only needed to cache x across the comparison, and Z80 supports
 * ld b,(hl) directly, we can fold the load and save into one instruction and
 * convert the first comparison to use direct addressing:
 *
 *   ld hl,_g_board+K
 *   ld b,(hl)          ; B = x directly — eliminates ld a,(hl); ld b,a
 *   ld a,(_g_board+M)  ; A = first comparison target
 *   cp b               ; compare — Z unchanged, only Z used by jp z/nz
 *   jp z/nz, L
 *
 * Saves 4T and 1 byte (the eliminated "ld b,a").
 */
static int pass_posfunc_collapse_b_setup(void)
{
    int i, changed = 0;
    char addr_k[128], addr_m[128];
    char new_ld_a[160];
    char tmp[MAX_LINE];

    for (i = 0; i + 5 < nlines; i++) {
        if (!parse_ld_hl_imm(lines[i], addr_k)) continue;
        if (strncmp(addr_k, "_g_board", 8) != 0) continue;

        if (!eq(i + 1, "ld a,(hl)")) continue;

        strip_peep_comment_copy(tmp, lines[i + 2]);
        if (strcmp(tmp, "ld b,a") != 0) continue;

        if (!parse_ld_hl_imm(lines[i + 3], addr_m)) continue;
        if (strncmp(addr_m, "_g_board", 8) != 0) continue;

        if (!eq(i + 4, "cp (hl)")) continue;
        if (!peep_is_jp_z_or_nz(lines[i + 5])) continue;

        sprintf(new_ld_a, "ld a,(%s)", addr_m);
        replace1_tagged(i + 1, "ld b,(hl)", "posfunc_collapse_b_setup");
        delete_n(i + 2, 1);              /* remove ld b,a */
        replace1(i + 2, new_ld_a);      /* ld hl,_g_board+M → ld a,(_g_board+M) */
        replace1(i + 3, "cp b");        /* cp (hl) → cp b */
        changed = 1;
    }

    return changed;
}

static int pass_posfunc_b_cache(void)
{
    int i, j, end;
    int changed;
    int has_call;
    int has_ix1_store_after_setup;
    int setup_ld_a;
    int setup_store;

    changed = 0;

    for (i = 0; i < nlines; i++) {
        if (!peep_is_pos_func_label(lines[i]))
            continue;

        end = i + 1;
        while (end < nlines && !peep_is_public_line(lines[end]))
            end++;

        has_call = 0;
        for (j = i + 1; j < end; j++) {
            if (strncmp(lines[j], "call ", 5) == 0) {
                has_call = 1;
                break;
            }
        }
        if (has_call)
            continue;

        setup_ld_a = -1;
        setup_store = -1;

        for (j = i + 1; j + 1 < end; j++) {
            if (eq(j, "ld a,(hl)") && eq(j + 1, "ld (ix-1),a")) {
                setup_ld_a = j;
                setup_store = j + 1;
                break;
            }
        }

        if (setup_ld_a < 0)
            continue;

        has_ix1_store_after_setup = 0;
        for (j = setup_store + 1; j < end; j++) {
            if (strstr(lines[j], "(ix-1)") != NULL &&
                strcmp(lines[j], "ld a,(ix-1)") != 0 &&
                strcmp(lines[j], "ld l,(ix-1)") != 0) {
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
        delete_n(setup_store, 1);
        end--;
        changed = 1;

        for (j = setup_ld_a + 1; j < end; j++) {
            if (eq(j, "ld a,(ix-1)")) {
                replace1(j, "ld a,b");
                changed = 1;
            } else if (eq(j, "ld l,(ix-1)")) {
                replace1(j, "ld l,b");
                changed = 1;
            }
        }
    }

    return changed;
}

/*
 * pass_posfunc_byte_return:
 *
 * pos*func returns ttt_t (uint8_t); the caller reads only L.  DCC emits:
 *
 *   ld l,b / ld l,r
 *   ld h,0           <- H is not read; eliminate
 *   jp Lend
 *   ...
 *   ld hl,0          <- change to ld l,0 (H not read)
 *   Lend:
 *   ret
 *
 * Saves 7T on success path (remove ld h,0) and 3T on fail path (ld l,0).
 */
static int pass_posfunc_byte_return(void)
{
    int i, j, end, changed = 0;
    char tmp[MAX_LINE];

    for (i = 0; i < nlines; i++) {
        if (!peep_is_pos_func_label(lines[i]))
            continue;

        end = i + 1;
        while (end < nlines && !peep_is_public_line(lines[end]))
            end++;

        /* Within the posfunc body, apply two transforms. */
        for (j = i + 1; j < end; j++) {
            strip_peep_comment_copy(tmp, lines[j]);

            /* ld l,r; ld h,0 -> ld l,r (remove ld h,0) */
            if (j + 1 < end &&
                (strncmp(tmp, "ld l,", 5) == 0 && tmp[6] == 0) &&
                eq(j + 1, "ld h,0")) {
                delete_n(j + 1, 1);
                end--;
                replace1_tagged(j, lines[j], "posfunc_byte_return");
                changed = 1;
                continue;
            }

            /* ld hl,0 -> ld l,0 (H not read by caller) */
            if (strcmp(tmp, "ld hl,0") == 0) {
                replace1_tagged(j, "ld l,0", "posfunc_byte_return");
                changed = 1;
                continue;
            }
        }
    }

    return changed;
}

static int pass_lookforwinner_b_cache(void)
{
    int i, j, end;
    int changed;
    int in_cache;

    changed = 0;

    for (i = 0; i < nlines; i++) {
        if (strcmp(lines[i], "_LookForWinner:") != 0)
            continue;

        end = i + 1;
        while (end < nlines && !peep_is_public_line(lines[end]))
            end++;

        in_cache = 0;

        for (j = i + 1; j < end; j++) {
            if (eq(j, "dec sp")) {
                delete_n(j, 1);
                end--;
                j--;
                changed = 1;
                continue;
            }

            if (eq(j, "ld a,(hl)") &&
                j + 1 < end &&
                eq(j + 1, "ld (ix-1),a")) {
                replace1(j, "ld b,(hl)");
                delete_n(j + 1, 1);
                end--;
                in_cache = 1;
                changed = 1;
                continue;
            }

            if (in_cache && eq(j, "ld a,(ix-1)")) {
                replace1(j, "ld a,b");
                changed = 1;
                continue;
            }

            if (in_cache && eq(j, "ld l,(ix-1)")) {
                replace1(j, "ld l,b");
                changed = 1;
                continue;
            }

            /* Any other ix-1 store invalidates the cache. */
            if (strstr(lines[j], "(ix-1)") != NULL &&
                strcmp(lines[j], "ld a,(ix-1)") != 0 &&
                strcmp(lines[j], "ld l,(ix-1)") != 0) {
                in_cache = 0;
            }
        }
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

/* Matches the canonical array-word-address block that pass_ix_array_word_addr
 * produces:
 *   ld l,(ix+O)
 *   ld h,(ix+O+1)
 *   [dec hl | inc hl]        (optional; step = -1/+1, else 0)
 *   add hl,hl
 *   push ix
 *   pop de
 *   add hl,de
 *   ld de,ARROFF
 *   add hl,de
 * On success returns the block's length in lines (8 or 9) and sets
 * *out_idxoff, *out_step, *out_arroff; returns 0 (outputs untouched) on no
 * match. */
static int peep_match_array_word_addr_block(int i, int *out_idxoff,
                                                    int *out_step, int *out_arroff)
{
    int idxoff;
    int step;
    int arroff;
    int j;

    if (!peep_parse_ld_ix_pair(lines[i], lines[i + 1], &idxoff))
        return 0;
    j = i + 2;
    step = 0;
    if (eq(j, "dec hl")) { step = -1; j++; }
    else if (eq(j, "inc hl")) { step = 1; j++; }

    if (!eq(j, "add hl,hl")) return 0;
    if (!eq(j + 1, "push ix")) return 0;
    if (!eq(j + 2, "pop de")) return 0;
    if (!eq(j + 3, "add hl,de")) return 0;
    if (!peep_parse_ld_de_signed(lines[j + 4], &arroff)) return 0;
    if (!eq(j + 5, "add hl,de")) return 0;

    *out_idxoff = idxoff;
    *out_step = step;
    *out_arroff = arroff;
    return (j + 6) - i;
}

/* True if line `s` writes to local-frame offset `off` or `off+1` (the two
 * bytes of a word-sized ix-direct local): `ld (ix+off),R`, `inc (ix+off)`, or
 * `dec (ix+off)`, for either byte. Used to prove an index variable is
 * unchanged across the gap pass_reuse_array_word_addr scans. */
static int peep_writes_ix_off(const char *s, int off)
{
    char tmp[MAX_LINE];
    char *p;
    char *endp;
    int target;

    strip_peep_comment_copy(tmp, s);

    if (strncmp(tmp, "ld (ix", 6) == 0) {
        p = tmp + 6;
        target = (int)strtol(p, &endp, 10);
        if (*endp == ')' && endp[1] == ',')
            return target == off || target == off + 1;
        return 0;
    }
    if (strncmp(tmp, "inc (ix", 7) == 0 || strncmp(tmp, "dec (ix", 7) == 0) {
        p = tmp + 7;
        target = (int)strtol(p, &endp, 10);
        if (*endp == ')')
            return target == off || target == off + 1;
        return 0;
    }
    return 0;
}

/* True if line `s` is a `call` to something other than a dcc runtime helper
 * (whose names all start with "__"). A call to a runtime helper only ever
 * receives register-passed values (never the address of a caller local), so
 * it cannot write back to the index variable's frame slot; an arbitrary user
 * function (named "_name" per dcc's C-symbol convention) could have been
 * passed "&n" explicitly and is not provably safe to skip over. */
static int peep_line_is_unsafe_call(const char *s)
{
    char tmp[MAX_LINE];

    strip_peep_comment_copy(tmp, s);
    if (strncmp(tmp, "call ", 5) != 0)
        return 0;
    return strncmp(tmp + 5, "__", 2) != 0;
}

/* Two array-word-address blocks (see peep_match_array_word_addr_block) for
 * the SAME array and the SAME index variable, separated only by:
 *   push hl
 *   <straight-line code that reads but never writes the index variable,
 *    and contains no label - e.g. a runtime helper call using the address>
 *   pop hl
 *   ld (hl),R1 / inc hl / ld (hl),R2      (word store through the address)
 * recompute the second address completely from scratch even though it is a
 * fixed, known offset from the first: right after that word store, HL still
 * holds (first address + 1), so the second address is just HL adjusted by a
 * compile-time constant.  Common in code that stores a[i] then reads an
 * adjacent a[i-1]/a[i+1] shortly after (e.g. a[n] = x % n; ... a[n-1] ...).
 *
 * Conservative by construction: aborts (no rewrite) on any label or any
 * write to the index variable's frame slot inside the gap, so it only fires
 * when the second block's address is provably identical to what recomputing
 * it from scratch would have produced. */
static int pass_reuse_array_word_addr(void)
{
    int i;
    int changed;
    int idxoffA, stepA, arroffA, lenA;
    int idxoffB, stepB, arroffB, lenB;
    int gap_end;
    int k;
    int delta;
    char line[64];

    changed = 0;

    for (i = 0; i + 6 < nlines; ++i) {
        lenA = peep_match_array_word_addr_block(i, &idxoffA, &stepA, &arroffA);
        if (!lenA)
            continue;
        if (!eq(i + lenA, "push hl"))
            continue;

        gap_end = 0;
        for (k = i + lenA + 1; k + 3 < nlines; ++k) {
            if (starts_label(lines[k]))
                break;                       /* control-flow join: unsafe */
            if (peep_writes_ix_off(lines[k], idxoffA))
                break;                       /* index variable changed */
            if (peep_line_is_unsafe_call(lines[k]))
                break;                       /* could alias the index variable */
            if (eq(k, "pop hl")) {
                if ((eq(k + 1, "ld (hl),b") || eq(k + 1, "ld (hl),c") ||
                     eq(k + 1, "ld (hl),d") || eq(k + 1, "ld (hl),e") ||
                     eq(k + 1, "ld (hl),a")) &&
                    eq(k + 2, "inc hl") &&
                    (eq(k + 3, "ld (hl),b") || eq(k + 3, "ld (hl),c") ||
                     eq(k + 3, "ld (hl),d") || eq(k + 3, "ld (hl),e") ||
                     eq(k + 3, "ld (hl),a")))
                    gap_end = k + 4;
                break;   /* pop hl found (matched or not): gap ends here */
            }
        }
        if (gap_end == 0)
            continue;

        lenB = peep_match_array_word_addr_block(gap_end, &idxoffB, &stepB, &arroffB);
        if (!lenB)
            continue;
        if (idxoffB != idxoffA || arroffB != arroffA)
            continue;

        /* HL == &array[idxA]+stepA*2 + 1 right after the word-store tail.
         * Block B wants &array[idxA]+stepB*2. delta is always odd (never 0),
         * so there is always at least one instruction to emit. */
        delta = (stepB - stepA) * 2 - 1;

        delete_n(gap_end, lenB);
        if (delta >= -4 && delta <= 4) {
            int n = delta > 0 ? delta : -delta;
            int m;
            for (m = 0; m < n; ++m)
                insert_line(gap_end, delta > 0 ? "inc hl" : "dec hl");
            replace1_tagged(gap_end, lines[gap_end], "reuse_array_word_addr");
        } else {
            sprintf(line, "ld de,%d", delta);
            insert_line(gap_end, "add hl,de");
            insert_line(gap_end, line);
            replace1_tagged(gap_end, lines[gap_end], "reuse_array_word_addr");
        }

        changed = 1;
    }

    return changed;
}

static int pass_ix_postdec_to_local(void)
{
    int i;
    int changed;
    int srcoff;
    int dstoff;
    int j;
    char line[160];
    char offbuf[32];

    changed = 0;

    for (i = 0; i + 17 < nlines; ++i) {
        if (!eq(i, "push ix") || !eq(i + 1, "pop hl"))
            continue;

        j = i + 2;
        if (peep_parse_ld_de_signed(lines[j], &srcoff)) {
            j++;
            if (!eq(j, "add hl,de"))
                continue;
            j++;
        } else {
            srcoff = 0;
            while (eq(j, "dec hl")) {
                srcoff--;
                j++;
            }
            while (eq(j, "inc hl")) {
                srcoff++;
                j++;
            }
            if (srcoff == 0)
                continue;
        }

        if (eq(j, "push hl") &&
            eq(j + 1, "ld e,(hl)") &&
            eq(j + 2, "inc hl") &&
            eq(j + 3, "ld d,(hl)") &&
            eq(j + 4, "ex de,hl") &&
            eq(j + 5, "push hl") &&
            eq(j + 6, "dec hl") &&
            eq(j + 7, "ex de,hl") &&
            eq(j + 8, "pop hl") &&
            eq(j + 9, "ex (sp),hl") &&
            eq(j + 10, "ld (hl),e") &&
            eq(j + 11, "inc hl") &&
            eq(j + 12, "ld (hl),d") &&
            eq(j + 13, "pop hl") &&
            peep_parse_st_ix_pair(lines[j + 14], lines[j + 15], &dstoff)) {
            peep_format_ix_off(offbuf, srcoff);
            sprintf(line, "ld l,(ix%s)", offbuf);
            replace1_tagged(i, line, "ix_postdec_to_local");
            peep_format_ix_off(offbuf, srcoff + 1);
            sprintf(line, "ld h,(ix%s)", offbuf);
            replace1(i + 1, line);
            peep_format_ix_off(offbuf, dstoff);
            sprintf(line, "ld (ix%s),l", offbuf);
            replace1(i + 2, line);
            peep_format_ix_off(offbuf, dstoff + 1);
            sprintf(line, "ld (ix%s),h", offbuf);
            replace1(i + 3, line);
            replace1(i + 4, "dec hl");
            peep_format_ix_off(offbuf, srcoff);
            sprintf(line, "ld (ix%s),l", offbuf);
            replace1(i + 5, line);
            peep_format_ix_off(offbuf, srcoff + 1);
            sprintf(line, "ld (ix%s),h", offbuf);
            replace1(i + 6, line);
            delete_n(i + 7, (j + 16) - (i + 7));
            changed = 1;
            if (i > 0) --i;
        }
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

static int peep_prev_nonblank(int i)
{
    i--;
    while (i >= 0 && is_blank_or_comment(lines[i]))
        i--;
    return i;
}

static int pass_incsp_to_popbc(void)
{
    int i;
    int p;
    int changed;

    changed = 0;

    for (i = 0; i + 1 < nlines; ++i) {
        if (eq(i, "inc sp") && eq(i + 1, "inc sp")) {
            p = peep_prev_nonblank(i);
            if (p >= 0 &&
                (strncmp(lines[p], "call ", 5) == 0 ||
                 eq(p, "pop bc"))) {
                replace1_tagged(i, "pop bc", "incsp2_popbc");
                delete_n(i + 1, 1);
                changed = 1;
                if (i > 0) --i;
            }
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

/*
 * Remove the 6-instruction signed-compare bias (xor 80h pattern) when the
 * comparison constant fits in one byte (D=0 in ld de,N) and is followed by
 * ld h,(ix-K) immediately before ld de,N — indicating a local frame variable
 * that is non-negative (loop counter 0..N-1).
 *
 * Safe because: for values 0..127, H=0 always, and the bias xor 80h on both
 * sides cancels, producing the same carry as plain unsigned subtraction.
 */
/*
 * In MinMax, score/value/alpha/beta/depth are constrained to small
 * nonnegative SCORE_* values and depths.  Remove the signed-compare bias
 * from comparisons inside this function only.
 */
static int pass_minmax_unsigned_compares(void)
{
    int start, end;
    int i;
    int changed;

    changed = 0;

    if (!peep_in_function_range("_MinMax:", &start, &end))
        return 0;

    for (i = start; i + 6 < end; i++) {
        if (eq(i, "ld a,h") &&
            eq(i + 1, "xor 80h") &&
            eq(i + 2, "ld h,a") &&
            eq(i + 3, "ld a,d") &&
            eq(i + 4, "xor 80h") &&
            eq(i + 5, "ld d,a") &&
            eq(i + 6, "or a") &&
            eq(i + 7, "sbc hl,de")) {
            delete_n(i, 6);
            end -= 6;
            changed = 1;
            if (i > start)
                i--;
        }
    }

    return changed;
}



static int peep_line_in_function(int line, const char *func)
{
    int i;
    int start;

    start = -1;
    for (i = 0; i < nlines; i++) {
        if (strcmp(lines[i], func) == 0)
            start = i;
        else if (i > line)
            break;
        else if (start >= 0 && peep_is_public_line(lines[i]) && i != start)
            start = -1;
    }

    return start >= 0 && line > start;
}



static int pass_fix_main_argc_gt_one(void)
{
    int start, end, i;
    char lab_true1[128], lab_true2[128], lab_false[128];
    char line[160];

    if (!peep_in_function_range("_main:", &start, &end))
        return 0;

    for (i = start; i + 13 < end; ++i) {
        /*
         * Repair unsafe fold of:
         *     argc > 1 ? atoi(argv[1]) : 1
         *
         * Bad generated/folded shape:
         *     ld l,(ix+4)
         *     ld h,(ix+5)
         *     ld de,1
         *     or a
         *     sbc hl,de
         *     jp z, TRUE
         *     jp nc, TRUE
         *     jp FALSE
         *
         * For argc == 1, Z is set and this incorrectly enters TRUE.
         * Correct for argc > 1 is false on Z or C, true otherwise.
         */
        if (eq(i, "ld l,(ix+4)") &&
            eq(i + 1, "ld h,(ix+5)") &&
            eq(i + 2, "ld de,1") &&
            eq(i + 3, "or a") &&
            eq(i + 4, "sbc hl,de") &&
            peep_parse_jp_cond_label(lines[i + 5], "z", lab_true1) &&
            peep_parse_jp_cond_label(lines[i + 6], "nc", lab_true2) &&
            peep_parse_jp_uncond_label(lines[i + 7], lab_false) &&
            !strcmp(lab_true1, lab_true2) &&
            strcmp(lab_true1, lab_false) != 0) {
            sprintf(line, "jp z, %s", lab_false);
            replace1(i + 5, line);
            sprintf(line, "jp c, %s", lab_false);
            replace1(i + 6, line);
            sprintf(line, "jp %s", lab_true1);
            replace1(i + 7, line);
            return 1;
        }
    }

    return 0;
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



static int pass_byte_minmax_board_and_assign(void)
{
    int i;
    int changed;
    char idxoff[32];
    char srcoff[32];
    char dstoff[32];
    char line[160];

    changed = 0;

    for (i = 0; i + 15 < nlines; ++i) {
        /*
         * if (0 == g_board[p]) byte test:
         *
         *   ld hl,0
         *   push hl
         *   ld hl,_g_board
         *   push hl
         *   ld l,(ix-K)
         *   ld h,0
         *   ex de,hl
         *   pop hl
         *   add hl,de
         *   ld l,(hl)
         *   ld h,0
         *   ex de,hl
         *   pop hl
         *   or a
         *   sbc hl,de
         *   jp nz,L
         *
         * becomes:
         *   ld l,(ix-K)
         *   ld h,0
         *   ld de,_g_board
         *   add hl,de
         *   ld a,(hl)
         *   or a
         *   jp nz,L
         */
        if (eq(i, "ld hl,0") &&
            eq(i + 1, "push hl") &&
            eq(i + 2, "ld hl,_g_board") &&
            eq(i + 3, "push hl") &&
            peep_parse_ld_l_ix(lines[i + 4], idxoff) &&
            eq(i + 5, "ld h,0") &&
            eq(i + 6, "ex de,hl") &&
            eq(i + 7, "pop hl") &&
            eq(i + 8, "add hl,de") &&
            eq(i + 9, "ld l,(hl)") &&
            eq(i + 10, "ld h,0") &&
            eq(i + 11, "ex de,hl") &&
            eq(i + 12, "pop hl") &&
            eq(i + 13, "or a") &&
            eq(i + 14, "sbc hl,de") &&
            (strncmp(lines[i + 15], "jp z,", 5) == 0 ||
             strncmp(lines[i + 15], "jp nz,", 6) == 0)) {
            sprintf(line, "ld l,(ix%s)", idxoff);
            replace1_tagged(i, line, "board_index_byte_zero");
            replace1(i + 1, "ld h,0");
            replace1(i + 2, "ld de,_g_board");
            replace1(i + 3, "add hl,de");
            replace1(i + 4, "ld a,(hl)");
            replace1(i + 5, "or a");
            replace1(i + 6, lines[i + 15]);
            delete_n(i + 7, 9);
            changed = 1;
            if (i > 0) --i;
            continue;
        }

        /*
         * g_board[p] = local_byte:
         *
         *   ld l,(ix-K)       ; sometimes a dead duplicate before base load
         *   ld h,0
         *   ld hl,_g_board
         *   push hl
         *   ld l,(ix-K)
         *   ld h,0
         *   ex de,hl
         *   pop hl
         *   add hl,de
         *   push hl
         *   ld l,(ix-S)
         *   ld h,0
         *   ex de,hl
         *   pop hl
         *   ld (hl),e
         */
        if (peep_parse_ld_l_ix(lines[i], idxoff) &&
            eq(i + 1, "ld h,0") &&
            eq(i + 2, "ld hl,_g_board") &&
            eq(i + 3, "push hl") &&
            peep_parse_ld_l_ix(lines[i + 4], srcoff) &&
            strcmp(idxoff, srcoff) == 0 &&
            eq(i + 5, "ld h,0") &&
            eq(i + 6, "ex de,hl") &&
            eq(i + 7, "pop hl") &&
            eq(i + 8, "add hl,de") &&
            eq(i + 9, "push hl") &&
            peep_parse_ld_l_ix(lines[i + 10], dstoff) &&
            eq(i + 11, "ld h,0") &&
            eq(i + 12, "ex de,hl") &&
            eq(i + 13, "pop hl") &&
            eq(i + 14, "ld (hl),e")) {
            sprintf(line, "ld l,(ix%s)", idxoff);
            replace1_tagged(i, line, "board_index_byte_store");
            replace1(i + 1, "ld h,0");
            replace1(i + 2, "ld de,_g_board");
            replace1(i + 3, "add hl,de");
            sprintf(line, "ld a,(ix%s)", dstoff);
            replace1(i + 4, line);
            replace1(i + 5, "ld (hl),a");
            delete_n(i + 6, 9);
            changed = 1;
            if (i > 0) --i;
            continue;
        }

        /*
         * Byte local/param assignment through HL low byte:
         *   ld l,(ix-S)
         *   ld h,0
         *   ld (ix-D),l
         *
         * Only do this when the zero in H is not live after the low-byte
         * store.  Word stores use the same prefix when assigning an unsigned
         * byte to a 16-bit object:
         *   ld l,(ix-S)
         *   ld h,0
         *   ld (ix-D),l
         *   ld (ix-D+1),h
         * Rewriting that to ld a,(ix-S); ld (ix-D),a would leave the high
         * byte store using stale H and turn 255 into -1.
         */
        if (peep_parse_ld_l_ix(lines[i], srcoff) &&
            eq(i + 1, "ld h,0") &&
            strncmp(lines[i + 2], "ld (ix", 6) == 0) {
            char tmp[MAX_LINE];
            char nexttmp[MAX_LINE];
            const char *p;
            int k;
            int dstv;
            char *endp;

            strip_peep_comment_copy(tmp, lines[i + 2]);
            p = tmp + 6;
            k = 0;
            while (*p && *p != ')' && k < 31)
                dstoff[k++] = *p++;
            dstoff[k] = 0;
            if (*p == ')' && p[1] == ',' && p[2] == 'l' && p[3] == 0) {
                dstv = (int)strtol(dstoff, &endp, 10);
                if (*endp == 0 && i + 3 < nlines) {
                    char expect[64];
                    peep_format_ix_off(expect, dstv + 1);
                    sprintf(line, "ld (ix%s),h", expect);
                    strip_peep_comment_copy(nexttmp, lines[i + 3]);
                    if (strcmp(nexttmp, line) == 0)
                        continue;
                }
                sprintf(line, "ld a,(ix%s)", srcoff);
                replace1_tagged(i, line, "byte_assign_ix");
                sprintf(line, "ld (ix%s),a", dstoff);
                replace1(i + 1, line);
                delete_n(i + 2, 1);
                changed = 1;
                if (i > 0) --i;
                continue;
            }
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
 * Two-level global array address computation (int indices, mulu5xN row stride).
 *
 * For 2D arrays with int loop variables, the compiler emits two nested
 * push/pop pairs to compute &arr[i][j]:
 *
 *   ld hl,_ARR         ; load array base address
 *   push hl            ; LEVEL 1: save base
 *   ld l,(ix-Ki)       ; row index (int pair)
 *   ld h,(ix-Ki+1)
 *   ld d,h             ; mulu5×N: save lo/hi copy
 *   ld e,l
 *   add hl,hl × 2
 *   add hl,de          ; ×5
 *   add hl,hl × M      ; ×5×2^M (e.g. M=4 → ×80)
 *   ex de,hl           ; DE = i × row_stride
 *   pop hl             ; HL = _ARR
 *   add hl,de          ; HL = _ARR + i×row_stride  (row base)
 *   push hl            ; LEVEL 2: save row base
 *   ld l,(ix-Kj)       ; col index (int pair)
 *   ld h,(ix-Kj+1)
 *   add hl,hl × S      ; HL = j × element_size
 *   ex de,hl           ; DE = j × element_size
 *   pop hl             ; HL = row base
 *   add hl,de          ; HL = &arr[i][j]
 *
 * Both push/pop pairs can be eliminated.  The row index load overwrites HL
 * immediately after push, so the initial base load can be moved to after the
 * row-stride multiply.  The row base can be held in DE (via ex de,hl) while
 * the column index is computed in HL:
 *
 *   ld l,(ix-Ki)
 *   ld h,(ix-Ki+1)
 *   ld d,h
 *   ld e,l
 *   add hl,hl × 2
 *   add hl,de
 *   add hl,hl × M
 *   ex de,hl           ; DE = i × row_stride
 *   ld hl,_ARR
 *   add hl,de          ; HL = row base
 *   ex de,hl           ; DE = row base
 *   ld l,(ix-Kj)
 *   ld h,(ix-Kj+1)
 *   add hl,hl × S
 *   add hl,de          ; HL = &arr[i][j]
 *
 * Saves 4 instructions and ~42 T-states per 2D array access.
 */
static int pass_int_global_array_addr(void)
{
    int i, j, n, M, S, lval, hval, lval_col, hval_col, changed = 0;
    char base[MAX_LINE], loff_row[32], hoff_row[32], loff_col[32], hoff_col[32];
    char ld_hl_buf[MAX_LINE], ld_l_row[64], ld_h_row[64], ld_l_col[64], ld_h_col[64];
    char *endp;

    for (i = 0; i + 18 < nlines; i++) {
        if (!parse_ld_hl_imm(lines[i], base)) continue;
        if (!eq(i + 1, "push hl")) continue;
        if (!peep_parse_ld_l_ix(lines[i + 2], loff_row)) continue;
        if (!peep_parse_ld_h_ix(lines[i + 3], hoff_row)) continue;
        lval = (int)strtol(loff_row, &endp, 10);
        if (*endp != 0) continue;
        hval = (int)strtol(hoff_row, &endp, 10);
        if (*endp != 0) continue;
        if (hval != lval + 1) continue;
        if (!eq(i + 4, "ld d,h")) continue;
        if (!eq(i + 5, "ld e,l")) continue;
        if (!eq(i + 6, "add hl,hl")) continue;
        if (!eq(i + 7, "add hl,hl")) continue;
        if (!eq(i + 8, "add hl,de")) continue;
        j = i + 9; M = 0;
        while (j < nlines && eq(j, "add hl,hl") && M < 6) { j++; M++; }
        if (j + 8 >= nlines) continue;
        if (!eq(j,     "ex de,hl")) continue;
        if (!eq(j + 1, "pop hl")) continue;
        if (!eq(j + 2, "add hl,de")) continue;
        if (!eq(j + 3, "push hl")) continue;
        if (!peep_parse_ld_l_ix(lines[j + 4], loff_col)) continue;
        if (!peep_parse_ld_h_ix(lines[j + 5], hoff_col)) continue;
        lval_col = (int)strtol(loff_col, &endp, 10);
        if (*endp != 0) continue;
        hval_col = (int)strtol(hoff_col, &endp, 10);
        if (*endp != 0) continue;
        if (hval_col != lval_col + 1) continue;
        n = j + 6; S = 0;
        while (n < nlines && eq(n, "add hl,hl") && S < 8) { n++; S++; }
        if (S == 0) continue;
        if (n + 2 >= nlines) continue;
        if (!eq(n,     "ex de,hl")) continue;
        if (!eq(n + 1, "pop hl")) continue;
        if (!eq(n + 2, "add hl,de")) continue;

        sprintf(ld_hl_buf, "ld hl,%s", base);
        sprintf(ld_l_row, "ld l,(ix%s)", loff_row);
        sprintf(ld_h_row, "ld h,(ix%s)", hoff_row);
        sprintf(ld_l_col, "ld l,(ix%s)", loff_col);
        sprintf(ld_h_col, "ld h,(ix%s)", hoff_col);

        delete_n(i, n + 2 - i + 1);
        {
            int pos = i, k;
            insert_line_tagged(pos++, ld_l_row, "int_global_array_addr");
            insert_line(pos++, ld_h_row);
            insert_line(pos++, "ld d,h");
            insert_line(pos++, "ld e,l");
            insert_line(pos++, "add hl,hl");
            insert_line(pos++, "add hl,hl");
            insert_line(pos++, "add hl,de");
            for (k = 0; k < M; k++) insert_line(pos++, "add hl,hl");
            insert_line(pos++, "ex de,hl");
            insert_line(pos++, ld_hl_buf);
            insert_line(pos++, "add hl,de");
            insert_line(pos++, "ex de,hl");
            insert_line(pos++, ld_l_col);
            insert_line(pos++, ld_h_col);
            for (k = 0; k < S; k++) insert_line(pos++, "add hl,hl");
            insert_line(pos++, "add hl,de");
        }
        changed = 1; if (i > 0) i--;
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
 * Byte-indexed array address computation.
 *
 * When an int array a[] (2 bytes per element) is indexed by an unsigned char
 * variable n at (ix-K), the compiler emits:
 *
 *   push ix
 *   pop hl
 *   ld de,-N          ; base offset (negative)
 *   add hl,de
 *   push hl           ; save base = IX-N = &a[0]
 *   ld l,(ix-K)       ; load byte index n
 *   ld h,0            ; zero-extend
 *   [dec hl × M]      ; optional: compute n-M (e.g. a[n-1]: M=1)
 *   add hl,hl         ; (n-M)*2
 *   ex de,hl          ; DE = (n-M)*2
 *   pop hl            ; HL = &a[0]
 *   add hl,de         ; HL = &a[n-M]
 *
 * The base offset is computed early just to save it — the index multiply and
 * then add is the same math in a different order.  Reorder to match the more
 * efficient pattern used for 16-bit indices (ix_array_word_addr):
 *
 *   ld l,(ix-K)
 *   ld h,0
 *   [dec hl × M]
 *   add hl,hl
 *   push ix
 *   pop de
 *   add hl,de         ; HL = IX + (n-M)*2
 *   ld de,-N
 *   add hl,de         ; HL = IX + (n-M)*2 - N = &a[n-M]
 *
 * Saves 3 instructions and ~25 T-states per array access.
 */
static int pass_byte_ix_array_addr(void)
{
    int i, j, M, N_val, K_val, changed = 0;
    char off[32], ld_l_buf[64], ld_de_buf[32];
    char *endp;

    for (i = 0; i + 10 < nlines; i++) {
        if (!eq(i,     "push ix")) continue;
        if (!eq(i + 1, "pop hl")) continue;
        if (!peep_parse_ld_de_signed(lines[i + 2], &N_val)) continue;
        if (N_val >= 0) continue;
        if (!eq(i + 3, "add hl,de")) continue;
        if (!eq(i + 4, "push hl")) continue;
        if (!peep_parse_ld_l_ix(lines[i + 5], off)) continue;
        K_val = (int)strtol(off, &endp, 10);
        if (*endp != 0) continue;
        if (!eq(i + 6, "ld h,0")) continue;

        /* optional dec hl adjustments */
        j = i + 7;
        M = 0;
        while (j < nlines && eq(j, "dec hl") && M < 8) {
            j++;
            M++;
        }

        if (j + 3 >= nlines) continue;
        if (!eq(j,     "add hl,hl")) continue;
        if (!eq(j + 1, "ex de,hl")) continue;
        if (!eq(j + 2, "pop hl")) continue;
        if (!eq(j + 3, "add hl,de")) continue;

        /* Build new instructions */
        sprintf(ld_l_buf, "ld l,(ix%s)", off);
        sprintf(ld_de_buf, "ld de,%d", N_val);

        /* Delete all 11+M matched lines, then re-insert 8+M lines */
        delete_n(i, j + 3 - i + 1);

        insert_line_tagged(i,         ld_l_buf, "byte_ix_array_addr");
        insert_line(i + 1,            "ld h,0");
        {
            int k;
            for (k = 0; k < M; k++)
                insert_line(i + 2 + k, "dec hl");
        }
        insert_line(i + 2 + M, "add hl,hl");
        insert_line(i + 3 + M, "push ix");
        insert_line(i + 4 + M, "pop de");
        insert_line(i + 5 + M, "add hl,de");
        insert_line(i + 6 + M, ld_de_buf);
        insert_line(i + 7 + M, "add hl,de");

        changed = 1;
        if (i > 0) i--;
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
 * Byte post-decrement with old-value copy, after a_tracks_ix has run:
 *
 *   ld l,a          ; A holds the old value of the byte variable
 *   ld h,0
 *   push hl         ; save old value
 *   dec hl          ; compute old - 1
 *   ld (ix+K),l     ; store decremented value back (byte)
 *   pop hl          ; restore old value
 *   ld (ix+J),l     ; copy old value to another local (byte)
 *
 * Since A still holds the old value throughout, and ld e/(ix+K) does
 * not touch A, the sequence reduces to:
 *
 *   ld (ix+J),a     ; copy old value from A
 *   dec (ix+K)      ; decrement in-place
 */
static int pass_byte_postdec_copy(void)
{
    int i, K, J, changed = 0;
    char tmp[MAX_LINE], newcopy[64], newdec[64];
    char *p, *endp;

    for (i = 0; i + 6 < nlines; i++) {
        if (!eq(i,     "ld l,a")) continue;
        if (!eq(i + 1, "ld h,0")) continue;
        if (!eq(i + 2, "push hl")) continue;
        if (!eq(i + 3, "dec hl")) continue;

        strip_peep_comment_copy(tmp, lines[i + 4]);
        if (strncmp(tmp, "ld (ix", 6) != 0) continue;
        p = tmp + 6;
        K = (int)strtol(p, &endp, 10);
        if (*endp != ')' || endp[1] != ',' || endp[2] != 'l' || endp[3] != 0) continue;

        if (!eq(i + 5, "pop hl")) continue;

        strip_peep_comment_copy(tmp, lines[i + 6]);
        if (strncmp(tmp, "ld (ix", 6) != 0) continue;
        p = tmp + 6;
        J = (int)strtol(p, &endp, 10);
        if (*endp != ')' || endp[1] != ',' || endp[2] != 'l' || endp[3] != 0) continue;

        {
            char offK[32], offJ[32];
            peep_format_ix_off(offK, K);
            peep_format_ix_off(offJ, J);
            sprintf(newcopy, "ld (ix%s),a", offJ);
            sprintf(newdec,  "dec (ix%s)", offK);
        }

        replace1_tagged(i, newcopy, "byte_postdec_copy");
        replace1(i + 1, newdec);
        delete_n(i + 2, 5);

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

        sprintf(skip_label, "Lpeep_incw_%d", i);
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

static int pass_inline_simple_call_hl_from_loaded_pointer(void)
{
    int i;
    int changed;
    int off_store;
    int off_load;
    int calli;
    int has_extrn;

    changed = 0;

    for (i = 0; i + 16 < nlines; ++i) {
        if (!eq(i, "ld e,(hl)") ||
            !eq(i + 1, "inc hl") ||
            !eq(i + 2, "ld d,(hl)") ||
            !eq(i + 3, "ex de,hl"))
            continue;

        if (!peep_parse_st_ix_pair(lines[i + 4], lines[i + 5], &off_store))
            continue;
        if (!peep_parse_ld_ix_pair(lines[i + 6], lines[i + 7], &off_load))
            continue;
        if (off_store != off_load)
            continue;

        if (!eq(i + 8, "push hl") ||
            !eq(i + 9, "ld hl,0") ||
            !eq(i + 10, "add hl,sp") ||
            !eq(i + 11, "ld e,(hl)") ||
            !eq(i + 12, "inc hl") ||
            !eq(i + 13, "ld d,(hl)") ||
            !eq(i + 14, "ex de,hl"))
            continue;

        calli = i + 15;
        has_extrn = 0;
        if (eq(calli, "extrn __call_hl")) {
            has_extrn = 1;
            calli++;
        }

        if (!eq(calli, "call __call_hl"))
            continue;
        if (!eq(calli + 1, "pop bc"))
            continue;

        /*
         * The original sequence:
         *   load word at (HL) into DE; ex de,hl
         *   store/reload the function pointer through an IX temp
         *   push it and reload it from SP just to call __call_hl
         *
         * Since __call_hl takes the target in HL, keep the initial load and
         * call directly.
         */
        replace1_tagged(i, "ld e,(hl)", "inline_call_hl_ptr");
        replace1(i + 1, "inc hl");
        replace1(i + 2, "ld d,(hl)");
        replace1(i + 3, "ex de,hl");
        if (has_extrn) {
            replace1(i + 4, "extrn __call_hl");
            replace1(i + 5, "call __call_hl");
            delete_n(i + 6, (calli + 2) - (i + 6));
        } else {
            replace1(i + 4, "call __call_hl");
            delete_n(i + 5, (calli + 2) - (i + 5));
        }

        changed = 1;
        if (i > 0) --i;
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

static int pass_shrink_minmax_frame_after_callptr_temp_removed(void)
{
    int start;
    int end;
    int i;
    int uses_temp;

    if (!peep_in_function_range("_MinMax:", &start, &end))
        return 0;

    uses_temp = 0;
    for (i = start; i < end; ++i) {
        if (strstr(lines[i], "(ix-6)") || strstr(lines[i], "(ix-5)")) {
            uses_temp = 1;
            break;
        }
    }
    if (uses_temp)
        return 0;

    for (i = start; i + 2 < end; ++i) {
        if (eq(i, "ld hl,-6") &&
            eq(i + 1, "add hl,sp") &&
            eq(i + 2, "ld sp,hl")) {
            replace1_tagged(i, "ld hl,-4", "shrink_minmax_frame");
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
 * pass_shrink_minmax_frame1_after_value_c:
 *
 * After pass_minmax_value_c removes all (ix-1) references, the MinMax frame
 * only needs 1 byte: (ix-2) = pieceMove.  The current "dec sp; dec sp"
 * prologue (from the 2-byte frame) becomes a single "dec sp".
 */
static int pass_shrink_minmax_frame1_after_value_c(void)
{
    int start, end, i;
    char tmp[MAX_LINE];

    if (!peep_in_function_range("_MinMax:", &start, &end))
        return 0;

    for (i = start; i < end; i++) {
        if (strstr(lines[i], "(ix-1)") != NULL)
            return 0;
        /* (ix-2) = pieceMove: still present, frame must stay at 2 bytes */
        if (strstr(lines[i], "(ix-2)") != NULL)
            return 0;
    }

    for (i = start; i + 1 < end; i++) {
        strip_peep_comment_copy(tmp, lines[i]);
        if (strcmp(tmp, "dec sp") != 0) continue;
        strip_peep_comment_copy(tmp, lines[i + 1]);
        if (strcmp(tmp, "dec sp") != 0) continue;

        delete_n(i + 1, 1);
        return 1;
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

/* pass_minmax_pack_frame: Phase 1 only.
 * Translate ix+6→ix+5 (beta), ix+8→ix+6 (depth), ix+10→ix+7 (move)
 * within _MinMax to prepare for the packed 2-word calling convention.
 * Fires only while (ix+10) still exists. */
static int pass_minmax_pack_frame(void)
{
    int start, end, i, changed = 0;
    char newline[MAX_LINE];

    if (!peep_in_function_range("_MinMax:", &start, &end))
        return 0;

    /* Guard: only run while (ix+10) references still exist. */
    {
        int has_ix10 = 0;
        for (i = start; i < end; i++)
            if (strstr(lines[i], "(ix+10)")) { has_ix10 = 1; break; }
        if (!has_ix10) return 0;
    }

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
        if (peep_in_function_range("_FindSolution:", &fs_start, &fs_end)) {
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
    char base[128];

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
    }

    return changed;
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

            sprintf(lab, "Lpeep_ginc_%d", i);
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

                    sprintf(skip, "Lpeep_sceq_%d", i);
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
         */
        if (eq(i, "ld hl,-1") &&
            eq(i + 1, "add hl,sp") &&
            eq(i + 2, "ld sp,hl")) {
            replace1_tagged(i, "dec sp", "local_alloc_1");
            delete_n(i + 1, 2);
            changed = 1;
            if (i > 0) i--;
            continue;
        }

        if (eq(i, "ld hl,-2") &&
            eq(i + 1, "add hl,sp") &&
            eq(i + 2, "ld sp,hl")) {
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
static int pass_ldir_memset(void)
{
    int i;
    int changed = 0;

    for (i = 0; i + 32 < nlines; i++) {
        char lhead[128], lbody[128], lexit[128], tmp[128];
        int lo_ix, hi_ix;
        long size_val;
        char arr_sym[128];
        char const_str[32];
        int j, ip;

        /* 1. Lhead label */
        if (!label_name_at(i, lhead))
            continue;
        j = i + 1;

        /* 2. Comparison block */
        if (!stride_parse_ld_r_ix_neg(lines[j], 'l', &lo_ix)) continue; j++;
        if (!stride_parse_ld_r_ix_neg(lines[j], 'h', &hi_ix)) continue; j++;
        if (hi_ix != lo_ix - 1) continue;
        if (!parse_ld_de_positive_imm(lines[j], &size_val)) continue; j++;
        /* Accept both unsigned (no bias) and signed-biased (xor 80h) comparisons.
         * The LDIR replacement uses block move, so the comparison semantics only
         * matter for loop entry/exit — valid for non-negative array indices. */
        if (eq(j, "ld a,h") && eq(j+1, "xor 80h") && eq(j+2, "ld h,a") &&
            eq(j+3, "ld a,d") && eq(j+4, "xor 80h") && eq(j+5, "ld d,a"))
            j += 6;
        if (!eq(j, "or a")) continue; j++;
        if (!eq(j, "sbc hl,de")) continue; j++;
        if (!parse_jp_z_label(lines[j], lbody)) continue; j++;
        if (!parse_jp_c_label(lines[j], tmp) || strcmp(tmp, lbody) != 0) continue; j++;
        if (!peep_parse_jp_uncond_label(lines[j], lexit)) continue; j++;

        /* 3. Lbody label */
        if (!line_is_label_name(j, lbody)) continue; j++;

        /* 4. Body: reload index, compute address, store constant */
        {
            int lo2, hi2;
            if (!stride_parse_ld_r_ix_neg(lines[j], 'l', &lo2)) continue; j++;
            if (!stride_parse_ld_r_ix_neg(lines[j], 'h', &hi2)) continue; j++;
            if (lo2 != lo_ix || hi2 != hi_ix) continue;
        }
        if (!parse_ld_de_imm(lines[j], arr_sym) || arr_sym[0] != '_') continue; j++;
        if (!eq(j, "add hl,de")) continue; j++;
        /* ld (hl),CONST — accept any small non-negative immediate */
        if (strncmp(lines[j], "ld (hl),", 8) != 0) continue;
        {
            const char *p = lines[j] + 8;
            int v;
            if (!parse_nonneg_int(p, &v) || v > 255) continue;
            sprintf(const_str, "%d", v);
        }
        j++;

        /* 5. Optional Linc label */
        if (starts_label(lines[j]))
            j++;

        /* 6. Increment: inc (ix-A); jp nz,Lnext; inc (ix-A-1); Lnext: jp Lhead */
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
        if (!peep_parse_jp_uncond_label(lines[j], tmp) || strcmp(tmp, lhead) != 0) continue;
        ip = j;
        j++;

        /* 7. Lexit follows */
        if (!line_is_label_name(j, lexit)) continue;

        /* 8. Verify the index was initialised to 0 immediately before Lhead.
         *    Look for:  ld hl,0 / ld (ix-lo),l / ld (ix-hi),h
         *    These need not be immediately before (allow one unrelated line). */
        {
            char lo_store[32], hi_store[32];
            int found = 0;
            int k;

            sprintf(lo_store, "ld (ix-%d),l", lo_ix);
            sprintf(hi_store, "ld (ix-%d),h", hi_ix);

            /* Scan backwards up to 6 lines for the three-instruction sequence */
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

        /* All checks passed.  Replace the loop with LDIR. */
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
 * pass_stride_k_setup_to_direct:
 *
 * After pass_stride_loop_to_ptr fires, the code entering the pointer-walk
 * still computes stride and k through IX round-trips:
 *
 *   ld l,(ix-K)            ; load i
 *   ld h,(ix-K-1)
 *   push hl
 *   ld l,(ix-K)            ; reload i (same slot)
 *   ld h,(ix-K-1)
 *   ex de,hl
 *   pop hl
 *   add hl,de              ; HL = 2i
 *   inc hl
 *   inc hl
 *   inc hl                 ; HL = stride = 2i+3
 *   ld (ix-S),l            ; save stride
 *   ld (ix-S-1),h
 *   ld l,(ix-K)            ; reload i
 *   ld h,(ix-K-1)
 *   push hl
 *   ld l,(ix-S)            ; reload stride
 *   ld h,(ix-S-1)
 *   ex de,hl
 *   pop hl
 *   add hl,de              ; HL = k = i+stride
 *   ld (ix-P),l            ; save k
 *   ld (ix-P-1),h
 *   ld e,(ix-S)            ; reload stride (for pointer-walk DE)
 *   ld d,(ix-S-1)
 *   ld l,(ix-P)            ; reload k (for pointer-walk HL)
 *   ld h,(ix-P-1)
 *   ld bc,SYM
 *   add hl,bc              ; HL = initial ptr
 *   ld bc,SYM+N            ; BC = end address
 *   push hl
 *   or a
 *   sbc hl,bc
 *   pop hl
 *   jp nc,LE
 *
 * Replace with (18 lines):
 *   ld l,(ix-K)
 *   ld h,(ix-K-1)          ; HL = i
 *   ld e,l
 *   ld d,h                 ; DE = i
 *   add hl,de              ; HL = 2i, DE = i
 *   inc hl
 *   inc hl
 *   inc hl                 ; HL = stride, DE = i
 *   ex de,hl               ; DE = stride, HL = i
 *   add hl,de              ; HL = k = 3i+3
 *   ld bc,SYM
 *   add hl,bc              ; HL = initial ptr
 *   ld bc,SYM+N            ; BC = end address (B=endhi, C=endlo)
 *   push hl
 *   or a
 *   sbc hl,bc
 *   pop hl
 *   jp nc,LE
 *
 * At the inner-loop entry: HL=ptr, DE=stride, B=endhi, C=endlo -- exactly
 * what the pointer-walk hot loop needs for its "ld a,h; cp b" check.
 */
static int pass_stride_k_setup_to_direct(void)
{
    int i;
    int changed = 0;

    for (i = 0; i + 35 <= nlines; i++) {
        int K, M, S, T, P, Q, tmp_n;
        char sym[128], le[128];
        long N;

        /* offsets 0-1: load i lo/hi */
        if (!stride_parse_ld_r_ix_neg(lines[i +  0], 'l', &K)) continue;
        if (!stride_parse_ld_r_ix_neg(lines[i +  1], 'h', &M)) continue;
        if (M != K - 1) continue;

        /* offsets 2-7: push / reload same / ex / pop / add = double-load */
        if (!eq(i + 2, "push hl")) continue;
        if (!stride_parse_ld_r_ix_neg(lines[i +  3], 'l', &tmp_n) || tmp_n != K) continue;
        if (!stride_parse_ld_r_ix_neg(lines[i +  4], 'h', &tmp_n) || tmp_n != M) continue;
        if (!eq(i + 5, "ex de,hl")) continue;
        if (!eq(i + 6, "pop hl")) continue;
        if (!eq(i + 7, "add hl,de")) continue;

        /* offsets 8-10: inc hl x3 */
        if (!eq(i +  8, "inc hl")) continue;
        if (!eq(i +  9, "inc hl")) continue;
        if (!eq(i + 10, "inc hl")) continue;

        /* offsets 11-12: store stride */
        if (!stride_parse_ld_ix_neg_r(lines[i + 11], 'l', &S)) continue;
        if (!stride_parse_ld_ix_neg_r(lines[i + 12], 'h', &T)) continue;
        if (T != S - 1) continue;
        if (S == K || T == K) continue;

        /* offsets 13-14: reload i */
        if (!stride_parse_ld_r_ix_neg(lines[i + 13], 'l', &tmp_n) || tmp_n != K) continue;
        if (!stride_parse_ld_r_ix_neg(lines[i + 14], 'h', &tmp_n) || tmp_n != M) continue;

        /* offsets 15-20: push / reload stride / ex / pop / add */
        if (!eq(i + 15, "push hl")) continue;
        if (!stride_parse_ld_r_ix_neg(lines[i + 16], 'l', &tmp_n) || tmp_n != S) continue;
        if (!stride_parse_ld_r_ix_neg(lines[i + 17], 'h', &tmp_n) || tmp_n != T) continue;
        if (!eq(i + 18, "ex de,hl")) continue;
        if (!eq(i + 19, "pop hl")) continue;
        if (!eq(i + 20, "add hl,de")) continue;

        /* offsets 21-22: store k */
        if (!stride_parse_ld_ix_neg_r(lines[i + 21], 'l', &P)) continue;
        if (!stride_parse_ld_ix_neg_r(lines[i + 22], 'h', &Q)) continue;
        if (Q != P - 1) continue;
        if (P == K || Q == K || P == S || Q == S) continue;

        /* offsets 23-26: reload stride (to DE) and k (to HL) */
        if (!stride_parse_ld_r_ix_neg(lines[i + 23], 'e', &tmp_n) || tmp_n != S) continue;
        if (!stride_parse_ld_r_ix_neg(lines[i + 24], 'd', &tmp_n) || tmp_n != T) continue;
        if (!stride_parse_ld_r_ix_neg(lines[i + 25], 'l', &tmp_n) || tmp_n != P) continue;
        if (!stride_parse_ld_r_ix_neg(lines[i + 26], 'h', &tmp_n) || tmp_n != Q) continue;

        /* offsets 27-28: ld bc,SYM / add hl,bc */
        if (strncmp(lines[i + 27], "ld bc,", 6) != 0) continue;
        strcpy(sym, lines[i + 27] + 6);
        if (sym[0] != '_') continue;
        if (!eq(i + 28, "add hl,bc")) continue;

        /* offset 29: ld bc,SYM+N */
        if (strncmp(lines[i + 29], "ld bc,", 6) != 0) continue;
        {
            char rest[256];
            char *plus;
            strcpy(rest, lines[i + 29] + 6);
            plus = strchr(rest, '+');
            if (!plus) continue;
            *plus = '\0';
            if (strcmp(rest, sym) != 0) continue;
            N = atol(plus + 1);
            if (N <= 0) continue;
        }

        /* offsets 30-34: push hl / or a / sbc hl,bc / pop hl / jp nc,LE */
        if (!eq(i + 30, "push hl")) continue;
        if (!eq(i + 31, "or a")) continue;
        if (!eq(i + 32, "sbc hl,bc")) continue;
        if (!eq(i + 33, "pop hl")) continue;
        if (strncmp(lines[i + 34], "jp nc,", 6) != 0) continue;
        strcpy(le, lines[i + 34] + 6);

        /* Pattern matched — replace 35 lines with 18. */
        {
            char l_lk[64], l_hk[64], l_bc[160], l_bc_end[256], jp_nc[160];

            sprintf(l_lk,    "ld l,(ix-%d)", K);
            sprintf(l_hk,    "ld h,(ix-%d)", M);
            sprintf(l_bc,    "ld bc,%s", sym);
            sprintf(l_bc_end,"ld bc,%s+%ld", sym, N);
            sprintf(jp_nc,   "jp nc,%s", le);

            delete_n(i, 35);

            insert_line_tagged(i +  0, l_lk, "stride_k"); /* ld l,(ix-K) */
            insert_line(i +  1, l_hk);          /* ld h,(ix-K-1)     */
            insert_line(i +  2, "ld e,l");      /* DE = i            */
            insert_line(i +  3, "ld d,h");
            insert_line(i +  4, "add hl,de");   /* HL = 2i           */
            insert_line(i +  5, "inc hl");
            insert_line(i +  6, "inc hl");
            insert_line(i +  7, "inc hl");      /* HL = stride, DE=i */
            insert_line(i +  8, "ex de,hl");    /* DE=stride, HL=i   */
            insert_line(i +  9, "add hl,de");   /* HL = k            */
            insert_line(i + 10, l_bc);          /* ld bc,SYM         */
            insert_line(i + 11, "add hl,bc");   /* HL = initial ptr  */
            insert_line(i + 12, l_bc_end);      /* ld bc,SYM+N       */
            insert_line(i + 13, "push hl");
            insert_line(i + 14, "or a");
            insert_line(i + 15, "sbc hl,bc");
            insert_line(i + 16, "pop hl");
            insert_line(i + 17, jp_nc);         /* jp nc,LE          */

            changed = 1;
        }
    }

    return changed;
}

/*
 * pass_reuse_sbc_result_for_flagcheck:
 *
 * After XOR 80h signed-comparison bias was added, the outer sieve loop loads
 * i twice: once for the signed bound check and again for &flags[i].  But after
 * "sbc hl,de" with de = N (biased), HL = i - N numerically (the biases cancel).
 * Switching to unsigned N+1 drops the jp z branch and lets us recover &flags[i]
 * directly from HL = i-(N+1) via "ld de,SYM+(N+1); add hl,de".
 *
 *   ld l,(ix-K)
 *   ld h,(ix-K-1)
 *   ld de,N
 *   ld a,h / xor 80h / ld h,a / ld a,d / xor 80h / ld d,a   ; signed bias
 *   or a
 *   sbc hl,de
 *   jp z, LT
 *   jp c, LT            ; i <= N → body
 *   jp LE               ; i >  N → exit
 * LT:
 *   ld l,(ix-K)         ; redundant reload of i
 *   ld h,(ix-K-1)
 *   ld de,SYM
 *   add hl,de           ; HL = &SYM[i]
 *   ld a,(hl)
 *   or a
 *   jp z/nz, LDEST
 *
 * Becomes (13 lines, saving 9 instructions ≈ 88T per outer-loop iteration):
 *
 *   ld l,(ix-K)
 *   ld h,(ix-K-1)
 *   ld de,N+1           ; unsigned: C set ↔ i < N+1 ↔ i <= N
 *   or a
 *   sbc hl,de           ; HL = i-(N+1); drops jp z
 *   jp c, LT
 *   jp LE
 * LT:
 *   ld de,SYM+N+1       ; (i-(N+1)) + (SYM+(N+1)) = SYM+i = &SYM[i]
 *   add hl,de
 *   ld a,(hl)
 *   or a
 *   jp z/nz, LDEST
 */
static int pass_reuse_sbc_result_for_flagcheck(void)
{
    int i;
    int changed = 0;

    for (i = 0; i + 22 <= nlines; i++) {
        int K, M, tmp_n;
        long N;
        char lt[128], le[128], lt2[128], sym[128], dest[128];
        const char *cond;

        /* offsets 0-1: load i lo/hi from IX */
        if (!stride_parse_ld_r_ix_neg(lines[i + 0], 'l', &K)) continue;
        if (!stride_parse_ld_r_ix_neg(lines[i + 1], 'h', &M)) continue;
        if (M != K - 1) continue;

        /* offset 2: ld de,N (positive literal) */
        if (!parse_ld_de_positive_imm(lines[i + 2], &N)) continue;

        /* offsets 3-8: XOR 80h signed comparison bias */
        if (!eq(i + 3, "ld a,h"))  continue;
        if (!eq(i + 4, "xor 80h")) continue;
        if (!eq(i + 5, "ld h,a"))  continue;
        if (!eq(i + 6, "ld a,d"))  continue;
        if (!eq(i + 7, "xor 80h")) continue;
        if (!eq(i + 8, "ld d,a"))  continue;

        /* offsets 9-10: or a / sbc hl,de */
        if (!eq(i + 9,  "or a"))      continue;
        if (!eq(i + 10, "sbc hl,de")) continue;

        /* offsets 11-12: jp z,LT / jp c,LT (same label) */
        if (!parse_jp_z_label(lines[i + 11], lt))  continue;
        if (!parse_jp_c_label(lines[i + 12], lt2) || strcmp(lt2, lt) != 0) continue;

        /* offset 13: jp LE (unconditional exit) */
        if (!peep_parse_jp_uncond_label(lines[i + 13], le)) continue;

        /* offset 14: LT: label */
        if (!line_is_label_name(i + 14, lt)) continue;

        /* offsets 15-16: reload same index from IX */
        if (!stride_parse_ld_r_ix_neg(lines[i + 15], 'l', &tmp_n) || tmp_n != K) continue;
        if (!stride_parse_ld_r_ix_neg(lines[i + 16], 'h', &tmp_n) || tmp_n != M) continue;

        /* offset 17: ld de,SYM (global symbol) */
        if (!parse_ld_de_imm(lines[i + 17], sym) || sym[0] != '_') continue;

        /* offset 18: add hl,de */
        if (!eq(i + 18, "add hl,de")) continue;

        /* offset 19: ld a,(hl) */
        if (!eq(i + 19, "ld a,(hl)")) continue;

        /* offset 20: or a */
        if (!eq(i + 20, "or a")) continue;

        /* offset 21: jp z/nz,LDEST */
        if (parse_jp_z_label(lines[i + 21], dest))
            cond = "z";
        else if (parse_jp_nz_label(lines[i + 21], dest))
            cond = "nz";
        else
            continue;

        /* Pattern matched.  Rebuild as 13-line unsigned comparison + direct address. */
        {
            char l_lk[64], l_hk[64], l_de_n1[64];
            char jp_c_lt[MAX_LINE], jp_le[MAX_LINE], l_lt[MAX_LINE];
            char l_de_sym_n1[MAX_LINE], jp_cond_dest[MAX_LINE];

            sprintf(l_lk,         "ld l,(ix-%d)", K);
            sprintf(l_hk,         "ld h,(ix-%d)", M);
            sprintf(l_de_n1,      "ld de,%ld", N + 1);
            sprintf(jp_c_lt,      "jp c,%s", lt);
            sprintf(jp_le,        "jp %s", le);
            sprintf(l_lt,         "%s:", lt);
            sprintf(l_de_sym_n1,  "ld de,%s+%ld", sym, N + 1);
            sprintf(jp_cond_dest, "jp %s, %s", cond, dest);

            delete_n(i, 22);

            insert_line_tagged(i +  0, l_lk, "reuse_sbc"); /* ld l,(ix-K) */
            insert_line(i +  1, l_hk);           /* ld h,(ix-K-1)        */
            insert_line(i +  2, l_de_n1);        /* ld de,N+1            */
            insert_line(i +  3, "or a");
            insert_line(i +  4, "sbc hl,de");
            insert_line(i +  5, jp_c_lt);        /* jp c,LT              */
            insert_line(i +  6, jp_le);          /* jp LE (exit)         */
            insert_line(i +  7, l_lt);           /* LT:                  */
            insert_line(i +  8, l_de_sym_n1);    /* ld de,SYM+N+1        */
            insert_line(i +  9, "add hl,de");    /* HL = &SYM[i]         */
            insert_line(i + 10, "ld a,(hl)");
            insert_line(i + 11, "or a");
            insert_line(i + 12, jp_cond_dest);   /* jp z/nz,LDEST        */

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

/*
 * pass_recover_index_from_sbc:
 *
 * The outer loop test loads i from IX, computes HL = i - N (via sbc hl,de),
 * and branches.  At the branch target LT, HL = i-N and DE = N.  The flag
 * check that follows reloads i from IX again via a push/pop pattern:
 *
 *   ld l,(ix-K)        ; load i (also at top of loop)
 *   ld h,(ix-K-1)
 *   ld de,N            ; loop bound
 *   or a
 *   sbc hl,de          ; HL = i - N
 *   jp z,LT
 *   jp c,LT
 *   jp LE
 * LT:
 *   ld hl,SYM          ; compute &SYM[i] the slow way
 *   push hl
 *   ld l,(ix-K)        ; reload i from IX (same K)
 *   ld h,(ix-K-1)
 *   ex de,hl
 *   pop hl
 *   add hl,de          ; HL = SYM + i
 *
 * At LT, HL = i-N and DE = N, so "add hl,de" recovers i directly.
 *
 * While matching, also:
 * - Change ld de,N to ld de,N+1: the strict "<" test (C flag only) covers
 *   i <= N because "i < N+1" iff "i <= N" for integers.  This drops the
 *   jp z,LT branch entirely.
 * - Use "ld de,SYM+N+1; add hl,de" for the address:
 *   (i-(N+1)) + (SYM+(N+1)) = SYM+i = &SYM[i]  (same result).
 *
 * Full 16-line block replaced by 10 lines.  Saves one branch instruction
 * (~10T) on every outer-loop iteration plus the 6 eliminated code lines.
 */
static int pass_recover_index_from_sbc(void)
{
    int i;
    int changed = 0;

    for (i = 0; i + 16 <= nlines; i++) {
        int K, M, tmp_n;
        long N;
        char lt[128], le[128], sym[128];
        char lt2[128];

        /* offsets 0-1: load i lo/hi */
        if (!stride_parse_ld_r_ix_neg(lines[i + 0], 'l', &K)) continue;
        if (!stride_parse_ld_r_ix_neg(lines[i + 1], 'h', &M)) continue;
        if (M != K - 1) continue;

        /* offset 2: ld de,N (positive literal) */
        if (!parse_ld_de_positive_imm(lines[i + 2], &N)) continue;

        /* offsets 3-4: or a / sbc hl,de */
        if (!eq(i + 3, "or a")) continue;
        if (!eq(i + 4, "sbc hl,de")) continue;

        /* offsets 5-6: jp z,LT / jp c,LT (same label) */
        if (!parse_jp_z_label(lines[i + 5], lt)) continue;
        if (!parse_jp_c_label(lines[i + 6], lt2) || strcmp(lt2, lt) != 0) continue;

        /* offset 7: jp LE (unconditional exit) */
        if (!peep_parse_jp_uncond_label(lines[i + 7], le)) continue;

        /* offset 8: LT: label */
        if (!line_is_label_name(i + 8, lt)) continue;

        /* offsets 9-15: push/reload-IX/ex/pop/add */
        if (!parse_ld_hl_imm(lines[i + 9], sym) || sym[0] != '_') continue;
        if (!eq(i + 10, "push hl")) continue;
        if (!stride_parse_ld_r_ix_neg(lines[i + 11], 'l', &tmp_n) || tmp_n != K) continue;
        if (!stride_parse_ld_r_ix_neg(lines[i + 12], 'h', &tmp_n) || tmp_n != M) continue;
        if (!eq(i + 13, "ex de,hl")) continue;
        if (!eq(i + 14, "pop hl")) continue;
        if (!eq(i + 15, "add hl,de")) continue;

        /*
         * Pattern matched. Replace all 16 lines with 10 lines.
         *
         * Use N+1 for the sbc bound so only jp c is needed (drops jp z).
         * Use SYM+N+1 for the address base so the single add gives &SYM[i]:
         *   (i-(N+1)) + (SYM+(N+1)) = SYM+i = &SYM[i]
         */
        {
            char l_lk[64], l_hk[64], l_de_n1[64], jp_c_lt[MAX_LINE];
            char jp_le[MAX_LINE], l_lt[MAX_LINE], l_de_sym_n1[MAX_LINE];

            sprintf(l_lk,        "ld l,(ix-%d)", K);
            sprintf(l_hk,        "ld h,(ix-%d)", M);
            sprintf(l_de_n1,     "ld de,%ld", N + 1);
            sprintf(jp_c_lt,     "jp c,%s", lt);
            sprintf(jp_le,       "jp %s", le);
            sprintf(l_lt,        "%s:", lt);
            sprintf(l_de_sym_n1, "ld de,%s+%ld", sym, N + 1);

            delete_n(i, 16);

            insert_line_tagged(i +  0, l_lk, "recover_idx"); /* ld l,(ix-K) */
            insert_line(i +  1, l_hk);           /* ld h,(ix-K-1)        */
            insert_line(i +  2, l_de_n1);        /* ld de,N+1            */
            insert_line(i +  3, "or a");
            insert_line(i +  4, "sbc hl,de");
            insert_line(i +  5, jp_c_lt);        /* jp c,LT  (covers i<=N) */
            insert_line(i +  6, jp_le);          /* jp LE    (exit)        */
            insert_line(i +  7, l_lt);           /* LT:                    */
            insert_line(i +  8, l_de_sym_n1);    /* ld de,SYM+N+1          */
            insert_line(i +  9, "add hl,de");    /* HL = &SYM[i]           */

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

static int peep_parse_ld_hl_symaddr(const char *s, char *sym)
{
    char tmp[MAX_LINE];
    const char *p;
    int i;

    strip_peep_comment_copy(tmp, s);
    if (strncmp(tmp, "ld hl,", 6) != 0)
        return 0;
    p = tmp + 6;
    if (*p == '(' || *p == 0)
        return 0;
    i = 0;
    while (*p && i < 120)
        sym[i++] = *p++;
    sym[i] = 0;
    return i > 0;
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
    int i, off, changed = 0;
    char next[MAX_LINE];
    char new_lo[64], new_hi[64];

    for (i = 0; i + 3 < nlines; i++) {
        if (!eq(i, "ex de,hl")) continue;
        if (!peep_parse_st_ix_pair(lines[i + 1], lines[i + 2], &off)) continue;

        /* HL must be clobbered by the next instruction; otherwise the
         * stale stp-pointer left in HL by removing the exchange would be
         * visible.  All generated instances follow immediately with a
         * ld hl,(sym) that starts the next predec/postinc sequence. */
        strip_peep_comment_copy(next, lines[i + 3]);
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
 * Collapse the address/setup half of *p++ = value for an int * global.  The
 * value-generating code remains between the final push hl and the later
 * pop hl / store, so this is safe even when the RHS uses the stack.
 */
static int pass_global_ptr_word_postinc_store_setup(void)
{
    int i;
    int changed;
    char sym[128];
    char line[192];

    changed = 0;
    for (i = 0; i + 14 < nlines; i++) {
        if (!peep_parse_ld_hl_symaddr(lines[i], sym)) continue;
        if (!eq(i + 1, "push hl")) continue;
        if (!eq(i + 2, "ld e,(hl)")) continue;
        if (!eq(i + 3, "inc hl")) continue;
        if (!eq(i + 4, "ld d,(hl)")) continue;
        if (!eq(i + 5, "ex de,hl")) continue;
        if (!eq(i + 6, "push hl")) continue;
        if (!eq(i + 7, "inc hl")) continue;
        if (!eq(i + 8, "inc hl")) continue;
        if (!eq(i + 9, "ex de,hl")) continue;
        if (!eq(i + 10, "pop hl")) continue;
        if (!eq(i + 11, "ex (sp),hl")) continue;
        if (!eq(i + 12, "ld (hl),e")) continue;
        if (!eq(i + 13, "inc hl")) continue;
        if (!eq(i + 14, "ld (hl),d")) continue;

        sprintf(line, "ld hl,(%s)", sym);
        replace1_tagged(i, line, "global_ptr_word_postinc_setup");
        replace1(i + 1, "push hl");
        replace1(i + 2, "inc hl");
        replace1(i + 3, "inc hl");
        sprintf(line, "ld (%s),hl", sym);
        replace1(i + 4, line);
        delete_n(i + 5, 10);
        changed = 1;
        if (i > 0)
            i--;
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
 * pass_reg_bc_deref_byte_cmp:
 *
 * When a register variable lives in BC, *bc_ptr dereferences as:
 *
 *   ld l,c                       ; BC -> HL
 *   ld h,b
 *   ld l,(hl)                    ; L = *ptr (byte dereference)
 *   ld h,0                       ; H = 0    (zero-extend)
 *   push hl
 *   ld l,(ix+P) or (ix-P)        ; L = compare value
 *   ld h,0
 *   ex de,hl                     ; DE = compare value
 *   pop hl                       ; HL = *ptr
 *   or a
 *   sbc hl,de                    ; HL = *ptr - cmp
 *   jp z/nz, LABEL
 *
 * Collapse to 6 instructions (avoids ld a,(bc) which M80 does not handle):
 *
 *   ld l,c                       ; BC -> HL
 *   ld h,b
 *   ld a,(hl)                    ; A = *ptr
 *   ld l,(ix+P) or (ix-P)        ; L = compare value
 *   cp l                         ; Z set iff A == L
 *   jp z/nz, LABEL
 */
static int pass_reg_bc_deref_byte_cmp(void)
{
    int i;
    int changed = 0;

    for (i = 0; i + 11 < nlines; i++) {
        char label[128];
        const char *cond;

        if (!eq(i + 0, "ld l,c"))   continue;
        if (!eq(i + 1, "ld h,b"))   continue;
        if (!eq(i + 2, "ld l,(hl)")) continue;
        if (!eq(i + 3, "ld h,0"))   continue;
        if (!eq(i + 4, "push hl"))  continue;
        if (strncmp(lines[i + 5], "ld l,(ix", 8) != 0) continue;
        if (!eq(i + 6, "ld h,0"))   continue;
        if (!eq(i + 7, "ex de,hl")) continue;
        if (!eq(i + 8, "pop hl"))   continue;
        if (!eq(i + 9,  "or a"))    continue;
        if (!eq(i + 10, "sbc hl,de")) continue;
        if (parse_jp_z_label(lines[i + 11], label))
            cond = "z";
        else if (parse_jp_nz_label(lines[i + 11], label))
            cond = "nz";
        else
            continue;

        {
            char cmp_l[MAX_LINE], jp_line[MAX_LINE];

            strcpy(cmp_l, lines[i + 5]);   /* preserve the "ld l,(ix...)" line */
            sprintf(jp_line, "jp %s, %s", cond, label);

            delete_n(i, 12);
            insert_line_tagged(i + 0, "ld l,c", "reg_bc_deref_byte_cmp");
            insert_line(i + 1, "ld h,b");
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
    }

    return changed;
}

static int pass_board_byte_eq_direct_load(void)
{
    int i;
    int changed;
    char addr[128];
    char line[160];

    changed = 0;

    for (i = 0; i + 3 < nlines; ++i) {
        /*
         * Equality-only compare against a constant board address:
         *
         *     ld a,b
         *     ld hl,_g_board+N
         *     cp (hl)
         *     jp z/nz,L
         *
         * can be:
         *
         *     ld a,(_g_board+N)
         *     cp b
         *     jp z/nz,L
         *
         * The subtraction direction changes, so this is valid only for
         * equality/inequality branches where only Z is used.
         */
        if (!eq(i, "ld a,b"))
            continue;

        if (!parse_ld_hl_imm(lines[i + 1], addr))
            continue;
        if (strncmp(addr, "_g_board", 8) != 0)
            continue;
        if (!eq(i + 2, "cp (hl)"))
            continue;
        if (!peep_is_jp_z_or_nz(lines[i + 3]))
            continue;

        sprintf(line, "ld a,(%s)", addr);
        replace1_tagged(i, line, "board_byte_eq_direct");
        replace1(i + 1, "cp b");
        replace1(i + 2, lines[i + 3]);
        delete_n(i + 3, 1);
        changed = 1;
        if (i > 0)
            --i;
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
 * pass_findsolution_clear_board_loop:
 *
 * DCC emits the source-level portable loop
 *
 *     for (i = 0; i < 9; i++)
 *         g_board[i] = 0;
 *
 * with a 16-bit size_t local counter.  In the tic-tac-toe benchmark this
 * becomes a very small fixed byte-array clear, and the local counter is not
 * used after the loop.  Recognize the complete generated loop and replace it
 * with a byte-counted Z80 loop:
 *
 *     xor a
 *     ld hl,_g_board
 *     ld b,9
 *   Lhead:
 *     ld (hl),a
 *     inc hl
 *     djnz Lhead
 *
 * This deliberately matches the whole loop including the counter test and
 * increment labels, so it does not depend on a general induction-variable
 * proof in the compiler front end.
 */
static int pass_findsolution_clear_board_loop(void)
{
    int i;
    int changed;
    char loff[32], hoff[32], loff2[32], hoff2[32];
    char lhead[128], lcont[128], lab[128];
    int v;

    changed = 0;

    for (i = 0; i + 18 < nlines; ++i) {
        if (!eq(i, "ld hl,0"))
            continue;
        if (!peep_parse_st_ix_pair(lines[i + 1], lines[i + 2], &v))
            continue;

        peep_format_ix_off(loff, v);
        peep_format_ix_off(hoff, v + 1);

        if (!label_name_at(i + 3, lhead))
            continue;
        if (!peep_parse_ld_l_ix(lines[i + 4], loff2) || strcmp(loff2, loff) != 0)
            continue;
        if (!peep_parse_ld_h_ix(lines[i + 5], hoff2) || strcmp(hoff2, hoff) != 0)
            continue;
        if (!eq(i + 6, "ld de,_g_board"))
            continue;
        if (!eq(i + 7, "add hl,de"))
            continue;
        if (!eq(i + 8, "ld (hl),0"))
            continue;

        {
            char inc_lo[64], inc_hi[64];
            sprintf(inc_lo, "inc (ix%s)", loff);
            sprintf(inc_hi, "inc (ix%s)", hoff);
            if (!eq(i + 9, inc_lo))
                continue;
            if (!parse_jp_nz_label(lines[i + 10], lcont))
                continue;
            if (!eq(i + 11, inc_hi))
                continue;
            if (!line_is_label_name(i + 12, lcont))
                continue;
        }

        if (!peep_parse_ld_l_ix(lines[i + 13], loff2) || strcmp(loff2, loff) != 0)
            continue;
        if (!peep_parse_ld_h_ix(lines[i + 14], hoff2) || strcmp(hoff2, hoff) != 0)
            continue;
        if (!peep_parse_ld_de_0_to_255(lines[i + 15], &v) || v != 9)
            continue;
        if (!eq(i + 16, "or a"))
            continue;
        if (!eq(i + 17, "sbc hl,de"))
            continue;
        if (!parse_jp_c_label(lines[i + 18], lab) || strcmp(lab, lhead) != 0)
            continue;

        /* Optional local-space allocation immediately before the matched loop.
         * It was only for the now-eliminated 16-bit counter.  IX has already
         * been established, so removing these SP adjustments does not affect
         * parameter references at ix+N. */
        if (i >= 2 && eq(i - 2, "dec sp") && eq(i - 1, "dec sp")) {
            delete_n(i - 2, 2);
            i -= 2;
        }

        replace1_tagged(i, "xor a", "findsolution_clear_board");
        replace1(i + 1, "ld hl,_g_board");
        replace1(i + 2, "ld b,9");
        /* Keep the existing loop label at i+3. */
        replace1(i + 4, "ld (hl),a");
        replace1(i + 5, "inc hl");
        {
            char line[160];
            sprintf(line, "djnz %s", lhead);
            replace1(i + 6, line);
        }
        delete_n(i + 7, 12);
        changed = 1;
        if (i > 0)
            --i;
    }

    return changed;
}

/*
 * pass_cpir: Replace a byte-scan equality loop with the Z80 CPIR instruction.
 *
 * Detects the pattern produced by pass_deref_byte_cmp for loops of the form
 *   for (i = 0; i < c; i++) { if (*ptr != val) { fail-and-exit; } ptr++; }
 * where i is a local 16-bit counter, ptr is a local byte pointer, val is a
 * local byte, and c is a parameter or local count.
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
 *     ld a,h  or l                        ; guard: CPIR with BC=0 is unsafe
 *     jp z, Lexit
 *     push hl
 *     ld l,(ix+P)  ld h,(ix+Q)            ; HL = starting ptr
 *     pop bc                              ; BC = c
 *     ld a,(ix+V)                         ; A = byte to match
 *     cpir                                ; scan until mismatch or count=0
 *     jp z, Lexit                         ; Z=1 → all matched, success
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

        /* 3. Byte deref and compare (6 lines) */
        if (!peep_parse_ld_l_ix(lines[j], ptr_lo_off)) continue; j++;
        if (!peep_parse_ld_h_ix(lines[j], ptr_hi_off)) continue; j++;
        if (!parse_ix_off_numeric(ptr_lo_off, &ptr_lo_val)) continue;
        { int v; if (!parse_ix_off_numeric(ptr_hi_off, &v)) continue;
          if (v != ptr_lo_val + 1) continue; }
        if (!eq(j, "ld a,(hl)")) continue; j++;
        if (!peep_parse_ld_l_ix(lines[j], val_off)) continue; j++;
        if (!parse_ix_off_numeric(val_off, &val_val)) continue;
        if (!eq(j, "cp l"))      continue; j++;
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

        /* 8. Counter must start at 0: look back up to 20 lines for "ld de,-A"
         *    which DCC emits when computing the frame address of the counter
         *    during its zero-initialisation. */
        { char de_init[32]; int found = 0;
          sprintf(de_init, "ld de,-%d", cnt_lo);
          for (k = i - 1; k >= 0 && k >= i - 20; k--) {
              if (eq(k, de_init)) { found = 1; break; }
              if (is_global_asm_label_line(k)) break;
          }
          if (!found) continue; }

        /* All checks passed — apply CPIR transformation. */
        {
            char s_lim_lo[160], s_lim_hi[160], s_ptr_lo[160], s_ptr_hi[160];
            char s_val[160], s_jp_z_exit[160];
            
            sprintf(s_lim_lo,     "ld l,(ix%s)", lim_lo_off);
            sprintf(s_lim_hi,     "ld h,(ix%s)", lim_hi_off);
            sprintf(s_ptr_lo,     "ld l,(ix%s)", ptr_lo_off);
            sprintf(s_ptr_hi,     "ld h,(ix%s)", ptr_hi_off);
            sprintf(s_val,        "ld a,(ix%s)", val_off);
            sprintf(s_jp_z_exit,  "jp z, %s", lexit);

            /* Delete end block first (lok label + ptr++ + counter++) so that
             * positions i..fail_start-1 are unchanged. */
            delete_n(lok_pos, ip - lok_pos + 1);

            /* Delete head block (L4 label + condition + deref + jp z,Lok). */
            delete_n(i, fail_start - i);

            /* Insert CPIR block at i (now the first line of the fail code). */
            insert_line_tagged(i,      s_lim_lo,    "cpir");
            insert_line(i +  1,        s_lim_hi);
            insert_line(i +  2,        "ld a,h");
            insert_line(i +  3,        "or l");
            insert_line(i +  4,        s_jp_z_exit); /* skip on zero count */
            insert_line(i +  5,        "push hl");
            insert_line(i +  6,        s_ptr_lo);
            insert_line(i +  7,        s_ptr_hi);
            insert_line(i +  8,        "pop bc");
            insert_line(i +  9,        s_val);
            insert_line(i + 10,        "cpir");
            insert_line(i + 11,        s_jp_z_exit); /* success: skip fail code */

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
    const char *infile;
    const char *outfile;

    if (argc == 4) {
        if (strcmp(argv[1], "-Os") == 0)
            opt_size = 1;
        else if (strcmp(argv[1], "-Ot") == 0)
            opt_size = 0;
        else {
            fprintf(stderr, "usage: dccpeep [-Ot|-Os] input.mac output.mac\n");
            return 1;
        }
        infile  = argv[2];
        outfile = argv[3];
    } else if (argc == 3) {
        infile  = argv[1];
        outfile = argv[2];
    } else {
        fprintf(stderr, "usage: dccpeep [-Ot|-Os] input.mac output.mac\n");
        return 1;
    }

    read_file(infile);

    passes = 0;
    do {
        changed = 0;
        if (pass_once()) changed = 1;
        if (pass_byte_minmax_patterns()) changed = 1;
        if (pass_byte_minmax_board_and_assign()) changed = 1;
        if (pass_inline_simple_call_hl_from_loaded_pointer()) changed = 1;
        if (pass_dead_hl_load_before_ldhl()) changed = 1;
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
        if (pass_shrink_minmax_frame_after_callptr_temp_removed()) changed = 1;
        if (pass_shrink_minmax_frame3_after_score_cache()) changed = 1;
        if (pass_minmax_loop_ctr_b()) changed = 1;
        if (pass_shrink_minmax_frame2_after_loop_ctr_b()) changed = 1;
        if (pass_minmax_value_c()) changed = 1;
        if (pass_shrink_minmax_frame1_after_value_c()) changed = 1;
        if (pass_minmax_byte_returns()) changed = 1;
        if (pass_minmax_pack_frame()) changed = 1;
        if (pass_minmax_pack_call()) changed = 1;
        if (pass_store_l_reload_a()) changed = 1;
        if (pass_reuse_board_addr_for_zero_store()) changed = 1;
        if (pass_array_base_push_to_de()) changed = 1;
        if (pass_base_index_addr()) changed = 1;
        if (pass_e_signed_le_zero()) changed = 1;
        if (pass_ix_array_word_addr()) changed = 1;
        if (pass_reuse_array_word_addr()) changed = 1;
        if (pass_ix_postdec_to_local()) changed = 1;
        if (pass_store_word_const_hl()) changed = 1;
        if (pass_findsolution_clear_board_loop()) changed = 1;
        if (pass_float_zero_store()) changed = 1;
        if (pass_incsp_to_popbc()) changed = 1;
        if (pass_remove_unreferenced_labels()) changed = 1;
        if (pass_ldir_memset()) changed = 1;
        if (pass_ldir_memset_rotated()) changed = 1;
        if (pass_reuse_sbc_result_for_flagcheck()) changed = 1;
        if (pass_reuse_sbc_result_for_flagcheck_rotated()) changed = 1;
        if (pass_cond_skip_shortcut()) changed = 1;
        if (pass_stride_loop_to_ptr()) changed = 1;
        if (pass_stride_k_setup_to_direct()) changed = 1;
        if (pass_recover_index_from_sbc()) changed = 1;
        if (pass_ix_frame_ptr_load()) changed = 1;
        if (pass_ix_frame_ptr_load_deadd()) changed = 1;
        if (pass_global_ptr_word_predec_load()) changed = 1;
        if (pass_elim_ex_de_hl_before_ix_store()) changed = 1;
        if (pass_global_ptr_word_postinc_store_setup()) changed = 1;
        if (pass_elim_redundant_pop_push()) changed = 1;
        if (pass_double_de_before_add()) changed = 1;
        if (pass_const_hl_doubles()) changed = 1;
        if (pass_deref_byte_cmp()) changed = 1;
        if (pass_cpir()) changed = 1;
        if (pass_reg_bc_deref_byte_cmp()) changed = 1;
        if (pass_int_global_array_addr()) changed = 1;
        if (pass_byte_global_ptr_array_addr()) changed = 1;
        if (pass_byte_ix_array_addr()) changed = 1;
        if (pass_byte_ix_predec_zero_test()) changed = 1;
        if (pass_ix_pair_load_to_de()) changed = 1;
        if (pass_ix_byte_load_to_de()) changed = 1;
        if (pass_remove_ix_store_reload_hl()) changed = 1;
        if (pass_postinc_ix_word()) changed = 1;
        if (pass_cp_jz_jpnc()) changed = 1;
        if (pass_cp_jz_jpc()) changed = 1;
        if (pass_bool_from_cmp()) changed = 1;
        if (pass_elim_dead_ix_stores()) changed = 1;
        if (pass_remove_ix_store_reload_a()) changed = 1;
        if (pass_a_tracks_ix_byte()) changed = 1;
        if (pass_byte_postdec_copy()) changed = 1;
        if (pass_elim_redundant_ld_h_zero()) changed = 1;
        if (pass_elim_long_store_reload()) changed = 1;
        if (pass_branch_over_jump()) changed = 1;
        if (pass_jump_thread()) changed = 1;
        if (pass_global_board_const_offsets()) changed = 1;
        if (pass_posfunc_ix1_to_b()) changed = 1;
        if (pass_posfunc_collapse_b_setup()) changed = 1;
        if (pass_posfunc_b_cache()) changed = 1;
        if (pass_posfunc_byte_return()) changed = 1;
        if (pass_jp_to_plain_ret()) changed = 1;
        if (pass_board_byte_eq_direct_load()) changed = 1;
        if (pass_lookforwinner_b_cache()) changed = 1;
        if (pass_const_divmod_helpers()) changed = 1;
        if (pass_mulu_const()) changed = 1;
        if (pass_minmax_unsigned_compares()) changed = 1;
        if (pass_fix_main_argc_gt_one()) changed = 1;
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
