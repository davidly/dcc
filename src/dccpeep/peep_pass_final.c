/**
 * @file peep_pass_final.c
 * @brief Implements post-convergence and terminal cleanup passes.
 *
 * @par Role
 * Uses the settled instruction stream to fold constant sign extension,
 * remove dead register loads, epilogue pops, and carry clears, widen tiny
 * local-allocation rewrites, and relax safely in-range JP instructions to JR.
 *
 * @par Key entry points
 * pass_jp_to_jr(), pass_fold_const_sign_extend(),
 * pass_elim_dead_register_loads(), pass_elim_redundant_carry_clear(), and
 * pass_local_alloc_wide().
 *
 * @par Boundary
 * These passes depend on converged shapes, final code size, effects, or
 * liveness and therefore run only at dedicated sites in dccpeep.c; they do
 * not own phase ordering or general fixed-point optimization.
 */
#include "dccpeep_internal.h"

static int instr_size_upper(int line_index)
{
    const PeepLineInfo *info = peep_line_info(line_index);
    const char *s = user_asm_original[line_index] != NULL
        ? user_asm_original[line_index] : lines[line_index];
    /* Labels, comments, blank lines and assembler directives emit no code. */
    if (info && (info->kind == PEEP_LINE_BLANK ||
                 info->kind == PEEP_LINE_COMMENT ||
                 info->kind == PEEP_LINE_LABEL))
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
    if ((info && info->opcode == PEEP_OPCODE_JR) || strncmp(s, "djnz", 4) == 0)
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

typedef struct LabelIndexEntry {
    const char *definition;
    int line;
} LabelIndexEntry;

static int compare_label_entries(const void *left, const void *right)
{
    const LabelIndexEntry *a = (const LabelIndexEntry *)left;
    const LabelIndexEntry *b = (const LabelIndexEntry *)right;
    int order = strcmp(a->definition, b->definition);

    if (order != 0)
        return order;
    return (a->line > b->line) - (a->line < b->line);
}

static int find_label_line(const LabelIndexEntry *labels, int count,
                           const char *definition)
{
    int lo = 0;
    int hi = count;

    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (strcmp(labels[mid].definition, definition) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < count && strcmp(labels[lo].definition, definition) == 0)
        return labels[lo].line;
    return -1;
}

static int branch_skips_return(int branch, int target)
{
    int i;

    if (target <= branch)
        return 0;
    for (i = branch + 1; i < target; ++i) {
        char clean[MAX_LINE];
        strip_peep_comment_copy(clean, lines[i]);
        if (strcmp(clean, "ret") == 0)
            return 1;
    }
    return 0;
}

static int likely_zero_call_result(int branch)
{
    char label[128];
    char clean[MAX_LINE];
    int i;

    if (branch < 3 || !parse_jp_z_label(lines[branch], label))
        return 0;
    strip_peep_comment_copy(clean, lines[branch - 1]);
    if (strcmp(clean, "or l") != 0)
        return 0;
    strip_peep_comment_copy(clean, lines[branch - 2]);
    if (strcmp(clean, "ld a,h") != 0)
        return 0;

    for (i = branch - 3; i >= 0 && i >= branch - 7; --i) {
        strip_peep_comment_copy(clean, lines[i]);
        if (strncmp(clean, "call ", 5) == 0)
            return 1;
        if (starts_label(lines[i]) || peep_is_public_line(lines[i]))
            break;
    }
    return 0;
}

static int likely_nonzero_call_result(int branch)
{
    char label[128];
    char clean[MAX_LINE];
    int i;

    if (branch < 3 || !parse_jp_nz_label(lines[branch], label))
        return 0;
    strip_peep_comment_copy(clean, lines[branch - 1]);
    if (strcmp(clean, "or l") != 0)
        return 0;
    strip_peep_comment_copy(clean, lines[branch - 2]);
    if (strcmp(clean, "ld a,h") != 0)
        return 0;
    for (i = branch - 3; i >= 0 && i >= branch - 7; --i) {
        strip_peep_comment_copy(clean, lines[i]);
        if (strncmp(clean, "call ", 5) == 0)
            return 1;
        if (starts_label(lines[i]) || peep_is_public_line(lines[i]))
            break;
    }
    return 0;
}

static int likely_nonzero_word_parameter(int branch)
{
    char label[128];
    char clean[MAX_LINE];

    if (branch < 4 || !parse_jp_nz_label(lines[branch], label))
        return 0;
    strip_peep_comment_copy(clean, lines[branch - 1]);
    if (strcmp(clean, "or l") != 0)
        return 0;
    strip_peep_comment_copy(clean, lines[branch - 2]);
    if (strcmp(clean, "ld a,h") != 0)
        return 0;
    strip_peep_comment_copy(clean, lines[branch - 3]);
    if (strncmp(clean, "ld h,(ix+", 9) != 0)
        return 0;
    strip_peep_comment_copy(clean, lines[branch - 4]);
    return strncmp(clean, "ld l,(ix+", 9) == 0;
}

static int branch_skips_call_to_epilogue(int branch, int target)
{
    int i;
    int saw_call = 0;

    if (target <= branch)
        return 0;
    for (i = branch + 1; i < target; ++i) {
        char clean[MAX_LINE];
        strip_peep_comment_copy(clean, lines[i]);
        if (strncmp(clean, "call ", 5) == 0)
            saw_call = 1;
    }
    if (!saw_call)
        return 0;
    for (i = target + 1; i < nlines && i <= target + 5; ++i) {
        char clean[MAX_LINE];
        strip_peep_comment_copy(clean, lines[i]);
        if (strcmp(clean, "ret") == 0)
            return 1;
        if (starts_label(lines[i]) || peep_is_public_line(lines[i]))
            break;
    }
    return 0;
}

static int branch_skips_value_to_epilogue(int branch, int target)
{
    char clean[MAX_LINE];
    int i;

    if (branch < 2 || target != branch + 3 ||
        !starts_label(lines[branch + 1]))
        return 0;
    strip_peep_comment_copy(clean, lines[branch - 1]);
    if (strncmp(clean, "ld h,(ix", 8) != 0)
        return 0;
    strip_peep_comment_copy(clean, lines[branch - 2]);
    if (strncmp(clean, "ld l,(ix", 8) != 0)
        return 0;
    strip_peep_comment_copy(clean, lines[branch + 2]);
    if (strncmp(clean, "ld hl,", 6) != 0)
        return 0;
    for (i = target + 1; i < nlines && i <= target + 5; ++i) {
        strip_peep_comment_copy(clean, lines[i]);
        if (strcmp(clean, "ret") == 0)
            return 1;
        if (starts_label(lines[i]) || peep_is_public_line(lines[i]))
            break;
    }
    return 0;
}

static int likely_taken_e_counter_backedge(int branch, int target)
{
    char label[128];
    char clean[MAX_LINE];
    int bound;
    int init;
    int k;

    if (target < branch && branch >= 3 &&
        parse_jp_nz_label(lines[branch], label) &&
        strstr(lines[branch - 1], "affine_byte_") != NULL) {
        strip_peep_comment_copy(clean, lines[branch - 1]);
        if (strcmp(clean, "cp d") == 0) {
            strip_peep_comment_copy(clean, lines[branch - 2]);
            if (strcmp(clean, "ld a,e") == 0) {
                strip_peep_comment_copy(clean, lines[branch - 3]);
                if (strcmp(clean, "inc e") == 0)
                    return 1;
            }
        }
    }

    if (target >= branch || branch < 3 ||
        !parse_jp_c_label(lines[branch], label) ||
        !peep_parse_cp_const(lines[branch - 1], &bound))
        return 0;
    strip_peep_comment_copy(clean, lines[branch - 2]);
    if (strcmp(clean, "ld a,e") != 0)
        return 0;
    strip_peep_comment_copy(clean, lines[branch - 3]);
    if (strcmp(clean, "inc e") != 0)
        return 0;

    for (k = target - 1; k >= 0 && k >= target - 12; --k) {
        if (starts_label(lines[k]) || peep_is_public_line(lines[k]))
            break;
        if (peep_parse_ld_e_imm8(lines[k], &init))
            return init >= 0 && bound - init >= 16;
    }
    return 0;
}

/* Split 16-bit "reg_pair <= const" loop-bound test compiled as two 8-bit
 * comparisons - high byte first (falling through to the low-byte check
 * only on an exact high-byte match), each branching back to the same
 * loop-continue label:
 *   ld a,<hi>  / cp <hi_bound> / jp c,label / jp nz,exit
 *   ld a,<lo>  / cp <lo_bound> / jp c,label
 * Both jp c instances are near-always-taken loop back-edges once the loop
 * has run for more than a handful of high-byte values; jp cc is a flat 10
 * T-states while jr cc costs 12 when taken, so shortening either is a net
 * timing loss despite the byte saved. */
/* The middle exit branch ("jp nz,exit") is not itself protected - it is
 * legitimately shortened to jr on an earlier fixed-point iteration of this
 * same pass, same as jump_target_any's "jp or jr" tolerance documented
 * above. Accept either spelling so re-scans after that conversion still
 * recognize the surrounding idiom. */
static int is_nz_branch_either_form(const char *s)
{
    char clean[MAX_LINE];
    strip_peep_comment_copy(clean, s);
    return strncmp(clean, "jp nz, ", 7) == 0 || strncmp(clean, "jr nz,", 6) == 0;
}

static int likely_taken_word_range_backedge(int branch, int target)
{
    char hi_label[128], lo_label[128];
    char clean[MAX_LINE];
    int hi, lo;
    int k;
    char hi_reg, lo_reg;

    if (target >= branch || branch < 2)
        return 0;

    for (k = branch - 6; k <= branch - 2; ++k) {
        if (k < 0 || k + 6 >= nlines)
            continue;
        if (branch != k + 2 && branch != k + 6)
            continue;

        strip_peep_comment_copy(clean, lines[k]);
        if (strlen(clean) != 6 || strncmp(clean, "ld a,", 5) != 0)
            continue;
        hi_reg = clean[5];
        if (hi_reg != 'b' && hi_reg != 'd' && hi_reg != 'h')
            continue;
        lo_reg = (hi_reg == 'b') ? 'c' : (hi_reg == 'd') ? 'e' : 'l';

        if (!peep_parse_cp_const(lines[k + 1], &hi) ||
            !parse_jp_c_label(lines[k + 2], hi_label) ||
            !is_nz_branch_either_form(lines[k + 3]))
            continue;

        strip_peep_comment_copy(clean, lines[k + 4]);
        if (strlen(clean) != 6 || strncmp(clean, "ld a,", 5) != 0 ||
            clean[5] != lo_reg)
            continue;

        if (!peep_parse_cp_const(lines[k + 5], &lo) ||
            !parse_jp_c_label(lines[k + 6], lo_label) ||
            strcmp(hi_label, lo_label) != 0)
            continue;

        return 1;
    }
    return 0;
}

static int loop_has_small_constant_bound(int target, int backedge,
                                         const LabelIndexEntry *labels,
                                         int label_count)
{
    int i;
    int limit = target + 12;

    if (limit > backedge)
        limit = backedge;
    for (i = target + 1; i + 1 < limit; ++i) {
        char exit_label[128];
        char definition[130];
        int bound;
        int exit_line;

        if (!peep_parse_cp_const(lines[i], &bound) || bound < 0 || bound > 4)
            continue;
        if (i == target + 1)
            continue;
        if (!jump_target_any(lines[i + 1], exit_label) ||
            strchr(lines[i + 1], ',') == NULL)
            continue;
        sprintf(definition, "%s:", exit_label);
        exit_line = find_label_line(labels, label_count, definition);
        if (exit_line > backedge)
            return 1;
    }
    return 0;
}

static int is_forward_diamond_arm(int branch, int target,
                                  const LabelIndexEntry *labels,
                                  int label_count)
{
    char other_target[128];
    char definition[130];
    int i;

    if (target <= branch)
        return 0;
    if (strchr(lines[branch], ',') != NULL) {
        for (i = target - 1; i > branch; --i) {
            const PeepLineInfo *info = peep_line_info(i);
            int other_line;
            if (starts_label(lines[i]))
                return 0;
            if (info != NULL && (info->kind == PEEP_LINE_BLANK ||
                                 info->kind == PEEP_LINE_COMMENT))
                continue;
            if (!is_uncond_jp(lines[i]) ||
                !jump_target_any(lines[i], other_target))
                return 0;
            sprintf(definition, "%s:", other_target);
            other_line = find_label_line(labels, label_count, definition);
            return other_line > target;
        }
        return 0;
    }
    if (branch + 1 >= nlines || !starts_label(lines[branch + 1]))
        return 0;
    for (i = branch - 1; i >= 0 && i >= branch - 16; --i) {
        char conditional_target[128];
        int conditional_line;
        if (starts_label(lines[i]) || peep_is_public_line(lines[i]))
            break;
        if (!jump_target_any(lines[i], conditional_target) ||
            strchr(lines[i], ',') == NULL)
            continue;
        sprintf(definition, "%s:", conditional_target);
        conditional_line = find_label_line(labels, label_count, definition);
        return conditional_line == branch + 1;
    }
    return 0;
}

int pass_jp_to_jr(void)
{
    static int addr[MAX_LINES];   /* upper-bound byte address of each line */
    static int hot_loop_depth[MAX_LINES + 1];
    LabelIndexEntry *labels;
    int label_count = 0;
    int i;
    int any = 0;
    int changed;

    labels = (LabelIndexEntry *)malloc((size_t)nlines * sizeof(*labels));
    if (labels == NULL && nlines != 0) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    for (i = 0; i < nlines; ++i) {
        if (starts_label(lines[i])) {
            labels[label_count].definition = lines[i];
            labels[label_count].line = i;
            ++label_count;
        }
    }
    qsort(labels, (size_t)label_count, sizeof(*labels), compare_label_entries);

        /* Mark structurally proven high-trip byte-counter loops.  This lets time
         * mode retain jp cc for their direct backedges and return-skipping
         * continuation branches.  jp cc is a steady 10 T-states, while jr cc
         * costs 12 when taken (7 when not taken).  Size mode deliberately keeps
         * the old shorten-everything policy. */
        memset(hot_loop_depth, 0,
            (size_t)(nlines + 1) * sizeof(hot_loop_depth[0]));
    for (i = 0; i < nlines; ++i) {
        char lab[128];
        char def[130];
        int target;

        if (!jump_target_any(lines[i], lab))
            continue;
        sprintf(def, "%s:", lab);
        target = find_label_line(labels, label_count, def);
        if (target >= 0 && (likely_taken_e_counter_backedge(i, target) ||
                            likely_taken_word_range_backedge(i, target))) {
            hot_loop_depth[target]++;
            hot_loop_depth[i + 1]--;
        }
    }
    for (i = 1; i < nlines; ++i)
        hot_loop_depth[i] += hot_loop_depth[i - 1];

    do {
        int pc = 0;
        int shortenable_count = 0;
        int short_small_backedges = 0;
        changed = 0;

        /* Assign an upper-bound address to every line. */
        for (i = 0; i < nlines; i++) {
            addr[i] = pc;
            pc += instr_size_upper(i);
        }

        for (i = 0; i < nlines; ++i) {
            char lab[128], def[130];
            int conditional, target, disp;
            if (!jr_convertible(i, lab))
                continue;
            sprintf(def, "%s:", lab);
            target = find_label_line(labels, label_count, def);
            if (target < 0)
                continue;
            disp = addr[target] - (addr[i] + 2);
            if (disp < -128 || disp > 127)
                continue;
            conditional = strchr(lines[i], ',') != NULL;
            if ((!conditional &&
                 ((target < i &&
                   !loop_has_small_constant_bound(target, i, labels,
                                                  label_count)) ||
                  branch_skips_value_to_epilogue(i, target))) ||
                (conditional &&
                 (likely_taken_e_counter_backedge(i, target) ||
                  likely_taken_word_range_backedge(i, target) ||
                  (likely_zero_call_result(i) &&
                   branch_skips_call_to_epilogue(i, target)) ||
                  (likely_nonzero_word_parameter(i) &&
                   branch_skips_call_to_epilogue(i, target)) ||
                  (hot_loop_depth[i] > 0 &&
                   branch_skips_return(i, target)))))
                continue;
            ++shortenable_count;
            if (target < i &&
                loop_has_small_constant_bound(target, i, labels, label_count))
                ++short_small_backedges;
        }

        for (i = 0; i < nlines; i++) {
            char lab[128];
            char def[130];
            int target = -1;
            int from, to, disp;

            if (!jr_convertible(i, lab))
                continue;

            /* Find the target label's first definition, matching the old
             * forward scan when malformed input contains duplicate labels. */
            sprintf(def, "%s:", lab);
            target = find_label_line(labels, label_count, def);
            if (target < 0)
                continue;

            if (!peep_context.options.optimize_size) {
                int conditional = strchr(lines[i], ',') != NULL;
                 if ((!conditional &&
                                         ((target < i &&
                                             (!loop_has_small_constant_bound(target, i, labels,
                                                                                                 label_count) ||
                                                short_small_backedges == 2)) ||
                                            (shortenable_count <= 2 && target > i &&
                                               is_forward_diamond_arm(i, target, labels,
                                                                      label_count)) ||
                      branch_skips_value_to_epilogue(i, target))) ||
                    (conditional &&
                     (likely_taken_e_counter_backedge(i, target) ||
                      likely_taken_word_range_backedge(i, target) ||
                                            (shortenable_count <= 2 &&
                                             is_forward_diamond_arm(i, target, labels,
                                                                                            label_count)) ||
                             (likely_zero_call_result(i) &&
                              branch_skips_call_to_epilogue(i, target)) ||
                                                         (shortenable_count <= 2 &&
                                                            likely_nonzero_call_result(i) &&
                                                            branch_skips_return(i, target)) ||
                              (likely_nonzero_word_parameter(i) &&
                               branch_skips_call_to_epilogue(i, target)) ||
                      (hot_loop_depth[i] > 0 &&
                       branch_skips_return(i, target)))))
                    continue;
            }

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

    free(labels);
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

int pass_elim_dead_register_loads(void)
{
    const unsigned protected_registers = PEEP_REG_IX | PEEP_REG_IY | PEEP_REG_SP;
    int i = 0;
    int changed = 0;

    while (i < nlines) {
        const PeepLineInfo *info = peep_line_info(i);

        if (info->kind == PEEP_LINE_INSTRUCTION &&
            info->opcode == PEEP_OPCODE_LD &&
            info->left.kind == PEEP_OPERAND_REGISTER &&
            (info->right.kind == PEEP_OPERAND_REGISTER ||
             info->right.kind == PEEP_OPERAND_IMMEDIATE) &&
            info->effects.writes != 0 &&
            !(info->effects.writes & protected_registers) &&
            peep_registers_dead_after(i, info->effects.writes)) {
            delete_n(i, 1);
            changed = 1;
            continue;
        }
        ++i;
    }
    return changed;
}

/*
 * Is line `i` a return-value materialization instruction that may legitimately
 * sit between the dead cleanup pops and the framed epilogue?  Such gap fillers
 * (`ld hl,N`, `ld l,(ix-2)`, `ld h,(ix-1)`, ...) must be provably harmless to
 * step over: they touch neither the stack pointer nor BC, perform no control
 * flow, write nothing to the stack, and have fully known effects.  Blank and
 * comment lines carry no code and are always skippable.
 */
static int epilogue_gap_skippable(int i)
{
    const PeepLineInfo *info = peep_line_info(i);
    unsigned touched;

    if (!info)
        return 0;
    if (info->kind == PEEP_LINE_BLANK || info->kind == PEEP_LINE_COMMENT)
        return 1;
    if (info->kind != PEEP_LINE_INSTRUCTION)
        return 0;
    if (info->effects.unknown || info->effects.control_flow)
        return 0;
    if (info->effects.memory_written & PEEP_MEM_STACK)
        return 0;
    touched = info->effects.reads | info->effects.writes;
    if (touched & (PEEP_REG_SP | PEEP_REG_B | PEEP_REG_C))
        return 0;
    return 1;
}

/*
 * Remove stack-cleanup "pop bc" instructions that are stranded in front of a
 * framed epilogue.  dcc emits a "pop bc" per pushed argument after every call
 * to discard the actuals; when the call is the last thing a function does, the
 * epilogue's "ld sp,ix" resets SP to the frame base and discards whatever the
 * pops were adjusting - so the pops are pure dead weight (10 T-states and one
 * byte each).
 *
 *     call _foo
 *     pop bc            <- dead: SP about to be overwritten
 *     pop bc            <- dead
 *     ld hl,0           }  optional return-value setup (SP/BC neutral)
 *     ld sp,ix          <- unconditionally reloads SP from IX
 *     pop ix
 *     ret
 *
 * Correctness:
 *   - "ld sp,ix" writes SP without reading it, so the SP value the pops leave
 *     behind is dead.
 *   - A "pop bc" that clobbers BC immediately before "ret" proves BC holds no
 *     live value at return (dcc returns values in HL or DE:HL, never BC); were
 *     it otherwise the original code would already be wrong.  The gap scan
 *     additionally rejects any intervening reader of B/C.
 *   - Only the canonical framed epilogue "ld sp,ix / pop ix / ret" qualifies.
 *     Leaf functions have no "ld sp,ix", so their trailing pops - which really
 *     do rebalance the stack for "ret" - are never touched.
 */
int pass_elim_dead_epilogue_cleanup_pops(void)
{
    int i;
    int changed = 0;

    for (i = 0; i + 2 < nlines; ++i) {
        int j;

        if (!eq(i, "ld sp,ix") || !eq(i + 1, "pop ix") || !eq(i + 2, "ret"))
            continue;

        /*
         * Walk backwards from the epilogue over skippable gap fillers and
         * delete every "pop bc" that precedes it, stopping at the first
         * instruction that is neither a cleanup pop nor a safe gap filler.
         */
        j = i - 1;
        while (j >= 0) {
            if (eq(j, "pop bc")) {
                delete_n(j, 1);
                changed = 1;
                --i;    /* the epilogue and everything after shifted down */
                --j;
                continue;
            }
            if (epilogue_gap_skippable(j)) {
                --j;
                continue;
            }
            break;
        }
    }

    return changed;
}

/* True when the instruction at i is a plain "or a" / "and a" carry clear. */
static int is_carry_clear_op(int i)
{
    return eq(i, "or a") || eq(i, "and a");
}

/*
 * True when the instruction at i provably leaves CF = 0.
 *
 * The Z80 logical operations (and/or/xor, in every addressing form) always
 * reset the carry flag, so any of them is a proof source regardless of
 * operand.  "cp"/"sub"/"add"/"sbc"/"inc"/"dec" are not: their carry is
 * data dependent.
 */
static int line_resets_carry(int i)
{
    char text[MAX_LINE];

    strip_peep_comment_lower_copy(text, lines[i]);
    return !strncmp(text, "and ", 4) || !strncmp(text, "or ", 3) ||
           !strncmp(text, "xor ", 4);
}

/*
 * True when the instruction at i is known not to disturb CF, so a backwards
 * carry-state walk may step over it.  Only "ld" (excluding "ld a,i"/"ld a,r",
 * which copy IFF2 into P/V but leave C alone - still accepted), "push",
 * "ex", "bit"/"set"/"res" and "nop" qualify; everything else either writes
 * carry or is not modelled.
 */
static int line_preserves_carry(int i)
{
    const PeepLineInfo *info = peep_line_info(i);

    if (!info || info->kind != PEEP_LINE_INSTRUCTION)
        return 0;
    if (info->effects.unknown || info->effects.control_flow)
        return 0;
    return (info->effects.flags_written & PEEP_FLAG_C) == 0;
}

/*
 * Decide whether CF is provably 0 immediately before line i by walking
 * backwards through straight-line code.
 *
 * The walk stops at anything that makes the incoming state unknowable: a
 * label (which may be reached from elsewhere), user assembly, a call, or an
 * instruction whose effect on carry is not modelled.
 */
static int carry_known_clear_before(int i)
{
    int j = i - 1;
    int steps = 0;

    while (j >= 0 && steps < 64) {
        const PeepLineInfo *info = peep_line_info(j);

        if (!info)
            return 0;
        if (info->kind == PEEP_LINE_BLANK || info->kind == PEEP_LINE_COMMENT) {
            --j;
            continue;
        }
        /* A label is a potential join point: predecessors are unknown. */
        if (info->kind != PEEP_LINE_INSTRUCTION)
            return 0;
        if (info->effects.unknown)
            return 0;

        /*
         * A conditional "jp c"/"jr c" only falls through when carry was
         * clear, so the fall-through path - which is the only way to reach
         * line i, since no label intervenes - has CF = 0.  Unconditional
         * jumps, calls and returns end the walk.
         */
        if (info->effects.control_flow) {
            char text[MAX_LINE];

            strip_peep_comment_lower_copy(text, lines[j]);
            if (!strncmp(text, "jp c,", 5) || !strncmp(text, "jr c,", 5))
                return 1;
            return 0;
        }

        if (line_resets_carry(j))
            return 1;
        if (!line_preserves_carry(j))
            return 0;
        --j;
        ++steps;
    }
    return 0;
}

/*
 * Delete "or a" / "and a" instructions that exist only to clear the carry
 * flag for a following 16-bit "sbc hl,rr", when carry is already provably
 * clear.
 *
 * dcc emits the idiom "or a / sbc hl,de" for every 16-bit compare and
 * subtraction.  In two very common situations the clear is dead weight:
 *
 *   1. A signed compare biases the high byte first, and "xor 80h" already
 *      reset carry:
 *
 *          ld a,h
 *          xor 80h      <- resets CF
 *          ld h,a       <- "ld" never touches flags
 *          or a         <- removed
 *          sbc hl,de
 *
 *   2. The block is reached by falling through a "jr c"/"jp c", which by
 *      definition only happens when carry was clear:
 *
 *          jr c,L
 *          ld l,(ix+4)
 *          ld h,(ix+5)
 *          ld de,7
 *          or a         <- removed
 *          sbc hl,de
 *
 * Correctness:
 *   - "or a"/"and a" leave A unchanged, so only their flag write matters.
 *   - The forward scan requires the next flag-relevant instruction to be
 *     "sbc hl,rr", which rewrites C, Z, S and P/V.  The Z/S/P/V values the
 *     removed instruction produced are therefore dead, and only the carry it
 *     consumed needs to be preserved - which the backwards proof guarantees.
 *   - Anything between is restricted to instructions that neither read nor
 *     write flags, so no other consumer can observe the difference.
 */
int pass_elim_redundant_carry_clear(void)
{
    int i;
    int changed = 0;

    for (i = 0; i < nlines; ++i) {
        int j;
        int found = 0;

        if (!is_carry_clear_op(i))
            continue;

        /*
         * Forward scan: step over flag-neutral filler until the consumer.
         * It must be a 16-bit "sbc hl,rr" - the only instruction for which
         * the preceding clear is required and whose flag write kills every
         * flag the clear produced.
         */
        for (j = i + 1; j < nlines && j <= i + 8; ++j) {
            const PeepLineInfo *info = peep_line_info(j);

            if (!info)
                break;
            if (info->kind == PEEP_LINE_BLANK || info->kind == PEEP_LINE_COMMENT)
                continue;
            if (info->kind != PEEP_LINE_INSTRUCTION || info->effects.unknown)
                break;
            if (eq(j, "sbc hl,bc") || eq(j, "sbc hl,de") ||
                eq(j, "sbc hl,hl") || eq(j, "sbc hl,sp")) {
                found = 1;
                break;
            }
            /* Any other flag reader or writer ends the scan. */
            if (info->effects.flags_read || info->effects.flags_written)
                break;
        }
        if (!found)
            continue;

        if (!carry_known_clear_before(i))
            continue;

        delete_n(i, 1);
        changed = 1;
        --i;
    }

    return changed;
}

/*
 * pass_local_alloc_wide: shrink a small (3 or 4 byte) local stack
 * allocation from the general form
 *
 *   ld hl,-N
 *   add hl,sp
 *   ld sp,hl
 *
 * into N copies of "dec sp". Each "dec sp" is 1 byte/6 cycles, so N of
 * them (N bytes, 6N cycles) beats the 3-instruction form (6 bytes, 27
 * cycles) in *both* bytes and cycles simultaneously only for N<=4 (N=4:
 * 4 bytes/24 cycles vs 6 bytes/27; N=5 would already cost more cycles
 * despite fewer bytes, which this project's "smaller text isn't proof of
 * faster" rule forbids relying on) - the rewrite is capped there.
 *
 * peep_pass_once.c's try_local_alloc_at already does this for N=1/2, but
 * deliberately does NOT handle N=3/4: it runs inside pass_once, which is
 * first in the fixed-point pass list, so eagerly consuming "ld hl,-4"/
 * "ld hl,-3" there would permanently destroy that exact text before
 * function-specific frame-shrinking passes elsewhere in the SAME
 * fixed-point list (pass_shrink_minmax_frame3_after_score_cache,
 * pass_shrink_minmax_frame2_after_loop_ctr_b - which look for that
 * literal text once they have proven the corresponding (ix-N) slot is
 * unused - and, given the fixed-point loop, only ever run in the SAME
 * iteration, before pass_once loops back around) ever get a chance to
 * shrink the allocation further. Running this pass here, after the whole
 * fixed-point group has already converged, guarantees every such
 * function-specific shrink has already had every opportunity to fire -
 * confirmed necessary via ttt.c's _MinMax regressing when the N=3/4
 * rewrite was tried inline in pass_once instead (see Item T10 of the
 * text-size MIR migration, which needed this widening for
 * tgoto.gt_block_label/gt_basic once MIR started allocating exactly 4
 * frame bytes for some newly-accepted functions).
 *
 * The rewrite deletes the definition of HL (the address of the fresh
 * allocation), so it must only fire when the following code fully
 * rewrites HL before reading it (local_alloc_hl_result_dead, shared with
 * peep_pass_once.c).
 */
int pass_local_alloc_wide(void)
{
    int i;
    int changed = 0;

    for (i = 0; i < nlines; ++i) {
        char tmp[MAX_LINE];
        int n;
        int k;

        if (!eq(i + 1, "add hl,sp") || !eq(i + 2, "ld sp,hl"))
            continue;

        strip_peep_comment_copy(tmp, lines[i]);
        if (strncmp(tmp, "ld hl,-", 7) != 0)
            continue;
        if (sscanf(tmp + 7, "%d", &n) != 1 || n < 3 || n > 4)
            continue;
        if (!local_alloc_hl_result_dead(i + 3))
            continue;

        for (k = 0; k < n && k < 3; k++)
            replace1_tagged(i + k, "dec sp", "local_alloc_wide");
        if (n > 3)
            insert_line_tagged(i + 3, "dec sp", "local_alloc_wide");

        changed = 1;
        --i;
    }

    return changed;
}
