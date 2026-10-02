/**
 * @file dcc_expr.c
 * @brief Parses declarators and resolves frontend expression metadata.
 *
 * @par Role
 * Parses sizeof operands and named/abstract function-pointer/array declarators,
 * preserves nested callable return and argument signatures, counts
 * initializer shapes, tracks user labels, and supplies type lookahead and
 * callable/aggregate type queries.
 *
 * @par Key entry points
 * parse_sizeof_expr_operand(), parse_funcptr_declarator(),
 * parse_abstract_funcptr_declarator(), parse_funcptr_prototype_suffix(),
 * parse_array_declarator_dims(), peek_simple_unary_type(), and
 * define_user_label().
 *
 * @par Boundary
 * dcc_ast_build.c owns general expression grammar and dcc_types.c owns the
 * target type model. These helpers do not form a production body fallback;
 * final function bodies come from selected MIR candidates.
 */

#include "dcc.h"

int parse_sizeof_expr_operand(void)
{
    int type;
    int sz;
    int rhs_type;
    int rhs_sz;
    int op;

    if (!sizeof_parse_primary_type(&type, &sz))
        return sz;

    /* Consume a simple expression without emitting code.  This is intentionally
     * conservative, but it is enough for C89 sizeof expression cases such as:
     *     sizeof a + b       (as parsed by caller inside parentheses)
     *     sizeof(a + 1L)
     *     sizeof(p[0])
     *     sizeof(s.field)
     * Stop at delimiters that belong to the surrounding grammar. */
    while (g_lex.tok.kind != TOK_EOF && g_lex.tok.kind != ')' && g_lex.tok.kind != ']' &&
           g_lex.tok.kind != ',' && g_lex.tok.kind != ';') {
        op = g_lex.tok.kind;

        if (op == '?' || op == ':')
            break;

        /* post-increment/decrement are unary postfix: no right operand */
        if (op == TOK_INC || op == TOK_DEC) {
            next_token();
            continue;
        }

        next_token();
        if (!sizeof_parse_primary_type(&rhs_type, &rhs_sz))
            break;

        type = sizeof_common_type(type, rhs_type, op);
        sz = type_size(type);
        if (sz <= 0) sz = 2;
    }

    return sz;
}


int type_is_struct_object(int type)
{
    return (type & TYPE_STRUCT) && type_ptr_depth(type) == 0;
}

int same_struct_type(int a, int b)
{
    return type_is_struct_object(a) && type_is_struct_object(b) &&
           type_struct_id(a) == type_struct_id(b);
}


static void parse_pointer_array_suffixes(int base_type)
{
    int dims[MAX_ARRAY_DIMS];
    int ndims;
    int i;
    int n;
    int elem_bytes;
    int total;

    ndims = 0;
    memset(dims, 0, sizeof(dims));
    while (accept('[')) {
        if (g_lex.tok.kind == ']') {
            n = 0;
            next_token();
        } else {
            n = parse_typed_array_bound_expr();
            expect(']');
        }
        if (n < 0) n = 0;
        if (ndims < MAX_ARRAY_DIMS) dims[ndims++] = n;
    }

    elem_bytes = type_size(base_type);
    if (elem_bytes <= 0) elem_bytes = 2;
    total = 1;
    for (i = 0; i < ndims; ++i) {
        if (dims[i] <= 0) {
            total = 0;
            break;
        }
        total *= dims[i];
    }
    g_ptr_array_dim_count = ndims;
    g_ptr_array_elem_size = total > 0 ? total * elem_bytes : elem_bytes;
    for (i = 0; i < ndims && i < MAX_ARRAY_DIMS; ++i)
        g_ptr_array_dims[i] = dims[i];
}

static void clear_funcptr_prototype(void)
{
    g_funcptr_has_proto = 0;
    g_funcptr_proto_nargs = 0;
    g_funcptr_proto_variadic = 0;
    memset(g_funcptr_proto_types, 0, sizeof(g_funcptr_proto_types));
}

/* Parse the parameter-type list after the opening `(` of a function-pointer
 * declarator.  Function pointers use the ordinary stack ABI, so retaining
 * these types is essential: an int actual passed to a long formal must be
 * widened and pushed as four bytes even though the call is indirect. */
