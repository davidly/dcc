/*
 * dcc_expr.c - expression code generation (core).
 *
    if (!accept('*')) {
 * through HL, struct copies, cast detection and numeric conversions
 * (byte/int/long/float), bitfield extract/insert, pre/post increment-decrement,
 * and function-call stack cleanup. Also holds declaration-side parsing helpers
 * (sizeof operands, function-pointer and array declarators, initializer-atom
 * counting, enum constants, and user-label bookkeeping).
 *
 * MODULE: compiled as its own translation unit; shared declarations are in dcc.h.
 * Source provenance: monolith src/ddc.c lines 5373-8841.
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
    while (tok.kind != TOK_EOF && tok.kind != ')' && tok.kind != ']' &&
           tok.kind != ',' && tok.kind != ';') {
        op = tok.kind;

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


void emit_load_from_hl(int type)
{
    if (type_size(type) == 1) {
        emit("\tld l,(hl)\n");
        if (type & TYPE_UNSIGNED)
            emit("\tld h,0\n");
        else
            emit("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n");
    } else if (type_size(type) == 4) {
        /* read 4 bytes little-endian from [HL] into DE:HL (DE=high, HL=low) */
        emit("\tld e,(hl)\n");
        emit("\tinc hl\n");
        emit("\tld d,(hl)\n");
        emit("\tinc hl\n");
        emit("\tld a,(hl)\n");
        emit("\tinc hl\n");
        emit("\tld h,(hl)\n");
        emit("\tld l,a\n");
        /* now H=high_hi, L=high_lo, D=low_hi, E=low_lo — need DE:HL swapped */
        emit("\tex de,hl\n"); /* HL=low word, DE=high word */
    } else {
        emit("\tld e,(hl)\n");
        emit("\tinc hl\n");
        emit("\tld d,(hl)\n");
        emit("\tex de,hl\n");
    }
}

void emit_store_de_to_addr_hl(int type)
{
    if (type_size(type) == 1) {
        if (type_is_bool(type))
            emit("\tld a,e\n\tor a\n\tld e,0\n\tjr z,$+3\n\tinc e\n");
        emit("\tld (hl),e\n");
    } else if (type_size(type) == 4) {
        /* value in DE:HL (DE=high, HL=low), address on stack (2 bytes) */
        emit("\tld b,d\n\tld c,e\n"); /* BC = high word */
        emit("\tpop de\n");           /* DE = address (popped from where caller pushed it) */
        emit("\tex de,hl\n");         /* HL = address, DE = low word */
        emit("\tld (hl),e\n");
        emit("\tinc hl\n");
        emit("\tld (hl),d\n");
        emit("\tinc hl\n");
        emit("\tld (hl),c\n");
        emit("\tinc hl\n");
        emit("\tld (hl),b\n");
    } else {
        emit("\tld (hl),e\n");
        emit("\tinc hl\n");
        emit("\tld (hl),d\n");
    }
}

