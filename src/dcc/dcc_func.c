/*
 * dcc_func.c - function and top-level declaration parsing.
 *
 * Parameter lists (prototype and K&R old-style), function prologue/epilogue
 * and frame layout, the function-body scan, typedef declarations, and parsing
 * plus emission of file-scope objects and their initializers
 * (parse_function_or_global, parse_translation_unit).
 *
 * MODULE: compiled as its own translation unit; shared declarations are in dcc.h.
 * Source provenance: monolith src/ddc.c lines 15880-17705.
 */

#include "dcc.h"
#include "dcc_ast.h"

static int inline_param_index(struct Sym *s, const char *name)
{
    int i;
    if (s == NULL || name == NULL)
        return -1;
    for (i = 0; i < s->proto_nargs && i < MAX_PROTO_PARAMS; ++i)
        if (!strcmp(s->inline_param_names[i], name))
            return i;
    return -1;
}

static int inline_expr_touches_param(struct Sym *fn, const struct AstNode *n)
{
    int i;

    if (n == NULL)
        return 0;
    if (n->kind == AST_IDENT)
        return inline_param_index(fn, n->sval) >= 0;
    if (inline_expr_touches_param(fn, n->a) || inline_expr_touches_param(fn, n->b) ||
        inline_expr_touches_param(fn, n->c) || inline_expr_touches_param(fn, n->d))
        return 1;
    for (i = 0; i < n->list_len; ++i)
        if (inline_expr_touches_param(fn, n->list[i]))
            return 1;
    return 0;
}

static int inline_expr_is_simple(struct Sym *fn, const struct AstNode *n)
{
    int i;

    if (n == NULL)
        return 0;
    switch (n->kind) {
    case AST_INT_LIT:
    case AST_FLOAT_LIT:
    case AST_STR_LIT:
    case AST_SIZEOF_EXPR:
    case AST_SIZEOF_TYPE:
        return 1;
    case AST_IDENT:
        i = inline_param_index(fn, n->sval);
        if (i >= 0) {
            if (i < MAX_PROTO_PARAMS)
                fn->inline_param_use_count[i]++;
            return 1;
        }
        return find_global(n->sval) != NULL;
    case AST_UNARY:
        /* ++/-- substituted verbatim onto a parameter would mutate the
         * caller's argument expression, so only allow it on operands that
         * don't reach a parameter (e.g. globals). */
        if ((n->op == TOK_INC || n->op == TOK_DEC) && inline_expr_touches_param(fn, n->a))
            return 0;
        return inline_expr_is_simple(fn, n->a);
    case AST_POSTFIX:
        if (inline_expr_touches_param(fn, n->a))
            return 0;
        return inline_expr_is_simple(fn, n->a);
    case AST_BINARY:
    case AST_LOGAND:
    case AST_LOGOR:
    case AST_INDEX:
    case AST_COMMA:
        return inline_expr_is_simple(fn, n->a) && inline_expr_is_simple(fn, n->b);
    case AST_ASSIGN:
        return inline_expr_is_simple(fn, n->a) && inline_expr_is_simple(fn, n->b);
    case AST_MEMBER:
        return inline_expr_is_simple(fn, n->a);
    case AST_COND:
        return inline_expr_is_simple(fn, n->a) && inline_expr_is_simple(fn, n->b) &&
               inline_expr_is_simple(fn, n->c);
    case AST_CAST:
        return inline_expr_is_simple(fn, n->a);
    case AST_CALL:
        if (n->a == NULL || n->a->kind != AST_IDENT)
            return 0;
        for (i = 0; i < n->list_len; ++i)
            if (!inline_expr_is_simple(fn, n->list[i]))
                return 0;
        return 1;
    default:
        return 0;
    }
}

static struct AstNode *inline_return_expr_from_seq(struct AstNode *body, int index);

static struct AstNode *inline_stmt_return_expr(struct AstNode *n)
{
    if (n == NULL)
        return NULL;
    if (n->kind == AST_RETURN)
        return n->a;
    if (n->kind == AST_COMPOUND)
        return inline_return_expr_from_seq(n, 0);
    return NULL;
}

static struct AstNode *inline_void_seq_to_expr(struct AstNode *n, int index)
{
    struct AstNode *stmt;
    struct AstNode *rest;
    struct AstNode *comma;
    struct AstNode *zero;

    if (n == NULL)
        return NULL;
    if (n->kind != AST_COMPOUND) {
        /* A bare (unbraced) single statement. */
        if (n->kind != AST_EXPR_STMT || n->a == NULL)
            return NULL;
        return n->a;
    }
    if (index >= n->list_len) {
        zero = ast_new(&g_ast_inline_arena, AST_INT_LIT);
        zero->ival = 0;
        zero->type = TYPE_INT;
        return zero;
    }
    stmt = n->list[index];
    if (stmt == NULL || stmt->kind != AST_EXPR_STMT || stmt->a == NULL)
        return NULL;
    rest = inline_void_seq_to_expr(n, index + 1);
    if (rest == NULL)
        return NULL;
    comma = ast_new(&g_ast_inline_arena, AST_COMMA);
    comma->op = ',';
    comma->a = stmt->a;
    comma->b = rest;
    comma->type = 0;
    return comma;
}

static struct AstNode *inline_return_expr_from_seq(struct AstNode *body, int index)
{
    struct AstNode *stmt;
    struct AstNode *then_expr;
    struct AstNode *else_expr;
    struct AstNode *rest_expr;
    struct AstNode *cond;
    struct AstNode *comma;
    struct AstNode *guard_expr;
    struct AstNode *zero;

    if (body == NULL || body->kind != AST_COMPOUND || index >= body->list_len)
        return NULL;

    stmt = body->list[index];
    if (stmt == NULL)
        return NULL;

    if (stmt->kind == AST_RETURN)
        return (index == body->list_len - 1) ? stmt->a : NULL;

    if (stmt->kind == AST_EXPR_STMT && stmt->a != NULL) {
        /* A side-effecting statement ahead of the eventual return: fold it
         * into a comma expression so it still executes exactly once, in
         * order, when the whole sequence is substituted at the call site. */
        rest_expr = inline_return_expr_from_seq(body, index + 1);
        if (rest_expr == NULL)
            return NULL;
        comma = ast_new(&g_ast_inline_arena, AST_COMMA);
        comma->op = ',';
        comma->a = stmt->a;
        comma->b = rest_expr;
        comma->type = 0;
        return comma;
    }

    if (stmt->kind != AST_IF)
        return NULL;

    then_expr = inline_stmt_return_expr(stmt->b);
    if (then_expr == NULL) {
        /* Not a return-producing branch: allow a side-effect-only guard
         * with no else, e.g. `if (sp <= 0) die("empty");` ahead of the
         * real return - folded as `(cond ? (side effects, 0) : 0), rest`
         * so it still runs exactly once, in order. */
        if (stmt->c != NULL)
            return NULL;
        guard_expr = inline_void_seq_to_expr(stmt->b, 0);
        if (guard_expr == NULL)
            return NULL;
        rest_expr = inline_return_expr_from_seq(body, index + 1);
        if (rest_expr == NULL)
            return NULL;
        zero = ast_new(&g_ast_inline_arena, AST_INT_LIT);
        zero->ival = 0;
        zero->type = TYPE_INT;
        cond = ast_new(&g_ast_inline_arena, AST_COND);
        cond->a = stmt->a;
        cond->b = guard_expr;
        cond->c = zero;
        cond->type = 0;
        comma = ast_new(&g_ast_inline_arena, AST_COMMA);
        comma->op = ',';
        comma->a = cond;
        comma->b = rest_expr;
        comma->type = 0;
        return comma;
    }

    if (stmt->c != NULL) {
        if (index != body->list_len - 1)
            return NULL;
        else_expr = inline_stmt_return_expr(stmt->c);
        if (else_expr == NULL)
            return NULL;
    } else {
        rest_expr = inline_return_expr_from_seq(body, index + 1);
        if (rest_expr == NULL)
            return NULL;
        else_expr = rest_expr;
    }

    cond = ast_new(&g_ast_inline_arena, AST_COND);
    cond->a = stmt->a;
    cond->b = then_expr;
    cond->c = else_expr;
    cond->type = 0;
    return cond;
}

static int inline_void_stmt_seq_is_simple(struct Sym *fn, const struct AstNode *n);

static int inline_void_body_stmt_is_simple(struct Sym *fn, const struct AstNode *stmt)
{
    if (stmt == NULL)
        return 0;
    if (stmt->kind == AST_EXPR_STMT)
        return stmt->a != NULL && inline_expr_is_simple(fn, stmt->a);
    if (stmt->kind == AST_IF) {
        if (!inline_expr_is_simple(fn, stmt->a))
            return 0;
        if (!inline_void_stmt_seq_is_simple(fn, stmt->b))
            return 0;
        if (stmt->c != NULL && !inline_void_stmt_seq_is_simple(fn, stmt->c))
            return 0;
        return 1;
    }
    return 0;
}

static int inline_void_stmt_seq_is_simple(struct Sym *fn, const struct AstNode *n)
{
    int i;

    if (n == NULL)
        return 0;
    if (n->kind != AST_COMPOUND)
        return inline_void_body_stmt_is_simple(fn, n);
    for (i = 0; i < n->list_len; ++i)
        if (!inline_void_body_stmt_is_simple(fn, n->list[i]))
            return 0;
    return 1;
}

static int inline_void_stmt_body_is_simple(struct Sym *fn, const struct AstNode *n)
{
    if (n == NULL || n->kind != AST_COMPOUND || n->list_len <= 0)
        return 0;
    return inline_void_stmt_seq_is_simple(fn, n);
}

static void record_inline_function_if_simple(struct Sym *s)
{
    long sv_pos;
    long sv_tok_start;
    int sv_line;
    int sv_tok_line;
    struct Token sv_tok;
    struct AstNode *body;
    struct AstNode *ret_expr;
    int i;
    int nparams;
    size_t namelen;

    if (s == NULL || !s->is_static || !s->is_inline || tok.kind != '{')
        return;
    if ((s->type & 15) != TYPE_VOID &&
        (!(type_size(s->type) == 2 || type_size(s->type) == 4) ||
         type_is_bool(s->type) || type_is_struct_object(s->type)))
        return;

    nparams = 0;
    for (i = 0; i < nlocals && nparams < MAX_PROTO_PARAMS; ++i) {
        if (locals[i].storage == SC_PARAM) {
            if (!(type_size(locals[i].type) == 2 || type_size(locals[i].type) == 4) ||
                type_is_struct_object(locals[i].type))
                return;
            namelen = strlen(locals[i].name);
            if (namelen > sizeof(s->inline_param_names[nparams]) - 1)
                namelen = sizeof(s->inline_param_names[nparams]) - 1;
            memcpy(s->inline_param_names[nparams], locals[i].name, namelen);
            s->inline_param_names[nparams][namelen] = 0;
            nparams++;
        }
    }
    if (nparams != s->proto_nargs || s->proto_variadic)
        return;

    sv_pos = posi;
    sv_tok_start = tok_start_pos;
    sv_line = line_no;
    sv_tok_line = tok_line;
    sv_tok = tok;

    /* This is a throwaway speculative parse of the function's own body,
     * run before any of its locals are declared for this pass - a
     * reference to one of them would otherwise resolve as "not found" and
     * default to int (see ast_expr_type_for_sizeof's AST_IDENT case),
     * which can trip a real type diagnostic (e.g. a bogus "incompatible
     * integer to pointer assignment") for a perfectly valid program.
     * asm_suppress_depth marks the parse as inert so dcc_error_at drops
     * any such false positive. */
    asm_suppress_depth++;
    body = ast_build_stmt(&g_ast_inline_arena);
    asm_suppress_depth--;

    posi = sv_pos;
    tok_start_pos = sv_tok_start;
    line_no = sv_line;
    tok_line = sv_tok_line;
    tok = sv_tok;

    for (i = 0; i < MAX_PROTO_PARAMS; ++i)
        s->inline_param_use_count[i] = 0;

    if ((s->type & 15) == TYPE_VOID) {
        if (!inline_void_stmt_body_is_simple(s, body))
            return;
        if (body->list_len == 1 && body->list[0]->kind == AST_EXPR_STMT)
            s->inline_stmt_expr = body->list[0]->a;
        else
            s->inline_stmt_body = body;
        return;
    }

    ret_expr = inline_return_expr_from_seq(body, 0);
    if (ret_expr == NULL)
        return;
    if (!inline_expr_is_simple(s, ret_expr))
        return;

    s->inline_return_expr = ret_expr;
}

