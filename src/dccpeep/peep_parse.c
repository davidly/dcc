/* peep_parse.c - stateless Z80/M80 instruction parsers and formatters. */
#include "dccpeep_internal.h"

int parse_ld_hl_imm(const char *s, char *val, size_t val_size)
{
    const char *p;
    const char *operand;
    size_t operand_len;
    char tmp[MAX_LINE];

    strip_peep_comment_copy(tmp, s);
    p = "ld hl,";
    if (strncmp(tmp, p, strlen(p)) != 0)
        return 0;

    operand = tmp + strlen(p);
    operand_len = strlen(operand);
    if (operand_len >= val_size)
        return 0;
    memcpy(val, operand, operand_len + 1);
    return 1;
}

int parse_ld_de_imm(const char *s, char *val, size_t val_size)
{
    const char *p;
    const char *operand;
    size_t operand_len;
    char tmp[MAX_LINE];

    strip_peep_comment_copy(tmp, s);
    p = "ld de,";
    if (strncmp(tmp, p, strlen(p)) != 0)
        return 0;

    operand = tmp + strlen(p);
    operand_len = strlen(operand);
    if (operand_len >= val_size)
        return 0;
    memcpy(val, operand, operand_len + 1);
    return 1;
}

int parse_nonneg_int(const char *s, int *out)
{
    int v;

    if (*s < '0' || *s > '9')
        return 0;

    v = 0;
    while (*s >= '0' && *s <= '9') {
        int digit = *s - '0';
        if (v > (INT_MAX - digit) / 10)
            return 0;
        v = v * 10 + digit;
        s++;
    }

    if (*s != 0)
        return 0;

    *out = v;
    return 1;
}

int parse_jp_cond_label(const char *s, const char *cond, char *label)
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

int parse_jp_z_label(const char *s, char *label)
{
    return parse_jp_cond_label(s, "z", label);
}

int parse_jp_nz_label(const char *s, char *label)
{
    return parse_jp_cond_label(s, "nz", label);
}

int parse_jp_c_label(const char *s, char *label)
{
    return parse_jp_cond_label(s, "c", label);
}

int parse_jp_nc_label(const char *s, char *label)
{
    return parse_jp_cond_label(s, "nc", label);
}

int parse_ld_de_positive_imm(const char *s, long *out)
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

int peep_parse_jp_cond_label(const char *s, const char *cond, char *lab)
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

int peep_parse_jp_uncond_label(const char *s, char *lab)
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

void peep_make_cond_jump(char *out, size_t size, const char *cond, const char *lab)
{
    snprintf(out, size, "jp %s, %s", cond, lab);
}

int peep_parse_any_cond_jump(const char *s, char *cond, char *lab)
{
    if (peep_parse_jp_cond_label(s, "z", lab)) { strcpy(cond, "z"); return 1; }
    if (peep_parse_jp_cond_label(s, "nz", lab)) { strcpy(cond, "nz"); return 1; }
    if (peep_parse_jp_cond_label(s, "c", lab)) { strcpy(cond, "c"); return 1; }
    if (peep_parse_jp_cond_label(s, "nc", lab)) { strcpy(cond, "nc"); return 1; }
    return 0;
}

const char *peep_inverse_cond(const char *cond)
{
    if (!strcmp(cond, "z")) return "nz";
    if (!strcmp(cond, "nz")) return "z";
    if (!strcmp(cond, "c")) return "nc";
    if (!strcmp(cond, "nc")) return "c";
    return NULL;
}

int peep_parse_ld_l_ix(const char *s, char *off)
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

int peep_is_jp_z_or_nz(const char *s)
{
    return strncmp(s, "jp z,", 5) == 0 || strncmp(s, "jp nz,", 6) == 0;
}

void peep_make_ld_a_ix(char *out, const char *off)
{
    sprintf(out, "ld a,(ix%s)", off);
}

int peep_parse_ld_ix_a(const char *s, char *off)
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

int peep_parse_ld_a_ix(const char *s, char *off)
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