void emit_bool_normalize_hl(int source_type)
{
    if (type_is_float(source_type))
        emit_convert_float_to_intlike(TYPE_INT);

    if (type_is_long(source_type))
        emit("\tld a,h\n\tor l\n\tor d\n\tor e\n");
    else
        emit("\tld a,h\n\tor l\n");

    /* Reduce the (non-)zero flag to a canonical 0/1 in HL using a short forward
     * skip instead of a two-label jump sequence.  The compact form matters for
     * large unoptimised builds, where the verbose sequence can bloat the object
     * enough to exceed the CP/M linker's memory (e.g. the a1 interpreter). */
    emit("\tld hl,0\n\tjr z,$+3\n\tinc l\n");
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

void emit_copy_de_to_hl_bytes(int n)
{
    int lab;

    if (n <= 0)
        return;

    lab = new_label();
    if (n <= 255) {
        fprintf(outf, "\tld b,%d\n", n);
        emit_label(lab);
        emit("\tld a,(de)\n");
        emit("\tld (hl),a\n");
        emit("\tinc de\n");
        emit("\tinc hl\n");
        fprintf(outf, "\tdjnz L%d\n", lab);
    } else {
        fprintf(outf, "\tld bc,%d\n", n);
        emit_label(lab);
        emit("\tld a,(de)\n");
        emit("\tld (hl),a\n");
        emit("\tinc de\n");
        emit("\tinc hl\n");
        emit("\tdec bc\n");
        emit("\tld a,b\n");
        emit("\tor c\n");
        emit_jp_label("jp nz,", lab);
    }
}

void emit_push_struct_arg_from_hl(int n)
{
    if (n <= 0)
        return;
    emit("\tex de,hl\n");          /* DE = source */
    fprintf(outf, "\tld hl,-%d\n", n);
    emit("\tadd hl,sp\n");        /* HL = destination */
    emit("\tld sp,hl\n");
    emit_copy_de_to_hl_bytes(n);
}

void emit_load_hl_from_sp_offset(int off)
{
    if (off == 0) {
        emit("\tpop hl\n\tpush hl\n");
    } else {
        fprintf(outf, "\tld hl,%d\n", off);
        emit("\tadd hl,sp\n");
        emit("\tld e,(hl)\n");
        emit("\tinc hl\n");
        emit("\tld d,(hl)\n");
        emit("\tex de,hl\n");
    }
}

void gen_expr(void);
void gen_expr_no_comma(void);
void gen_unary(void);
void gen_snippet_lvalue_addr(const char *snippet, int *ptype);
void gen_statement(void);
int parse_funcptr_declarator(int *ptype, char *name, int namesz)
{
    int type;
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    struct Token save_tok;

    g_funcptr_decl_array_len = 0;
    g_funcptr_is_funcret_decl = 0;
    g_ptr_array_dim_count = 0;
    g_ptr_array_elem_size = 0;
    memset(g_ptr_array_dims, 0, sizeof(g_ptr_array_dims));

    if (tok.kind != '(')
        return 0;

    save_pos = posi;
    save_tok_start = tok_start_pos;
    save_line = line_no;
    save_tok_line = tok_line;
    save_tok = tok;

    next_token();
    if (!accept('*')) {
        posi = save_pos;
        tok_start_pos = save_tok_start;
        line_no = save_line;
        tok_line = save_tok_line;
        tok = save_tok;
        return 0;
    }
    skip_type_qualifiers();

    if (tok.kind == '(') {
        int depth;
        next_token();
        if (!accept('*') || tok.kind != TOK_ID) {
            posi = save_pos;
            tok_start_pos = save_tok_start;
            line_no = save_line;
            tok_line = save_tok_line;
            tok = save_tok;
            return 0;
        }
        strncpy(name, tok.text, namesz - 1);
        name[namesz - 1] = 0;
        next_token();
        if (!accept(')')) {
            posi = save_pos;
            tok_start_pos = save_tok_start;
            line_no = save_line;
            tok_line = save_tok_line;
            tok = save_tok;
            return 0;
        }
        if (accept('(')) {
            depth = 1;
            while (tok.kind != TOK_EOF && depth > 0) {
                if (tok.kind == '(') depth++;
                else if (tok.kind == ')') depth--;
                next_token();
            }
        }
        if (!accept(')')) {
            posi = save_pos;
            tok_start_pos = save_tok_start;
            line_no = save_line;
            tok_line = save_tok_line;
            tok = save_tok;
            return 0;
        }
        if (accept('(')) {
            depth = 1;
            while (tok.kind != TOK_EOF && depth > 0) {
                if (tok.kind == '(') depth++;
                else if (tok.kind == ')') depth--;
                next_token();
            }
        }
        type = type_add_ptr(ptype[0]);
        ptype[0] = type;
        return 1;
    }

    if (tok.kind != TOK_ID) {
        posi = save_pos;
        tok_start_pos = save_tok_start;
        line_no = save_line;
        tok_line = save_tok_line;
        tok = save_tok;
        return 0;
    }

    strncpy(name, tok.text, namesz - 1);
    name[namesz - 1] = 0;
    next_token();

    if (accept('[')) {
        if (tok.kind == ']') {
            g_funcptr_decl_array_len = 0;
            next_token();
        } else {
            g_funcptr_decl_array_len = parse_const_int_expr();
            expect(']');
        }
    }

    if (!accept(')')) {
        /* C89: return_type (*func_name(param_list))(pointed_fn_params)
         * A function declaration whose return type is a pointer to function.
         * The (*name has already been consumed; tok is now '(' (the param list). */
        if (tok.kind == '(') {
            int depth;
            next_token(); /* consume opening '(' of param list */
            parse_param_list();
            if (tok.kind != ')') {
                posi = save_pos;
                tok_start_pos = save_tok_start;
                line_no = save_line;
                tok_line = save_tok_line;
                tok = save_tok;
                g_funcptr_decl_array_len = 0;
                g_ptr_array_dim_count = 0;
                g_ptr_array_elem_size = 0;
                memset(g_ptr_array_dims, 0, sizeof(g_ptr_array_dims));
                return 0;
            }
            next_token(); /* consume ')' of name(...) */
            if (!accept(')')) {
                posi = save_pos;
                tok_start_pos = save_tok_start;
                line_no = save_line;
                tok_line = save_tok_line;
                tok = save_tok;
                g_funcptr_decl_array_len = 0;
                g_ptr_array_dim_count = 0;
                g_ptr_array_elem_size = 0;
                memset(g_ptr_array_dims, 0, sizeof(g_ptr_array_dims));
                return 0;
            }
            /* Skip the trailing (...) describing the pointed-to function's params */
            if (accept('(')) {
                depth = 1;
                while (tok.kind != TOK_EOF && depth > 0) {
                    if (tok.kind == '(') depth++;
                    else if (tok.kind == ')') depth--;
                    next_token();
                }
            }
            type = type_add_ptr(ptype[0]);
            ptype[0] = type;
            g_funcptr_is_funcret_decl = 1;
            return 1;
        }

        posi = save_pos;
        tok_start_pos = save_tok_start;
        line_no = save_line;
        tok_line = save_tok_line;
        tok = save_tok;
        g_funcptr_decl_array_len = 0;
        g_ptr_array_dim_count = 0;
        g_ptr_array_elem_size = 0;
        memset(g_ptr_array_dims, 0, sizeof(g_ptr_array_dims));
        return 0;
    }

    type = type_add_ptr(ptype[0]);

    if (accept('(')) {
        while (tok.kind != ')' && tok.kind != TOK_EOF)
            next_token();
        expect(')');
    } else if (tok.kind == '[') {
        int dims[8];
        int ndims;
        int i;
        int n;
        int elem_bytes;
        int total;

        ndims = 0;
        memset(dims, 0, sizeof(dims));
        while (accept('[')) {
            if (tok.kind == ']') {
                n = 0;
                next_token();
            } else {
                n = parse_const_int_expr();
                expect(']');
            }
            if (n < 0) n = 0;
            if (ndims < 8) dims[ndims++] = n;
        }

        elem_bytes = type_size(ptype[0]);
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
        for (i = 0; i < ndims && i < 8; ++i)
            g_ptr_array_dims[i] = dims[i];
    }

    ptype[0] = type;
    return 1;
}


int parse_abstract_funcptr_declarator(int *ptype)
{
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    struct Token save_tok;
    int type;

    if (tok.kind != '(')
        return 0;

    save_pos = posi;
    save_tok_start = tok_start_pos;
    save_line = line_no;
    save_tok_line = tok_line;
    save_tok = tok;

    next_token();
    if (!accept('*')) {
        posi = save_pos;
        tok_start_pos = save_tok_start;
        line_no = save_line;
        tok_line = save_tok_line;
        tok = save_tok;
        return 0;
    }

    if (!accept(')')) {
        posi = save_pos;
        tok_start_pos = save_tok_start;
        line_no = save_line;
        tok_line = save_tok_line;
        tok = save_tok;
        return 0;
    }

    type = type_add_ptr(ptype[0]);

    if (accept('(')) {
        while (tok.kind != ')' && tok.kind != TOK_EOF)
            next_token();
        expect(')');
    } else if (tok.kind == '[') {
        while (accept('[')) {
            skip_parameter_array_qualifiers();
            if (tok.kind != ']')
                (void)parse_const_int_expr();
            expect(']');
        }
    } else {
        posi = save_pos;
        tok_start_pos = save_tok_start;
        line_no = save_line;
        tok_line = save_tok_line;
        tok = save_tok;
        return 0;
    }

    ptype[0] = type;
    return 1;
}

int char_array_string_initializer_size(int base_type)
{
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    struct Token save_tok;
    int n;

    if ((base_type & 15) != TYPE_CHAR || type_ptr_depth(base_type) != 0)
        return 0;
    if (tok.kind != '=')
        return 0;

    save_pos = posi;
    save_tok_start = tok_start_pos;
    save_line = line_no;
    save_tok_line = tok_line;
    save_tok = tok;

    next_token();
    if (tok.kind == TOK_STR) {
        char *lit;
        int is_wide;

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
        lit = read_adjacent_string_literals_ex(&is_wide);
        if (is_wide)
            n = 0;
        else
            n = (int)strlen(lit) + 1;
        free(lit);
    } else {
        n = 0;
    }

    posi = save_pos;
    tok_start_pos = save_tok_start;
    line_no = save_line;
    tok_line = save_tok_line;
    tok = save_tok;
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

    if (tok.kind != open)
        return;
    depth = 1;
    next_token();
    while (tok.kind != TOK_EOF && depth > 0) {
        if (tok.kind == open)
            depth++;
        else if (tok.kind == close)
            depth--;
        next_token();
    }
}

static void skip_sizeof_array_dim_operand(void)
{
    int done;

    if (tok.kind == TOK_SIZEOF)
        next_token();

    while (tok.kind == TOK_SIZEOF || tok.kind == '*' || tok.kind == '&' ||
           tok.kind == '+' || tok.kind == '-' || tok.kind == '!' ||
           tok.kind == '~') {
        if (tok.kind == TOK_SIZEOF)
            next_token();
        else
            next_token();
    }

    if (tok.kind == '(') {
        skip_array_dim_balanced('(', ')');
        return;
    }

    if (tok.kind == TOK_ID || tok.kind == TOK_NUM || tok.kind == TOK_CHARLIT ||
        tok.kind == TOK_STR || tok.kind == TOK_WSTR) {
        next_token();
        done = 0;
        while (!done) {
            if (tok.kind == '[') {
                skip_array_dim_balanced('[', ']');
            } else if (tok.kind == '(') {
                skip_array_dim_balanced('(', ')');
            } else if (tok.kind == '.' || tok.kind == TOK_ARROW) {
                next_token();
                if (tok.kind == TOK_ID)
                    next_token();
            } else {
                done = 1;
            }
        }
    }
}

static int array_dim_has_runtime_identifier(void)
{
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    struct Token save_tok;
    int depth;
    int has_runtime;

    save_pos = posi;
    save_tok_start = tok_start_pos;
    save_line = line_no;
    save_tok_line = tok_line;
    save_tok = tok;

    depth = 0;
    has_runtime = 0;
    while (tok.kind != TOK_EOF) {
        if (depth == 0 && tok.kind == ']')
            break;
        if (tok.kind == TOK_SIZEOF) {
            skip_sizeof_array_dim_operand();
            continue;
        }
        if (tok.kind == '(') {
            /* A parenthesized construct that begins with a type is a cast (or
             * parenthesized type): its type-name identifiers (e.g. size_t in
             * (size_t)8) are not runtime values, so skip the whole `(type)`
             * and keep scanning the operand.  An ordinary parenthesized
             * expression is counted normally so its identifiers are seen. */
            next_token();
            if (starts_type()) {
                int d2 = 1;
                while (tok.kind != TOK_EOF && d2 > 0) {
                    if (tok.kind == '(')
                        d2++;
                    else if (tok.kind == ')')
                        d2--;
                    next_token();
                }
            } else {
                depth++;
            }
            continue;
        }
        if (tok.kind == TOK_ID && find_enum_const(tok.text) < 0) {
            has_runtime = 1;
            break;
        }
        if (tok.kind == '[' || tok.kind == '{')
            depth++;
        else if (tok.kind == ')' || tok.kind == ']' || tok.kind == '}') {
            if (depth > 0)
                depth--;
        }
        next_token();
    }

    posi = save_pos;
    tok_start_pos = save_tok_start;
    line_no = save_line;
    tok_line = save_tok_line;
    tok = save_tok;
    return has_runtime;
}

/*
 * Skip tokens up to the `]` that closes the current array dimension, honoring
 * nested brackets/parens/braces so a subscript or call inside the dimension
 * expression (e.g. `b[a[n-1] + 2]`) does not stop early on an inner `]`.  The
 * opening `[` of the dimension has already been consumed by the caller; on
 * return the closing `]` has been consumed too.
 */
static void skip_array_dim_to_close(void)
{
    int depth = 0;
    while (tok.kind != TOK_EOF) {
        if (depth == 0 && tok.kind == ']')
            break;
        if (tok.kind == '(' || tok.kind == '[' || tok.kind == '{')
            depth++;
        else if (tok.kind == ')' || tok.kind == '}')
            { if (depth > 0) depth--; }
        else if (tok.kind == ']')
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
    int dims[8];
    int ndims;
    int i;
    int n;
    int elem_bytes;
    int total;
    int inner;

    ndims = 0;
    g_last_array_dim_count = 0;
    g_vla_pending = 0;
    memset(g_last_array_dims, 0, sizeof(g_last_array_dims));

    while (accept('[')) {
        if (tok.kind == ']') {
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
                    g_vla_dim_posi = posi;
                    g_vla_dim_tok_start = tok_start_pos;
                    g_vla_dim_line = line_no;
                    g_vla_dim_tok_line = tok_line;
                    g_vla_dim_tok = tok;
                    skip_array_dim_to_close();
                    n = 0;
                } else {
                    if (asm_suppress_depth == 0)
                        error_here("variable length arrays are not supported; use malloc and an explicit pointer");
                    skip_array_dim_to_close();
                    n = 0;
                }
            } else {
                n = parse_const_int_expr();
                expect(']');
            }
        }

        if (n < 0)
            n = 0;
        if (ndims < 8)
            dims[ndims++] = n;
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
        total *= dims[i];
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
        inner *= dims[i];
    }

    total_len[0] = total;
    first_stride_bytes[0] = (ndims > 1 && inner > 0) ? inner * elem_bytes : elem_bytes;

    g_last_array_dim_count = ndims;
    for (i = 0; i < ndims && i < 8; ++i)
        g_last_array_dims[i] = dims[i];
}




