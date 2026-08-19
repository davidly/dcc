/**
 * @file peep_control_flow.c
 * @brief Indexes labels and functions for conservative control-flow queries.
 *
 * @par Role
 * Owns textual label and jump parsing, versioned label/function indexes,
 * function-bound lookup, bounded label searches, and loop-body reachability
 * checks shared by multiple pass families.
 *
 * @par Key entry points
 * jump_target(), jump_target_any(), peep_indexed_function_bounds(),
 * find_label_line_in_range(), and loop_body_internal_labels_safe().
 *
 * @par Boundary
 * peep_dataflow.c owns CFG construction and liveness, while
 * peep_pass_control_flow.c owns label and branch rewrites. Unknown branch
 * syntax remains conservative here.
 */
#include "dccpeep_internal.h"

static unsigned jumpref_hash(const char *name)
{
    unsigned h = 0;
    while (*name)
        h = h * 131u + (unsigned char)*name++;
    return h % PEEP_JUMPREF_HASH_SIZE;
}

/* Find or create the group for `name`, then append a reference to it for
 * `line` (is_jp: 1 for a "jp " line, 0 for "jr "). Capacity for both arrays
 * is sized to nlines up front by the caller (a jump line can target at most
 * one group, and a name can start at most one new group), so no growth
 * logic is needed here. */
static void jumpref_add(PeepIndexes *indexes, const char *name, int line, int is_jp)
{
    unsigned h = jumpref_hash(name);
    int g, e;

    for (g = indexes->jumpref_buckets[h]; g >= 0; g = indexes->jumpref_groups[g].next_group)
        if (strcmp(indexes->jumpref_groups[g].name, name) == 0)
            break;
    if (g < 0) {
        g = indexes->jumpref_group_count++;
        strncpy(indexes->jumpref_groups[g].name, name,
                sizeof(indexes->jumpref_groups[g].name) - 1);
        indexes->jumpref_groups[g].name[sizeof(indexes->jumpref_groups[g].name) - 1] = 0;
        indexes->jumpref_groups[g].first_ref = -1;
        indexes->jumpref_groups[g].next_group = indexes->jumpref_buckets[h];
        indexes->jumpref_buckets[h] = g;
    }

    e = indexes->jumpref_entry_count++;
    indexes->jumpref_entries[e].line = line;
    indexes->jumpref_entries[e].is_jp = is_jp;
    indexes->jumpref_entries[e].next_ref = indexes->jumpref_groups[g].first_ref;
    indexes->jumpref_groups[g].first_ref = e;
}

/* Group index for every jump line in this pass's chain targeting `name`, or
 * -1 if nothing in the whole file currently jumps to it. */
static int jumpref_find_group(PeepIndexes *indexes, const char *name)
{
    unsigned h = jumpref_hash(name);
    int g;

    for (g = indexes->jumpref_buckets[h]; g >= 0; g = indexes->jumpref_groups[g].next_group)
        if (strcmp(indexes->jumpref_groups[g].name, name) == 0)
            return g;
    return -1;
}

