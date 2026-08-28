/**
 * @file dcc_global_init.c
 * @brief Records file-scope object initializers for later data emission.
 *
 * @par Role
 * Parses scalar, array, struct/union, bitfield, string, compound-literal, and
 * designated initializers into each symbol's byte-and-relocation image,
 * including padding, overwrites, and inferred array bounds.
 *
 * @par Key entry points
 * parse_global_init_list(), parse_global_init_type(),
 * parse_global_init_array(), parse_global_init_struct(),
 * parse_global_init_atom(), and append_global_init().
 *
 * @par Boundary
 * dcc_data.c serializes the recorded image, while dcc_decl.c handles automatic
 * objects. This module records data and emits no function body.
 */
#include "dcc.h"

static void parse_global_init_type_at(struct Sym *s, int type, int size, int baseoff);
static void parse_global_init_array_at(struct Sym *s, int elem_type, int count, int elem_size, int baseoff);
static void parse_global_init_struct_at(struct Sym *s, int type, int baseoff);

static int global_compound_literal_seq;

static int parse_global_compound_literal_address(char *label, int labelsz)
{
    int type;
    int size;
    char name[64];
    struct Sym *lit;

    if (g_lex.tok.kind != '(' || !paren_starts_cast())
        return 0;

    next_token();
    parse_type_name_decl(&type, &size);
    expect(')');

    if (g_lex.tok.kind != '{') {
        error_here("compound literal initializer expected");
        return 1;
    }

    sprintf(name, "__clit%d", global_compound_literal_seq++);
    lit = add_global(name, type, SC_GLOBAL);
    lit->is_defined = 1;
    lit->is_static = 1;
    lit->needs_extrn = 0;
    lit->has_init = 1;
    lit->init_count = 0;
    lit->size = size;
    parse_global_init_type_at(lit, type, size, 0);

    if (label && labelsz > 0) {
        strncpy(label, name, labelsz - 1);
        label[labelsz - 1] = 0;
    }
    return 1;
}

static int parse_global_addr_suffix(int base_type, long *offset)
{
    int cur_type;
    long idx;

    cur_type = base_type;
    while (g_lex.tok.kind == '[' || g_lex.tok.kind == '.' || g_lex.tok.kind == TOK_ARROW) {
        if (g_lex.tok.kind == '[') {
            int elem_size;
            next_token();
            idx = parse_typed_const_long_expr();
            expect(']');
            elem_size = type_size(cur_type);
            if (elem_size <= 0) elem_size = 2;
            *offset += idx * elem_size;
            continue;
        }
        if (g_lex.tok.kind == TOK_ARROW)
            cur_type = type_decay_ptr(cur_type);
        next_token();
        if (g_lex.tok.kind != TOK_ID) {
            error_here("field name expected in address initializer");
            return 0;
        }
        {
            int sid;
            struct FieldDef *fd;
            sid = type_struct_id(cur_type);
            fd = find_field_def(sid, g_lex.tok.text);
            if (fd == NULL) {
                error_here("unknown field in address initializer");
                return 0;
            }
            *offset += fd->offset;
            cur_type = fd->type;
        }
        next_token();
    }
    return 1;
}

static int parse_global_cast_null_member_address(long *val)
{
    int base_type;
    long offset;

    if (g_lex.tok.kind != '(')
        return 0;
    next_token();
    if (g_lex.tok.kind != '(')
        return 0;
    next_token();
    base_type = parse_type();
    expect(')');
    if (g_lex.tok.kind != TOK_NUM || g_lex.tok.val != 0)
        return 0;
    next_token();
    expect(')');
    if (g_lex.tok.kind != TOK_ARROW)
        return 0;
    offset = 0;
    if (!parse_global_addr_suffix(base_type, &offset))
        return 0;
    *val = offset;
    return 1;
}

static int parse_global_symbol_member_address(char *label, int labelsz)
{
    struct Sym *ls;
    const char *lname;
    long offset;
    int base_type;

    if (g_lex.tok.kind != TOK_ID)
        return 0;
    ls = find_sym(g_lex.tok.text);
    lname = ls ? sym_asm_name(ls) : g_lex.tok.text;
    base_type = ls ? ls->type : TYPE_INT;
    if (ls && ls->is_array)
        base_type = ls->type;
    next_token();

    offset = 0;
    if (!parse_global_addr_suffix(base_type, &offset))
        return 0;

    if (label && labelsz > 0) {
        const char *aname = asm_name_for(lname);
        if (offset == 0) {
            strncpy(label, lname, (size_t)labelsz - 1);
        } else {
            char tmp[64];
            sprintf(tmp, "%s+%ld", aname, offset);
            strncpy(label, tmp, (size_t)labelsz - 1);
        }
        label[labelsz - 1] = 0;
    }
    return 1;
}

/* Parse the address of an element of a string literal, with optional grouping:
 *
 *     &"text"[1]
 *     &("text"[1])
 *
 * A string literal already has a relocatable S<n> label in the global data
 * stream.  Taking the address of one of its constant-index elements therefore
 * differs only by a byte offset (or a two-byte offset for a wide literal). */
