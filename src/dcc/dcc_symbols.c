/**
 * @file dcc_symbols.c
 * @brief Owns symbol lookup, scopes, linkage bookkeeping, and symbol access.
 *
 * @par Role
 * Allocates globals, locals, parameters, temporaries, and strings; manages
 * block and for-init renames plus VLA scope/fixup state; tracks deferred
 * EXTRNs; and supplies symbol-layout predicates plus sizeof/offsetof parsing.
 *
 * @par Key entry points
 * find_sym(), add_global(), add_local_alloc(), enter_scope(), leave_scope(),
 * record_extern_reference() and sizeof_parse_primary_type().
 *
 * @par Boundary
 * dcc_state.c stores shared tables, dcc_asmname.c maps target spellings, and
 * dcc_types.c owns type construction. VLA scope helpers record MIR metadata,
 * not instructions; MIR alone emits production function bodies.
 */

#include "dcc.h"
#include "dcc_mir.h"

static int block_scope_ids[MAX_SCOPE_DEPTH];
static int block_scope_rename_counts[MAX_SCOPE_DEPTH];

/*
 * C99 for-init renames.  While code generation (or the frame-sizing scan) is
 * inside a for-loop with init declarations, each declared source name is mapped
 * to a unique internal local name.  That gives the variable real C99 loop
 * scope even though dcc's local symbol table is otherwise function-flat.
 * resolve_local_rename applies the innermost active mapping; it is idempotent
 * because the mapped-to names contain '#', which never appears in a source
 * identifier and so never matches a "from" entry.
 */
const char *resolve_local_rename(const char *name)
{
    int k;
    for (k = g_func_pass.forren_n - 1; k >= 0; --k) {
        if (!strcmp(g_forren_from[k], name))
            return g_forren_to[k];
    }
    return name;
}

void make_for_rename_name(char *dst, int dstsz, const char *from, int for_seq, int rename_index)
{
    char suffix[24];
    int from_len;
    int suffix_len;

    sprintf(suffix, "#%d#%d", for_seq, rename_index);
    suffix_len = (int)strlen(suffix);
    if (dstsz <= suffix_len + 1)
        fatal("for-init rename buffer too small");

    from_len = (int)strlen(from);
    if (from_len > dstsz - suffix_len - 1)
        from_len = dstsz - suffix_len - 1;

    memcpy(dst, from, from_len);
    strcpy(dst + from_len, suffix);
}

void add_for_scope_rename(int for_seq, const char *from)
{
    int n;

    if (for_seq >= MAX_FOR_SCOPES)
        fatal("too many for statements");

    n = g_for_rename_count[for_seq];
    if (n >= MAX_FOR_SCOPE_RENAMES)
        fatal("too many for-init declarators");

    strncpy(g_for_rename_from[for_seq][n], from, 63);
    g_for_rename_from[for_seq][n][63] = 0;
    make_for_rename_name(g_for_rename_to[for_seq][n], 64,
                         g_for_rename_from[for_seq][n], for_seq, n);
    g_for_rename_count[for_seq] = n + 1;
}

const char *enter_for_decl_rename(const char *name)
{
    int n;

    if (g_func_pass.for_decl_seq < 0)
        fatal("bad for-init scope");

    n = g_func_pass.for_decl_rename_index;
    if (g_func_pass.for_decl_recording) {
        add_for_scope_rename(g_func_pass.for_decl_seq, name);
    } else {
        if (g_func_pass.for_decl_seq >= MAX_FOR_SCOPES)
            fatal("too many for statements");
        if (n >= g_for_rename_count[g_func_pass.for_decl_seq])
            fatal("for-init scope mismatch");
    }

    push_for_rename(g_for_rename_from[g_func_pass.for_decl_seq][n],
                    g_for_rename_to[g_func_pass.for_decl_seq][n]);
    g_func_pass.for_decl_rename_index = n + 1;
    return g_for_rename_to[g_func_pass.for_decl_seq][n];
}

const char *enter_block_decl_rename(const char *name)
{
    static char renamed[MAX_SCOPE_DEPTH][MAX_BLOCK_SCOPE_RENAMES][64];
    int depth;
    int ordinal;

    if (g_func_pass.scope_depth <= 1 || find_local_decl(name) != NULL)
        return name;
    depth = g_func_pass.scope_depth - 1;
    ordinal = block_scope_rename_counts[depth];
    if (ordinal >= MAX_BLOCK_SCOPE_RENAMES)
        fatal("too many block-scope shadow declarations");
    snprintf(renamed[depth][ordinal], sizeof(renamed[depth][ordinal]),
             "%s#b%d#%d", name, block_scope_ids[depth], ordinal);
    push_for_rename(name, renamed[depth][ordinal]);
    block_scope_rename_counts[depth] = ordinal + 1;
    return renamed[depth][ordinal];
}