int count_initializer_atoms_level(void)
{
    int n;
    int depth;

    n = 0;

    if (accept('{')) {
        while (tok.kind != TOK_EOF && tok.kind != '}') {
            n += count_initializer_atoms_level();
            if (!accept(','))
                break;
            if (tok.kind == '}')
                break;
        }
        expect('}');
        return n;
    }

    depth = 0;
    while (tok.kind != TOK_EOF) {
        if (depth == 0 && (tok.kind == ',' || tok.kind == '}'))
            break;

        if (tok.kind == '(' || tok.kind == '[' || tok.kind == '{') {
            depth++;
        } else if (tok.kind == ')' || tok.kind == ']' || tok.kind == '}') {
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
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    struct Token save_tok;
    int n;

    save_pos = posi;
    save_tok_start = tok_start_pos;
    save_line = line_no;
    save_tok_line = tok_line;
    save_tok = tok;

    n = 0;
    if (accept('=') && tok.kind == '{')
        n = count_initializer_atoms_level();

    posi = save_pos;
    tok_start_pos = save_tok_start;
    line_no = save_line;
    tok_line = save_tok_line;
    tok = save_tok;
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
        while (tok.kind != TOK_EOF && tok.kind != '}') {
            count_initializer_atoms_level();   /* skip one whole element */
            n++;
            if (!accept(','))
                break;
            if (tok.kind == '}')
                break;
        }
        expect('}');
    }
    return n;
}

int count_omitted_array_initializer_top_elems(void)
{
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    struct Token save_tok;
    int n;

    save_pos = posi;
    save_tok_start = tok_start_pos;
    save_line = line_no;
    save_tok_line = tok_line;
    save_tok = tok;

    n = 0;
    if (accept('=') && tok.kind == '{')
        n = count_initializer_top_elems_level();

    posi = save_pos;
    tok_start_pos = save_tok_start;
    line_no = save_line;
    tok_line = save_tok_line;
    tok = save_tok;
    return n;
}

void emit_init_auto_char_array_from_string(struct Sym *s, const char *str)
{
    int i;
    int n;
    int limit;

    n = (int)strlen(str) + 1;
    limit = s->size;
    if (limit <= 0)
        limit = n;

    /*
     * Automatic aggregate initializers must zero-fill any elements not
     * explicitly initialized.  The old code emitted only the string bytes
     * plus the first NUL, leaving the rest of a larger local char array
     * containing old stack contents.
     *
     * For char s[3] = "abc", limit is the declared size and the terminating
     * NUL is correctly not emitted.  For char s[8] = "abc", bytes 4..7 are
     * now explicitly zeroed.
     */
    for (i = 0; i < limit; ++i) {
        int ch;
        ch = (i + 1 < n) ? ((unsigned char)str[i]) : 0;
        emit_load_sym_addr(s);
        emit_add_const_to_hl(i);
        fprintf(outf, "\tld e,%d\n", ch);
        emit_store_de_to_addr_hl(TYPE_CHAR);
    }
}

void parse_typedef_decl(void);
void parse_global_init_list(struct Sym *s);
void scan_static_local_decl_after_type(int base);
char *copy_range(long a, long b);
void gen_snippet_expr(const char *snippet);
void emit_incdec_addr(int type, int op);

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
    ulabel_vla_depth[nulabels] = 0;
    ulabel_ref_vla_depth[nulabels] = 0;
    ulabel_ref_vla_restored[nulabels] = 0;
    return nulabels++;
}