static int parse_global_string_element_address(char *label, int labelsz)
{
    LexState saved;
    char *lit;
    long index;
    long offset;
    int grouping;
    int is_wide;
    int litlen;
    int sid;

    saved = lex_save();
    grouping = 0;
    while (g_lex.tok.kind == '(' && !paren_starts_cast()) {
        next_token();
        ++grouping;
    }
    if (g_lex.tok.kind != TOK_STR && g_lex.tok.kind != TOK_WSTR) {
        lex_restore(&saved);
        return 0;
    }

    lit = read_adjacent_string_literals_ex(&is_wide, &litlen);
    if (g_lex.tok.kind != '[') {
        free(lit);
        lex_restore(&saved);
        return 0;
    }
    next_token();
    index = parse_typed_const_long_expr();
    expect(']');
    while (grouping-- > 0)
        expect(')');

    sid = add_string_ex(lit, litlen, is_wide);
    free(lit);
    offset = index * (is_wide ? 2L : 1L);
    if (label != NULL && labelsz > 0) {
        if (offset == 0)
            snprintf(label, (size_t)labelsz, "S%d", sid);
        else
            snprintf(label, (size_t)labelsz, "S%d%+ld", sid, offset);
    }
    return 1;
}

/* Parse a relocatable symbol address hidden behind optional grouping and
 * pointer casts, for example `(char *)(buf + 1)`, `(char *)buf + 1`, or
 * `((char *)buf + 1)`.  The ordinary constant-expression parser cannot fold a
 * symbol, and treating every leading '(' as numeric used to desynchronise the
 * lexer on these standard address constants. */
static int parse_global_grouped_symbol_address(char *label, int labelsz)
{
    LexState saved;
    struct Sym *symbol;
    const char *name;
    long offset;
    int grouping = 0;
    int saw_wrapper = 0;
    int base_type;

    if (g_lex.tok.kind != '(')
        return 0;
    saved = lex_save();
    while (g_lex.tok.kind == '(') {
        if (paren_starts_cast()) {
            int cast_type;
            int cast_size;

            next_token();
            parse_type_name_decl(&cast_type, &cast_size);
            if (g_lex.tok.kind != ')' || type_ptr_depth(cast_type) <= 0) {
                lex_restore(&saved);
                return 0;
            }
            next_token();
            saw_wrapper = 1;
        } else {
            next_token();
            ++grouping;
            saw_wrapper = 1;
        }
    }
    if (!saw_wrapper || g_lex.tok.kind != TOK_ID ||
        find_enum_const(g_lex.tok.text) >= 0) {
        lex_restore(&saved);
        return 0;
    }
    symbol = find_sym(g_lex.tok.text);
    name = symbol != NULL ? sym_asm_name(symbol) : g_lex.tok.text;
    base_type = symbol != NULL ? symbol->type : TYPE_CHAR;
    next_token();
    offset = 0;
    if (g_lex.tok.kind == '+' || g_lex.tok.kind == '-') {
        int negative = g_lex.tok.kind == '-';
        int element_size;

        next_token();
        if (g_lex.tok.kind != TOK_NUM) {
            lex_restore(&saved);
            return 0;
        }
        element_size = type_size(base_type);
        if (element_size <= 0)
            element_size = 1;
        offset = g_lex.tok.val * element_size;
        if (negative)
            offset = -offset;
        next_token();
    }
    while (grouping-- > 0) {
        if (g_lex.tok.kind != ')') {
            lex_restore(&saved);
            return 0;
        }
        next_token();
    }
    if (label != NULL && labelsz > 0) {
        const char *asm_name = asm_name_for(name);
        if (offset == 0)
            snprintf(label, (size_t)labelsz, "%s", name);
        else
            snprintf(label, (size_t)labelsz, "%s%+ld", asm_name, offset);
    }
    return 1;
}

