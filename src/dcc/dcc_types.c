/*
 * dcc_types.c - type system and aggregate/typedef parsing.
 *
 * Base-type and declarator parsing (parse_base_type/parse_type), struct/union
 * and typedef tables, bitfield layout, type sizing/promotion/arithmetic
 * helpers, and enum-constant lookup. find_enum_const() is relocated here from
 * the monolith's declaration block (its only caller is the global-init parser).
 *
 * MODULE: compiled as its own translation unit; shared declarations are in dcc.h.
 * Source provenance: monolith src/ddc.c line 467-474 (find_enum_const) then
 * lines 2872-3559.
 */

#include "dcc.h"
int find_enum_const(const char *name)
{
    int i;
    for (i = 0; i < nenum_consts; ++i)
        if (!strcmp(enum_const_names[i], name))
            return i;
    return -1;
}

int is_type_qualifier_token(int k)
{
    return k == TOK_CONST || k == TOK_VOLATILE;
}

int is_restrict_qualifier_token(void)
{
    /* `restrict` is a C99/C11 keyword but dcc's lexer has no dedicated token
     * for it, so it arrives as an identifier.  Treat it as a type qualifier
     * everywhere ordinary const/volatile qualifiers are accepted. */
    return tok.kind == TOK_ID && !strcmp(tok.text, "restrict");
}

void skip_parameter_array_qualifiers(void)
{
    while (is_type_qualifier_token(tok.kind) || is_restrict_qualifier_token() ||
           tok.kind == TOK_STATIC)
        next_token();
}

void skip_type_qualifiers(void)
{
    while (is_type_qualifier_token(tok.kind) || is_restrict_qualifier_token())
        next_token();
}

int parse_type(void);
int is_unsupported_target_type_name(const char *name)
{
    return name && (!strcmp(name, "double") || !strcmp(name, "int64_t") || !strcmp(name, "uint64_t"));
}
int parse_const_int_expr(void);

int type_struct_id(int type)
{
    return (type / 256) & 255;
}

int make_struct_type(int id)
{
    return TYPE_STRUCT | (id * 256);
}

int type_size(int type)
{
    int sid;

    if (type & (TYPE_PTR | TYPE_PTR2)) return 2;
    if (type & TYPE_STRUCT) {
        sid = type_struct_id(type);
        if (sid > 0 && sid <= nstruct_defs)
            return struct_defs[sid - 1].size;
        return 0;
    }
    if ((type & 15) == TYPE_CHAR) return 1;
    if ((type & 15) == TYPE_BOOL) return 1;
    if ((type & 15) == TYPE_VOID) return 0;
    if ((type & 15) == TYPE_LONG) return 4;
    if ((type & 15) == TYPE_FLOAT) return 4;
    return 2;
}

int type_is_long(int type)
{
    if (type & (TYPE_PTR | TYPE_PTR2)) return 0;
    return (type & ~TYPE_UNSIGNED & 15) == TYPE_LONG;
}

int type_is_float(int type)
{
    if (type & (TYPE_PTR | TYPE_PTR2)) return 0;
    return (type & 15) == TYPE_FLOAT;
}

int type_is_bool(int type)
{
    if (type & (TYPE_PTR | TYPE_PTR2)) return 0;
    return (type & 15) == TYPE_BOOL;
}

int object_array_size(int type, int count)
{
    int base_size;

    base_size = type_size(type);
    if (base_size <= 0)
        base_size = 2;

    return base_size * count;
}

/*
 * Number of scalar initializer "atoms" a single object of `type` consumes in a
 * brace initializer.  A scalar (or pointer) is one atom; a struct is the sum of
 * its fields' atoms (recursively); a union is the atoms of its first member
 * (only the first member is brace-initialized in C89).  Array fields multiply
 * by their element count.
 *
 * This is the divisor needed to turn a flattened atom count (from
 * count_initializer_atoms_level) back into a count of array ELEMENTS when the
 * element type is a struct, e.g. `Instr prog[] = {{..},{..}}` has 2 elements
 * but 4 flattened atoms.
 */
