/*
 * dcc_decl.c - local declaration and initializer code generation.
 *
 * Emits storage and initialisation for function-local declarations: scalars,
 * arrays, structs/unions and bitfields, brace-enclosed initializer lists, and
 * const-scalar folding of local initializers into immediates.
 *
 * MODULE: compiled as its own translation unit; shared declarations are in dcc.h.
 * Source provenance: monolith src/ddc.c lines 13128-13991.
 */

#include "dcc.h"
#include "dcc_ast.h"
int parse_float_init_literal(unsigned long *bits)
{
    int sign;
    char lit[MAX_TOK_TEXT + 2];
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    struct Token save_tok;

    /*
     * This helper is only for the compact constant-initializer fast path.
     * Be conservative: if a float literal is followed by an operator, as in
     *     float r = 16.0 * f;
     * then it is not a complete initializer.  Rewind and let gen_expr()
     * compile the full expression.
     */
    save_pos = posi;
    save_tok_start = tok_start_pos;
    save_line = line_no;
    save_tok_line = tok_line;
    save_tok = tok;

    sign = 1;
    if (tok.kind == '-') {
        sign = -1;
        next_token();
    } else if (tok.kind == '+') {
        next_token();
    }

    if (tok.kind == TOK_FLOATLIT) {
        if (sign < 0) {
            lit[0] = '-';
            strncpy(lit + 1, tok.text, MAX_TOK_TEXT);
            lit[MAX_TOK_TEXT] = 0;
            bits[0] = parse_float_literal_bits(lit);
        } else {
            bits[0] = parse_float_literal_bits(tok.text);
        }
        next_token();

        if (tok.kind == ';' || tok.kind == ',' || tok.kind == '}')
            return 1;

        posi = save_pos;
        tok_start_pos = save_tok_start;
        line_no = save_line;
        tok_line = save_tok_line;
        tok = save_tok;
        return 0;
    }

    if (tok.kind == TOK_NUM || tok.kind == TOK_CHARLIT) {
        double d;
        union { float f; unsigned char b[4]; } u;
        unsigned long v;
        d = (double)(sign * tok.val);
        u.f = (float)d;
        v = ((unsigned long)u.b[0]) |
            ((unsigned long)u.b[1] << 8) |
            ((unsigned long)u.b[2] << 16) |
            ((unsigned long)u.b[3] << 24);
        bits[0] = v;
        next_token();

        if (tok.kind == ';' || tok.kind == ',' || tok.kind == '}')
            return 1;

        posi = save_pos;
        tok_start_pos = save_tok_start;
        line_no = save_line;
        tok_line = save_tok_line;
        tok = save_tok;
        return 0;
    }

    posi = save_pos;
    tok_start_pos = save_tok_start;
    line_no = save_line;
    tok_line = save_tok_line;
    tok = save_tok;
    return 0;
}


int type_is_const_scalar_candidate(int type)
{
    if (type_ptr_depth(type) != 0)
        return 0;
    if (type & TYPE_STRUCT)
        return 0;
    return type_size(type) == 1 || type_size(type) == 2 || type_size(type) == 4;
}

int try_parse_local_const_initializer(int type, unsigned long *valuep)
{
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    int save_errors;
    int save_long_suffix;
    int save_unsigned_suffix;
    struct Token save_tok;

    save_pos = posi;
    save_tok_start = tok_start_pos;
    save_line = line_no;
    save_tok_line = tok_line;
    save_errors = errors;
    save_long_suffix = g_tok_long_suffix;
    save_unsigned_suffix = g_tok_unsigned_suffix;
    save_tok = tok;

    if (type_is_float(type)) {
        unsigned long bits;
        if (parse_float_init_literal(&bits)) {
            valuep[0] = bits;
            return 1;
        }
    } else {
        struct ConstVal cv;
        if (try_parse_const_expr_value(&cv) &&
            (tok.kind == ';' || tok.kind == ',' || tok.kind == '}') &&
            errors == save_errors) {
            cf_cast_to_type(&cv, type);
            valuep[0] = cv.u;
            return 1;
        }
    }

    posi = save_pos;
    tok_start_pos = save_tok_start;
    line_no = save_line;
    tok_line = save_tok_line;
    errors = save_errors;
    g_tok_long_suffix = save_long_suffix;
    g_tok_unsigned_suffix = save_unsigned_suffix;
    tok = save_tok;
    return 0;
}

