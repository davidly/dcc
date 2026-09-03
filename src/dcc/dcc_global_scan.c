/**
 * @file dcc_global_scan.c
 * @brief Collects conservative whole-file global write and address facts.
 *
 * @par Role
 * Runs a one-time lexical prepass over the finalized source, records writes
 * and address-taking for names and direct fields with enclosing-function
 * attribution, then restores source, lexer, macro, and option state before
 * normal parsing begins.
 *
 * @par Key entry points
 * scan_global_write_info(), global_text_write_count(),
 * global_text_addr_taken_count(), and the global_text_field_*() queries.
 *
 * @par Boundary
 * This is token-pattern analysis, not symbol resolution or call-graph proof;
 * shadowing and uncertain shapes may only add conservative false positives.
 * It builds no AST or symbols and emits no code.
 */

#include "dcc.h"

#define MAX_GLOBAL_SCAN_NAMES 512
#define MAX_GLOBAL_SCAN_WRITES 1024
#define MAX_GLOBAL_SCAN_FIELDS 1024
#define MAX_GLOBAL_SCAN_FIELD_WRITES 2048

struct GlobalScanEntry {
    char name[64];
    int write_count;
    int addr_taken_count;
};

static struct GlobalScanEntry g_scan_entries[MAX_GLOBAL_SCAN_NAMES];
static int g_scan_nentries;
static int g_scan_overflowed;

struct GlobalScanWrite {
    char name[64];
    char func[64];
};

static struct GlobalScanWrite g_scan_writes[MAX_GLOBAL_SCAN_WRITES];
static int g_scan_nwrites;

struct GlobalScanFieldEntry {
    char base[64];
    char field[64];
    int write_count;
    int addr_taken_count;
    int all_writes_byte_constants;
};

static struct GlobalScanFieldEntry g_scan_field_entries[MAX_GLOBAL_SCAN_FIELDS];
static int g_scan_nfield_entries;

struct GlobalScanFieldWrite {
    char base[64];
    char field[64];
    char func[64];
};

static struct GlobalScanFieldWrite
    g_scan_field_writes[MAX_GLOBAL_SCAN_FIELD_WRITES];
static int g_scan_nfield_writes;

static struct GlobalScanEntry *find_or_add_scan_entry(const char *name)
{
    int i;

    for (i = 0; i < g_scan_nentries; ++i)
        if (!strcmp(g_scan_entries[i].name, name))
            return &g_scan_entries[i];

    if (g_scan_nentries >= MAX_GLOBAL_SCAN_NAMES) {
        g_scan_overflowed = 1;
        return NULL;
    }

    snprintf(g_scan_entries[g_scan_nentries].name,
             sizeof(g_scan_entries[g_scan_nentries].name), "%s", name);
    g_scan_entries[g_scan_nentries].write_count = 0;
    g_scan_entries[g_scan_nentries].addr_taken_count = 0;
    return &g_scan_entries[g_scan_nentries++];
}

static struct GlobalScanFieldEntry *find_or_add_scan_field_entry(
    const char *base, const char *field)
{
    int i;

    for (i = 0; i < g_scan_nfield_entries; ++i)
        if (!strcmp(g_scan_field_entries[i].base, base) &&
            !strcmp(g_scan_field_entries[i].field, field))
            return &g_scan_field_entries[i];

    if (g_scan_nfield_entries >= MAX_GLOBAL_SCAN_FIELDS) {
        g_scan_overflowed = 1;
        return NULL;
    }

    strncpy(g_scan_field_entries[g_scan_nfield_entries].base, base, 63);
    g_scan_field_entries[g_scan_nfield_entries].base[63] = 0;
    strncpy(g_scan_field_entries[g_scan_nfield_entries].field, field, 63);
    g_scan_field_entries[g_scan_nfield_entries].field[63] = 0;
    g_scan_field_entries[g_scan_nfield_entries].write_count = 0;
    g_scan_field_entries[g_scan_nfield_entries].addr_taken_count = 0;
    g_scan_field_entries[g_scan_nfield_entries].all_writes_byte_constants = 1;
    return &g_scan_field_entries[g_scan_nfield_entries++];
}

static void record_write(const char *name, const char *func)
{
    struct GlobalScanEntry *e = find_or_add_scan_entry(name);
    if (e != NULL)
        e->write_count++;

    if (g_scan_nwrites >= MAX_GLOBAL_SCAN_WRITES) {
        g_scan_overflowed = 1;
        return;
    }
    strncpy(g_scan_writes[g_scan_nwrites].name, name, 63);
    g_scan_writes[g_scan_nwrites].name[63] = 0;
    strncpy(g_scan_writes[g_scan_nwrites].func, func ? func : "", 63);
    g_scan_writes[g_scan_nwrites].func[63] = 0;
    g_scan_nwrites++;
}