static int static_inline_body_can_be_buffered(struct Sym *s)
{
    return s != NULL && s->is_static && s->is_inline &&
           (s->inline_return_expr != NULL || s->inline_stmt_expr != NULL ||
            s->inline_stmt_body != NULL);
}

/* Independent of is_inline/is_static: captures a zero-argument function's
 * return expression (bare return, or an early-return if-chain collapsed to
 * a ternary, exactly like the inline substitution shape) purely so
 * dcc_array_narrow.c can recursively bound a call site like rndrm() when
 * proving an array's values are provably in [0,255]. Deliberately does NOT
 * reuse inline_expr_is_simple's gate - that check is about whether an
 * expression is safe to *duplicate at a call site*, a different question
 * from whether dcc_array_narrow.c's own (separate, narrower) rule set can
 * bound it. */
static void record_narrow_return_expr_if_simple(struct Sym *s)
{
    long sv_pos;
    long sv_tok_start;
    int sv_line;
    int sv_tok_line;
    struct Token sv_tok;
    struct AstNode *body;
    struct AstNode *ret_expr;

    if (s == NULL || s->proto_nargs != 0 || s->proto_variadic || tok.kind != '{')
        return;
    if ((s->type & 15) == TYPE_VOID || type_size(s->type) != 2 ||
        type_is_bool(s->type) || type_is_struct_object(s->type))
        return;

    sv_pos = posi;
    sv_tok_start = tok_start_pos;
    sv_line = line_no;
    sv_tok_line = tok_line;
    sv_tok = tok;

    /* See the identical comment in record_inline_function_if_simple: this
     * speculatively parses the whole body before any of its own locals are
     * declared for this pass, so a reference to one can misresolve and
     * trip a false-positive diagnostic; asm_suppress_depth marks the parse
     * as inert so dcc_error_at drops it. */
    asm_suppress_depth++;
    body = ast_build_stmt(&g_ast_inline_arena);
    asm_suppress_depth--;

    posi = sv_pos;
    tok_start_pos = sv_tok_start;
    line_no = sv_line;
    tok_line = sv_tok_line;
    tok = sv_tok;

    ret_expr = inline_return_expr_from_seq(body, 0);
    if (ret_expr == NULL)
        return;

    s->narrow_return_expr = ret_expr;
}

/* Any other static function's body: buffer it too, so it can be dropped at
 * end-of-file if nothing in this translation unit ever calls it or uses its
 * address (see emit_needed_deferred_bodies / the deferred_body_needed
 * marking sites in dcc_ast_gen_expr.c and the global-initializer symbol
 * resolution in this file). `main` is excluded even though it is never
 * `static` in valid, idiomatic C: the CRT startup shim below calls it via a
 * raw fprintf'd `call` that bypasses the AST-based marking entirely, so a
 * static `main` would otherwise look unreferenced and get silently
 * dropped. */
static int plain_static_body_can_be_buffered(struct Sym *s, const char *name)
{
    return s != NULL && s->is_static && strcmp(name, "main") != 0;
}

static void inline_temp_name(char *dst, int dstsz, int index)
{
    sprintf(dst, "#itmp%d", index);
    (void)dstsz;
}

static int inline_function_has_multiuse_param(struct Sym *s)
{
    int i;

    if (s == NULL || !s->is_static || !s->is_inline ||
        (s->inline_return_expr == NULL && s->inline_stmt_expr == NULL &&
         s->inline_stmt_body == NULL))
        return 0;
    for (i = 0; i < s->proto_nargs && i < MAX_PROTO_PARAMS; ++i)
        if (s->inline_param_use_count[i] > 1)
            return 1;
    return 0;
}

static int function_body_mentions_multiuse_inline_call(void)
{
    long sv_pos;
    long sv_tok_start;
    int sv_line;
    int sv_tok_line;
    struct Token sv_tok;
    int depth;
    int result;

    if (tok.kind != '{')
        return 0;

    sv_pos = posi;
    sv_tok_start = tok_start_pos;
    sv_line = line_no;
    sv_tok_line = tok_line;
    sv_tok = tok;

    depth = 1;
    result = 0;
    next_token();
    while (tok.kind != TOK_EOF && depth > 0) {
        if (tok.kind == TOK_ID) {
            char name[64];
            struct Sym *s;

            strncpy(name, tok.text, sizeof(name) - 1);
            name[sizeof(name) - 1] = 0;
            next_token();
            if (tok.kind == '(') {
                s = find_global(name);
                if (inline_function_has_multiuse_param(s)) {
                    result = 1;
                    break;
                }
            }
            continue;
        }
        if (tok.kind == '{')
            depth++;
        else if (tok.kind == '}')
            depth--;
        next_token();
    }

    posi = sv_pos;
    tok_start_pos = sv_tok_start;
    line_no = sv_line;
    tok_line = sv_tok_line;
    tok = sv_tok;
    return result;
}

static void reserve_inline_temp_locals(void)
{
    int i;

    for (i = 0; i < MAX_PROTO_PARAMS; ++i) {
        char name[64];
        inline_temp_name(name, sizeof(name), i);
        add_local_alloc(name, TYPE_INT, 2);
    }
}

void emit_needed_deferred_bodies(void)
{
    int i;

    for (i = 0; i < nglobals; ++i) {
        struct Sym *s;
        int c;

        s = &globals[i];
        if (s->deferred_body_file == NULL)
            continue;
        if (s->deferred_body_needed) {
            rewind(s->deferred_body_file);
            while ((c = fgetc(s->deferred_body_file)) != EOF)
                fputc(c, outf);
        }
        fclose(s->deferred_body_file);
        s->deferred_body_file = NULL;
    }
}

int current_void_is_empty_param_list(void)
{
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    struct Token save_tok;
    int r;

    if (tok.kind != TOK_VOID)
        return 0;

    save_pos = posi;
    save_tok_start = tok_start_pos;
    save_line = line_no;
    save_tok_line = tok_line;
    save_tok = tok;

    next_token();
    r = (tok.kind == ')');

    posi = save_pos;
    tok_start_pos = save_tok_start;
    line_no = save_line;
    tok_line = save_tok_line;
    tok = save_tok;

    return r;
}

void skip_prototype_array_suffixes(int *ptype)
{
    int dims[MAX_ARRAY_DIMS];
    int ndims = 0;
    int i, n, inner, elem_bytes;
    int orig_type = *ptype;

    if (tok.kind != '[') return;

    /* Reset: we're taking over array suffix parsing from scratch. */
    g_ptr_array_dim_count = 0;
    g_ptr_array_elem_size = 0;
    memset(g_ptr_array_dims, 0, sizeof(g_ptr_array_dims));

    while (accept('[')) {
        skip_parameter_array_qualifiers();

        if (tok.kind == ']') {
            n = 0;
            next_token();
        } else if (tok.kind == '*') {
            /* C99 `[*]` unspecified-size VLA marker in a prototype; it decays
             * to a pointer exactly like `[]`. */
            next_token();
            expect(']');
            n = 0;
        } else {
            n = parse_const_int_expr();
            expect(']');
        }
        if (n < 0) n = 0;
        if (ndims < MAX_ARRAY_DIMS) dims[ndims] = n;
        ndims++;
    }

    if (ndims == 0) return;

    /* Any array parameter decays to a single pointer to its element group.
     * int a[]      -> int *a  (dim_count = 0, no inner dims)
     * int a[N][M]  -> int (*a)[M], stride = M*sizeof(int)
     *                 (dim_count = 1, dims = {M}, elem_size = M*sizeof(int))
     */
    ptype[0] = type_add_ptr(orig_type);

    if (ndims <= 1) return;

    elem_bytes = type_size(orig_type);
    if (elem_bytes <= 0) elem_bytes = 2;

    inner = 1;
    for (i = 1; i < ndims; ++i) {
        if (dims[i] <= 0) { inner = 0; break; }
        inner *= dims[i];
    }

    g_ptr_array_dim_count = ndims - 1;
    g_ptr_array_elem_size = (inner > 0) ? inner * elem_bytes : elem_bytes;
    for (i = 0; i < ndims - 1 && i < MAX_ARRAY_DIMS; ++i)
        g_ptr_array_dims[i] = dims[i + 1];
}

void skip_prototype_function_suffix(void)
{
    int depth;
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    struct Token save_tok;

    if (!accept('('))
        return;

    depth = 1;
    while (tok.kind != TOK_EOF && depth > 0) {
        if (tok.kind == '(')
            depth++;
        else if (tok.kind == ')')
            depth--;
        next_token();
    }

    while (tok.kind == '(')
        skip_prototype_function_suffix();

    if (tok.kind == ')') {
        save_pos = posi;
        save_tok_start = tok_start_pos;
        save_line = line_no;
        save_tok_line = tok_line;
        save_tok = tok;
        next_token();
        if (tok.kind != ',') {
            posi = save_pos;
            tok_start_pos = save_tok_start;
            line_no = save_line;
            tok_line = save_tok_line;
            tok = save_tok;
        }
    }
}


void clear_parsed_prototype(void)
{
    int i;
    g_proto_has = 0;
    g_proto_nargs = 0;
    g_proto_variadic = 0;
    for (i = 0; i < MAX_PROTO_PARAMS; ++i)
        g_proto_types[i] = 0;
}

void copy_parsed_prototype_to_sym(struct Sym *s)
{
    int i;
    if (!s) return;
    s->has_proto = g_proto_has;
    s->proto_nargs = g_proto_nargs;
    s->proto_variadic = g_proto_variadic;
    for (i = 0; i < MAX_PROTO_PARAMS; ++i)
        s->proto_types[i] = g_proto_types[i];
}

void remember_proto_param_type(int type)
{
    g_proto_has = 1;
    if (g_proto_nargs < MAX_PROTO_PARAMS)
        g_proto_types[g_proto_nargs] = type;
    g_proto_nargs++;
}

int old_style_param_list_starts(void)
{
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    struct Token save_tok;
    int r;

    if (tok.kind != TOK_ID || find_typedef(tok.text) >= 0)
        return 0;

    save_pos = posi;
    save_tok_start = tok_start_pos;
    save_line = line_no;
    save_tok_line = tok_line;
    save_tok = tok;

    r = 1;
    for (;;) {
        if (tok.kind != TOK_ID || find_typedef(tok.text) >= 0) {
            r = 0;
            break;
        }
        next_token();
        if (tok.kind == ')')
            break;
        if (tok.kind != ',') {
            r = 0;
            break;
        }
        next_token();
    }

    posi = save_pos;
    tok_start_pos = save_tok_start;
    line_no = save_line;
    tok_line = save_tok_line;
    tok = save_tok;
    return r;
}

void recompute_param_offsets(void)
{
    int i;
    int off;
    int sz;

    off = ((parse_function_return_type & TYPE_STRUCT) &&
           type_ptr_depth(parse_function_return_type) == 0) ? 6 : 4;

    for (i = 0; i < nlocals; ++i) {
        if (locals[i].storage != SC_PARAM)
            continue;
        sz = type_size(locals[i].type);
        if (sz < 2) sz = 2;
        locals[i].offset = off;
        locals[i].size = sz;
        off += sz;
    }
    param_offset = off;
}

void parse_old_style_param_id_list(void)
{
    char name[64];

    for (;;) {
        if (tok.kind != TOK_ID) {
            error_here("parameter name expected");
            break;
        }
        strncpy(name, tok.text, sizeof(name) - 1);
        name[sizeof(name) - 1] = 0;
        next_token();
        add_param_alloc(name, TYPE_INT);
        if (!accept(','))
            break;
    }
}

void parse_old_style_param_declarations(void)
{
    int base;
    int type;
    char name[64];
    struct Sym *s;

    while (tok.kind != TOK_EOF && tok.kind != '{' && starts_type()) {
        base = parse_base_type();

        for (;;) {
            type = base;
            while (accept('*')) {
                skip_type_qualifiers();
                type = type_add_ptr(type);
            }

            if (tok.kind != TOK_ID) {
                error_here("parameter declaration name expected");
                while (tok.kind != ';' && tok.kind != TOK_EOF && tok.kind != '{')
                    next_token();
                break;
            }

            strncpy(name, tok.text, sizeof(name) - 1);
            name[sizeof(name) - 1] = 0;
            next_token();

            skip_prototype_array_suffixes(&type);
            if (tok.kind == '(') {
                skip_prototype_function_suffix();
                type = type_add_ptr(type);
            }

            s = find_local(name);
            if (!s || s->storage != SC_PARAM) {
                error_here("old-style parameter declaration does not match parameter list");
            } else {
                int pi;
                s->type = type;
                if (g_ptr_array_dim_count > 0) {
                    s->elem_size = g_ptr_array_elem_size;
                    s->dim_count = g_ptr_array_dim_count;
                    for (pi = 0; pi < MAX_ARRAY_DIMS; ++pi)
                        s->dims[pi] = (pi < g_ptr_array_dim_count) ? g_ptr_array_dims[pi] : 0;
                }
                g_ptr_array_dim_count = 0;
                g_ptr_array_elem_size = 0;
            }

            if (!accept(','))
                break;
        }

        expect(';');
    }

    recompute_param_offsets();
}

