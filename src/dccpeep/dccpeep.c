/*
 * dccpeep.c - fixed-point peephole optimizer for dcc-generated Z80/M80 assembly.
 *
 * main() preserves the order of convergent, post-convergence, size-mode, and
 * final-cleanup passes. Individual passes return nonzero when they rewrite the
 * shared line program declared in dccpeep_internal.h.
 */
#include "dccpeep_internal.h"

#define MAX_PASS_STATS 160
typedef struct PassStat {
    const char *name;
    unsigned long calls;
    unsigned long changes;
} PassStat;

static PassStat pass_stats[MAX_PASS_STATS];
static int pass_stats_count;

typedef struct PeepPass {
    const char *name;
    int (*run)(void);
    unsigned flags;
} PeepPass;

enum { PEEP_PASS_UNDOCUMENTED_Z80 = 1u << 0 };

static int run_counted_pass(const char *name, int (*pass)(void))
{
    int i;
    int changed;

    for (i = 0; i < pass_stats_count; ++i)
        if (strcmp(pass_stats[i].name, name) == 0)
            break;
    if (i == pass_stats_count) {
        if (pass_stats_count >= MAX_PASS_STATS) {
            fprintf(stderr, "too many optimizer passes for statistics\n");
            exit(1);
        }
        pass_stats[i].name = name;
        pass_stats[i].calls = 0;
        pass_stats[i].changes = 0;
        ++pass_stats_count;
    }

    changed = pass();
    ++pass_stats[i].calls;
    if (changed)
        ++pass_stats[i].changes;
    return changed;
}

#define RUN_PASS(pass) run_counted_pass(#pass, pass)

static void report_stats(int iterations)
{
    int i;

    fprintf(stderr, "dccpeep stats: iterations=%d inserted=%lu deleted=%lu\n",
            iterations, peep_context.stats.lines_inserted,
            peep_context.stats.lines_deleted);
    for (i = 0; i < pass_stats_count; ++i)
        fprintf(stderr, "  %-44s calls=%lu changes=%lu\n",
                pass_stats[i].name, pass_stats[i].calls,
                pass_stats[i].changes);
}

/* -fundocumented-z80: allow peephole passes that rely on undocumented Z80
 * opcodes (currently just the IYH/IYL half-register load/inc/dec forms
 * pass_byte_loop_counter_to_reg_iyl/pass_byte_incr_loop_counter_to_reg_iyl
 * use, wrapped in M80 macros since M80 has no native mnemonic for them -
 * see the macro prelude in main()). These opcodes are well-established
 * folklore on real NMOS Z80 silicon and its common clones, and verified
 * working under ntvcm, but are not part of the documented Z80 instruction
 * set, so they are opt-in and OFF by default. */





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
        if (!parse_ld_hl_imm(lines[i], base, sizeof(base)))
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

/*
 * pass_fold_hl_base_const_offset:
 *
 * Struct field / array element address computation for a link-time-constant
 * base (a plain global/static symbol's own address - `ld hl,LABEL`, never
 * `ld hl,(LABEL)`, which loads a stored *value* through a runtime pointer,
 * not an address) often lands right next to adding a constant field offset:
 *
 *     ld hl,LABEL
 *     ld de,N
 *     add hl,de
 *
 * LABEL+N is itself a valid M80 assembler constant expression, so this
 * collapses to a single `ld hl,LABEL+N` and drops the other two
 * instructions. Conservative in the same spirit as
 * pass_double_de_before_add just above: this triple only ever appears as
 * address arithmetic feeding a later dereference or store, so the flags
 * `add hl,de` sets are dead here, and DE's incoming value was already
 * about to be clobbered by the `ld de,N` this pass removes, so leaving it
 * unclobbered can only be safe. Never matches `ld hl,(LABEL)`: an indirect
 * load through a runtime pointer variable's stored value isn't a
 * compile-time constant, so there's nothing to fold into it. Found via
 * corpus mining a static-struct-heavy interpreter (tests/cobint.c, whose
 * global state lives in one struct with ~20 fields - 567 instances of this
 * exact triple in that file alone; a broader sample of the tests directory's
 * .c files found it elsewhere too, just far less densely).
 */

/* Both range_is_user_asm callers below query it once per line position in
 * an O(nlines) outer loop; range_is_user_asm itself used to rescan from
 * line 0 every single call, making each caller O(nlines^2) - measured as
 * dccpeep's single largest hotspot (>50% of self time on tests/bint.c
 * under gprof). Precomputing the same depth-bracketing scan ONCE per pass
 * invocation into a mask, then doing an O(1)-ish bounded lookup per query,
 * preserves the exact semantics (both marker lines themselves count as
 * "inside", matching the original's depth++/return/depth-- ordering) at
 * O(nlines) total per pass instead of O(nlines^2). */
static char user_asm_mask[MAX_LINES];

static void build_user_asm_mask(void)
{
    int depth = 0, i;
    for (i = 0; i < nlines; ++i) {
        if (strcmp(lines[i], "; dcc user asm begin") == 0)
            depth++;
        user_asm_mask[i] = (char)(depth > 0);
        if (strcmp(lines[i], "; dcc user asm end") == 0 && depth > 0)
            depth--;
    }
}

static int mask_range_is_user_asm(int start, int end)
{
    int i;
    for (i = (start < 0 ? 0 : start); i <= end && i < nlines; ++i) {
        if (user_asm_mask[i])
            return 1;
    }
    return 0;
}

static int symbol_is_external(const char *symbol)
{
    int i;
    char name[128];
    char extra;

    /* Only a genuine EXTRN reference (a symbol defined in another module)
     * hits the Link-80 addend bug. A PUBLIC symbol is defined in this
     * module, so M80 folds BASE+offset within its own segment - the same
     * guarantee a private local label has. */
    for (i = 0; i < nlines; ++i) {
        if (sscanf(lines[i], "extrn %127s %c", name, &extra) == 1 &&
            strcmp(name, symbol) == 0)
            return 1;
    }

    return 0;
}

static int pass_fold_hl_base_const_offset(void)
{
    int i;
    int changed;
    char base[128];
    char off_text[64];
    int off;
    char out[256];

    changed = 0;
    build_user_asm_mask();
    for (i = 0; i + 2 < nlines; ++i) {
        if (!input_is_dcc_generated || mask_range_is_user_asm(i, i + 2))
            continue;
        if (!parse_ld_hl_imm(lines[i], base, sizeof(base)))
            continue;
        if (base[0] == '(')
            continue;
        if (!parse_ld_de_imm(lines[i + 1], off_text, sizeof(off_text)))
            continue;
        if (!parse_nonneg_int(off_text, &off) || off == 0)
            continue;
        if (!eq(i + 2, "add hl,de"))
            continue;
        if (symbol_is_external(base))
            continue;

        sprintf(out, "ld hl,%s+%d", base, off);
        replace1_tagged(i, out, "fold_hl_base_const_offset");
        delete_n(i + 1, 2);
        build_user_asm_mask(); /* lines shifted; mask indices after i are now stale */
        changed = 1;
        if (i > 0)
            --i;
    }

    return changed;
}

/*
 * pass_fold_hl_label_word_deref:
 *
 * Reading a 16-bit value stored at a link-time-constant address often
 * follows right after computing that address into HL (e.g. right after
 * pass_fold_hl_base_const_offset above folds a struct field's address, or
 * directly from dcc's own codegen for any global/static pointer- or
 * int-sized field/variable):
 *
 *     ld hl,LABEL
 *     ld e,(hl)
 *     inc hl
 *     ld d,(hl)
 *     ex de,hl
 *
 * This computes HL = the 16-bit value stored at LABEL - exactly what the
 * native Z80 `ld hl,(nn)` instruction does directly. Collapses five
 * instructions into one. Never matches `ld hl,(LABEL)`: that's already an
 * indirect load (a runtime pointer's stored value), not a link-time
 * address, so there's nothing to fold. Flag-neutral in both directions
 * (none of ld/16-bit inc/ex touch flags), so nothing downstream can
 * observe a difference there; DE ends up holding whatever it held before
 * the sequence instead of the LABEL+1 the original left there as a pure
 * side effect of dereferencing byte-at-a-time, which no correct compiler
 * output would intentionally depend on.
 *
 * Found the same way as pass_fold_hl_base_const_offset: profiling
 * tests/cint.c's run() dispatch loop after converting its own G-> heap
 * pointer to a static struct (test/pint.c-style "in = &code[pc++]" plus
 * the switch dispatch's opcode fetch, hot on every VM instruction) showed
 * this residual pattern immediately following that fold. Not confined to
 * interpreters - a corpus sample found it elsewhere too, e.g. 118
 * instances in tests/na.c (a text editor with heavy struct/global-array
 * state), just less densely.
 */
static int pass_fold_hl_label_word_deref(void)
{
    int i;
    int changed;
    char label[128];
    char out[160];

    changed = 0;
    build_user_asm_mask();
    for (i = 0; i + 4 < nlines; ++i) {
        if (!input_is_dcc_generated || mask_range_is_user_asm(i, i + 4))
            continue;
        if (!parse_ld_hl_imm(lines[i], label, sizeof(label)))
            continue;
        if (label[0] == '(')
            continue;
        if (!eq(i + 1, "ld e,(hl)"))
            continue;
        if (!eq(i + 2, "inc hl"))
            continue;
        if (!eq(i + 3, "ld d,(hl)"))
            continue;
        if (!eq(i + 4, "ex de,hl"))
            continue;

        sprintf(out, "ld hl,(%s)", label);
        replace1_tagged(i, out, "fold_hl_label_word_deref");
        delete_n(i + 1, 4);
        build_user_asm_mask(); /* lines shifted; mask indices after i are now stale */
        changed = 1;
        if (i > 0)
            --i;
    }

    return changed;
}


/* Match exactly:
 *     ld (ix+N),a
 *     ld a,(ix+N)
 * allowing optimizer tags/comments after either instruction.  This is safe
 * only for adjacent instructions because the store does not alter A and no
 * intervening instruction can clobber it.
 */


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

/*
 * In tiny posNfunc helpers, cache the selected board byte in B instead of
 * a one-byte stack local at ix-1.  These helpers make no calls, so B is safe.
 */



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

/* pass_cache_noix_byte_param_reload additionally needs SP to be stable (it
 * caches an SP-relative address, not just a value), so push/pop - which
 * don't clobber BC but do shift SP - are hazards there even though they
 * aren't for a plain register-value cache like
 * pass_cache_global_word_reload's. */

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
        if (!parse_ld_hl_imm(lines[i], valbuf, sizeof(valbuf))) continue;
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
int bc_regalloc_claimed_before(int at);

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
            if (!parse_ld_hl_imm(lines[i], valbuf, sizeof(valbuf))) continue;
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
                    if (!parse_ld_hl_imm(lines[i], valbuf, sizeof(valbuf))) continue;
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


/* This function's own boundaries: the most recent "public NAME" at or
 * before `from`, and the next "public NAME" after it (or nlines if this is
 * the last function in the file). Used to bound the label-reachability
 * check below to the current function only, so it can never be fooled by
 * a same-numbered label belonging to a different function. */

/* Same as find_function_bounds, but also recognizes "; static function "
 * (see emit_function_prologue) as a function boundary - a static
 * function's definition never emits a public line, so find_function_bounds
 * alone treats its whole body as still belonging to whichever public
 * function happens to precede it in the file. */


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