const char *enter_static_local_rename(const char *name,
                                      const char *backing_name)
{
    static char renamed[MAX_SCOPE_DEPTH][MAX_BLOCK_SCOPE_RENAMES][64];
    int depth;
    int ordinal;

    if (g_func_pass.scope_depth <= 0)
        return name;
    depth = g_func_pass.scope_depth - 1;
    ordinal = block_scope_rename_counts[depth];
    if (ordinal >= MAX_BLOCK_SCOPE_RENAMES)
        fatal("too many block-scope shadow declarations");
    snprintf(renamed[depth][ordinal], sizeof(renamed[depth][ordinal]),
             "%s#%s", name, backing_name);
    push_for_rename(name, renamed[depth][ordinal]);
    block_scope_rename_counts[depth] = ordinal + 1;
    return renamed[depth][ordinal];
}

void push_for_rename(const char *from, const char *to)
{
    if (g_func_pass.forren_n >= MAX_FORREN)
        fatal("too many nested for-init scopes");
    strncpy(g_forren_from[g_func_pass.forren_n], from, 63);
    g_forren_from[g_func_pass.forren_n][63] = 0;
    strncpy(g_forren_to[g_func_pass.forren_n], to, 63);
    g_forren_to[g_func_pass.forren_n][63] = 0;
    g_func_pass.forren_n++;
}

void pop_for_rename(void)
{
    if (g_func_pass.forren_n > 0)
        g_func_pass.forren_n--;
}

const char *sym_asm_name(struct Sym *s)
{
    if (s && s->link_name[0])
        return s->link_name;
    return s ? s->name : "";
}

void debug_symbol_name(const struct Sym *s, char *name, size_t name_size)
{
    const char *internal = s ? s->name : "";
    const char *suffix = strchr(internal, '#');
    size_t length = suffix ? (size_t)(suffix - internal) : strlen(internal);

    if (name_size == 0)
        return;
    if (length >= name_size)
        length = name_size - 1;
    memcpy(name, internal, length);
    name[length] = 0;
}

/*
 * General lexical block scope.  The frame-sizing scan and codegen both bracket
 * every { } block with enter_scope/leave_scope, and both build the locals[]
 * table the same way: a block's locals are truncated away when it closes.
 * Storage (local_size) is monotonic, so a block-local still gets a distinct
 * frame slot and the reserved frame is the sum over all scopes; because codegen
 * rebuilds the table exactly as the scan did, the two passes assign identical
 * offsets and resolve names identically.
 */
void enter_scope(void)
{
    if (g_func_pass.scope_depth >= MAX_SCOPE_DEPTH)
        fatal("too many nested block scopes");
    block_scope_ids[g_func_pass.scope_depth] = g_func_pass.block_seq++;
    block_scope_rename_counts[g_func_pass.scope_depth] = 0;
    g_scope_watermark[g_func_pass.scope_depth++] = g_frame.nlocals;
    /* A freshly opened scope has no VLA save slot yet. */
    if (g_func_pass.scope_depth < MAX_SCOPE_DEPTH)
        g_vla_scope_off[g_func_pass.scope_depth] = 0;
}

/*
 * Ensure the current block scope has a hidden slot in which to save SP before
 * its first VLA is allocated, so the scope's VLAs can be reclaimed when it
 * exits.  Called by BOTH the frame-sizing scan and codegen at the first VLA in
 * a scope, so the two passes reserve the identical slot (local_size is
 * monotonic).  Returns the slot's frame offset, or 0 if the scope already has
 * one / on overflow.
 */
int vla_scope_ensure_save_slot(void)
{
    struct Sym *s;

    if (g_func_pass.scope_depth < 0 || g_func_pass.scope_depth >= MAX_SCOPE_DEPTH)
        return 0;
    if (g_vla_scope_off[g_func_pass.scope_depth] != 0)
        return 0;                       /* already allocated for this scope */
    s = add_local_alloc("#vlasp", TYPE_INT, 2);
    g_vla_scope_off[g_func_pass.scope_depth] = s->offset;
    /* MIR allocation and epilogues must account for dynamic stack storage. */
    current_function_has_vla = 1;
    return s->offset;
}

int vla_active_scope_depth(void)
{
    int d;
    for (d = 1; d <= g_func_pass.scope_depth && d < MAX_SCOPE_DEPTH; ++d)
        if (g_vla_scope_off[d] != 0)
            return d;
    return 0;
}