void parse_param_list(void)
{
    int type;
    char name[64];
    int unnamed_id;

    nlocals = 0;
    local_size = 0;
    param_offset = ((parse_function_return_type & TYPE_STRUCT) && type_ptr_depth(parse_function_return_type) == 0) ? 6 : 4;
    clear_parsed_prototype();

    if (current_void_is_empty_param_list()) {
        g_proto_has = 1;
        next_token();
        return;
    }

    /* Empty parentheses in C89 mean old-style/no prototype. */
    if (tok.kind == ')') return;

    if (old_style_param_list_starts()) {
        parse_old_style_param_id_list();
        return;
    }

    for (;;) {
        if (tok.kind == TOK_ELLIPSIS) {
            g_proto_has = 1;
            g_proto_variadic = 1;
            next_token();
            break;
        }

        type = parse_type();
        if (g_typedef_array_len > 0) {
            type = type_add_ptr(type);
            g_typedef_array_len = 0;
        }
        unnamed_id = 0;

        while (accept('*')) {
            skip_type_qualifiers();
            type = type_add_ptr(type);
        }
        skip_type_qualifiers();

        if (parse_funcptr_declarator(&type, name, sizeof(name))) {
            /* function pointer parameter */
        } else if (parse_abstract_funcptr_declarator(&type)) {
            sprintf(name, "__p%d", param_offset);
            unnamed_id = 1;
        } else if (tok.kind == TOK_ID) {
            strncpy(name, tok.text, sizeof(name) - 1);
            name[sizeof(name) - 1] = 0;
            next_token();
        } else {
            /* Prototype declarations may omit parameter names:
             *     int f(int, char *);
             * Give such parameters private dummy names so function
             * definitions using named parameters continue to work exactly
             * as before, while header prototypes are accepted. */
            sprintf(name, "__p%d", param_offset);
            unnamed_id = 1;
        }

        /* Parameter arrays decay to pointers.  This makes both named and
         * unnamed forms work:
         *     char *argv[]
         *     char *[]
         */
        skip_prototype_array_suffixes(&type);

        /* Accept function-typed parameters in prototypes and treat them as
         * pointer-sized for this compiler's simple type model.  This keeps
         * declarations like int f(int cb(void)); from poisoning the parse. */
        if (tok.kind == '(') {
            skip_prototype_function_suffix();
            type = type_add_ptr(type);
        }

        remember_proto_param_type(type);
        {
            struct Sym *ps;
            int pi;
            add_param_alloc(name, type);
            ps = find_local(name);
            if (ps && g_ptr_array_dim_count > 0) {
                ps->elem_size = g_ptr_array_elem_size;
                ps->dim_count = g_ptr_array_dim_count;
                for (pi = 0; pi < MAX_ARRAY_DIMS; ++pi)
                    ps->dims[pi] = (pi < g_ptr_array_dim_count) ? g_ptr_array_dims[pi] : 0;
            }
            g_ptr_array_dim_count = 0;
            g_ptr_array_elem_size = 0;
        }
        (void)unnamed_id;

        if (!accept(',')) break;
    }
}


int current_function_param_count(void)
{
    int i;
    int n;

    n = 0;
    for (i = 0; i < nlocals; ++i)
        if (locals[i].storage == SC_PARAM)
            n++;
    return n;
}

int current_function_safe_to_omit_ix(int return_type, int local_bytes)
{
    (void)return_type;
    (void)local_bytes;
    (void)current_function_param_count();

    /*
     * Disabled for now.
     *
     * The first no-IX implementation accessed parameters through fixed SP
     * offsets.  That is only correct if the generated function never changes
     * SP after entry.  Even very small leaf functions such as:
     *
     *     return p[0] + p[1] + p[2] + p[3];
     *
     * use push/pop temporaries during expression evaluation, so later
     * parameter reloads from sp+N read those temporaries instead of the
     * original argument.  This corrupted tests with struct string
     * initializers through helper functions like sum4().
     *
     * Keep the leaf BC/DE loop optimizations, but do not omit IX until the
     * compiler has either stable SP-depth tracking for parameter references
     * or a dedicated no-stack codegen path for recognized functions.
     */
    return 0;
}

void emit_function_prologue(const char *name, int local_bytes, int omit_ix_frame)
{
    struct Sym *s;
    const char *aname;

    flush_pending_asm();

    s = find_global(name);
    aname = asm_name_for(name);

    if (!s || !s->is_static) {
        asm_name_check_public_collision(name);
        fprintf(outf, "\n\tpublic %s\n", aname);
    } else {
        /* File-scope static functions are mangled to avoid M80/L80 short-name
         * collisions.  Emit the original C spelling beside the generated label
         * so .mac listings remain readable during debugging. */
        fprintf(outf, "\n; static function %s\n", name);
    }

    fprintf(outf, "%s:\n", aname);
    current_omit_ix_frame = omit_ix_frame;
    if (!omit_ix_frame) {
        emit("\tpush ix\n");
        emit("\tld ix,0\n");
        emit("\tadd ix,sp\n");
    }

    if (local_bytes > 0) {
        fprintf(outf, "\tld hl,-%d\n", local_bytes);
        emit("\tadd hl,sp\n");
        emit("\tld sp,hl\n");
    }

    /* -fstack-check: after the frame (saved IX + locals) is allocated, verify
     * the stack has not grown past its reserve into the heap region.  Emitted
     * last so dccpeep's shared-frame-stub pass can still fold the prologue
     * (the call follows the recognised push-ix/locals sequence). */
    if (opt_stack_check)
        emit_runtime_call("__stchk");
}

void emit_function_epilogue(int implicit_zero_return)
{
    if (implicit_zero_return)
        emit("\tld hl,0\n");
    emit_label(current_return_label);
    /* Always emit ld sp,ix so returns from nested control flow restore the
     * caller stack reliably. pass_elim_ix_frame and pass_shared_frame_stubs clean up the extra
     * instruction for functions that never actually need the stack restore. */
    if (!current_omit_ix_frame) {
        emit("\tld sp,ix\n");
        emit("\tpop ix\n");
    }
    emit("\tret\n");
    current_omit_ix_frame = 0;
    flush_pending_asm();
}

void skip_initializer_or_decl_tail(void)
{
    int depth;

    depth = 0;

    while (tok.kind != TOK_EOF) {
        if (depth == 0 && (tok.kind == ',' || tok.kind == ';')) return;

        if (tok.kind == '(' || tok.kind == '[' || tok.kind == '{') depth++;
        else if (tok.kind == ')' || tok.kind == ']' || tok.kind == '}') {
            if (depth > 0) depth--;
        }

        next_token();
    }
}

static int scan_compound_literal_if_present(void)
{
    long save_pos;
    long save_tok_start;
    int save_line;
    int save_tok_line;
    int save_long_suffix;
    int save_unsigned_suffix;
    struct Token save_tok;
    int type;
    int size;
    int depth;

    if (tok.kind != '(' || !paren_starts_cast())
        return 0;

    save_pos = posi;
    save_tok_start = tok_start_pos;
    save_line = line_no;
    save_tok_line = tok_line;
    save_long_suffix = g_tok_long_suffix;
    save_unsigned_suffix = g_tok_unsigned_suffix;
    save_tok = tok;

    depth = 1;
    next_token();
    while (tok.kind != TOK_EOF && depth > 0) {
        if (tok.kind == '(')
            depth++;
        else if (tok.kind == ')')
            depth--;
        next_token();
    }

    if (tok.kind != '{') {
        posi = save_pos;
        tok_start_pos = save_tok_start;
        line_no = save_line;
        tok_line = save_tok_line;
        g_tok_long_suffix = save_long_suffix;
        g_tok_unsigned_suffix = save_unsigned_suffix;
        tok = save_tok;
        return 0;
    }

    posi = save_pos;
    tok_start_pos = save_tok_start;
    line_no = save_line;
    tok_line = save_tok_line;
    g_tok_long_suffix = save_long_suffix;
    g_tok_unsigned_suffix = save_unsigned_suffix;
    tok = save_tok;

    next_token();
    parse_type_name_decl(&type, &size);
    expect(')');

    if (tok.kind != '{') {
        posi = save_pos;
        tok_start_pos = save_tok_start;
        line_no = save_line;
        tok_line = save_tok_line;
        tok = save_tok;
        return 0;
    }

    add_compound_literal_local(type);

    /* Walk the braced initializer, recursing into nested compound literals so
     * each reserves its own frame slot in source order. The codegen pass
     * re-parses this same initializer at emit time and allocates one frame
     * slot per nested compound literal (add_compound_literal_local, reached
     * through ast_emit_init_expr for each non-constant field). Emit consumes
     * the initializer tokens in source order, so a source-order recursive walk
     * here reserves exactly the same slots at the same offsets. Skipping the
     * body (the old behavior) under-reserved the frame: the prologue is sized
     * from this scan, so the nested literals then landed below SP where an
     * intervening push/call clobbers them. */
    depth = 0;
    do {
        if (tok.kind == TOK_EOF)
            break;
        if (depth >= 1 && tok.kind == '(' && scan_compound_literal_if_present())
            continue;
        if (tok.kind == '{')
            depth++;
        else if (tok.kind == '}')
            depth--;
        next_token();
    } while (depth > 0);

    return 1;
}

static void scan_initializer_or_decl_tail(void)
{
    int depth;

    depth = 0;

    while (tok.kind != TOK_EOF) {
        if (depth == 0 && (tok.kind == ',' || tok.kind == ';')) return;

        if (tok.kind == '(' && scan_compound_literal_if_present())
            continue;

        if (tok.kind == '(' || tok.kind == '[' || tok.kind == '{') depth++;
        else if (tok.kind == ')' || tok.kind == ']' || tok.kind == '}') {
            if (depth > 0) depth--;
        }

        next_token();
    }
}



int local_name_address_taken_ahead(const char *name)
{
    long p;
    int depth;
    int c;
    int n;

    /* Conservative forward scan of the rest of the current function body.
     * Local consts optimized as immediates have no stack address.  If the
     * source later forms &name, keep normal storage instead.  This deliberately
     * ignores strings/comments and stops at the function's closing brace.
     */
    p = posi;
    depth = 1;
    n = (int)strlen(name);

    while (p < src_len && depth > 0) {
        c = (unsigned char)src[p];

        if (c == '"') {
            p++;
            while (p < src_len) {
                c = (unsigned char)src[p++];
                if (c == '\\' && p < src_len) { p++; continue; }
                if (c == '"') break;
            }
            continue;
        }

        if (c == '\'') {
            p++;
            while (p < src_len) {
                c = (unsigned char)src[p++];
                if (c == '\\' && p < src_len) { p++; continue; }
                if (c == '\'') break;
            }
            continue;
        }

        if (c == '/' && p + 1 < src_len && src[p + 1] == '*') {
            p += 2;
            while (p + 1 < src_len && !(src[p] == '*' && src[p + 1] == '/'))
                p++;
            if (p + 1 < src_len)
                p += 2;
            continue;
        }

        if (c == '/' && p + 1 < src_len && src[p + 1] == '/') {
            p += 2;
            while (p < src_len && src[p] != '\n')
                p++;
            continue;
        }

        if (c == '{') {
            depth++;
            p++;
            continue;
        }
        if (c == '}') {
            depth--;
            p++;
            continue;
        }

        if (c == '&') {
            long q;
            q = p + 1;
            while (q < src_len && (src[q] == ' ' || src[q] == '\t' || src[q] == '\r' || src[q] == '\n'))
                q++;
            if (q + n <= src_len && strncmp(src + q, name, (size_t)n) == 0) {
                int before_ok;
                int after_ok;
                before_ok = 1;
                after_ok = (q + n >= src_len) || !is_ident_char((unsigned char)src[q + n]);
                if (before_ok && after_ok)
                    return 1;
            }
        }

        p++;
    }

    return 0;
}

