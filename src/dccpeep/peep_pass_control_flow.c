/**
 * @file peep_pass_control_flow.c
 * @brief Implements label and branch simplification passes.
 *
 * @par Role
 * Collapses adjacent labels, removes unreferenced labels when safe, folds
 * branch-over-jump shapes, threads and shortcuts jump targets, replaces
 * jumps to plain returns, and turns a call immediately followed by a bare
 * ret into a tail-call jump.
 *
 * @par Key entry points
 * pass_labels(), pass_branch_over_jump(), pass_jump_thread(),
 * pass_cond_skip_shortcut(), pass_jp_to_plain_ret(), and
 * pass_call_to_tail_jp().
 *
 * @par Boundary
 * peep_control_flow.c owns shared label indexes and textual branch queries;
 * this module owns only rewrites. dccpeep.c decides when they run.
 */
#include "dccpeep_internal.h"

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

int is_label_referenced(const char *lab)
{
    int i;
    char tgt[128];
    int lablen;
    const char *found;
    char before;
    char after;

    /* Opaque user-asm barriers must survive adjacent-label cleanup. */
    if (strncmp(lab, "__dcc_user_asm_", 15) == 0)
        return 1;

    lablen = (int)strlen(lab);

    for (i = 0; i < nlines; i++) {
        if (jump_target_any(lines[i], tgt) && strcmp(tgt, lab) == 0)
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
int pass_labels(void)
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

int pass_branch_over_jump(void)
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
int pass_jump_thread(void)
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
int pass_cond_skip_shortcut(void)
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
int pass_jp_to_plain_ret(void)
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

/*
 * Tail-call: replace a "call FUNC" immediately followed by "ret" with a
 * plain "jp FUNC". The callee's own eventual ret pops exactly the return
 * address our own ret would have popped, so control lands back in our
 * caller as soon as the callee finishes - skipping the extra call/ret
 * round trip through this function entirely.
 *
 * Safety: dcc's codegen only ever emits code between a call and this
 * function's own return when the callee's return-value shape doesn't
 * already match what this function needs to return (sign/zero extension
 * widening a narrower callee, truncation narrowing a wider one, a
 * struct-copy epilogue, and so on). Every case where the shapes already
 * match - including same-width signed/unsigned mismatches, and void
 * callers discarding any callee's return value - compiles to nothing at
 * all between the call and the ret. So requiring these two lines be
 * strictly adjacent, with nothing between them, already proves the
 * rewrite is safe: dccpeep never has to know either function's C-level
 * return type to make this call.
 *
 * Deliberately narrow for now: only fires when "ret" is the literal next
 * line, not e.g. "ld sp,ix" / "pop ix" / "ret" (a framed function tearing
 * its own IX frame down before returning). Folding a frame epilogue in
 * ahead of the call is a real further optimization, but a bigger, riskier
 * rewrite than this pass attempts - see the design notes this pass grew
 * out of. That said, this narrower form still costs nothing in the
 * debugger: dcc-debug-host's backtrace walks the IX-frame save chain (see
 * MiServer::stack_frames), so a function with no IX frame - which is
 * exactly what "ret" appearing with nothing before it implies - was
 * already invisible to it before this pass ever ran.
 *
 * Conditional calls ("call z,FUNC" etc.) are out of scope for now: dcc's
 * own codegen never emits one in this position, and excluding them keeps
 * the label extraction below unambiguous (no comma to strip out first).
 */
int pass_call_to_tail_jp(void)
{
    int i;
    int changed;
    char tmp[MAX_LINE];
    char next[MAX_LINE];
    char label[128];
    char new_jp[160];

    changed = 0;

    for (i = 0; i + 1 < nlines; ++i) {
        strip_peep_comment_copy(tmp, lines[i]);
        if (strncmp(tmp, "call ", 5) != 0)
            continue;
        if (strchr(tmp + 5, ',') != NULL)
            continue;
        strncpy(label, tmp + 5, sizeof(label) - 1);
        label[sizeof(label) - 1] = 0;
        if (label[0] == 0)
            continue;

        strip_peep_comment_copy(next, lines[i + 1]);
        if (strcmp(next, "ret") != 0)
            continue;

        snprintf(new_jp, sizeof(new_jp), "jp %s", label);
        replace1_tagged(i, new_jp, "call_to_tail_jp");
        changed = 1;
    }

    return changed;
}