int stride_parse_ld_r_ix_neg(const char *s, char r, int *n); /* forward */

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
            parse_ld_hl_imm(lines[i + 2], imm, sizeof(imm))) {
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
 * pass_word_switch_cmp_avoid_push_pop:
 *
 * emit_switch_jump_table's upper-bound check emits, for every switch
 * statement dcc compiles to a jump table:
 *
 *   push hl           ; save the switch value across the compare
 *   ld de,N           ; N = maxv-minv (the range width)
 *   or a
 *   sbc hl,de         ; destroys hl, sets Z/C
 *   pop hl            ; restore the switch value (needed again below)
 *   jp z,OK           ; value == N: still in range
 *   jp nc,DEFAULT     ; value > N: out of range
 *
 * When the switch value was just zero-extended from a byte (`ld h,0`
 * immediately before "push hl"), pass_byte_cmp_push_pop_hl (just above)
 * already collapses this whole thing to a single 8-bit `cp` instruction -
 * far cheaper than anything this pass could do, so this pass explicitly
 * declines whenever that precondition is present, and only ever fires on
 * exactly the cases that pass leaves alone.
 *
 * For a genuinely word-sized switch value (e.g. fint.c's `int op` field -
 * dispatched on every single VM instruction of every interpreted program,
 * confirmed via dccprof profiling to be the hottest code in the whole
 * interpreter benchmark suite, well above any individual opcode handler),
 * no such byte collapse is possible, and the push/pop is still avoidable:
 * swap the operand order (N - hl instead of hl - N) so the SBC destroys
 * the constant instead of the switch value, keeping the value alive in DE
 * via an EX DE,HL instead of a push/pop - trading an 11T push + 10T pop
 * for a cheaper 4T+4T register copy plus a 4T ex. This flips the carry
 * flag's meaning (borrow now happens when hl > N, not <), so the
 * out-of-range branch must flip from jp nc, to jp c, to match; Z is
 * unaffected (N-hl and hl-N are zero at the same time), so jp z, is
 * untouched. Verified against all three cases (below/at/above the
 * boundary) with a standalone assembly test before landing this - the
 * same class of subtle correctness trap a naive version of this
 * transformation could otherwise hide.
 *
 * An earlier, more sweeping attempt applied this same transformation
 * directly in emit_switch_jump_table itself (dcc_stmt.c), unconditionally
 * for every switch-to-jump-table compile. That measured as a broad
 * PERFORMANCE REGRESSION (cint/bint/pint/adaint/cobint all 3-7% slower)
 * caught by the full suite's perf-baseline check: every one of those
 * apps' switch values IS byte-sized, so changing the emitted shape
 * silently defeated pass_byte_cmp_push_pop_hl's much bigger win across
 * the board. Doing it here instead, gated on that pass's own precondition
 * being absent, targets only the cases where there is no competing
 * optimization to lose.
 */
static int pass_word_switch_cmp_avoid_push_pop(void)
{
    int i, changed = 0;
    int n;
    char label_ok[128];
    char label_default[128];
    char buf[64];

    for (i = 0; i + 6 < nlines; i++) {
        if (!eq(i, "push hl")) continue;
        if (i > 0 && eq(i - 1, "ld h,0")) continue;
        if (!peep_parse_ld_de_signed(lines[i + 1], &n)) continue;
        if (!eq(i + 2, "or a")) continue;
        if (!eq(i + 3, "sbc hl,de")) continue;
        if (!eq(i + 4, "pop hl")) continue;
        if (!peep_parse_jp_cond_label(lines[i + 5], "z", label_ok)) continue;
        if (!peep_parse_jp_cond_label(lines[i + 6], "nc", label_default)) continue;

        replace1_tagged(i, "ld d,h", "word_switch_cmp_avoid_push_pop");
        insert_line_tagged(i + 1, "ld e,l", "word_switch_cmp_avoid_push_pop");
        sprintf(buf, "ld hl,%d", n);
        replace1(i + 2, buf);
        replace1(i + 5, "ex de,hl");
        sprintf(buf, "jp c, %s", label_default);
        replace1(i + 7, buf);

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
    char base[MAX_LINE], off[32], ld_hl_buf[MAX_LINE + 16];

    for (i = 0; i + 6 < nlines; i++) {
        if (!parse_ld_hl_imm(lines[i], base, sizeof(base))) continue;
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

        snprintf(ld_hl_buf, sizeof(ld_hl_buf), "ld hl,%s", base);
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


  /* pass_minmax_pack_call */

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

static int pass_array_base_push_to_de(void)
{
    int i;
    int changed;
    char base[128], index[128];

    changed = 0;

    for (i = 0; i + 7 < nlines; ++i) {
        if (parse_ld_hl_imm(lines[i], base, sizeof(base)) &&
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
            parse_ld_hl_imm(lines[i], base, sizeof(base)) && base[0] != '(' &&
            eq(i + 1, "push hl") &&
            parse_ld_hl_imm(lines[i + 2], index, sizeof(index)) && index[0] == '(' &&
            eq(i + 3, "push hl") &&
            eq(i + 4, "inc hl") &&
            eq(i + 6, "pop hl") &&
            eq(i + 7, "add hl,hl") &&
            eq(i + 8, "ex de,hl") &&
            eq(i + 9, "pop hl") &&
            eq(i + 10, "add hl,de") &&
            peep_de_dead_at(i + 11)) {
            char store[128], expected_store[136], line[180];

            strip_peep_comment_copy(store, lines[i + 5]);
            snprintf(expected_store, sizeof(expected_store), "ld %s,hl", index);
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
int stride_parse_ld_r_ix_neg(const char *s, char r, int *n); /* forward */

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
        if (!parse_ld_de_imm(lines[j], arr_sym, sizeof(arr_sym)) || arr_sym[0] != '_') continue; j++;
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
int stride_parse_ld_r_ix_neg(const char *s, char r, int *n)
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
        if (!parse_ld_de_imm(lines[j], arr_sym, sizeof(arr_sym)) || arr_sym[0] != '_') continue; j++;
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
        if (!parse_ld_de_imm(lines[j], arr_sym, sizeof(arr_sym)) || arr_sym[0] != '_') continue; j++;
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

    if (strstr(line, "@dcc-regalloc-bc-prime") != NULL)
        return 1;
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
int bc_regalloc_claimed_before(int at)
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

/* Parse "push R" or "pop R" for R in {hl,de,bc,af,ix,iy}, reporting which
 * via *reg (lowercase, e.g. "hl") and which mnemonic via *is_push (1 push,
 * 0 pop). Returns 0 for anything else, including a push/pop of a single
 * 8-bit register (not a real Z80 form).
 *
 * ix and iy are tracked for depth-counting purposes only (never returned
 * as a target register from pass_defer_global_push_reload's own
 * search - decline_af_or_frame_reg below excludes them the same way it
 * excludes af) - but they still MUST be recognized here, not just ignored:
 * "push ix / pop hl" is dcc's own standard idiom for copying the current
 * frame pointer into a general register (Z80 has no direct ix-to-hl move),
 * and omitting ix/iy from this recognition set left that idiom's "push ix"
 * uncounted while its own "pop hl" still was - silently unbalancing this
 * function's depth tracking by exactly one level, which let an outer
 * pass_defer_global_push_reload candidate's search match that inner "pop
 * hl" as if it were the outer's own, one push too early. Confirmed as a
 * real miscompile (a corrupted computed value) on tests/forint.c and
 * tests/pint.c, both of which use this exact frame-pointer-to-register
 * idiom heavily for local-array/struct addressing. */
static int peep_parse_push_or_pop(const char *s, char *reg, int *is_push)
{
    char clean[MAX_LINE];
    const char *r;

    strip_peep_comment_copy(clean, s);
    if (!strncmp(clean, "push ", 5)) {
        *is_push = 1;
        r = clean + 5;
    } else if (!strncmp(clean, "pop ", 4)) {
        *is_push = 0;
        r = clean + 4;
    } else {
        return 0;
    }
    if (!strcmp(r, "hl") || !strcmp(r, "de") || !strcmp(r, "bc") ||
        !strcmp(r, "af") || !strcmp(r, "ix") || !strcmp(r, "iy")) {
        strcpy(reg, r);
        return 1;
    }
    return 0;
}

/* True if `reg` (a peep_parse_push_or_pop register name) is never a valid
 * pass_defer_global_push_reload replacement target: af has no memory-load
 * form at all ("ld af,(nn)" is not a Z80 instruction), and ix/iy, while
 * loadable from memory, would change what the reloaded value actually IS
 * if this pass ever matched a push/pop of one of those two directly (it
 * doesn't today - pass_defer_global_push_reload only ever looks for "ld
 * hl,(NAME)/push hl" as the outer candidate's own start, never "push ix" -
 * but this check is what makes that constraint explicit and robust against
 * a future change to that candidate-matching logic, rather than relying on
 * it never changing). */
static int decline_af_or_frame_reg(const char *reg)
{
    return !strcmp(reg, "af") || !strcmp(reg, "ix") || !strcmp(reg, "iy");
}

/*
 * pass_defer_global_push_reload:
 *
 * "ld hl,(NAME) / push hl", immediately followed - possibly much later,
 * with other, independently-balanced push/pop pairs in between - by the
 * matching pop (net push/pop depth reaching zero), with NAME written
 * nowhere in between: the whole point of the push/pop here is to carry
 * NAME's value across some computation that needs HL/DE/BC for itself,
 * then retrieve it back unchanged - which a fresh reload accomplishes for
 * less. "ld hl,(nn)" is 16 T-states; push+pop is 11+10=21; loading NAME
 * once up front and shuttling it through the stack this way costs 16+21=37
 * T-states versus just reloading it fresh at the pop site (16 T-states) -
 * a flat 21 T-state saving, independent of how long the value sits on the
 * stack or what the matching pop's own target register is (the reload
 * simply targets that same register instead).
 *
 * Confirmed as dcc's own default codegen shape for compound expressions
 * like `mem[sym[si].scalar]` (tests/bint.c's OP_LDV/OP_STA and similar):
 * dcc evaluates the outer index expression's own base pointer (mem) first
 * and pushes it immediately, then descends into the inner index expression
 * (sym[si].scalar, itself requiring sym's own base pushed the same way)
 * before finally popping each base back only once the address arithmetic
 * actually needs it - eagerly preserving a value across unrelated work
 * instead of deferring its load to the point of use, the same waste this
 * file's pass_cache_global_word_reload/pass_cache_ix_local_word_reload
 * each address for a *repeated* reload; this is the matched-single-use-
 * push/pop counterpart of that same general problem, so it runs right
 * after them.
 *
 * Declines (leaves the push/pop alone) on:
 *   - anything that writes NAME in between (symbol_written_in_range, the
 *     same whole-word-store text match pass_cache_global_word_reload
 *     already relies on for the identical reason: a write through an
 *     aliased pointer, rather than literal "(NAME),hl" text, isn't visible
 *     to a text-level scan - the same accepted-risk class as that pass,
 *     not a new one here),
 *   - any call (opaque - could write NAME, and this file has no way to
 *     know what an arbitrary callee does),
 *   - any label, jp/jr/djnz, ret, or "sp"/"(sp)" mention - reusing exactly
 *     pass_inline_temp_spill_to_stack's own break conditions, for the
 *     identical reasons: a label means this code might be reached from
 *     elsewhere with a different (or no) value at this stack depth; an
 *     explicit SP mention means push/pop-based depth tracking can no
 *     longer be trusted; and a jump or return leaving this scan's own
 *     straight-line assumption makes it impossible to be sure the matching
 *     pop being searched for is even still reachable from here.
 * Any OTHER balanced push/pop pair in between - including ones this same
 * pass rewrites on a later iteration - is fine and does not end the scan;
 * only the net depth reaching zero at some pop matters.
 *
 * Runs once after the main loop converges (see pass_cache_ix_local_word_
 * reload's own call site comment for why: this pass's own precondition can
 * become satisfiable on an earlier main-loop iteration than a more
 * specific pass's own precondition does if that pass needs some other
 * change first, and once this pass has rewritten the shape a more specific
 * pass wanted to match, that match is gone for good). Since this pass
 * shrinks code (never grows it) and never introduces a label, call, or new
 * push/pop nesting of its own, a single pass here is expected to be enough
 * - pass_labels tidies up.
 */
static int pass_defer_global_push_reload(void)
{
    int i, j;
    int changed = 0;
    char sym[128];
    char reg[4];
    int is_push;
    int depth;
    int pop_line;
    char pop_reg[4];
    char clean[MAX_LINE];

    for (i = 0; i + 1 < nlines; i++) {
        if (!peep_parse_ld_hl_paren_sym(lines[i], sym))
            continue;
        if (!eq(i + 1, "push hl"))
            continue;

        depth = 1;
        pop_line = -1;
        pop_reg[0] = 0;
        for (j = i + 2; j < nlines; j++) {
            if (starts_label(lines[j]) || line_starts_function_marker(lines[j]))
                break;
            strip_peep_comment_lower_copy(clean, lines[j]);
            if (strncmp(clean, "jp ", 3) == 0 || strncmp(clean, "jr ", 3) == 0 ||
                strncmp(clean, "djnz", 4) == 0 || strcmp(clean, "ret") == 0 ||
                strncmp(clean, "ret ", 4) == 0 || strstr(clean, "sp") != NULL)
                break;
            if (strncmp(clean, "call", 4) == 0 &&
                (clean[4] == ' ' || clean[4] == '\t'))
                break;
            if (peep_parse_push_or_pop(lines[j], reg, &is_push)) {
                if (is_push) {
                    depth++;
                } else {
                    depth--;
                    if (depth == 0) {
                        strcpy(pop_reg, reg);
                        pop_line = j;
                        break;
                    }
                }
            }
        }

        if (pop_line < 0 || decline_af_or_frame_reg(pop_reg))
            continue;
        if (symbol_written_in_range(sym, i + 2, pop_line))
            continue;

        {
            char newline[MAX_LINE];
            int target = pop_line - 2;

            sprintf(newline, "ld %s,(%s)", pop_reg, sym);
            delete_n(i, 2);
            replace1_tagged(target, newline, "defer_global_push_reload");
        }
        changed = 1;
        if (i > 0) --i;
    }

    return changed;
}

/* Parse the two-line "ld l,(ix-N)" / "ld h,(ix-(N-1))" shape dcc's own
 * codegen always emits, never split apart or reordered, for a 16-bit
 * ix-relative local's word reload - the ix-frame counterpart of
 * peep_parse_ld_hl_paren_sym's single-line "ld hl,(NAME)" for a global.
 * *n is the low byte's offset magnitude (N). Returns 1 and implicitly
 * consumes lines i and i+1 on success. */
static int peep_parse_ld_hl_ix_pair(int i, int *n)
{
    char tmp[MAX_LINE];
    const char *p;
    int lo;
    char hpat[32];

    strip_peep_comment_copy(tmp, lines[i]);
    if (strncmp(tmp, "ld l,(ix-", 9) != 0)
        return 0;
    p = tmp + 9;
    if (*p < '0' || *p > '9')
        return 0;
    lo = 0;
    while (*p >= '0' && *p <= '9')
        lo = lo * 10 + (*p++ - '0');
    if (*p != ')' || p[1] != 0 || lo <= 1)
        return 0;

    if (i + 1 >= nlines)
        return 0;
    sprintf(hpat, "ld h,(ix-%d)", lo - 1);
    if (!eq(i + 1, hpat))
        return 0;

    *n = lo;
    return 1;
}

/* Is ix-offset `off` (the low byte; off-1 is the paired high byte) written
 * to - via any "ld (ix-off),R" or "ld (ix-off),imm" - anywhere in
 * [start,end)? A write's destination always has a trailing comma right
 * after the closing paren ("ld (ix-N),e"), which a read never does ("ld
 * e,(ix-N)" ends the operand there instead) - the same asymmetry
 * symbol_written_in_range above exploits for "NAME),hl" vs a bare reload,
 * just applied to an ix-relative offset instead of a symbol name. */
static int ix_offset_written_in_range(int off, int start, int end)
{
    char pat_lo[24], pat_hi[24];
    char clean[MAX_LINE];
    int i;

    sprintf(pat_lo, "(ix-%d),", off);
    sprintf(pat_hi, "(ix-%d),", off - 1);
    for (i = start; i < end; i++) {
        strip_peep_comment_copy(clean, lines[i]);
        if (strstr(clean, pat_lo) != NULL || strstr(clean, pat_hi) != NULL)
            return 1;
    }
    return 0;
}

/*
 * pass_cache_ix_local_word_reload:
 *
 * The ix-frame counterpart of pass_cache_global_word_reload above, for the
 * exact same "reloaded more than once in a short hazard-free span" waste,
 * just for a word-sized LOCAL (frame slot) instead of a global. Confirmed
 * on tests/bint.c's OP_STA handler (`mem[sym[si].base+idx] = v;`): si, idx,
 * and v are all plain run()-locals, each materialized once (from an
 * earlier computation or a popv() call) and reloaded once more, tens of
 * instructions of bounds-check logic later, via two full "ld r,(ix+d)"
 * byte reads each way (19 T-states apiece) - none of it inline-call-
 * argument machinery (that's pass_inline_temp_spill_to_stack /
 * inline_temp_de_live's territory, gated on a compiler-emitted tag this
 * pass doesn't need and doesn't look for), so nothing else in this file
 * previously touched it.
 *
 * Shares pass_cache_global_word_reload's entire hazard-segmentation
 * machinery (line_clobbers_bc segment boundaries, the label/function-marker
 * boundary fix from the cobint "too many statements" miscompile,
 * bc_regalloc_claimed_before against dcc's own reg_alloc from the forint.c
 * eval_e miscompile, and the "declined if the segment is bounded by another
 * pass's still-pending load" rule from the tptrlhs.c gpwrap/gpleaf
 * miscompile) - see that pass's own comments for why each of those is
 * load-bearing, not just defensive. The one addition specific to locals: no
 * whole-file write-count proof is needed (unlike a global, an ix-relative
 * offset only means anything within this one function, and a segment
 * already never crosses a call or a function boundary), just that the
 * offset isn't written anywhere in-segment (ix_offset_written_in_range
 * above). Like the global version, this doesn't attempt to prove the
 * local's address was never taken and written through elsewhere without
 * literal "(ix-N)," text - the same "distinctly-stored objects don't
 * alias" assumption already accepted for the global case, validated the
 * same way: full regression plus the extended corpus, not a formal proof.
 *
 * Uses line_clobbers_bc directly for its own segment boundaries - this
 * pass's own target shape (a value materialized once, then reloaded across
 * a run of comparisons) originally hit line_clobbers_bc's "jp c,LABEL"/
 * "jr c,LABEL"/"ret c" false positive (the bare "C" condition code read as
 * a reference to register C) constantly enough, on exactly the
 * bounds-check bodies that motivated this pass, that a local exclusion
 * lived here first - see line_clobbers_bc's own comment for why that fix
 * was moved there instead once it became clear other passes sharing that
 * function (pass_word_loop_var_to_reg_bc, pass_byte_loop_var_to_reg_c, ...)
 * could benefit from it too, not just this one. */

/* Is BC used ANYWHERE in the function containing `at` - not just
 * backward from `at` (bc_regalloc_claimed_before's own scope), and not
 * just for dcc's own compiler-level reg_alloc priming pattern
 * (bc_regalloc_claimed_before's own trigger)? This pass runs after the
 * main fixed-point loop has already fully converged (see this pass's own
 * call site for why), by which point pass_word_loop_var_to_reg_bc and
 * pass_byte_loop_var_to_reg_c may already have promoted some OTHER local
 * into BC/C for the function's ENTIRE body - a dccpeep-level reservation
 * with its own text signature ("; peep: word_loop_var_bc" / "; peep:
 * byte_loop_var_c"), invisible to bc_regalloc_claimed_before, which only
 * recognizes dcc's own "ld c,(ix+d)" compiler-side priming line. Confirmed
 * as a real miscompile (an infinite loop) on tests/tlngnarw.c's heap_pop:
 * pass_word_loop_var_to_reg_bc had already promoted the loop's own index
 * into BC for heap_pop's whole body; this pass, not recognizing that
 * reservation at all, cached a completely unrelated ix-relative local into
 * BC too, silently corrupting the live loop variable.
 *
 * Conservative by construction: rather than enumerate every pass that
 * might reserve BC/C this same way (fragile against any future one this
 * file doesn't know about yet), this declines to cache anything in BC
 * anywhere in a function where BC or C is mentioned at all, by
 * line_clobbers_bc's own definition - the same per-token test already used
 * for this pass's own segment boundaries, just applied to the whole
 * function instead of one segment. Strictly more conservative than
 * necessary (a function that uses C only in a part that provably can't
 * overlap this pass's own candidate span still declines), but simple, and
 * matches this codebase's own established default of forgoing a smaller
 * optimization rather than risking a data-corrupting one. */
static int ix_cache_bc_used_in_function(int at)
{
    int func_start, func_end;
    int k;

    /* Lines this pass itself already tagged, from an earlier (necessarily
     * lower-indexed - segments are processed in file order) segment in
     * this same function, are excluded: those caches are segment-scoped
     * (bounded by line_clobbers_bc the same way any other candidate
     * segment is), not whole-function reservations, so an already-closed
     * one from earlier in this function must not prevent a later,
     * unrelated segment from caching something of its own - without this
     * exclusion, only the very first segment in any given function could
     * ever benefit, which measured as most of this pass's value on
     * tests/bint.c alone. Everything else that mentions B or C - dcc's own
     * compiler priming, pass_word_loop_var_to_reg_bc,
     * pass_byte_loop_var_to_reg_c, or anything this file doesn't have a
     * name for yet - still counts, by design (see this function's own
     * comment above). */
    find_function_bounds_any(at, &func_start, &func_end);
    for (k = func_start; k < func_end; ++k) {
        if (strstr(lines[k], "ix_local_word_cache"))
            continue;
        if (line_clobbers_bc(lines[k]))
            return 1;
    }
    return 0;
}
static int pass_cache_ix_local_word_reload(void)
{
    int i;
    int changed = 0;
    int segstart;

    segstart = 0;
    for (i = 0; i <= nlines; i++) {
        int j, k;
        int off;
        int best_off, best_count;
        struct { int off; int count; } seen[32];
        int nseen;
        int occ[64];
        int noc;
        int delta;

        if (i < nlines && !line_clobbers_bc(lines[i]) &&
            !starts_label(lines[i]) && !line_starts_function_marker(lines[i]))
            continue;

        /* [segstart, i) is one hazard-free segment. Find the best repeated
         * ix-relative word reload within it. Scanning one line at a time
         * (rather than skipping past a matched pair's second line) is safe:
         * a pair's own second line ("ld h,(ix-(N-1))") never itself starts
         * with "ld l,(ix-", so it silently fails to match as some other
         * pair's first line and the scan just continues. */
        nseen = 0;
        for (j = segstart; j < i; j++) {
            if (!peep_parse_ld_hl_ix_pair(j, &off))
                continue;
            for (k = 0; k < nseen; k++)
                if (seen[k].off == off) break;
            if (k == nseen) {
                if (nseen < 32) { seen[nseen].off = off; seen[nseen].count = 1; nseen++; }
            } else {
                seen[k].count++;
            }
        }

        best_count = 0;
        best_off = 0;
        for (k = 0; k < nseen; k++) {
            if (seen[k].count > best_count) {
                best_count = seen[k].count;
                best_off = seen[k].off;
            }
        }

        /* See pass_cache_global_word_reload's own identical check (tptrlhs.c
         * gpwrap/gpleaf miscompile) - refuse to start a brand new cache in a
         * segment bounded by anyone else's (or this pass's own, from an
         * earlier segment) still-pending load. */
        if (i < nlines && (strstr(lines[i], "global_word_cache_load") ||
                            strstr(lines[i], "ix_local_word_cache_load")))
            best_count = 0;

        /* >= 3, not >= 2: see pass_cache_global_word_reload's own comment
         * for the cost/benefit reasoning behind requiring a clear margin,
         * not just a break-even one - the numbers differ here (a two-byte
         * ix-relative reload is 38 T-states, not 16, so the margin per
         * avoided reload is even wider than the global case's), but the
         * same "don't rewrite a pattern some other, more specific pass might
         * still want to match in its original form" caution applies, and
         * >= 3 is the threshold already proven safe for that. */
        if (best_count >= 3 && !bc_regalloc_claimed_before(i) &&
            !ix_cache_bc_used_in_function(i)) {
            noc = 0;
            for (j = segstart; j < i; j++) {
                if (!peep_parse_ld_hl_ix_pair(j, &off)) continue;
                if (off != best_off) continue;
                if (noc < 64) occ[noc++] = j;
            }

            /* Unlike symbol_written_in_range for a global (checked across
             * the WHOLE segment, since a global can legitimately be read
             * before this segment even starts), a local's own leading
             * write - materializing it in the first place - is always
             * inside [segstart, occ[0]) here: every local is written
             * before its first read, so checking from segstart would flag
             * that ordinary initialization as a hazard on every single
             * candidate, never firing at all. What actually matters is
             * whether the slot is reassigned anywhere from occ[0]'s own
             * reload onward - occ[0]+2 skips past that reload's own two
             * lines, which read, not write, the slot. */
            if (noc > 0 && ix_offset_written_in_range(best_off, occ[0] + 2, i))
                noc = 0;

            if (noc > 0) {
                delta = 0;
                /* Last occurrence first, same reasoning as
                 * pass_cache_global_word_reload: insert/delete only ever
                 * shift indices at or after the edit point. Each
                 * occurrence is a 2-line pair ("ld l,.."/"ld h,.."),
                 * replaced with a 2-line register move ("ld l,c"/"ld
                 * h,b"), so no line-count delta at the occurrence itself. */
                for (k = noc - 1; k >= 1; k--) {
                    replace1_tagged(occ[k], "ld l,c", "ix_local_word_cache_load");
                    replace1(occ[k] + 1, "ld h,b");
                    changed = 1;
                }

                /* occ[0] is left as the real reload, with the cache primed
                 * right after it. */
                insert_line_tagged(occ[0] + 2, "ld c,l", "ix_local_word_cache_store");
                insert_line(occ[0] + 3, "ld b,h");
                delta = 2;
                changed = 1;

                i += delta;
            }
        }

        segstart = i + 1;
    }

    return changed;
}

/* Forward declarations: both defined further down this file (line_mentions_iy
 * alongside pass_promote_ix_pointer_to_iy, peep_parse_ld_de_ix_pair alongside
 * that same pass's other parse helpers), needed here for pass_cache_ix_
 * spill_via_iy below. */
static int line_mentions_iy(const char *line);
static int peep_parse_ld_de_ix_pair(int line, int *offset);

static int line_is_call_or_rst(const char *line)
{
    char clean[MAX_LINE];

    strip_peep_comment_lower_copy(clean, line);
    if (strncmp(clean, "call", 4) == 0 && (clean[4] == ' ' || clean[4] == '\t'))
        return 1;
    if (strncmp(clean, "rst", 3) == 0 && (clean[3] == ' ' || clean[3] == '\t'))
        return 1;
    return 0;
}

/* Is IY mentioned anywhere in the function containing line `at`, EXCLUDING
 * lines this same pass already tagged (ix_spill_iy) from an earlier
 * candidate elsewhere in the same function? The exclusion matters: switch
 * cases are mutually exclusive at runtime, so two DIFFERENT cases (e.g.
 * fint.c's OP_ADD and OP_SUB, each independently store/reload their own
 * short-lived temp) never actually contend for IY even though both this
 * pass's rewrites live in the same function's text - without the
 * exclusion, only the first candidate in any given function could ever
 * benefit, the same "only the very first segment could benefit" trap
 * ix_cache_bc_used_in_function's own comment (just above) already
 * documents and works around for the BC case. Everything else that
 * mentions IY - dcc's own compiler output (never emits it) or
 * pass_promote_ix_pointer_to_iy's whole-function reservation - still
 * counts, by design: those really do need to reserve IY for the entire
 * function, or its whole-file scope in that pass's own case. */
static int iy_used_in_function(int at)
{
    int func_start, func_end;
    int k;

    find_function_bounds_any(at, &func_start, &func_end);
    for (k = func_start; k < func_end; ++k) {
        if (strstr(lines[k], "ix_spill_iy") != NULL)
            continue;
        if (line_mentions_iy(lines[k]))
            return 1;
    }
    return 0;
}

/* Is ix-offset `off` (signed, as returned by peep_parse_st_ix_pair/
 * peep_parse_ld_de_ix_pair - NOT the positive-magnitude convention
 * ix_offset_written_in_range above uses) referenced anywhere in
 * [func_start,func_end) OTHER than at the four given line indices (the
 * matched store's two lines and the matched reload's two lines)? Read OR
 * write, any register - stronger than ix_offset_written_in_range needs,
 * because this pass eliminates the frame slot's only store and only
 * reload both (pass_cache_ix_local_word_reload, by contrast, keeps a real
 * first store/reload pair around and only rewrites repeats, so it only
 * ever needs to rule out a WRITE, never a read, in between). */
static int ix_offset_pair_referenced_outside(int off, int func_start, int func_end,
                                             int excl_a, int excl_b, int excl_c, int excl_d)
{
    char pat_lo[24], pat_hi[24];
    int i;

    sprintf(pat_lo, "(ix%+d)", off);
    sprintf(pat_hi, "(ix%+d)", off + 1);
    for (i = func_start; i < func_end; ++i) {
        if (i == excl_a || i == excl_b || i == excl_c || i == excl_d)
            continue;
        if (strstr(lines[i], pat_lo) != NULL || strstr(lines[i], pat_hi) != NULL)
            return 1;
    }
    return 0;
}

/*
 * pass_cache_ix_spill_via_iy:
 *
 * A block-scoped C temp materialized once from a computed value and read
 * back exactly once shortly after (e.g. `{ int _t = lst[--lsp]; lst[lsp-1]
 * += _t; }` - fint.c's OP_ADD/SUB/MUL/EQ/NE/LT/GT/AND/OR, hit on nearly
 * every binary VM opcode) round-trips through its own frame slot: `ld
 * (ix+N),l` / `ld (ix+N+1),h` to store (38T), then later `ld e,(ix+N)` /
 * `ld d,(ix+N+1)` to reload (38T) - 76T total for a value that's dead the
 * instant it's reloaded. pass_cache_ix_local_word_reload (just above)
 * targets a related but different shape - a value reloaded 3+ times, kept
 * in BC - and never fires here: its own >=3-reload threshold is never met
 * by a single store-then-single-reload. BC itself is also frequently
 * unavailable for this shape specifically: run_at-style giant dispatch
 * functions often already reserve BC/C for a DIFFERENT, whole-function
 * global-pointer cache (pass_cache_global_word_reload), which
 * ix_cache_bc_used_in_function's own deliberately whole-function-
 * conservative check (see its comment) correctly declines to disturb.
 *
 * IY, however, is very often completely unused in exactly these functions.
 * Cache the value there instead, via push/pop - the same documented-Z80
 * idiom pass_promote_ix_pointer_to_iy already uses, not the undocumented
 * ld iyl/iyh 8-bit forms: `push hl` / `pop iy` to store (25T), `push iy` /
 * `pop de` to reload (25T) - 50T total, still a real ~34% cut.
 *
 * Narrower than pass_promote_ix_pointer_to_iy's whole-function register
 * promotion: IY only needs to survive a short, straight-line span here, so
 * this requires - rather than proving every call in the whole file is
 * IY-safe - simply that no call/rst and no label fall between the store
 * and the matched reload (a call to code outside this file could touch IY
 * in ways this file's text can't see; a label would let some other path
 * reach the reload without ever running the store). Requires IY to be
 * unused elsewhere in the containing function (iy_used_in_function mirrors
 * ix_cache_bc_used_in_function's own per-function conservatism, just for
 * IY - see its own comment for why switch-case candidates still coexist
 * safely despite this), and the frame slot to be referenced nowhere else
 * in the function at all (ix_offset_pair_referenced_outside - stronger
 * than pass_cache_ix_local_word_reload needs, since that pass keeps the
 * real memory slot around for its first, unrewritten occurrence - this
 * pass eliminates the memory slot's only store and only reload both, so
 * nothing else may depend on it holding the value).
 */
static int pass_cache_ix_spill_via_iy(void)
{
    int i;
    int changed = 0;

    for (i = 0; i + 1 < nlines; ++i) {
        int off, reload_off;
        int func_start, func_end;
        int reload_line;
        int k;

        if (!peep_parse_st_ix_pair(lines[i], lines[i + 1], &off))
            continue;

        if (iy_used_in_function(i))
            continue;

        find_function_bounds_any(i, &func_start, &func_end);

        reload_line = -1;
        for (k = i + 2; k < func_end; ++k) {
            int other_off;

            if (starts_label(lines[k]) || line_starts_function_marker(lines[k]) ||
                line_is_call_or_rst(lines[k]))
                break;
            if (peep_parse_ld_de_ix_pair(k, &reload_off) && reload_off == off) {
                reload_line = k;
                break;
            }
            /* A DIFFERENT local's own store (e.g. tests/tstruct.c's
             * `i = "Karina"; j = "Winter";` - two distinct register-char*
             * locals, each independently store/reload-shaped) landing
             * inside THIS candidate's span means the two candidates'
             * store-to-reload spans overlap: both would claim IY for
             * themselves, and whichever fires second clobbers the first's
             * still-pending value before its own reload ever runs - a real
             * miscompile (tstruct's stack[0]/[1] fields all reading back
             * the LAST-stored string instead of their own) caught by the
             * full suite before this pass ever shipped. Declining whenever
             * another store-shaped line appears in the span - regardless
             * of whether IT ends up qualifying as its own candidate - is
             * the simple, sufficient fix: it forces every accepted
             * candidate's span to be genuinely self-contained. */
            if (k != i && k + 1 < nlines &&
                peep_parse_st_ix_pair(lines[k], lines[k + 1], &other_off)) {
                break;
            }
        }
        if (reload_line < 0)
            continue;

        if (ix_offset_pair_referenced_outside(off, func_start, func_end,
                                              i, i + 1, reload_line, reload_line + 1))
            continue;

        replace1_tagged(i, "push hl", "ix_spill_iy");
        replace1_tagged(i + 1, "pop iy", "ix_spill_iy");
        replace1_tagged(reload_line, "push iy", "ix_spill_iy");
        replace1(reload_line + 1, "pop de");
        changed = 1;
    }

    return changed;
}

/* Parse dcc's four-line little-endian long load from consecutive ix offsets:
 * ld l,(ix+N) / ld h,(ix+N+1) / ld e,(ix+N+2) / ld d,(ix+N+3). */
static int peep_parse_ld_long_ix_at(int line, int *offset)
{
    char eoff[32], doff[32];
    int lo, e, d;

    if (line < 0 || line + 3 >= nlines ||
        !peep_parse_ld_ix_pair(lines[line], lines[line + 1], &lo) ||
        !peep_parse_ld_e_ix(lines[line + 2], eoff) ||
        !peep_parse_ld_d_ix(lines[line + 3], doff) ||
        !parse_ix_off_numeric(eoff, &e) ||
        !parse_ix_off_numeric(doff, &d) || e != lo + 2 || d != lo + 3)
        return 0;
    *offset = lo;
    return 1;
}

static int ix_long_slot_written_in_range(int offset, int start, int end)
{
    char patterns[4][24];
    char clean[MAX_LINE];
    int i, byte;

    for (byte = 0; byte < 4; ++byte)
        sprintf(patterns[byte], "(ix%+d),", offset + byte);
    for (i = start; i < end; ++i) {
        strip_peep_comment_copy(clean, lines[i]);
        for (byte = 0; byte < 4; ++byte)
            if (strstr(clean, patterns[byte]) != NULL)
                return 1;
    }
    return 0;
}

/* Repeated long-parameter loads in small framed leaf helpers are expensive:
 * four indexed loads cost 76T and 12 bytes each time. Save the first DE:HL
 * value beneath the working stack, then restore and re-save it at every later
 * occurrence (42T, 4 bytes). The canonical IX epilogue discards the saved copy
 * on every exit path. */
static int pass_cache_ix_long_param_reload(void)
{
    int i;
    int changed = 0;

    for (i = 3; i + 3 < nlines; ++i) {
        int offset, func_start, func_end;
        int prologue_line;
        int occurrences[64];
        int occurrence_count = 0;
        int k, other_offset;
        int safe = 1;
        int saw_epilogue = 0;

        prologue_line = -1;
        if (eq(i - 3, "push ix") && eq(i - 2, "ld ix,0") &&
            eq(i - 1, "add ix,sp"))
            prologue_line = i - 3;
        else if (i >= 4 && eq(i - 4, "push ix") &&
                 eq(i - 3, "ld ix,0") && eq(i - 2, "add ix,sp") &&
                 eq(i - 1, "call __stchk"))
            prologue_line = i - 4;
        if (prologue_line < 0 || !peep_parse_ld_long_ix_at(i, &offset))
            continue;

        find_function_bounds_any(i, &func_start, &func_end);
        if (func_start > prologue_line - 1 || offset < 4)
            continue;

        for (k = i; k + 3 < func_end; ++k) {
            if (!peep_parse_ld_long_ix_at(k, &other_offset))
                continue;
            if (other_offset != offset) {
                safe = 0;
                break;
            }
            if (occurrence_count >= 64) {
                safe = 0;
                break;
            }
            occurrences[occurrence_count++] = k;
            k += 3;
        }
        if (!safe || occurrence_count < 3 ||
            ix_long_slot_written_in_range(offset, i + 4, func_end))
            continue;

        for (k = i + 4; k < func_end && safe; ++k) {
            char clean[MAX_LINE];
            char target[128];

            if (eq(k, "ld sp,ix") && k + 2 < func_end &&
                eq(k + 1, "pop ix") && eq(k + 2, "ret")) {
                saw_epilogue = 1;
                k += 2;
                continue;
            }
            strip_peep_comment_copy(clean, lines[k]);
            if (!strncmp(clean, "call ", 5) || !strncmp(clean, "rst ", 4) ||
                !strcmp(clean, "exx") || !strncmp(clean, "push ", 5) ||
                !strncmp(clean, "pop ", 4) || !strncmp(clean, "ret", 3) ||
                strstr(clean, "sp") != NULL) {
                safe = 0;
                break;
            }
            if (jump_target_any(clean, target) &&
                find_label_line_in_range(target, func_start, func_end) < 0) {
                safe = 0;
                break;
            }
        }
        if (!safe || !saw_epilogue)
            continue;

        for (k = occurrence_count - 1; k >= 1; --k) {
            int line = occurrences[k];
            replace1_tagged(line, "pop hl", "ix_long_param_cache_load");
            replace1(line + 1, "pop de");
            replace1(line + 2, "push de");
            replace1(line + 3, "push hl");
        }
        insert_line_tagged(i + 4, "push de", "ix_long_param_cache_store");
        insert_line(i + 5, "push hl");
        changed = 1;
        i = func_end + 1;
    }
    return changed;
}

static int all_compare_flags_dead_from(int start)
{
    int i;
    char clean[MAX_LINE];

    for (i = start; i < start + 20 && i < nlines; ++i) {
        strip_peep_comment_copy(clean, lines[i]);
        if (starts_label(clean) || !strncmp(clean, "jp ", 3) ||
            !strncmp(clean, "jr ", 3) || !strncmp(clean, "ret", 3) ||
            !strncmp(clean, "djnz", 4))
            return 0;
        if (!strncmp(clean, "or ", 3) || !strncmp(clean, "and ", 4) ||
            !strncmp(clean, "xor ", 4) || !strncmp(clean, "cp ", 3) ||
            !strncmp(clean, "add a,", 6) || !strncmp(clean, "sub ", 4) ||
            !strncmp(clean, "sbc ", 4) || !strncmp(clean, "adc ", 4) ||
            !strncmp(clean, "call ", 5))
            return 1;
    }
    return 0;
}

static int is_uncond_jr(const char *s);

static int flags_dead_after_resolved_jump(int line, int func_start, int func_end)
{
    char target[128];
    int target_line;

    if (all_compare_flags_dead_from(line))
        return 1;
    if (line < 0 || line >= func_end ||
        (!is_uncond_jp(lines[line]) && !is_uncond_jr(lines[line])) ||
        !jump_target_any(lines[line], target))
        return 0;
    target_line = find_label_line_in_range(target, func_start, func_end);
    return target_line >= 0 && all_compare_flags_dead_from(target_line + 1);
}

/* Preserve an IX-loaded pointer across `left < right` loop-bound tests.
 * DCC normally computes left-right in HL, then reloads left for an immediate
 * dereference. Computing right-left instead leaves left in DE; carry or zero
 * means left>=right, and EX DE,HL restores left on the continuing path. */
static int pass_preserve_ix_pointer_compare(void)
{
    int i;
    int changed = 0;

    for (i = 0; i + 12 < nlines; ++i) {
        int left, right;
        int eoff, doff;
        char ebuf[32], dbuf[32];
        char left_text[32], right_text[32];
        char target[128];
        char line[160];

        if (!peep_parse_ld_ix_pair(lines[i], lines[i + 1], &left) ||
            !peep_parse_ld_e_ix(lines[i + 2], ebuf) ||
            !peep_parse_ld_d_ix(lines[i + 3], dbuf) ||
            !parse_ix_off_numeric(ebuf, &eoff) ||
            !parse_ix_off_numeric(dbuf, &doff) || doff != eoff + 1)
            continue;
        right = eoff;
        if (!eq(i + 4, "or a") || !eq(i + 5, "sbc hl,de") ||
            !parse_jp_nc_label(lines[i + 6], target) ||
            !peep_parse_ld_ix_pair(lines[i + 7], lines[i + 8], &eoff) ||
            eoff != left || !eq(i + 9, "ld e,(hl)") ||
            !eq(i + 10, "inc hl") || !eq(i + 11, "ld d,(hl)") ||
            !eq(i + 12, "ex de,hl") ||
            !all_compare_flags_dead_from(i + 13))
            continue;

        peep_format_ix_off(left_text, left);
        peep_format_ix_off(right_text, right);
        sprintf(line, "ld l,(ix%s)", right_text);
        replace1_tagged(i, line, "preserve_ix_pointer_compare");
        sprintf(line, "ld h,(ix%+d)", right + 1);
        replace1(i + 1, line);
        sprintf(line, "ld e,(ix%s)", left_text);
        replace1(i + 2, line);
        sprintf(line, "ld d,(ix%+d)", left + 1);
        replace1(i + 3, line);
        sprintf(line, "jp c, %s", target);
        replace1(i + 6, line);
        sprintf(line, "jp z, %s", target);
        replace1(i + 7, line);
        replace1(i + 8, "ex de,hl");
        changed = 1;
    }
    return changed;
}

static int parse_nz_jump_any(const char *line, char *target)
{
    char clean[MAX_LINE];

    strip_peep_comment_copy(clean, line);
    if (strncmp(clean, "jp nz,", 6) != 0 &&
        strncmp(clean, "jr nz,", 6) != 0)
        return 0;
    return jump_target_any(clean, target);
}

static int count_jumps_any_to_label(const char *label)
{
    int i, count = 0;
    char target[128];

    for (i = 0; i < nlines; ++i)
        if (jump_target_any(lines[i], target) && !strcmp(target, label))
            ++count;
    return count;
}

/* Collapse a serial chain of equality tests against small constants:
 *
 *   ld (ix+N),l              ld (ix+N),l
 *   ld (ix+N+1),h            ld (ix+N+1),h
 *   ld de,K1                 ld a,h
 *   or a                     or a
 *   sbc hl,de                jp nz,Lfallback
 *   jp nz,Lnext              ld a,l
 *                         -> cp K1
 * Lnext:                     jp nz,Lnext
 *   ld l,(ix+N)           Lnext:
 *   ld h,(ix+N+1)            cp K2
 *   ld de,K2                 jp nz,...
 *   or a
 *   sbc hl,de
 *   jp nz,...
 *
 * The high-byte jump goes to the final failure target, exactly where the
 * original chain arrives after every small constant fails. Intermediate
 * labels must have one incoming jump, so retaining A across them cannot be
 * bypassed by another path. CP preserves the equality result used by each
 * original NZ branch; no carry-dependent use is admitted. */
static int pass_ix_word_small_eq_chain(void)
{
    int i;

    for (i = 0; i + 5 < nlines; ++i) {
        int slot, first_value, compare_count = 1;
        int label_lines[32], values[32];
        int previous_line = i;
        int func_start, func_end;
        char target[128], fallback[128];
        char first_jump[MAX_LINE], line[MAX_LINE];

        if (!peep_parse_st_ix_pair(lines[i], lines[i + 1], &slot) ||
            !peep_parse_ld_de_0_to_255(lines[i + 2], &first_value) ||
            first_value == 0 || !eq(i + 3, "or a") ||
            !eq(i + 4, "sbc hl,de") ||
            !parse_nz_jump_any(lines[i + 5], target))
            continue;

        find_function_bounds_any(i, &func_start, &func_end);
        strcpy(first_jump, lines[i + 5]);
        while (compare_count < 32) {
            int label_line, next_slot, value;
            char next_target[128];

            label_line = find_label_line_in_range(target, func_start, func_end);
            if (label_line <= previous_line || label_line + 6 >= func_end ||
                count_jumps_any_to_label(target) != 1 ||
                !peep_parse_ld_ix_pair(lines[label_line + 1],
                                       lines[label_line + 2], &next_slot) ||
                next_slot != slot ||
                !peep_parse_ld_de_0_to_255(lines[label_line + 3], &value) ||
                value == 0 || !eq(label_line + 4, "or a") ||
                !eq(label_line + 5, "sbc hl,de") ||
                !parse_nz_jump_any(lines[label_line + 6], next_target))
                break;

            label_lines[compare_count] = label_line;
            values[compare_count] = value;
            ++compare_count;
            previous_line = label_line;
            strcpy(target, next_target);
        }
        if (compare_count < 3)
            continue;
        strcpy(fallback, target);

        while (--compare_count > 0) {
            int label_line = label_lines[compare_count];
            sprintf(line, "cp %d", values[compare_count]);
            replace1_tagged(label_line + 1, line, "ix_word_small_eq_chain");
            replace1(label_line + 2, lines[label_line + 6]);
            delete_n(label_line + 3, 4);
        }

        replace1_tagged(i + 2, "ld a,h", "ix_word_small_eq_chain");
        sprintf(line, "jp nz, %s", fallback);
        replace1(i + 4, line);
        replace1(i + 5, "ld a,l");
        sprintf(line, "cp %d", first_value);
        insert_line(i + 6, line);
        insert_line(i + 7, first_jump);
        return 1;
    }
    return 0;
}

static int line_mentions_iy(const char *line)
{
    char clean[MAX_LINE];
    const char *p;

    strip_peep_comment_lower_copy(clean, line);
    if (!strncmp(clean, "db 0fdh,", 8))
        return 1;
    p = clean;
    while (*p) {
        if (isalnum((unsigned char)*p) || *p == '_') {
            const char *start = p;
            int length = 0;
            while (isalnum((unsigned char)*p) || *p == '_') {
                ++p;
                ++length;
            }
            if ((length == 2 && !strncmp(start, "iy", 2)) ||
                (length == 3 && (!strncmp(start, "iyh", 3) ||
                                 !strncmp(start, "iyl", 3))))
                return 1;
        } else {
            ++p;
        }
    }
    return 0;
}

static int peep_parse_ld_de_ix_pair(int line, int *offset)
{
    char ebuf[32], dbuf[32];
    int e, d;

    if (line < 0 || line + 1 >= nlines ||
        !peep_parse_ld_e_ix(lines[line], ebuf) ||
        !peep_parse_ld_d_ix(lines[line + 1], dbuf) ||
        !parse_ix_off_numeric(ebuf, &e) || !parse_ix_off_numeric(dbuf, &d) ||
        d != e + 1)
        return 0;
    *offset = e;
    return 1;
}

static int parse_small_add_a(const char *line, int *amount)
{
    char clean[MAX_LINE];
    char *end;
    long value;

    strip_peep_comment_copy(clean, line);
    if (strncmp(clean, "add a,", 6) != 0)
        return 0;
    value = strtol(clean + 6, &end, 10);
    if (*end || value < 1 || value > 4)
        return 0;
    *amount = (int)value;
    return 1;
}

/* Promote one frame-resident pointer in a closed static helper to documented
 * IY. The candidate must have one HL initialization, only canonical HL/DE
 * reloads, and one small carry-skip increment whose flags are dead at the
 * loop target. Calls may only target same-file functions (the whole file is
 * proven IY-free) or DCCRTL's __ helpers, which never touch IY. */
static int pass_promote_ix_pointer_to_iy(void)
{
    int i, k;

    for (i = 0; i < nlines; ++i)
        if (line_mentions_iy(lines[i]))
            return 0;

    for (i = 0; i < nlines; ++i) {
        int func_start, func_end;
        int offset, init_line = -1, increment_line = -1, increment_amount = 0;
        int best_candidate_loads = -1;
        int loads[128], load_kinds[128], load_count = 0;
        int safe = 1;
        char low_pat[24], high_pat[24];

        if (strncmp(lines[i], "; static function ", 18) != 0)
            continue;
        find_function_bounds_any(i + 1, &func_start, &func_end);
        if (func_start != i)
            continue;

        /* Find a single canonical small increment candidate first. */
        for (k = i + 1; k + 5 < func_end; ++k) {
            char offbuf[32], storebuf[32], highbuf[48], target[128];
            int parsed_offset, stored_offset;
            int candidate_loads = 0;
            int candidate_amount;
            int q, qoff;

            if (!peep_parse_ld_a_ix(lines[k], offbuf) ||
                !parse_ix_off_numeric(offbuf, &parsed_offset) ||
                !parse_small_add_a(lines[k + 1], &candidate_amount) ||
                !peep_parse_ld_ix_a(lines[k + 2], storebuf) ||
                !parse_ix_off_numeric(storebuf, &stored_offset) ||
                stored_offset != parsed_offset ||
                !peep_parse_inc_ix_byte(lines[k + 4], &stored_offset) ||
                stored_offset != parsed_offset + 1 ||
                !label_name_at(k + 5, target))
                continue;
            sprintf(highbuf, "jr nc,%s", target);
            if (!eq(k + 3, highbuf)) {
                sprintf(highbuf, "jp nc, %s", target);
                if (!eq(k + 3, highbuf))
                    continue;
            }
            if (!flags_dead_after_resolved_jump(k + 6, func_start, func_end))
                continue;
            for (q = i + 1; q + 1 < func_end; ++q) {
                if ((peep_parse_ld_ix_pair(lines[q], lines[q + 1], &qoff) ||
                     peep_parse_ld_de_ix_pair(q, &qoff)) &&
                    qoff == parsed_offset) {
                    ++candidate_loads;
                    ++q;
                }
            }
            if (candidate_loads > best_candidate_loads) {
                best_candidate_loads = candidate_loads;
                increment_line = k;
                offset = parsed_offset;
                increment_amount = candidate_amount;
            }
        }
        if (!safe || increment_line < 0)
            continue;

        peep_format_ix_off(low_pat, offset);
        peep_format_ix_off(high_pat, offset + 1);
        for (k = i + 1; k < func_end && safe; ++k) {
            int parsed_offset;
            char clean[MAX_LINE], target[128];

            if (k == increment_line) {
                k += 5;
                continue;
            }
            if (k + 1 < func_end &&
                peep_parse_st_ix_pair(lines[k], lines[k + 1], &parsed_offset) &&
                parsed_offset == offset) {
                if (init_line >= 0) {
                    safe = 0;
                    break;
                }
                init_line = k;
                ++k;
                continue;
            }
            if (k + 1 < func_end &&
                peep_parse_ld_ix_pair(lines[k], lines[k + 1], &parsed_offset) &&
                parsed_offset == offset) {
                if (load_count >= 128) { safe = 0; break; }
                loads[load_count] = k;
                load_kinds[load_count++] = 0;
                ++k;
                continue;
            }
            if (peep_parse_ld_de_ix_pair(k, &parsed_offset) &&
                parsed_offset == offset) {
                if (load_count >= 128) { safe = 0; break; }
                loads[load_count] = k;
                load_kinds[load_count++] = 1;
                ++k;
                continue;
            }
            strip_peep_comment_copy(clean, lines[k]);
            if (strstr(clean, low_pat) || strstr(clean, high_pat)) {
                safe = 0;
                break;
            }
            if (!strncmp(clean, "call ", 5)) {
                const char *callee = clean + 5;
                int is_runtime_call;

                while (*callee == ' ' || *callee == '\t') ++callee;
                is_runtime_call = callee[0] == '_' && callee[1] == '_' &&
                                  callee[2] != '_';
                if (!is_runtime_call && !is_local_func_label(callee)) {
                    safe = 0;
                    break;
                }
            }
            if (jump_target_any(clean, target) &&
                target[0] != '(' &&
                find_label_line_in_range(target, func_start, func_end) < 0) {
                safe = 0;
                break;
            }
        }
        if (!safe || init_line < 0 || load_count < 3 || init_line >= increment_line)
            continue;

        replace1_tagged(init_line, "push hl", "ix_pointer_to_iy");
        replace1(init_line + 1, "pop iy");
        for (k = 0; k < load_count; ++k) {
            replace1_tagged(loads[k], "push iy", "ix_pointer_to_iy");
            replace1(loads[k] + 1, load_kinds[k] ? "pop de" : "pop hl");
        }
        for (k = 0; k < increment_amount; ++k)
            replace1_tagged(increment_line + k, "inc iy", "ix_pointer_to_iy");
        delete_n(increment_line + increment_amount, 5 - increment_amount);
        return 1;
    }
    return 0;
}

/* Parse "ld (ix-N),R" for a single 8-bit register R, extracting N. Unlike
 * stride_parse_ld_ix_neg_r (which shares this exact shape but doesn't
 * strip a trailing peep-comment tag before comparing), this pass runs
 * after the main loop has converged, when an earlier pass may already
 * have tagged this exact line for an unrelated reason - stripping first
 * is required for a reliable match at this point in the pipeline. */
static int peep_parse_st_ix_neg_reg(const char *s, char reg, int *n)
{
    char tmp[MAX_LINE];
    char suffix[8];
    const char *p;
    int v;

    strip_peep_comment_copy(tmp, s);
    if (strncmp(tmp, "ld (ix-", 7) != 0)
        return 0;
    p = tmp + 7;
    if (*p < '0' || *p > '9')
        return 0;
    v = 0;
    while (*p >= '0' && *p <= '9')
        v = v * 10 + (*p++ - '0');
    sprintf(suffix, "),%c", reg);
    if (strcmp(p, suffix) != 0 || v <= 0)
        return 0;
    *n = v;
    return 1;
}

/* Parse "ld de,K" for a signed integer constant K (dcc emits negative
 * constants as literal "ld de,-K" text, never a normalized unsigned
 * 16-bit form - see e.g. pass_stride_loop_to_ptr's own comment on this),
 * requiring 0 < |K| <= 255 so a single 8-bit add/sub can stand in for the
 * low byte of a full 16-bit add/sub (pass_small_const_incr_carry_skip's
 * own precondition; K==0 would be a no-op not worth rewriting). */
static int peep_parse_ld_de_small_const(const char *s, int *k)
{
    char tmp[MAX_LINE];
    const char *p;
    int sign;
    long v;

    strip_peep_comment_copy(tmp, s);
    if (strncmp(tmp, "ld de,", 6) != 0)
        return 0;
    p = tmp + 6;
    sign = 1;
    if (*p == '-') { sign = -1; p++; }
    if (*p < '0' || *p > '9')
        return 0;
    v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p++ - '0');
        if (v > 255)
            return 0;
    }
    if (*p != 0 || v == 0)
        return 0;
    *k = (int)(sign * v);
    return 1;
}

/*
 * pass_small_const_incr_carry_skip:
 *
 * "ld l,(ix-N) / ld h,(ix-(N-1)) / ld de,K / add hl,de / ld (ix-N),l /
 * ld (ix-(N-1)),h" (dcc's own codegen for `local += K;` or `local++;` on a
 * word-sized ix-relative local, K a compile-time constant small enough to
 * fit one byte) does a full 16-bit add every time: 19+19+10+11+19+19 = 97
 * T-states, unconditionally. Since only the low byte of a small constant
 * ever needs adding, and the low byte overflowing into the high byte (a
 * carry, for a positive K; a borrow, for a negative one) is rare, doing
 * the low byte's add/sub with an 8-bit register and only touching the
 * high byte on the rare carry/borrow is cheaper on both paths, not just
 * the common one:
 *
 *   ld a,(ix-N)      19
 *   add a,K / sub a,|K|   7
 *   ld (ix-N),a      19
 *   jp nc,SKIP / jp c,SKIP   10   (common case: no carry/borrow - done)
 *   inc (ix-(N-1)) / dec (ix-(N-1))   23   (rare case only)
 * SKIP:
 *
 * Common-case total: 19+7+19+10 = 55 T-states (a 42 T-state, 43% saving).
 * Rare-case total: 55+23 = 78 T-states (still a 19 T-state saving) - this
 * optimization has no downside case at all, unlike most that trade a rare
 * path's cost for a common one's.
 *
 * "ld (ix-N),a" and "inc"/"dec (ix-(N-1))" don't affect any flag ADD A/SUB
 * A didn't already set (LD never touches flags on Z80; INC/DEC touch Z/S/
 * H/PV/N but never carry, which only ADD/ADC/SUB/SBC/CP/NEG/RLA-family
 * instructions set), so the carry tested by "jp nc,"/"jp c," is exactly
 * the one the ADD/SUB two lines earlier left behind - nothing in between
 * can have changed it.
 *
 * Confirmed as z88dk/SDCC's own default codegen for this exact C shape
 * (tests/bint.c's `in++;`, advancing the bytecode dispatch loop's own
 * instruction pointer once per opcode - one of the hottest single lines
 * in any of this suite's interpreters), which dcc had no equivalent for.
 *
 * This pattern is generic (`local += K;`/`local++;`/`local--;` on any
 * word-sized ix-relative local, not just a VM instruction pointer), so
 * pass_stride_loop_to_ptr - a structural, loop-recognizing pass in the
 * main loop that looks for exactly this "index reload / add stride / store
 * back" shape as part of a larger loop transformation - needs to see it
 * in its original, untouched form first; folding it here, after the main
 * loop has already fully converged, avoids the same "steal a pattern
 * before a more specific pass is ready" risk pass_cache_ix_local_word_
 * reload was moved here to avoid for pass_cpir (see that pass's own call
 * site comment for the tests/tcpirlp.c regression that taught this
 * lesson). Placed after pass_cache_ix_local_word_reload and before frame
 * elimination for the same reason as that pass: an ix-relative local is
 * this pass's whole precondition, so it needs to run before frame
 * elimination might remove some functions' ix frames entirely.
 *
 * Declines only when the pattern doesn't match exactly as above (the
 * offsets between the two loads/stores must agree, and the constant must
 * fit one byte) - unlike this file's BC-caching passes, this rewrite
 * doesn't touch BC/C or need any whole-function or cross-segment
 * reasoning: it only ever uses A (already dead here - the original
 * six-line sequence never reads or writes A either, and dcc's codegen has
 * no convention of preserving a register's value across a statement
 * boundary for it to violate) and the exact same ix-relative slot the
 * original sequence already reads and writes, for a strictly shorter span
 * than the original already occupied.
 */
/* True if "hl" is mentioned at all - as a register or as a "(hl)"
 * dereference, read or write, no distinction - anywhere from line `from`
 * up to the next label, call, or function marker (dcc's own "reuse
 * whatever's already in a register" convention, the same one this
 * function exists to protect, doesn't survive any of those either: a
 * label may be reached from elsewhere with hl holding something
 * unrelated, and a call clobbers every register). Used by
 * pass_small_const_incr_carry_skip to decide whether the line right after
 * its own match could be relying on the leftover hl value the original
 * "add hl,de / store back" sequence left resident there - see that pass's
 * own comment for why. Deliberately blunt (declines even a line that's
 * itself about to load hl with something else entirely, or an "add
 * hl,de" that only extends the staleness rather than resolving it) rather
 * than trying to characterize every mention of hl as safe or not: this
 * rewrite's whole value is a cheap, purely local text substitution, not
 * worth the risk of a second miscompile in the same pass to save chasing
 * a more precise, harder-to-verify condition. */
static int hl_relied_on_after(int from)
{
    int j;
    char clean[MAX_LINE];

    for (j = from; j < nlines; j++) {
        if (starts_label(lines[j]) || line_starts_function_marker(lines[j]))
            return 0;
        strip_peep_comment_lower_copy(clean, lines[j]);
        if (strncmp(clean, "call", 4) == 0 &&
            (clean[4] == ' ' || clean[4] == '\t'))
            return 0;
        if (strstr(clean, "hl") != NULL)
            return 1;
    }
    return 0;
}

static int pass_small_const_incr_carry_skip(void)
{
    int i;
    int changed = 0;
    static int label_counter;

    for (i = 0; i + 5 < nlines; i++) {
        int lo, hi, k;
        int st_lo, st_hi;
        char label[48];
        char line1[32], line2[32], line3[32], line4[64], line5[32];

        if (!peep_parse_ld_hl_ix_pair(i, &lo))
            continue;
        hi = lo - 1;
        if (!peep_parse_ld_de_small_const(lines[i + 2], &k))
            continue;
        if (!eq(i + 3, "add hl,de"))
            continue;
        if (!peep_parse_st_ix_neg_reg(lines[i + 4], 'l', &st_lo) || st_lo != lo)
            continue;
        if (!peep_parse_st_ix_neg_reg(lines[i + 5], 'h', &st_hi) || st_hi != hi)
            continue;

        /* The original "ld (ix-N),l / ld (ix-(N-1)),h" store leaves the
         * incremented pointer's own value still resident in HL (a plain
         * store never clobbers a register on Z80), and dcc's own codegen
         * relies on that: a later statement, if it needs the same address
         * again (the common `p++; p->field = x;` shape - a pointer bump
         * immediately followed by a dereference through the bumped
         * pointer, possibly after a few lines computing the value to
         * store first), reuses HL directly instead of reloading it. This
         * rewrite never puts the new value in HL at all (the low byte
         * lives in A throughout, and the high byte, when touched, goes
         * straight from memory to memory via INC/DEC), so it must decline
         * whenever anything reachable after this match could be relying
         * on that leftover HL value - confirmed as a real miscompile on
         * tests/tstruct.c's test2 (`sp++; sp->l = x;`, with two lines
         * computing x's own address in between: the dereference wasn't
         * even in the very next line, so an earlier version of this check
         * that only looked at that one line missed it and silently wrote
         * through the stale, pre-increment sp instead of the bumped one,
         * corrupting an unrelated struct field). hl_relied_on_after's own
         * comment covers why it's deliberately blunt about what counts as
         * "relying on it". */
        if (hl_relied_on_after(i + 6))
            continue;

        sprintf(label, "Lpeep_incr_skip%d", label_counter++);
        sprintf(line1, "ld a,(ix-%d)", lo);
        if (k > 0)
            sprintf(line2, "add a,%d", k);
        else
            sprintf(line2, "sub a,%d", -k);
        sprintf(line3, "ld (ix-%d),a", lo);
        sprintf(line4, "jp %s, %s", k > 0 ? "nc" : "c", label);
        sprintf(line5, k > 0 ? "inc (ix-%d)" : "dec (ix-%d)", hi);

        replace1_tagged(i, line1, "small_const_incr_carry_skip");
        replace1(i + 1, line2);
        replace1(i + 2, line3);
        replace1(i + 3, line4);
        replace1(i + 4, line5);
        {
            char labelline[56];
            sprintf(labelline, "%s:", label);
            replace1(i + 5, labelline);
        }
        changed = 1;
    }

    return changed;
}

/*
 * pass_word_postinc_ix_local_no_save:
 *
 * dcc's codegen for `arr[idx++] = val;` (idx a word-sized ix-relative local)
 * needs idx's PRE-increment value for the address and must also leave idx
 * incremented in memory, so it round-trips the old value through the stack:
 *
 *   ld l,(ix-N) / ld h,(ix-(N-1))   ; HL = old idx
 *   push hl                        ; [A] save old idx for later use as index
 *   inc hl                         ; HL = idx+1
 *   ld (ix-N),l / ld (ix-(N-1)),h   ; store idx+1 back
 *   pop hl                         ; [B] restore old idx into HL
 *
 * The push/pop is unnecessary: HL already holds the old value the whole
 * time, so the +1 can be applied directly to memory (with dcc's own
 * byte-wise carry-skip trick - see pass_small_const_incr_carry_skip, which
 * this mirrors but for the "old value is still needed in a register"
 * shape) without ever touching HL at all:
 *
 *   ld l,(ix-N) / ld h,(ix-(N-1))   ; HL = old idx (unchanged, still needed)
 *   inc (ix-N)
 *   jp nz, L                       ; low byte didn't wrap: skip hi-byte bump
 *   inc (ix-(N-1))
 *   L:
 *
 * Confirmed against z88dk/sdcc's own codegen for the same C source (fint.c's
 * `lst[lsp++] = in->a;` in run_at) - sdcc already generates exactly this
 * shape, which is what led to finding this gap.
 *
 * The original 7-line sequence never touches any flag (push/inc hl/ld/pop
 * are all flag-transparent on Z80), so whatever flags were live coming in
 * are still live going out unchanged. The rewrite's "inc (ix-N)" does set
 * flags, so this must decline whenever anything reachable after the match
 * could still be relying on the flags that were live before it - checked
 * with the real dataflow-based peep_flags_dead_after rather than a blunt
 * textual scan, since this pass (unlike the older textual-heuristic passes
 * in this file) has that CFG-based liveness available.
 */
static int pass_word_postinc_ix_local_no_save(void)
{
    int i;
    int changed = 0;
    static int label_counter;
    const unsigned all_flags = PEEP_FLAG_C | PEEP_FLAG_Z | PEEP_FLAG_S | PEEP_FLAG_PV;

    for (i = 0; i + 6 < nlines; i++) {
        int lo, hi, st_lo, st_hi;
        char label[48];
        char line_inc_lo[32], line_jp[64], line_inc_hi[32], line_label[56];

        if (!peep_parse_ld_hl_ix_pair(i, &lo))
            continue;
        hi = lo - 1;
        if (!eq(i + 2, "push hl"))
            continue;
        if (!eq(i + 3, "inc hl"))
            continue;
        if (!peep_parse_st_ix_neg_reg(lines[i + 4], 'l', &st_lo) || st_lo != lo)
            continue;
        if (!peep_parse_st_ix_neg_reg(lines[i + 5], 'h', &st_hi) || st_hi != hi)
            continue;
        if (!eq(i + 6, "pop hl"))
            continue;

        if (!peep_flags_dead_after(i + 6, all_flags))
            continue;

        sprintf(label, "Lpeep_postinc_skip%d", label_counter++);
        sprintf(line_inc_lo, "inc (ix-%d)", lo);
        sprintf(line_jp, "jp nz, %s", label);
        sprintf(line_inc_hi, "inc (ix-%d)", hi);
        sprintf(line_label, "%s:", label);

        replace1_tagged(i + 2, line_inc_lo, "word_postinc_ix_local_no_save");
        replace1(i + 3, line_jp);
        replace1(i + 4, line_inc_hi);
        replace1(i + 5, line_label);
        delete_n(i + 6, 1);

        changed = 1;
    }

    return changed;
}

/* Matches, starting at `start`:
 *   ld l,(ix-A) / ld h,(ix-(A-1))   ; word-sized ix-local index
 *   [dec hl]                        ; optional: reading one slot below TOS
 *   add hl,hl                       ; index*2 (word-sized array elements)
 *   ld e,(ix-C) / ld d,(ix-(C-1))   ; word-sized ix-local array base
 *   add hl,de                       ; &arr[index]
 * - dcc's standard codegen shape for computing the address of a word-sized
 * array element indexed by a word-sized ix-local, itself indexed through a
 * word-sized ix-local base pointer/array-descriptor (the Forth stack-machine
 * `lst[lsp]`/`lst[lsp-1]` shape). Returns the block length (6 or 7,
 * depending on whether the optional dec hl is present) on a full match,
 * filling *a and *c (positive ix- magnitudes) and *has_dec; returns 0 on any
 * mismatch. */
static int match_ix_word_array_addr_block(int start, int *a, int *c, int *has_dec)
{
    int idx = start;
    int av;
    char ebuf[32], dbuf[32];
    int e, d;

    if (start < 0 || !peep_parse_ld_hl_ix_pair(idx, &av))
        return 0;
    idx += 2;
    *has_dec = eq(idx, "dec hl") ? 1 : 0;
    if (*has_dec)
        idx++;
    if (!eq(idx, "add hl,hl"))
        return 0;
    idx++;
    if (idx + 1 >= nlines)
        return 0;
    if (!peep_parse_ld_e_ix(lines[idx], ebuf) || !parse_ix_off_numeric(ebuf, &e))
        return 0;
    if (!peep_parse_ld_d_ix(lines[idx + 1], dbuf) || !parse_ix_off_numeric(dbuf, &d))
        return 0;
    if (d != e + 1)
        return 0;
    idx += 2;
    if (!eq(idx, "add hl,de"))
        return 0;
    idx++;

    *a = av;
    *c = -e;
    return idx - start;
}

/*
 * pass_elim_dup_ix_word_array_addr_after_push:
 *
 * dcc's codegen for a compound assignment through a shared array-element
 * expression - `arr[idx] = f(arr[idx], x);`, the shape behind every Forth
 * stack-machine binary op (`lst[lsp-1] = lst[lsp-1] == _t;` and friends) -
 * doesn't recognize that the read and the write share the same address
 * expression. It computes &arr[idx] (match_ix_word_array_addr_block's own
 * shape above), pushes it to use again later as the store target, and then
 * recomputes the exact same address a second time just to read through it.
 *
 * "push hl" doesn't modify HL (or any other register/flag) - it only reads
 * HL and writes memory + SP - so HL already holds &arr[idx] right after the
 * push; the second computation is provably redundant and is deleted
 * outright.
 *
 * Deliberately narrow (this one fixed, concrete ix-relative shape) and run
 * once, after the main fixed-point loop has fully converged, rather than as
 * a fully general "any duplicate block after any push" pass: an earlier,
 * broader version of that more general idea matched a global-symbol reload
 * that pass_defer_global_push_reload was still mid-transforming when both
 * ran inside the same shared fixed point, silently corrupting
 * tests/tforblk.c's static-pointer-initializer case (the duplicate lines
 * were textually identical and individually "safe" by that version's
 * criteria, but the surrounding transformation was not yet in its final
 * form). Restricting to this one shape - which no other pass produces or
 * touches - and running post-convergence, after every other pass has
 * already settled into its final output, avoids that entire class of
 * interaction.
 */
static int pass_elim_dup_ix_word_array_addr_after_push(void)
{
    int i;
    int changed = 0;

    for (i = 0; i < nlines; i++) {
        int a1, c1, dec1, len1;
        int a2, c2, dec2, len2;

        if (!eq(i, "push hl"))
            continue;

        len1 = 0;
        if (i - 7 >= 0 &&
            match_ix_word_array_addr_block(i - 7, &a1, &c1, &dec1) == 7 &&
            dec1)
            len1 = 7;
        else if (i - 6 >= 0 &&
                 match_ix_word_array_addr_block(i - 6, &a1, &c1, &dec1) == 6 &&
                 !dec1)
            len1 = 6;
        if (len1 == 0)
            continue;

        len2 = match_ix_word_array_addr_block(i + 1, &a2, &c2, &dec2);
        if (len2 != len1 || a2 != a1 || c2 != c1 || dec2 != dec1)
            continue;

        delete_n(i + 1, len1);
        changed = 1;
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

        if (!parse_ld_hl_imm(lines[i], imm_text, sizeof(imm_text)))
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

/* Conservative check: does this line reference register `lo`, `hi`, or
 * the `pair` register pair (e.g. "b"/"c"/"bc") in any way? Guards a pass's
 * exclusive claim on a register pair for the whole loop body. A false
 * positive just declines the optimization; a false negative could
 * silently corrupt a live value, so this errs deliberately broad - every
 * Z80 mnemonic that touches a register pair implicitly (the block/repeat
 * instructions all use BC as a counter and DE/HL as pointers) is
 * included, not just explicit register-name operands. */




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
        strip_label_colon(label);

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
        loop_end = find_last_loop_back(i + 1, label, 0);
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
            /* _any, not the public-only find_function_bounds: this pass runs
             * on every loop, including ones in file-scope static functions,
             * whose bodies emit no "public NAME" line. With the public-only
             * bounds, func_end of a static function overshoots past any
             * following static function(s) to the next public label, so the
             * backward scan below would start inside a LATER function and
             * answer the ownership question for the wrong body. */
            find_function_bounds_any(i, &func_start, &func_end);
            /* The loop-body scan below cannot see a whole-function or
             * earlier-loop BC candidate primed elsewhere in this function.
             * Check through the shared ownership guard so both IX-based
             * local/parameter primes and marker-tagged global primes are
             * covered, along with BC claims made by an earlier peephole
             * iteration. */
            if (bc_regalloc_claimed_before(func_end))
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
            pos = find_label_line_in_range(tgt, 0, func_end);
            continue;
        }
        ++pos;
    }
    return 0;
}

/* A full HL overwrite is harmless to an H-resident loop invariant when the
 * very next instruction unconditionally exits this loop. The target must be
 * resolved and lie outside [loop_start, loop_end]; ambiguous or conditional
 * control flow declines. */
static int hl_overwrite_exits_loop(int line, int loop_start, int loop_end)
{
    char clean[MAX_LINE];
    char target[128];
    int target_line;

    strip_peep_comment_copy(clean, lines[line]);
    if (strncmp(clean, "ld hl,", 6) != 0 || line + 1 >= nlines)
        return 0;
    if (!is_uncond_jp(lines[line + 1]) && !is_uncond_jr(lines[line + 1]))
        return 0;
    if (!jump_target_any(lines[line + 1], target))
        return 0;
    target_line = find_label_line_in_range(target, 0, nlines);
    return target_line >= 0 &&
           (target_line < loop_start || target_line > loop_end);
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
    int loop_end;
    int match_k;
    int access_k;
    int access_is_cmp;
    int match_count;
    int inc_e_count;
    int bc_ok;
    int de_ok;
    int invariant_load_k;
    int invariant_ok;
    char invariant_off[32];
    char invariant_pat[40];
    int func_start, func_end;

    changed = 0;

    for (i = 0; i < nlines; ++i) {
        if (!starts_label(lines[i]))
            continue;

        strcpy(label, lines[i]);
        strip_label_colon(label);

        loop_end = find_last_loop_back(i + 1, label, 1);
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

        /* Once the address recombination below is removed, H is available
         * for one loop-invariant byte used to form the RHS. Cache exactly one
         * indexed A load; decline if the same frame slot appears anywhere
         * else in the loop or if H is live outside instructions this pass
         * already removes/replaces. */
        invariant_load_k = -1;
        invariant_off[0] = 0;
        for (k = match_k + 4; k < access_k; ++k) {
            char off[32];
            if (!peep_parse_ld_a_ix(lines[k], off))
                continue;
            if (invariant_load_k >= 0) {
                invariant_load_k = -1;
                break;
            }
            invariant_load_k = k;
            strcpy(invariant_off, off);
        }
        if (invariant_load_k < 0 && access_is_cmp && match_k >= i + 3 &&
            peep_parse_ld_a_ix(lines[match_k - 2], invariant_off) &&
            eq(match_k - 1, "add a,e"))
            invariant_load_k = match_k - 2;
        if (invariant_load_k >= 0) {
            char tmp[MAX_LINE];

            sprintf(invariant_pat, "(ix%s)", invariant_off);
            invariant_ok = 1;
            for (k = i + 1; k < loop_end && invariant_ok; ++k) {
                if (k != invariant_load_k &&
                    strstr(lines[k], invariant_pat) != NULL) {
                    invariant_ok = 0;
                    break;
                }
                if ((k >= match_k && k <= match_k + 3) ||
                    k == access_k || k == invariant_load_k)
                    continue;
                strip_peep_comment_copy(tmp, lines[k]);
                if (strncmp(tmp, "call ", 5) == 0 ||
                    strncmp(tmp, "rst ", 4) == 0 || strcmp(tmp, "exx") == 0 ||
                    (line_touches_hl(tmp) &&
                     !hl_overwrite_exits_loop(k, i, loop_end)))
                    invariant_ok = 0;
            }
            if (!invariant_ok)
                invariant_load_k = -1;
        }

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
                int jtgt_line = find_label_line_in_range(jtgt, func_start, func_end);
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
                if (invariant_load_k >= 0)
                    invariant_load_k += 4;
            }
        }

        if (invariant_load_k >= 0) {
            char prime[48];

            sprintf(prime, "ld h,(ix%s)", invariant_off);
            insert_line_tagged(i, prime, "walk_invariant_byte_h");
            ++i;
            ++loop_end;
            ++match_k;
            ++access_k;
            ++invariant_load_k;
        }

        {
            int access_after_delete = access_k - 4;
            int invariant_after_delete = invariant_load_k < match_k
                ? invariant_load_k : invariant_load_k - 4;
            delete_n(match_k, 4);
            if (invariant_load_k >= 0)
                replace1_tagged(invariant_after_delete, "ld a,h",
                                "walk_invariant_byte_h");
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
        strip_label_colon(label);

        loop_end = find_last_loop_back(i + 1, label, 1);
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

/* Parse an IX offset string (e.g. "+8", "-2") to an integer. */



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

int main(int argc, char **argv)
{
    int changed;
    int passes;
    const char *infile = NULL;
    const char *outfile = NULL;
    int ai;

    peep_context_init();

    for (ai = 1; ai < argc; ++ai) {
        if (strcmp(argv[ai], "-Os") == 0) {
            peep_context.options.optimize_size = 1;
        } else if (strcmp(argv[ai], "-Ot") == 0) {
            peep_context.options.optimize_size = 0;
        } else if (strcmp(argv[ai], "-fundocumented-z80") == 0) {
            peep_context.options.allow_undocumented_z80 = 1;
        } else if (strcmp(argv[ai], "-fstats") == 0) {
            peep_context.options.stats_enabled = 1;
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
            "usage: dccpeep [-Ot|-Os] [-fundocumented-z80] [-fstats] input.mac output.mac\n");
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

    static const PeepPass fixed_point_passes[] = {
        { "pass_once", pass_once, 0 },
        { "pass_byte_minmax_patterns", pass_byte_minmax_patterns, 0 },
        { "pass_dead_hl_load_before_ldhl", pass_dead_hl_load_before_ldhl, 0 },
        { "pass_word_load_push_de_call", pass_word_load_push_de_call, 0 },
        { "pass_long_load_push_no_ex_call", pass_long_load_push_no_ex_call, 0 },
        { "pass_elim_loop_back_signed_bias", pass_elim_loop_back_signed_bias, 0 },
        { "pass_cp_zero_to_or_a", pass_cp_zero_to_or_a, 0 },
        { "pass_hl_cmp_zero_to_or_hl", pass_hl_cmp_zero_to_or_hl, 0 },
        { "pass_signed_cmp_const_low0", pass_signed_cmp_const_low0, 0 },
        { "pass_zeroext_byte_cmp_const", pass_zeroext_byte_cmp_const, 0 },
        { "pass_byte_cmp_push_pop_hl", pass_byte_cmp_push_pop_hl, 0 },
        { "pass_word_switch_cmp_avoid_push_pop", pass_word_switch_cmp_avoid_push_pop, 0 },
        { "pass_call_hl_stack_roundtrip", pass_call_hl_stack_roundtrip, 0 },
        { "pass_minmax_winner_result_no_temp", pass_minmax_winner_result_no_temp, 0 },
        { "pass_minmax_score_b_cache", pass_minmax_score_b_cache, 0 },
        { "pass_minmax_save_board_addr", pass_minmax_save_board_addr, 0 },
        { "pass_elim_redundant_ld_a_reg", pass_elim_redundant_ld_a_reg, 0 },
        { "pass_minmax_elim_label_reload", pass_minmax_elim_label_reload, 0 },
        { "pass_elim_c_reload_after_store", pass_elim_c_reload_after_store, 0 },
        { "pass_and1_ix_to_bit", pass_and1_ix_to_bit, 0 },
        { "pass_winner_check_dec_a", pass_winner_check_dec_a, 0 },
        { "pass_shrink_minmax_frame3_after_score_cache", pass_shrink_minmax_frame3_after_score_cache, 0 },
        { "pass_minmax_loop_ctr_b", pass_minmax_loop_ctr_b, 0 },
        { "pass_shrink_minmax_frame2_after_loop_ctr_b", pass_shrink_minmax_frame2_after_loop_ctr_b, 0 },
        { "pass_minmax_value_c", pass_minmax_value_c, 0 },
        { "pass_minmax_board_ptr_loop", pass_minmax_board_ptr_loop, 0 },
        { "pass_minmax_byte_returns", pass_minmax_byte_returns, 0 },
        { "pass_minmax_pack_frame", pass_minmax_pack_frame, 0 },
        { "pass_minmax_pack_call", pass_minmax_pack_call, 0 },
        { "pass_store_l_reload_a", pass_store_l_reload_a, 0 },
        { "pass_reuse_board_addr_for_zero_store", pass_reuse_board_addr_for_zero_store, 0 },
        { "pass_array_base_push_to_de", pass_array_base_push_to_de, 0 },
        { "pass_base_index_addr", pass_base_index_addr, 0 },
        { "pass_fold_hl_base_const_offset", pass_fold_hl_base_const_offset, 0 },
        { "pass_fold_hl_label_word_deref", pass_fold_hl_label_word_deref, 0 },
        { "pass_e_signed_le_zero", pass_e_signed_le_zero, 0 },
        { "pass_ix_array_word_addr", pass_ix_array_word_addr, 0 },
        { "pass_ix_array_byte_addr", pass_ix_array_byte_addr, 0 },
        { "pass_byte_loop_counter_to_reg_c", pass_byte_loop_counter_to_reg_c, 0 },
        { "pass_byte_for_counter_to_reg_c", pass_byte_for_counter_to_reg_c, 0 },
        { "pass_byte_for_counter_to_reg_e", pass_byte_for_counter_to_reg_e, 0 },
        { "pass_store_word_const_hl", pass_store_word_const_hl, 0 },
        { "pass_float_zero_store", pass_float_zero_store, 0 },
        { "pass_remove_unreferenced_labels", pass_remove_unreferenced_labels, 0 },
        { "pass_ldir_memset_rotated", pass_ldir_memset_rotated, 0 },
        { "pass_reuse_sbc_result_for_flagcheck_rotated", pass_reuse_sbc_result_for_flagcheck_rotated, 0 },
        { "pass_cond_skip_shortcut", pass_cond_skip_shortcut, 0 },
        { "pass_stride_loop_to_ptr", pass_stride_loop_to_ptr, 0 },
        { "pass_ix_frame_ptr_load", pass_ix_frame_ptr_load, 0 },
        { "pass_ix_frame_ptr_load_deadd", pass_ix_frame_ptr_load_deadd, 0 },
        { "pass_hoist_index_ptr_to_bc", pass_hoist_index_ptr_to_bc, 0 },
        { "pass_walk_hoisted_index_ptr", pass_walk_hoisted_index_ptr, 0 },
        { "pass_walk_row_cached_float_index", pass_walk_row_cached_float_index, 0 },
        { "pass_global_ptr_word_predec_load", pass_global_ptr_word_predec_load, 0 },
        { "pass_elim_ex_de_hl_before_ix_store", pass_elim_ex_de_hl_before_ix_store, 0 },
        { "pass_elim_redundant_pop_push", pass_elim_redundant_pop_push, 0 },
        { "pass_double_de_before_add", pass_double_de_before_add, 0 },
        { "pass_const_hl_doubles", pass_const_hl_doubles, 0 },
        { "pass_deref_byte_cmp", pass_deref_byte_cmp, 0 },
        { "pass_cpir", pass_cpir, 0 },
        { "pass_byte_global_ptr_array_addr", pass_byte_global_ptr_array_addr, 0 },
        { "pass_byte_ix_predec_zero_test", pass_byte_ix_predec_zero_test, 0 },
        { "pass_byte_loop_counter_to_reg_iyl", pass_byte_loop_counter_to_reg_iyl, PEEP_PASS_UNDOCUMENTED_Z80 },
        { "pass_byte_incr_loop_counter_to_reg_iyl", pass_byte_incr_loop_counter_to_reg_iyl, PEEP_PASS_UNDOCUMENTED_Z80 },
        { "pass_ix_pair_load_to_de", pass_ix_pair_load_to_de, 0 },
        { "pass_bc_pair_load_to_de", pass_bc_pair_load_to_de, 0 },
        { "pass_ix_byte_load_to_de", pass_ix_byte_load_to_de, 0 },
        { "pass_remove_ix_store_reload_hl", pass_remove_ix_store_reload_hl, 0 },
        { "pass_inline_temp_spill_to_stack", pass_inline_temp_spill_to_stack, 0 },
        { "pass_remove_inline_temp_markers", pass_remove_inline_temp_markers, 0 },
        { "pass_postinc_ix_word", pass_postinc_ix_word, 0 },
        { "pass_cp_jz_jpnc", pass_cp_jz_jpnc, 0 },
        { "pass_cp_jz_jpc", pass_cp_jz_jpc, 0 },
        { "pass_bool_from_cmp", pass_bool_from_cmp, 0 },
        { "pass_elim_dead_ix_stores", pass_elim_dead_ix_stores, 0 },
        { "pass_ix_addr_byte_store_imm", pass_ix_addr_byte_store_imm, 0 },
        { "pass_remove_ix_store_reload_a", pass_remove_ix_store_reload_a, 0 },
        { "pass_a_tracks_ix_byte", pass_a_tracks_ix_byte, 0 },
        { "pass_elim_redundant_ld_h_zero", pass_elim_redundant_ld_h_zero, 0 },
        { "pass_elim_long_store_reload", pass_elim_long_store_reload, 0 },
        { "pass_skip_ix_reload_across_label", pass_skip_ix_reload_across_label, 0 },
        { "pass_branch_over_jump", pass_branch_over_jump, 0 },
        { "pass_jump_thread", pass_jump_thread, 0 },
        { "pass_global_board_const_offsets", pass_global_board_const_offsets, 0 },
        { "pass_posfunc_b_cache", pass_posfunc_b_cache, 0 },
        { "pass_jp_to_plain_ret", pass_jp_to_plain_ret, 0 },
        { "pass_const_divmod_helpers", pass_const_divmod_helpers, 0 },
        { "pass_mulu_const", pass_mulu_const, 0 },
        { "pass_cache_noix_byte_param_reload", pass_cache_noix_byte_param_reload, 0 },
        { "pass_cache_global_word_reload", pass_cache_global_word_reload, 0 },
        { "pass_word_loop_var_to_reg_bc", pass_word_loop_var_to_reg_bc, 0 },
        { "pass_byte_loop_var_to_reg_c", pass_byte_loop_var_to_reg_c, 0 },
        { "pass_labels", pass_labels, 0 },
    };
    size_t fixed_pass_count = sizeof(fixed_point_passes) / sizeof(fixed_point_passes[0]);
    size_t pass_index;

    passes = 0;
    do {
        changed = 0;
        for (pass_index = 0; pass_index < fixed_pass_count; ++pass_index) {
            const PeepPass *pass = &fixed_point_passes[pass_index];
            if ((pass->flags & PEEP_PASS_UNDOCUMENTED_Z80) &&
                !peep_context.options.allow_undocumented_z80)
                continue;
            if (run_counted_pass(pass->name, pass->run))
                changed = 1;
        }
        passes++;
        peep_context.stats.iterations = passes;
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
    if (!peep_context.options.optimize_size && RUN_PASS(pass_signed_cmp_const_bias_fold))
        RUN_PASS(pass_labels);
    if (!peep_context.options.optimize_size && RUN_PASS(pass_signed_zero_branch))
        RUN_PASS(pass_labels);

    /* pass_defer_global_push_reload runs once here for the same reason as
     * pass_cache_ix_local_word_reload just below (see that pass's own call
     * site comment): both are structural rewrites whose own precondition
     * can be satisfied on an earlier main-loop iteration than a more
     * specific pass's precondition is, so both need to wait until every
     * loop-recognizing pass in the main loop has already fully converged.
     * Placed before pass_cache_ix_local_word_reload rather than after:
     * removing a push/pop pair here can only ever shrink the text between
     * two occurrences of a repeated ix-relative reload, never introduce a
     * new hazard between them (push/pop are not hazards to
     * pass_cache_ix_local_word_reload's own segmentation - see that pass's
     * shared line_clobbers_bc comment), so running first here can only
     * help that pass find a segment it would have found anyway, never hurt
     * it. Purely local (no control flow change, and it only ever removes
     * instructions), so a single pass suffices; pass_labels tidies up. */
    if (RUN_PASS(pass_defer_global_push_reload))
        RUN_PASS(pass_labels);

    /* pass_cache_ix_local_word_reload runs once here, after the main loop
     * converges, for the same reason pass_signed_cmp_const_bias_fold does
     * (see its own comment just above): a structural, loop-recognizing pass
     * - pass_cpir here, rather than pass_ldir_memset/pass_stride_loop_to_ptr
     * - needs to see a loop's canonical, untouched shape, and this pass's
     * own precondition (>= 3 occurrences of a repeated ix-relative reload in
     * one hazard-free span) can already be satisfied on an EARLIER main-loop
     * iteration than pass_cpir's own precondition is, if pass_cpir needs
     * some other pass's change first. Since both ran inside the same shared
     * fixed-point loop, this pass could - and, confirmed as a real, measured
     * regression on tests/tcpirlp.c, did - claim the loop pointer's repeated
     * reload before pass_cpir was ready, permanently blocking pass_cpir from
     * ever replacing the whole loop with a single hardware CPIR instruction,
     * a far bigger win than this pass's own per-reload saving; simply
     * reordering the two calls within the shared loop did not help, since
     * the problem isn't which one runs first on a given iteration but that
     * this pass's own turn can come on an iteration earlier than pass_cpir's
     * first eligible one. Moving it here - after every structural,
     * loop-recognizing pass in the main loop has already fully converged -
     * closes that gap the same way the bias fold's own move here already
     * does for pass_ldir_memset/pass_stride_loop_to_ptr. Purely local (no
     * control flow change), so a single pass suffices; pass_labels tidies
     * up. */
    if (RUN_PASS(pass_cache_ix_local_word_reload))
        RUN_PASS(pass_labels);

    RUN_PASS(pass_cache_ix_long_param_reload);
    RUN_PASS(pass_preserve_ix_pointer_compare);
    if (RUN_PASS(pass_ix_word_small_eq_chain))
        RUN_PASS(pass_labels);

    /* pass_small_const_incr_carry_skip runs once here, for the same reason
     * as pass_cache_ix_local_word_reload just above (see that pass's own
     * call site comment, and this pass's own for which structural,
     * loop-recognizing pass in the main loop it would otherwise steal a
     * pattern from before that pass is ready): pass_stride_loop_to_ptr
     * looks for the exact same "index reload / add stride / store back"
     * shape this pass rewrites, as part of a larger loop transformation
     * that saves far more than this pass's own per-increment saving would.
     * Placed before frame elimination since an ix-relative local is this
     * pass's whole precondition. Introduces new control flow (a
     * conditional jump and a label) unlike every other pass placed in
     * this post-convergence section, all of which are pure substitutions -
     * still safe to run once here rather than in the main loop's own
     * fixed point, since it only ever shortens the exact span it matches
     * and never changes what any label anywhere else in the file targets;
     * pass_labels tidies up regardless. */
    if (RUN_PASS(pass_small_const_incr_carry_skip))
        RUN_PASS(pass_labels);

    /* pass_word_postinc_ix_local_no_save: same placement rationale as
     * pass_small_const_incr_carry_skip immediately above (its own call-site
     * comment covers the pass_stride_loop_to_ptr interaction this section
     * exists to avoid) - an ix-relative local is this pass's precondition
     * too, and it likewise introduces new control flow (a conditional jump
     * and a label), so it runs once here rather than in the main fixed
     * point, with pass_labels to tidy up. */
    if (RUN_PASS(pass_word_postinc_ix_local_no_save))
        RUN_PASS(pass_labels);

    /* pass_elim_dup_ix_word_array_addr_after_push: see its own comment for
     * why this runs post-convergence rather than in the shared fixed point
     * (a real miscompile from an earlier, more general version of this
     * idea, found on tests/tforblk.c). Pure deletion, no new control flow,
     * so no pass_labels needed afterward. */
    RUN_PASS(pass_elim_dup_ix_word_array_addr_after_push);

    if (RUN_PASS(pass_promote_ix_pointer_to_iy)) {
        RUN_PASS(pass_remove_unreferenced_labels);
        RUN_PASS(pass_labels);
    }

    /* pass_cache_ix_spill_via_iy runs after pass_promote_ix_pointer_to_iy,
     * not before: that pass's own whole-FILE "IY unused anywhere" gate (see
     * its own comment) would see this pass's rewrites and decline for the
     * entire file if this ran first, and its whole-function register
     * promotion is a substantially bigger win (a value live for a whole
     * function, not just one short span) than this pass's own per-spill
     * saving - confirmed as a real regression (forint +2.6%) when this was
     * tried in the other order: this pass had already claimed IY somewhere
     * low-value in forint.c before pass_promote_ix_pointer_to_iy got a
     * chance at eval_e's much more valuable token-pointer promotion, and
     * that pass's whole-file gate then declined entirely. Running after
     * means this pass's own per-function iy_used_in_function check simply
     * sees pass_promote_ix_pointer_to_iy's already-claimed functions (their
     * rewrites mention "iy") and correctly skips them, same as it already
     * does for its own earlier candidates elsewhere in a file. Same
     * post-convergence placement rationale as pass_cache_ix_local_word_
     * reload (own precondition can be satisfied on an earlier main-loop
     * iteration than a structural, loop-recognizing pass's own). Purely
     * local, so a single pass suffices; pass_labels tidies up. */
    if (RUN_PASS(pass_cache_ix_spill_via_iy))
        RUN_PASS(pass_labels);

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
    if (RUN_PASS(pass_elim_ix_frame)) {
        RUN_PASS(pass_jp_to_plain_ret);
        RUN_PASS(pass_labels);
    }

    /* Convert remaining framed prologues/epilogues to shared stub calls.
     * Runs after frame elimination so only functions that genuinely need IX
     * are transformed.  A follow-up branch/label pass collapses any return
     * labels that now just contain "jp __lve" into direct jumps. */
    if (peep_context.options.optimize_size && RUN_PASS(pass_shared_frame_stubs)) {
        RUN_PASS(pass_branch_over_jump);
        RUN_PASS(pass_labels);
    }

    /* Load-arg stubs and frame-pointer copy stub run last: they remove "ix"
     * text from lines, so they must not run before pass_elim_ix_frame (which
     * uses that text to detect live frame usage). */
    if (peep_context.options.optimize_size) {
        RUN_PASS(pass_larg_stubs);
        RUN_PASS(pass_phix_stub);
        RUN_PASS(pass_lvar_stubs);
        RUN_PASS(pass_svar_stubs);
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
        RUN_PASS(pass_larg_direct_store);
        RUN_PASS(pass_icmp_stub);
        RUN_PASS(pass_sxde_stub);
        RUN_PASS(pass_sxhl_stub);
        /* Enable for more size at some perf cost: */
#if 1
        RUN_PASS(pass_wand_stub);    /* -200 bytes, +5% perf */
        RUN_PASS(pass_ldwl_stub);    /* -231 bytes, +8% perf */
#endif
    }

    pass_fix_divmod_extrns();
    pass_fix_mulu_extrn();

    /* Final cleanup: drop dead 16-bit reloads, then relax in-range absolute
     * jumps to relative jumps.  Both run after every structural pass so they
     * only tidy the settled instruction stream; dead-load removal first since
     * it shrinks code and can bring more branches into jr range. */
    RUN_PASS(pass_dup_ix_load_to_reg_copy);
    RUN_PASS(pass_fold_const_sign_extend);
    do {
        changed = 0;
        if (RUN_PASS(pass_elim_dead_register_loads))
            changed = 1;
        if (RUN_PASS(pass_remove_ix_store_reload_hl))
            changed = 1;
    } while (changed);
    RUN_PASS(pass_elim_dead_reg16_reload);
    RUN_PASS(pass_jp_to_jr);

    write_file(outfile);
    if (peep_context.options.stats_enabled)
        report_stats(passes);
    return 0;
}