/* Is `name` ever referenced again before the end of the block that
 * currently encloses the parser's position? Scans forward from here,
 * tracking brace depth so a name used only in a later, unrelated sibling
 * block (after this one closes) correctly does not count - the same name
 * there is out of scope for this declaration regardless of whether it
 * happens to be a shadowing declaration. A crude "not immediately preceded
 * by '.' or '->'" guard avoids miscounting a struct/union member access
 * that merely shares this local's name as a use of the local itself.
 *
 * This is a lexical scan (not symbol-table-based, matching
 * scan_global_write_info's approach for the analogous whole-file
 * question), so it necessarily overcounts in some cases - a same-named
 * member access with the guard defeated by an intervening comment or
 * macro, for instance. Overcounting only means a genuinely-unused local
 * gets kept (a missed optimization); it can never cause a used local to be
 * dropped, which is the only direction that would be unsafe. */
int local_name_used_ahead(const char *name)
{
    long sv_pos;
    long sv_tok_start;
    int sv_line;
    int sv_tok_line;
    struct Token sv_tok;
    int depth;
    int prev_was_member_access;
    int found;

    sv_pos = posi;
    sv_tok_start = tok_start_pos;
    sv_line = line_no;
    sv_tok_line = tok_line;
    sv_tok = tok;

    depth = 0;
    found = 0;
    prev_was_member_access = 0;
    while (tok.kind != TOK_EOF) {
        if (tok.kind == '{') {
            depth++;
        } else if (tok.kind == '}') {
            if (depth == 0)
                break;
            depth--;
        } else if (tok.kind == TOK_ID && !strcmp(tok.text, name)) {
            if (!prev_was_member_access) {
                found = 1;
                break;
            }
        }
        prev_was_member_access = (tok.kind == '.' || tok.kind == TOK_ARROW);
        next_token();
    }

    posi = sv_pos;
    tok_start_pos = sv_tok_start;
    line_no = sv_line;
    tok_line = sv_tok_line;
    tok = sv_tok;
    return found;
}

/* Purely lexical skip of one declaration statement with NO initializer,
 * tracking paren/bracket/brace depth to find the terminating top-level
 * ';' - no symbol-table side effects, no attempt to understand the
 * declaration. Used only so the speculative narrow-safety walk (see
 * narrow_build_speculative_scope) can step past a LATER declaration that
 * ast_build_stmt cannot itself handle.
 *
 * Returns 1 (and leaves the token stream just past the ';') only for a
 * plain, uninitialized declaration - its only content besides the name is
 * compile-time-constant array dimensions, which by C89 rules cannot
 * reference a local variable, so it truly cannot alias or escape any name
 * this analysis cares about. Returns 0 if a top-level '=' is seen anywhere
 * in the statement: an initializer CAN reference (and so alias/escape) one
 * of the names being proven narrow-safe - e.g. `int *ip = ai;` aliases
 * `ai` - and that reference would never reach narrow_name_escapes if this
 * function silently skipped past it. On a 0 return the token position is
 * unspecified; the caller aborts the whole speculative parse either way,
 * so nothing needs to resync it. */
static int narrow_skip_declaration_statement(void)
{
    int depth = 0;
    while (tok.kind != TOK_EOF) {
        if (depth == 0 && tok.kind == ';') {
            next_token();
            return 1;
        }
        if (depth == 0 && tok.kind == '=')
            return 0;
        if (tok.kind == '(' || tok.kind == '[' || tok.kind == '{')
            depth++;
        else if (tok.kind == ')' || tok.kind == ']' || tok.kind == '}') {
            if (depth > 0) depth--;
        }
        next_token();
    }
    return 0;
}

/* Shared by try_narrow_local_int_array and try_narrow_register_scalar:
 * speculatively parses the rest of the enclosing block, from the current
 * position, into an AST. A further local declaration in between (common -
 * neither the array nor the scalar being proven need be the last local in
 * the block) is lexically skipped rather than requiring ast_build_stmt to
 * handle it (declarations are parsed by this file, not the AST builder).
 * A typedef, or any other construct ast_build_stmt itself declines, still
 * aborts the whole speculative parse (returns NULL) rather than guessing. */
static struct AstNode *narrow_build_speculative_scope(struct AstArena *ar)
{
    struct AstNode *seq;

    /* The caller's current position may be mid-declaration - e.g. proving
     * the FIRST name in `register int i, j;` or `int a[200], b[200];`
     * narrow-safe leaves the token stream sitting at the comma before the
     * next declarator, not at a fresh statement boundary. Lexically skip
     * whatever remains of the CURRENT declaration statement first (exactly
     * like the loop below already does for a LATER, separate declaration -
     * narrow_skip_declaration_statement bails safely on a top-level '=' the
     * same way there too) so the loop always starts at a genuine statement
     * boundary. A no-op past the ';' when the caller's declarator was
     * already the last, or only, one in its statement. Without this, every
     * declarator except the last in a comma-separated declaration silently
     * failed to narrow at all (returned NULL here, so the caller always
     * saw "not safe to narrow" regardless of the actual proof). */
    if (!narrow_skip_declaration_statement())
        return NULL;

    seq = ast_new(ar, AST_COMPOUND);
    for (;;) {
        struct AstNode *stmt;
        if (tok.kind == '}' || tok.kind == TOK_EOF)
            return seq;
        if (starts_type() && tok.kind != TOK_TYPEDEF) {
            if (!narrow_skip_declaration_statement())
                return NULL;
            continue;
        }
        stmt = ast_build_stmt(ar);
        if (stmt == NULL)
            return NULL;
        ast_list_push(ar, seq, stmt);
    }
}

/* Speculatively parses the rest of the enclosing block (from the current
 * position, which must be right after an eligible array declarator with no
 * initializer) into an AST, then asks dcc_array_narrow.c whether every
 * value ever stored into `name` is provably in [0,255]. Always rewinds the
 * lexer position and every per-function counter that must stay in sync
 * between this (scan) pass and the later, independent codegen pass
 * (gen_local_decl_after_type must reach the identical conclusion using the
 * identical scratch parse, since both determine the same array's frame
 * size/offset independently - see the frame-sizing comments in
 * parse_function_or_global).
 *
 * Bails (returns 0, the safe default) if the speculative parse cannot
 * reach the block's closing brace - e.g. some construct ast_build_stmt
 * cannot handle at all - rather than guess. */
int try_narrow_local_int_array(const char *name, int type, int arrlen, int total_elems)
{
    long sv_pos, sv_tok_start;
    int sv_line, sv_tok_line;
    struct Token sv_tok;
    int sv_nulabels, sv_for_seq, sv_forren_n, sv_for_decl_seq, sv_for_decl_rename_index;
    int sv_for_decl_recording, sv_scope_depth, sv_compound_literal_seq, sv_licm_seq;
    static struct AstArena narrow_scratch_arena;
    static int narrow_scratch_inited;
    struct AstNode *seq;
    int result;

    if ((type & 15) != TYPE_INT || type_ptr_depth(type) != 0 || type_is_struct_object(type) ||
        (arrlen <= 0 && total_elems <= 0) || tok.kind == '=' || g_last_array_dim_count > 1)
        return 0;

    if (!narrow_scratch_inited) {
        ast_arena_init(&narrow_scratch_arena);
        narrow_scratch_inited = 1;
    }
    ast_arena_reset(&narrow_scratch_arena);

    sv_pos = posi; sv_tok_start = tok_start_pos;
    sv_line = line_no; sv_tok_line = tok_line;
    sv_tok = tok;
    sv_nulabels = nulabels;
    sv_for_seq = g_for_seq; sv_forren_n = g_forren_n;
    sv_for_decl_seq = g_for_decl_seq; sv_for_decl_rename_index = g_for_decl_rename_index;
    sv_for_decl_recording = g_for_decl_recording; sv_scope_depth = g_scope_depth;
    sv_compound_literal_seq = g_compound_literal_seq; sv_licm_seq = g_licm_seq;

    /* Same rationale as record_narrow_return_expr_if_simple: this walks
     * forward through code whose later declarations (if any follow) have
     * not been (re-)entered into the symbol table for this pass, so a
     * reference to one can misresolve and trip a false-positive diagnostic;
     * asm_suppress_depth marks the parse as inert so dcc_error_at drops it. */
    asm_suppress_depth++;
    seq = narrow_build_speculative_scope(&narrow_scratch_arena);
    asm_suppress_depth--;
    result = (seq != NULL) ? narrow_array_is_byte_safe(seq, name) : 0;

    posi = sv_pos; tok_start_pos = sv_tok_start;
    line_no = sv_line; tok_line = sv_tok_line;
    tok = sv_tok;
    nulabels = sv_nulabels;
    g_for_seq = sv_for_seq; g_forren_n = sv_forren_n;
    g_for_decl_seq = sv_for_decl_seq; g_for_decl_rename_index = sv_for_decl_rename_index;
    g_for_decl_recording = sv_for_decl_recording; g_scope_depth = sv_scope_depth;
    g_compound_literal_seq = sv_compound_literal_seq; g_licm_seq = sv_licm_seq;

    return result;
}

/* Scalar counterpart of try_narrow_local_int_array: proves a plain
 * register-qualified int local's own value (not an array's elements) is
 * always in [0,255], so its storage can narrow to unsigned char - e.g.
 * e.c's `register int n`, which this same engine already has to bound
 * anyway as a dependency of proving `a[]` narrow-safe (n is a %-divisor).
 * is_register is captured by the caller (from decl_is_register) rather
 * than read here, since nothing this function calls is expected to touch
 * that global, but relying on a value already in hand is more robust than
 * re-reading a global after a speculative parse. */
int try_narrow_register_scalar(const char *name, int type, int is_register,
                               int arrlen, int total_elems)
{
    long sv_pos, sv_tok_start;
    int sv_line, sv_tok_line;
    struct Token sv_tok;
    int sv_nulabels, sv_for_seq, sv_forren_n, sv_for_decl_seq, sv_for_decl_rename_index;
    int sv_for_decl_recording, sv_scope_depth, sv_compound_literal_seq, sv_licm_seq;
    static struct AstArena narrow_scalar_scratch_arena;
    static int narrow_scalar_scratch_inited;
    struct AstNode *seq;
    int result;

    if (!is_register || (type & 15) != TYPE_INT || type_ptr_depth(type) != 0 ||
        type_is_struct_object(type) || arrlen > 0 || total_elems > 0 || tok.kind == '=')
        return 0;

    if (!narrow_scalar_scratch_inited) {
        ast_arena_init(&narrow_scalar_scratch_arena);
        narrow_scalar_scratch_inited = 1;
    }
    ast_arena_reset(&narrow_scalar_scratch_arena);

    sv_pos = posi; sv_tok_start = tok_start_pos;
    sv_line = line_no; sv_tok_line = tok_line;
    sv_tok = tok;
    sv_nulabels = nulabels;
    sv_for_seq = g_for_seq; sv_forren_n = g_forren_n;
    sv_for_decl_seq = g_for_decl_seq; sv_for_decl_rename_index = g_for_decl_rename_index;
    sv_for_decl_recording = g_for_decl_recording; sv_scope_depth = g_scope_depth;
    sv_compound_literal_seq = g_compound_literal_seq; sv_licm_seq = g_licm_seq;

    asm_suppress_depth++;
    seq = narrow_build_speculative_scope(&narrow_scalar_scratch_arena);
    asm_suppress_depth--;
    result = (seq != NULL) ? narrow_scalar_is_byte_safe(seq, name) : 0;

    posi = sv_pos; tok_start_pos = sv_tok_start;
    line_no = sv_line; tok_line = sv_tok_line;
    tok = sv_tok;
    nulabels = sv_nulabels;
    g_for_seq = sv_for_seq; g_forren_n = sv_forren_n;
    g_for_decl_seq = sv_for_decl_seq; g_for_decl_rename_index = sv_for_decl_rename_index;
    g_for_decl_recording = sv_for_decl_recording; g_scope_depth = sv_scope_depth;
    g_compound_literal_seq = sv_compound_literal_seq; g_licm_seq = sv_licm_seq;

    return result;
}