void parse_funcptr_prototype_suffix(void)
{
    int types[MAX_PROTO_PARAMS];
    int nargs;
    int variadic;
    int has_proto;
    int i;
    DeclState saved_decl = g_decl;
    int saved_array_len = g_funcptr_decl_array_len;
    int saved_funcret = g_funcptr_is_funcret_decl;

    nargs = 0;
    variadic = 0;
    has_proto = 0;
    memset(types, 0, sizeof(types));

    if (g_lex.tok.kind == ')') {
        next_token();                  /* C89: unspecified parameters */
        clear_funcptr_prototype();
        return;
    }

    for (;;) {
        int type;

        if (g_lex.tok.kind == TOK_ELLIPSIS) {
            has_proto = 1;
            variadic = 1;
            next_token();
            break;
        }

        type = parse_type();
        if (g_typedef_array_len > 0) {
            type = type_add_ptr(type);
            g_typedef_array_len = 0;
        }
        while (accept('*')) {
            skip_type_qualifiers();
            type = type_add_ptr(type);
        }
        skip_type_qualifiers();

        if (g_lex.tok.kind == '(' && parse_abstract_funcptr_declarator(&type)) {
        } else if (g_lex.tok.kind == TOK_ID && find_typedef(g_lex.tok.text) < 0)
            next_token();
        skip_prototype_array_suffixes(&type);

        if (type == TYPE_VOID && nargs == 0 && g_lex.tok.kind == ')') {
            has_proto = 1;
            break;
        }

        has_proto = 1;
        if (nargs < MAX_PROTO_PARAMS)
            types[nargs] = type;
        nargs++;
        if (!accept(','))
            break;
    }
    expect(')');

    clear_funcptr_prototype();
    g_funcptr_has_proto = has_proto;
    g_funcptr_proto_nargs = nargs;
    g_funcptr_proto_variadic = variadic;
    for (i = 0; i < MAX_PROTO_PARAMS; ++i)
        g_funcptr_proto_types[i] = types[i];
    g_decl = saved_decl;
    g_funcptr_decl_array_len = saved_array_len;
    g_funcptr_is_funcret_decl = saved_funcret;
}