/* Record the scope's SP save for MIR lowering; sizing passes only reserve it. */
void capture_vla_save_sp(int off)
{
    mir_capture_vla_save(off);
}

/* Record reclamation of the scope's VLAs without emitting instructions. */
void capture_vla_restore_sp(int off)
{
    mir_capture_vla_restore(off);
}

/*
 * Reclaim VLAs when leaving a loop/switch via break or continue.  Restore SP to
 * the outermost active VLA save slot among the scopes being exited (depths
 * greater than floor_depth); restoring an outer slot reclaims every inner
 * scope's VLAs, so only one restore is needed.  Emits nothing when no VLA was
 * declared inside the loop.
 */
void capture_vla_restore_for_flow(int floor_depth)
{
    int d;
    for (d = floor_depth + 1; d <= g_func_pass.scope_depth && d < MAX_SCOPE_DEPTH; ++d) {
        if (g_vla_scope_off[d] != 0) {
            capture_vla_restore_sp(g_vla_scope_off[d]);
            return;
        }
    }
}

/*
 * Snapshot a forward goto's active VLA scopes for target validation and MIR
 * reclamation. Reserve the established metadata label ID even during sizing
 * passes so removing instruction emission does not renumber later labels.
 */
int vla_record_fwd_goto(int label_index, int line)
{
    struct VlaFwdGoto *g;
    int fixup;
    int d, i, same;

    /* Reuse an existing snapshot when another goto already targets this
     * label with the identical active-VLA snapshot: same target and same
     * scope offsets mean identical reclaim, so one shared entry suffices (and
     * a fresh label is not consumed). */
    for (i = 0; i < g_vla_fwd_ngoto; ++i) {
        g = &g_vla_fwd_gotos[i];
        if (g->label_index != label_index || g->snap_depth != g_func_pass.scope_depth)
            continue;
        same = 1;
        for (d = 0; d < MAX_SCOPE_DEPTH; ++d) {
            int off = (d <= g_func_pass.scope_depth) ? g_vla_scope_off[d] : 0;
            if (g->snap_off[d] != off) { same = 0; break; }
        }
        if (same)
            return g->fixup_id;
    }

    fixup = new_label();
    if (g_vla_fwd_ngoto >= MAX_VLA_FWD_GOTOS) {
        if (asm_suppress_depth == 0)
            fatal("too many forward gotos out of variable-length array scopes");
        return fixup;
    }
    g = &g_vla_fwd_gotos[g_vla_fwd_ngoto++];
    g->label_index = label_index;
    g->fixup_id = fixup;
    g->line = line;
    g->snap_depth = g_func_pass.scope_depth;
    for (d = 0; d < MAX_SCOPE_DEPTH; ++d)
        g->snap_off[d] = (d <= g_func_pass.scope_depth) ? g_vla_scope_off[d] : 0;
    return fixup;
}

/* Is frame-slot offset `off` one of the VLA scopes currently active (i.e. an
 * enclosing scope of the point being emitted)?  Offsets are monotonic and never
 * reused, so a matching offset means the very same scope instance. */
static int vla_off_active_now(int off)
{
    int d;
    if (off == 0)
        return 0;
    for (d = 1; d <= g_func_pass.scope_depth && d < MAX_SCOPE_DEPTH; ++d)
        if (g_vla_scope_off[d] == off)
            return 1;
    return 0;
}

static int vla_off_in_label_scope(int label_index, int off)
{
    int d;
    if (off == 0)
        return 0;
    for (d = 1; d <= ulabel_vla_snap_depth[label_index] && d < MAX_SCOPE_DEPTH; ++d)
        if (ulabel_vla_snap_off[label_index][d] == off)
            return 1;
    return 0;
}

void vla_snapshot_user_label(int label_index)
{
    int d;
    if (label_index < 0 || label_index >= MAX_USER_LABELS)
        return;
    ulabel_vla_snap_depth[label_index] = g_func_pass.scope_depth;
    for (d = 0; d < MAX_SCOPE_DEPTH; ++d)
        ulabel_vla_snap_off[label_index][d] = (d <= g_func_pass.scope_depth) ? g_vla_scope_off[d] : 0;
}

int vla_jump_enters_label_scope(int label_index)
{
    int d;
    if (label_index < 0 || label_index >= MAX_USER_LABELS)
        return 0;
    for (d = 1; d <= ulabel_vla_snap_depth[label_index] && d < MAX_SCOPE_DEPTH; ++d) {
        int off = ulabel_vla_snap_off[label_index][d];
        if (off != 0 && !vla_off_active_now(off))
            return 1;
    }
    return 0;
}