int parse_global_init_atom(long *val, char *label, int labelsz)
{
    int sign;

    sign = 1;

    /*
     * A pointer cast of an address, e.g. `(char *) "Plain"` or
     * `(void *)&("X"[0])`, names a relocation, not an arithmetic constant - the
     * constant-expression evaluator the numeric branch below uses only folds
     * numeric casts, and silently desyncs the lexer on a string operand
     * (DCC-E1102 "expected ';'" at the string, immediately followed by
     * DCC-E1101).  Detect this one shape up front and consume just the cast,
     * so control falls through to the ordinary string/address handling below
     * as if the cast were never there - it only changes the static type, not
     * which label the initializer resolves to.
     */
    if (g_lex.tok.kind == '(' && paren_starts_cast()) {
        LexState _ls = lex_save();
        int cast_type;
        int cast_size;

        next_token();
        parse_type_name_decl(&cast_type, &cast_size);
        expect(')');
        if (!(type_ptr_depth(cast_type) > 0 &&
              (g_lex.tok.kind == TOK_STR || g_lex.tok.kind == TOK_WSTR ||
               g_lex.tok.kind == '&')))
            lex_restore(&_ls);
    }

    if (parse_global_grouped_symbol_address(label, labelsz))
        return 2;

    /*
     * Numeric scalar initializers may be full C constant expressions, not just
     * a single token.  This handles forms used by lzpack such as:
     *
     *     static long s_win_start = (MAXDIST * 2);
     *
     * and array bounds using parenthesized macro expressions.
     */
    if (g_lex.tok.kind == TOK_NUM || g_lex.tok.kind == TOK_CHARLIT ||
        g_lex.tok.kind == '-' || g_lex.tok.kind == '+' || g_lex.tok.kind == '~' ||
        g_lex.tok.kind == '!' || g_lex.tok.kind == '(' ||
        g_lex.tok.kind == TOK_SIZEOF) {
        val[0] = parse_typed_const_expr_long();
        if (label) label[0] = 0;
        return 1;
    }

    if (sign != 1) {
        error_here("numeric constant expected after sign");
        return 0;
    }

    if (g_lex.tok.kind == TOK_STR || g_lex.tok.kind == TOK_WSTR) {
        int sid;

        {
            char *lit;
            int is_wide;
            int litlen;
            lit = read_adjacent_string_literals_ex(&is_wide, &litlen);
            sid = add_string_ex(lit, litlen, is_wide);
            free(lit);
        }
        if (label && labelsz > 0)
            sprintf(label, "S%d", sid);
        return 2;       /* symbolic address */
    }

    if (g_lex.tok.kind == TOK_ID) {
        /* An enumerator is an integer constant, not an address-bearing
         * external symbol.  Let the constant-expression parser consume the
         * whole expression so global initializers such as:
         *     enum E e = BLUE;
         *     int a[] = { RED, GREEN + 1 };
         * emit numeric data instead of dw _BLUE / dw _RED.
         */
        if (find_enum_const(g_lex.tok.text) >= 0) {
            val[0] = parse_typed_const_expr_long();
            if (label) label[0] = 0;
            return 1;
        }

        {
            struct Sym *ls;
            const char *lname;
            ls = find_sym(g_lex.tok.text);
            /* A global initializer that names a function (e.g. a function-
             * pointer table) is a real reference: this bypasses the normal
             * runtime expression codegen entirely, so it must mark the
             * function needed itself rather than relying on the ast_gen_expr
             * SC_FUNC hook. */
            if (ls != NULL && ls->storage == SC_FUNC && ls->is_static)
                ls->deferred_body_needed = 1;
            lname = ls ? sym_asm_name(ls) : g_lex.tok.text;
            if (label && labelsz > 0) {
                strncpy(label, lname, labelsz - 1);
                label[labelsz - 1] = 0;
            }
            next_token();

            /* pointer +/- constant: e.g. buf - 0x4000
             * Emit as a raw asm arithmetic expression so M80 can relocate it. */
            if (label && (g_lex.tok.kind == '-' || g_lex.tok.kind == '+')) {
                int neg = (g_lex.tok.kind == '-');
                long delta;
                int element_size = 1;

                next_token();
                delta = parse_typed_const_expr_long();
                if (ls != NULL) {
                    int element_type = ls->type;

                    if (!ls->is_array && type_ptr_depth(element_type) > 0)
                        element_type = type_decay_ptr(element_type);
                    element_size = type_size(element_type);
                    if (element_size <= 0)
                        element_size = 1;
                }
                delta *= element_size;
                snprintf(label, (size_t)labelsz, "%s%c%ld",
                         asm_name_for(lname), neg ? '-' : '+', delta);
            }
        }
        return 2;       /* symbolic address */
    }

    if (g_lex.tok.kind == '&') {
        next_token();
        {
            LexState _ls = lex_save();
            if (parse_global_cast_null_member_address(val)) {
                if (label) label[0] = 0;
                return 1;
            }
            lex_restore(&_ls);
        }
        if (parse_global_compound_literal_address(label, labelsz))
            return 2;
        if (parse_global_string_element_address(label, labelsz))
            return 2;
        if (parse_global_symbol_member_address(label, labelsz))
            return 2;
        if (g_lex.tok.kind == TOK_ID) {
            if (label && labelsz > 0) {
                struct Sym *ls;
                const char *lname;
                ls = find_sym(g_lex.tok.text);
                lname = ls ? sym_asm_name(ls) : g_lex.tok.text;
                strncpy(label, lname, labelsz - 1);
                label[labelsz - 1] = 0;
            }
            next_token();
            return 2;   /* symbolic address */
        }
        error_here("identifier expected after & in initializer");
        if (g_lex.tok.kind != ',' && g_lex.tok.kind != ';' && g_lex.tok.kind != '}')
            next_token();
        return 0;
    }

    error_here("constant initializer expected");
    if (g_lex.tok.kind != ',' && g_lex.tok.kind != ';' && g_lex.tok.kind != '}')
        next_token();
    return 0;
}



static void grow_init_cap(struct Sym *s, int need)
{
    int newcap;
    if (need <= s->init_cap) return;
    newcap = s->init_cap ? s->init_cap * 2 : 16;
    while (newcap < need) newcap *= 2;
    s->init_labels = (char (*)[64])realloc(s->init_labels, (size_t)newcap * sizeof(s->init_labels[0]));
    s->init_sizes  = (int *)realloc(s->init_sizes,  (size_t)newcap * sizeof(s->init_sizes[0]));
    if (!s->init_labels || !s->init_sizes) fatal("out of memory for initializer");
    s->init_cap = newcap;
}

void append_global_init(struct Sym *s, const char *label, long v, int bytes, int is_label)
{
    grow_init_cap(s, s->init_count + 1);
    if (bytes <= 0) bytes = 2;
    if (is_label) {
        strncpy(s->init_labels[s->init_count], label, sizeof(s->init_labels[0]));
        s->init_labels[s->init_count][sizeof(s->init_labels[0]) - 1] = '\0';
    } else {
        sprintf(s->init_labels[s->init_count], "%lu", (unsigned long)v);
    }
    s->init_sizes[s->init_count] = bytes;
    s->init_count++;
}