int type_scalar_atom_count(int type)
{
    int sid;
    int i;
    int total;
    int is_union;

    if (type & (TYPE_PTR | TYPE_PTR2))
        return 1;
    if (!(type & TYPE_STRUCT))
        return 1;

    sid = type_struct_id(type);
    if (sid <= 0 || sid > nstruct_defs)
        return 1;
    is_union = struct_defs[sid - 1].is_union;

    total = 0;
    for (i = 0; i < nfield_defs; ++i) {
        struct FieldDef *fd;
        int fcount;
        int fatoms;
        int base;
        int d;

        fd = &field_defs[i];
        if (fd->parent_struct_id != sid || fd->is_promoted)
            continue;

        fcount = 1;
        if (fd->is_array) {
            if (fd->dim_count > 0) {
                fcount = 1;
                for (d = 0; d < fd->dim_count; ++d)
                    if (fd->dims[d] > 0)
                        fcount *= fd->dims[d];
            } else if (fd->array_len > 0) {
                fcount = fd->array_len;
            }
        }

        base = fd->is_array ? fd->elem_type : fd->type;
        fatoms = type_scalar_atom_count(base);
        if (fatoms <= 0)
            fatoms = 1;

        if (is_union) {
            total = fcount * fatoms;   /* only the first member is initialized */
            break;
        }
        total += fcount * fatoms;
    }

    if (total <= 0)
        total = 1;
    return total;
}

int type_ptr_depth(int type)
{
    if (type & TYPE_PTR2) return 2;
    if (type & TYPE_PTR) return 1;
    return 0;
}

int type_add_ptr(int type)
{
    if (type & TYPE_PTR)
        return type | TYPE_PTR2;
    return type | TYPE_PTR;
}

int type_decay_ptr(int type)
{
    if (type & TYPE_PTR2)
        return (type & ~TYPE_PTR2);
    if (type & TYPE_PTR)
        return (type & ~TYPE_PTR);
    return type;
}

int type_index_elem_size(int type)
{
    if (type & TYPE_PTR2) return 2;
    if (type & TYPE_PTR) {
        int base = type & 15;
        if (type & TYPE_STRUCT)
            return type_size(type & ~(TYPE_PTR | TYPE_PTR2));
        if (base == TYPE_BOOL) return 1;
        if (base == TYPE_CHAR) return 1;
        if (base == TYPE_VOID) return 1;
        if (base == TYPE_LONG) return 4;
        if (base == TYPE_FLOAT) return 4;
        return 2;
    }
    return type_size(type);
}


void copy_last_array_dims_to_sym(struct Sym *s)
{
    int i;

    s->dim_count = g_last_array_dim_count;
    for (i = 0; i < MAX_ARRAY_DIMS; ++i)
        s->dims[i] = (i < g_last_array_dim_count) ? g_last_array_dims[i] : 0;
}

int sym_array_inner_count_from(struct Sym *s, int from_dim)
{
    int i;
    int n;

    n = 1;
    if (!s || s->dim_count <= 0)
        return 1;

    for (i = from_dim; i < s->dim_count; ++i) {
        if (s->dims[i] <= 0)
            return 1;
        n *= s->dims[i];
    }

    return n;
}

int sym_array_index_elem_size(struct Sym *s, int index_count)
{
    int elem;

    elem = type_size(s->type);
    if (elem <= 0)
        elem = 2;

    if (!s || s->dim_count <= 1)
        return s && s->elem_size > 0 ? s->elem_size : elem;

    return sym_array_inner_count_from(s, index_count + 1) * elem;
}

int sym_pointer_array_index_elem_size(struct Sym *s, int cur_type, int index_count)
{
    int elem;

    if (s && !s->is_array && s->dim_count > 0 && s->elem_size > 0) {
        if (index_count == 0)
            return s->elem_size;

        elem = type_size(type_decay_ptr(s->type));
        if (elem <= 0)
            elem = type_index_elem_size(cur_type);
        if (elem <= 0)
            elem = 2;

        if (index_count < s->dim_count)
            return sym_array_inner_count_from(s, index_count) * elem;
    }

    return type_index_elem_size(cur_type);
}

void infer_omitted_first_dim_from_init(struct Sym *s, int init_elems)
{
    int inner;

    if (!s || !s->is_array || s->dim_count <= 0 || s->dims[0] != 0)
        return;

    inner = sym_array_inner_count_from(s, 1);
    if (inner <= 0)
        inner = 1;

    s->dims[0] = (init_elems + inner - 1) / inner;
    s->array_len = s->dims[0];

    if (s->elem_size <= 0) {
        int elem = type_size(s->type);
        if (elem <= 0) elem = 2;
        s->elem_size = inner * elem;
    }
}