void capture_vla_restore_to_label_scope(int label_index)
{
    int d;
    if (label_index < 0 || label_index >= MAX_USER_LABELS)
        return;
    for (d = 1; d <= g_func_pass.scope_depth && d < MAX_SCOPE_DEPTH; ++d) {
        int off = g_vla_scope_off[d];
        if (off != 0 && !vla_off_in_label_scope(label_index, off)) {
            capture_vla_restore_sp(off);
            return;
        }
    }
}

/*
 * Resolve forward-goto scope snapshots at their target. Reject jumps into VLA
 * scopes and record restoration of the scopes being left through MIR replay.
 * Sizing/metadata passes validate the same constraints without emitting code.
 */
void vla_resolve_fwd_gotos(int label_index)
{
    int i, d, k;
    int any;
    int bad;

    any = 0;
    for (i = 0; i < g_vla_fwd_ngoto; ++i)
        if (g_vla_fwd_gotos[i].label_index == label_index) { any = 1; break; }
    if (!any)
        return;

    for (i = 0; i < g_vla_fwd_ngoto; ++i) {
        struct VlaFwdGoto *g = &g_vla_fwd_gotos[i];
        if (g->label_index != label_index)
            continue;

        /* Jump-into check: each VLA scope active at the label must be one that
         * was also active at the goto (same frame slot). */
        bad = 0;
        for (d = 1; d <= g_func_pass.scope_depth && d < MAX_SCOPE_DEPTH; ++d) {
            int off = g_vla_scope_off[d];
            int seen = 0;
            if (off == 0)
                continue;
            for (k = 1; k <= g->snap_depth && k < MAX_SCOPE_DEPTH; ++k)
                if (g->snap_off[k] == off) { seen = 1; break; }
            if (!seen) {
                dcc_error_at(g_lex.tok.file[0] ? g_lex.tok.file :
                                 (input_name ? input_name : "<input>"),
                             g->line, -1,
                             "goto into a variable-length array scope is not supported",
                             NULL);
                bad = 1;
                break;
            }
        }
        if (bad)
            continue;

        /* Reclaim the goto's inner VLA scopes the label is not within: restore
         * the outermost such slot (which reclaims it and every deeper scope). */
        for (k = 1; k <= g->snap_depth && k < MAX_SCOPE_DEPTH; ++k) {
            int off = g->snap_off[k];
            if (off != 0 && !vla_off_active_now(off)) {
                capture_vla_restore_sp(off);
                break;
            }
        }
    }
}

void leave_scope(void)
{
    int first;
    int i;
    if (g_func_pass.scope_depth <= 0)
        return;
    /* Block-local names leave scope.  local_size is intentionally left alone:
     * storage is monotonic (slots are never reused), so the frame size still
     * equals the sum over every scope. */
    --g_func_pass.scope_depth;
    while (block_scope_rename_counts[g_func_pass.scope_depth] > 0) {
        pop_for_rename();
        --block_scope_rename_counts[g_func_pass.scope_depth];
    }
    first = g_scope_watermark[g_func_pass.scope_depth];
    for (i = first; i < g_frame.nlocals; ++i)
        emit_debug_variable_end(&locals[i]);
    g_frame.nlocals = first;
}

/* Lookup used while DECLARING a local: only the innermost open block is
 * considered, and for-init renames are ignored.  This lets an inner block (or a
 * for body) declare a name that shadows an outer local, a parameter, or an
 * active for-init loop variable instead of binding to it. */
struct Sym *find_local_decl(const char *name)
{
    int i, base;
    base = g_func_pass.scope_depth > 0 ? g_scope_watermark[g_func_pass.scope_depth - 1] : 0;
    for (i = g_frame.nlocals - 1; i >= base; --i)
        if (!strcmp(locals[i].name, name)) return &locals[i];
    return NULL;
}

struct Sym *find_local(const char *name)
{
    int i;
    const char *rn;
    int plain_idx;
    int ren_idx;

    /* Two backward searches over the in-scope locals (nlocals is truncated on
     * block exit, so out-of-scope names are excluded): one for the original
     * spelling (block locals and ordinary locals) and one for an active
     * for-init rename (the loop variable lives under a unique internal name).
     * The higher index wins because declaration order equals scope depth, so
     * the innermost binding is selected.  This makes a for-init variable shadow
     * an outer same-named local, and an inner block redeclaration shadow the
     * for-init variable, both correctly. */
    plain_idx = -1;
    for (i = g_frame.nlocals - 1; i >= 0; --i)
        if (!strcmp(locals[i].name, name)) { plain_idx = i; break; }