void append_global_zero_bytes(struct Sym *s, int bytes)
{
    while (bytes > 0) {
        int n;
        n = bytes >= 2 ? 2 : 1;
        append_global_init(s, NULL, 0, n, 0);
        bytes -= n;
    }
}

static int global_init_used_bytes(struct Sym *s)
{
    int i;
    int used;

    used = 0;
    for (i = 0; i < s->init_count; ++i)
        used += s->init_sizes[i] ? s->init_sizes[i] : 2;
    return used;
}

static int global_init_entry_at_offset(struct Sym *s, int off, int *startp)
{
    int i;
    int cur;
    int sz;

    cur = 0;
    for (i = 0; i < s->init_count; ++i) {
        sz = s->init_sizes[i] ? s->init_sizes[i] : 2;
        if (cur == off) {
            if (startp) startp[0] = cur;
            return i;
        }
        if (off > cur && off < cur + sz) {
            if (startp) startp[0] = cur;
            return -2;
        }
        cur += sz;
    }

    if (cur == off) {
        if (startp) startp[0] = cur;
        return s->init_count;
    }

    if (startp) startp[0] = cur;
    return -1;
}

static void global_init_pad_to_offset(struct Sym *s, int off)
{
    while (global_init_used_bytes(s) < off)
        append_global_init(s, NULL, 0, 1, 0);
}

static void global_init_write_byte_at(struct Sym *s, int off, unsigned int v)
{
    int idx;
    int start;

    if (off < 0) {
        error_here("negative initializer offset");
        return;
    }

    global_init_pad_to_offset(s, off);
    idx = global_init_entry_at_offset(s, off, &start);
    if (idx == s->init_count) {
        append_global_init(s, NULL, (long)(v & 255U), 1, 0);
        return;
    }

    if (idx < 0 || s->init_sizes[idx] != 1) {
        error_here("initializer designator overlaps address constant");
        return;
    }

    sprintf(s->init_labels[idx], "%u", v & 255U);
    s->init_sizes[idx] = 1;
}

static void global_init_insert_entry_at(struct Sym *s, int idx, const char *label, long v, int bytes, int is_label)
{
    grow_init_cap(s, s->init_count + 1);
    if (idx < s->init_count) {
        memmove(&s->init_labels[idx + 1], &s->init_labels[idx],
                (size_t)(s->init_count - idx) * sizeof(s->init_labels[0]));
        memmove(&s->init_sizes[idx + 1], &s->init_sizes[idx],
                (size_t)(s->init_count - idx) * sizeof(s->init_sizes[0]));
    }
    if (is_label) {
        /* Full sizeof (not sizeof(...) - 1), matching append_global_init
         * above: strncpy's own length argument exactly matching a source
         * GCC can prove is exactly that long reads as guaranteed
         * truncation to -Wstringop-truncation, even though the explicit
         * NUL write on the next line makes either form equally safe. */
        strncpy(s->init_labels[idx], label, sizeof(s->init_labels[0]));
        s->init_labels[idx][sizeof(s->init_labels[0]) - 1] = 0;
    } else {
        sprintf(s->init_labels[idx], "%ld", v);
    }
    s->init_sizes[idx] = bytes;
    s->init_count++;
}

static void global_init_remove_entries(struct Sym *s, int idx, int count)
{
    if (count <= 0) return;
    if (idx + count < s->init_count) {
        memmove(&s->init_labels[idx], &s->init_labels[idx + count],
                (size_t)(s->init_count - idx - count) * sizeof(s->init_labels[0]));
        memmove(&s->init_sizes[idx], &s->init_sizes[idx + count],
                (size_t)(s->init_count - idx - count) * sizeof(s->init_sizes[0]));
    }
    s->init_count -= count;
}

static void global_init_write_label_at(struct Sym *s, int off, const char *label, int bytes)
{
    int idx;
    int start;
    int consumed;
    int count;
    int i;

    if (bytes <= 0) bytes = 2;
    if (off < 0) {
        error_here("negative initializer offset");
        return;
    }

    global_init_pad_to_offset(s, off);
    idx = global_init_entry_at_offset(s, off, &start);
    if (idx == s->init_count) {
        append_global_init(s, label, 0, bytes, 1);
        return;
    }
    if (idx < 0) {
        error_here("initializer designator overlaps address constant");
        return;
    }

    consumed = 0;
    count = 0;
    for (i = idx; i < s->init_count && consumed < bytes; ++i) {
        if (s->init_sizes[i] != 1) {
            error_here("initializer designator overlaps address constant");
            return;
        }
        consumed++;
        count++;
    }
    while (consumed < bytes) {
        append_global_init(s, NULL, 0, 1, 0);
        consumed++;
        count++;
    }
    global_init_remove_entries(s, idx, count);
    global_init_insert_entry_at(s, idx, label, 0, bytes, 1);
}

static void global_init_write_value_at(struct Sym *s, int off, const char *label, long v, int bytes, int is_label)
{
    int i;
    unsigned long uv;

    if (bytes <= 0) bytes = 2;
    if (!is_label && bytes == 1 && type_is_bool(s->type))
        v = v ? 1 : 0;
    if (is_label) {
        global_init_write_label_at(s, off, label, bytes);
        return;
    }

    uv = (unsigned long)v;
    for (i = 0; i < bytes; ++i)
        global_init_write_byte_at(s, off + i, (unsigned int)((uv >> (8 * i)) & 255UL));
}