int parse_funcptr_declarator(int *ptype, char *name, int namesz)
{
    int type;
    int return_type = *ptype;
    struct Sym *result_prototype = capture_funcptr_prototype(*ptype, 0);
    int save_decl_is_volatile;
    int save_decl_pointee_is_volatile;
    unsigned int save_decl_volatile_mask;
    int object_is_volatile;
    int pointee_is_volatile;
    int function_suffix = 0;
    LexState _ls;

    g_funcptr_decl_array_len = 0;
    g_funcptr_is_funcret_decl = 0;
    g_funcptr_return_type = 0;
    g_funcptr_result_prototype = NULL;
    clear_funcptr_prototype();
    g_ptr_array_dim_count = 0;
    g_ptr_array_elem_size = 0;
    memset(g_ptr_array_dims, 0, sizeof(g_ptr_array_dims));

    if (g_lex.tok.kind != '(')
        return 0;

    _ls = lex_save();
    save_decl_is_volatile = g_decl.is_volatile;
    save_decl_pointee_is_volatile = g_decl.pointee_is_volatile;
    save_decl_volatile_mask = g_decl.pointee_volatile_mask |
        (unsigned int)(save_decl_pointee_is_volatile != 0);

    next_token();
    if (!accept('*')) {
        lex_restore(&_ls);
        g_decl.is_volatile = save_decl_is_volatile;
        g_decl.pointee_is_volatile = save_decl_pointee_is_volatile;
        return 0;
    }
    pointee_is_volatile = save_decl_is_volatile;
    object_is_volatile = skip_type_qualifiers_volatile();

    if (g_lex.tok.kind == '(') {
        struct Sym outer_prototype;
        struct Sym *returned_prototype;
        int callee_is_volatile;
        memset(&outer_prototype, 0, sizeof(outer_prototype));
        next_token();
        if (!accept('*')) {
            lex_restore(&_ls);
            g_decl.is_volatile = save_decl_is_volatile;
            g_decl.pointee_is_volatile = save_decl_pointee_is_volatile;
            return 0;
        }
        callee_is_volatile = skip_type_qualifiers_volatile();
        if (g_lex.tok.kind != TOK_ID && !(name == NULL && g_lex.tok.kind == ')')) {
            lex_restore(&_ls);
            return 0;
        }
        if (g_lex.tok.kind == TOK_ID) {
            if (name != NULL) {
                strncpy(name, g_lex.tok.text, namesz - 1);
                name[namesz - 1] = 0;
            }
            next_token();
        }
        if (!accept(')')) {
            lex_restore(&_ls);
            g_decl.is_volatile = save_decl_is_volatile;
            g_decl.pointee_is_volatile = save_decl_pointee_is_volatile;
            return 0;
        }
        if (accept('(')) {
            parse_funcptr_prototype_suffix();
        }
        outer_prototype.has_proto = g_funcptr_has_proto;
        outer_prototype.proto_nargs = g_funcptr_proto_nargs;
        outer_prototype.proto_variadic = g_funcptr_proto_variadic;
        memcpy(outer_prototype.proto_types, g_funcptr_proto_types, sizeof(g_funcptr_proto_types));
        if (!accept(')')) {
            lex_restore(&_ls);
            g_decl.is_volatile = save_decl_is_volatile;
            g_decl.pointee_is_volatile = save_decl_pointee_is_volatile;
            return 0;
        }
        if (accept('(')) {
            parse_funcptr_prototype_suffix();
        }
        g_decl.is_volatile = object_is_volatile;
        g_decl.pointee_is_volatile = pointee_is_volatile;
        g_decl.pointee_volatile_mask = (save_decl_volatile_mask << 1) |
            (unsigned int)(pointee_is_volatile != 0);
        g_funcptr_return_type = return_type;
        g_funcptr_result_prototype = result_prototype;
        type = type_add_ptr(return_type);
        returned_prototype = capture_funcptr_prototype(type, 1);
        ptype[0] = type_add_ptr(type);
        g_decl.pointee_volatile_mask = (g_decl.pointee_volatile_mask << 1) |
            (unsigned int)(object_is_volatile != 0);
        g_decl.is_volatile = callee_is_volatile;
        g_decl.pointee_is_volatile = object_is_volatile;
        g_funcptr_return_type = type;
        g_funcptr_result_prototype = returned_prototype;
        g_funcptr_has_proto = outer_prototype.has_proto;
        g_funcptr_proto_nargs = outer_prototype.proto_nargs;
        g_funcptr_proto_variadic = outer_prototype.proto_variadic;
        memcpy(g_funcptr_proto_types, outer_prototype.proto_types, sizeof(g_funcptr_proto_types));
        return 1;
    }

    if (g_lex.tok.kind != TOK_ID && !(name == NULL && g_lex.tok.kind == ')')) {
        lex_restore(&_ls);
        g_decl.is_volatile = save_decl_is_volatile;
        g_decl.pointee_is_volatile = save_decl_pointee_is_volatile;
        return 0;
    }

    if (g_lex.tok.kind == TOK_ID) {
        if (name != NULL) {
            strncpy(name, g_lex.tok.text, namesz - 1);
            name[namesz - 1] = 0;
        }
        next_token();
    }

    if (accept('[')) {
        if (g_lex.tok.kind == ']') {
            g_funcptr_decl_array_len = 0;
            next_token();
        } else {
            g_funcptr_decl_array_len = parse_typed_array_bound_expr();
            expect(']');
        }
    }

    if (!accept(')')) {
        /* C89: return_type (*func_name(param_list))(pointed_fn_params)
         * A function declaration whose return type is a pointer to function.
         * The (*name has already been consumed; tok is now '(' (the param list). */
        if (g_lex.tok.kind == '(') {
            next_token(); /* consume opening '(' of param list */
            parse_param_list();
            if (g_lex.tok.kind != ')') {
                lex_restore(&_ls);
                g_funcptr_decl_array_len = 0;
                g_ptr_array_dim_count = 0;
                g_ptr_array_elem_size = 0;
                memset(g_ptr_array_dims, 0, sizeof(g_ptr_array_dims));
                g_decl.is_volatile = save_decl_is_volatile;
                g_decl.pointee_is_volatile = save_decl_pointee_is_volatile;
                g_decl.pointee_volatile_mask = save_decl_volatile_mask;
                return 0;
            }
            next_token(); /* consume ')' of name(...) */
            if (!accept(')')) {
                lex_restore(&_ls);
                g_funcptr_decl_array_len = 0;
                g_ptr_array_dim_count = 0;
                g_ptr_array_elem_size = 0;
                memset(g_ptr_array_dims, 0, sizeof(g_ptr_array_dims));
                g_decl.is_volatile = save_decl_is_volatile;
                g_decl.pointee_is_volatile = save_decl_pointee_is_volatile;
                g_decl.pointee_volatile_mask = save_decl_volatile_mask;
                return 0;
            }
            if (accept('(')) {
                parse_funcptr_prototype_suffix();
            } else if (g_lex.tok.kind == '[') {
                parse_pointer_array_suffixes(ptype[0]);
            }
            type = type_add_ptr(ptype[0]);
            ptype[0] = type;
            g_funcptr_is_funcret_decl = 1;
            g_decl.is_volatile = object_is_volatile;
            g_decl.pointee_is_volatile = pointee_is_volatile;
            g_decl.pointee_volatile_mask = (save_decl_volatile_mask << 1) |
                (unsigned int)(pointee_is_volatile != 0);
            g_funcptr_return_type = return_type;
            g_funcptr_result_prototype = result_prototype;
            return 1;
        }

        lex_restore(&_ls);
        g_funcptr_decl_array_len = 0;
        g_ptr_array_dim_count = 0;
        g_ptr_array_elem_size = 0;
        memset(g_ptr_array_dims, 0, sizeof(g_ptr_array_dims));
        g_decl.is_volatile = save_decl_is_volatile;
        g_decl.pointee_is_volatile = save_decl_pointee_is_volatile;
        return 0;
    }

    type = type_add_ptr(ptype[0]);

    if (accept('(')) {
        function_suffix = 1;
        parse_funcptr_prototype_suffix();
    } else if (g_lex.tok.kind == '[') {
        parse_pointer_array_suffixes(ptype[0]);
    }

    ptype[0] = type;
    g_decl.is_volatile = object_is_volatile;
    g_decl.pointee_is_volatile = pointee_is_volatile;
    g_decl.pointee_volatile_mask = (save_decl_volatile_mask << 1) |
        (unsigned int)(pointee_is_volatile != 0);
    g_funcptr_return_type = function_suffix ? return_type : 0;
    g_funcptr_result_prototype = function_suffix ? result_prototype : NULL;
    return 1;
}