    ren_idx = -1;
    rn = resolve_local_rename(name);
    if (rn != name) {
        for (i = g_frame.nlocals - 1; i >= 0; --i)
            if (!strcmp(locals[i].name, rn)) { ren_idx = i; break; }
    }

    if (ren_idx > plain_idx) return &locals[ren_idx];
    if (plain_idx >= 0) return &locals[plain_idx];
    return NULL;
}

/* Hash index over globals[], keyed by name. add_global (the sole inserter -
 * globals[] is otherwise append-only for the whole compile: nglobals never
 * resets or decrements) is the only place that needs to update this, since
 * every other reference to globals[] is a lookup through find_global.
 *
 * Buckets/chain links store (index + 1), 0 meaning empty/end-of-chain, so
 * the static arrays' default zero-initialization already leaves the table
 * empty - no explicit init pass needed for a process that compiles exactly
 * one translation unit before exiting. */
#define GLOBAL_HASH_BUCKETS 2048
static int global_hash_buckets[GLOBAL_HASH_BUCKETS];
static int global_hash_next[MAX_SYMS];

static unsigned global_name_hash(const char *name)
{
    unsigned h = 0;
    while (*name)
        h = h * 131u + (unsigned char)*name++;
    return h & (GLOBAL_HASH_BUCKETS - 1);
}

static void global_hash_insert(int index)
{
    unsigned h = global_name_hash(globals[index].name);
    global_hash_next[index] = global_hash_buckets[h];
    global_hash_buckets[h] = index + 1;
}

struct Sym *find_global(const char *name)
{
    unsigned h = global_name_hash(name);
    int slot = global_hash_buckets[h];

    while (slot) {
        int index = slot - 1;
        if (!strcmp(globals[index].name, name))
            return &globals[index];
        slot = global_hash_next[index];
    }
    return NULL;
}

struct Sym *find_sym(const char *name)
{
    struct Sym *s;
    s = find_local(name);
    if (s) return s;
    return find_global(name);
}


int is_global_char_array_sym(struct Sym *s)
{
    if (!s) return 0;
    if (s->storage != SC_GLOBAL) return 0;
    if (!s->is_array) return 0;
    if ((s->type & 15) != TYPE_CHAR) return 0;
    if (type_ptr_depth(s->type) != 0) return 0;
    return 1;
}

struct Sym *add_global(const char *name, int type, int storage)
{
    struct Sym *s;
    s = find_global(name);
    if (s) {
        if (storage == SC_FUNC) s->storage = SC_FUNC;
        return s;
    }

    if (nglobals >= MAX_SYMS) fatal("too many globals");

    s = &globals[nglobals++];
    memset(s, 0, sizeof(*s));
    snprintf(s->name, sizeof(s->name), "%s", name);
    s->type = type;
    s->storage = storage;
    s->size = type_size(type);
    global_hash_insert(nglobals - 1);
    return s;
}

struct Sym *add_local_known(const char *name, int type, int storage,
                                   int offset, int bytes)
{
    struct Sym *s;

    if (g_frame.nlocals >= MAX_LOCALS) fatal("too many locals");

    s = &locals[g_frame.nlocals++];
    memset(s, 0, sizeof(*s));
    snprintf(s->name, sizeof(s->name), "%s", name);
    s->type = type;
    s->storage = storage;
    s->offset = offset;
    s->size = bytes;
    return s;
}

/* A block-scope extern introduces a lexical alias for an object with external
 * linkage; it does not reserve automatic storage. */
struct Sym *add_block_extern_alias(const char *name, const char *link_name,
                                   int type, int bytes)
{
    struct Sym *global = find_global(link_name);
    struct Sym *local;

    if (global == NULL)
        global = add_global(link_name, type, SC_EXTERN);
    local = add_local_known(name, type, global->storage, 0, bytes);
    strncpy(local->link_name, link_name, sizeof(local->link_name) - 1);
    local->link_name[sizeof(local->link_name) - 1] = 0;
    local->needs_extrn = global->needs_extrn;
    local->is_defined = global->is_defined;
    return local;
}

struct Sym *add_local_alloc(const char *name, int type, int bytes)
{
    struct Sym *s;
    g_frame.local_size += bytes;
    s = add_local_known(name, type, SC_LOCAL, -g_frame.local_size, bytes);
    return s;
}

struct Sym *add_compound_literal_local(int type)
{
    char name[64];
    int bytes;