static void record_addr_taken(const char *name)
{
    struct GlobalScanEntry *e = find_or_add_scan_entry(name);
    if (e != NULL)
        e->addr_taken_count++;
}

static void record_field_write(const char *base, const char *field,
                               const char *func, int byte_constant)
{
    struct GlobalScanFieldEntry *e =
        find_or_add_scan_field_entry(base, field);
    if (e != NULL) {
        e->write_count++;
        if (!byte_constant)
            e->all_writes_byte_constants = 0;
    }

    if (g_scan_nfield_writes >= MAX_GLOBAL_SCAN_FIELD_WRITES) {
        g_scan_overflowed = 1;
        return;
    }
    strncpy(g_scan_field_writes[g_scan_nfield_writes].base, base, 63);
    g_scan_field_writes[g_scan_nfield_writes].base[63] = 0;
    strncpy(g_scan_field_writes[g_scan_nfield_writes].field, field, 63);
    g_scan_field_writes[g_scan_nfield_writes].field[63] = 0;
    strncpy(g_scan_field_writes[g_scan_nfield_writes].func,
            func ? func : "", 63);
    g_scan_field_writes[g_scan_nfield_writes].func[63] = 0;
    g_scan_nfield_writes++;
}

static void record_field_addr_taken(const char *base, const char *field)
{
    struct GlobalScanFieldEntry *e =
        find_or_add_scan_field_entry(base, field);
    if (e != NULL)
        e->addr_taken_count++;
}

static int token_starts_assignment_or_incdec(int kind)
{
    switch (kind) {
    case '=': case TOK_ADDEQ: case TOK_SUBEQ: case TOK_MULEQ:
    case TOK_DIVEQ: case TOK_MODEQ: case TOK_ANDEQ: case TOK_OREQ:
    case TOK_XOREQ: case TOK_SHLEQ: case TOK_SHREQ:
    case TOK_INC: case TOK_DEC:
        return 1;
    default:
        return 0;
    }
}

static int token_ends_simple_assignment_rhs(int kind)
{
    return kind == ';' || kind == ',' || kind == ')' ||
           kind == ']' || kind == ':';
}

/* One-time whole-file lexical scan (see file header). Must run before any
 * real parsing begins: it fully rewinds the lexer position and the macro
 * table afterward, but does NOT rewind file-scope tables that only grow
 * monotonically during real compilation (there are none touched here - this
 * only tokenises, it builds no symbols, types, or AST). */