int mark_user_label_reference(const char *name)
{
    int i;

    i = find_or_alloc_user_label_index(name);
    ulabel_referenced[i] = 1;
    return ulabel_ids[i];
}

int define_user_label(const char *name)
{
    int i;

    i = find_or_alloc_user_label_index(name);
    if (ulabel_defined[i])
        error_here("duplicate goto label");
    ulabel_defined[i] = 1;
    ulabel_vla_depth[i] = g_scope_depth;
    return ulabel_ids[i];
}

void check_undefined_user_labels(void)
{
    int i;

    for (i = 0; i < nulabels; ++i) {
        if (ulabel_referenced[i] && !ulabel_defined[i]) {
            char msg[96];
            sprintf(msg, "undefined goto label '%s'", ulabel_names[i]);
            dcc_error_at(tok.file[0] ? tok.file : (input_name ? input_name : "<input>"),
                         tok_line, -1, msg, NULL);
        }
    }
}

/* Parse an integer constant expression used in enum bodies.
 * Keep the signed target value in an int-sized host object; code generation
 * masks it back to the 16-bit target representation when emitted. */
int parse_enum_const_value(void)
{
    return (int)parse_const_long_expr();
}





void gen_post_update_symbol_addr_value(struct Sym *s, int op)
{
    int t;
    int elem;

    t = s->type;
    elem = type_index_elem_size(t);

    if (sym_can_ix_direct(s) || is_global_word_sym(s)) {
        /* A plain ix-direct local or global-word symbol (the overwhelmingly
         * common case for `p++`/`p--` on a pointer variable, e.g. the `pc`
         * in `*pc++ = val;`) never needs its own address computed at all -
         * emit_load_sym_value_direct/emit_store_hl_to_sym_direct already
         * know how to read and write it with a couple of plain `ld`
         * instructions each. The generic fallback below computes the
         * variable's address, then reads and writes back *through* that
         * address, which for exactly this case is a needless address
         * computation plus a pile of push/pop/ex shuffling to keep the old
         * value, the new value, and the variable's address all live at
         * once - the classic "store the pointer back to itself" dance that
         * is only actually required for lvalues without a direct load/store
         * form (out-of-range stack offsets, etc). */
        emit_load_sym_value_direct(s);  /* HL = old pointer value */
        emit("\tpush hl\n");             /* save old pointer value for lvalue */
        if (op == TOK_INC) {
            emit("\tinc hl\n");
            if (elem >= 2) emit("\tinc hl\n");
            if (elem >= 4) { emit("\tinc hl\n"); emit("\tinc hl\n"); }
        } else {
            emit("\tdec hl\n");
            if (elem >= 2) emit("\tdec hl\n");
            if (elem >= 4) { emit("\tdec hl\n"); emit("\tdec hl\n"); }
        }
        emit_store_hl_to_sym_direct(s);  /* store new pointer value */
        emit("\tpop hl\n");               /* HL = old pointer, used as lvalue address */
        g_expr_type = t;
        return;
    }

    emit_load_sym_addr(s);          /* HL = address of pointer variable */
    emit("\tpush hl\n");            /* save pointer variable address */
    emit_load_from_hl(t);           /* HL = old pointer value */
    emit("\tpush hl\n");            /* save old pointer value for lvalue */

    if (op == TOK_INC) {
        emit("\tinc hl\n");
        if (elem >= 2) emit("\tinc hl\n");
        if (elem >= 4) { emit("\tinc hl\n"); emit("\tinc hl\n"); }
    } else {
        emit("\tdec hl\n");
        if (elem >= 2) emit("\tdec hl\n");
        if (elem >= 4) { emit("\tdec hl\n"); emit("\tdec hl\n"); }
    }

    emit("\tex de,hl\n");           /* DE = new pointer value */
    emit("\tpop hl\n");             /* HL = old pointer value */
    emit("\tex (sp),hl\n");         /* HL = pointer variable address, stack = old pointer */
    emit_store_de_to_addr_hl(t);    /* store new pointer */
    emit("\tpop hl\n");             /* HL = old pointer, used as lvalue address */
    g_expr_type = t;
}


