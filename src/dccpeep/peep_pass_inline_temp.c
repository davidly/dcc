/**
 * @file peep_pass_inline_temp.c
 * @brief Rewrites compiler-tagged inline-temporary spills.
 *
 * @par Role
 * Consumes dcc inline-temporary markers, proves safe register-bank or stack
 * substitutions for their memory spills, applies those rewrites, and removes
 * markers that survive optimization.
 *
 * @par Key entry points
 * pass_inline_temp_spill_to_stack() and pass_remove_inline_temp_markers().
 *
 * @par Boundary
 * This module acts only on compiler-emitted marker contracts; it does not
 * discover arbitrary spills. dccpeep.c controls rewrite and marker-removal
 * ordering.
 */
#include "dccpeep_internal.h"

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
int pass_inline_temp_spill_to_stack(void)
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

int pass_remove_inline_temp_markers(void)
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