int find_struct_def(const char *name)
{
    int i;

    for (i = nstruct_defs - 1; i >= 0; --i)
        if (!strcmp(struct_defs[i].name, name)) return i + 1;

    return 0;
}

int add_struct_def(const char *name)
{
    int id;

    id = find_struct_def(name);
    if (id) return id;

    if (nstruct_defs >= MAX_STRUCTS)
        fatal("too many structs");

    id = ++nstruct_defs;
    memset(&struct_defs[id - 1], 0, sizeof(struct_defs[id - 1]));
    dcc_copy_str(struct_defs[id - 1].name, sizeof(struct_defs[id - 1].name), name);
    struct_defs[id - 1].first_field = nfield_defs;
    return id;
}

struct FieldDef *find_field_def(int struct_id, const char *field_name)
{
    int i;

    if (struct_id <= 0 || struct_id > nstruct_defs)
        return NULL;

    /* Search all fields by parent_struct_id — handles non-consecutive layout
     * caused by inline anonymous struct/union definitions whose fields are
     * interleaved in the flat field_defs[] array. */
    for (i = 0; i < nfield_defs; ++i) {
        if (field_defs[i].parent_struct_id == struct_id &&
            !strcmp(field_defs[i].name, field_name))
            return &field_defs[i];
    }

    return NULL;
}

static void promote_anonymous_aggregate_fields(int parent_struct_id, struct FieldDef *anon_fd)
{
    int child_sid;
    int i;
    int limit;

    if (anon_fd == NULL || !(anon_fd->type & TYPE_STRUCT) || type_ptr_depth(anon_fd->type) != 0)
        return;

    child_sid = type_struct_id(anon_fd->type);
    if (child_sid <= 0 || child_sid > nstruct_defs)
        return;

    limit = nfield_defs;
    for (i = 0; i < limit; ++i) {
        if (field_defs[i].parent_struct_id != child_sid || field_defs[i].is_anonymous)
            continue;
        if (field_defs[i].name[0] == 0)
            continue;
        if (nfield_defs >= MAX_FIELDS)
            fatal("too many struct fields");

        field_defs[nfield_defs] = field_defs[i];
        field_defs[nfield_defs].parent_struct_id = parent_struct_id;
        field_defs[nfield_defs].offset += anon_fd->offset;
        field_defs[nfield_defs].is_anonymous = 0;
        field_defs[nfield_defs].is_promoted = 1;
        nfield_defs++;
    }
}