int parse_abstract_funcptr_declarator(int *ptype)
{
    return parse_funcptr_declarator(ptype, NULL, 0);
}

int char_array_string_initializer_size(int base_type)
{
    LexState _ls;
    int n;

    if ((base_type & 15) != TYPE_CHAR || type_ptr_depth(base_type) != 0)
        return 0;
    if (g_lex.tok.kind != '=')
        return 0;

    _ls = lex_save();

    next_token();
    if (g_lex.tok.kind == TOK_STR) {
        char *lit;
        int is_wide;
        int litlen;

        /*
         * Omitted-size char arrays must be sized from the whole C string
         * literal sequence, not just the first token.  The old lookahead used
         * tok.text directly, so:
         *
         *     char t[] = "xy" "z";
         *
         * allocated only sizeof("xy") == 3 bytes, while code generation
         * later emitted the concatenated four-byte initializer.  On CP/M this
         * overwrote the next local slot and sizeof(t) was also wrong.
         */
        lit = read_adjacent_string_literals_ex(&is_wide, &litlen);
        if (is_wide)
            n = 0;
        else
            n = litlen + 1;
        free(lit);
    } else {
        n = 0;
    }

    lex_restore(&_ls);
    return n;
}

/*
 * Parse one or more array declarator dimensions after the identifier.
 *
 * DCC stores arrays as a flat byte/object allocation.  For multidimensional
 * arrays, total_len is the product of all dimensions, and first_stride_elems
 * is the product of the inner dimensions.  Example:
 *
 *     char bufs[2][256]
 *
 * total_len = 512, first_stride_elems = 256.  Existing array indexing code
 * already uses Sym.elem_size as the stride for the first index, so bufs[i]
 * points at the correct row without needing a full C array type system.
 */
static void skip_array_dim_balanced(int open, int close)
{
    int depth;

    if (g_lex.tok.kind != open)
        return;
    depth = 1;
    next_token();
    while (g_lex.tok.kind != TOK_EOF && depth > 0) {
        if (g_lex.tok.kind == open)
            depth++;
        else if (g_lex.tok.kind == close)
            depth--;
        next_token();
    }
}

static void skip_sizeof_array_dim_operand(void)
{
    int done;

    if (g_lex.tok.kind == TOK_SIZEOF)
        next_token();

    while (g_lex.tok.kind == TOK_SIZEOF || g_lex.tok.kind == '*' || g_lex.tok.kind == '&' ||
           g_lex.tok.kind == '+' || g_lex.tok.kind == '-' || g_lex.tok.kind == '!' ||
           g_lex.tok.kind == '~') {
        if (g_lex.tok.kind == TOK_SIZEOF)
            next_token();
        else
            next_token();
    }

    if (g_lex.tok.kind == '(') {
        skip_array_dim_balanced('(', ')');
        return;
    }

    if (g_lex.tok.kind == TOK_ID || g_lex.tok.kind == TOK_NUM || g_lex.tok.kind == TOK_CHARLIT ||
        g_lex.tok.kind == TOK_STR || g_lex.tok.kind == TOK_WSTR) {
        next_token();
        done = 0;
        while (!done) {
            if (g_lex.tok.kind == '[') {
                skip_array_dim_balanced('[', ']');
            } else if (g_lex.tok.kind == '(') {
                skip_array_dim_balanced('(', ')');
            } else if (g_lex.tok.kind == '.' || g_lex.tok.kind == TOK_ARROW) {
                next_token();
                if (g_lex.tok.kind == TOK_ID)
                    next_token();
            } else {
                done = 1;
            }
        }
    }
}