void scan_local_decl_after_type(int base)
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
            if (tok.kind != TOK_ID) return;

            strncpy(name, tok.text, sizeof(name) - 1);
            name[sizeof(name) - 1] = 0;
            next_token();
        }
        strncpy(source_name, name, sizeof(source_name) - 1);
        source_name[sizeof(source_name) - 1] = 0;

        if (tok.kind == '(') {
            skip_prototype_function_suffix();
            if (!accept(','))
                break;
            continue;
        }

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

                /* flattened atoms -> array elements: divide by the element
                 * type's scalar-atom count (1 for non-struct element types). */
                satoms = type_scalar_atom_count(type);
                if (satoms <= 0) satoms = 1;

                if (atoms > 0) {
                    int elems;
                    if (satoms > 1) {
                        /* Struct elements are always braced; count top-level
                         * groups so PARTIAL inits ({ {1},{2},{3} }) size
                         * correctly instead of truncating via atoms/satoms. */
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

            current_field_array_elem_size = first_stride_bytes;
        }
        /* inherit array length from array typedef */
        if (arrlen == 0 && g_typedef_array_len > 0) {
            arrlen = g_typedef_array_len;
            total_elems = g_typedef_array_len;
        }

        if (try_narrow_local_int_array(name, type, arrlen, total_elems)) {
            type = (type & ~15) | TYPE_CHAR | TYPE_UNSIGNED;
            /* first_stride_bytes (see parse_array_declarator_dims) was
             * computed from the pre-narrowing int element size and is still
             * sitting in current_field_array_elem_size; a single-dimension
             * array (guaranteed by the g_last_array_dim_count > 1 eligibility
             * check above) has no real per-row stride distinct from the
             * element size, so clearing it makes the Sym.elem_size ternary
             * below fall through to type_size(type), matching the narrowed
             * type instead of silently keeping the stale, too-wide stride. */
            current_field_array_elem_size = 0;
        } else if (try_narrow_register_scalar(name, type, decl_is_register, arrlen, total_elems)) {
            type = (type & ~15) | TYPE_CHAR | TYPE_UNSIGNED;
        }

        bytes = type_size(type);
        if (total_elems > 0) bytes *= total_elems;
        if (g_vla_pending) bytes = 2;   /* VLA: reserve only a pointer slot */

        /* A name already present in the innermost open block is a redefinition.
         * find_local_decl() only searches the current scope (and ignores
         * for-init renames), so a match here is a genuine same-scope duplicate,
         * which C89 6.1.2.2 makes a constraint violation.  dcc historically
         * swallowed this and silently kept the first declaration's type; report
         * it instead, then reuse the existing symbol for error recovery. */
        s = find_local_decl(name);
        if (s && !scan_mode) {
            char redef_msg[96];
            sprintf(redef_msg, "redefinition of '%s'", source_name);
            error_here(redef_msg);
        }
        if (!s)
            s = try_const_fold_local(name, source_name, type,
                                     arrlen != 0 || g_last_array_dim_count != 0);

        {
        int freshly_allocated = 0;
        if (!s) {
            s = add_local_alloc(name, type, bytes);
            freshly_allocated = 1;
            if (arrlen > 0 || g_last_array_dim_count > 0) {
                s->is_array = 1;
                s->array_len = arrlen;
                s->elem_size = current_field_array_elem_size ? current_field_array_elem_size : type_size(type);
                if (s->elem_size <= 0) s->elem_size = 2;
                copy_last_array_dims_to_sym(s);
                if (g_vla_pending) {
                    struct Sym *size_slot;
                    /* VLA: keep the elem_size set above (element size for
                     * a[n], row stride for a[n][C]); the slot holds a runtime
                     * pointer, mirrored by gen_local_decl_after_type. */
                    s->is_vla = 1;
                    s->array_len = 0;
                    if (s->elem_size <= 0) s->elem_size = 1;
                    size_slot = add_local_alloc("#vlasz", TYPE_INT, 2);
                    s->vla_size_offset = size_slot->offset;
                    /* Reserve this scope's SP-save slot (first VLA only) so the
                     * frame matches the codegen pass, which also emits it. */
                    vla_scope_ensure_save_slot();
                }
            } else if (g_ptr_array_dim_count > 0) {
                int pi;
                s->elem_size = g_ptr_array_elem_size;
                s->dim_count = g_ptr_array_dim_count;
                for (pi = 0; pi < MAX_ARRAY_DIMS; ++pi)
                    s->dims[pi] = (pi < g_ptr_array_dim_count) ? g_ptr_array_dims[pi] : 0;
            }
        }
        g_ptr_array_dim_count = 0;
        g_ptr_array_elem_size = 0;

        if (s && !s->is_const_value && accept('=')) {
            scan_initializer_or_decl_tail();
        } else if (freshly_allocated && !g_vla_pending && !local_name_used_ahead(source_name)) {
            /* No initializer, and never referenced again in this scope:
             * add_local_alloc just appended this Sym as the last local and
             * reserved its frame space, so popping both back off is safe -
             * nothing later in this same declarator loop has allocated
             * anything above it yet. freshly_allocated (rather than just
             * !s->is_const_value) guards against the redefinition-error
             * recovery case, where s is an unrelated pre-existing symbol and
             * bytes/nlocals do not describe it. */
            nlocals--;
            local_size -= bytes;
        }
        }

        if (!accept(',')) break;
    }

    expect(';');
}

/* Function-scope static declarations are backed by normal global storage,
 * but are entered in the local symbol table so ordinary references inside
 * the function resolve correctly.  This is enough for forms such as:
 *
 *     static int knight_dir[8] = { 17, 15, ... };
 *
 * and also supports uninitialized local static arrays.
 */
void scan_static_local_decl_after_type(int base)
{
    int type, bytes, arrlen;
    char name[64];
    char backing_name[64];
    struct Sym *g;
    struct Sym *l;

    for (;;) {
        type = base;

        while (accept('*')) { skip_type_qualifiers(); type = type_add_ptr(type); }

        if (tok.kind != TOK_ID) return;

        strncpy(name, tok.text, sizeof(name) - 1);
        name[sizeof(name) - 1] = 0;
        next_token();

        arrlen = g_funcptr_decl_array_len;
        g_funcptr_decl_array_len = 0;
        {
            int first_stride_bytes;
            first_stride_bytes = 0;
            if (arrlen == 0)
                parse_array_declarator_dims(type, &arrlen, &first_stride_bytes, 1);
            current_field_array_elem_size = first_stride_bytes;
        }
        if (g_vla_pending) {
            /* A static (or file-scope) array cannot have a run-time bound:
             * its storage is a fixed global, not stack-allocated.  Reject
             * rather than silently emit a wrong-sized static array.  Clear the
             * flag so it does not leak into the next declarator. */
            if (asm_suppress_depth == 0)
                error_here("variable length array declaration cannot have static storage duration");
            g_vla_pending = 0;
            arrlen = 0;
        }
        if (arrlen == 0 && g_typedef_array_len > 0)
            arrlen = g_typedef_array_len;

        bytes = type_size(type);
        if (arrlen > 0)
            bytes = object_array_size(type, arrlen);
        else if (arrlen < 0)
            bytes = 0;

        l = find_local_decl(name);
        if (l && l->link_name[0]) {
            strncpy(backing_name, l->link_name, sizeof(backing_name) - 1);
            backing_name[sizeof(backing_name) - 1] = 0;
        } else {
            sprintf(backing_name, "__sl%d_%d", g_static_local_func_index,
                    g_static_local_seq++);
        }

        g = add_global(backing_name, type, SC_GLOBAL);
        g->is_defined = 1;
        g->needs_extrn = 0;
        g->is_static = 1;
        g->size = bytes;
        if (arrlen != 0) {
            g->is_array = 1;
            g->array_len = arrlen > 0 ? arrlen : 0;
            g->elem_size = current_field_array_elem_size ? current_field_array_elem_size : type_size(type);
            if (g->elem_size <= 0) g->elem_size = 2;
            copy_last_array_dims_to_sym(g);
        }

        if (!l) {
            l = add_local_known(name, type, SC_GLOBAL, 0, bytes);
            strncpy(l->link_name, backing_name, sizeof(l->link_name) - 1);
            l->link_name[sizeof(l->link_name) - 1] = 0;
            if (arrlen != 0) {
                l->is_array = 1;
                l->array_len = arrlen > 0 ? arrlen : 0;
                l->elem_size = g->elem_size;
                l->dim_count = g->dim_count;
                memcpy(l->dims, g->dims, sizeof(l->dims));
            }
        }

        parse_global_init_list(g);

        /* If this was static char name[] = "...", parse_global_init_list()
         * inferred the real storage size.  Mirror that back into the local
         * alias used for references inside the function.
         */
        l = find_local_decl(name);
        if (l && g->is_array && l->is_array) {
            l->size = g->size;
            l->array_len = g->array_len;
            l->elem_size = g->elem_size;
            l->dim_count = g->dim_count;
            memcpy(l->dims, g->dims, sizeof(l->dims));
        }

        if (!accept(',')) break;
    }

    expect(';');
}

void scan_function_body(void)
{
    int brace;
    int can_decl;

    /* Restart the per-function for-loop counter so the frame-sizing scan and
     * the real codegen agree on which for-loop is which. */
    g_for_seq = 0;
    g_forren_n = 0;
    g_for_decl_seq = -1;
    g_for_decl_rename_index = 0;
    g_for_decl_recording = 0;
    g_scope_depth = 0;
    g_compound_literal_seq = 0;
    g_licm_seq = 0;
    g_vla_fwd_ngoto = 0;

    expect('{');
    enter_scope();              /* function body block */
    brace = 1;
    can_decl = 1;

    while (tok.kind != TOK_EOF && brace > 0) {
        if (tok.kind == '{') {
            enter_scope();
            brace++;
            next_token();
            can_decl = 1;
        } else if (tok.kind == '}') {
            brace--;
            next_token();
            leave_scope();
            can_decl = 1;
        } else if (tok.kind == TOK_FOR) {
            /*
             * Build and replay the whole for-statement (header + body)
             * through the AST builder/emitter (ast_scan_for_stmt, output
             * suppressed) instead of hand-walking tokens. This is the exact
             * same builder+emitter the real codegen pass uses, so frame
             * sizing - declarations inside the body, C99 for-init renaming,
             * and any AST-level for-loop fast path that reserves extra frame
             * space - stays in sync with the real pass by construction,
             * rather than needing a hand-written parallel scanner kept in
             * sync by hand. (That hand-written scanner used to live here;
             * see git history for its final form and the cast-vs-declaration
             * bug it once had to work around - both are now moot since this
             * runs the real parser instead of guessing at token shapes.)
             *
             * A 0 return (AST build declined) is left alone: it only happens
             * for malformed/unsupported input that the real pass will report
             * with a proper diagnostic anyway.
             */
            ast_scan_for_stmt();
            can_decl = 1;
        } else if (can_decl && tok.kind == TOK_TYPEDEF) {
            parse_typedef_decl();
            can_decl = 1;
        } else if (can_decl && starts_type()) {
            int t;
            int is_static_local;
            decl_is_extern = 0;
            decl_is_static = 0;
            decl_is_inline = 0;
            decl_is_const = 0;
            is_static_local = (tok.kind == TOK_STATIC);
            t = parse_base_type();
            if (tok.kind == ';') {
                next_token();
            } else if (is_static_local) {
                scan_static_local_decl_after_type(t);
            } else {
                scan_local_decl_after_type(t);
            }
            can_decl = 1;
        } else {
            int k;
            if (tok.kind == '(' && scan_compound_literal_if_present()) {
                can_decl = 0;
                continue;
            }
            k = tok.kind;
            if (k == TOK_ID) {
                next_token();
                if (tok.kind == '(')
                    current_function_has_call = 1;
            } else {
                next_token();
            }

            if (k == ';' || k == ':')
                can_decl = 1;
            else
                can_decl = 0;
        }
    }
}