/*
 * Shared const-scalar folding decision for a local declaration, used by BOTH
 * the frame-sizing scan and codegen so they allocate identically.  A
 * `const`-qualified scalar with a compile-time-constant initializer whose
 * address is never taken needs no stack storage: it folds to its value.  On
 * success the symbol is created with zero storage (is_const_value set) and the
 * initializer tokens are consumed.  On any miss the lexer is left exactly at
 * the '=' so the caller can allocate real storage and emit/skip the
 * initializer normally.  store_name is the (possibly for-init-renamed) table
 * name; src_name is the original spelling used for the address-taken probe.
 */
struct Sym *try_const_fold_local(const char *store_name, const char *src_name,
                                 int type, int has_array)
{
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    int save_errors;
    int save_long_suffix;
    int save_unsigned_suffix;
    struct Token save_tok;
    unsigned long const_value;
    struct Sym *s;

    if (!decl_is_const || has_array ||
        !type_is_const_scalar_candidate(type) || tok.kind != '=' ||
        local_name_address_taken_ahead(src_name))
        return NULL;

    save_pos = posi;
    save_tok_start = tok_start_pos;
    save_line = line_no;
    save_tok_line = tok_line;
    save_errors = errors;
    save_long_suffix = g_tok_long_suffix;
    save_unsigned_suffix = g_tok_unsigned_suffix;
    save_tok = tok;

    next_token();   /* consume '=' */
    if (try_parse_local_const_initializer(type, &const_value)) {
        s = add_local_known(store_name, type, SC_LOCAL, 0, 0);
        s->is_const_value = 1;
        s->const_value = const_value;
        return s;
    }

    /* Not a compile-time constant: rewind to the '=' for the caller. */
    posi = save_pos;
    tok_start_pos = save_tok_start;
    line_no = save_line;
    tok_line = save_tok_line;
    errors = save_errors;
    g_tok_long_suffix = save_long_suffix;
    g_tok_unsigned_suffix = save_unsigned_suffix;
    tok = save_tok;
    return NULL;
}

void emit_load_const_sym_value(struct Sym *s)
{
    struct ConstVal cv;

    if (type_is_float(s->type)) {
        emit_load_float_bits(s->const_value);
        g_expr_type = TYPE_FLOAT;
        return;
    }

    cv.u = s->const_value;
    cv.type = s->type;
    emit_const_value(cv);
}
int parse_global_init_atom(long *val, char *label, int labelsz);

int try_parse_auto_const_init_value(int type, long *valuep)
{
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    int save_errors;
    int save_long_suffix;
    int save_unsigned_suffix;
    struct Token save_tok;
    struct ConstVal cv;

    if (tok.kind == TOK_ID || tok.kind == TOK_STR || tok.kind == TOK_WSTR)
        return 0;

    save_pos = posi;
    save_tok_start = tok_start_pos;
    save_line = line_no;
    save_tok_line = tok_line;
    save_errors = errors;
    save_long_suffix = g_tok_long_suffix;
    save_unsigned_suffix = g_tok_unsigned_suffix;
    save_tok = tok;

    if (try_parse_const_expr_value(&cv) &&
        (tok.kind == ',' || tok.kind == '}') &&
        errors == save_errors) {
        cf_cast_to_type(&cv, type);
        valuep[0] = (long)cv.u;
        return 1;
    }

    posi = save_pos;
    tok_start_pos = save_tok_start;
    line_no = save_line;
    tok_line = save_tok_line;
    errors = save_errors;
    g_tok_long_suffix = save_long_suffix;
    g_tok_unsigned_suffix = save_unsigned_suffix;
    tok = save_tok;
    return 0;
}