void parse_struct_definition(int struct_id)
{
    struct StructDef *sd;
    int ftype;
    char fname[64];
    int bytes;
    int bit_next;
    int bit_unit_offset;

    sd = &struct_defs[struct_id - 1];

    expect('{');

    sd->first_field = nfield_defs;
    sd->field_count = 0;
    sd->size = 0;
    bit_next = 0;
    bit_unit_offset = 0;

    while (tok.kind != TOK_EOF && tok.kind != '}') {
        ftype = parse_type();

        for (;;) {
            int is_funcptr_field;
            int is_anonymous_field;
            int is_unnamed_bitfield;
            int field_index;
            while (accept('*')) { skip_type_qualifiers(); ftype = type_add_ptr(ftype); }

            is_funcptr_field = 0;
            is_anonymous_field = 0;
            is_unnamed_bitfield = 0;
            if (parse_funcptr_declarator(&ftype, fname, sizeof(fname))) {
                is_funcptr_field = 1;
            } else {
                if (tok.kind != TOK_ID) {
                    if (tok.kind == ':' && (ftype & 15) == TYPE_INT &&
                        type_ptr_depth(ftype) == 0) {
                        fname[0] = 0;
                        is_unnamed_bitfield = 1;
                    } else if (tok.kind == ';' && (ftype & TYPE_STRUCT) &&
                               type_ptr_depth(ftype) == 0) {
                        fname[0] = 0;
                        is_anonymous_field = 1;
                    } else {
                        error_here("field name expected");
                        break;
                    }
                } else {
                    dcc_copy_str(fname, sizeof(fname), tok.text);
                    next_token();
                }
            }

            if (tok.kind == ':') {
                int bw;
                next_token();
                bw = parse_const_int_expr();
                if (bw < 0 || bw > 16) {
                    error_here("invalid bitfield width");
                    bw = 1;
                }

                if (nfield_defs >= MAX_FIELDS)
                    fatal("too many struct fields");

                /* First-pass C89 bitfields: pack into 16-bit int units.
                 * A zero-width field forces the next field into a new storage
                 * unit and allocates no field of its own (named or not).  It
                 * still terminates a declarator, so consume the trailing ','
                 * or leave the ';' for the caller.
                 */
                if (bw == 0) {
                    bit_next = 0;
                    if (!accept(',')) break;
                    continue;
                }
                if (bit_next == 0 || bit_next + bw > 16) {
                    bit_unit_offset = sd->is_union ? 0 : sd->size;
                    if (sd->is_union) {
                        if (sd->size < 2) sd->size = 2;
                    } else {
                        sd->size += 2;
                    }
                    bit_next = 0;
                }

                /* An unnamed bit-field reserves bits in the current unit but is
                 * not addressable and does not consume an initializer slot, so
                 * it gets no field_defs entry. */
                if (is_unnamed_bitfield) {
                    bit_next += bw;
                    if (!accept(',')) break;
                    continue;
                }

                memset(&field_defs[nfield_defs], 0, sizeof(field_defs[nfield_defs]));
                dcc_copy_str(field_defs[nfield_defs].name, sizeof(field_defs[nfield_defs].name), fname);
                if ((ftype & 15) != TYPE_INT || type_ptr_depth(ftype) != 0)
                    error_here("bitfield type must be int or unsigned int");
                field_defs[nfield_defs].type = ((ftype & TYPE_UNSIGNED) || g_parse_type_was_enum) ?
                    (TYPE_UNSIGNED | TYPE_INT) : TYPE_INT;
                field_defs[nfield_defs].parent_struct_id = struct_id;
                field_defs[nfield_defs].offset = bit_unit_offset;
                field_defs[nfield_defs].elem_type = TYPE_UNSIGNED | TYPE_INT;
                field_defs[nfield_defs].elem_size = 2;
                field_defs[nfield_defs].size = 2;
                field_defs[nfield_defs].bit_width = bw;
                field_defs[nfield_defs].bit_shift = bit_next;
                field_defs[nfield_defs].bit_mask = (unsigned int)(((1UL << bw) - 1UL) << bit_next);
                nfield_defs++;
                sd->field_count++;
                bit_next += bw;

                if (!accept(',')) break;
                continue;
            }

            if (nfield_defs >= MAX_FIELDS)
                fatal("too many struct fields");

            bit_next = 0;
            bytes = type_size(ftype);

            memset(&field_defs[nfield_defs], 0, sizeof(field_defs[nfield_defs]));
            dcc_copy_str(field_defs[nfield_defs].name, sizeof(field_defs[nfield_defs].name), fname);
            field_defs[nfield_defs].type = ftype;
            field_defs[nfield_defs].parent_struct_id = struct_id;
            /* union: all fields at offset 0; struct: cumulative */
            field_defs[nfield_defs].offset = sd->is_union ? 0 : sd->size;

            field_defs[nfield_defs].elem_type = ftype;
            field_defs[nfield_defs].elem_size = bytes;

            if (is_funcptr_field && g_funcptr_decl_array_len > 0) {
                field_defs[nfield_defs].is_array = 1;
                field_defs[nfield_defs].array_len = g_funcptr_decl_array_len;
                field_defs[nfield_defs].dims[0] = g_funcptr_decl_array_len;
                field_defs[nfield_defs].dim_count = 1;
                bytes *= g_funcptr_decl_array_len;
            }

            while (accept('[')) {
                int flen;
                flen = parse_const_int_expr();
                expect(']');
                field_defs[nfield_defs].is_array = 1;
                if (field_defs[nfield_defs].array_len == 0)
                    field_defs[nfield_defs].array_len = flen;
                if (field_defs[nfield_defs].dim_count < 4)
                    field_defs[nfield_defs].dims[field_defs[nfield_defs].dim_count++] = flen;
                bytes *= flen;
            }

            field_defs[nfield_defs].size = bytes;
            field_defs[nfield_defs].is_anonymous = is_anonymous_field;
            field_index = nfield_defs;
            nfield_defs++;

            sd->field_count++;
            /* union: size = max(field_sizes); struct: cumulative sum */
            if (sd->is_union) {
                if (bytes > sd->size) sd->size = bytes;
            } else {
                sd->size += bytes;
            }

            if (is_anonymous_field)
                promote_anonymous_aggregate_fields(struct_id, &field_defs[field_index]);

            if (!accept(',')) break;
        }

        expect(';');
    }

    expect('}');
}


