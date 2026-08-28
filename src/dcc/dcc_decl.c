/**
 * @file dcc_decl.c
 * @brief Parses function-local declarations and their initialization plans.
 *
 * @par Role
 * Allocates automatic objects, folds eligible constant locals, handles scalar,
 * array, aggregate, bitfield, string, and VLA initialization, and keeps local
 * declaration replay consistent with frame sizing and MIR capture.
 *
 * @par Key entry points
 * gen_local_decl_after_type(), try_const_fold_local(),
 * emit_init_auto_struct_from_list(), emit_init_auto_array_from_list(), and
 * emit_zero_local_bytes().
 *
 * @par Boundary
 * File-scope initializers are recorded by dcc_global_init.c. Expression trees
 * and metadata come from the AST modules, while selected MIR candidates alone
 * provide production function bodies.
 */

#include "dcc.h"
#include "dcc_mir.h"
#include "dcc_ast.h"

/* One term of a constant float initializer expression: an optional numeric
 * cast applied to a single literal, e.g. `(float)0x100000` or plain `16`.
 * Used only by parse_float_const_expr_value below; declines (returns 0,
 * lexer untouched) on anything else. */
static int parse_float_const_term(double *out)
{
    LexState _ls;
    int cast_type;
    int cast_size;
    double value;

    if (g_lex.tok.kind == '(') {
        _ls = lex_save();
        next_token();
        if (starts_type()) {
            parse_type_name_decl(&cast_type, &cast_size);
            if (g_lex.tok.kind == ')') {
                next_token();
                if (g_lex.tok.kind == TOK_FLOATLIT) {
                    union { float f; unsigned long b; } u;
                    u.b = parse_float_literal_bits(g_lex.tok.text);
                    value = (double)u.f;
                    next_token();
                } else if (g_lex.tok.kind == TOK_NUM || g_lex.tok.kind == TOK_CHARLIT) {
                    value = (double)g_lex.tok.val;
                    next_token();
                } else {
                    lex_restore(&_ls);
                    return 0;
                }
                if (type_is_float(cast_type))
                    value = (double)(float)value;
                else if (cast_type & TYPE_UNSIGNED)
                    value = (double)(unsigned long)(long)value;
                else
                    value = (double)(long)value;
                *out = value;
                return 1;
            }
        }
        lex_restore(&_ls);
        return 0;
    }

    if (g_lex.tok.kind == TOK_FLOATLIT) {
        union { float f; unsigned long b; } u;
        u.b = parse_float_literal_bits(g_lex.tok.text);
        *out = (double)u.f;
        next_token();
        return 1;
    }
    if (g_lex.tok.kind == TOK_NUM || g_lex.tok.kind == TOK_CHARLIT) {
        *out = (double)g_lex.tok.val;
        next_token();
        return 1;
    }
    return 0;
}

/*
 * A small, deliberately narrow constant-float-expression evaluator: a chain
 * of terms (see parse_float_const_term above, each optionally negated)
 * combined by '*' or '/' - e.g. `(float)0x100000 * 0x10`.  '*'/'/' share one
 * precedence level so left-to-right evaluation is exactly C's associativity,
 * unlike mixing in '+'/'-' would be.  This is not a general constant-
 * expression folder (dcc_fold.c's ConstVal evaluator already covers that,
 * but only for integers - it truncates any float operand straight to a
 * long); it exists solely so parse_float_init_literal's compact-initializer
 * fast path recognizes an obviously-constant float initializer that isn't a
 * single literal token, instead of rejecting it with "float initializer
 * must be constant". */
static int parse_float_const_expr_value(double *out)
{
    double value;
    double rhs;
    int sign;
    int op;

    sign = 1;
    if (g_lex.tok.kind == '-') {
        sign = -1;
        next_token();
    } else if (g_lex.tok.kind == '+') {
        next_token();
    }
    if (!parse_float_const_term(&value))
        return 0;
    if (sign < 0)
        value = -value;

    while (g_lex.tok.kind == '*' || g_lex.tok.kind == '/') {
        op = g_lex.tok.kind;
        next_token();
        sign = 1;
        if (g_lex.tok.kind == '-') {
            sign = -1;
            next_token();
        } else if (g_lex.tok.kind == '+') {
            next_token();
        }
        if (!parse_float_const_term(&rhs))
            return 0;
        if (sign < 0)
            rhs = -rhs;
        if (op == '*') {
            value *= rhs;
        } else {
            if (rhs == 0.0)
                return 0;
            value /= rhs;
        }
    }

    *out = value;
    return 1;
}

int parse_float_init_literal(unsigned long *bits)
{
    int sign;
    char lit[MAX_TOK_TEXT + 2];
    LexState _ls;

    /*
     * This helper is only for the compact constant-initializer fast path.
     * Be conservative: if a float literal is followed by an operator, as in
     *     float r = 16.0 * f;
     * then it is not a complete initializer.  Rewind and let gen_expr()
     * compile the full expression.
     */
    _ls = lex_save();

    sign = 1;
    if (g_lex.tok.kind == '-') {
        sign = -1;
        next_token();
    } else if (g_lex.tok.kind == '+') {
        next_token();
    }

    if (g_lex.tok.kind == TOK_FLOATLIT) {
        if (sign < 0) {
            lit[0] = '-';
            strncpy(lit + 1, g_lex.tok.text, MAX_TOK_TEXT);
            lit[MAX_TOK_TEXT] = 0;
            bits[0] = parse_float_literal_bits(lit);
        } else {
            bits[0] = parse_float_literal_bits(g_lex.tok.text);
        }
        next_token();

        if (g_lex.tok.kind == ';' || g_lex.tok.kind == ',' || g_lex.tok.kind == '}')
            return 1;

        lex_restore(&_ls);
        return 0;
    }

    if (g_lex.tok.kind == TOK_NUM || g_lex.tok.kind == TOK_CHARLIT) {
        double d;
        union { float f; unsigned char b[4]; } u;
        unsigned long v;
        d = (double)(sign * g_lex.tok.val);
        u.f = (float)d;
        v = ((unsigned long)u.b[0]) |
            ((unsigned long)u.b[1] << 8) |
            ((unsigned long)u.b[2] << 16) |
            ((unsigned long)u.b[3] << 24);
        bits[0] = v;
        next_token();

        if (g_lex.tok.kind == ';' || g_lex.tok.kind == ',' || g_lex.tok.kind == '}')
            return 1;

        lex_restore(&_ls);
        return 0;
    }

    /*
     * Neither bare form matched: a single literal isn't the only shape a
     * human (and C89 6.6) would call constant - `(float)0x100000 * 0x10` is
     * just as knowable at compile time.  Try the narrow constant-expression
     * evaluator above before giving up; it declines (leaving the lexer
     * untouched) on anything that isn't its specific cast/multiply/divide
     * shape, so a genuine runtime expression like `16.0 * f` still falls
     * through to gen_expr() exactly as before.
     */
    lex_restore(&_ls);
    {
        double value;
        if (parse_float_const_expr_value(&value) &&
            (g_lex.tok.kind == ';' || g_lex.tok.kind == ',' || g_lex.tok.kind == '}')) {
            union { float f; unsigned char b[4]; } u;
            u.f = (float)value;
            bits[0] = ((unsigned long)u.b[0]) |
                      ((unsigned long)u.b[1] << 8) |
                      ((unsigned long)u.b[2] << 16) |
                      ((unsigned long)u.b[3] << 24);
            return 1;
        }
    }

    lex_restore(&_ls);
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
    LexState _ls;
    int save_errors;
    int save_long_suffix;
    int save_unsigned_suffix;

    _ls = lex_save();
    save_errors = errors;
    save_long_suffix = g_tok_long_suffix;
    save_unsigned_suffix = g_tok_unsigned_suffix;

    if (type_is_float(type)) {
        unsigned long bits;
        if (parse_float_init_literal(&bits)) {
            valuep[0] = bits;
            return 1;
        }
    } else {
        struct ConstVal cv;
        if (try_parse_const_expr_value(&cv) &&
            (g_lex.tok.kind == ';' || g_lex.tok.kind == ',' || g_lex.tok.kind == '}') &&
            errors == save_errors) {
            cf_cast_to_type(&cv, type);
            valuep[0] = cv.u;
            return 1;
        }
    }

    lex_restore(&_ls);
    errors = save_errors;
    g_tok_long_suffix = save_long_suffix;
    g_tok_unsigned_suffix = save_unsigned_suffix;
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
    LexState _ls;
    int save_errors;
    int save_long_suffix;
    int save_unsigned_suffix;
    unsigned long const_value;
    struct Sym *s;

    if (!g_decl.is_const || g_decl.is_volatile || has_array ||
        !type_is_const_scalar_candidate(type) || g_lex.tok.kind != '=' ||
        local_name_address_taken_ahead(src_name))
        return NULL;

    _ls = lex_save();
    save_errors = errors;
    save_long_suffix = g_tok_long_suffix;
    save_unsigned_suffix = g_tok_unsigned_suffix;

    next_token();   /* consume '=' */
    if (try_parse_local_const_initializer(type, &const_value)) {
        s = add_local_known(store_name, type, SC_LOCAL, 0, 0);
        s->is_const_value = 1;
        s->const_value = const_value;
        return s;
    }

    /* Not a compile-time constant: rewind to the '=' for the caller. */
    lex_restore(&_ls);
    errors = save_errors;
    g_tok_long_suffix = save_long_suffix;
    g_tok_unsigned_suffix = save_unsigned_suffix;
    return NULL;
}