void emit_store_const_to_local_array_elem(struct Sym *s, int elem_type, int index, long v)
{
    int elem_size;

    elem_size = type_size(elem_type);
    if (elem_size <= 0) elem_size = 2;

    emit_load_sym_addr(s);
    emit_add_const_to_hl((long)index * elem_size);
    emit("\tpush hl\n");

    if (type_size(elem_type) == 4) {
        unsigned long uv;
        uv = (unsigned long)v;
        fprintf(outf, "\tld hl,%lu\n", uv & 0xffffUL);
        fprintf(outf, "\tld de,%lu\n", (uv >> 16) & 0xffffUL);
        emit_store_de_to_addr_hl(elem_type);
    } else {
        fprintf(outf, "\tld hl,%ld\n", v & 0xffffL);
        emit("\tex de,hl\n\tpop hl\n");
        emit_store_de_to_addr_hl(elem_type);
    }
}

void emit_store_const_to_local_offset(struct Sym *s, int off, int type, long v)
{
    emit_load_sym_addr(s);
    emit_add_const_to_hl(off);
    emit("\tpush hl\n");

    if (type_size(type) == 4) {
        unsigned long uv;
        uv = (unsigned long)v;
        fprintf(outf, "\tld hl,%lu\n", uv & 0xffffUL);
        fprintf(outf, "\tld de,%lu\n", (uv >> 16) & 0xffffUL);
        emit_store_de_to_addr_hl(type);
    } else {
        fprintf(outf, "\tld hl,%ld\n", v & 0xffffL);
        emit("\tex de,hl\n\tpop hl\n");
        emit_store_de_to_addr_hl(type);
    }
}

void emit_store_expr_to_local_offset(struct Sym *s, int off, int type)
{
    emit_load_sym_addr(s);
    emit_add_const_to_hl(off);
    emit("\tpush hl\n");

    ast_emit_init_expr();

    if (type_is_long(type)) {
        if (type_is_float(g_expr_type))
            emit_convert_float_to_intlike(type);
        else if (!type_is_long(g_expr_type))
            emit_extend_to_long_typed(g_expr_type);
        emit_store_de_to_addr_hl(type);
    } else if (type_is_float(type)) {
        if (!type_is_float(g_expr_type))
            emit_convert_int_to_float(g_expr_type);
        emit_store_de_to_addr_hl(type);
    } else {
        if (type_is_float(g_expr_type))
            emit_convert_float_to_intlike(type);
        else if (type_size(type) > 1 && !type_is_long(g_expr_type))
            emit_promote_byte_to_int(g_expr_type);
        emit("\tex de,hl\n\tpop hl\n");
        emit_store_de_to_addr_hl(type);
    }
}

void emit_store_expr_to_local_array_elem(struct Sym *s, int elem_type, int index)
{
    int elem_size;

    elem_size = type_size(elem_type);
    if (elem_size <= 0) elem_size = 2;
    emit_store_expr_to_local_offset(s, (long)index * elem_size, elem_type);
}

void emit_zero_local_bytes(struct Sym *s, int off, int count)
{
    int i;
    for (i = 0; i < count; ++i)
        emit_store_const_to_local_offset(s, off + i, TYPE_CHAR | TYPE_UNSIGNED, 0);
}

void emit_init_auto_char_array_at_offset_from_string(struct Sym *s, int baseoff, int count, const char *str)
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
        emit_store_const_to_local_offset(s, baseoff + i, TYPE_CHAR | TYPE_UNSIGNED,
                                         (unsigned char)str[i]);

    while (i < count) {
        emit_store_const_to_local_offset(s, baseoff + i, TYPE_CHAR | TYPE_UNSIGNED, 0);
        i++;
    }
}

void emit_init_auto_struct_type(struct Sym *s, int baseoff, int type);
void skip_initializer_or_decl_tail(void);

void emit_init_auto_struct_scalar(struct Sym *s, int off, int type)
{
    long v;
    int k;
    char label[64];

    if ((type & 15) == TYPE_FLOAT && type_ptr_depth(type) == 0) {
        unsigned long bits;
        if (parse_float_init_literal(&bits))
            emit_store_const_to_local_offset(s, off, type, (long)bits);
        else
            emit_store_expr_to_local_offset(s, off, type);
        return;
    }

    (void)k;
    (void)label;
    if (try_parse_auto_const_init_value(type, &v))
        emit_store_const_to_local_offset(s, off, type, v);
    else
        emit_store_expr_to_local_offset(s, off, type);
}