    bytes = type_size(type);
    if (bytes <= 0)
        bytes = 2;

    sprintf(name, "#clit%d", g_func_pass.compound_literal_seq++);
    return add_local_alloc(name, type, bytes);
}

struct Sym *add_param_alloc(const char *name, int type)
{
    struct Sym *s;
    int sz = type_size(type);
    if (sz < 2) sz = 2;
    s = add_local_known(name, type, SC_PARAM, g_frame.param_offset, sz);
    g_frame.param_offset += sz;
    return s;
}

int add_string_ex(const char *s, int len, int is_wide)
{
    int i;
    char *copy;
    is_wide = is_wide ? 1 : 0;
    for (i = 0; i < nstrings; i++)
        if (string_wide[i] == is_wide && string_len[i] == len &&
            memcmp(strings[i], s, (size_t)len) == 0)
            return i;
    if (nstrings >= MAX_STRINGS) fatal("too many strings");
    copy = (char *)xmalloc((size_t)len + 1);
    memcpy(copy, s, (size_t)len);
    copy[len] = 0;
    strings[nstrings] = copy;
    string_wide[nstrings] = is_wide;
    string_len[nstrings] = len;
    return nstrings++;
}

char *read_adjacent_string_literals_ex(int *is_widep, int *lenp)
{
    char *buf;
    int cap;
    int len;
    int is_wide;

    cap = 256;
    len = 0;
    is_wide = 0;
    buf = (char *)xmalloc((size_t)cap);
    buf[0] = 0;

    while (g_lex.tok.kind == TOK_STR || g_lex.tok.kind == TOK_WSTR) {
        int slen;
        if (g_lex.tok.kind == TOK_WSTR)
            is_wide = 1;

        /*
         * Use the lexer's recorded length, not strlen(tok.text): a literal
         * containing a \0 escape has real bytes past that point, which
         * strlen() would silently discard.
         */
        slen = g_lex.tok.text_len;
        if (len + slen + 1 > cap) {
            char *nbuf;
            int ncap;
            ncap = cap;
            while (len + slen + 1 > ncap)
                ncap *= 2;
            nbuf = (char *)xmalloc((size_t)ncap);
            memcpy(nbuf, buf, (size_t)len);
            free(buf);
            buf = nbuf;
            cap = ncap;
        }
        memcpy(buf + len, g_lex.tok.text, (size_t)slen);
        len += slen;
        buf[len] = 0;
        next_token();
    }

    if (is_widep)
        *is_widep = is_wide;
    if (lenp)
        *lenp = len;
    return buf;
}

/* If an extern symbol had its extrn deferred, emit it now (once). */
void record_extern_reference(struct Sym *s)
{
    int i;

    /*
     * Do not emit M80 EXTRN at the reference site.  C file-scope function
     * declarations are external by default, but the same function may still be
     * defined later in this translation unit.  Emitting EXTRN eagerly can then
     * create duplicate EXTRN/PUBLIC trouble.  Instead, remember that code used
     * the symbol and flush only still-undefined symbols at end of assembly.
     */
    if (!s || !s->needs_extrn || s->is_defined)
        return;

    /*
     * Defer all EXTRNs to emit_deferred_extrns() at the end of the file.
     * M80 resolves externals in two passes, so EXTRN can appear after the
     * first reference.  Deferring avoids emitting EXTRN for symbols that are
     * later defined in the same translation unit (e.g. an extern declaration
     * in a header followed by a definition in an #included .c file), which
     * would otherwise cause an EXTRN/PUBLIC conflict in M80.
     */
    for (i = 0; i < nused_extrns; ++i)
        if (used_extrns[i] == s)
            return;

    if (nused_extrns >= MAX_USED_EXTRNS)
        fatal("too many used externs");

    used_extrns[nused_extrns++] = s;
}

void emit_deferred_extrns(void)
{
    int i;

    for (i = 0; i < nused_extrns; ++i) {
        struct Sym *s;
        s = used_extrns[i];
        if (s && s->needs_extrn && !s->is_defined && !asm_name_is_internal_public(s->name))
            fprintf(g_emit_sink.stream, "\textrn %s\n", asm_name_for(sym_asm_name(s)));
    }
}


int sym_can_ix_direct(struct Sym *s)
{
    int sz;
    if (!s) return 0;
    if (s->storage != SC_LOCAL && s->storage != SC_PARAM) return 0;
    if (s->is_array) return 0;
    sz = type_size(s->type);
    if (sz < 1) sz = 1;
    if (s->offset < -128 || s->offset + sz - 1 > 127) return 0;
    return 1;
}