int array_dim_has_runtime_identifier(void)
{
    LexState _ls;
    int depth;
    int has_runtime;

    _ls = lex_save();

    depth = 0;
    has_runtime = 0;
    while (g_lex.tok.kind != TOK_EOF) {
        if (depth == 0 && g_lex.tok.kind == ']')
            break;
        if (g_lex.tok.kind == TOK_SIZEOF) {
            skip_sizeof_array_dim_operand();
            continue;
        }
        if (g_lex.tok.kind == '(') {
            /* A parenthesized construct that begins with a type is a cast (or
             * parenthesized type): its type-name identifiers (e.g. size_t in
             * (size_t)8) are not runtime values, so skip the whole `(type)`
             * and keep scanning the operand.  An ordinary parenthesized
             * expression is counted normally so its identifiers are seen. */
            next_token();
            if (starts_type()) {
                int d2 = 1;
                while (g_lex.tok.kind != TOK_EOF && d2 > 0) {
                    if (g_lex.tok.kind == '(')
                        d2++;
                    else if (g_lex.tok.kind == ')')
                        d2--;
                    next_token();
                }
            } else {
                depth++;
            }
            continue;
        }
        if (g_lex.tok.kind == TOK_ID && find_enum_const(g_lex.tok.text) < 0) {
            has_runtime = 1;
            break;
        }
        if (g_lex.tok.kind == '[' || g_lex.tok.kind == '{')
            depth++;
        else if (g_lex.tok.kind == ')' || g_lex.tok.kind == ']' || g_lex.tok.kind == '}') {
            if (depth > 0)
                depth--;
        }
        next_token();
    }

    lex_restore(&_ls);
    return has_runtime;
}

/*
 * Skip tokens up to the `]` that closes the current array dimension, honoring
 * nested brackets/parens/braces so a subscript or call inside the dimension
 * expression (e.g. `b[a[n-1] + 2]`) does not stop early on an inner `]`.  The
 * opening `[` of the dimension has already been consumed by the caller; on
 * return the closing `]` has been consumed too.
 */
void skip_array_dim_to_close(void)
{
    int depth = 0;
    while (g_lex.tok.kind != TOK_EOF) {
        if (depth == 0 && g_lex.tok.kind == ']')
            break;
        if (g_lex.tok.kind == '(' || g_lex.tok.kind == '[' || g_lex.tok.kind == '{')
            depth++;
        else if (g_lex.tok.kind == ')' || g_lex.tok.kind == '}')
            { if (depth > 0) depth--; }
        else if (g_lex.tok.kind == ']')
            { if (depth > 0) depth--; }
        next_token();
    }
    expect(']');
}