void emit_init_auto_struct_array(struct Sym *s, int baseoff, int elem_type, int count, int elem_size)
{
    int n;
    int total_bytes;

    if (elem_size <= 0) elem_size = type_size(elem_type);
    if (elem_size <= 0) elem_size = 2;

    if ((elem_type & 15) == TYPE_CHAR && type_ptr_depth(elem_type) == 0 &&
        tok.kind == TOK_STR) {
        char *lit;
        int is_wide;
        lit = read_adjacent_string_literals_ex(&is_wide);
        if (is_wide)
            error_here("wide string cannot initialize char array field");
        else
            emit_init_auto_char_array_at_offset_from_string(s, baseoff, count, lit);
        free(lit);
        return;
    }

    if (tok.kind == '{')
        next_token();

    n = 0;
    while (tok.kind != TOK_EOF && tok.kind != '}') {
        if (count > 0 && n >= count) {
            error_here("too many initializer elements");
            skip_initializer_or_decl_tail();
            break;
        }

        if ((elem_type & TYPE_STRUCT) && type_ptr_depth(elem_type) == 0)
            emit_init_auto_struct_type(s, baseoff + n * elem_size, elem_type);
        else
            emit_init_auto_struct_scalar(s, baseoff + n * elem_size, elem_type);

        n++;
        if (!accept(',')) break;
        if (tok.kind == '}') break;
    }
    expect('}');

    if (count > 0 && n < count) {
        total_bytes = (count - n) * elem_size;
        emit_zero_local_bytes(s, baseoff + n * elem_size, total_bytes);
    }
}


long parse_struct_init_const_value(void)
{
    long v;
    char label[64];
    int k;

    k = parse_global_init_atom(&v, label, sizeof(label));
    if (k != 1) {
        error_here("bitfield initializer must be constant integer");
        if (tok.kind != ',' && tok.kind != '}')
            next_token();
        return 0;
    }
    return v;
}

unsigned int bitfield_init_part(struct FieldDef *fd, long v)
{
    unsigned long mask;

    if (fd->bit_width <= 0)
        return 0;

    mask = (1UL << fd->bit_width) - 1UL;
    return (unsigned int)(((unsigned long)v & mask) << fd->bit_shift);
}

int next_parent_field_index(int sid, int start)
{
    int k;
    for (k = start; k < nfield_defs; ++k)
        if (field_defs[k].parent_struct_id == sid)
            return k;
    return -1;
}

void emit_store_const_bitfield_unit_to_local(struct Sym *s, int off, unsigned int unit)
{
    emit_store_const_to_local_offset(s, off, TYPE_UNSIGNED | TYPE_INT, (long)(unit & 0xffffU));
}