/* True for global/extern 16-bit non-array variables that support direct word load/store. */
int is_global_word_sym(struct Sym *s)
{
    if (!s) return 0;
    if (s->storage != SC_GLOBAL && s->storage != SC_EXTERN) return 0;
    if (s->is_array) return 0;
    return type_size(s->type) == 2;
}


int base_struct_id_from_type(int type)
{
    if (type & TYPE_STRUCT)
        return type_struct_id(type);
    return 0;
}

void skip_balanced_bracket(int open_ch, int close_ch)
{
    int depth;

    depth = 1;
    next_token();

    while (g_lex.tok.kind != TOK_EOF && depth > 0) {
        if (g_lex.tok.kind == open_ch) {
            depth++;
        } else if (g_lex.tok.kind == close_ch) {
            depth--;
        }

        next_token();
    }
}

int parse_offsetof_value(void)
{
    int t;
    int sz;
    int sid;
    int off;
    struct FieldDef *fd;

    if (g_lex.tok.kind != TOK_ID || strcmp(g_lex.tok.text, "__offsetof") != 0) {
        error_here("__offsetof expected");
        return 0;
    }
    next_token();
    expect('(');
    parse_type_name_decl(&t, &sz);
    (void)sz;
    expect(',');

    sid = base_struct_id_from_type(t);
    if (sid <= 0) {
        error_here("offsetof needs struct/union type");
        sid = 0;
    }

    off = 0;
    for (;;) {
        if (g_lex.tok.kind != TOK_ID) {
            error_here("field name expected in offsetof");
            while (g_lex.tok.kind != TOK_EOF && g_lex.tok.kind != ')')
                next_token();
            break;
        }

        fd = find_field_def(sid, g_lex.tok.text);
        if (!fd) {
            error_here("unknown field in offsetof");
            next_token();
            break;
        }
        next_token();

        off += fd->offset;
        t = fd->is_array ? fd->elem_type : fd->type;

        while (g_lex.tok.kind == '[') {
            int idx;
            int elem;
            next_token();
            idx = parse_typed_const_int_expr();
            expect(']');
            elem = fd->is_array ? fd->elem_size : type_size(t);
            if (elem <= 0)
                elem = 1;
            off += idx * elem;
            t = fd->elem_type ? fd->elem_type : t;
        }

        if (g_lex.tok.kind != '.')
            break;
        next_token();
        sid = base_struct_id_from_type(t);
        if (sid <= 0) {
            error_here("nested offsetof field is not struct/union");
            while (g_lex.tok.kind != TOK_EOF && g_lex.tok.kind != ')')
                next_token();
            break;
        }
    }

    expect(')');
    return off;
}

int parse_sizeof_expr_operand(void);

int sizeof_common_type(int a, int b, int op)
{
    int au;
    int bu;

    /* Comparisons and logical operators have int result. */
    if (op == TOK_EQ || op == TOK_NE || op == TOK_LE || op == TOK_GE ||
        op == '<' || op == '>' || op == TOK_ANDAND || op == TOK_OROR)
        return TYPE_INT;

    if (type_is_long(a) || type_is_long(b)) {
        if ((a & TYPE_UNSIGNED) || (b & TYPE_UNSIGNED))
            return TYPE_LONG | TYPE_UNSIGNED;
        return TYPE_LONG;
    }

    au = a & TYPE_UNSIGNED;
    bu = b & TYPE_UNSIGNED;
    if (au || bu)
        return TYPE_INT | TYPE_UNSIGNED;

    return TYPE_INT;
}