void parse_array_declarator_dims(int base_type,
                                        int *total_len,
                                        int *first_stride_bytes,
                                        int allow_empty_first)
{
    int dims[MAX_ARRAY_DIMS];
    int ndims;
    int i;
    int n;
    int elem_bytes;
    int object_bytes;
    int total;
    int inner;
    int overflowed;

    ndims = 0;
    overflowed = 0;
    g_last_array_dim_count = 0;
    g_vla_pending = 0;
    memset(g_last_array_dims, 0, sizeof(g_last_array_dims));

    while (accept('[')) {
        if (g_lex.tok.kind == ']') {
            next_token();
            n = (allow_empty_first && ndims == 0)
                    ? char_array_string_initializer_size(base_type)
                    : 0;
        } else {
            if (array_dim_has_runtime_identifier()) {
                /*
                 * Non-constant array bound.  A local VLA whose only variable
                 * dimension is the first is supported: capture the dimension
                 * expression so the declaration codegen can evaluate it at run
                 * time and allocate the block below SP (the array then decays
                 * to that pointer).  Capture in every pass - including the
                 * frame-sizing scan (asm_suppress_depth > 0) - so scan and
                 * codegen reserve the identical pointer slot.  A variable inner
                 * dimension has a runtime stride and is rejected below (the
                 * error is emitted only when not suppressed, i.e. at codegen).
                 */
                if (ndims == 0) {
                    g_vla_pending = 1;
                    g_vla_dim_posi = g_lex.posi;
                    g_vla_dim_tok_start = g_lex.tok_start_pos;
                    g_vla_dim_line = g_lex.line_no;
                    g_vla_dim_tok_line = g_lex.tok_line;
                    g_vla_dim_tok = g_lex.tok;
                    skip_array_dim_to_close();
                    n = 0;
                } else {
                    /* dcc_error_at already suppresses printing during a
                     * structural scan while still recording that the parse
                     * encountered a real diagnostic. */
                    error_here("variable inner dimensions in variable-length arrays are not supported; use malloc and an explicit pointer");
                    skip_array_dim_to_close();
                    n = 0;
                }
            } else {
                n = parse_typed_array_bound_expr();
                expect(']');
            }
        }

        if (n < 0)
            n = 0;
        if (ndims < MAX_ARRAY_DIMS) {
            dims[ndims++] = n;
        } else {
            /* Array rank exceeds the supported maximum (C99/C11 5.2.4.1
             * guarantees at least 12).  Emit one diagnostic and keep ndims
             * capped so the dims[] buffer is never indexed out of range. */
            if (!overflowed)
                error_here("too many array dimensions");
            overflowed = 1;
        }
    }

    if (ndims == 0) {
        total_len[0] = 0;
        first_stride_bytes[0] = 0;
        return;
    }

    total = 1;
    for (i = 0; i < ndims; ++i) {
        if (dims[i] <= 0) {
            total = 0;
            break;
        }
        if (!target_size_multiply(total, dims[i], &total)) {
            error_here("object size exceeds 16-bit address space");
            break;
        }
    }

    elem_bytes = type_size(base_type);
    if (elem_bytes <= 0)
        elem_bytes = 2;

    inner = 1;
    for (i = 1; i < ndims; ++i) {
        if (dims[i] <= 0) {
            inner = 0;
            break;
        }
        if (!target_size_multiply(inner, dims[i], &inner))
            break;
    }

    total_len[0] = total;
    if (!target_size_multiply(total, elem_bytes, &object_bytes)) {
        if (total > 0)
            error_here("object size exceeds 16-bit address space");
        total_len[0] = 0;
    }
    if (ndims > 1 && inner > 0 &&
        target_size_multiply(inner, elem_bytes, &object_bytes))
        first_stride_bytes[0] = object_bytes;
    else
        first_stride_bytes[0] = elem_bytes;

    g_last_array_dim_count = ndims;
    for (i = 0; i < ndims && i < MAX_ARRAY_DIMS; ++i)
        g_last_array_dims[i] = dims[i];
}

/* Accept redundant grouping around an array direct-declarator, such as the
 * standard-C spelling `int *(p[3])` (equivalent to `int *p[3]`).  Function
 * pointer declarators also begin with `(`, so probe through the identifier and
 * commit only when the following token is an array suffix. */
int parse_parenthesized_array_declarator(int base_type, char *name, int namesz,
                                         int *total_len,
                                         int *first_stride_bytes)
{
    LexState saved;

    if (g_lex.tok.kind != '(')
        return 0;
    saved = lex_save();
    next_token();
    if (g_lex.tok.kind != TOK_ID) {
        lex_restore(&saved);
        return 0;
    }
    strncpy(name, g_lex.tok.text, (size_t)namesz - 1);
    name[namesz - 1] = 0;
    next_token();
    if (g_lex.tok.kind != '[') {
        lex_restore(&saved);
        return 0;
    }
    parse_array_declarator_dims(base_type, total_len, first_stride_bytes, 1);
    expect(')');
    return 1;
}




int count_initializer_atoms_level(void)
{
    int n;
    int depth;

    n = 0;

    if (accept('{')) {
        while (g_lex.tok.kind != TOK_EOF && g_lex.tok.kind != '}') {
            n += count_initializer_atoms_level();
            if (!accept(','))
                break;
            if (g_lex.tok.kind == '}')
                break;
        }
        expect('}');
        return n;
    }

    depth = 0;
    while (g_lex.tok.kind != TOK_EOF) {
        if (depth == 0 && (g_lex.tok.kind == ',' || g_lex.tok.kind == '}'))
            break;

        if (g_lex.tok.kind == '(' || g_lex.tok.kind == '[' || g_lex.tok.kind == '{') {
            depth++;
        } else if (g_lex.tok.kind == ')' || g_lex.tok.kind == ']' || g_lex.tok.kind == '}') {
            if (depth > 0)
                depth--;
            else
                break;
        }

        next_token();
    }

    return 1;
}

int count_omitted_array_initializer_atoms(void)
{
    LexState _ls;
    int n;

    _ls = lex_save();

    n = 0;
    if (accept('=') && g_lex.tok.kind == '{')
        n = count_initializer_atoms_level();

    lex_restore(&_ls);
    return n;
}