void emit_init_auto_struct_type(struct Sym *s, int baseoff, int type)
{
    int sid;
    int i;
    int used;
    int total;
    int is_union;
    int had_brace;

    sid = type_struct_id(type);
    total = type_size(type);
    used = 0;
    is_union = (sid > 0 && sid <= nstruct_defs && struct_defs[sid - 1].is_union);

    had_brace = 0;
    if (tok.kind == '{') {
        next_token();
        had_brace = 1;
    }

    if (is_union) {
        struct FieldDef *first;
        first = NULL;
        for (i = 0; i < nfield_defs; ++i) {
            if (field_defs[i].parent_struct_id == sid) {
                first = &field_defs[i];
                break;
            }
        }

        if (first && tok.kind != TOK_EOF && tok.kind != '}') {
            if (first->is_array)
                emit_init_auto_struct_array(s, baseoff, first->elem_type, first->array_len, first->elem_size);
            else if ((first->type & TYPE_STRUCT) && type_ptr_depth(first->type) == 0)
                emit_init_auto_struct_type(s, baseoff, first->type);
            else
                emit_init_auto_struct_scalar(s, baseoff, first->type);
            used = first->size;

            /* Only a braced union element (e.g. {{1},{2}}) may carry extra
             * members; a braceless element in an array (U a[] = {1,2,3}) ends
             * at its single initializer and the array loop owns the comma. */
            if (had_brace && accept(',')) {
                if (tok.kind != '}') {
                    error_here("too many union initializer elements");
                    while (tok.kind != TOK_EOF && tok.kind != '}') {
                        skip_initializer_or_decl_tail();
                        if (tok.kind == ',') next_token();
                        else break;
                    }
                }
            }
        }

        if (had_brace)
            expect('}');
        if (total > used)
            emit_zero_local_bytes(s, baseoff + used, total - used);
        return;
    }

    for (i = 0; i < nfield_defs && tok.kind != TOK_EOF && tok.kind != '}'; ++i) {
        struct FieldDef *fd;
        fd = &field_defs[i];
        if (fd->parent_struct_id != sid)
            continue;

        if (fd->offset > used)
            emit_zero_local_bytes(s, baseoff + used, fd->offset - used);

        if (fd->bit_width > 0) {
            int unit_off;
            int k;
            int next;
            unsigned int unit;
            int stop;

            unit_off = fd->offset;
            unit = 0;
            stop = 0;
            k = i;
            while (k >= 0 && k < nfield_defs && tok.kind != TOK_EOF && tok.kind != '}') {
                struct FieldDef *bfd;
                bfd = &field_defs[k];
                if (bfd->parent_struct_id == sid) {
                    if (bfd->bit_width <= 0 || bfd->offset != unit_off)
                        break;
                    unit |= bitfield_init_part(bfd, parse_struct_init_const_value());
                    if (!accept(',')) {
                        stop = 1;
                        break;
                    }
                    if (tok.kind == '}') {
                        stop = 1;
                        break;
                    }
                }
                next = next_parent_field_index(sid, k + 1);
                if (next < 0) {
                    if (tok.kind != '}') {
                        error_here("too many initializer elements");
                        while (tok.kind != TOK_EOF && tok.kind != '}') {
                            skip_initializer_or_decl_tail();
                            if (tok.kind == ',') next_token();
                            else break;
                        }
                    }
                    stop = 1;
                    break;
                }
                k = next;
            }
            emit_store_const_bitfield_unit_to_local(s, baseoff + unit_off, unit);
            used = unit_off + 2;
            if (k > i)
                i = k - 1;
            if (stop)
                break;
            continue;
        }

        if (fd->is_array)
            emit_init_auto_struct_array(s, baseoff + fd->offset, fd->elem_type, fd->array_len, fd->elem_size);
        else if ((fd->type & TYPE_STRUCT) && type_ptr_depth(fd->type) == 0)
            emit_init_auto_struct_type(s, baseoff + fd->offset, fd->type);
        else
            emit_init_auto_struct_scalar(s, baseoff + fd->offset, fd->type);

        used = fd->offset + fd->size;
        if (!accept(',')) break;
        if (tok.kind == '}') break;
    }
    expect('}');

    if (total > used)
        emit_zero_local_bytes(s, baseoff + used, total - used);
}

void emit_init_auto_struct_from_list(struct Sym *s)
{
    emit_init_auto_struct_type(s, 0, s->type);
}

void emit_init_auto_struct_array_from_list(struct Sym *s)
{
    int leaf_size;
    int leaf_count;

    /*
     * Struct-array initializers are written in dcc's flattened form (one brace
     * group per leaf struct), so drive the contiguous element loop by the TOTAL
     * number of leaf structs (product of every dimension) and the size of a
     * single leaf struct -- not the first-dimension count and row stride.  For
     * a 1-D array this is identical to array_len / element size; for a
     * multidimensional local array (e.g. Pair grid[][2]) it lays every leaf
     * struct down contiguously instead of stopping after the first row.
     */
    leaf_size = type_size(s->type);
    if (leaf_size <= 0) leaf_size = 2;
    leaf_count = sym_array_total_elems(s);
    if (leaf_count <= 0) leaf_count = s->array_len;
    emit_init_auto_struct_array(s, 0, s->type, leaf_count, leaf_size);
}

int sym_array_elems_from_level(struct Sym *s, int level)
{
    int i;
    int n;

    if (s->dim_count <= 0)
        return s->array_len;

    if (level < 0)
        level = 0;
    if (level >= s->dim_count)
        return 1;

    n = 1;
    for (i = level; i < s->dim_count; ++i) {
        if (s->dims[i] <= 0)
            return s->array_len;
        n *= s->dims[i];
    }
    return n;
}