void emit_load_const_sym_value(struct Sym *s)
{
    struct ConstVal cv;

    if (type_is_float(s->type)) {
        emit_load_float_bits(s->const_value);
        g_expr.type = TYPE_FLOAT;
        return;
    }

    cv.u = s->const_value;
    cv.type = s->type;
    emit_const_value(cv);
}
int parse_global_init_atom(long *val, char *label, int labelsz);

int try_parse_auto_const_init_value(int type, long *valuep)
{
    LexState _ls;
    int save_errors;
    int save_long_suffix;
    int save_unsigned_suffix;
    struct ConstVal cv;

    if (g_lex.tok.kind == TOK_ID || g_lex.tok.kind == TOK_STR || g_lex.tok.kind == TOK_WSTR)
        return 0;

    _ls = lex_save();
    save_errors = errors;
    save_long_suffix = g_tok_long_suffix;
    save_unsigned_suffix = g_tok_unsigned_suffix;

    if (try_parse_const_expr_value(&cv) &&
        (g_lex.tok.kind == ',' || g_lex.tok.kind == '}') &&
        errors == save_errors) {
        cf_cast_to_type(&cv, type);
        valuep[0] = (long)cv.u;
        return 1;
    }

    lex_restore(&_ls);
    errors = save_errors;
    g_tok_long_suffix = save_long_suffix;
    g_tok_unsigned_suffix = save_unsigned_suffix;
    return 0;
}

void emit_store_const_to_local_array_elem(struct Sym *s, int elem_type, int index, long v)
{
    int elem_size;

    if (type_is_bool(elem_type))
        v = v ? 1 : 0;

    elem_size = type_size(elem_type);
    if (elem_size <= 0) elem_size = 2;
    mir_capture_init_constant(s, index * elem_size, elem_type, v);
    if (mir_is_active())
        return;

    emit_load_sym_addr(s);
    emit_add_const_to_hl((long)index * elem_size);
    emit("\tpush hl\n");

    if (type_size(elem_type) == 4) {
        unsigned long uv;
        uv = (unsigned long)v;
        fprintf(g_emit_sink.stream, "\tld hl,%lu\n", uv & 0xffffUL);
        fprintf(g_emit_sink.stream, "\tld de,%lu\n", (uv >> 16) & 0xffffUL);
        emit_store_de_to_addr_hl(elem_type);
    } else {
        fprintf(g_emit_sink.stream, "\tld hl,%ld\n", v & 0xffffL);
        emit("\tex de,hl\n\tpop hl\n");
        emit_store_de_to_addr_hl(elem_type);
    }
}

void emit_store_const_to_local_offset(struct Sym *s, int off, int type, long v)
{
    unsigned long uv;

    if (type_is_bool(type))
        v = v ? 1 : 0;
    mir_capture_init_constant(s, off, type, v);
    if (mir_is_active())
        return;

    if (local_offset_can_ix_direct(s, off, type_size(type))) {
        /* Constant initializer at a frame-relative offset that fits
         * (ix+d) directly (the plain scalar case, off == 0, but also an
         * in-range array element or struct member): write the immediate
         * bytes straight to their frame slots - no address computation
         * and no register round-trip needed at all, unlike the generic
         * path below. */
        int d = s->offset + off;
        uv = (unsigned long)v;
        fprintf(g_emit_sink.stream, "\tld (ix%+d),%lu\n", d, uv & 0xffUL);
        if (type_size(type) >= 2)
            fprintf(g_emit_sink.stream, "\tld (ix%+d),%lu\n", d + 1, (uv >> 8) & 0xffUL);
        if (type_size(type) == 4) {
            fprintf(g_emit_sink.stream, "\tld (ix%+d),%lu\n", d + 2, (uv >> 16) & 0xffUL);
            fprintf(g_emit_sink.stream, "\tld (ix%+d),%lu\n", d + 3, (uv >> 24) & 0xffUL);
        }
        return;
    }

    emit_load_sym_addr(s);
    emit_add_const_to_hl(off);
    emit("\tpush hl\n");

    if (type_size(type) == 4) {
        uv = (unsigned long)v;
        fprintf(g_emit_sink.stream, "\tld hl,%lu\n", uv & 0xffffUL);
        fprintf(g_emit_sink.stream, "\tld de,%lu\n", (uv >> 16) & 0xffffUL);
        emit_store_de_to_addr_hl(type);
    } else {
        fprintf(g_emit_sink.stream, "\tld hl,%ld\n", v & 0xffffL);
        emit("\tex de,hl\n\tpop hl\n");
        emit_store_de_to_addr_hl(type);
    }
}

/* Store HL (or DE:HL for a 4-byte type) directly at frame-relative
 * s->offset + off, once local_offset_can_ix_direct has confirmed it fits
 * (ix+d). Mirrors emit_store_hl_to_sym_direct's plain-ix-direct byte
 * layout, generalized to a possibly-nonzero offset (an array element or
 * struct member) rather than just the whole of s's own extent. */
static void emit_store_hl_direct_at(struct Sym *s, int off, int type)
{
    int d = s->offset + off;
    if (type_size(type) == 1) {
        fprintf(g_emit_sink.stream, "\tld (ix%+d),l\n", d);
    } else if (type_size(type) == 4) {
        fprintf(g_emit_sink.stream, "\tld (ix%+d),l\n", d);
        fprintf(g_emit_sink.stream, "\tld (ix%+d),h\n", d + 1);
        fprintf(g_emit_sink.stream, "\tld (ix%+d),e\n", d + 2);
        fprintf(g_emit_sink.stream, "\tld (ix%+d),d\n", d + 3);
    } else {
        fprintf(g_emit_sink.stream, "\tld (ix%+d),l\n", d);
        fprintf(g_emit_sink.stream, "\tld (ix%+d),h\n", d + 1);
    }
}