void scan_global_write_info(void)
{
    long saved_posi;
    long saved_tok_start;
    int saved_line;
    int saved_tok_line;
    struct Token saved_tok;
    int saved_stack_check;
    struct Def *saved_defs;
    int saved_ndefs;
    char *saved_src;
    long saved_src_len;
    int brace_depth;
    int paren_depth;
    char func_at_depth0[64];
    char pending_call_name[64];
    int prev_kind;
    char prev_text[MAX_TOK_TEXT];

    saved_posi = g_lex.posi;
    saved_tok_start = g_lex.tok_start_pos;
    saved_line = g_lex.line_no;
    saved_tok_line = g_lex.tok_line;
    saved_tok = g_lex.tok;
    saved_stack_check = opt_stack_check;

    saved_defs = (struct Def *)xmalloc(sizeof(defs));
    saved_ndefs = ndefs;
    memcpy(saved_defs, defs, sizeof(defs));

    /* Object-like macro expansion (dcc_preproc.c's replace_source_range)
     * rewrites the shared `src` buffer IN PLACE - it reallocates a new
     * buffer, repoints the global `src` at it, and updates `src_len`. That
     * makes tokenising the file even once observably non-idempotent for a
     * macro whose expansion text itself ends in the macro's own name (e.g.
     * tests/cobint.c's `#define tp G->tp`): the first expansion rewrites
     * "tp" to "G->tp" in the actual source text, so a second tokenisation
     * pass over what it assumes is still the original text finds "tp"
     * again at the tail and expands it again, into "G->G->tp". Snapshot
     * and fully restore the buffer's *content*, not just posi/src_len,
     * so this scan is truly invisible to the real pass that follows. */
    saved_src = (char *)xmalloc((size_t)src_len + 1);
    memcpy(saved_src, src, (size_t)src_len + 1);
    saved_src_len = src_len;

    /* This scan tokenises the whole file purely for its write/addr-taken
     * bookkeeping; the real pass re-tokenises the same source afterward. Any
     * lexer/preprocessor diagnostic here (over-long string literal, function-
     * like macro arg-count mismatch, ...) would therefore be emitted twice, so
     * suppress diagnostics for the duration - the real pass is the one that
     * surfaces them to the user. This also keeps the scan from tripping the
     * errors>40 fatal before real compilation begins. */
    asm_suppress_depth++;

    g_lex.posi = 0;
    g_lex.tok_start_pos = 0;
    g_lex.line_no = 1;
    g_lex.tok_line = 1;
    next_token();

    brace_depth = 0;
    paren_depth = 0;
    func_at_depth0[0] = 0;
    pending_call_name[0] = 0;
    prev_kind = TOK_EOF;
    prev_text[0] = 0;

    while (g_lex.tok.kind != TOK_EOF) {
        if (g_lex.tok.kind == TOK_ID && prev_kind != '.' && prev_kind != TOK_ARROW) {
            char name[64];
            LexState _ls = lex_save();
            int next_kind;
            int field_kind = TOK_EOF;
            int field_next_kind = TOK_EOF;
            int field_rhs_kind = TOK_EOF;
            int field_rhs_end_kind = TOK_EOF;
            long field_rhs_value = 0;
            char field_name[64];

            strncpy(name, g_lex.tok.text, 63);
            name[63] = 0;
            field_name[0] = 0;

            next_token();
            next_kind = g_lex.tok.kind;
            if (next_kind == '.' || next_kind == TOK_ARROW) {
                next_token();
                field_kind = g_lex.tok.kind;
                if (field_kind == TOK_ID) {
                    strncpy(field_name, g_lex.tok.text, 63);
                    field_name[63] = 0;
                    next_token();
                    field_next_kind = g_lex.tok.kind;
                    if (field_next_kind == '=') {
                        next_token();
                        field_rhs_kind = g_lex.tok.kind;
                        field_rhs_value = g_lex.tok.val;
                        next_token();
                        field_rhs_end_kind = g_lex.tok.kind;
                    }
                }
            }

            lex_restore(&_ls);

            /* `&EXPR->field`/`&EXPR.field`/`&EXPR[i]` takes the address of
             * a *member/element reached through* this identifier, not of
             * the identifier's own storage (postfix `->`/`.`/`[` bind
             * tighter than unary `&`, so `&G->sym[i]` parses as
             * `&(G->sym[i])`) - only count a bare `&name` (nothing
             * postfix-chained onto it) as taking name's own address. */
            if (prev_kind == '&' && next_kind != '.' && next_kind != TOK_ARROW &&
                next_kind != '[') {
                record_addr_taken(name);
            } else if (prev_kind == TOK_INC || prev_kind == TOK_DEC) {
                record_write(name, func_at_depth0);
            } else if (token_starts_assignment_or_incdec(next_kind)) {
                record_write(name, func_at_depth0);
            }

            if (field_kind == TOK_ID) {
                if (prev_kind == '&' && field_next_kind != '.' &&
                    field_next_kind != TOK_ARROW &&
                    field_next_kind != '[') {
                    record_field_addr_taken(name, field_name);
                } else if (prev_kind == TOK_INC || prev_kind == TOK_DEC) {
                    record_field_write(name, field_name, func_at_depth0, 0);
                } else if (token_starts_assignment_or_incdec(
                               field_next_kind)) {
                    record_field_write(
                        name, field_name, func_at_depth0,
                        field_next_kind == '=' &&
                        field_rhs_kind == TOK_NUM &&
                        field_rhs_value >= 0 && field_rhs_value <= 255 &&
                        token_ends_simple_assignment_rhs(
                            field_rhs_end_kind));
                }
            }

            if (paren_depth == 0 && brace_depth == 0) {
                strncpy(pending_call_name, name, 63);
                pending_call_name[63] = 0;
            }
        } else if (g_lex.tok.kind == '(') {
            paren_depth++;
        } else if (g_lex.tok.kind == ')') {
            if (paren_depth > 0)
                paren_depth--;
        } else if (g_lex.tok.kind == '{') {
            if (brace_depth == 0 && paren_depth == 0) {
                strncpy(func_at_depth0, pending_call_name, 63);
                func_at_depth0[63] = 0;
            }
            brace_depth++;
        } else if (g_lex.tok.kind == '}') {
            if (brace_depth > 0)
                brace_depth--;
            if (brace_depth == 0)
                func_at_depth0[0] = 0;
        } else if (g_lex.tok.kind != TOK_ID) {
            pending_call_name[0] = 0;
        }

        prev_kind = g_lex.tok.kind;
        strncpy(prev_text, g_lex.tok.text, MAX_TOK_TEXT - 1);
        prev_text[MAX_TOK_TEXT - 1] = 0;
        (void)prev_text;
        next_token();
    }

    asm_suppress_depth--;

    ndefs = saved_ndefs;
    memcpy(defs, saved_defs, sizeof(defs));
    free(saved_defs);

    /* `src` has very likely been reallocated one or more times by macro
     * expansion during the scan above (see the comment where saved_src is
     * captured) - free whatever it currently points to and restore the
     * untouched snapshot, rather than relying on posi/src_len alone. */
    free(src);
    src = saved_src;
    src_len = saved_src_len;
    g_src_generation++;

    /* Position-keyed preprocessor state populated by that same macro
     * expansion (dcc_preproc.c's disabled-macro-range tracking and
     * push_macro/pop_macro stack) is meaningless once `src` has been
     * swapped back - see reset_preproc_scan_state's own comment. */
    reset_preproc_scan_state();

    g_lex.posi = saved_posi;
    g_lex.tok_start_pos = saved_tok_start;
    g_lex.line_no = saved_line;
    g_lex.tok_line = saved_tok_line;
    g_lex.tok = saved_tok;
    opt_stack_check = saved_stack_check;
}