void append_global_char_array_string(struct Sym *s, int count, const char *str)
{
    int i;
    int n;

    n = (int)strlen(str);
    if (count <= 0)
        return;

    if (n > count) {
        error_here("string initializer too long for char array field");
        n = count;
    }

    for (i = 0; i < n; ++i)
        append_global_init(s, NULL, (unsigned char)str[i], 1, 0);

    while (i < count) {
        append_global_init(s, NULL, 0, 1, 0);
        i++;
    }
}

void parse_global_init_type(struct Sym *s, int type, int size);

static void global_init_write_char_array_string_at(struct Sym *s, int baseoff, int count, const char *str, int n)
{
    int i;

    if (count <= 0)
        return;

    if (n > count) {
        error_here("string initializer too long for char array field");
        n = count;
    }

    for (i = 0; i < n; ++i)
        global_init_write_value_at(s, baseoff + i, NULL, (unsigned char)str[i], 1, 0);
    while (i < count) {
        global_init_write_value_at(s, baseoff + i, NULL, 0, 1, 0);
        i++;
    }
}

static void parse_global_init_array_at(struct Sym *s, int elem_type, int count, int elem_size, int baseoff)
{
    int n;
    int maxn;
    int had_brace;
    int parse_elem_size;

    if (elem_size <= 0) elem_size = type_size(elem_type);
    if (elem_size <= 0) elem_size = 2;
    parse_elem_size = elem_size;
    if (count <= 0 && s->is_array && s->dim_count > 1 && s->dims[0] == 0) {
        parse_elem_size = type_size(elem_type);
        if (parse_elem_size <= 0) parse_elem_size = 2;
    }

    if ((elem_type & 15) == TYPE_CHAR && type_ptr_depth(elem_type) == 0 &&
        g_lex.tok.kind == TOK_STR) {
        char *lit;
        int is_wide;
        int litlen;
        lit = read_adjacent_string_literals_ex(&is_wide, &litlen);
        if (is_wide)
            error_here("wide string cannot initialize char array field");
        else
            global_init_write_char_array_string_at(s, baseoff, count, lit, litlen);
        free(lit);
        return;
    }

    had_brace = 0;
    if (g_lex.tok.kind == '{') {
        next_token();
        had_brace = 1;
    }
    n = 0;
    maxn = 0;
    while (g_lex.tok.kind != TOK_EOF && (had_brace || count <= 0 || n < count) &&
           (had_brace || g_lex.tok.kind != '}')) {
        if (had_brace && g_lex.tok.kind == '}')
            break;
        if (had_brace && g_lex.tok.kind == '[') {
            next_token();
            n = parse_typed_designator_index_expr();
            expect(']');
            expect('=');
        }
        if (count > 0 && n >= count) {
            error_here("too many initializer elements");
            skip_initializer_or_decl_tail();
            break;
        }
        parse_global_init_type_at(s, elem_type, parse_elem_size, baseoff + n * parse_elem_size);
        n++;
        if (n > maxn) maxn = n;
        if (!had_brace && count > 0 && n >= count)
            break;
        if (!accept(',')) break;
        if (had_brace && g_lex.tok.kind == '}') break;
    }
    if (had_brace)
        expect('}');
    /*
     * Omitted first dimension on an array of structs, e.g.
     *     static const Instr prog[] = { {..}, {..}, {..} };
     *     static const Instr grid[][2] = { {..}, {..}, {..}, {..} };
     * parse_global_init_struct/_type consumes one whole struct per top-level
     * element, so `n` is the number of fully parsed struct objects.  The
     * struct-array branch in parse_global_init_list returns immediately
     * without inferring the size, so record the first dimension here.  For
     * multidimensional arrays, elem_size is the first-dimension stride.
     */
    if (count <= 0 && s->is_array && s->array_len == 0 && s->dim_count > 0 && s->dims[0] == 0) {
        int inner;
        int rows;
        int stride;

        inner = sym_array_inner_count_from(s, 1);
        if (inner <= 0)
            inner = 1;
        rows = (maxn + inner - 1) / inner;
        stride = elem_size;
        if (stride <= 0) {
            int base = type_size(elem_type);
            if (base <= 0) base = 2;
            stride = inner * base;
        }

        s->dims[0] = rows;
        s->array_len = rows;
        s->size = rows * stride;
        if (s->elem_size <= 0)
            s->elem_size = stride;
    }
}

void parse_global_init_array(struct Sym *s, int elem_type, int count, int elem_size)
{
    parse_global_init_array_at(s, elem_type, count, elem_size, global_init_used_bytes(s));
}

static int field_def_index(struct FieldDef *fd)
{
    if (fd == NULL)
        return -1;
    return (int)(fd - field_defs);
}