int sym_array_total_elems(struct Sym *s)
{
    return sym_array_elems_from_level(s, 0);
}

void emit_init_auto_array_scalar(struct Sym *s, int elem_type, int *np)
{
    long v;
    int k;
    int n;
    char label[64];

    n = np[0];
    if (s->array_len > 0 && n >= s->array_len) {
        error_here("too many initializer elements");
        if (tok.kind != ',' && tok.kind != '}')
            next_token();
        return;
    }

    if ((elem_type & 15) == TYPE_FLOAT && type_ptr_depth(elem_type) == 0) {
        unsigned long bits;
        if (parse_float_init_literal(&bits))
            emit_store_const_to_local_array_elem(s, elem_type, n, (long)bits);
        else
            emit_store_expr_to_local_array_elem(s, elem_type, n);
    } else {
        (void)k;
        (void)label;
        if (try_parse_auto_const_init_value(elem_type, &v))
            emit_store_const_to_local_array_elem(s, elem_type, n, v);
        else
            emit_store_expr_to_local_array_elem(s, elem_type, n);
    }

    np[0] = n + 1;
}

void emit_init_auto_array_level(struct Sym *s, int elem_type, int *np, int level)
{
    int start;
    int limit;

    if (!accept('{')) {
        emit_init_auto_array_scalar(s, elem_type, np);
        return;
    }

    start = np[0];
    limit = start + sym_array_elems_from_level(s, level);

    while (tok.kind != TOK_EOF && tok.kind != '}') {
        if (tok.kind == '{' && s->dim_count > 0 && level + 1 < s->dim_count)
            emit_init_auto_array_level(s, elem_type, np, level + 1);
        else
            emit_init_auto_array_scalar(s, elem_type, np);

        if (!accept(','))
            break;
        if (tok.kind == '}')
            break;
    }
    expect('}');

    while (np[0] < limit) {
        emit_store_const_to_local_array_elem(s, elem_type, np[0], 0);
        np[0] = np[0] + 1;
    }
}

void emit_init_auto_array_from_list(struct Sym *s, int elem_type)
{
    int n;
    int total;

    n = 0;
    emit_init_auto_array_level(s, elem_type, &n, 0);

    total = sym_array_total_elems(s);
    while (total > 0 && n < total) {
        emit_store_const_to_local_array_elem(s, elem_type, n, 0);
        n++;
    }
}

void emit_alloc_auto_vla(struct Sym *s, int elem_type)
{
    struct Sym *bound;
    int elem_size;

    if (!s || g_last_array_vla_bound_name[0] == 0)
        return;

    bound = find_sym(g_last_array_vla_bound_name);
    if (!bound) {
        error_here("unknown VLA bound");
        return;
    }

    elem_size = type_size(elem_type);
    if (elem_size <= 0)
        elem_size = 2;

    emit_load_sym_addr(bound);
    emit("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n");
    scale_hl_by_elem_size(elem_size);
    emit("\tld d,h\n\tld e,l\n");
    emit("\tpush sp\n\tpop hl\n");
    emit("\tor a\n\tsbc hl,de\n\tld sp,hl\n");
    emit("\tld e,l\n\tld d,h\n");
    emit_load_sym_addr(s);
    emit("\tld (hl),e\n\tinc hl\n\tld (hl),d\n");
}