void emit_store_expr_to_local_offset(struct Sym *s, int off, int type)
{
    /* Fast path: a frame-relative offset (the plain scalar case, off == 0,
     * but also an in-range array element or struct member) that fits
     * (ix+d) directly. The generic path below computes the destination
     * address into HL, pushes it, evaluates the initializer, then stores
     * back through the pushed address - wasteful push/pop-heavy address
     * arithmetic for what a direct store can do once the value is in
     * HL/DE:HL. This is exactly the codegen a separate `T x; x = expr;`
     * assignment already gets via gen_assign_ast's sym_can_ix_direct fast
     * paths; declaration-with-initializer (and array/struct member
     * initializers) never shared it. */
    int fast = local_offset_can_ix_direct(s, off, type_size(type));

    if (!fast && !mir_is_active()) {
        emit_load_sym_addr(s);
        emit_add_const_to_hl(off);
        emit("\tpush hl\n");
    }

    mir_set_init_expression_target(s, off, type);
    ast_emit_init_expr();
    if (mir_is_active())
        return;

    if (type_is_bool(type)) {
        if (!type_is_bool(g_expr.type))
            emit_bool_normalize_hl(g_expr.type);
        if (fast) {
            emit_store_hl_direct_at(s, off, type);
            return;
        }
        emit("\tex de,hl\n\tpop hl\n");
        emit_store_de_to_addr_hl(type);
        return;
    }

    if (type_is_long(type)) {
        if (type_is_float(g_expr.type))
            emit_convert_float_to_intlike(type);
        else if (!type_is_long(g_expr.type))
            emit_extend_to_long_typed(g_expr.type);
        if (fast) {
            emit_store_hl_direct_at(s, off, type);
            return;
        }
        emit_store_de_to_addr_hl(type);
    } else if (type_is_float(type)) {
        if (!type_is_float(g_expr.type))
            emit_convert_int_to_float(g_expr.type);
        if (fast) {
            emit_store_hl_direct_at(s, off, type);
            return;
        }
        emit_store_de_to_addr_hl(type);
    } else {
        if (type_is_float(g_expr.type))
            emit_convert_float_to_intlike(type);
        else if (type_size(type) > 1 && !type_is_long(g_expr.type))
            emit_promote_byte_to_int(g_expr.type);
        if (fast) {
            emit_store_hl_direct_at(s, off, type);
            return;
        }
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

void emit_init_auto_char_array_at_offset_from_string(struct Sym *s, int baseoff, int count, const char *str, int n)
{
    int i;

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
static void emit_init_auto_struct_array_field(struct Sym *s, int baseoff, struct FieldDef *fd);

int emit_init_auto_struct_chained_designator(struct Sym *s, int baseoff, struct FieldDef *fd)
{
    int off;
    int type;
    int elem_size;
    int is_array;
    int count;

    off = baseoff + fd->offset;
    type = fd->is_array ? fd->elem_type : fd->type;
    elem_size = fd->elem_size ? fd->elem_size : type_size(type);
    if (elem_size <= 0) elem_size = 2;
    is_array = fd->is_array;
    count = fd->array_len;

    if (g_lex.tok.kind != '[' && g_lex.tok.kind != '.')
        return 0;

    for (;;) {
        if (g_lex.tok.kind == '[') {
            int idx;
            if (!is_array) {
                error_here("subscripted initializer designator is not an array");
                skip_initializer_or_decl_tail();
                return 1;
            }
            next_token();
            idx = parse_typed_designator_index_expr();
            expect(']');
            if (idx < 0) {
                error_here("negative array initializer designator");
                idx = 0;
            } else if (count > 0 && idx >= count) {
                error_here("array initializer designator out of range");
                idx = 0;
            }
            off += idx * elem_size;
            is_array = 0;
        } else if (g_lex.tok.kind == '.') {
            struct FieldDef *sub;
            int sid;
            if (!((type & TYPE_STRUCT) && type_ptr_depth(type) == 0)) {
                error_here("field initializer designator is not a struct");
                skip_initializer_or_decl_tail();
                return 1;
            }
            next_token();
            if (g_lex.tok.kind != TOK_ID) {
                error_here("expected a field designator, such as '.field = value'");
                skip_initializer_or_decl_tail();
                return 1;
            }
            sid = type_struct_id(type);
            sub = find_field_def(sid, g_lex.tok.text);
            if (sub == NULL) {
                error_here("unknown field initializer designator");
                skip_initializer_or_decl_tail();
                return 1;
            }
            off += sub->offset;
            type = sub->is_array ? sub->elem_type : sub->type;
            elem_size = sub->elem_size ? sub->elem_size : type_size(type);
            if (elem_size <= 0) elem_size = 2;
            is_array = sub->is_array;
            count = sub->array_len;
            fd = sub;
            next_token();
        } else {
            break;
        }
    }

    expect('=');
    if (is_array)
        emit_init_auto_struct_array_field(s, off, fd);
    else if ((type & TYPE_STRUCT) && type_ptr_depth(type) == 0)
        emit_init_auto_struct_type(s, off, type);
    else if (fd->bit_width > 0)
        error_here("bitfield chained initializer designator unsupported");
    else
        emit_init_auto_struct_scalar(s, off, type);
    return 1;
}

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

static int field_array_elems_from_level(struct FieldDef *fd, int level)
{
    int i;
    int n;

    if (fd->dim_count <= 0)
        return fd->array_len;

    if (level < 0)
        level = 0;
    if (level >= fd->dim_count)
        return 1;

    n = 1;
    for (i = level; i < fd->dim_count; ++i) {
        if (fd->dims[i] <= 0)
            return fd->array_len;
        n *= fd->dims[i];
    }
    return n;
}

static void emit_init_auto_struct_array_leaf(struct Sym *s, int off, int elem_type)
{
    if ((elem_type & TYPE_STRUCT) && type_ptr_depth(elem_type) == 0)
        emit_init_auto_struct_type(s, off, elem_type);
    else
        emit_init_auto_struct_scalar(s, off, elem_type);
}

static void emit_init_auto_struct_array_field_level(struct Sym *s, int baseoff,
                                                   struct FieldDef *fd,
                                                   int *np, int level)
{
    int start;
    int limit;
    int maxn;
    int leaf_size;
    int total;
    int had_brace;

    leaf_size = type_size(fd->elem_type);
    if (leaf_size <= 0) leaf_size = 2;

    /* Total number of scalar leaves in the whole array member.  Every store /
     * zero-fill offset is bounded by this so an over-long (already erroneous)
     * initializer can never write past the object. */
    total = field_array_elems_from_level(fd, 0);

    /*
     * A brace at this position opens a sub-aggregate for this level.  When it
     * is absent the initializer is brace-elided (C99 6.7.9p20): consume only
     * as many elements as this level holds, then hand the separating comma and
     * any remaining elements back to the caller so sibling subobjects (or the
     * enclosing struct's later members) get them.  Inner levels are only
     * entered when the current token is '{', so had_brace is effectively
     * always true there; the elided path only matters at the member's top
     * level (e.g. `struct Dist x = { 1, 2, 3, 4 };`).
     */
    had_brace = accept('{');

    start = np[0];
    limit = start + field_array_elems_from_level(fd, level);
    maxn = np[0];

    while (g_lex.tok.kind != TOK_EOF && g_lex.tok.kind != '}') {
        if (g_lex.tok.kind == '[') {
            int idx;
            int span;

            next_token();
            idx = parse_typed_designator_index_expr();
            expect(']');
            expect('=');
            span = field_array_elems_from_level(fd, level + 1);
            if (span <= 0) span = 1;
            if (idx < 0)
                error_here("negative array initializer designator");
            else
                np[0] = start + idx * span;
        }
        if (g_lex.tok.kind == '{' && fd->dim_count > 0 && level + 1 < fd->dim_count)
            emit_init_auto_struct_array_field_level(s, baseoff, fd, np, level + 1);
        else {
            if (total > 0 && np[0] >= total) {
                error_here("too many initializer elements");
                skip_initializer_or_decl_tail();
                break;
            }
            emit_init_auto_struct_array_leaf(s, baseoff + np[0] * leaf_size, fd->elem_type);
            np[0] = np[0] + 1;
        }
        if (np[0] > maxn) maxn = np[0];

        /* Brace-elided level: stop once full, leaving the comma for the
         * caller.  The break happens before consuming the separator so the
         * next sibling starts cleanly. */
        if (!had_brace && np[0] >= limit)
            break;

        if (!accept(','))
            break;
        if (g_lex.tok.kind == '}')
            break;
    }
    if (had_brace)
        expect('}');

    if (maxn > np[0])
        np[0] = maxn;
    while (np[0] < limit && np[0] < total) {
        emit_zero_local_bytes(s, baseoff + np[0] * leaf_size, leaf_size);
        np[0] = np[0] + 1;
    }
}

static void emit_init_auto_struct_array_field(struct Sym *s, int baseoff, struct FieldDef *fd)
{
    int n;
    int total;
    int leaf_size;

    if (fd->dim_count <= 1) {
        emit_init_auto_struct_array(s, baseoff, fd->elem_type, fd->array_len, fd->elem_size);
        return;
    }

    leaf_size = type_size(fd->elem_type);
    if (leaf_size <= 0) leaf_size = 2;

    n = 0;
    emit_init_auto_struct_array_field_level(s, baseoff, fd, &n, 0);

    total = field_array_elems_from_level(fd, 0);
    while (total > 0 && n < total) {
        emit_zero_local_bytes(s, baseoff + n * leaf_size, leaf_size);
        n++;
    }
}

void emit_init_auto_struct_array(struct Sym *s, int baseoff, int elem_type, int count, int elem_size)
{
    int n;
    int maxn;
    int total_bytes;
    int had_brace;

    if (elem_size <= 0) elem_size = type_size(elem_type);
    if (elem_size <= 0) elem_size = 2;

    if ((elem_type & 15) == TYPE_CHAR && type_ptr_depth(elem_type) == 0 &&
        g_lex.tok.kind == TOK_STR) {
        char *lit;
        int is_wide;
        int litlen;
        lit = read_adjacent_string_literals_ex(&is_wide, &litlen);
        if (is_wide)
            error_here("wide string cannot initialize char array field");
        else
            emit_init_auto_char_array_at_offset_from_string(s, baseoff, count, lit, litlen);
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
    while (g_lex.tok.kind != TOK_EOF && g_lex.tok.kind != '}') {
        if (g_lex.tok.kind == '[') {
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

        if ((elem_type & TYPE_STRUCT) && type_ptr_depth(elem_type) == 0)
            emit_init_auto_struct_type(s, baseoff + n * elem_size, elem_type);
        else
            emit_init_auto_struct_scalar(s, baseoff + n * elem_size, elem_type);

        n++;
        if (n > maxn) maxn = n;
        /* With braces elided, exactly one array subobject belongs to this
         * call.  Leave its following comma for the enclosing struct, whose
         * next member must receive the next initializer. */
        if (!had_brace && count > 0 && n >= count)
            break;
        if (!accept(',')) break;
        if (g_lex.tok.kind == '}') break;
    }
    if (had_brace)
        expect('}');

    if (maxn > n) n = maxn;
    if (count > 0 && n < count) {
        total_bytes = (count - n) * elem_size;
        emit_zero_local_bytes(s, baseoff + n * elem_size, total_bytes);
    }
}


/* Set by parse_struct_init_const_value whenever a bit-field element isn't a
 * compile-time constant, in addition to (and regardless of) whether
 * error_here actually reports it - emit_init_auto_struct_type uses this
 * under asm_suppress_depth to silently probe whether an automatic struct's
 * bit-field unit is fully constant (worth the packed single-store fast
 * path) without leaving a diagnostic from a probe that gets re-parsed for
 * real right after. */
static int g_bitfield_const_trial_failed;

long parse_struct_init_const_value(void)
{
    long v;
    char label[64];
    int k;

    k = parse_global_init_atom(&v, label, sizeof(label));
    if (k != 1) {
        g_bitfield_const_trial_failed = 1;
        error_here("bitfield initializer must be constant integer");
        if (g_lex.tok.kind != ',' && g_lex.tok.kind != '}')
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

/* Bit mask a bit-field occupies inside its 16-bit storage unit. */
unsigned int bitfield_field_mask(struct FieldDef *fd)
{
    if (fd->bit_width <= 0)
        return 0;
    return (unsigned int)((((1UL << fd->bit_width) - 1UL) << fd->bit_shift) &
                          0xffffUL);
}

int next_parent_field_index(int sid, int start)
{
    int k;
    for (k = start; k < nfield_defs; ++k)
        if (field_defs[k].parent_struct_id == sid && !field_defs[k].is_promoted)
            return k;
    return -1;
}

/*
 * Shared bit-field storage-unit packer for struct designated initializers,
 * used by BOTH the local (emit) and global (record) init paths so the two
 * cannot drift out of sync (they did once - a fix had to be applied twice).
 *
 * Starting at field index `i` (fd = &field_defs[i], a bit-field), this
 * consumes from the token stream the initializers of every bit-field sharing
 * fd's 16-bit storage unit - including same-unit `.field =` designators - and
 * returns the packed unit value.  It merges with any value previously stored
 * for this unit via the caller-owned tracking arrays
 * (unit_offs[], unit_vals[], *nunits, capacity `cap`), so an out-of-order
 * designator that revisits a unit is last-designator-wins per C99.
 *
 * On return *out_unit_off is the unit's byte offset, *out_k the final field
 * index the packing reached, and *out_stop is 1 when the initializer list
 * ended (no trailing ',').  The CALLER performs the actual unit store (emit a
 * runtime store, or record init bytes) and any `used` bookkeeping, and then
 * the shared `if (k > i) i = k - 1; if (stop) break; ...` tail.
 *
 * allow_promoted_owner: the local path also accepts a promoted bit-field when
 * the unit's owner fd is itself promoted (anonymous struct/union member); the
 * global path does not.
 */
unsigned int pack_struct_bitfield_unit(int sid, int i, struct FieldDef *fd,
                                       int allow_promoted_owner,
                                       int *unit_offs, unsigned int *unit_vals,
                                       int *nunits, int cap,
                                       int *out_unit_off, int *out_k,
                                       int *out_stop)
{
    int unit_off = fd->offset;
    int k = i;
    int next;
    int u;
    int found;
    unsigned int unit = 0;
    unsigned int unit_mask = 0;
    int stop = 0;

    while (k >= 0 && k < nfield_defs && g_lex.tok.kind != TOK_EOF && g_lex.tok.kind != '}') {
        struct FieldDef *bfd = &field_defs[k];
        if (bfd->parent_struct_id == sid &&
            (!bfd->is_promoted || (allow_promoted_owner && fd->is_promoted))) {
            if (bfd->bit_width <= 0 || bfd->offset != unit_off)
                break;
            unit &= ~bitfield_field_mask(bfd);
            unit |= bitfield_init_part(bfd, parse_struct_init_const_value());
            unit_mask |= bitfield_field_mask(bfd);
            if (!accept(',')) {
                stop = 1;
                break;
            }
            if (g_lex.tok.kind == '}') {
                stop = 1;
                break;
            }
        }
        /*
         * A designated element (`.field = ...`) may follow.  When it names
         * another bit-field in the SAME storage unit, keep accumulating it
         * into `unit` so all designators for the unit are packed with a single
         * store (the unit store overwrites, so they cannot be emitted one at a
         * time).  A designator that targets a different unit (or a non-bit-
         * field) is left for the outer field loop; the comma has already been
         * consumed, which is exactly where the outer loop expects to resume.
         */
        if (g_lex.tok.kind == '.') {
            LexState _ls = lex_save();
            struct FieldDef *nf = NULL;

            next_token();
            if (g_lex.tok.kind == TOK_ID)
                nf = find_field_def(sid, g_lex.tok.text);
            if (nf != NULL && nf->bit_width > 0 && nf->offset == unit_off) {
                next_token();
                if (g_lex.tok.kind == '=')
                    next_token();
                else if (g_lex.tok.kind != '[' && g_lex.tok.kind != '.')
                    expect('=');
                k = (int)(nf - field_defs);
                continue;
            }
            lex_restore(&_ls);
            break;
        }
        next = next_parent_field_index(sid, k + 1);
        if (next < 0) {
            if (g_lex.tok.kind != '}') {
                error_here("too many initializer elements");
                while (g_lex.tok.kind != TOK_EOF && g_lex.tok.kind != '}') {
                    skip_initializer_or_decl_tail();
                    if (g_lex.tok.kind == ',') next_token();
                    else break;
                }
            }
            stop = 1;
            break;
        }
        k = next;
    }

    found = -1;
    for (u = 0; u < *nunits; ++u) {
        if (unit_offs[u] == unit_off) {
            found = u;
            break;
        }
    }
    if (found >= 0)
        unit = (unit_vals[found] & ~unit_mask) | unit;
    else if (*nunits < cap) {
        found = (*nunits)++;
        unit_offs[found] = unit_off;
    }
    if (found >= 0)
        unit_vals[found] = unit;

    *out_unit_off = unit_off;
    *out_k = k;
    *out_stop = stop;
    return unit;
}

void emit_init_auto_struct_type(struct Sym *s, int baseoff, int type)
{
    int sid;
    int i;
    int used;
    int total;
    int is_union;
    int had_brace;
    int end_used;
    /* Bit-field storage units already zero-filled at this struct level, for
     * the runtime read-modify-write path below.  Sibling bit-fields share a
     * 16-bit unit; the first one reached (in source or designator order)
     * zero-fills the whole unit so an omitted sibling defaults to 0, and
     * this array remembers which unit offsets already got that treatment
     * so an out-of-order designator revisiting the unit (e.g.
     * `{ .a=1, .x=2, .b=3 }` with a and b sharing a unit) doesn't zero away
     * a field already stored this pass. */
    int bf_unit_offs[32];
    int bf_nunits;
    /* Separate revisit-merge tracking for the all-constant fast path
     * (pack_struct_bitfield_unit's own out-of-order-designator merge, see
     * its comment) - kept apart from bf_unit_offs above since the two
     * paths track different things (zeroed-or-not vs. accumulated value). */
    int bf_const_unit_offs[32];
    unsigned int bf_const_unit_vals[32];
    int bf_const_nunits;

    bf_nunits = 0;
    bf_const_nunits = 0;

    sid = type_struct_id(type);
    total = type_size(type);
    used = 0;
    is_union = (sid > 0 && sid <= nstruct_defs && struct_defs[sid - 1].is_union);

    had_brace = 0;
    if (g_lex.tok.kind == '{') {
        next_token();
        had_brace = 1;
    }

    if (is_union) {
        struct FieldDef *first;
        LexState _ls;
        int child_sid;
        struct FieldDef *candidate;
        first = NULL;

        /* A promoted designator can select an anonymous union member other
         * than the first one.  Resolve that owner before recursively parsing
         * the selected member's initializer. */
        if (g_lex.tok.kind == '.') {
            _ls = lex_save();
            next_token();
            if (g_lex.tok.kind == TOK_ID) {
                for (i = 0; i < nfield_defs; ++i) {
                    candidate = &field_defs[i];
                    if (candidate->parent_struct_id != sid || candidate->is_promoted)
                        continue;
                    if (!strcmp(candidate->name, g_lex.tok.text)) {
                        first = candidate;
                        break;
                    }
                    if ((candidate->type & TYPE_STRUCT) &&
                        type_ptr_depth(candidate->type) == 0) {
                        child_sid = type_struct_id(candidate->type);
                        if (find_field_def(child_sid, g_lex.tok.text) != NULL) {
                            first = candidate;
                            break;
                        }
                    }
                }
            }
            lex_restore(&_ls);
        }
        for (i = 0; i < nfield_defs; ++i) {
            if (first == NULL && field_defs[i].parent_struct_id == sid &&
                !field_defs[i].is_promoted) {
                first = &field_defs[i];
                break;
            }
        }

        if (first && g_lex.tok.kind != TOK_EOF && g_lex.tok.kind != '}') {
            if (g_lex.tok.kind == '.' && first->name[0] != 0) {
                next_token();
                if (g_lex.tok.kind == TOK_ID)
                    next_token();
                expect('=');
            }
            if (first->is_array)
                emit_init_auto_struct_array_field(s, baseoff, first);
            else if ((first->type & TYPE_STRUCT) && type_ptr_depth(first->type) == 0)
                emit_init_auto_struct_type(s, baseoff, first->type);
            else
                emit_init_auto_struct_scalar(s, baseoff, first->type);
            used = first->size;

            /* Only a braced union element (e.g. {{1},{2}}) may carry extra
             * members; a braceless element in an array (U a[] = {1,2,3}) ends
             * at its single initializer and the array loop owns the comma. */
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
        if (total > used)
            emit_zero_local_bytes(s, baseoff + used, total - used);
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
            i = (int)(fd - field_defs);
            next_token();
            if (g_lex.tok.kind == '=') {
                next_token();
            } else if (g_lex.tok.kind != '[' && g_lex.tok.kind != '.') {
                expect('=');
            }
        } else {
            fd = &field_defs[i];
            if (fd->parent_struct_id != sid || fd->is_promoted)
                continue;
        }

        if (fd->offset > used)
            emit_zero_local_bytes(s, baseoff + used, fd->offset - used);

        if (g_lex.tok.kind == '[' || g_lex.tok.kind == '.') {
            if (used <= fd->offset)
                emit_zero_local_bytes(s, baseoff + fd->offset, fd->size);
            if (emit_init_auto_struct_chained_designator(s, baseoff, fd)) {
                end_used = fd->offset + fd->size;
                if (end_used > used) used = end_used;
                if (!accept(',')) break;
                if (g_lex.tok.kind == '}') break;
                if (g_lex.tok.kind == '.') i = -1;
                continue;
            }
        }

        if (fd->bit_width > 0) {
            int j;
            int seen;
            struct AstNode *rhs;
            LexState trial_ls;
            int trial_offs[32];
            unsigned int trial_vals[32];
            int trial_n;
            int trial_unit_off, trial_k, trial_stop;

            /*
             * Bit-fields sharing a unit are usually all-constant (the common
             * case tbitfld.c exercises), which the packed single-store path
             * just below handles in one instruction sequence; silently probe
             * for that first (asm_suppress_depth swallows the diagnostic
             * parse_struct_init_const_value would otherwise raise) and only
             * fall back to the slower but more general per-field runtime
             * read-modify-write path when some field genuinely isn't
             * constant - C89 6.5.7's constant-only rule is for static-
             * storage objects, so an automatic struct's bit-field may be
             * initialized from any expression.
             */
            trial_ls = lex_save();
            trial_n = 0;
            g_bitfield_const_trial_failed = 0;
            asm_suppress_depth++;
            (void)pack_struct_bitfield_unit(sid, i, fd, 1,
                trial_offs, trial_vals, &trial_n,
                (int)(sizeof(trial_offs) / sizeof(trial_offs[0])),
                &trial_unit_off, &trial_k, &trial_stop);
            asm_suppress_depth--;
            lex_restore(&trial_ls);

            if (!g_bitfield_const_trial_failed) {
                int unit_off, k, stop;
                unsigned int unit;

                unit = pack_struct_bitfield_unit(sid, i, fd, 1,
                    bf_const_unit_offs, bf_const_unit_vals, &bf_const_nunits,
                    (int)(sizeof(bf_const_unit_offs) / sizeof(bf_const_unit_offs[0])),
                    &unit_off, &k, &stop);
                emit_store_const_to_local_offset(s, baseoff + unit_off,
                    TYPE_UNSIGNED | TYPE_INT, (long)(unit & 0xffffU));
                end_used = unit_off + 2;
                if (end_used > used) used = end_used;
                if (k > i)
                    i = k - 1;
                if (stop)
                    break;
                if (g_lex.tok.kind == '.')
                    i = -1;
                continue;
            }

            /*
             * Some field in this unit isn't constant: fall back to a
             * per-field runtime read-modify-write store, zero-filling this
             * field's storage unit the first time it's reached (see
             * bf_unit_offs above) so an omitted sibling still defaults to 0
             * - exactly the codegen an ordinary `s.field = expr;` assignment
             * statement already gets, reused here (via
             * mir_capture_bitfield_init_expr, or directly below for the
             * non-MIR fallback) instead of a bespoke implementation.
             */
            seen = 0;
            for (j = 0; j < bf_nunits; ++j)
                if (bf_unit_offs[j] == fd->offset) { seen = 1; break; }
            if (!seen) {
                if (bf_nunits < (int)(sizeof(bf_unit_offs) / sizeof(bf_unit_offs[0])))
                    bf_unit_offs[bf_nunits++] = fd->offset;
                emit_zero_local_bytes(s, baseoff + fd->offset, 2);
            }

            rhs = ast_build_assign_expr(&g_ast_init_arena);
            ast_validate_expr_symbols(rhs);
            if (mir_is_active()) {
                mir_capture_bitfield_init_expr(s, baseoff + fd->offset, fd, rhs);
            } else {
                emit_load_sym_addr(s);
                emit_add_const_to_hl(baseoff + fd->offset);
                emit("\tpush hl\n");
                ast_gen_expr(rhs);
                current_field_bit_width = fd->bit_width;
                current_field_bit_shift = fd->bit_shift;
                current_field_bit_mask = fd->bit_mask;
                emit_store_bitfield_from_hl();
            }
            ast_arena_reset(&g_ast_init_arena);

            end_used = fd->offset + fd->size;
            if (end_used > used) used = end_used;
            if (!accept(','))
                break;
            if (g_lex.tok.kind == '}')
                break;
            if (g_lex.tok.kind == '.')
                i = -1;
            continue;
        }

        if (fd->is_array)
            emit_init_auto_struct_array_field(s, baseoff + fd->offset, fd);
        else if ((fd->type & TYPE_STRUCT) && type_ptr_depth(fd->type) == 0)
            emit_init_auto_struct_type(s, baseoff + fd->offset, fd->type);
        else
            emit_init_auto_struct_scalar(s, baseoff + fd->offset, fd->type);

        end_used = fd->offset + fd->size;
        if (end_used > used) used = end_used;
        /* A braceless struct element nested in an array must leave the comma
         * after its final field for the enclosing array's element loop. */
        if (!had_brace && next_parent_field_index(sid, i + 1) < 0)
            break;
        if (!accept(',')) break;
        if (g_lex.tok.kind == '}') break;
        if (g_lex.tok.kind == '.') i = -1;
    }
    if (had_brace)
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
        if (g_lex.tok.kind != ',' && g_lex.tok.kind != '}')
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
    int maxn;

    if (!accept('{')) {
        emit_init_auto_array_scalar(s, elem_type, np);
        return;
    }

    start = np[0];
    limit = start + sym_array_elems_from_level(s, level);
    maxn = np[0];

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
            else
                np[0] = start + idx * span;
        }
        if (g_lex.tok.kind == '{' && s->dim_count > 0 && level + 1 < s->dim_count)
            emit_init_auto_array_level(s, elem_type, np, level + 1);
        else
            emit_init_auto_array_scalar(s, elem_type, np);
        if (np[0] > maxn) maxn = np[0];

        if (!accept(','))
            break;
        if (g_lex.tok.kind == '}')
            break;
    }
    expect('}');

    if (maxn > np[0])
        np[0] = maxn;
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

/*
 * Allocate a C99 variable-length array at run time.  parse_array_declarator_dims
 * captured the (non-constant) first-dimension expression; re-seek the lexer to
 * it, evaluate it into HL, scale by the element size, carve the block off the
 * stack below SP, and store the resulting base pointer into the VLA's frame
 * slot.  The block is reclaimed at block-scope exit by restoring SP from the
 * scope's hidden `#vlasp` save slot (see emit_vla_save_sp/emit_vla_restore_sp),
 * and by the function epilogue's `ld sp,ix` on any remaining return path.
 */
static void emit_vla_alloc(struct Sym *s)
{
    long r_posi;
    long r_tok_start;
    int r_line;
    int r_tok_line;
    struct Token r_tok;

    r_posi = g_lex.posi;
    r_tok_start = g_lex.tok_start_pos;
    r_line = g_lex.line_no;
    r_tok_line = g_lex.tok_line;
    r_tok = g_lex.tok;

    g_lex.posi = g_vla_dim_posi;
    g_lex.tok_start_pos = g_vla_dim_tok_start;
    g_lex.line_no = g_vla_dim_line;
    g_lex.tok_line = g_vla_dim_tok_line;
    g_lex.tok = g_vla_dim_tok;

    mir_set_vla_target(s);
    ast_emit_init_expr();               /* HL = element count */

    g_lex.posi = r_posi;
    g_lex.tok_start_pos = r_tok_start;
    g_lex.line_no = r_line;
    g_lex.tok_line = r_tok_line;
    g_lex.tok = r_tok;

    if (mir_is_active())
        return;

    if (s->elem_size > 1)
        emit_mul_hl_const((long)s->elem_size);   /* HL = size in bytes */

    if (s->vla_size_offset != 0) {
        emit("\tpush hl\n");
        emit("\tpush ix\n\tpop hl\n");
        fprintf(g_emit_sink.stream, "\tld de,%d\n\tadd hl,de\n", s->vla_size_offset);
        emit("\tpop de\n");
        emit("\tld (hl),e\n\tinc hl\n\tld (hl),d\n");
        emit("\tex de,hl\n");
    }

    /* SP -= size; the new SP is the block base, stored into the slot. */
    emit("\tex de,hl\n");
    emit("\tld hl,0\n");
    emit("\tadd hl,sp\n");
    emit("\tor a\n");
    emit("\tsbc hl,de\n");
    emit("\tld sp,hl\n");
    if (opt_stack_check)
        emit_runtime_call("__stchk");
    emit("\tld hl,0\n");
    emit("\tadd hl,sp\n");
    emit("\tpush hl\n");
    emit_load_frame_addr_hl(s);         /* HL = &slot (may clobber DE) */
    emit("\tpop de\n");                 /* DE = block base */
    emit("\tld (hl),e\n\tinc hl\n\tld (hl),d\n");
}

void gen_local_decl_after_type(int base)
{
    int type, bytes, arrlen;
    int base_is_volatile;
    int base_pointee_is_volatile;
    int total_elems;
    int direct_funcptr;
    int parenthesized_array;
    int parenthesized_total;
    int parenthesized_stride;
    char name[64];
    char source_name[64];
    struct Sym *s;
    int freshly_allocated;
    int narrowed_as_counter;

    base_is_volatile = g_decl.is_volatile;
    base_pointee_is_volatile = g_decl.pointee_is_volatile;

    for (;;) {
        type = base;
        g_decl.is_volatile = base_is_volatile;
        g_decl.pointee_is_volatile = base_pointee_is_volatile;
        direct_funcptr = 0;
        parenthesized_array = 0;
        parenthesized_total = 0;
        parenthesized_stride = 0;

        while (accept('*')) {
            g_decl.pointee_is_volatile = g_decl.is_volatile;
            g_decl.is_volatile = skip_type_qualifiers_volatile();
            type = type_add_ptr(type);
        }

        if (parse_parenthesized_array_declarator(type, name, sizeof(name),
                                                 &parenthesized_total,
                                                 &parenthesized_stride)) {
            parenthesized_array = 1;
        } else if (parse_funcptr_declarator(&type, name, sizeof(name))) {
            direct_funcptr = 1;
        } else {
            if (g_lex.tok.kind != TOK_ID) {
                error_here("identifier expected");
                break;
            }

            strncpy(name, g_lex.tok.text, sizeof(name) - 1);
            name[sizeof(name) - 1] = 0;
            next_token();
        }

        strncpy(source_name, name, sizeof(source_name) - 1);
        source_name[sizeof(source_name) - 1] = 0;

        if (g_lex.tok.kind == '(') {
            if (g_func_pass.for_decl_seq >= 0 && !direct_funcptr)
                g_for_decl_saw_nonobject = 1;
            skip_prototype_function_suffix();
            if (!accept(','))
                break;
            continue;
        }

        if (g_func_pass.for_decl_seq >= 0) {
            const char *rn;
            rn = enter_for_decl_rename(name);
            if (rn != name)
                strncpy(name, rn, sizeof(name) - 1);
            name[sizeof(name) - 1] = 0;
        } else {
            const char *rn = enter_block_decl_rename(name);
            if (rn != name)
                strncpy(name, rn, sizeof(name) - 1);
            name[sizeof(name) - 1] = 0;
        }

        arrlen = parenthesized_array ? parenthesized_total
                                     : g_funcptr_decl_array_len;
        g_funcptr_decl_array_len = 0;
        total_elems = arrlen;
        {
            int first_stride_bytes;
            first_stride_bytes = 0;
            if (parenthesized_array)
                first_stride_bytes = parenthesized_stride;
            else if (arrlen == 0)
                parse_array_declarator_dims(type, &total_elems, &first_stride_bytes, 1);
            else
                total_elems = arrlen;

            arrlen = total_elems;

            /*
             * Local omitted-size arrays need the initializer count before
             * allocation:
             *     char data[] = { 'a', 'b', 0 };
             */
            if (arrlen == 0 && g_last_array_dim_count > 0 && g_lex.tok.kind == '=') {
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
        } else if (g_last_array_dim_count > 0 && g_typedef_array_len > 0 &&
                   g_last_array_dim_count < MAX_ARRAY_DIMS) {
            /* `ARR2 table[2]` composes the typedef's own array length as an
             * extra trailing dimension - see the identical composition in
             * scan_local_decl_after_type (dcc_func.c), which this codegen
             * pass must reach the same conclusion as. */
            g_last_array_dims[g_last_array_dim_count++] = g_typedef_array_len;
            if (target_size_multiply(arrlen, g_typedef_array_len, &arrlen))
                total_elems = arrlen;
            else
                error_here("object size exceeds 16-bit address space");
            /* current_field_array_elem_size (-> Sym.elem_size) reflected only
             * this declarator's own dims; redo it as the stride across every
             * dim past the first now that the typedef's dimension is appended
             * - see the identical fixup in scan_local_decl_after_type
             * (dcc_func.c), which this codegen pass must match. */
            {
                int inner_stride = 1;
                int elem_bytes = type_size(type);
                int di;
                if (elem_bytes <= 0) elem_bytes = 2;
                for (di = 1; di < g_last_array_dim_count; ++di)
                    if (g_last_array_dims[di] > 0)
                        inner_stride *= g_last_array_dims[di];
                current_field_array_elem_size = inner_stride * elem_bytes;
            }
        }

        /* Must reach the identical conclusion scan_local_decl_after_type
         * already reached for this same declarator during the earlier
         * frame-sizing pass - both independently re-run the same
         * speculative parse over the same source text, so they agree. */
        narrowed_as_counter = 0;
        if (!g_decl.is_extern && !g_decl.is_volatile &&
            try_narrow_local_int_array(source_name, type, arrlen, total_elems)) {
            type = (type & ~15) | TYPE_CHAR | TYPE_UNSIGNED;
            /* See the identical comment in scan_local_decl_after_type
             * (dcc_func.c): first_stride_bytes was computed from the
             * pre-narrowing int element size, so it must be invalidated
             * here too or Sym.elem_size below keeps the stale, too-wide
             * stride even though Sym.type is now correctly narrowed. */
            current_field_array_elem_size = 0;
        } else if (!g_decl.is_extern && !g_decl.is_volatile &&
                   try_narrow_register_scalar(name, type, g_decl.is_register, arrlen, total_elems)) {
            type = (type & ~15) | TYPE_CHAR | TYPE_UNSIGNED;
        } else if (!g_decl.is_extern && !g_decl.is_volatile &&
                   try_narrow_for_counter(name, type, arrlen, total_elems)) {
            type = (type & ~15) | TYPE_CHAR | TYPE_UNSIGNED;
            narrowed_as_counter = 1;
        }

        s = find_local_decl(name);
        if (!s)
            s = try_const_fold_local(name, source_name, type,
                                     total_elems > 0 || g_last_array_dim_count > 0);
        freshly_allocated = 0;
        if (!s) {
            bytes = type_size(type);
            if (g_vla_pending)
                bytes = 2;              /* VLA: reserve only a pointer slot */
            else if (total_elems > 0)
                bytes = object_array_size(type, total_elems);

            s = g_decl.is_extern
                ? add_block_extern_alias(name, source_name, type, bytes)
                : add_local_alloc(name, type, bytes);
            copy_funcptr_prototype_to_sym(s, direct_funcptr);
            s->is_volatile = g_decl.is_volatile;
            s->pointee_is_volatile = g_decl.pointee_is_volatile;
            s->is_register = g_decl.is_register;
            freshly_allocated = 1;
            if (arrlen > 0 || g_last_array_dim_count > 0) {
                s->is_array = 1;
                s->array_len = arrlen;
                s->elem_size = current_field_array_elem_size ? current_field_array_elem_size : type_size(type);
                if (s->elem_size <= 0) s->elem_size = 2;
                copy_last_array_dims_to_sym(s);
                if (g_vla_pending) {
                    struct Sym *size_slot;
                    /* VLA: the frame slot holds a runtime pointer to the
                     * block.  Keep the elem_size set above - the element size
                     * for a[n], or the (constant) row stride for a[n][C] - so
                     * both indexing and the allocation size are correct for
                     * the variable outer dimension. */
                    s->is_vla = 1;
                    s->array_len = 0;
                    if (s->elem_size <= 0) s->elem_size = 1;
                    size_slot = add_local_alloc("#vlasz", TYPE_INT, 2);
                    s->vla_size_offset = size_slot->offset;
                    /* Save SP once per scope, before its first VLA, so the
                     * scope's VLAs can be reclaimed at block/loop exit. */
                    {
                        int vsp_off = vla_scope_ensure_save_slot();
                        if (vsp_off != 0)
                            emit_vla_save_sp(vsp_off);
                    }
                } else {
                    /* Mirrors the identical hook in scan_local_decl_after_type
                     * (dcc_func.c): a VLA's slot holds a runtime pointer, not
                     * a fixed address, so address-caching only applies to
                     * ordinary fixed arrays. Must run here too, not just in
                     * the scan pass - gen_local_decl_after_type is a separate
                     * declaration handler used only by the real codegen pass
                     * (see gen_compound), with its own freshly-allocated Sym
                     * that the scan pass's has_addr_cache/addr_cache_offset
                     * never reaches. */
                    maybe_reserve_addr_cache_for_array(s, name);
                }
            } else if (g_ptr_array_dim_count > 0) {
                int pi;
                s->elem_size = g_ptr_array_elem_size;
                s->dim_count = g_ptr_array_dim_count;
                for (pi = 0; pi < MAX_ARRAY_DIMS; ++pi)
                    s->dims[pi] = (pi < g_ptr_array_dim_count) ? g_ptr_array_dims[pi] : 0;
            }
        }
        if (freshly_allocated || (opt_debug_lines && s && s->is_const_value)) {
            mir_note_declared_symbol(s);
            mir_note_declared_alias(source_name, s);
            emit_debug_variable(s);
        }
        if (narrowed_as_counter)
            mir_note_narrowed_for_counter();
        g_ptr_array_dim_count = 0;
        g_ptr_array_elem_size = 0;

        if (g_vla_pending) {
            /* A variable inner dimension (runtime row stride) already errored
             * during the declarator parse; only a variable outer dimension
             * with constant inner dimensions reaches here. */
            if (freshly_allocated)
                emit_vla_alloc(s);
            g_vla_pending = 0;
        }

        if (s->is_const_value) {
            if (accept('=')) {
                unsigned long parsed_const_value;
                if (try_parse_local_const_initializer(type,
                                                      &parsed_const_value)) {
                    s->type = type;
                    s->const_value = parsed_const_value;
                } else {
                    ast_emit_init_expr();
                }
            }
            mir_note_declared_symbol(s);
        } else if (accept('=')) {
            if ((type & TYPE_STRUCT) && type_ptr_depth(type) == 0 && g_lex.tok.kind != '{') {
                ast_emit_struct_init_expr_assign(s);
            } else if (s->is_array && (type & 15) == TYPE_CHAR && type_ptr_depth(type) == 0 && g_lex.tok.kind == TOK_STR) {
                char *lit;
                int is_wide;
                int litlen;
                lit = read_adjacent_string_literals_ex(&is_wide, &litlen);
                if (is_wide)
                    error_here("wide string cannot initialize char array");
                mir_capture_init_char_array(s, lit, litlen + 1);
                if (!mir_is_active())
                    emit_init_auto_char_array_from_string(s, lit, litlen);
                free(lit);
            } else if (s->is_array && g_lex.tok.kind == '{' && (type & TYPE_STRUCT) && type_ptr_depth(type) == 0) {
                emit_init_auto_struct_array_from_list(s);
            } else if (!s->is_array && g_lex.tok.kind == '{' && (type & TYPE_STRUCT) && type_ptr_depth(type) == 0) {
                emit_init_auto_struct_from_list(s);
            } else if (s->is_array && g_lex.tok.kind == '{' && (!(type & TYPE_STRUCT) || type_ptr_depth(type) > 0)) {
                emit_init_auto_array_from_list(s, type);
            } else if (!s->is_array && g_lex.tok.kind == '{') {
                /* Same ix-direct fast path as the plain (no-braces) scalar
                 * case below - this is just `T x = {expr};`, a legacy/GNU
                 * brace-wrapped scalar initializer, not an array/struct. */
                int fast = sym_can_ix_direct(s);
                next_token();
                if (!fast && !mir_is_active()) {
                    emit_load_sym_addr(s);
                    emit("\tpush hl\n");
                }
                mir_set_initializer_target(s);
                ast_emit_init_expr();
                if (mir_is_active()) {
                    accept(',');
                    expect('}');
                    goto initializer_done;
                }
                if (type_is_long(type)) {
                    if (type_is_float(g_expr.type))
                        emit_convert_float_to_intlike(type);
                    else if (!type_is_long(g_expr.type))
                        emit_extend_to_long_typed(g_expr.type);
                    if (fast)
                        emit_store_hl_to_sym_direct(s);
                    else
                        emit_store_de_to_addr_hl(type);
                } else {
                    if (type_is_float(g_expr.type))
                        emit_convert_float_to_intlike(type);
                    else if (type_size(type) > 1 && !type_is_long(g_expr.type))
                        emit_promote_byte_to_int(g_expr.type);
                    if (fast) {
                        emit_store_hl_to_sym_direct(s);
                    } else {
                        emit("\tex de,hl\n\tpop hl\n");
                        emit_store_de_to_addr_hl(type);
                    }
                }
                accept(',');
                expect('}');
            } else if (!s->is_array && (type & 15) == TYPE_FLOAT && type_ptr_depth(type) == 0) {
                unsigned long bits;
                int fast = sym_can_ix_direct(s);
                if (parse_float_init_literal(&bits)) {
                    mir_capture_init_constant(s, 0, type, (long)bits);
                    if (mir_is_active())
                        goto initializer_done;
                    if (fast) {
                        /* Compile-time-constant float bits: write the 4
                         * immediate bytes straight to the frame slot, no
                         * register round-trip needed at all. */
                        fprintf(g_emit_sink.stream, "\tld (ix%+d),%lu\n", s->offset, bits & 0xffUL);
                        fprintf(g_emit_sink.stream, "\tld (ix%+d),%lu\n", s->offset + 1, (bits >> 8) & 0xffUL);
                        fprintf(g_emit_sink.stream, "\tld (ix%+d),%lu\n", s->offset + 2, (bits >> 16) & 0xffUL);
                        fprintf(g_emit_sink.stream, "\tld (ix%+d),%lu\n", s->offset + 3, (bits >> 24) & 0xffUL);
                    } else {
                        emit_load_sym_addr(s);
                        emit("\tpush hl\n");
                        fprintf(g_emit_sink.stream, "\tld hl,%lu\n", bits & 0xffffUL);
                        fprintf(g_emit_sink.stream, "\tld de,%lu\n", (bits >> 16) & 0xffffUL);
                        emit_store_de_to_addr_hl(type);
                    }
                } else {
                    /* Extension beyond strict C89: allow automatic float
                     * declarations to use expression initializers, e.g.
                     *     float r = 16.0f * f;
                     * This is emitted like a declaration followed by an
                     * assignment.  The constant fast path above stays for
                     * smaller code.
                     */
                    if (!fast && !mir_is_active()) {
                        emit_load_sym_addr(s);
                        emit("\tpush hl\n");
                    }
                    mir_set_initializer_target(s);
                    ast_emit_init_expr();
                    if (mir_is_active())
                        goto initializer_done;
                    if (!type_is_float(g_expr.type))
                        emit_convert_int_to_float(g_expr.type);
                    if (fast)
                        emit_store_hl_to_sym_direct(s);
                    else
                        emit_store_de_to_addr_hl(type);
                }
            } else {
                /* Fast path: this plain scalar local (no struct/array/brace
                 * initializer involved here) skips the address computation
                 * entirely when its frame offset fits (ix+d) directly,
                 * reusing emit_store_hl_to_sym_direct - the same helper a
                 * separate `T x; x = expr;` assignment already gets via
                 * gen_assign_ast's own sym_can_ix_direct fast paths. This was
                 * the single biggest source of dcc's own generated code
                 * being far slower than after dccpeep's cleanup: EVERY
                 * `T x = expr;` declaration paid a push-ix/pop-hl/dec-hl
                 * address computation plus a push/pop round trip for what a
                 * one- or two-instruction direct store can do once the
                 * value is in HL/DE:HL. */
                int fast = sym_can_ix_direct(s);
                mir_set_initializer_target(s);
                if (!fast && !mir_is_active()) {
                    emit_load_sym_addr(s);
                    emit("\tpush hl\n");
                }
                ast_emit_init_expr();
                if (mir_is_active())
                    goto initializer_done;
                if (type_is_long(type)) {
                    /* For long locals, emit_store_de_to_addr_hl pops the
                     * address itself via "pop de", so don't consume it here. */
                    if (type_is_float(g_expr.type))
                        emit_convert_float_to_intlike(type);
                    else if (!type_is_long(g_expr.type))
                        emit_extend_to_long_typed(g_expr.type);
                    if (fast) {
                        emit_store_hl_to_sym_direct(s);
                    } else {
                        emit_store_de_to_addr_hl(type);
                    }
                } else {
                    if (type_is_float(g_expr.type))
                        emit_convert_float_to_intlike(type);
                    else if (type_size(type) > 1 && !type_is_long(g_expr.type))
                        emit_promote_byte_to_int(g_expr.type);
                    if (fast) {
                        emit_store_hl_to_sym_direct(s);
                    } else {
                        emit("\tex de,hl\n\tpop hl\n");
                        emit_store_de_to_addr_hl(type);
                    }
                }
            }
        } else if (freshly_allocated && !s->is_vla && !local_name_used_ahead(source_name)) {
            /* No initializer, and never referenced again in this scope:
             * add_local_alloc just appended this Sym as the last local and
             * reserved its frame space, so popping both back off is safe -
             * nothing later in this same declarator loop has allocated
             * anything above it yet. freshly_allocated (rather than just
             * !s->is_const_value) guards against the redefinition-error
             * recovery case, where s is an unrelated pre-existing symbol and
             * bytes/nlocals do not describe it. A VLA is never pruned: it has
             * a side-effecting stack allocation plus hidden #vlasz/#vlasp
             * slots whose offsets are already baked into emitted save/restore
             * code, and its `bytes` (2, the pointer slot) does not describe
             * the whole reservation. scan_local_decl_after_type skips the
             * prune for the same case via its still-set g_vla_pending guard,
             * so both passes must agree here (g_vla_pending is already cleared
             * above in this pass, hence the s->is_vla test instead). */
            g_frame.nlocals--;
            g_frame.local_size -= bytes;
        }

initializer_done:
        if (!accept(',')) break;
    }

    expect(';');
}