/* Total number of textual write-context occurrences of `name` anywhere in
 * the file (see the file header for exactly what counts). If the scan
 * table overflowed, every query is answered as "unsafe" (a large sentinel),
 * since an overflowed scan cannot prove an absence of writes. */
int global_text_write_count(const char *name)
{
    int i;

    if (g_scan_overflowed)
        return 999;
    for (i = 0; i < g_scan_nentries; ++i)
        if (!strcmp(g_scan_entries[i].name, name))
            return g_scan_entries[i].write_count;
    return 0;
}

int global_text_addr_taken_count(const char *name)
{
    int i;

    if (g_scan_overflowed)
        return 999;
    for (i = 0; i < g_scan_nentries; ++i)
        if (!strcmp(g_scan_entries[i].name, name))
            return g_scan_entries[i].addr_taken_count;
    return 0;
}

/* Is there a recorded write to `name` whose enclosing function is exactly
 * `func`? Used together with global_text_write_count(name)==1 to prove: the
 * one write anywhere in the file is not inside the function currently being
 * compiled. */
int global_text_written_in_function(const char *name, const char *func)
{
    int i;

    if (g_scan_overflowed)
        return 1;
    for (i = 0; i < g_scan_nwrites; ++i)
        if (!strcmp(g_scan_writes[i].name, name) &&
            !strcmp(g_scan_writes[i].func, func))
            return 1;
    return 0;
}

int global_text_field_write_count(const char *base, const char *field)
{
    int i;

    if (g_scan_overflowed)
        return 999;
    for (i = 0; i < g_scan_nfield_entries; ++i)
        if (!strcmp(g_scan_field_entries[i].base, base) &&
            !strcmp(g_scan_field_entries[i].field, field))
            return g_scan_field_entries[i].write_count;
    return 0;
}

int global_text_field_addr_taken_count(const char *base, const char *field)
{
    int i;

    if (g_scan_overflowed)
        return 999;
    for (i = 0; i < g_scan_nfield_entries; ++i)
        if (!strcmp(g_scan_field_entries[i].base, base) &&
            !strcmp(g_scan_field_entries[i].field, field))
            return g_scan_field_entries[i].addr_taken_count;
    return 0;
}

int global_text_field_all_writes_byte_constants(
    const char *base, const char *field)
{
    int i;

    if (g_scan_overflowed)
        return 0;
    for (i = 0; i < g_scan_nfield_entries; ++i)
        if (!strcmp(g_scan_field_entries[i].base, base) &&
            !strcmp(g_scan_field_entries[i].field, field))
            return g_scan_field_entries[i].write_count > 0 &&
                   g_scan_field_entries[i].all_writes_byte_constants;
    return 0;
}

int global_text_field_written_in_function(
    const char *base, const char *field, const char *func)
{
    int i;

    if (g_scan_overflowed)
        return 1;
    for (i = 0; i < g_scan_nfield_writes; ++i)
        if (!strcmp(g_scan_field_writes[i].base, base) &&
            !strcmp(g_scan_field_writes[i].field, field) &&
            !strcmp(g_scan_field_writes[i].func, func))
            return 1;
    return 0;
}