int find_typedef(const char *name)
{
    int i;

    for (i = ntypedefs - 1; i >= 0; --i)
        if (!strcmp(typedefs[i].name, name)) return i;

    return -1;
}

void add_typedef_name_ex(const char *name, int type, int array_len, int is_func)
{
    int i;
    int pi;

    i = find_typedef(name);
    if (i < 0) {
        if (ntypedefs >= MAX_TYPEDEFS) fatal("too many typedefs");
        i = ntypedefs++;
        memset(&typedefs[i], 0, sizeof(typedefs[i]));
        strncpy(typedefs[i].name, name, sizeof(typedefs[i].name) - 1);
    }

    typedefs[i].type = type;
    typedefs[i].array_len = array_len;
    typedefs[i].is_func = is_func;
    typedefs[i].has_proto = g_funcptr_has_proto;
    typedefs[i].proto_nargs = g_funcptr_proto_nargs;
    typedefs[i].proto_variadic = g_funcptr_proto_variadic;
    for (pi = 0; pi < MAX_PROTO_PARAMS; ++pi)
        typedefs[i].proto_types[pi] = g_funcptr_proto_types[pi];
}

void add_typedef_name(const char *name, int type, int array_len)
{
    add_typedef_name_ex(name, type, array_len, 0);
}