void parse_typedef_decl(void)
{
    int base_type;
    int done;

    expect(TOK_TYPEDEF);

    /* Parse C89 typedef declarator lists with per-declarator pointer and
     * suffix handling:
     *     typedef unsigned long UL, *PUL;
     *     typedef int A4[4], FN(int), (*PF)(int);
     */
    base_type = parse_base_type();
    done = 0;

    while (!done && tok.kind != TOK_EOF) {
        int type;
        int typedef_array_len;
        int is_func;
        char name[64];

        type = base_type;
        typedef_array_len = 0;
        is_func = 0;
        name[0] = 0;

        while (accept('*')) {
            skip_type_qualifiers();
            type = type_add_ptr(type);
        }

        if (parse_funcptr_declarator(&type, name, sizeof(name))) {
            /* Parenthesized function-pointer typedef. */
        } else {
            if (tok.kind != TOK_ID) {
                error_here("identifier expected in typedef");
                while (tok.kind != ';' && tok.kind != TOK_EOF) next_token();
                expect(';');
                return;
            }
            strncpy(name, tok.text, sizeof(name) - 1);
            name[sizeof(name) - 1] = 0;
            next_token();
        }

        if (tok.kind == '[') {
            next_token();
            if (tok.kind == ']') {
                typedef_array_len = 0;
                next_token();
            } else {
                typedef_array_len = parse_const_int_expr();
                expect(']');
            }
            /* Multidimensional array typedefs (typedef T A[2][3]) collapse to a
             * flat element count: fold every inner dimension into the total so
             * sizeof(A) is element_size * product-of-dims, not just the first
             * dimension.  A typedef tracks only a single total length, so the
             * product is the correct flattened size. */
            while (tok.kind == '[') {
                next_token();
                if (tok.kind != ']') {
                    int inner = parse_const_int_expr();
                    if (typedef_array_len > 0 && inner > 0)
                        typedef_array_len *= inner;
                }
                expect(']');
            }
        } else if (tok.kind == '(') {
            skip_prototype_function_suffix();
            is_func = (type_ptr_depth(type) == 0);
        }

        add_typedef_name_ex(name, type, typedef_array_len, is_func);

        if (accept(','))
            continue;
        expect(';');
        done = 1;
    }
}

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

    if (tok.kind != '(' || !paren_starts_cast())
        return 0;

    next_token();
    parse_type_name_decl(&type, &size);
    expect(')');

    if (tok.kind != '{') {
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

int parse_global_init_atom(long *val, char *label, int labelsz)
{
    int sign;

    sign = 1;

    /*
     * Numeric scalar initializers may be full C constant expressions, not just
     * a single token.  This handles forms used by lzpack such as:
     *
     *     static long s_win_start = (MAXDIST * 2);
     *
     * and array bounds using parenthesized macro expressions.
     */
    if (tok.kind == TOK_NUM || tok.kind == TOK_CHARLIT ||
        tok.kind == '-' || tok.kind == '+' || tok.kind == '(' ||
        tok.kind == TOK_SIZEOF) {
        val[0] = parse_const_long_expr();
        if (label) label[0] = 0;
        return 1;
    }

    if (sign != 1) {
        error_here("numeric constant expected after sign");
        return 0;
    }

    if (tok.kind == TOK_STR || tok.kind == TOK_WSTR) {
        int sid;

        {
            char *lit;
            int is_wide;
            lit = read_adjacent_string_literals_ex(&is_wide);
            sid = add_string_ex(lit, is_wide);
            free(lit);
        }
        if (label && labelsz > 0)
            sprintf(label, "S%d", sid);
        return 2;       /* symbolic address */
    }

    if (tok.kind == TOK_ID) {
        /* An enumerator is an integer constant, not an address-bearing
         * external symbol.  Let the constant-expression parser consume the
         * whole expression so global initializers such as:
         *     enum E e = BLUE;
         *     int a[] = { RED, GREEN + 1 };
         * emit numeric data instead of dw _BLUE / dw _RED.
         */
        if (find_enum_const(tok.text) >= 0) {
            val[0] = parse_const_long_expr();
            if (label) label[0] = 0;
            return 1;
        }

        {
            struct Sym *ls;
            const char *lname;
            ls = find_sym(tok.text);
            /* A global initializer that names a function (e.g. a function-
             * pointer table) is a real reference: this bypasses the normal
             * runtime expression codegen entirely, so it must mark the
             * function needed itself rather than relying on the ast_gen_expr
             * SC_FUNC hook. */
            if (ls != NULL && ls->storage == SC_FUNC && ls->is_static)
                ls->deferred_body_needed = 1;
            lname = ls ? sym_asm_name(ls) : tok.text;
            if (label && labelsz > 0) {
                strncpy(label, lname, labelsz - 1);
                label[labelsz - 1] = 0;
            }
            next_token();

            /* pointer +/- constant: e.g. buf - 0x4000
             * Emit as a raw asm arithmetic expression so M80 can relocate it. */
            if (label && (tok.kind == '-' || tok.kind == '+')) {
                int neg = (tok.kind == '-');
                long save_pos2 = posi;
                long save_tok_start2 = tok_start_pos;
                int save_line2 = line_no;
                int save_tok_line2 = tok_line;
                struct Token save_tok2 = tok;
                next_token();
                if (tok.kind == TOK_NUM) {
                    char tmp[64];
                    const char *aname = asm_name_for(lname);
                    if (neg)
                        sprintf(tmp, "%s-%ld", aname, tok.val);
                    else
                        sprintf(tmp, "%s+%ld", aname, tok.val);
                    strncpy(label, tmp, labelsz - 1);
                    label[labelsz - 1] = 0;
                    next_token();
                } else {
                    posi = save_pos2;
                    tok_start_pos = save_tok_start2;
                    line_no = save_line2;
                    tok_line = save_tok_line2;
                    tok = save_tok2;
                }
            }
        }
        return 2;       /* symbolic address */
    }

    if (tok.kind == '&') {
        next_token();
        if (parse_global_compound_literal_address(label, labelsz))
            return 2;
        if (tok.kind == TOK_ID) {
            if (label && labelsz > 0) {
                struct Sym *ls;
                const char *lname;
                ls = find_sym(tok.text);
                lname = ls ? sym_asm_name(ls) : tok.text;
                strncpy(label, lname, labelsz - 1);
                label[labelsz - 1] = 0;
            }
            next_token();
            return 2;   /* symbolic address */
        }
        error_here("identifier expected after & in initializer");
        if (tok.kind != ',' && tok.kind != ';' && tok.kind != '}')
            next_token();
        return 0;
    }

    error_here("constant initializer expected");
    if (tok.kind != ',' && tok.kind != ';' && tok.kind != '}')
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
        strncpy(s->init_labels[idx], label, sizeof(s->init_labels[0]) - 1);
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

static void global_init_write_char_array_string_at(struct Sym *s, int baseoff, int count, const char *str)
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
        tok.kind == TOK_STR) {
        char *lit;
        int is_wide;
        lit = read_adjacent_string_literals_ex(&is_wide);
        if (is_wide)
            error_here("wide string cannot initialize char array field");
        else
            global_init_write_char_array_string_at(s, baseoff, count, lit);
        free(lit);
        return;
    }

    had_brace = 0;
    if (tok.kind == '{') {
        next_token();
        had_brace = 1;
    }
    n = 0;
    maxn = 0;
    while (tok.kind != TOK_EOF && (had_brace || count <= 0 || n < count) &&
           (had_brace || tok.kind != '}')) {
        if (had_brace && tok.kind == '}')
            break;
        if (had_brace && tok.kind == '[') {
            next_token();
            n = parse_const_int_expr();
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
        if (had_brace && tok.kind == '}') break;
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

    sid = type_struct_id(type);
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
            if (field_defs[i].parent_struct_id == sid && !field_defs[i].is_promoted) {
                first = &field_defs[i];
                break;
            }
        }

        if (first && tok.kind != TOK_EOF && tok.kind != '}') {
            if (first->is_array)
                parse_global_init_array_at(s, first->elem_type, first->array_len, first->elem_size, baseoff);
            else
                parse_global_init_type_at(s, first->type, first->size, baseoff);

            /* Braceless union element in an array (static U a[] = {1,2,3})
             * stops after its single initializer; the array loop owns the
             * comma.  Only a braced element may report extra members. */
            if (had_brace && accept(',')) {
                if (tok.kind != '}') {
                    error_here("too many union initializer elements");
                    while (tok.kind != TOK_EOF && tok.kind != '}')
                        next_token();
                }
            }
        }

        if (had_brace)
            expect('}');
        return;
    }

    for (i = 0; i < nfield_defs && tok.kind != TOK_EOF && tok.kind != '}'; ++i) {
        struct FieldDef *fd;
        if (tok.kind == '.') {
            next_token();
            if (tok.kind != TOK_ID) {
                error_here("expected a field designator, such as '.field = value'");
                while (tok.kind != TOK_EOF && tok.kind != '}')
                    next_token();
                break;
            }
            fd = find_field_def(sid, tok.text);
            if (fd == NULL) {
                error_here("unknown field initializer designator");
                while (tok.kind != TOK_EOF && tok.kind != '}')
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
                if (bfd->parent_struct_id == sid && !bfd->is_promoted) {
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
            global_init_write_value_at(s, baseoff + unit_off, NULL, (long)(unit & 0xffffU), 2, 0);
            if (k > i)
                i = k - 1;
            if (stop)
                break;
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
        if (tok.kind == '}') break;
        if (tok.kind == '.') i = -1;
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
            if (tok.kind != ',' && tok.kind != '}') next_token();
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
            if (tok.kind != ',' && tok.kind != '}')
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
            if (tok.kind != ',' && tok.kind != '}')
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

    while (tok.kind != TOK_EOF && tok.kind != '}') {
        if (tok.kind == '[') {
            int idx;
            int span;

            next_token();
            idx = parse_const_int_expr();
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
        if (tok.kind == '{' && s->dim_count > 0 && level + 1 < s->dim_count)
            parse_global_scalar_array_init_level(s, np, level + 1);
        else
            parse_global_scalar_array_init_scalar(s, np);

        if (!accept(','))
            break;
        if (tok.kind == '}')
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
        tok.kind == TOK_STR) {
        n = 0;
        while (tok.kind == TOK_STR) {
            int si;
            for (si = 0; tok.text[si]; ++si) {
                grow_init_cap(s, n + 1);
                sprintf(s->init_labels[n], "%u", (unsigned char)tok.text[si]);
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
    if ((s->type & TYPE_STRUCT) && type_ptr_depth(s->type) == 0 && tok.kind == '{') {
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
            while (tok.kind != TOK_EOF && tok.kind != ';' && tok.kind != ',')
                next_token();
        }
        return;
    }

    n = 0;
    maxn = 0;
    while (tok.kind != TOK_EOF && tok.kind != '}') {
        if (tok.kind == '[') {
            int idx;
            int span;
            int target;

            next_token();
            idx = parse_const_int_expr();
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
        if (tok.kind == '{' && s->dim_count > 1)
            parse_global_scalar_array_init_level(s, &n, 1);
        else
            parse_global_scalar_array_init_scalar(s, &n);
        if (n > maxn) maxn = n;
        if (!accept(','))
            break;
        if (tok.kind == '}')
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

/* Cheap pre-filter for try_speculative_noix_function_body: is it even worth
 * attempting? A function that makes any call almost certainly pushes
 * arguments for it (current_function_has_call is set during the scan pass,
 * so it is already known here) - cheap to check and saves a wasted extra
 * generation pass on the common case. main() is excluded outright: it is
 * never a hot leaf, and the __mrun-shim emission right after this codegen
 * assumes the ordinary IX-framed epilogue shape.
 *
 * local_bytes must be exactly 0 (no declared locals AND no compiler-
 * generated temporaries - #itmp/#clit locals count too, since local_bytes is
 * just local_size after the scan). This is load-bearing, not a size
 * heuristic: the disabled no-IX-frame support only ever extended to
 * SC_PARAM addressing (frame_sp_offset_for_sym, and the current_omit_ix_
 * frame checks in emit_load_sym_value_direct/emit_load_frame_addr_hl are
 * both gated on `s->storage == SC_PARAM`). A local variable is still
 * addressed (ix+N) unconditionally - with no `push ix`/`ld ix,0` this reads
 * and writes through whatever garbage IX happens to hold at entry, silently
 * corrupting memory rather than crashing. A speculative generation pass
 * cannot catch this the way it catches an SP-shifting push (there is no
 * "did the buffer do something wrong" signal to check for - the emitted
 * instructions are individually valid, just relative to the wrong
 * register), so functions with any local storage must be excluded here,
 * before ever attempting it, rather than verified after the fact. */
static int function_qualifies_for_speculative_noix(const char *name, int local_bytes)
{
    if (strcmp(name, "main") == 0)
        return 0;
    if (current_function_has_call)
        return 0;
    if (local_bytes != 0)
        return 0;
    return 1;
}

/* Does the buffered speculative body contain anything unsafe for a function
 * that never set up an IX frame? Two independent signals, both fatal:
 *
 *   - "push": the only way SP-relative (sp+N) addressing already computed
 *     earlier in the body can go wrong - a push shifts SP out from under it,
 *     and dcc has no live SP-delta tracking to compensate.
 *   - "(ix": IX was never loaded with anything meaningful in this function
 *     (no `push ix`/`ld ix,0`/`add ix,sp`), so ANY `(ix+N)`/`(ix-N)` memory
 *     reference anywhere in the body reads or writes through whatever
 *     garbage IX happens to hold at entry - silently corrupting memory
 *     rather than crashing. This is not hypothetical: the disabled no-IX
 *     support only ever touched a handful of load/store helpers
 *     (emit_load_sym_value_direct and friends in dcc_symbols.c) to check
 *     current_omit_ix_frame; other, unrelated fast paths elsewhere in the
 *     codegen (e.g. gen_return_ast's dedicated "return a 1-byte identifier"
 *     shortcut) emit `(ix+N)` addressing directly and were never taught
 *     about current_omit_ix_frame at all, so cannot be trusted to have
 *     skipped it just because this function has no locals. Rather than
 *     audit every such shortcut across the codebase (and every future one
 *     anyone adds), checking for the address mode itself is exact: a
 *     correctly-generated no-IX-frame body can never legitimately contain
 *     it, so any occurrence at all is proof something used it by mistake.
 *
 * Every push/(ix in dcc's own codegen is emitted as literal text (there are
 * no comments or other decoration at this stage - dccpeep is a separate
 * later pass), so a plain substring search is exact, not a heuristic. */
static int tmpfile_unsafe_for_noix(FILE *f)
{
    long size;
    char *buf;
    int found;

    fseek(f, 0, SEEK_END);
    size = ftell(f);
    rewind(f);
    if (size <= 0)
        return 0;
    buf = (char *)xmalloc((size_t)size + 1);
    if (fread(buf, 1, (size_t)size, f) != (size_t)size)
        fatal("cannot read speculative no-ix-frame temp file");
    buf[size] = 0;
    found = strstr(buf, "push") != NULL || strstr(buf, "(ix") != NULL;
    free(buf);
    return found;
}

/* Speculatively generate `name`'s already-scanned body without an IX frame
 * (params/locals addressed sp-relative - see current_function_safe_to_omit_ix,
 * which stays hard-disabled for the ordinary path below), and check whether
 * it ever pushed anything. A push is the only way that addressing can go
 * wrong: it shifts SP out from under every sp+N address already computed,
 * and dcc has no live SP-delta tracking to compensate (auditing every push
 * emission site to add that tracking would be a much larger, more invasive
 * change - see the long comment on current_function_safe_to_omit_ix). Rather
 * than prove push-freedom statically, generate into a scratch buffer and
 * inspect the result: no push anywhere means SP provably never moved, so
 * every address in the buffer is correct and it can be used as-is; a push
 * means discard it and let the caller regenerate normally with the IX frame,
 * via the exact same rewind this function performs on failure.
 *
 * Returns 1 if the no-IX version was kept and already written to outf; 0 if
 * the caller must still run the normal (IX-framed) codegen path itself, with
 * every relevant piece of parser/codegen state already rewound to the body
 * start as if this function had never been called - the same state the
 * scan-to-codegen reset just above this call already established. */
static int try_speculative_noix_function_body(const char *name, int type,
                                                     int local_bytes, struct Sym *s,
                                                     long body_start_pos,
                                                     long body_start_tok_start,
                                                     int body_start_line,
                                                     int body_start_tok_line,
                                                     struct Token body_start_tok,
                                                     int body_start_nlocals,
                                                     int body_start_local_size)
{
    FILE *scratch;
    FILE *saved_outf_ptr;
    int saved_stack_check;
    int generated_stack_check;
    int implicit_zero_return;
    int c;

    implicit_zero_return = strcmp(name, "main") == 0 &&
                            (type & 15) == TYPE_INT && type_ptr_depth(type) == 0;

    scratch = tmpfile();
    if (scratch == NULL)
        fatal("cannot create speculative no-ix-frame temp file");

    saved_outf_ptr = outf;
    saved_stack_check = opt_stack_check;
    outf = scratch;
    opt_stack_check = s->stack_check_enabled;
    /* emit_runtime_extrn_if_needed (dcc_symbols.c) caches which runtime-
     * helper EXTRNs have already been emitted in a *persistent*,
     * compilation-wide table, so a helper's declaration is normally only
     * ever written once no matter how many call sites reference it. That
     * cache has no idea this generation is speculative and may be thrown
     * away: without g_inline_body_buffering set, a call emitted here (e.g.
     * -fstack-check's `call __stchk` in every prologue) marks the helper as
     * "already declared" globally, and if this attempt is then discarded,
     * the real fallback generation's identical call skips its own EXTRN
     * line - producing a `call` with no matching declaration anywhere in
     * the actual output. g_inline_body_buffering is the existing mechanism
     * for exactly this hazard (see emit_runtime_extrn_if_needed and the
     * static-inline-body-buffering branch above): it makes every EXTRN
     * unconditionally self-contained in whatever `outf` currently points
     * at, instead of relying on the global cache. */
    g_inline_body_buffering++;
    nulabels = 0;
    current_return_label = new_label();
    g_for_seq = 0;
    g_forren_n = 0;
    g_for_decl_seq = -1;
    g_for_decl_rename_index = 0;
    g_for_decl_recording = 0;
    g_scope_depth = 0;
    g_static_local_func_index = (int)(s - globals);
    g_static_local_seq = 0;
    g_compound_literal_seq = 0;
    g_licm_seq = 0;
    emit_function_prologue(name, local_bytes, 1);
    gen_compound();
    emit_function_epilogue(implicit_zero_return);
    g_inline_body_buffering--;
    generated_stack_check = opt_stack_check;
    opt_stack_check = saved_stack_check;
    outf = saved_outf_ptr;

    /* check_undefined_user_labels() is deliberately not called above: if
     * this attempt is about to be discarded, calling it here would both
     * double-report a genuine undefined-label error (the caller's normal
     * codegen path below already calls it once) and, more subtly, leave
     * ulabel_defined[]/nulabels populated from this attempt's goto/label
     * bookkeeping for the caller's fresh gen_compound() run to collide
     * with - exactly the "duplicate goto label" false positive this
     * function's first version produced by forgetting to reset nulabels
     * (and the rest of the per-function codegen state below) before
     * falling back. */
    if (!tmpfile_unsafe_for_noix(scratch)) {
        check_undefined_user_labels();
        rewind(scratch);
        while ((c = fgetc(scratch)) != EOF)
            fputc(c, outf);
        fclose(scratch);
        opt_stack_check = generated_stack_check;
        return 1;
    }

    fclose(scratch);

    /* Undo every bit of per-function codegen state this discarded attempt
     * touched - the same set the scan-to-codegen transition above this
     * function resets - so the caller's normal codegen path runs exactly as
     * if this function had never been called. */
    posi = body_start_pos;
    tok_start_pos = body_start_tok_start;
    line_no = body_start_line;
    tok_line = body_start_tok_line;
    tok = body_start_tok;
    nlocals = body_start_nlocals;
    local_size = body_start_local_size;
    nulabels = 0;
    current_return_label = new_label();
    g_for_seq = 0;
    g_forren_n = 0;
    g_for_decl_seq = -1;
    g_for_decl_rename_index = 0;
    g_for_decl_recording = 0;
    g_scope_depth = 0;
    g_static_local_func_index = (int)(s - globals);
    g_static_local_seq = 0;
    g_compound_literal_seq = 0;
    g_licm_seq = 0;
    return 0;
}

void parse_function_or_global(int base_type)
{
    int done;

    done = 0;

    while (!done && tok.kind != TOK_EOF) {
        int type;
        char name[64];
        int arrlen;
        struct Sym *s;
        long body_end_pos;
        long body_end_tok_start;
        int body_end_line;
        int body_end_tok_line;
        struct Token body_end_tok;
        long saved_pos;
        long saved_tok_start;
        int saved_line;
        int saved_tok_line;
        struct Token saved_tok;
        int saved_nlocals;
        int saved_local_size;
        int saved_param_offset;
        int saved_nenum_consts;
        int saved_nulabels;
        int saved_stack_check;

        int base_is_func_typedef;
        int is_funcret_funcptr_decl;

        type = base_type;
        base_is_func_typedef = g_typedef_is_func;
        is_funcret_funcptr_decl = 0;
        name[0] = 0;

        /* Each declarator starts again from the shared declaration-specifier
         * base type.  This is the important C declarator rule for forms like:
         *     int *a, b, c[10];
         * where only a is a pointer. */
        while (accept('*')) {
            skip_type_qualifiers();
            type = type_add_ptr(type);
            base_is_func_typedef = 0;
        }

        if (!parse_funcptr_declarator(&type, name, sizeof(name))) {
            if (tok.kind != TOK_ID) {
                error_here("identifier expected");
                while (tok.kind != ';' && tok.kind != TOK_EOF) next_token();
                expect(';');
                return;
            }

            strncpy(name, tok.text, sizeof(name) - 1);
            name[sizeof(name) - 1] = 0;
            next_token();
        }

        if (g_funcptr_is_funcret_decl) {
            g_funcptr_is_funcret_decl = 0;
            is_funcret_funcptr_decl = 1;
        }

        /* A typedef-name that denotes a function type can declare a function
         * without repeating the parameter list:
         *     typedef int fn_t(int);
         *     extern fn_t foo;
         * Treat this as a function declaration.  Pointer declarators such as
         * fn_t *fp have already cleared base_is_func_typedef above. */
        if (base_is_func_typedef && g_funcptr_decl_array_len == 0) {
            s = add_global(name, type, SC_FUNC);
            s->is_inline |= decl_is_inline;
            parse_function_return_type = type;
            if (decl_is_static) {
                s->is_static = 1;
                s->needs_extrn = 0;
            } else if (!s->is_defined)
                s->needs_extrn = 1;
            if (accept(','))
                continue;
            expect(';');
            return;
        }

        /* Function declarator or definition. */
        if (is_funcret_funcptr_decl || (g_funcptr_decl_array_len == 0 && accept('('))) {
            s = add_global(name, type, SC_FUNC);
            s->is_inline |= decl_is_inline;
            parse_function_return_type = type;
            if (decl_is_static) {
                s->is_static = 1;
                s->needs_extrn = 0;
            }
            if (!is_funcret_funcptr_decl)
                parse_param_list();
            copy_parsed_prototype_to_sym(s);
            if (!is_funcret_funcptr_decl)
                expect(')');

            /* Snapshot nlocals after prototype params are registered but before
             * K&R declarations: used to detect main() with no parameters. */
            int pre_params_nlocals = nlocals;

            if (!g_proto_has && tok.kind != '{' && tok.kind != ';' && tok.kind != ',')
                parse_old_style_param_declarations();

            if (tok.kind == '{') {
                /* Set once here, covering both frame-sizing scan passes below
                 * and the real codegen pass later in this same block, so a
                 * hoist decision keyed on "am I compiling function X" (see
                 * ast_for_hoist_global_member_value_supported) is identical
                 * across all passes over this function - required for the
                 * scan pass to reserve the same frame space the real pass
                 * allocates. */
                strncpy(g_current_compiling_func, name, sizeof(g_current_compiling_func) - 1);
                g_current_compiling_func[sizeof(g_current_compiling_func) - 1] = 0;

                /* Capture the stack-check state in effect at the function's
                 * opening brace.  This is the value baked into THIS function's
                 * prologue and VLA guards (stored in s->stack_check_enabled
                 * below and re-applied before the real codegen pass).
                 *
                 * Every body-inspection helper and frame-sizing scan below
                 * tokenizes past the body, which processes any later
                 * `#pragma stack_check(...)` and mutates the global
                 * opt_stack_check as a side effect.  None of them READ
                 * opt_stack_check (runtime-call emission is a no-op while
                 * scanning - see emit_runtime_call's scan_mode guard), so a
                 * single restore after the group re-synchronizes the flag with
                 * the rewound source position; the two rewind blocks further
                 * below each restore it again alongside posi/tok/nlocals. */
                saved_stack_check = opt_stack_check;
                record_inline_function_if_simple(s);
                record_narrow_return_expr_if_simple(s);
                if (function_body_mentions_multiuse_inline_call())
                    reserve_inline_temp_locals();
                opt_stack_check = saved_stack_check;

                saved_pos = posi;
                saved_tok_start = tok_start_pos;
                saved_line = line_no;
                saved_tok_line = tok_line;
                saved_tok = tok;
                saved_nlocals = nlocals;
                saved_local_size = local_size;
                saved_param_offset = param_offset;
                saved_nenum_consts = nenum_consts;
                saved_nulabels = nulabels;

                current_function_has_call = 0;
                g_static_local_func_index = (int)(s - globals);
                g_static_local_seq = 0;
                asm_suppress_depth++;
                scan_function_body();
                asm_suppress_depth--;
                body_end_pos = posi;
                body_end_tok_start = tok_start_pos;
                body_end_line = line_no;
                body_end_tok_line = tok_line;
                body_end_tok = tok;
                current_local_bytes = local_size;
                if (current_local_bytes > max_function_local_bytes)
                    max_function_local_bytes = current_local_bytes;

                posi = saved_pos;
                tok_start_pos = saved_tok_start;
                line_no = saved_line;
                tok_line = saved_tok_line;
                tok = saved_tok;
                nlocals = saved_nlocals;
                local_size = saved_local_size;
                param_offset = saved_param_offset;
                nenum_consts = saved_nenum_consts;
                opt_stack_check = saved_stack_check;
                /* ast_scan_for_stmt (called by scan_function_body via the AST
                 * builder/emitter for for-loops) can now reach a labeled
                 * statement inside a loop body and call define_user_label,
                 * which the old hand-walked scanner never did. Reset nulabels
                 * before the second scan pass so it does not see the first
                 * scan's labels as already-defined duplicates - matching how
                 * nlocals/local_size are reset here for the same reason. */
                nulabels = saved_nulabels;

                g_static_local_func_index = (int)(s - globals);
                g_static_local_seq = 0;
                asm_suppress_depth++;
                scan_function_body();
                asm_suppress_depth--;

                posi = saved_pos;
                tok_start_pos = saved_tok_start;
                line_no = saved_line;
                tok_line = saved_tok_line;
                tok = saved_tok;
                nenum_consts = saved_nenum_consts;
                opt_stack_check = saved_stack_check;

                s->is_defined = 1;
                s->needs_extrn = 0;
                s->stack_check_enabled = saved_stack_check;

                nulabels = 0;
                current_return_label = new_label();
                current_return_type = type;
                /* Restart the for-loop counter for the codegen pass so it
                 * lines up with the frame-sizing scan. */
                g_for_seq = 0;
                g_forren_n = 0;
                g_for_decl_seq = -1;
                g_for_decl_rename_index = 0;
                g_for_decl_recording = 0;
                /* Codegen rebuilds the local table exactly as the frame-sizing
                 * scan did - block scopes truncate nlocals as they close - so
                 * restart from just the parameters with an empty scope stack.
                 * Both passes therefore assign identical frame offsets. */
                nlocals = saved_nlocals;
                local_size = saved_local_size;
                g_scope_depth = 0;
                g_static_local_func_index = (int)(s - globals);
                g_static_local_seq = 0;
                g_compound_literal_seq = 0;
                g_licm_seq = 0;
                opt_stack_check = s->stack_check_enabled;
                if (static_inline_body_can_be_buffered(s)) {
                    FILE *saved_outf;

                    s->deferred_body_file = tmpfile();
                    if (s->deferred_body_file == NULL)
                        fatal("cannot create deferred body temp file");
                    saved_outf = outf;
                    outf = s->deferred_body_file;
                    g_inline_body_buffering++;
                    emit_function_prologue(name, current_local_bytes, current_function_safe_to_omit_ix(type, current_local_bytes));
                    gen_compound();
                    check_undefined_user_labels();
                    emit_function_epilogue(0);
                    g_inline_body_buffering--;
                    outf = saved_outf;
                } else if (function_qualifies_for_speculative_noix(name, current_local_bytes) &&
                           try_speculative_noix_function_body(name, type, current_local_bytes, s,
                                                               saved_pos, saved_tok_start, saved_line,
                                                               saved_tok_line, saved_tok,
                                                               saved_nlocals, saved_local_size)) {
                    /* No-IX-frame body already generated and written to outf
                     * inside try_speculative_noix_function_body. */
                } else if (plain_static_body_can_be_buffered(s, name)) {
                    FILE *saved_outf;

                    s->deferred_body_file = tmpfile();
                    if (s->deferred_body_file == NULL)
                        fatal("cannot create deferred body temp file");
                    saved_outf = outf;
                    outf = s->deferred_body_file;
                    g_inline_body_buffering++;
                    emit_function_prologue(name, current_local_bytes, current_function_safe_to_omit_ix(type, current_local_bytes));
                    gen_compound();
                    check_undefined_user_labels();
                    emit_function_epilogue(0);
                    g_inline_body_buffering--;
                    outf = saved_outf;
                } else {
                    emit_function_prologue(name, current_local_bytes, current_function_safe_to_omit_ix(type, current_local_bytes));
                    gen_compound();
                    check_undefined_user_labels();
                    emit_function_epilogue(strcmp(name, "main") == 0 &&
                                           (type & 15) == TYPE_INT &&
                                           type_ptr_depth(type) == 0);
                }
                nenum_consts = saved_nenum_consts;

                /* Emit the __mrun shim that start: dispatches to.  When main has
                 * no args the shim omits any reference to __build_argv/__argc/argv
                 * so dccrtlstrip drops those runtime blocks (~350 bytes). */
                if (strcmp(name, "main") == 0) {
                    int has_args = !((s->has_proto  && s->proto_nargs == 0) ||
                                     (!s->has_proto && pre_params_nlocals == 0));
                    fprintf(outf, "\n\tpublic __mrun\n");
                    if (has_args) {
                        fprintf(outf, "\textrn __build_argv\n");
                        fprintf(outf, "\textrn __argc\n");
                        fprintf(outf, "\textrn argv\n");
                        fprintf(outf, "__mrun:\n");
                        fprintf(outf, "\tcall __build_argv\n");
                        fprintf(outf, "\tld hl,argv\n");
                        fprintf(outf, "\tpush hl\n");
                        fprintf(outf, "\tld hl,(__argc)\n");
                        fprintf(outf, "\tpush hl\n");
                        fprintf(outf, "\tcall _main\n");
                        fprintf(outf, "\tpop de\n");
                        fprintf(outf, "\tpop de\n");
                    } else {
                        fprintf(outf, "__mrun:\n");
                        fprintf(outf, "\tcall _main\n");
                    }
                    fprintf(outf, "\tret\n");
                }

                posi = body_end_pos;
                tok_start_pos = body_end_tok_start;
                line_no = body_end_line;
                tok_line = body_end_tok_line;
                tok = body_end_tok;
                return;
            }

            /*
             * C89: a file-scope function declaration has external linkage even
             * without the 'extern' keyword.  Record it as a possible external,
             * but the M80 EXTRN is emitted only if actually referenced and not
             * later defined in this translation unit.
             */
            if (decl_is_static) {
                s->is_static = 1;
                s->needs_extrn = 0;
            } else if (!s->is_defined)
                s->needs_extrn = 1;

            if (accept(','))
                continue;
            expect(';');
            return;
        }

        {
            int total_count = 1;
            int first_dim = g_funcptr_decl_array_len;
            int base_size = type_size(type);
            int dim_count = 0;
            int dims[MAX_ARRAY_DIMS];
            int i;
            int inner_count;

            for (i = 0; i < MAX_ARRAY_DIMS; ++i)
                dims[i] = 0;

            if (g_funcptr_decl_array_len > 0) {
                total_count = g_funcptr_decl_array_len;
                dim_count = 1;
                dims[0] = g_funcptr_decl_array_len;
            }
            g_funcptr_decl_array_len = 0;

            while (tok.kind == '[') {
                int d;
                next_token();
                if (tok.kind == ']') {
                    d = 0;
                    next_token();
                } else {
                    d = parse_const_int_expr();
                    expect(']');
                }
                if (dim_count < MAX_ARRAY_DIMS) {
                    dims[dim_count++] = d;
                } else {
                    /* Array rank exceeds the supported maximum (C99/C11
                     * 5.2.4.1 guarantees at least 12); keep dim_count capped
                     * so dims[] is never indexed out of range. */
                    error_here("too many array dimensions");
                }
            }

            if (dim_count > 0) {
                first_dim = dims[0];

                total_count = 1;
                for (i = 0; i < dim_count; ++i) {
                    if (dims[i] <= 0) {
                        total_count = 0;
                        break;
                    }
                    total_count *= dims[i];
                }
            }

            arrlen = first_dim;
            if (arrlen == 0 && dim_count == 0 && g_typedef_array_len > 0) {
                arrlen = g_typedef_array_len;
                first_dim = g_typedef_array_len;
                total_count = g_typedef_array_len;
                dim_count = 1;
                dims[0] = g_typedef_array_len;
            }

            inner_count = 1;
            if (dim_count > 1) {
                for (i = 1; i < dim_count; ++i) {
                    if (dims[i] <= 0) {
                        inner_count = 1;
                        break;
                    }
                    inner_count *= dims[i];
                }
            }

            if (decl_is_extern) {
                int already_declared = (find_global(name) != NULL);
                s = add_global(name, type, SC_EXTERN);
                if (!already_declared && !asm_name_is_internal_public(name))
                    s->needs_extrn = 1;
                else if (asm_name_is_internal_public(name))
                    s->needs_extrn = 0;

                /* Extern declarations may also be declarator lists:
                 *     extern int a, *b, f(void);
                 * Do not skip to ';' after the first one. */
                if (accept(','))
                    continue;
                expect(';');
                return;
            }

            s = add_global(name, type, SC_GLOBAL);
            if (s->storage == SC_EXTERN)
                s->storage = SC_GLOBAL;
            s->is_defined = 1;
            s->needs_extrn = 0;
            if (decl_is_static)
                s->is_static = 1;

            if (dim_count > 0 || arrlen || total_count == 0) {
                s->is_array = 1;
                s->array_len = arrlen;
                s->dim_count = dim_count;
                for (i = 0; i < MAX_ARRAY_DIMS; ++i)
                    s->dims[i] = (i < dim_count) ? dims[i] : 0;

                if (dim_count > 1)
                    s->elem_size = inner_count * base_size;
                else
                    s->elem_size = base_size;
                if (s->elem_size <= 0) s->elem_size = 2;

                if (total_count > 0)
                    s->size = object_array_size(type, total_count);
                else
                    s->size = 0;
            } else if (g_ptr_array_dim_count > 0) {
                int pi;
                s->elem_size = g_ptr_array_elem_size;
                s->dim_count = g_ptr_array_dim_count;
                for (pi = 0; pi < MAX_ARRAY_DIMS; ++pi)
                    s->dims[pi] = (pi < g_ptr_array_dim_count) ? g_ptr_array_dims[pi] : 0;
            }
            g_ptr_array_dim_count = 0;
            g_ptr_array_elem_size = 0;

            parse_global_init_list(s);
        }

        if (accept(','))
            continue;

        expect(';');
        done = 1;
    }
}

void add_predefined_extern(const char *name, int type, int storage)
{
    struct Sym *s;

    s = add_global(name, type, storage);
    if (!asm_name_is_internal_public(name))
        s->needs_extrn = 1;
}

void parse_translation_unit(void)
{
    emit("\t; dcc stage-1d output\n\n      cseg\n");

    /*
     * Do not predeclare C library/runtime functions here.  In C89, file-scope
     * prototypes in headers already have external linkage even without the
     * 'extern' keyword; parse_function_or_global() records those prototypes.
     * If no prototype is visible, call codegen can still create an implicit
     * extern function symbol.  M80 EXTRN records are deferred until end of
     * translation unit and only emitted for symbols that were actually used and
     * not defined here.
     */

    /* Predefined linker-visible bounds of the final app's BSS.
     * Compile-only helper modules must not define or reference these, or
     * multiple independently compiled modules will collide at link time. */
    if (!opt_module) {
        add_global("__bssb", TYPE_CHAR, SC_EXTERN);
        add_global("__bsse", TYPE_CHAR, SC_EXTERN);
        add_global("__hstart", TYPE_CHAR, SC_EXTERN);
        add_global("__data_end", TYPE_CHAR, SC_EXTERN);
    }

    add_predefined_extern("stdin", TYPE_INT, SC_EXTERN);
    add_predefined_extern("stdout", TYPE_INT, SC_EXTERN);
    add_predefined_extern("stderr", TYPE_INT, SC_EXTERN);
    add_predefined_extern("errno", TYPE_INT, SC_EXTERN);

    next_token();

    while (tok.kind != TOK_EOF) {
        if (tok.kind == TOK_TYPEDEF) {
            parse_typedef_decl();
        } else if (starts_type()) {
            int t;
            decl_is_extern = 0;
            decl_is_static = 0;
            decl_is_inline = 0;
            decl_is_const = 0;
            t = parse_type();
            if (tok.kind == ';') {
                next_token();
            } else {
                parse_function_or_global(t);
            }
        } else if (tok.kind == TOK_ID && is_unsupported_target_type_name(tok.text)) {
            int t;
            decl_is_extern = 0;
            decl_is_static = 0;
            decl_is_inline = 0;
            decl_is_const = 0;
            t = parse_type();
            if (tok.kind == ';')
                next_token();
            else
                parse_function_or_global(t);
        } else if (tok.kind == TOK_ID) {
            /* C89: implicit int return type for function definition/declaration. */
            decl_is_extern = 0;
            decl_is_static = 0;
            decl_is_inline = 0;
            decl_is_const = 0;
            parse_function_or_global(TYPE_INT);
        } else {
            error_here("external declaration expected");
            next_token();
        }
    }
}