void gen_post_update_from_addr(int type, int op)
{
    if (type_is_long(type)) {
        if (expr_result_dead) {
            /* Statement context: just increment in place, no old value needed */
            emit_incdec_addr(type, op);
            return;
        }
        /* Expression context: return old value in DE:HL, store new value.
         * Save address in BC (HL will be destroyed by load), then store
         * the new value byte-by-byte using BC as the address. */
        int no_carry = new_label();
        emit("\tld b,h\n\tld c,l\n");    /* BC = address */
        emit_load_from_hl(type);          /* HL=low16_old, DE=high16_old */
        emit("\tpush de\n");              /* save high16_old for return */
        emit("\tpush hl\n");              /* save low16_old for return */
        if (op == TOK_INC) {
            emit("\tinc hl\n");
            emit("\tld a,h\n\tor l\n");
            emit_jp_label("jp nz,", no_carry);
            emit("\tinc de\n");           /* carry from low to high word */
        } else {
            emit("\tdec hl\n");
            emit("\tld a,h\n\tand l\n\tinc a\n"); /* A=0 only if HL==0xFFFF (borrow) */
            emit_jp_label("jp nz,", no_carry);
            emit("\tdec de\n");           /* borrow from high word */
        }
        emit_label(no_carry);
        /* HL=new_low, DE=new_high; stack: [..., high16_old, low16_old] */
        emit("\tpush hl\n");              /* save new_low */
        emit("\tld h,b\n\tld l,c\n");    /* HL = address */
        emit("\tpop bc\n");               /* BC = new_low */
        emit("\tld (hl),c\n\tinc hl\n"); /* store new_low[0] */
        emit("\tld (hl),b\n\tinc hl\n"); /* store new_low[1] */
        emit("\tld (hl),e\n\tinc hl\n"); /* store new_high[0] */
        emit("\tld (hl),d\n");           /* store new_high[1] */
        emit("\tpop hl\n");              /* HL = low16_old  (return value low) */
        emit("\tpop de\n");              /* DE = high16_old (return value high) */
        g_expr_type = type;
        return;
    }

    emit("\tpush hl\n");             /* address */
    emit_load_from_hl(type);
    emit("\tpush hl\n");             /* old value */

    if (op == TOK_INC) {
        emit("\tinc hl\n");
    } else {
        emit("\tdec hl\n");
    }

    emit("\tex de,hl\n");            /* DE = new */
    emit("\tpop hl\n");              /* HL = old */
    emit("\tex (sp),hl\n");          /* HL = addr, stack = old */
    emit_store_de_to_addr_hl(type);
    emit("\tpop hl\n");              /* expression result = old */
    g_expr_type = type;
}