int sizeof_parse_primary_type(int *typep, int *sizep)
{
    struct Sym *s;
    struct FieldDef *fd;
    int type;
    int sz;
    int is_arr;
    int elem_size;
    int sid;
    int i;

    if (g_lex.tok.kind == '*') {
        next_token();
        if (!sizeof_parse_primary_type(&type, &sz)) {
            *typep = TYPE_INT;
            *sizep = 2;
            return 0;
        }
        type = type_decay_ptr(type);
        sz = type_size(type);
        if (sz <= 0) sz = 1;
        *typep = type;
        *sizep = sz;
        return 1;
    }

    if (g_lex.tok.kind == '&') {
        next_token();
        if (!sizeof_parse_primary_type(&type, &sz)) {
            *typep = TYPE_INT | TYPE_PTR;
            *sizep = 2;
            return 0;
        }
        *typep = type_add_ptr(type);
        *sizep = 2;
        return 1;
    }

    if (g_lex.tok.kind == TOK_NUM) {
        type = g_tok_long_suffix ? TYPE_LONG : TYPE_INT;
        if (g_tok_unsigned_suffix)
            type |= TYPE_UNSIGNED;
        next_token();
        *typep = type;
        *sizep = type_size(type);
        return 1;
    }

    if (g_lex.tok.kind == TOK_CHARLIT) {
        next_token();
        *typep = TYPE_INT;
        *sizep = 2;
        return 1;
    }

    if (g_lex.tok.kind == TOK_STR || g_lex.tok.kind == TOK_WSTR) {
        char *lit;
        int is_wide;
        int litlen;
        lit = read_adjacent_string_literals_ex(&is_wide, &litlen);
        sz = litlen + 1;
        if (is_wide)
            sz *= 2;
        free(lit);
        *typep = (is_wide ? TYPE_INT : TYPE_CHAR) | TYPE_PTR;
        *sizep = sz;
        return 1;
    }

    if (g_lex.tok.kind == '(') {
        next_token();
        if (starts_type()) {
            parse_type_name_decl(&type, &sz);
            expect(')');
            *typep = type;
            *sizep = sz;
            return 1;
        }

        sz = parse_sizeof_expr_operand();
        expect(')');
        *typep = TYPE_INT;
        *sizep = sz;
        return 1;
    }

    if (g_lex.tok.kind != TOK_ID) {
        error_here("unsupported sizeof expression");
        *typep = TYPE_INT;
        *sizep = 2;
        return 0;
    }

    s = find_sym(g_lex.tok.text);
    if (!s) {
        /* enum constants behave like int; unknown identifiers are diagnosed. */
        for (i = 0; i < nenum_consts; ++i) {
            if (!strcmp(enum_const_names[i], g_lex.tok.text)) {
                next_token();
                *typep = TYPE_INT;
                *sizep = 2;
                return 1;
            }
        }
        {
            char msg[MAX_TOK_TEXT + 64];
            sprintf(msg, "use of undeclared identifier '%s'", g_lex.tok.text);
            error_here(msg);
        }
        next_token();
        *typep = TYPE_INT;
        *sizep = 2;
        return 0;
    }

    type = s->type;
    is_arr = s->is_array;
    sz = is_arr ? s->size : type_size(type);
    elem_size = s->elem_size ? s->elem_size : type_size(type);
    if (elem_size <= 0) elem_size = 1;
    {
        /* sizeof of a whole VLA needs its runtime byte size, which is not a
         * compile-time constant; reject rather than silently use the pointer
         * slot size.  Indexing/field access below reduces to a constant-size
         * subobject, so only the bare VLA operand is diagnosed (checked after
         * the postfix loop via vla_whole). */
        int vla_whole = s->is_vla;
        next_token();

        for (;;) {
            if (g_lex.tok.kind == '[') {
                skip_balanced_bracket('[', ']');
                vla_whole = 0;
                if (is_arr) {
                    sz = elem_size;
                    is_arr = 0;
                } else {
                    type = type_decay_ptr(type);
                    sz = type_size(type);
                    if (sz <= 0) sz = 1;
                }
            } else if (g_lex.tok.kind == '(') {
                /* Function call expression: sizeof uses the function return
                 * type.  Arguments are not evaluated; just skip the list. */
                skip_balanced_bracket('(', ')');
                is_arr = 0;
                vla_whole = 0;
                sz = type_size(type);
                if (sz <= 0) sz = 2;
            } else if (g_lex.tok.kind == '.' || g_lex.tok.kind == TOK_ARROW) {
                int arrow;

                arrow = g_lex.tok.kind == TOK_ARROW;
                vla_whole = 0;
                next_token();

                if (g_lex.tok.kind != TOK_ID) {
                    error_here("field name expected");
                    break;
                }

                if (arrow)
                    sid = base_struct_id_from_type(type_decay_ptr(type));
                else
                    sid = base_struct_id_from_type(type);

                fd = find_field_def(sid, g_lex.tok.text);
                if (!fd) {
                    error_here("unknown struct field");
                    next_token();
                    break;
                }

                next_token();

                type = fd->is_array ? fd->elem_type : fd->type;
                sz = fd->is_array ? fd->size : fd->size;
                is_arr = fd->is_array;
                elem_size = fd->elem_size ? fd->elem_size : type_size(type);
                if (elem_size <= 0) elem_size = 1;
            } else {
                break;
            }
        }

        if (vla_whole)
            error_here("sizeof applied to a variable-length array is not supported");
    }

    *typep = type;
    *sizep = sz;
    return 1;
}