static void parse_global_init_struct_at(struct Sym *s, int type, int baseoff)
{
    int sid;
    int i;
    int is_union;
    int had_brace;
    /* Bit-field storage units already written at this struct level; a revisit
     * via out-of-order designators must merge with the earlier value because
     * each unit write covers the whole 16-bit word. */
    int bf_unit_offs[32];
    unsigned int bf_unit_vals[32];
    int bf_nunits;

    bf_nunits = 0;

    sid = type_struct_id(type);
    is_union = (sid > 0 && sid <= nstruct_defs && struct_defs[sid - 1].is_union);

    had_brace = 0;
    if (g_lex.tok.kind == '{') {
        next_token();
        had_brace = 1;
    }

    if (is_union) {
        struct FieldDef *first;
        first = NULL;
        for (i = 0; i < nfield_defs; ++i) {
            if (field_defs[i].parent_struct_id == sid && !field_defs[i].is_promoted) {
                first = &field_defs[i];
                break;
            }
        }

        if (first && g_lex.tok.kind != TOK_EOF && g_lex.tok.kind != '}') {
            if (first->is_array)
                parse_global_init_array_at(s, first->elem_type, first->array_len, first->elem_size, baseoff);
            else
                parse_global_init_type_at(s, first->type, first->size, baseoff);

            /* Braceless union element in an array (static U a[] = {1,2,3})
             * stops after its single initializer; the array loop owns the
             * comma.  Only a braced element may report extra members. */
            if (had_brace && accept(',')) {
                if (g_lex.tok.kind != '}') {
                    error_here("too many union initializer elements");
                    while (g_lex.tok.kind != TOK_EOF && g_lex.tok.kind != '}')
                        next_token();
                }
            }
        }

        if (had_brace)
            expect('}');
        return;
    }

    for (i = 0; i < nfield_defs && g_lex.tok.kind != TOK_EOF && g_lex.tok.kind != '}'; ++i) {
        struct FieldDef *fd;
        if (g_lex.tok.kind == '.') {
            next_token();
            if (g_lex.tok.kind != TOK_ID) {
                error_here("expected a field designator, such as '.field = value'");
                while (g_lex.tok.kind != TOK_EOF && g_lex.tok.kind != '}')
                    next_token();
                break;
            }
            fd = find_field_def(sid, g_lex.tok.text);
            if (fd == NULL) {
                error_here("unknown field initializer designator");
                while (g_lex.tok.kind != TOK_EOF && g_lex.tok.kind != '}')
                    next_token();
                break;
            }
            i = field_def_index(fd);
            next_token();
            expect('=');
        } else {
            fd = &field_defs[i];
            if (fd->parent_struct_id != sid || fd->is_promoted)
                continue;
        }

        if (fd->bit_width > 0) {
            int unit_off;
            int k;
            int stop;
            unsigned int unit;

            unit = pack_struct_bitfield_unit(sid, i, fd, 0,
                bf_unit_offs, bf_unit_vals, &bf_nunits,
                (int)(sizeof(bf_unit_offs) / sizeof(bf_unit_offs[0])),
                &unit_off, &k, &stop);
            global_init_write_value_at(s, baseoff + unit_off, NULL, (long)(unit & 0xffffU), 2, 0);
            if (k > i)
                i = k - 1;
            if (stop)
                break;
            /* Restart the field scan when a designator stopped the packing
             * loop, so it is handled even when this unit's owner was the last
             * declared field. */
            if (g_lex.tok.kind == '.')
                i = -1;
            continue;
        }

        if (fd->is_array)
            parse_global_init_array_at(s, fd->elem_type, fd->array_len, fd->elem_size,
                                       baseoff + fd->offset);
        else
            parse_global_init_type_at(s, fd->type, fd->size, baseoff + fd->offset);

        if (!had_brace && next_parent_field_index(sid, i + 1) < 0)
            break;
        if (!accept(',')) break;
        if (g_lex.tok.kind == '}') break;
        if (g_lex.tok.kind == '.') i = -1;
    }
    if (had_brace)
        expect('}');
}

void parse_global_init_struct(struct Sym *s, int type)
{
    parse_global_init_struct_at(s, type, global_init_used_bytes(s));
}

static void parse_global_init_type_at(struct Sym *s, int type, int size, int baseoff)
{
    long v;
    char label[64];
    int k;

    if ((type & TYPE_STRUCT) && type_ptr_depth(type) == 0) {
        parse_global_init_struct_at(s, type, baseoff);
        return;
    }

    if ((type & 15) == TYPE_FLOAT && type_ptr_depth(type) == 0) {
        unsigned long bits;
        if (parse_float_init_literal(&bits))
            global_init_write_value_at(s, baseoff, NULL, (long)bits, 4, 0);
        else {
            error_here("float initializer must be constant");
            if (g_lex.tok.kind != ',' && g_lex.tok.kind != '}') next_token();
        }
        return;
    }

    k = parse_global_init_atom(&v, label, sizeof(label));
    if (k == 1)
    {
        if (type_is_bool(type))
            v = v ? 1 : 0;
        global_init_write_value_at(s, baseoff, NULL, v, size, 0);
    }
    else if (k == 2)
        global_init_write_value_at(s, baseoff, label, 0, size, 1);
    else
        next_token();
}

void parse_global_init_type(struct Sym *s, int type, int size)
{
    parse_global_init_type_at(s, type, size, global_init_used_bytes(s));
}