void gen_local_decl_after_type(int base)
{
    int type, bytes, arrlen;
    int total_elems;
    char name[64];
    char source_name[64];
    struct Sym *s;

    for (;;) {
        type = base;

        while (accept('*')) { skip_type_qualifiers(); type = type_add_ptr(type); }

        if (!parse_funcptr_declarator(&type, name, sizeof(name))) {
            if (tok.kind != TOK_ID) {
                error_here("identifier expected");
                break;
            }

            strncpy(name, tok.text, sizeof(name) - 1);
            name[sizeof(name) - 1] = 0;
            next_token();
        }

        strncpy(source_name, name, sizeof(source_name) - 1);
        source_name[sizeof(source_name) - 1] = 0;

        if (g_for_decl_seq >= 0) {
            const char *rn;
            rn = enter_for_decl_rename(name);
            strncpy(name, rn, sizeof(name) - 1);
            name[sizeof(name) - 1] = 0;
        }

        arrlen = g_funcptr_decl_array_len;
        g_funcptr_decl_array_len = 0;
        total_elems = arrlen;
        {
            int first_stride_bytes;
            first_stride_bytes = 0;
            if (arrlen == 0)
                parse_array_declarator_dims(type, &total_elems, &first_stride_bytes, 1);
            else
                total_elems = arrlen;

            arrlen = total_elems;

            /*
             * Local omitted-size arrays need the initializer count before
             * allocation:
             *     char data[] = { 'a', 'b', 0 };
             */
            if (arrlen == 0 && g_last_array_dim_count > 0 && tok.kind == '=') {
                int atoms;
                int inner;
                int di;
                int satoms;

                atoms = count_omitted_array_initializer_atoms();
                inner = 1;
                for (di = 1; di < g_last_array_dim_count; ++di) {
                    if (g_last_array_dims[di] > 0)
                        inner *= g_last_array_dims[di];
                }
                if (inner <= 0) inner = 1;

                /* count_initializer_atoms_level() flattens struct elements to
                 * scalar atoms; divide by the element type's atom count to
                 * recover the number of array elements (1 for scalar types). */
                satoms = type_scalar_atom_count(type);
                if (satoms <= 0) satoms = 1;

                if (atoms > 0) {
                    int elems;
                    if (satoms > 1) {
                        /* Struct elements are always braced; counting the
                         * top-level groups gets the element count right even
                         * for PARTIAL initializers like { {1}, {2}, {3} }
                         * where atoms/satoms would truncate. */
                        elems = count_omitted_array_initializer_top_elems();
                        if (elems <= 0) elems = atoms / satoms;
                    } else {
                        elems = atoms;
                    }
                    if (elems <= 0) elems = atoms;
                    total_elems = elems;
                    arrlen = (elems + inner - 1) / inner;
                    g_last_array_dims[0] = arrlen;
                }
            }

            if (first_stride_bytes > 0) {
                /* stash temporarily in bytes; assigned to Sym below */
                current_field_array_elem_size = first_stride_bytes;
            } else {
                current_field_array_elem_size = 0;
            }
        }
        /* inherit array length from array typedef (e.g. typedef int T[4]) */
        if (arrlen == 0 && g_typedef_array_len > 0) {
            arrlen = g_typedef_array_len;
            total_elems = g_typedef_array_len;
        }

        s = find_local_decl(name);
        if (!s)
            s = try_const_fold_local(name, source_name, type,
                                     total_elems > 0 || g_last_array_dim_count > 0);
        if (!s) {
            bytes = type_size(type);
            if (total_elems > 0)
                bytes = object_array_size(type, total_elems);
            if (g_last_array_had_vla)
                bytes = 2;

            s = add_local_alloc(name, g_last_array_had_vla ? type_add_ptr(type) : type, bytes);
            if (g_last_array_had_vla) {
                s->is_array = 0;
                s->array_len = 0;
                s->elem_size = type_size(type);
                if (s->elem_size <= 0) s->elem_size = 2;
            } else if (arrlen > 0 || g_last_array_dim_count > 0) {
                s->is_array = 1;
                s->array_len = arrlen;
                s->elem_size = current_field_array_elem_size ? current_field_array_elem_size : type_size(type);
                if (s->elem_size <= 0) s->elem_size = 2;
                copy_last_array_dims_to_sym(s);
            } else if (g_ptr_array_dim_count > 0) {
                int pi;
                s->elem_size = g_ptr_array_elem_size;
                s->dim_count = g_ptr_array_dim_count;
                for (pi = 0; pi < 8; ++pi)
                    s->dims[pi] = (pi < g_ptr_array_dim_count) ? g_ptr_array_dims[pi] : 0;
            }
        }
        g_ptr_array_dim_count = 0;
        g_ptr_array_elem_size = 0;

        if (g_last_array_had_vla && s && !s->is_const_value)
            emit_alloc_auto_vla(s, type);

        if (s->is_const_value) {
            if (accept('=')) {
                unsigned long ignored_const_value;
                if (!try_parse_local_const_initializer(type, &ignored_const_value)) {
                    ast_emit_init_expr();
                }
            }
        } else if (accept('=')) {
            if ((type & TYPE_STRUCT) && type_ptr_depth(type) == 0 && tok.kind != '{') {
                error_here("struct initializer list expected");
                skip_initializer_or_decl_tail();
            } else if (s->is_array && (type & 15) == TYPE_CHAR && type_ptr_depth(type) == 0 && tok.kind == TOK_STR) {
                char *lit;
                int is_wide;
                lit = read_adjacent_string_literals_ex(&is_wide);
                if (is_wide)
                    error_here("wide string cannot initialize char array");
                emit_init_auto_char_array_from_string(s, lit);
                free(lit);
            } else if (s->is_array && tok.kind == '{' && (type & TYPE_STRUCT) && type_ptr_depth(type) == 0) {
                emit_init_auto_struct_array_from_list(s);
            } else if (!s->is_array && tok.kind == '{' && (type & TYPE_STRUCT) && type_ptr_depth(type) == 0) {
                emit_init_auto_struct_from_list(s);
            } else if (s->is_array && tok.kind == '{' && !(type & TYPE_STRUCT)) {
                emit_init_auto_array_from_list(s, type);
            } else if (!s->is_array && tok.kind == '{') {
                next_token();
                emit_load_sym_addr(s);
                emit("\tpush hl\n");
                ast_emit_init_expr();
                if (type_is_long(type)) {
                    if (type_is_float(g_expr_type))
                        emit_convert_float_to_intlike(type);
                    else if (!type_is_long(g_expr_type))
                        emit_extend_to_long_typed(g_expr_type);
                    emit_store_de_to_addr_hl(type);
                } else {
                    if (type_is_float(g_expr_type))
                        emit_convert_float_to_intlike(type);
                    else if (type_size(type) > 1 && !type_is_long(g_expr_type))
                        emit_promote_byte_to_int(g_expr_type);
                    emit("\tex de,hl\n\tpop hl\n");
                    emit_store_de_to_addr_hl(type);
                }
                accept(',');
                expect('}');
            } else if (!s->is_array && (type & 15) == TYPE_FLOAT && type_ptr_depth(type) == 0) {
                unsigned long bits;
                if (parse_float_init_literal(&bits)) {
                    emit_load_sym_addr(s);
                    emit("\tpush hl\n");
                    fprintf(outf, "\tld hl,%lu\n", bits & 0xffffUL);
                    fprintf(outf, "\tld de,%lu\n", (bits >> 16) & 0xffffUL);
                    emit_store_de_to_addr_hl(type);
                } else {
                    /* Extension beyond strict C89: allow automatic float
                     * declarations to use expression initializers, e.g.
                     *     float r = 16.0f * f;
                     * This is emitted like a declaration followed by an
                     * assignment.  The constant fast path above stays for
                     * smaller code.
                     */
                    emit_load_sym_addr(s);
                    emit("\tpush hl\n");
                    ast_emit_init_expr();
                    if (!type_is_float(g_expr_type))
                        emit_convert_int_to_float(g_expr_type);
                    emit_store_de_to_addr_hl(type);
                }
            } else {
                emit_load_sym_addr(s);
                emit("\tpush hl\n");
                ast_emit_init_expr();
                if (type_is_long(type)) {
                    /* For long locals, emit_store_de_to_addr_hl pops the
                     * address itself via "pop de", so don't consume it here. */
                    if (type_is_float(g_expr_type))
                        emit_convert_float_to_intlike(type);
                    else if (!type_is_long(g_expr_type))
                        emit_extend_to_long_typed(g_expr_type);
                    emit_store_de_to_addr_hl(type);
                } else {
                    if (type_is_float(g_expr_type))
                        emit_convert_float_to_intlike(type);
                    else if (type_size(type) > 1 && !type_is_long(g_expr_type))
                        emit_promote_byte_to_int(g_expr_type);
                    emit("\tex de,hl\n\tpop hl\n");
                    emit_store_de_to_addr_hl(type);
                }
            }
        }

        if (!accept(',')) break;
    }

    expect(';');
}