int peep_parse_ld_de_0_to_255(const char *s, int *out)
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

int peep_parse_ld_de_signed(const char *s, int *out)
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

void peep_format_ix_off(char *buf, int off)
{
    if (off >= 0)
        sprintf(buf, "+%d", off);
    else
        sprintf(buf, "%d", off);
}

int peep_parse_ld_e_imm8(const char *s, int *out)
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

int peep_parse_ld_hl_0_to_255(const char *s, int *out)
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

int peep_parse_ld_h_ix(const char *s, char *off)
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

int peep_parse_ld_e_ix(const char *s, char *off)
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

int peep_parse_ld_d_ix(const char *s, char *off)
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

int peep_parse_ld_ix_pair(const char *s1, const char *s2, int *off)
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

int peep_parse_st_ix_pair(const char *s1, const char *s2, int *off)
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

int peep_parse_jp_same_z_c(int iz, int ic, char *lab)
{
    char lab2[128];

    if (!peep_parse_jp_cond_label(lines[iz], "z", lab))
        return 0;
    if (!peep_parse_jp_cond_label(lines[ic], "c", lab2))
        return 0;
    return strcmp(lab, lab2) == 0;
}

int peep_parse_dec_ix_byte(const char *s, int *off)
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

int peep_parse_ld_ix_byte_imm(const char *s, int *off, int *val)
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

int peep_parse_inc_ix_byte(const char *s, int *off)
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

int peep_parse_cp_const(const char *s, int *val)
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

int peep_parse_st_ix_de_pair(const char *s1, const char *s2, int *off)
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

int peep_parse_ld_hl_paren_sym(const char *s, char *sym)
{
    char tmp[MAX_LINE];
    const char *p;
    int i;

    /* Never match a line pass_fold_hl_label_word_deref produced: that pass
     * collapses a struct field's address-then-dereference into this same
     * "ld hl,(X)" text, but X there is a field offset expression
     * (e.g. "_Z0002+96"), not the bare global-variable symbol this
     * function's callers assume - global_write_count_in_file and
     * symbol_written_in_range below both do literal-text lookups against
     * sym elsewhere in the file, which isn't a meaningful safety check for
     * "some field of struct _Z0002 at this offset". Confirmed as a real
     * miscompile on tests/cint.c (eu.cin produced wrong output): before
     * this exclusion, the global-word-cache pass below started firing on
     * these newly-folded lines - the exact mechanism of the resulting bad
     * codegen wasn't traced further once excluding this tag was confirmed
     * (via a before/after diff of the two passes' combined output) to be
     * both necessary and sufficient to fix it and restore this pass's
     * trigger set to exactly what it was before
     * pass_fold_hl_label_word_deref existed. */
    if (strstr(s, "fold_hl_label_word_deref"))
        return 0;

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

/* Same as peep_parse_ld_hl_paren_sym above, but for "ld de,(NAME)" - see
 * that function's own comment for why the fold_hl_label_word_deref
 * exclusion matters (no DE-producing pass currently emits a struct-field
 * offset form the same way, but excluding it here too costs nothing and
 * keeps this parser exactly as conservative as its HL sibling). */
int peep_parse_ld_de_paren_sym(const char *s, char *sym)
{
    char tmp[MAX_LINE];
    const char *p;
    int i;

    if (strstr(s, "fold_hl_label_word_deref"))
        return 0;

    strip_peep_comment_copy(tmp, s);
    if (strncmp(tmp, "ld de,(", 7) != 0)
        return 0;
    p = tmp + 7;
    i = 0;
    while (*p && *p != ')' && i < 120)
        sym[i++] = *p++;
    sym[i] = 0;
    return i > 0 && *p == ')' && p[1] == 0;
}

int peep_parse_ld_paren_sym_hl(const char *s, char *sym)
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

int parse_ix_off_numeric(const char *off, int *val)
{
    char *endp;
    if (off[0] == 0) return 0;
    *val = (int)strtol(off, &endp, 10);
    return *endp == 0;
}

int parse_ld_reg16_dest(const char *s, char *out)
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