void emit_promote_byte_to_int(int actual_type)
{
    if ((actual_type & 15) != TYPE_CHAR || type_ptr_depth(actual_type) != 0)
        return;

    if (actual_type & TYPE_UNSIGNED)
        emit("\tld h,0\n");
    else
        emit("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n");
}

void emit_promote_int_to_long(int actual_type, int expected_type)
{
    (void)expected_type;

    /*
     * A byte-typed expression, especially a function call returning
     * unsigned char, is only guaranteed to have its value in L.  Normalize
     * HL before forming DE:HL; otherwise stale/sign bits in H turn uint8_t
     * 255 into 65535 or 0xffffffff when widened.
     */
    emit_promote_byte_to_int(actual_type);

    if ((actual_type & TYPE_UNSIGNED) || type_ptr_depth(actual_type)) {
        emit("\tld de,0\n");
    } else {
        /* Sign-extend signed 16-bit HL into DE. */
        emit("\tld a,h\n");
        emit("\trlca\n");
        emit("\tsbc a,a\n");
        emit("\tld d,a\n");
        emit("\tld e,a\n");
    }
}


void emit_convert_int_to_float(int actual_type)
{
    if (type_is_long(actual_type)) {
        if (actual_type & TYPE_UNSIGNED)
            emit_runtime_call("__fulf");
        else
            emit_runtime_call("__flf");
        g_expr_type = TYPE_FLOAT;
        return;
    }
    if ((actual_type & TYPE_UNSIGNED) || type_ptr_depth(actual_type))
        emit_runtime_call("__fuf");
    else
        emit_runtime_call("__fif");
    g_expr_type = TYPE_FLOAT;
}

