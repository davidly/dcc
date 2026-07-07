/*
 * dcc_symbols.c - symbol tables and symbol-access code generation.
 *
 * Lookup/allocation of locals, parameters and globals; the string-literal
 * pool; EXTRN bookkeeping (emit_extrn_if_needed/emit_deferred_extrns); and the
 * code that loads/stores a symbol's address or value (frame-relative or direct
 * for globals), including post-increment/decrement fast paths.
 *
 * MODULE: compiled as its own translation unit; shared declarations are in dcc.h.
 * Source provenance: monolith src/ddc.c lines 3829-4801.
 */

#include "dcc.h"

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
    for (k = g_forren_n - 1; k >= 0; --k) {
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

    if (g_for_decl_seq < 0)
        fatal("bad for-init scope");

    n = g_for_decl_rename_index;
    if (g_for_decl_recording) {
        add_for_scope_rename(g_for_decl_seq, name);
    } else {
        if (g_for_decl_seq >= MAX_FOR_SCOPES)
            fatal("too many for statements");
        if (n >= g_for_rename_count[g_for_decl_seq])
            fatal("for-init scope mismatch");
    }

    push_for_rename(g_for_rename_from[g_for_decl_seq][n],
                    g_for_rename_to[g_for_decl_seq][n]);
    g_for_decl_rename_index = n + 1;
    return g_for_rename_to[g_for_decl_seq][n];
}

void push_for_rename(const char *from, const char *to)
{
    if (g_forren_n >= MAX_FORREN)
        fatal("too many nested for-init scopes");
    strncpy(g_forren_from[g_forren_n], from, 63);
    g_forren_from[g_forren_n][63] = 0;
    strncpy(g_forren_to[g_forren_n], to, 63);
    g_forren_to[g_forren_n][63] = 0;
    g_forren_n++;
}

void pop_for_rename(void)
{
    if (g_forren_n > 0)
        g_forren_n--;
}

const char *sym_asm_name(struct Sym *s)
{
    if (s && s->link_name[0])
        return s->link_name;
    return s ? s->name : "";
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
    if (g_scope_depth >= MAX_SCOPE_DEPTH)
        fatal("too many nested block scopes");
    g_scope_watermark[g_scope_depth++] = nlocals;
    /* A freshly opened scope has no VLA save slot yet. */
    if (g_scope_depth < MAX_SCOPE_DEPTH)
        g_vla_scope_off[g_scope_depth] = 0;
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

    if (g_scope_depth < 0 || g_scope_depth >= MAX_SCOPE_DEPTH)
        return 0;
    if (g_vla_scope_off[g_scope_depth] != 0)
        return 0;                       /* already allocated for this scope */
    s = add_local_alloc("#vlasp", TYPE_INT, 2);
    g_vla_scope_off[g_scope_depth] = s->offset;
    return s->offset;
}

int vla_active_scope_depth(void)
{
    int d;
    for (d = 1; d <= g_scope_depth && d < MAX_SCOPE_DEPTH; ++d)
        if (g_vla_scope_off[d] != 0)
            return d;
    return 0;
}

/* HL-free helper: save the current SP into the frame slot at `off`. */
void emit_vla_save_sp(int off)
{
    emit("\tld hl,0\n\tadd hl,sp\n");   /* HL = SP */
    emit("\tpush hl\n");                /* stash SP value */
    emit("\tpush ix\n\tpop hl\n");      /* HL = IX */
    fprintf(outf, "\tld de,%d\n\tadd hl,de\n", off);
    emit("\tpop de\n");                 /* DE = SP value */
    emit("\tld (hl),e\n\tinc hl\n\tld (hl),d\n");
}

/* Restore SP from the frame slot at `off`, reclaiming that scope's VLAs. */
void emit_vla_restore_sp(int off)
{
    emit("\tpush ix\n\tpop hl\n");      /* HL = IX */
    fprintf(outf, "\tld de,%d\n\tadd hl,de\n", off);
    emit("\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n");  /* HL = saved SP */
    emit("\tld sp,hl\n");
}