int parse_base_type(void)
{
    int t;
    int td;
    int saw_any;
    int saw_unsigned;
    int saw_long;
    int saw_long_long;
    int saw_short;
    int saw_char;
    int saw_void;
    int saw_float;
    int saw_bool;
    int storage_class_seen;

    t = 0;
    saw_any = 0;
    saw_unsigned = 0;
    saw_long = 0;
    saw_long_long = 0;
    saw_short = 0;
    saw_char = 0;
    saw_void = 0;
    saw_float = 0;
    saw_bool = 0;
    storage_class_seen = 0;
    g_typedef_array_len = 0;
    g_typedef_is_func = 0;
    g_typedef_has_proto = 0;
    g_typedef_proto_nargs = 0;
    g_typedef_proto_variadic = 0;
    memset(g_typedef_proto_types, 0, sizeof(g_typedef_proto_types));
    decl_is_register = 0;
    decl_is_const = 0;
    decl_is_inline = 0;
    g_parse_type_was_enum = 0;

    /* C89 declaration specifiers are order-independent. */
    for (;;) {
        if (tok.kind == TOK_REGISTER) {
            if (storage_class_seen)
                error_here("multiple storage classes in declaration");
            storage_class_seen = 1;
            decl_is_register = 1;
            next_token();
            continue;
        }
        if (tok.kind == TOK_CONST) { decl_is_const = 1; next_token(); continue; }
        if (tok.kind == TOK_INLINE) { decl_is_inline = 1; next_token(); continue; }
        if (tok.kind == TOK_VOLATILE ||
            tok.kind == TOK_AUTO) {
            if (tok.kind == TOK_AUTO) {
                if (storage_class_seen)
                    error_here("multiple storage classes in declaration");
                storage_class_seen = 1;
            }
            next_token();
            continue;
        }
        if (tok.kind == TOK_EXTERN) {
            if (storage_class_seen)
                error_here("multiple storage classes in declaration");
            storage_class_seen = 1;
            decl_is_extern = 1;
            next_token();
            continue;
        }
        if (tok.kind == TOK_STATIC) {
            if (storage_class_seen)
                error_here("multiple storage classes in declaration");
            storage_class_seen = 1;
            decl_is_static = 1;
            next_token();
            continue;
        }
        if (tok.kind == TOK_UNSIGNED) { saw_unsigned = 1; saw_any = 1; next_token(); continue; }
        if (tok.kind == TOK_SIGNED) { saw_any = 1; next_token(); continue; }
        if (tok.kind == TOK_LONG) {
            if (saw_long && !saw_long_long) {
                error_here("long long is not supported by dcc's CP/M/Z80 target; use long");
                saw_long_long = 1;
            }
            saw_long = 1;
            saw_any = 1;
            next_token();
            continue;
        }
        if (tok.kind == TOK_SHORT) { saw_short = 1; saw_any = 1; next_token(); continue; }
        if (tok.kind == TOK_INT) { saw_any = 1; next_token(); continue; }
        if (tok.kind == TOK_FLOAT) { saw_float = 1; saw_any = 1; next_token(); continue; }
        if (tok.kind == TOK_BOOL) { saw_bool = 1; saw_any = 1; next_token(); continue; }
        if (tok.kind == TOK_CHAR) { saw_char = 1; saw_any = 1; next_token(); continue; }
        if (tok.kind == TOK_VOID) { saw_void = 1; saw_any = 1; next_token(); continue; }

        if (tok.kind == TOK_STRUCT || tok.kind == TOK_UNION) {
            int sid;
            char sname[64];
            int is_union_kw;
            is_union_kw = (tok.kind == TOK_UNION);
            next_token();
            if (tok.kind == TOK_ID) {
                dcc_copy_str(sname, sizeof(sname), tok.text);
                next_token();
                sid = add_struct_def(sname);
            } else if (tok.kind == '{') {
                sprintf(sname, "__anon_%d", ++g_anon_struct_counter);
                sid = add_struct_def(sname);
            } else {
                error_here("struct/union name or '{' expected");
                sprintf(sname, "__anon_%d", ++g_anon_struct_counter);
                sid = add_struct_def(sname);
            }
            if (is_union_kw) struct_defs[sid - 1].is_union = 1;
            if (tok.kind == '{') parse_struct_definition(sid);
            t = make_struct_type(sid);
            saw_any = 1;
            break;
        }

        if (tok.kind == TOK_ENUM) {
            int cur_val;
            cur_val = 0;
            next_token();
            if (tok.kind == TOK_ID) next_token();
            if (tok.kind == '{') {
                next_token();
                while (tok.kind != '}' && tok.kind != TOK_EOF) {
                    char ename[64];
                    int ei;
                    int dup;
                    if (tok.kind != TOK_ID) {
                        error_here("enum constant name expected");
                        while (tok.kind != TOK_EOF && tok.kind != '}')
                            next_token();
                        break;
                    }
                    dcc_copy_str(ename, sizeof(ename), tok.text);
                    next_token();

                    /* C89 enumerator values are integer constant expressions,
                     * not just bare numeric literals.  This accepts forms such
                     * as B = A + 2, C = (1 << 4), D = sizeof(int), and
                     * negative expressions. */
                    if (accept('=')) {
                        cur_val = parse_enum_const_value();
                        if (tok.kind != ',' && tok.kind != '}') {
                            while (tok.kind != TOK_EOF && tok.kind != '}')
                                next_token();
                        }
                    }

                    dup = 0;
                    for (ei = 0; ei < nenum_consts; ++ei) {
                        if (!strcmp(enum_const_names[ei], ename)) {
                            error_here("duplicate enum constant");
                            dup = 1;
                            break;
                        }
                    }
                    if (!dup && nenum_consts < MAX_ENUM_CONSTS) {
                        dcc_copy_str(enum_const_names[nenum_consts], sizeof(enum_const_names[nenum_consts]), ename);
                        enum_const_values[nenum_consts] = cur_val;
                        nenum_consts++;
                    }
                    cur_val++;
                    if (!accept(',')) break;
                    /* Be liberal for common code that leaves a trailing comma
                     * before the closing brace. */
                    if (tok.kind == '}') break;
                }
                expect('}');
            }
            t = TYPE_INT;
            g_parse_type_was_enum = 1;
            saw_any = 1;
            break;
        }

        if (!saw_any && tok.kind == TOK_ID && !strcmp(tok.text, "double")) {
            error_here("double is not supported by dcc's CP/M/Z80 target; use float");
            saw_float = 1;
            saw_any = 1;
            next_token();
            continue;
        }

        if (!saw_any && tok.kind == TOK_ID &&
            (!strcmp(tok.text, "int64_t") || !strcmp(tok.text, "uint64_t"))) {
            error_here("64-bit integer types are not supported by dcc's CP/M/Z80 target; use long");
            saw_long = 1;
            saw_any = 1;
            next_token();
            continue;
        }

        if (!saw_any && tok.kind == TOK_ID && (td = find_typedef(tok.text)) >= 0) {
            t = typedefs[td].type;
            g_typedef_array_len = typedefs[td].array_len;
            g_typedef_is_func = typedefs[td].is_func;
                 g_typedef_has_proto = typedefs[td].has_proto;
                 g_typedef_proto_nargs = typedefs[td].proto_nargs;
                 g_typedef_proto_variadic = typedefs[td].proto_variadic;
                 memcpy(g_typedef_proto_types, typedefs[td].proto_types,
                     sizeof(g_typedef_proto_types));
            saw_any = 1;
            next_token();
            break;
        }
        break;
    }

    if (!saw_any) {
        if (storage_class_seen) {
            t = TYPE_INT;
        } else {
            error_here("type expected");
            t = TYPE_INT;
        }
    } else if (t == 0) {
        if (saw_bool) t = TYPE_BOOL;
        else if (saw_float) t = TYPE_FLOAT;
        else if (saw_void) t = TYPE_VOID;
        else if (saw_char) t = TYPE_CHAR;
        else if (saw_long) t = TYPE_LONG;
        else t = TYPE_INT;
        (void)saw_short;
        if (saw_unsigned && t != TYPE_FLOAT && t != TYPE_VOID)
            t |= TYPE_UNSIGNED;
    }
    skip_type_qualifiers();
    return t;
}