void emit_convert_float_to_intlike(int target_type)
{
    if (type_is_long(target_type)) {
        if (target_type & TYPE_UNSIGNED)
            emit_runtime_call("__fful");
        else
            emit_runtime_call("__ffl");
        g_expr_type = target_type;
        return;
    }

    if ((target_type & TYPE_UNSIGNED) || type_ptr_depth(target_type))
        emit_runtime_call("__ffu");
    else
        emit_runtime_call("__ffi");

    if (type_size(target_type) == 1) {
        if (target_type & TYPE_UNSIGNED)
            emit("\tld h,0\n");
        else
            emit("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n");
    }

    g_expr_type = target_type;
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



void emit_cleanup_stack_bytes(int bytes)
{
    int k;

    /*
     * POP BC is 1 byte and discards 2 bytes from the stack without touching
     * HL, DE, or flags — safe regardless of whether the callee returned a
     * 16-bit value (in HL) or a 32-bit value (in DE:HL).  Arguments are
     * always pushed in 2- or 4-byte units so bytes is always even; the
     * trailing inc sp is a safety net only.
     */
    for (k = bytes; k >= 2; k -= 2)
        emit("\tpop bc\n");
    if (k > 0)
        emit("\tinc sp\n");
}


int try_emit_push_struct_return_call_arg(const char *snippet, int want_type);


void emit_call_hl_from_stack_offset(int off)
{
    fprintf(outf, "\tld hl,%d\n", off);
    emit("\tadd hl,sp\n");
    emit("\tld e,(hl)\n");
    emit("\tinc hl\n");
    emit("\tld d,(hl)\n");
    emit("\tex de,hl\n");
    emit_runtime_call("__call_hl");
}


void emit_extract_bitfield(void)
{
    int i;
    unsigned int mask;
    int out_type;

    if (current_field_bit_width <= 0)
        return;

    out_type = (g_expr_type & TYPE_UNSIGNED) ? (TYPE_UNSIGNED | TYPE_INT) : TYPE_INT;

    for (i = 0; i < current_field_bit_shift; ++i)
        emit("\tsrl h\n\trr l\n");

    mask = (unsigned int)((1UL << current_field_bit_width) - 1UL);
    fprintf(outf, "\tld de,%u\n", mask & 0xffffU);
    emit("\tld a,l\n\tand e\n\tld l,a\n");
    emit("\tld a,h\n\tand d\n\tld h,a\n");

    if (!(out_type & TYPE_UNSIGNED) && current_field_bit_width < 16) {
        int lab;
        unsigned int signbit;
        unsigned int extend_mask;

        lab = new_label();
        signbit = (unsigned int)(1UL << (current_field_bit_width - 1));
        extend_mask = (~mask) & 0xffffU;

        fprintf(outf, "\tld de,%u\n", signbit & 0xffffU);
        emit("\tld a,l\n\tand e\n\tld e,a\n");
        emit("\tld a,h\n\tand d\n\tor e\n");
        fprintf(outf, "\tjp z,L%d\n", lab);
        fprintf(outf, "\tld de,%u\n", extend_mask);
        emit("\tld a,l\n\tor e\n\tld l,a\n");
        emit("\tld a,h\n\tor d\n\tld h,a\n");
        emit_label(lab);
    }

    g_expr_type = out_type;
}

void emit_store_bitfield_from_hl(void)
{
    int i;
    unsigned int clear_mask;
    unsigned int mask;

    mask = current_field_bit_mask & 0xffffU;
    clear_mask = (~mask) & 0xffffU;

    /* Stack top is the field storage-unit address, value is in HL. */
    emit("\tex de,hl\n");       /* DE = new field value */
    emit("\tpop hl\n");        /* HL = storage-unit address */
    emit("\tpush hl\n");       /* keep address for final store */
    emit("\tpush de\n");       /* keep raw field value */
    emit_load_from_hl(TYPE_INT); /* HL = old storage-unit word */

    fprintf(outf, "\tld de,%u\n", clear_mask);
    emit("\tld a,l\n\tand e\n\tld l,a\n");
    emit("\tld a,h\n\tand d\n\tld h,a\n");

    emit("\tpop de\n");        /* DE = raw field value */
    for (i = 0; i < current_field_bit_shift; ++i)
        emit("\tsla e\n\trl d\n");

    fprintf(outf, "\tld bc,%u\n", mask);
    emit("\tld a,e\n\tand c\n\tld e,a\n");
    emit("\tld a,d\n\tand b\n\tld d,a\n");
    emit("\tld a,l\n\tor e\n\tld l,a\n");
    emit("\tld a,h\n\tor d\n\tld h,a\n");

    emit("\tex de,hl\n");       /* DE = merged storage-unit word */
    emit("\tpop hl\n");        /* HL = address */
    emit_store_de_to_addr_hl(TYPE_INT);
}

void emit_load_float_bits(unsigned long bits);
void emit_load_const_sym_value(struct Sym *s);
void emit_float_compare_call(int op);





int paren_starts_cast(void)
{
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    struct Token save_tok;
    int r;

    if (tok.kind != '(')
        return 0;

    save_pos = posi;
    save_tok_start = tok_start_pos;
    save_line = line_no;
    save_tok_line = tok_line;
    save_tok = tok;

    next_token();
    r = starts_type();

    posi = save_pos;
    tok_start_pos = save_tok_start;
    line_no = save_line;
    tok_line = save_tok_line;
    tok = save_tok;

    return r;
}




void emit_incdec_value_in_dehl(int type, int op)
{
    int no_carry;

    if (type_ptr_depth(type) > 0) {
        int elem;
        elem = type_index_elem_size(type);
        if (op == TOK_INC) {
            emit_add_const_to_hl(elem);
        } else {
            emit_ld_de_const(elem);
            emit("\tor a\n\tsbc hl,de\n");
        }
        return;
    }

    if (type_is_long(type)) {
        no_carry = new_label();
        if (op == TOK_INC) {
            emit("\tinc hl\n");
            emit("\tld a,h\n\tor l\n");
            emit_jp_label("jp nz,", no_carry);
            emit("\tinc de\n");
        } else {
            emit("\tld a,h\n\tor l\n");
            emit_jp_label("jp nz,", no_carry);
            emit("\tdec de\n");
            emit_label(no_carry);
            emit("\tdec hl\n");
            return;
        }
        emit_label(no_carry);
    } else {
        if (op == TOK_INC)
            emit("\tinc hl\n");
        else
            emit("\tdec hl\n");

        /* The Z80 update is done in 16-bit HL even for 8-bit objects.
         * For ++uint8_t at 0xff this leaves HL == 0x0100 unless we narrow
         * it back to the stored object type.  That corrupts expressions such
         * as m[0x100 + ++sp], where the stored byte wraps to 0 but the
         * returned expression value was still 0x0100. */
        emit_promote_byte_to_int(type);
    }
}

void emit_pre_incdec_lvalue(int type, int op)
{
    if (type_is_long(type)) {
        /* Address is in HL.  Save it, load DE:HL, update full 32-bit value,
         * store through the saved address, and leave the new value in DE:HL. */
        emit("\tpush hl\n");
        emit_load_from_hl(type);
        emit_incdec_value_in_dehl(type, op);
        emit("\tpop bc\n");
        emit("\tld a,l\n\tld (bc),a\n\tinc bc\n");
        emit("\tld a,h\n\tld (bc),a\n\tinc bc\n");
        emit("\tld a,e\n\tld (bc),a\n\tinc bc\n");
        emit("\tld a,d\n\tld (bc),a\n");
    } else {
        emit("\tpush hl\n");
        emit_load_from_hl(type);
        emit_incdec_value_in_dehl(type, op);
        emit("\tex de,hl\n\tpop hl\n");
        emit_store_de_to_addr_hl(type);
        emit("\tex de,hl\n");
    }
    g_expr_type = type;
}