void parse_global_scalar_array_init_scalar(struct Sym *s, int *np)
{
    long v;
    int k;
    int n;
    int elem_bytes;

    /* Optional braces may wrap a scalar array element, including a string
     * literal used to initialize a pointer element.  Real array-dimension
     * braces are handled by parse_global_scalar_array_init_level before it
     * delegates here. */
    if (accept('{')) {
        parse_global_scalar_array_init_scalar(s, np);
        accept(',');
        expect('}');
        return;
    }

    n = np[0];
    grow_init_cap(s, n + 1);

    s->init_labels[n][0] = 0;

    elem_bytes = type_size(s->type);
    if (elem_bytes <= 0)
        elem_bytes = 2;

    if ((s->type & 15) == TYPE_FLOAT && type_ptr_depth(s->type) == 0) {
        unsigned long bits;
        if (parse_float_init_literal(&bits)) {
            sprintf(s->init_labels[n], "%lu", bits);
            s->init_sizes[n] = 4;
            np[0] = n + 1;
        } else {
            error_here("float initializer must be constant");
            if (g_lex.tok.kind != ',' && g_lex.tok.kind != '}')
                next_token();
        }
    } else {
        k = parse_global_init_atom(&v, s->init_labels[n],
                                   sizeof(s->init_labels[n]));
        if (k == 1) {
            if (type_is_bool(s->type))
                v = v ? 1 : 0;
            sprintf(s->init_labels[n], "%ld", v);
            s->init_sizes[n] = elem_bytes;
            np[0] = n + 1;
        } else if (k == 2) {
            s->init_sizes[n] = elem_bytes;
            np[0] = n + 1;
        } else {
            if (g_lex.tok.kind != ',' && g_lex.tok.kind != '}')
                next_token();
        }
    }
}

void parse_global_scalar_array_zero_to(struct Sym *s, int *np, int limit)
{
    int elem_bytes;

    elem_bytes = type_size(s->type);
    if (elem_bytes <= 0)
        elem_bytes = 2;

    while (np[0] < limit) {
        grow_init_cap(s, np[0] + 1);
        sprintf(s->init_labels[np[0]], "0");
        s->init_sizes[np[0]] = elem_bytes;
        np[0] = np[0] + 1;
    }
}

void parse_global_scalar_array_init_level(struct Sym *s, int *np, int level)
{
    int start;
    int limit;

    if (!accept('{')) {
        parse_global_scalar_array_init_scalar(s, np);
        return;
    }

    start = np[0];
    limit = start + sym_array_elems_from_level(s, level);

    /* A string literal initializes one complete row of a multidimensional
     * character array.  It is not a pointer-valued scalar atom: copy its
     * bytes (and terminating NUL when it fits) inline, then zero-fill the
     * rest of this row. */
    if ((s->type & 15) == TYPE_CHAR && type_ptr_depth(s->type) == 0 &&
        g_lex.tok.kind == TOK_STR) {
        char *lit;
        int is_wide;
        int litlen;
        int i;

        lit = read_adjacent_string_literals_ex(&is_wide, &litlen);
        if (is_wide) {
            error_here("wide string cannot initialize char array row");
        } else {
            if (litlen > limit - start)
                error_here("string initializer too long for char array row");
            for (i = 0; i < litlen && np[0] < limit; ++i) {
                grow_init_cap(s, np[0] + 1);
                sprintf(s->init_labels[np[0]], "%u",
                        (unsigned char)lit[i]);
                s->init_sizes[np[0]] = 1;
                np[0] = np[0] + 1;
            }
            if (np[0] < limit) {
                grow_init_cap(s, np[0] + 1);
                sprintf(s->init_labels[np[0]], "0");
                s->init_sizes[np[0]] = 1;
                np[0] = np[0] + 1;
            }
            parse_global_scalar_array_zero_to(s, np, limit);
        }
        free(lit);
        accept(',');
        expect('}');
        return;
    }

    while (g_lex.tok.kind != TOK_EOF && g_lex.tok.kind != '}') {
        if (g_lex.tok.kind == '[') {
            int idx;
            int span;

            next_token();
            idx = parse_typed_designator_index_expr();
            expect(']');
            expect('=');
            span = sym_array_elems_from_level(s, level + 1);
            if (span <= 0) span = 1;
            if (idx < 0)
                error_here("negative array initializer designator");
            else {
                int target;
                target = start + idx * span;
                if (target > np[0])
                    parse_global_scalar_array_zero_to(s, np, target);
                else
                    np[0] = target;
            }
        }
        if (g_lex.tok.kind == '{' && s->dim_count > 0 && level + 1 < s->dim_count)
            parse_global_scalar_array_init_level(s, np, level + 1);
        else
            parse_global_scalar_array_init_scalar(s, np);

        if (!accept(','))
            break;
        if (g_lex.tok.kind == '}')
            break;
    }
    expect('}');

    parse_global_scalar_array_zero_to(s, np, limit);
}