/*
 * Reclaim VLAs when leaving a loop/switch via break or continue.  Restore SP to
 * the outermost active VLA save slot among the scopes being exited (depths
 * greater than floor_depth); restoring an outer slot reclaims every inner
 * scope's VLAs, so only one restore is needed.  Emits nothing when no VLA was
 * declared inside the loop.
 */
void emit_vla_restore_for_flow(int floor_depth)
{
    int d;
    for (d = floor_depth + 1; d <= g_scope_depth && d < MAX_SCOPE_DEPTH; ++d) {
        if (g_vla_scope_off[d] != 0) {
            emit_vla_restore_sp(g_vla_scope_off[d]);
            return;
        }
    }
}

/*
 * Record a forward goto issued from within a VLA scope.  Single-pass codegen
 * cannot yet know whether the (not-yet-emitted) target label sits inside the
 * same VLA scopes, an enclosing one, or outside them all, so the exact SP to
 * restore is unknown here.  Snapshot the goto's active VLA save-slot offsets
 * and hand back a fresh stub label id; the caller jumps there, and
 * vla_resolve_fwd_gotos() later emits the stub once the label's scope is known.
 * Returns the stub label id (a fresh label even on overflow so the emitted jump
 * is still well-formed).
 */
int vla_record_fwd_goto(int label_index, int line)
{
    struct VlaFwdGoto *g;
    int fixup;
    int d, i, same;

    /* Reuse an existing pending stub when another goto already targets this
     * label with the identical active-VLA snapshot: same target and same
     * scope offsets mean identical reclaim, so one shared stub suffices (and
     * a fresh label is not consumed). */
    for (i = 0; i < g_vla_fwd_ngoto; ++i) {
        g = &g_vla_fwd_gotos[i];
        if (g->label_index != label_index || g->snap_depth != g_scope_depth)
            continue;
        same = 1;
        for (d = 0; d < MAX_SCOPE_DEPTH; ++d) {
            int off = (d <= g_scope_depth) ? g_vla_scope_off[d] : 0;
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
    g->snap_depth = g_scope_depth;
    for (d = 0; d < MAX_SCOPE_DEPTH; ++d)
        g->snap_off[d] = (d <= g_scope_depth) ? g_vla_scope_off[d] : 0;
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
    for (d = 1; d <= g_scope_depth && d < MAX_SCOPE_DEPTH; ++d)
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
    ulabel_vla_snap_depth[label_index] = g_scope_depth;
    for (d = 0; d < MAX_SCOPE_DEPTH; ++d)
        ulabel_vla_snap_off[label_index][d] = (d <= g_scope_depth) ? g_vla_scope_off[d] : 0;
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

void emit_vla_restore_to_label_scope(int label_index)
{
    int d;
    if (label_index < 0 || label_index >= MAX_USER_LABELS)
        return;
    for (d = 1; d <= g_scope_depth && d < MAX_SCOPE_DEPTH; ++d) {
        int off = g_vla_scope_off[d];
        if (off != 0 && !vla_off_in_label_scope(label_index, off)) {
            emit_vla_restore_sp(off);
            return;
        }
    }
}

/*
 * Emit the deferred SP-fixup stubs for every forward goto that targeted this
 * label from inside a VLA scope, now that the label's own scope is known.  For
 * each such goto: verify the C99 constraint that the jump does not enter the
 * scope of a VLA (every VLA scope still active at the label must also have been
 * active at the goto), then restore SP to reclaim exactly the VLA scopes the
 * goto is leaving before jumping to the real label.  Emits nothing when no
 * forward goto targeted this label.  Must be called just before the real label
 * is emitted; `real_id` is that label's id.
 */
void vla_resolve_fwd_gotos(int label_index, int real_id)
{
    int i, d, k;
    int any;
    int bad;

    any = 0;
    for (i = 0; i < g_vla_fwd_ngoto; ++i)
        if (g_vla_fwd_gotos[i].label_index == label_index) { any = 1; break; }
    if (!any)
        return;

    /* Fall-through from the preceding statement must land on the real label,
     * not run into the stubs that sit just above it. */
    emit_jp_label("jp", real_id);

    for (i = 0; i < g_vla_fwd_ngoto; ++i) {
        struct VlaFwdGoto *g = &g_vla_fwd_gotos[i];
        if (g->label_index != label_index)
            continue;

        /* Jump-into check: each VLA scope active at the label must be one that
         * was also active at the goto (same frame slot). */
        bad = 0;
        for (d = 1; d <= g_scope_depth && d < MAX_SCOPE_DEPTH; ++d) {
            int off = g_vla_scope_off[d];
            int seen = 0;
            if (off == 0)
                continue;
            for (k = 1; k <= g->snap_depth && k < MAX_SCOPE_DEPTH; ++k)
                if (g->snap_off[k] == off) { seen = 1; break; }
            if (!seen) {
                dcc_error_at(tok.file[0] ? tok.file :
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

        emit_label(g->fixup_id);
        /* Reclaim the goto's inner VLA scopes the label is not within: restore
         * the outermost such slot (which reclaims it and every deeper scope). */
        for (k = 1; k <= g->snap_depth && k < MAX_SCOPE_DEPTH; ++k) {
            int off = g->snap_off[k];
            if (off != 0 && !vla_off_active_now(off)) {
                emit_vla_restore_sp(off);
                break;
            }
        }
        emit_jp_label("jp", real_id);
    }
}

void leave_scope(void)
{
    if (g_scope_depth <= 0)
        return;
    /* Block-local names leave scope.  local_size is intentionally left alone:
     * storage is monotonic (slots are never reused), so the frame size still
     * equals the sum over every scope. */
    nlocals = g_scope_watermark[--g_scope_depth];
}

/* Lookup used while DECLARING a local: only the innermost open block is
 * considered, and for-init renames are ignored.  This lets an inner block (or a
 * for body) declare a name that shadows an outer local, a parameter, or an
 * active for-init loop variable instead of binding to it. */
struct Sym *find_local_decl(const char *name)
{
    int i, base;
    base = g_scope_depth > 0 ? g_scope_watermark[g_scope_depth - 1] : 0;
    for (i = nlocals - 1; i >= base; --i)
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
    for (i = nlocals - 1; i >= 0; --i)
        if (!strcmp(locals[i].name, name)) { plain_idx = i; break; }

    ren_idx = -1;
    rn = resolve_local_rename(name);
    if (rn != name) {
        for (i = nlocals - 1; i >= 0; --i)
            if (!strcmp(locals[i].name, rn)) { ren_idx = i; break; }
    }

    if (ren_idx > plain_idx) return &locals[ren_idx];
    if (plain_idx >= 0) return &locals[plain_idx];
    return NULL;
}

struct Sym *find_global(const char *name)
{
    int i;
    for (i = nglobals - 1; i >= 0; --i)
        if (!strcmp(globals[i].name, name)) return &globals[i];
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

void emit_global_char_index_addr(struct Sym *s)
{
    emit_extrn_if_needed(s);
    fprintf(outf, "\tld de,%s\n", asm_name_for(sym_asm_name(s)));
    emit("\tadd hl,de\n");
}


void emit_test_global_char_index_zero(struct Sym *s, int false_label)
{
    emit_global_char_index_addr(s);
    emit("\tld a,(hl)\n");
    emit("\tor a\n");
    emit_jp_label("jp z,", false_label);
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
    strncpy(s->name, name, sizeof(s->name) - 1);
    s->type = type;
    s->storage = storage;
    s->size = type_size(type);
    return s;
}

struct Sym *add_local_known(const char *name, int type, int storage,
                                   int offset, int bytes)
{
    struct Sym *s;

    if (nlocals >= MAX_LOCALS) fatal("too many locals");

    s = &locals[nlocals++];
    memset(s, 0, sizeof(*s));
    strncpy(s->name, name, sizeof(s->name) - 1);
    s->type = type;
    s->storage = storage;
    s->offset = offset;
    s->size = bytes;
    return s;
}

struct Sym *add_local_alloc(const char *name, int type, int bytes)
{
    struct Sym *s;
    local_size += bytes;
    s = add_local_known(name, type, SC_LOCAL, -local_size, bytes);
    return s;
}

struct Sym *add_compound_literal_local(int type)
{
    char name[64];
    int bytes;

    bytes = type_size(type);
    if (bytes <= 0)
        bytes = 2;

    sprintf(name, "#clit%d", g_compound_literal_seq++);
    return add_local_alloc(name, type, bytes);
}

struct Sym *add_param_alloc(const char *name, int type)
{
    struct Sym *s;
    int sz = type_size(type);
    if (sz < 2) sz = 2;
    s = add_local_known(name, type, SC_PARAM, param_offset, sz);
    param_offset += sz;
    return s;
}

int add_string_ex(const char *s, int is_wide)
{
    int i;
    is_wide = is_wide ? 1 : 0;
    for (i = 0; i < nstrings; i++)
        if (string_wide[i] == is_wide && strcmp(strings[i], s) == 0)
            return i;
    if (nstrings >= MAX_STRINGS) fatal("too many strings");
    strings[nstrings] = xstrdup2(s);
    string_wide[nstrings] = is_wide;
    return nstrings++;
}

char *read_adjacent_string_literals_ex(int *is_widep)
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

    while (tok.kind == TOK_STR || tok.kind == TOK_WSTR) {
        int slen;
        if (tok.kind == TOK_WSTR)
            is_wide = 1;

        slen = (int)strlen(tok.text);
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
        memcpy(buf + len, tok.text, (size_t)slen);
        len += slen;
        buf[len] = 0;
        next_token();
    }

    if (is_widep)
        *is_widep = is_wide;
    return buf;
}

/* If an extern symbol had its extrn deferred, emit it now (once). */
void emit_extrn_if_needed(struct Sym *s)
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
            fprintf(outf, "\textrn %s\n", asm_name_for(sym_asm_name(s)));
    }
}


void emit_runtime_extrn_if_needed(const char *name)
{
    static const char *emitted[64];
    static int nemitted;
    int i;

    /*
     * During a suppressed scan/replay (scan_mode), do not emit the EXTRN and,
     * crucially, do not record it as emitted.  Otherwise a runtime helper first
     * "used" inside a suppressed gate replay would be marked emitted while its
     * EXTRN line went nowhere, so the real emission would skip it and leave the
     * helper undefined at link time.
     */
    if (scan_mode)
        return;

    if (g_inline_body_buffering) {
        fprintf(outf, "\textrn %s\n", name);
        return;
    }

    for (i = 0; i < nemitted; ++i) {
        if (!strcmp(emitted[i], name))
            return;
    }

    if (nemitted >= 64)
        fatal("too many runtime extrns");

    fprintf(outf, "\textrn %s\n", name);
    emitted[nemitted++] = name;
}

void emit_runtime_call(const char *name)
{
    emit_runtime_extrn_if_needed(name);
    if (!scan_mode)
        fprintf(outf, "\tcall %s\n", name);
}


int frame_sp_offset_for_sym(struct Sym *s)
{
    /* With normal frame, first parameter is ix+4.  With no IX frame,
     * SP still points at the return address, so the same parameter is sp+2. */
    return s->offset - 2;
}

void emit_load_frame_addr_hl(struct Sym *s)
{
    int n;
    if (current_omit_ix_frame && s->storage == SC_PARAM) {
        n = frame_sp_offset_for_sym(s);
        if (n == 0) {
            emit("\tpush sp\n\tpop hl\n");
        } else {
            fprintf(outf, "\tld hl,%d\n", n);
            emit("\tadd hl,sp\n");
        }
    } else {
        emit("\tpush ix\n");
        emit("\tpop hl\n");
        if (s->offset > 0 && s->offset <= 3) {
            for (n = 0; n < s->offset; ++n) emit("\tinc hl\n");
        } else if (s->offset < 0 && s->offset >= -3) {
            for (n = 0; n < -s->offset; ++n) emit("\tdec hl\n");
        } else if (s->offset != 0) {
            fprintf(outf, "\tld de,%d\n", s->offset);
            emit("\tadd hl,de\n");
        }
    }
}

void emit_load_sym_addr(struct Sym *s)
{
    if (s->is_vla) {
        /* A VLA's storage is allocated at run time below SP; its frame slot
         * holds a pointer to that block.  The array decays to that pointer
         * value, so load the slot contents rather than the slot's address. */
        emit_load_frame_addr_hl(s);
        emit("\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n");
        return;
    }
    if (s->storage == SC_LOCAL || s->storage == SC_PARAM) {
        emit_load_frame_addr_hl(s);
    } else {
        emit_extrn_if_needed(s);
        fprintf(outf, "\tld hl,%s\n", asm_name_for(sym_asm_name(s)));
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

/* Z80: ld hl,(name) — load 16-bit value from global/extern directly. */
void emit_load_global_word_direct(struct Sym *s)
{
    emit_extrn_if_needed(s);
    fprintf(outf, "\tld hl,(%s)\n", asm_name_for(sym_asm_name(s)));
}

/* Z80: ld (name),hl — store 16-bit HL value to global/extern directly. */
void emit_store_global_word_direct(struct Sym *s)
{
    emit_extrn_if_needed(s);
    fprintf(outf, "\tld (%s),hl\n", asm_name_for(sym_asm_name(s)));
}

void emit_load_sym_value_direct(struct Sym *s)
{
    if (is_global_word_sym(s)) {
        emit_load_global_word_direct(s);
        return;
    }
    if (current_omit_ix_frame && s->storage == SC_PARAM) {
        if (type_size(s->type) == 1) {
            emit_load_frame_addr_hl(s);
            emit("\tld l,(hl)\n");
            if ((s->type & TYPE_UNSIGNED) || type_is_bool(s->type))
                emit("\tld h,0\n");
            else
                emit("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n");
            if (type_is_bool(s->type))
                emit_bool_normalize_hl(s->type);
        } else if (type_size(s->type) == 4) {
            emit_load_frame_addr_hl(s);
            emit("\tld a,(hl)\n\tld l,a\n\tinc hl\n\tld a,(hl)\n\tld h,a\n");
            emit("\tpush hl\n");
            emit_load_frame_addr_hl(s);
            emit("\tinc hl\n\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpop hl\n");
        } else {
            emit_load_frame_addr_hl(s);
            emit("\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n");
        }
        return;
    }
    if (type_size(s->type) == 1) {
        fprintf(outf, "\tld l,(ix%+d)\n", s->offset);
        if ((s->type & TYPE_UNSIGNED) || type_is_bool(s->type))
            emit("\tld h,0\n");
        else
            emit("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n");
        if (type_is_bool(s->type) && s->storage == SC_PARAM)
            emit_bool_normalize_hl(s->type);
    } else if (type_size(s->type) == 4) {
        fprintf(outf, "\tld l,(ix%+d)\n", s->offset);
        fprintf(outf, "\tld h,(ix%+d)\n", s->offset + 1);
        fprintf(outf, "\tld e,(ix%+d)\n", s->offset + 2);
        fprintf(outf, "\tld d,(ix%+d)\n", s->offset + 3);
    } else {
        fprintf(outf, "\tld l,(ix%+d)\n", s->offset);
        fprintf(outf, "\tld h,(ix%+d)\n", s->offset + 1);
    }
}

void emit_load_sym_de_direct(struct Sym *s)
{
    if (current_omit_ix_frame && s->storage == SC_PARAM) {
        if (type_size(s->type) == 1) {
            emit_load_frame_addr_hl(s);
            emit("\tld e,(hl)\n");
            if ((s->type & TYPE_UNSIGNED) || type_is_bool(s->type))
                emit("\tld d,0\n");
            else
                emit("\tld a,e\n\trlca\n\tsbc a,a\n\tld d,a\n");
            if (type_is_bool(s->type))
                emit("\tld a,e\n\tor a\n\tld e,0\n\tjr z,$+3\n\tinc e\n\tld d,0\n");
        } else {
            emit_load_frame_addr_hl(s);
            emit("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n");
        }
        return;
    }
    if (type_size(s->type) == 1) {
        fprintf(outf, "\tld e,(ix%+d)\n", s->offset);
        if ((s->type & TYPE_UNSIGNED) || type_is_bool(s->type))
            emit("\tld d,0\n");
        else
            emit("\tld a,e\n\trlca\n\tsbc a,a\n\tld d,a\n");
        if (type_is_bool(s->type) && s->storage == SC_PARAM)
            emit("\tld a,e\n\tor a\n\tld e,0\n\tjr z,$+3\n\tinc e\n\tld d,0\n");
    } else {
        fprintf(outf, "\tld e,(ix%+d)\n", s->offset);
        fprintf(outf, "\tld d,(ix%+d)\n", s->offset + 1);
    }
}

void emit_store_hl_to_sym_direct(struct Sym *s)
{
    if (is_global_word_sym(s)) {
        emit_store_global_word_direct(s);
        return;
    }
    if (current_omit_ix_frame && s->storage == SC_PARAM) {
        if (type_size(s->type) == 1) {
            if (type_is_bool(s->type))
                emit_bool_normalize_hl(s->type);
            emit("\tld e,l\n");
            emit_load_frame_addr_hl(s);
            emit("\tld (hl),e\n");
        } else if (type_size(s->type) == 4) {
            emit("\tpush de\n\tex de,hl\n");
            emit_load_frame_addr_hl(s);
            emit("\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tinc hl\n\tpop de\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n");
        } else {
            emit("\tex de,hl\n");
            emit_load_frame_addr_hl(s);
            emit("\tld (hl),e\n\tinc hl\n\tld (hl),d\n");
        }
        return;
    }
    if (type_size(s->type) == 1) {
        if (type_is_bool(s->type))
            emit_bool_normalize_hl(s->type);
        fprintf(outf, "\tld (ix%+d),l\n", s->offset);
    } else if (type_size(s->type) == 4) {
        fprintf(outf, "\tld (ix%+d),l\n", s->offset);
        fprintf(outf, "\tld (ix%+d),h\n", s->offset + 1);
        fprintf(outf, "\tld (ix%+d),e\n", s->offset + 2);
        fprintf(outf, "\tld (ix%+d),d\n", s->offset + 3);
    } else {
        fprintf(outf, "\tld (ix%+d),l\n", s->offset);
        fprintf(outf, "\tld (ix%+d),h\n", s->offset + 1);
    }
}


int try_emit_post_update_sym_direct(struct Sym *s, int op)
{
    int elem;

    if (!s || s->is_array)
        return 0;
    if (type_is_long(s->type) || type_is_float(s->type))
        return 0;
    if (type_size(s->type) > 2)
        return 0;
    if (!sym_can_ix_direct(s) && !is_global_word_sym(s))
        return 0;

    emit_load_sym_value_direct(s);     /* HL = old value, expression result */
    emit("\tpush hl\n");              /* save old value for result */

    if (type_ptr_depth(s->type) > 0) {
        elem = type_index_elem_size(s->type);
        if (op == TOK_INC) {
            emit_add_const_to_hl(elem);
        } else {
            emit_ld_de_const(elem);
            emit("\tor a\n\tsbc hl,de\n");
        }
    } else {
        if (op == TOK_INC)
            emit("\tinc hl\n");
        else
            emit("\tdec hl\n");
    }

    emit_store_hl_to_sym_direct(s);    /* store new value */
    emit("\tpop hl\n");               /* return old value */
    g_expr_type = s->type;
    return 1;
}

void emit_incdec_sym_direct(struct Sym *s, int op)
{
    int done;

    /* Global 16-bit integer (non-pointer): ld hl,(nn); inc/dec hl; ld (nn),hl.
     * inc hl / dec hl are atomic 16-bit ops so no byte-by-byte ripple needed. */
    if (is_global_word_sym(s) && type_ptr_depth(s->type) == 0) {
        emit_load_global_word_direct(s);
        if (op == TOK_INC) emit("\tinc hl\n");
        else               emit("\tdec hl\n");
        emit_store_global_word_direct(s);
        return;
    }

    /* Pointer ++/-- advances by the pointed-to object size, not by one
     * byte. Global pointer case now handled via emit_load_sym_value_direct +
     * emit_store_hl_to_sym_direct which both support globals. */
    if (s && type_ptr_depth(s->type) > 0) {
        int elem;
        elem = type_index_elem_size(s->type);
        emit_load_sym_value_direct(s);
        if (op == TOK_INC) {
            emit_add_const_to_hl(elem);
        } else {
            emit_ld_de_const(elem);
            emit("\tor a\n\tsbc hl,de\n");
        }
        emit_store_hl_to_sym_direct(s);
        return;
    }

    if (type_size(s->type) == 1) {
        if (op == TOK_INC)
            fprintf(outf, "\tinc (ix%+d)\n", s->offset);
        else
            fprintf(outf, "\tdec (ix%+d)\n", s->offset);
        return;
    }

    done = new_label();

    if (type_size(s->type) == 4) {
        /*
         * 4-byte IX-direct long ++/--.  The previous fast path always used
         * the 2-byte sequence, so a long decrement 0 -> -1 produced
         * 0000FFFF instead of FFFFFFFF.  That made loops like:
         *
         *     for (i = BLOCKS - 1; i >= 0; i--)
         *
         * continue once more with i's low word equal to -1.
         */
        if (op == TOK_INC) {
            fprintf(outf, "\tinc (ix%+d)\n", s->offset);
            emit_jp_label("jp nz,", done);
            fprintf(outf, "\tinc (ix%+d)\n", s->offset + 1);
            emit_jp_label("jp nz,", done);
            fprintf(outf, "\tinc (ix%+d)\n", s->offset + 2);
            emit_jp_label("jp nz,", done);
            fprintf(outf, "\tinc (ix%+d)\n", s->offset + 3);
        } else {
            fprintf(outf, "\tld a,(ix%+d)\n", s->offset);
            fprintf(outf, "\tdec (ix%+d)\n", s->offset);
            emit("\tor a\n");
            emit_jp_label("jp nz,", done);
            fprintf(outf, "\tld a,(ix%+d)\n", s->offset + 1);
            fprintf(outf, "\tdec (ix%+d)\n", s->offset + 1);
            emit("\tor a\n");
            emit_jp_label("jp nz,", done);
            fprintf(outf, "\tld a,(ix%+d)\n", s->offset + 2);
            fprintf(outf, "\tdec (ix%+d)\n", s->offset + 2);
            emit("\tor a\n");
            emit_jp_label("jp nz,", done);
            fprintf(outf, "\tdec (ix%+d)\n", s->offset + 3);
        }
    } else {
        /* 2-byte int ++/--. */
        if (op == TOK_INC) {
            fprintf(outf, "\tinc (ix%+d)\n", s->offset);
            emit_jp_label("jp nz,", done);
            fprintf(outf, "\tinc (ix%+d)\n", s->offset + 1);
        } else {
            fprintf(outf, "\tld a,(ix%+d)\n", s->offset);
            fprintf(outf, "\tdec (ix%+d)\n", s->offset);
            emit("\tor a\n");
            emit_jp_label("jp nz,", done);
            fprintf(outf, "\tdec (ix%+d)\n", s->offset + 1);
        }
    }

    emit_label(done);
}



void emit_load_from_hl(int type);
void emit_promote_byte_to_int(int actual_type);
void emit_extend_to_long_typed(int source_type);
void emit_extend_to_long(int source_is_unsigned);

int base_struct_id_from_type(int type)
{
    if (type & TYPE_STRUCT)
        return type_struct_id(type);
    return 0;
}

void emit_add_field_offset(struct FieldDef *fd)
{
    int i;
    /* inc hl is 1 byte; ld de,N + add hl,de is 4 bytes regardless of N */
    if (fd->offset >= 1 && fd->offset <= 3) {
        for (i = 0; i < fd->offset; i++)
            emit("\tinc hl\n");
    } else if (fd->offset) {
        fprintf(outf, "\tld de,%d\n", fd->offset);
        emit("\tadd hl,de\n");
    }
}

void skip_balanced_bracket(int open_ch, int close_ch)
{
    int depth;

    depth = 1;
    next_token();

    while (tok.kind != TOK_EOF && depth > 0) {
        if (tok.kind == open_ch) {
            depth++;
        } else if (tok.kind == close_ch) {
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

    if (tok.kind != TOK_ID || strcmp(tok.text, "__offsetof") != 0) {
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
        if (tok.kind != TOK_ID) {
            error_here("field name expected in offsetof");
            while (tok.kind != TOK_EOF && tok.kind != ')')
                next_token();
            break;
        }

        fd = find_field_def(sid, tok.text);
        if (!fd) {
            error_here("unknown field in offsetof");
            next_token();
            break;
        }
        next_token();

        off += fd->offset;
        t = fd->is_array ? fd->elem_type : fd->type;

        while (tok.kind == '[') {
            int idx;
            int elem;
            next_token();
            idx = parse_const_int_expr();
            expect(']');
            elem = fd->is_array ? fd->elem_size : type_size(t);
            if (elem <= 0)
                elem = 1;
            off += idx * elem;
            t = fd->elem_type ? fd->elem_type : t;
        }

        if (tok.kind != '.')
            break;
        next_token();
        sid = base_struct_id_from_type(t);
        if (sid <= 0) {
            error_here("nested offsetof field is not struct/union");
            while (tok.kind != TOK_EOF && tok.kind != ')')
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

    if (tok.kind == '*') {
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

    if (tok.kind == '&') {
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

    if (tok.kind == TOK_NUM) {
        type = g_tok_long_suffix ? TYPE_LONG : TYPE_INT;
        if (g_tok_unsigned_suffix)
            type |= TYPE_UNSIGNED;
        next_token();
        *typep = type;
        *sizep = type_size(type);
        return 1;
    }

    if (tok.kind == TOK_CHARLIT) {
        next_token();
        *typep = TYPE_INT;
        *sizep = 2;
        return 1;
    }

    if (tok.kind == TOK_STR || tok.kind == TOK_WSTR) {
        char *lit;
        int is_wide;
        lit = read_adjacent_string_literals_ex(&is_wide);
        sz = (int)strlen(lit) + 1;
        if (is_wide)
            sz *= 2;
        free(lit);
        *typep = (is_wide ? TYPE_INT : TYPE_CHAR) | TYPE_PTR;
        *sizep = sz;
        return 1;
    }

    if (tok.kind == '(') {
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

    if (tok.kind != TOK_ID) {
        error_here("unsupported sizeof expression");
        *typep = TYPE_INT;
        *sizep = 2;
        return 0;
    }

    s = find_sym(tok.text);
    if (!s) {
        /* enum constants behave like int; unknown identifiers are diagnosed. */
        for (i = 0; i < nenum_consts; ++i) {
            if (!strcmp(enum_const_names[i], tok.text)) {
                next_token();
                *typep = TYPE_INT;
                *sizep = 2;
                return 1;
            }
        }
        {
            char msg[MAX_TOK_TEXT + 64];
            sprintf(msg, "use of undeclared identifier '%s'", tok.text);
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
            if (tok.kind == '[') {
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
            } else if (tok.kind == '(') {
                /* Function call expression: sizeof uses the function return
                 * type.  Arguments are not evaluated; just skip the list. */
                skip_balanced_bracket('(', ')');
                is_arr = 0;
                vla_whole = 0;
                sz = type_size(type);
                if (sz <= 0) sz = 2;
            } else if (tok.kind == '.' || tok.kind == TOK_ARROW) {
                int arrow;

                arrow = tok.kind == TOK_ARROW;
                vla_whole = 0;
                next_token();

                if (tok.kind != TOK_ID) {
                    error_here("field name expected");
                    break;
                }

                if (arrow)
                    sid = base_struct_id_from_type(type_decay_ptr(type));
                else
                    sid = base_struct_id_from_type(type);

                fd = find_field_def(sid, tok.text);
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

        if (vla_whole && asm_suppress_depth == 0)
            error_here("sizeof applied to a variable-length array is not supported");
    }

    *typep = type;
    *sizep = sz;
    return 1;
}