int parse_type(void)
{
    int t;
    t = parse_base_type();
    while (accept('*')) {
        skip_type_qualifiers();
        t = type_add_ptr(t);
    }
    return t;
}

void skip_type_name_param_list(void)
{
    int depth;

    depth = 1;
    next_token();
    while (tok.kind != TOK_EOF && depth > 0) {
        if (tok.kind == '(')
            depth++;
        else if (tok.kind == ')')
            depth--;
        next_token();
    }
}

int parse_type_name_decl(int *typep, int *sizep)
{
    int t;
    int sz;
    int n;
    int saw_paren_ptr;
    int size_is_pointer_object;

    t = parse_base_type();
    size_is_pointer_object = 0;
    sz = type_size(t);
    if (g_typedef_array_len > 0)
        sz = object_array_size(t, g_typedef_array_len);
    if (sz <= 0)
        sz = 1;

    while (accept('*')) {
        skip_type_qualifiers();
        t = type_add_ptr(t);
        sz = 2;
    }

    if (tok.kind == '(') {
        next_token();
        skip_type_qualifiers();
        saw_paren_ptr = 0;
        while (accept('*')) {
            skip_type_qualifiers();
            saw_paren_ptr = 1;
        }
        if (tok.kind == TOK_ID)
            next_token();
        if (tok.kind == ')') {
            next_token();
            if (saw_paren_ptr) {
                t = type_add_ptr(t);
                sz = 2;
                size_is_pointer_object = 1;
            }
        } else {
            while (tok.kind != TOK_EOF && tok.kind != ')')
                next_token();
            if (tok.kind == ')')
                next_token();
            if (saw_paren_ptr) {
                t = type_add_ptr(t);
                sz = 2;
                size_is_pointer_object = 1;
            }
        }
    } else if (tok.kind == TOK_ID) {
        /* Also accept the same helper for declarations with a concrete name.
         * sizeof(type) normally uses an abstract declarator, but accepting an
         * identifier here lets the cast parser reuse the helper for old DCC
         * function-pointer forms without changing ordinary expression parsing.
         */
        next_token();
    }

    for (;;) {
        if (tok.kind == '[') {
            next_token();
            n = 0;
            if (tok.kind != ']')
                n = parse_const_int_expr();
            expect(']');
            if (n < 0)
                n = 0;
            if (n == 0)
                n = 1;
            /* In an abstract declarator such as char (*)[4], the [4]
             * qualifies the pointee, not the object being sized.  Keep the
             * result pointer-sized.  Without this, sizeof(char (*)[4]) was
             * incorrectly computed as 2 * 4.
             */
            if (!size_is_pointer_object)
                sz *= n;
        } else if (tok.kind == '(') {
            /* Function type suffix.  Function designators are pointer-sized
             * only when the declarator already introduced a pointer, e.g.
             *     sizeof(int (*)(int))
             * Plain sizeof(function type) is invalid C; keep a small, safe
             * size so DCC can continue after the diagnostic-free parse.
             */
            skip_type_name_param_list();
            if (t & (TYPE_PTR | TYPE_PTR2))
                sz = 2;
            else if (sz <= 0)
                sz = 1;
        } else {
            break;
        }
    }

    typep[0] = t;
    sizep[0] = sz;
    return 1;
}

int parse_sizeof_expr_operand(void);
long parse_const_long_expr(void);