static void ensure_control_flow_indexes(void)
{
    PeepIndexes *indexes = &peep_context.indexes;
    int i;

    if (indexes->version == peep_context.program_version && indexes->labels)
        return;

    if (indexes->label_capacity < nlines) {
        PeepLabelIndexEntry *labels = (PeepLabelIndexEntry *)realloc(
            indexes->labels, (size_t)nlines * sizeof(*labels));
        int *public_functions = (int *)realloc(
            indexes->public_functions, (size_t)nlines * sizeof(*public_functions));
        int *all_functions = (int *)realloc(
            indexes->all_functions, (size_t)nlines * sizeof(*all_functions));
        PeepJumpLabelGroup *jumpref_groups = (PeepJumpLabelGroup *)realloc(
            indexes->jumpref_groups, (size_t)nlines * sizeof(*jumpref_groups));
        PeepJumpRefEntry *jumpref_entries = (PeepJumpRefEntry *)realloc(
            indexes->jumpref_entries, (size_t)nlines * sizeof(*jumpref_entries));
        if ((nlines && !labels) || (nlines && !public_functions) ||
            (nlines && !all_functions) || (nlines && !jumpref_groups) ||
            (nlines && !jumpref_entries)) {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
        indexes->labels = labels;
        indexes->public_functions = public_functions;
        indexes->all_functions = all_functions;
        indexes->jumpref_groups = jumpref_groups;
        indexes->jumpref_entries = jumpref_entries;
        indexes->label_capacity = nlines;
        indexes->function_capacity = nlines;
        indexes->jumpref_group_capacity = nlines;
        indexes->jumpref_entry_capacity = nlines;
    }

    indexes->label_count = 0;
    indexes->public_function_count = 0;
    indexes->all_function_count = 0;
    indexes->jumpref_group_count = 0;
    indexes->jumpref_entry_count = 0;
    for (i = 0; i < PEEP_JUMPREF_HASH_SIZE; ++i)
        indexes->jumpref_buckets[i] = -1;
    for (i = 0; i < nlines; ++i) {
        char tgt[128];

        if (starts_label(lines[i])) {
            indexes->labels[indexes->label_count].name = lines[i];
            indexes->labels[indexes->label_count].line = i;
            indexes->label_count++;
        }
        if (peep_is_public_line(lines[i])) {
            indexes->public_functions[indexes->public_function_count++] = i;
            indexes->all_functions[indexes->all_function_count++] = i;
        } else if (strncmp(lines[i], "; static function ", 18) == 0) {
            indexes->all_functions[indexes->all_function_count++] = i;
        }
        /* jump_target and jump_target_any agree byte-for-byte on target text
         * for every "jp " line (identical parse once the "jp "/"jr " prefix
         * check passes - jump_target_any's own comment documents this), so
         * one jump_target_any-driven pass here, tagged with is_jp, serves
         * both find_last_loop_back(any=0) and (any=1) plus label_targeted_
         * only_within (always "any" semantics) without needing two indexes. */
        if (jump_target_any(lines[i], tgt))
            jumpref_add(indexes, tgt, i, strncmp(lines[i], "jp ", 3) == 0);
    }
    indexes->version = peep_context.program_version;
}

/* Strip a single trailing ':' from an assembly label, in place. The caller
 * has already copied a known label line (starts_label true) into `s`. */
void strip_label_colon(char *s)
{
    size_t n = strlen(s);
    if (n > 0 && s[n - 1] == ':')
        s[n - 1] = 0;
}

int is_uncond_jp(const char *s)
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

int line_is_label_name(int i, const char *name)
{
    char tmp[MAX_LINE];
    if (i < 0 || i >= nlines)
        return 0;
    sprintf(tmp, "%s:", name);
    return strcmp(lines[i], tmp) == 0;
}

int is_global_asm_label_line(int i)
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

int is_jump_line(const char *s)
{
    return strncmp(s, "jp ", 3) == 0;
}

int jump_target(const char *s, char *out)
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

int label_name_at(int i, char *out)
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

int peep_is_public_line(const char *s)
{
    return strncmp(s, "public ", 7) == 0;
}

/* Find a loop's own closing back-branch: the LAST jump anywhere in
 * [body_start, next "public NAME") that targets `label`. `any` selects
 * jump_target_any (jp+jr) over jump_target (jp only). Returns the matching
 * line index, or -1 if none. Callers still apply their own minimum-body
 * threshold and loop_body_internal_labels_safe proof.
 *
 * Was a plain O(nlines) text scan calling jump_target/jump_target_any on
 * every line from body_start to end of file - profiled as dccpeep's single
 * largest cost on a large app (cobint.c: jump_target/jump_target_any
 * together over 100M calls, roughly half of total runtime, confirmed by
 * stubbing both to an immediate return and re-measuring). Now looks up
 * `label`'s pre-built reference chain (ensure_control_flow_indexes'
 * jumpref index - one entry per jump line targeting it, built once per
 * program_version) and the already-indexed public_functions[] for the
 * boundary, so cost is proportional to how many places actually jump to
 * this one label plus the public-function count, not file size. */
int find_last_loop_back(int body_start, const char *label, int any)
{
    PeepIndexes *indexes = &peep_context.indexes;
    int boundary = nlines;
    int k, g, e;
    int loop_end = -1;

    ensure_control_flow_indexes();
    for (k = 0; k < indexes->public_function_count; ++k) {
        if (indexes->public_functions[k] >= body_start) {
            boundary = indexes->public_functions[k];
            break;
        }
    }

    g = jumpref_find_group(indexes, label);
    if (g < 0)
        return -1;
    for (e = indexes->jumpref_groups[g].first_ref; e >= 0;
         e = indexes->jumpref_entries[e].next_ref) {
        int line = indexes->jumpref_entries[e].line;
        if (line < body_start || line >= boundary)
            continue;
        if (!any && !indexes->jumpref_entries[e].is_jp)
            continue;
        if (line > loop_end)
            loop_end = line;
    }
    return loop_end;
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
 * tests/fint.c; this reachability check is what makes it safe).
 *
 * Same jumpref-index rewrite as find_last_loop_back above, for the same
 * measured reason: walks only label_name's own reference chain instead of
 * every line in [scan_lo, scan_hi). */
static int label_targeted_only_within(const char *label_name,
                                      int scan_lo, int scan_hi,
                                      int range_lo, int range_hi)
{
    PeepIndexes *indexes = &peep_context.indexes;
    int g, e;

    ensure_control_flow_indexes();
    g = jumpref_find_group(indexes, label_name);
    if (g < 0)
        return 1;
    for (e = indexes->jumpref_groups[g].first_ref; e >= 0;
         e = indexes->jumpref_entries[e].next_ref) {
        int k = indexes->jumpref_entries[e].line;
        if (k < scan_lo || k >= scan_hi)
            continue;
        if (k < range_lo || k >= range_hi)
            return 0;
    }
    return 1;
}

/* Validates every internal label within [lo, hi) via
 * label_targeted_only_within, bounded to the current function
 * (find_function_bounds). Returns 1 iff the whole range is safe to treat
 * as a single loop's straight-line-equivalent body. */
int loop_body_internal_labels_safe(int lo, int hi)
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
int find_label_line_in_range(const char *name, int lo, int hi)
{
    PeepIndexes *indexes = &peep_context.indexes;
    size_t name_length = strlen(name);
    int k;

    ensure_control_flow_indexes();
    for (k = 0; k < indexes->label_count; ++k) {
        const PeepLabelIndexEntry *entry = &indexes->labels[k];
        if (entry->line < lo)
            continue;
        if (entry->line >= hi)
            break;
        if (strncmp(entry->name, name, name_length) == 0 &&
            entry->name[name_length] == ':' && entry->name[name_length + 1] == 0)
            return entry->line;
    }
    return -1;
}

void peep_indexed_function_bounds(int from, int include_static,
                                  int *func_start, int *func_end)
{
    PeepIndexes *indexes = &peep_context.indexes;
    int *functions;
    int count;
    int i;

    ensure_control_flow_indexes();
    functions = include_static ? indexes->all_functions : indexes->public_functions;
    count = include_static ? indexes->all_function_count : indexes->public_function_count;
    *func_start = 0;
    *func_end = nlines;
    for (i = 0; i < count; ++i) {
        if (functions[i] <= from)
            *func_start = functions[i];
        else {
            *func_end = functions[i];
            break;
        }
    }
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
int jump_target_any(const char *s, char *out)
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