void parse_global_init_list(struct Sym *s)
{
    int n;
    int maxn;
    long v;
    int k;

    if (!accept('='))
        return;

    /*
     * C permits char arrays to be initialized by one or more adjacent
     * string literals:
     *     static char s[6] = "he" "llo";
     * The lexer already decodes escapes into tok.text.  Store the bytes in
     * init_labels[] as decimal strings so the existing data emitter can use
     * the normal 1-byte array path.  Add the trailing NUL if there is room in
     * the declared object.
     */
    if (s->is_array && (s->type & 15) == TYPE_CHAR && type_ptr_depth(s->type) == 0 &&
        g_lex.tok.kind == TOK_STR) {
        n = 0;
        while (g_lex.tok.kind == TOK_STR) {
            int si;
            for (si = 0; si < g_lex.tok.text_len; ++si) {
                grow_init_cap(s, n + 1);
                sprintf(s->init_labels[n], "%u", (unsigned char)g_lex.tok.text[si]);
                s->init_sizes[n] = 1;
                n++;
            }
            next_token();
        }

        /* For C89 forms like:
         *     static char s[] = "hello";
         * infer the array size from the string literal plus the terminating
         * NUL.  Callers mark omitted-dimension arrays with is_array set and
         * size/array_len left as zero.
         */
        if (s->size == 0 || s->array_len == 0) {
            grow_init_cap(s, n + 1);
            sprintf(s->init_labels[n], "0");
            s->init_sizes[n] = 1;
            n++;
            s->size = n;
            s->array_len = n;
            s->elem_size = 1;
        } else if (n < s->size) {
            grow_init_cap(s, n + 1);
            sprintf(s->init_labels[n], "0");
            s->init_sizes[n] = 1;
            n++;
        }

        while (s->size > 0 && n < s->size) {
            grow_init_cap(s, n + 1);
            sprintf(s->init_labels[n], "0");
            s->init_sizes[n] = 1;
            n++;
        }

        s->has_init = 1;
        s->init_count = n;
        return;
    }

    /*
     * Aggregate initializers for structs and arrays of structs/unions are
     * flattened field-by-field so each emitted element uses the correct byte
     * width.  Handle them BEFORE consuming the array's opening brace so that
     * parse_global_init_array / parse_global_init_struct own every brace
     * symmetrically (each element, including the first, consumes its own
     * brace).  Consuming the array brace here first used to leave the first
     * element's brace to be eaten by parse_global_init_array -- an off-by-one
     * that balanced for plain structs but broke union array elements.
     */
    if ((s->type & TYPE_STRUCT) && type_ptr_depth(s->type) == 0 && g_lex.tok.kind == '{') {
        s->init_count = 0;
        if (s->is_array)
            parse_global_init_array(s, s->type, s->array_len, s->elem_size);
        else
            parse_global_init_struct(s, s->type);
        s->has_init = 1;
        return;
    }

    if (!accept('{')) {
        if (!s->is_array) {
            if ((s->type & TYPE_STRUCT) && type_ptr_depth(s->type) == 0) {
                error_here("struct initializer list expected");
                skip_initializer_or_decl_tail();
                return;
            }
            s->has_init = 1;
            grow_init_cap(s, 1);
            if ((s->type & 15) == TYPE_FLOAT && type_ptr_depth(s->type) == 0) {
                unsigned long bits;
                if (parse_float_init_literal(&bits)) {
                    sprintf(s->init_labels[0], "%lu", bits);
                    s->init_sizes[0] = 4;
                    s->init_count = 1;
                }
            } else {
                k = parse_global_init_atom(&v, s->init_labels[0],
                                           sizeof(s->init_labels[0]));
                if (k == 1) {
                    if (type_is_bool(s->type))
                        v = v ? 1 : 0;
                    s->init_value = v;
                    s->init_count = 0;
                } else if (k == 2) {
                    s->init_sizes[0] = type_size(s->type);
                    if (s->init_sizes[0] <= 0) s->init_sizes[0] = 2;
                    s->init_count = 1;
                }
            }
        } else {
            error_here("array initializer list expected");
            while (g_lex.tok.kind != TOK_EOF && g_lex.tok.kind != ';' && g_lex.tok.kind != ',')
                next_token();
        }
        return;
    }

    n = 0;
    maxn = 0;
    while (g_lex.tok.kind != TOK_EOF && g_lex.tok.kind != '}') {
        if (g_lex.tok.kind == '[') {
            int idx;
            int span;
            int target;

            next_token();
            idx = parse_typed_designator_index_expr();
            expect(']');
            expect('=');

            span = sym_array_elems_from_level(s, 1);
            if (span <= 0) span = 1;
            if (idx < 0) {
                error_here("negative array initializer designator");
            } else {
                target = idx * span;
                if (target > n)
                    parse_global_scalar_array_zero_to(s, &n, target);
                else
                    n = target;
            }
        }
        if (g_lex.tok.kind == '{' && s->dim_count > 1)
            parse_global_scalar_array_init_level(s, &n, 1);
        else
            parse_global_scalar_array_init_scalar(s, &n);
        if (n > maxn) maxn = n;
        if (!accept(','))
            break;
        if (g_lex.tok.kind == '}')
            break;
    }

    expect('}');
    if (maxn > n)
        n = maxn;
    if (s->is_array && s->array_len > 0) {
        int total;
        total = sym_array_total_elems(s);
        parse_global_scalar_array_zero_to(s, &n, total);
    }
    s->has_init = 1;
    s->init_count = n;
    if (s->is_array && (s->array_len == 0 || s->size == 0)) {
        int elem_bytes;
        elem_bytes = type_size(s->type);
        if (elem_bytes <= 0) elem_bytes = 2;

        infer_omitted_first_dim_from_init(s, n);

        if (s->array_len == 0)
            s->array_len = n;
        if (s->size == 0)
            s->size = n * elem_bytes;
        if (s->elem_size <= 0)
            s->elem_size = elem_bytes;
    }
}