/*
 * Count the TOP-LEVEL comma-separated elements inside the outer initializer
 * brace, regardless of how many scalar atoms each element spells.  For
 * { {1},{2},{3} } this returns 3 even though each braced group is a *partial*
 * struct that only initializes its first field.  count_initializer_atoms_level
 * is reused to skip over each element's contents.
 */
int count_initializer_top_elems_level(void)
{
    int n;

    n = 0;
    if (accept('{')) {
        while (g_lex.tok.kind != TOK_EOF && g_lex.tok.kind != '}') {
            count_initializer_atoms_level();   /* skip one whole element */
            n++;
            if (!accept(','))
                break;
            if (g_lex.tok.kind == '}')
                break;
        }
        expect('}');
    }
    return n;
}

int count_omitted_array_initializer_top_elems(void)
{
    LexState _ls;
    int n;

    _ls = lex_save();

    n = 0;
    if (accept('=') && g_lex.tok.kind == '{')
        n = count_initializer_top_elems_level();

    lex_restore(&_ls);
    return n;
}

void parse_typedef_decl(void);
void parse_global_init_list(struct Sym *s);
void scan_static_local_decl_after_type(int base);
char *copy_range(long a, long b);

/* Look up or pre-allocate a user label ID (for goto / label: targets).
 * Labels are function-scoped; nulabels is reset before each function. */
int find_or_alloc_user_label_index(const char *name)
{
    int i;

    for (i = 0; i < nulabels; ++i)
        if (!strcmp(ulabel_names[i], name))
            return i;

    if (nulabels >= MAX_USER_LABELS) fatal("too many goto labels");
    memset(&ulabel_names[nulabels], 0, sizeof(ulabel_names[nulabels]));
    strncpy(ulabel_names[nulabels], name, sizeof(ulabel_names[nulabels]) - 1);
    ulabel_ids[nulabels] = new_label();
    ulabel_defined[nulabels] = 0;
    ulabel_referenced[nulabels] = 0;
    ulabel_vla_snap_depth[nulabels] = 0;
    memset(ulabel_vla_snap_off[nulabels], 0, sizeof(ulabel_vla_snap_off[nulabels]));
    ulabel_shallow_fwd_ref[nulabels] = 0;
    return nulabels++;
}

int define_user_label(const char *name)
{
    int i;

    i = find_or_alloc_user_label_index(name);
    if (ulabel_defined[i])
        error_here("duplicate goto label");
    ulabel_defined[i] = 1;
    vla_snapshot_user_label(i);
    return ulabel_ids[i];
}

void check_undefined_user_labels(void)
{
    int i;

    for (i = 0; i < nulabels; ++i) {
        if (ulabel_referenced[i] && !ulabel_defined[i]) {
            char msg[96];
            /* ulabel_names[i] is char[64]; the explicit .63s precision
             * (its declared size - 1) lets gcc prove the result always fits
             * msg's 96 bytes, since it otherwise can't see the bound
             * through the array index (-Wformat-overflow false positive -
             * same class fixed in dccmake.c and asm_name_prefix_underscore). */
            sprintf(msg, "undefined goto label '%.63s'", ulabel_names[i]);
            dcc_error_at(g_lex.tok.file[0] ? g_lex.tok.file : (input_name ? input_name : "<input>"),
                         g_lex.tok_line, -1, msg, NULL);
        }
    }
}

/* Parse an integer constant expression used in enum bodies.
 * Keep the signed target value in an int-sized host object; code generation
 * masks it back to the 16-bit target representation when emitted. */
int parse_enum_const_value(void)
{
    return parse_typed_enum_const_expr();
}
int expected_arg_type(struct Sym *fn, int arg_index, int *ptype)
{
    if (!fn || !fn->has_proto)
        return 0;
    if (arg_index < 0)
        return 0;
    if (arg_index >= fn->proto_nargs)
        return 0;          /* variadic or excess args use normal/default behavior */
    ptype[0] = fn->proto_types[arg_index];
    return 1;
}

int paren_starts_cast(void)
{
    LexState _ls;
    int r;

    if (g_lex.tok.kind != '(')
        return 0;

    _ls = lex_save();

    next_token();
    r = starts_type();

    lex_restore(&_ls);

    return r;
}

