/* peep_control_flow.c - shared labels, jumps, and conservative reachability.
 *
 * This module owns textual branch/label parsing and bounded control-flow
 * queries used by multiple pass families. Unknown syntax remains conservative.
 */
#include "dccpeep_internal.h"

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
        if ((nlines && !labels) || (nlines && !public_functions) ||
            (nlines && !all_functions)) {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
        indexes->labels = labels;
        indexes->public_functions = public_functions;
        indexes->all_functions = all_functions;
        indexes->label_capacity = nlines;
        indexes->function_capacity = nlines;
    }

    indexes->label_count = 0;
    indexes->public_function_count = 0;
    indexes->all_function_count = 0;
    for (i = 0; i < nlines; ++i) {
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
 * threshold and loop_body_internal_labels_safe proof. */
int find_last_loop_back(int body_start, const char *label, int any)
{
    int k;
    int loop_end = -1;
    char tgt[128];

    for (k = body_start; k < nlines; ++k) {
        if (strncmp(lines[k], "public ", 7) == 0)
            break;
        if ((any ? jump_target_any(lines[k], tgt)
                 : jump_target(lines[k], tgt)) &&
            strcmp(tgt, label) == 0)
            loop_end = k;
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
 * tests/fint.c; this reachability check is what makes it safe). */
static int label_targeted_only_within(const char *label_name,
                                      int scan_lo, int scan_hi,
                                      int range_lo, int range_hi)
{
    int k;
    char tgt[128];

    for (k = scan_lo; k < scan_hi; ++k) {
        if (jump_target_any(lines[k], tgt) && strcmp(tgt, label_name) == 0) {
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