int peek_simple_unary_type(void)
{
    LexState _ls;
    int save_long_suffix;
    int save_unsigned_suffix;
    int t;
    struct Sym *s;

    _ls = lex_save();
    save_long_suffix = g_tok_long_suffix;
    save_unsigned_suffix = g_tok_unsigned_suffix;

    t = TYPE_INT;

    if (g_lex.tok.kind == '(') {
        next_token();
        if (starts_type()) {
            t = parse_type();
            if (g_lex.tok.kind == ')') {
                lex_restore(&_ls);
                g_tok_long_suffix = save_long_suffix; g_tok_unsigned_suffix = save_unsigned_suffix;
                return promote_int_type(t);
            }
        } else {
            /*
             * Parenthesized expression (not a cast): peek the type of its
             * first operand so a compound RHS such as (fa * fb) or (la + lb)
             * is predicted as float / long instead of defaulting to int.
             * This keeps type lookahead from predicting 16-bit arithmetic
             * for x + (fa * fb) when the operands are float or long.
             * The recursion is bounded by paren nesting and only
             * refines the lookahead; it returns the inner operand's promoted
             * type.
             */
            int inner = peek_simple_unary_type();
            lex_restore(&_ls);
            g_tok_long_suffix = save_long_suffix; g_tok_unsigned_suffix = save_unsigned_suffix;
            return inner;
        }
    } else if (g_lex.tok.kind == TOK_FLOATLIT) {
        t = TYPE_FLOAT;
    } else if (g_lex.tok.kind == TOK_NUM) {
        if (g_lex.tok.val > 0xffffL || g_lex.tok.val < -32768L || g_tok_long_suffix)
            t = TYPE_LONG;
        else
            t = TYPE_INT;
        if (g_tok_unsigned_suffix)
            t |= TYPE_UNSIGNED;
    } else if (g_lex.tok.kind == TOK_CHARLIT) {
        t = TYPE_INT;
    } else if (g_lex.tok.kind == TOK_ID) {
        s = find_sym(g_lex.tok.text);
        if (s) {
            int is_arr;
            int tt;
            t = s->type;

            /* For lookahead purposes, recognize calls through function
             * pointers and function-pointer arrays as returning int.
             * Without this, an expression like:
             *
             *     tab[0](30) + tab[1](40)
             *
             * is misclassified as integer + pointer before the RHS is
             * generated, so gen_add() applies pointer scaling to the left
             * call result.  This compiler only tracks int-returning
             * function pointers today, which matches the supported
             * declarator forms. */
            tt = t;
            is_arr = s->is_array;
            next_token();
            {
                int nsubs;
                int dim_count;
                int base_type;
                nsubs = 0;
                dim_count = s->dim_count;
                base_type = s->type;

                for (;;) {
                    if (g_lex.tok.kind == '[') {
                        skip_balanced_bracket('[', ']');
                        nsubs++;

                        if (is_arr) {
                            /* A real array subscript consumes one array dimension.
                             * If dimensions remain, the result is still an array
                             * expression that decays to a pointer in value context;
                             * otherwise it is the scalar element type. */
                            if (dim_count > 0 && nsubs < dim_count)
                                tt = type_add_ptr(base_type);
                            else
                                tt = base_type;
                            if (dim_count <= 0 || nsubs >= dim_count)
                                is_arr = 0;
                            continue;
                        } else if (dim_count > 0 && type_ptr_depth(base_type) > 0) {
                            /* Pointer-to-array declarators need one more subscript
                             * than their stored array-dimension count to reach the
                             * scalar element. */
                            if (nsubs <= dim_count)
                                tt = type_add_ptr(type_decay_ptr(base_type));
                            else
                                tt = type_decay_ptr(base_type);
                            continue;
                        } else {
                            tt = type_decay_ptr(tt);
                            continue;
                        }
                    }

                    if (g_lex.tok.kind == '.' || g_lex.tok.kind == TOK_ARROW) {
                        struct FieldDef *fd;
                        int sid;

                        next_token();
                        if (g_lex.tok.kind != TOK_ID)
                            break;

                        sid = base_struct_id_from_type(tt);
                        fd = find_field_def(sid, g_lex.tok.text);
                        if (!fd)
                            break;

                        tt = fd->is_array ? fd->elem_type : fd->type;
                        is_arr = fd->is_array;
                        dim_count = fd->dim_count;
                        base_type = fd->elem_type;
                        nsubs = 0;
                        next_token();
                        continue;
                    }

                    break;
                }
            }
            t = tt;
            if (g_lex.tok.kind == '(' && type_ptr_depth(tt) > 0)
                t = TYPE_INT;
        }
    }

    lex_restore(&_ls);
    g_tok_long_suffix = save_long_suffix;
    g_tok_unsigned_suffix = save_unsigned_suffix;
    return promote_int_type(t);
}
