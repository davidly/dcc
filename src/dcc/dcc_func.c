/**
 * @file dcc_func.c
 * @brief Parses file-scope declarations and coordinates function capture.
 *
 * @par Role
 * Handles prototype and K&R parameter lists, typedefs, top-level declaration
 * dispatch, frame-sizing scans, inline and narrowing metadata, debug records,
 * deferred static bodies, and the begin/end lifecycle for each MIR function.
 *
 * @par Key entry points
 * parse_translation_unit(), parse_function_or_global(), scan_function_body(),
 * begin_function_mir(), finish_function_mir(), and
 * emit_needed_deferred_bodies().
 *
 * @par Boundary
 * dcc_stmt.c traverses compound bodies and dcc_global_init.c records file-scope
 * initializers. This module coordinates MIR capture; selected MIR candidates,
 * never AST or legacy output, supply production function bodies.
 */

#include "dcc.h"
#include "dcc_ast.h"
#include "dcc_mir.h"

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
        /* The one leading local declaration try_scan_inline_local_decl may
         * have captured (see struct Sym's inline_local_* fields) - a read
         * of it is simple the same way a parameter read is, since it's
         * always materialized into its own per-call-site temp before this
         * expression runs, exactly like a parameter that needs one. Only
         * ever set once eligibility for it is otherwise fully established,
         * so this can't wrongly accept a name from some other function. */
        if (fn->has_inline_local && !strcmp(n->sval, fn->inline_local_name))
            return 1;
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
        /* Same hazard as TOK_INC/TOK_DEC just above, for the same reason:
         * substituted verbatim onto a parameter, the assignment target
         * would become the caller's argument EXPRESSION, not an lvalue
         * ("3 = 0" for a call site like f(3)) - inline_param_index's own
         * caller (gen_assign_ast, dcc_ast_gen_expr.c) then calls find_sym
         * on whatever AST_IDENT node the substitution produced there,
         * which is simply absent for a non-identifier expression,
         * crashing on a NULL name. Found via a minimal repro: `static
         * inline int f(int cond,int idx){if(cond)idx=0;return idx+1;}` -
         * the existing guard-capture machinery (inline_return_expr_from_
         * seq's "side-effect-only guard" case, this file) explicitly
         * supports folding `if (cond) idx=0;` ahead of a return into the
         * inlined expression, with no check that `idx` here is a
         * parameter being reassigned rather than some unrelated side
         * effect like the die() call its own comment uses as the
         * motivating example - this is the missing check that makes that
         * substitution sound.
         *
         * Deliberately narrower than inline_expr_touches_param(fn, n->a)
         * (which the TOK_INC/TOK_DEC case above uses): that checks whether
         * a parameter appears ANYWHERE in n->a, but the actual hazard is
         * only when n->a - the assignment target ITSELF - reduces to a
         * bare parameter identifier. `fold_mem[base+idx*esz] = v;` (an
         * array-element target whose INDEX merely reads parameters) is
         * perfectly sound to substitute and must stay eligible - an
         * earlier, broader version of this check (matching the INC/DEC
         * one exactly) wrongly declined it too, regressing several
         * already-working inline candidates (tests/tinline.c, tinlinfb.c,
         * attnc11.c, and others) that never touched the actual crash. */
        if (n->a != NULL && n->a->kind == AST_IDENT &&
            inline_param_index(fn, n->a->sval) >= 0)
            return 0;
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

/* True if `name` is ever reassigned anywhere in n - a plain AST_ASSIGN
 * targeting it directly, or ++/-- applied to it. Used only to confirm the
 * one local declaration try_scan_inline_local_decl considers capturing is
 * genuinely single-assignment (initialized once, read only thereafter):
 * substituting it with a temp materialized once per call site, the same
 * way a parameter needing one already is, is only sound under that
 * invariant. A write through it (`*s = ...` or `s->f = ...`) is fine and
 * doesn't trip this - only reassigning the pointer/scalar itself does. Not
 * bounded to "after the declaration": scanning the whole tree (decl
 * included) is a harmless superset, since an AST_DECL span's fields are
 * unset (NULL) and the recursion into them immediately terminates. */
static int inline_local_is_reassigned(const char *name, const struct AstNode *n)
{
    int i;

    if (n == NULL || name == NULL)
        return 0;
    if (n->kind == AST_ASSIGN && n->a != NULL && n->a->kind == AST_IDENT &&
        !strcmp(n->a->sval, name))
        return 1;
    if ((n->kind == AST_UNARY || n->kind == AST_POSTFIX) &&
        (n->op == TOK_INC || n->op == TOK_DEC) &&
        n->a != NULL && n->a->kind == AST_IDENT && !strcmp(n->a->sval, name))
        return 1;
    if (inline_local_is_reassigned(name, n->a) || inline_local_is_reassigned(name, n->b) ||
        inline_local_is_reassigned(name, n->c) || inline_local_is_reassigned(name, n->d))
        return 1;
    for (i = 0; i < n->list_len; ++i)
        if (inline_local_is_reassigned(name, n->list[i]))
            return 1;
    return 0;
}

/* Builds a new AST_COMPOUND containing body->list[1..] - the statements
 * after a leading local declaration try_scan_inline_local_decl captured -
 * so the void-body eligibility path (inline_void_stmt_body_is_simple) and
 * whatever gets stored as inline_stmt_expr/inline_stmt_body never need to
 * know a declaration preceded them at all. Sub-nodes are reused, not
 * deep-cloned, matching how inline_return_expr/inline_stmt_expr/
 * inline_stmt_body already just point into the one speculatively-built
 * body tree rather than copying out of it. */
static struct AstNode *build_body_skip_first(struct AstArena *ar, struct AstNode *body)
{
    struct AstNode *n;
    int i;

    n = ast_new(ar, AST_COMPOUND);
    for (i = 1; i < body->list_len; ++i)
        ast_list_push(ar, n, body->list[i]);
    return n;
}

/* Speculatively re-parses body->list[0] - already captured elsewhere
 * (ast_build_decl_span) as an opaque "replay this token span later"
 * marker, not a real AST subtree - as a declarator + initializer, using
 * the same building blocks (parse_base_type, ast_build_expr) ordinary
 * declaration codegen uses, but never emitting anything: this only ever
 * runs inside record_inline_function_if_simple's own already-speculative,
 * asm_suppress_depth-guarded body parse. Declines (0) on anything but
 * exactly `TYPE *name = simple-initializer;` - multiple declarators,
 * arrays, function pointers, a missing initializer, a non-pointer type, or
 * an initializer that fails inline_expr_is_simple (e.g. it references a
 * local this same speculative parse hasn't reached yet) - which is always
 * safe: the caller falls back to the function simply not becoming
 * inline-eligible, exactly as before this existed. Only pointer types are
 * accepted for now (type_size 2 on this target, matching the #itmpN slot
 * width every real parameter temp already uses) - not because a scalar
 * local would be unsound, just because no case needing one has come up
 * yet to verify against. */
static int try_scan_inline_local_decl(struct Sym *fn, struct AstNode *body)
{
    struct DeclSpanSave save;
    struct AstNode *first;
    int type;
    char name[64];
    struct AstNode *init_expr;
    int ok;
    size_t namelen;

    if (fn->proto_nargs >= MAX_PROTO_PARAMS - 1)
        return 0;
    if (body == NULL || body->kind != AST_COMPOUND || body->list_len < 2)
        return 0;
    first = body->list[0];
    if (first == NULL || first->kind != AST_DECL)
        return 0;
    if (!ast_decl_span_seek(first, &save))
        return 0;

    ok = 0;
    name[0] = 0;
    init_expr = NULL;

    type = parse_base_type();
    while (accept('*'))
        type = type_add_ptr(type);
    if (g_lex.tok.kind == TOK_ID && type_size(type) == 2) {
        strncpy(name, g_lex.tok.text, sizeof(name) - 1);
        name[sizeof(name) - 1] = 0;
        next_token();
        if (accept('=')) {
            init_expr = ast_build_expr(&g_ast_inline_arena);
            if (init_expr != NULL && g_lex.tok.kind == ';' &&
                inline_expr_is_simple(fn, init_expr) &&
                !inline_local_is_reassigned(name, body))
                ok = 1;
        }
    }

    ast_decl_span_restore(&save);

    if (!ok)
        return 0;

    namelen = strlen(name);
    if (namelen > sizeof(fn->inline_local_name) - 1)
        namelen = sizeof(fn->inline_local_name) - 1;
    memcpy(fn->inline_local_name, name, namelen);
    fn->inline_local_name[namelen] = 0;
    fn->inline_local_init = init_expr;
    fn->inline_local_type = type;
    fn->has_inline_local = 1;
    return 1;
}

static void record_inline_function_if_simple(struct Sym *s)
{
    LexState _ls;
    struct AstNode *body;
    struct AstNode *ret_expr;
    int i;
    int nparams;
    size_t namelen;
    int has_local;

    if (s == NULL || !s->is_static || !s->is_inline || g_lex.tok.kind != '{')
        return;
    if ((s->type & 15) != TYPE_VOID &&
        (!(type_size(s->type) == 1 || type_size(s->type) == 2 || type_size(s->type) == 4) ||
         type_is_struct_object(s->type)))
        return;

    nparams = 0;
    for (i = 0; i < g_frame.nlocals && nparams < MAX_PROTO_PARAMS; ++i) {
        if (locals[i].storage == SC_PARAM) {
            if (!(type_size(locals[i].type) == 1 || type_size(locals[i].type) == 2 ||
                  type_size(locals[i].type) == 4) ||
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

    _ls = lex_save();

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

    lex_restore(&_ls);
    for (i = 0; i < MAX_PROTO_PARAMS; ++i)
        s->inline_param_use_count[i] = 0;

    has_local = try_scan_inline_local_decl(s, body);

    if ((s->type & 15) == TYPE_VOID) {
        struct AstNode *void_body;
        void_body = has_local ? build_body_skip_first(&g_ast_inline_arena, body) : body;
        if (!inline_void_stmt_body_is_simple(s, void_body))
            return;
        if (void_body->list_len == 1 && void_body->list[0]->kind == AST_EXPR_STMT)
            s->inline_stmt_expr = void_body->list[0]->a;
        else
            s->inline_stmt_body = void_body;
        return;
    }

    ret_expr = inline_return_expr_from_seq(body, has_local ? 1 : 0);
    if (ret_expr == NULL)
        return;
    if (!inline_expr_is_simple(s, ret_expr))
        return;
    /* A bool-returning function normally gets its 0/1 canonicalization from
     * AST_RETURN's own codegen (mir_lower_conversion on the return value) -
     * substituting ret_expr directly at a call site bypasses that entirely,
     * so only accept it here when ret_expr already provably yields 0/1 on
     * its own (ast_expr_yields_bool01: a bool-typed subexpression, a 0/1
     * literal, `!`, a comparison, `&&`/`||`, or a cast to bool - the same
     * proof dcc_ast_gen_expr.c already trusts elsewhere for an RHS being
     * stored into a bool). Anything else (e.g. `return some_int_expr;` used
     * where C's implicit bool conversion would normally truncate/canonicalize
     * it) is declined rather than risk splicing in a non-canonical value. */
    if (type_is_bool(s->type) && !ast_expr_yields_bool01(ret_expr))
        return;

    s->inline_return_expr = ret_expr;
}

static void scan_reserve_struct_return_member_temp(void)
{
    LexState _ls;
    struct Sym *fn;
    char name[64];
    int depth;
    int bytes;

    if (g_lex.tok.kind != TOK_ID)
        return;
    fn = find_global(g_lex.tok.text);
    if (fn == NULL || fn->storage != SC_FUNC || !type_is_struct_object(fn->type))
        return;

    _ls = lex_save();

    next_token();
    if (g_lex.tok.kind != '(') {
        lex_restore(&_ls);
        return;
    }

    depth = 0;
    do {
        if (g_lex.tok.kind == TOK_EOF)
            break;
        if (g_lex.tok.kind == '(')
            depth++;
        else if (g_lex.tok.kind == ')')
            depth--;
        next_token();
    } while (depth > 0);

    /* Parentheses around the call are transparent in the AST - `(mk()).f`
     * builds the same member-on-call node as `mk().f` and allocates the same
     * temp - so skip any run of closing parens before looking for the `.`.
     * This can only OVER-reserve (e.g. `f(g(1)).x` also matches at `g`),
     * which merely pads the frame; under-reserving is what corrupts it. */
    while (g_lex.tok.kind == ')')
        next_token();

    if (g_lex.tok.kind == '.') {
        bytes = type_size(fn->type);
        if (bytes <= 0)
            bytes = 2;
        sprintf(name, "#sret%d", g_frame.nlocals);
        add_local_alloc(name, fn->type, bytes);
    }

    lex_restore(&_ls);
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
    LexState _ls;
    struct AstNode *body;
    struct AstNode *ret_expr;

    if (s == NULL || s->proto_nargs != 0 || s->proto_variadic || g_lex.tok.kind != '{')
        return;
    if ((s->type & 15) == TYPE_VOID || type_size(s->type) != 2 ||
        type_is_bool(s->type) || type_is_struct_object(s->type))
        return;

    _ls = lex_save();

    /* See the identical comment in record_inline_function_if_simple: this
     * speculatively parses the whole body before any of its own locals are
     * declared for this pass, so a reference to one can misresolve and
     * trip a false-positive diagnostic; asm_suppress_depth marks the parse
     * as inert so dcc_error_at drops it. */
    asm_suppress_depth++;
    body = ast_build_stmt(&g_ast_inline_arena);
    asm_suppress_depth--;

    lex_restore(&_ls);

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

int is_inline_substitutable(struct Sym *s)
{
    return s != NULL && s->is_static && s->is_inline &&
           (s->inline_return_expr != NULL || s->inline_stmt_expr != NULL ||
            s->inline_stmt_body != NULL);
}

static int inline_function_has_multiuse_param(struct Sym *s)
{
    int i;

    if (!is_inline_substitutable(s))
        return 0;
    for (i = 0; i < s->proto_nargs && i < MAX_PROTO_PARAMS; ++i)
        if (s->inline_param_use_count[i] > 1)
            return 1;
    return 0;
}

/* Lexically scans a call's argument list (tok positioned just after the
 * opening '(') for anything that could make emit_inline_arg_temps
 * materialize a temp under dcc_ast_gen_expr.c's conservative argument rule,
 * independent of whether the callee has a multi-use parameter - the
 * pre-existing case inline_function_has_multiuse_param covers. Only needs
 * to be a safe over-approximation, not exact: a false positive just
 * reserves unused #itmpN stack slots (see reserve_inline_temp_locals); a
 * false negative just means that one call site's emit_inline_arg_temps
 * finds no local reserved (find_local returns NULL) and quietly falls
 * back to a real, non-inlined call - a missed optimization, never a
 * miscompile, since reserving the locals is orthogonal to whether a given
 * call site's arguments actually need one. */
static int call_args_may_need_temps(void)
{
    int depth;

    depth = 1;
    while (g_lex.tok.kind != TOK_EOF && depth > 0) {
        if (g_lex.tok.kind == '(') {
            depth++;
        } else if (g_lex.tok.kind == ')') {
            depth--;
            if (depth == 0)
                break;
        } else if (g_lex.tok.kind == TOK_INC || g_lex.tok.kind == TOK_DEC || g_lex.tok.kind == '=' ||
                   (g_lex.tok.kind >= TOK_ADDEQ && g_lex.tok.kind <= TOK_SHREQ)) {
            return 1;
        } else if (g_lex.tok.kind == TOK_ID) {
            /* Block locals are not in the symbol table during this lexical
             * pre-scan, so it cannot distinguish a private automatic from a
             * global, volatile, or address-taken object. Reserve on any
             * identifier and let the AST emitter make the exact decision. */
            return 1;
        }
        next_token();
    }
    return 0;
}

static int function_body_may_need_inline_temps(void)
{
    LexState _ls;
    int depth;
    int result;

    if (g_lex.tok.kind != '{')
        return 0;

    _ls = lex_save();

    depth = 1;
    result = 0;
    next_token();
    while (g_lex.tok.kind != TOK_EOF && depth > 0) {
        if (g_lex.tok.kind == TOK_ID) {
            char name[64];
            struct Sym *s;

            strncpy(name, g_lex.tok.text, sizeof(name) - 1);
            name[sizeof(name) - 1] = 0;
            next_token();
            if (g_lex.tok.kind == '(') {
                s = find_global(name);
                if (inline_function_has_multiuse_param(s)) {
                    result = 1;
                    break;
                }
                if (is_inline_substitutable(s)) {
                    next_token(); /* consume '(', start of argument list */
                    if (call_args_may_need_temps()) {
                        result = 1;
                        break;
                    }
                    continue;
                }
            }
            continue;
        }
        if (g_lex.tok.kind == '{')
            depth++;
        else if (g_lex.tok.kind == '}')
            depth--;
        next_token();
    }

    lex_restore(&_ls);
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

/* Local-array address caching: a local array's address (`&arr`, or the
 * implicit decay when it's passed/used as a pointer) is a compile-time
 * constant offset from IX for the entire life of the function, yet every
 * reference recomputes it from scratch (push ix/pop hl/ld de,N/add hl,de -
 * see emit_load_frame_addr_hl). When an array's address is materialized
 * repeatedly - e.g. passed to two calls in the same loop iteration - that's
 * pure waste: compute it once, unconditionally, right after the prologue
 * allocates locals (so it's valid before any user statement runs, sidestepping
 * any question of which control-flow path reaches which use first), and have
 * every use site just reload the cached pointer.
 *
 * Two-step design, mirroring function_body_may_need_inline_temps():
 *   1. A read-only token scan (this function) counts every identifier's bare
 *      occurrences in the function body, without knowing yet which ones are
 *      local arrays - declarations haven't been processed. For an array,
 *      every bare occurrence except `sizeof`/`&` (which this simple count
 *      doesn't try to distinguish - overcounting only costs an unneeded
 *      cache slot, never correctness) is an address materialization, so
 *      total occurrence count is a direct, if slightly conservative, proxy
 *      for "how many times will this array's address be computed". Scalars
 *      are deliberately excluded from this optimization entirely (see the
 *      declaration hook in scan_local_decl_after_type): an ordinary scalar's
 *      name is read/written directly via cheap ix-relative access without
 *      ever calling emit_load_frame_addr_hl, so a bare occurrence count would
 *      be a poor proxy for "how many times is its address actually taken".
 *   2. When a local array's own declaration is later processed (in all three
 *      passes over the function body - two frame-sizing scans plus the real
 *      codegen pass - the pattern already used by everything else in this
 *      file), if its name's count clears the threshold, reserve a 2-byte
 *      cache slot right there via add_local_alloc (identically in all three
 *      passes, since they replay the same declarations in the same order
 *      from the same starting local_size) and record the (array offset,
 *      cache slot offset) pair in g_addr_cache_arrays so the prologue -
 *      emitted before the codegen pass re-declares anything - can still emit
 *      the eager population using the last (identical) pass's values. */
#define ADDR_CACHE_MIN_COUNT 3
#define MAX_IDENT_COUNTS 128

#define MAX_ADDR_CACHE_ARRAYS 16

struct IdentCount { char name[64]; int count; int addr_taken; };
static struct IdentCount g_ident_counts[MAX_IDENT_COUNTS];
static int g_ident_count_n;

struct AddrCacheArrayInfo { int array_offset; int cache_slot_offset; };
static struct AddrCacheArrayInfo g_addr_cache_arrays[MAX_ADDR_CACHE_ARRAYS];
static int g_addr_cache_array_count;

/* Set for the duration of the current function's three passes when its body
 * directly calls exec()/execv(). Both are hand-written RTL (DCCRTL.MAC,
 * __xmain) that computes a scratch "trampoline" region at a fixed offset (67
 * bytes) below the BDOS entry point and zero-fills it - if the calling
 * function's stack has grown deep enough to reach that region, the zero-fill
 * corrupts the caller's own live stack. This is a pre-existing fragility
 * completely independent of address-caching (confirmed empirically: adding
 * ANY unrelated 2-byte local to such a caller's frame reproduces the same
 * corruption with the optimization fully disabled) - but address-caching's
 * extra 2-byte-per-array slot is exactly the kind of frame growth that can
 * tip a marginal case over that edge, as it did for tests/texec.c. Rather
 * than fix that RTL fragility (a separate, unrelated concern), just decline
 * to grow the frame of any function that directly calls exec()/execv() at
 * all - the narrowest, safest way to avoid ever being the change that
 * triggers it. */
static int g_addr_cache_calls_exec;

static void bump_ident_count(const char *name)
{
    int i;

    for (i = 0; i < g_ident_count_n; ++i) {
        if (strcmp(g_ident_counts[i].name, name) == 0) {
            g_ident_counts[i].count++;
            return;
        }
    }
    if (g_ident_count_n < MAX_IDENT_COUNTS) {
        /* Manual bounded copy, not strncpy: name (63+NUL) is far smaller than
         * a token's text (MAX_TOK_TEXT, 512), and GCC's fortify source flags
         * that size disparity as possible truncation despite the explicit
         * terminator below. */
        size_t namelen = strlen(name);
        size_t cap = sizeof(g_ident_counts[0].name) - 1;
        if (namelen > cap) namelen = cap;
        memcpy(g_ident_counts[g_ident_count_n].name, name, namelen);
        g_ident_counts[g_ident_count_n].name[namelen] = 0;
        g_ident_counts[g_ident_count_n].count = 1;
        g_ident_counts[g_ident_count_n].addr_taken = 0;
        g_ident_count_n++;
    }
}

/* Called immediately after bump_ident_count for an identifier reached through
 * an address-of token with only parentheses in between - i.e. its address was
 * taken somewhere in the function body. MIR object promotion and the
 * remaining AST metadata/optimization probes use this conservative
 * whole-function fact. */
static void mark_ident_addr_taken(const char *name)
{
    int i;

    for (i = 0; i < g_ident_count_n; ++i) {
        if (strcmp(g_ident_counts[i].name, name) == 0) {
            g_ident_counts[i].addr_taken = 1;
            return;
        }
    }
}


static int ident_count_for(const char *name)
{
    int i;

    for (i = 0; i < g_ident_count_n; ++i)
        if (strcmp(g_ident_counts[i].name, name) == 0)
            return g_ident_counts[i].count;
    return 0;
}

static int ident_addr_taken_for(const char *name)
{
    int i;

    for (i = 0; i < g_ident_count_n; ++i)
        if (strcmp(g_ident_counts[i].name, name) == 0)
            return g_ident_counts[i].addr_taken;
    return 0;
}

int local_name_address_taken_in_function(const char *name)
{
    return ident_addr_taken_for(name);
}

/* Record (or update, on a later pass) that the array at this frame offset got
 * a cache slot. Keyed on array_offset, NOT name: two distinct arrays with the
 * same name can legitimately exist in the same function in separate,
 * non-overlapping scopes (dcc's frame storage is monotonic - see
 * leave_scope() - so they get different, permanent offsets, never reused).
 * Keying on name would conflate them, silently dropping one's entry from this
 * table when the other's got recorded later - its cache slot would then never
 * be populated by the prologue while its use sites still unconditionally read
 * from it (has_addr_cache is set independently per-Sym), reading garbage.
 * Idempotent across the three passes over the same function body for the
 * SAME instance: each pass computes identical offsets for it (array_offset
 * comes from add_local_alloc's monotonic local_size counter, deterministic
 * given identical declaration order each pass), so re-finding it and
 * overwriting with the same values is harmless. */
static void record_addr_cache_array(int array_offset, int cache_slot_offset)
{
    int i;

    for (i = 0; i < g_addr_cache_array_count; ++i) {
        if (g_addr_cache_arrays[i].array_offset == array_offset) {
            g_addr_cache_arrays[i].cache_slot_offset = cache_slot_offset;
            return;
        }
    }
    if (g_addr_cache_array_count < MAX_ADDR_CACHE_ARRAYS) {
        g_addr_cache_arrays[g_addr_cache_array_count].array_offset = array_offset;
        g_addr_cache_arrays[g_addr_cache_array_count].cache_slot_offset = cache_slot_offset;
        g_addr_cache_array_count++;
    }
}

/* Give a local array a cache slot if its name's bare occurrence count (from
 * the pre-scan below) clears the threshold. Called from the ordinary local
 * array declaration path, once per pass; add_local_alloc's own local_size
 * bookkeeping keeps the reserved slot's offset identical across all three
 * passes, exactly like every other per-declaration frame reservation in this
 * file. */
void maybe_reserve_addr_cache_for_array(struct Sym *s, const char *name)
{
    struct Sym *cache_slot;
    int would_be_offset;

    if (ident_count_for(name) < ADDR_CACHE_MIN_COUNT)
        return;
    /* See g_addr_cache_calls_exec's comment: a function that directly calls
     * exec()/execv() must not have its frame grown by this optimization at
     * all, regardless of how many arrays would otherwise qualify. */
    if (g_addr_cache_calls_exec)
        return;
    /* The cache slot is read/written via (ix+d) direct addressing (both in
     * the prologue's eager store and at every use site in
     * emit_load_frame_addr_hl), which the Z80 only encodes with a signed
     * 8-bit displacement, -128..127. A function with a large enough frame
     * can push this reservation past that range - confirmed by a real M80
     * "out of range" assembly failure on tarray6.c's large frame - so decline
     * the optimization entirely for this array rather than reserve a slot
     * that can never actually be addressed this way. local_size is the
     * running total BEFORE this reservation, matching what add_local_alloc
     * itself is about to compute (local_size += bytes; offset = -local_size). */
    would_be_offset = -(g_frame.local_size + 2);
    if (would_be_offset < -128)
        return;
    cache_slot = add_local_alloc("#addrcache", TYPE_INT, 2);
    s->has_addr_cache = 1;
    s->addr_cache_offset = cache_slot->offset;
    record_addr_cache_array(s->offset, cache_slot->offset);
}

/* Token-scan pre-pass (read-only, saves/restores lexer position exactly like
 * function_body_may_need_inline_temps): count every identifier's bare
 * occurrences in the function body, and reset the per-function address-cache
 * table for the upcoming three passes over this function; also sets
 * g_addr_cache_calls_exec (declared above) when the body directly calls
 * exec()/execv(). */
static void scan_function_body_ident_counts(void)
{
    LexState _ls;
    int depth;
    int address_pending;

    g_ident_count_n = 0;
    g_addr_cache_array_count = 0;
    g_addr_cache_calls_exec = 0;

    if (g_lex.tok.kind != '{')
        return;

    _ls = lex_save();

    depth = 1;
    address_pending = 0;
    next_token();
    while (g_lex.tok.kind != TOK_EOF && depth > 0) {
        if (g_lex.tok.kind == TOK_ID) {
            bump_ident_count(g_lex.tok.text);
            if (address_pending)
                mark_ident_addr_taken(g_lex.tok.text);
            address_pending = 0;
            if (strcmp(g_lex.tok.text, "exec") == 0 || strcmp(g_lex.tok.text, "execv") == 0)
                g_addr_cache_calls_exec = 1;
        } else if (g_lex.tok.kind == '{') {
            depth++;
        } else if (g_lex.tok.kind == '}') {
            depth--;
        }

        if (g_lex.tok.kind == '&')
            address_pending = 1;
        else if (address_pending && g_lex.tok.kind != '(' && g_lex.tok.kind != ')')
            address_pending = 0;
        next_token();
    }

    lex_restore(&_ls);
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
                fputc(c, g_emit_sink.stream);
        }
        fclose(s->deferred_body_file);
        s->deferred_body_file = NULL;
    }
}

int current_void_is_empty_param_list(void)
{
    LexState _ls;
    int r;

    if (g_lex.tok.kind != TOK_VOID)
        return 0;

    _ls = lex_save();

    next_token();
    r = (g_lex.tok.kind == ')');

    lex_restore(&_ls);

    return r;
}

void skip_prototype_array_suffixes(int *ptype)
{
    int dims[MAX_ARRAY_DIMS];
    int ndims = 0;
    int i, n, inner, elem_bytes;
    int orig_type = *ptype;
    int rt_count = 0;     /* runtime inner (ndims>0) dimensions seen */
    int rt_dim = -1;      /* dimension index of the first such */
    int rt_simple = 0;    /* first such dimension is a lone identifier `[name]` */
    char rt_name[64];

    rt_name[0] = 0;

    if (g_lex.tok.kind != '[') return;

    /* Reset: we're taking over array suffix parsing from scratch. */
    g_ptr_array_dim_count = 0;
    g_ptr_array_elem_size = 0;
    g_ptr_array_runtime_stride_name[0] = 0;
    memset(g_ptr_array_dims, 0, sizeof(g_ptr_array_dims));

    while (accept('[')) {
        skip_parameter_array_qualifiers();

        if (g_lex.tok.kind == ']') {
            n = 0;
            next_token();
        } else if (g_lex.tok.kind == '*') {
            /* C99 `[*]` unspecified-size VLA marker in a prototype; it decays
             * to a pointer exactly like `[]`. */
            next_token();
            expect(']');
            n = 0;
        } else if (array_dim_has_runtime_identifier()) {
            /* C99 variable-length-array parameter: `T p[n]` (with `n` another
             * parameter or any run-time expression) is equivalent to `T *p`.
             * The bound merely documents the length, so consume the dimension
             * expression and let the array decay to a pointer just like `[]`.
             * An inner (ndims>0) runtime dimension additionally implies a
             * run-time row stride; note it here so the representable
             * `T p[x][col]` shape (single inner bound, a lone identifier) can
             * be lowered, while any other runtime inner shape is rejected
             * below rather than silently miscompiled. */
            if (ndims > 0) {
                rt_count++;
                if (rt_dim < 0) {
                    rt_dim = ndims;
                    if (g_lex.tok.kind == TOK_ID) {
                        long s_pos = g_lex.posi;
                        long s_ts = g_lex.tok_start_pos;
                        int s_ln = g_lex.line_no;
                        int s_tl = g_lex.tok_line;
                        struct Token s_tk = g_lex.tok;
                        strncpy(rt_name, g_lex.tok.text, sizeof(rt_name) - 1);
                        rt_name[sizeof(rt_name) - 1] = 0;
                        next_token();
                        rt_simple = (g_lex.tok.kind == ']');
                        g_lex.posi = s_pos;
                        g_lex.tok_start_pos = s_ts;
                        g_lex.line_no = s_ln;
                        g_lex.tok_line = s_tl;
                        g_lex.tok = s_tk;
                    }
                }
            }
            skip_array_dim_to_close();
            n = 0;
        } else {
            n = parse_typed_array_bound_expr();
            expect(']');
        }
        if (n < 0) n = 0;
        if (ndims < MAX_ARRAY_DIMS) dims[ndims] = n;
        ndims++;
    }

    if (ndims == 0) return;

    /* A single runtime inner dimension that is a lone identifier and the only
     * inner dimension (`T p[x][col]`) is representable as a pointer to a
     * runtime-width row: keep its bound name for row-stride indexing.  Any
     * other runtime inner shape - an expression bound (`[col+1]`, `[2*n]`), a
     * three-or-more-dimensional array, or a runtime dimension followed by
     * further dimensions - cannot be described by one stride symbol, so reject
     * it rather than emit wrong element addresses. */
    if (rt_count > 0) {
        if (rt_count == 1 && rt_simple && ndims == 2 && rt_dim == 1) {
            strncpy(g_ptr_array_runtime_stride_name, rt_name,
                    sizeof(g_ptr_array_runtime_stride_name) - 1);
            g_ptr_array_runtime_stride_name[sizeof(g_ptr_array_runtime_stride_name) - 1] = 0;
        } else {
            error_here("variable inner dimensions in variable-length arrays are not supported; use malloc and an explicit pointer");
        }
    }

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
    LexState _ls;

    if (!accept('('))
        return;

    depth = 1;
    while (g_lex.tok.kind != TOK_EOF && depth > 0) {
        if (g_lex.tok.kind == '(')
            depth++;
        else if (g_lex.tok.kind == ')')
            depth--;
        next_token();
    }

    while (g_lex.tok.kind == '(')
        skip_prototype_function_suffix();

    if (g_lex.tok.kind == ')') {
        _ls = lex_save();
        next_token();
        if (g_lex.tok.kind != ',') {
            lex_restore(&_ls);
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

/* __fastcall eligibility: up to 3 parameters, each a char/short/int/pointer
 * (nothing that needs more than one 16-bit register pair or a hidden
 * pointer - long, float, struct/union by value), no varargs, no struct
 * return, and a real prototype (rules out K&R old-style parameter lists,
 * which never populate proto_types). Call after copy_parsed_prototype_to_sym
 * so proto_nargs/proto_types/proto_variadic are current. See gen_fastcall_
 * user_call in dcc_ast_gen_expr.c for the matching HL/DE/BC codegen. */
void validate_fastcall_prototype(struct Sym *s)
{
    int i;

    if (!s->is_fastcall)
        return;
    if (!s->has_proto)
        error_here("__fastcall function must have a prototyped parameter list");
    if (s->proto_variadic)
        error_here("__fastcall function cannot be variadic");
    if (s->proto_nargs > 3)
        error_here("__fastcall function may have at most 3 parameters");
    for (i = 0; i < s->proto_nargs; ++i) {
        int t = s->proto_types[i];
        if (type_is_struct_object(t) || type_is_long(t) || type_is_float(t) ||
            type_size(t) > 2)
            error_here("__fastcall parameter must be char, short, int, or a pointer");
    }
    if (type_is_struct_object(s->type))
        error_here("__fastcall function cannot return a struct or union");
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

void copy_funcptr_prototype_to_sym(struct Sym *s, int direct_declarator)
{
    int i;

    if (s == NULL || type_ptr_depth(s->type) <= 0)
        return;
    s->is_funcptr = direct_declarator || g_typedef_has_proto;
    if (direct_declarator) {
        s->has_proto = g_funcptr_has_proto;
        s->proto_nargs = g_funcptr_proto_nargs;
        s->proto_variadic = g_funcptr_proto_variadic;
        for (i = 0; i < MAX_PROTO_PARAMS; ++i)
            s->proto_types[i] = g_funcptr_proto_types[i];
    } else if (g_typedef_has_proto) {
        s->has_proto = g_typedef_has_proto;
        s->proto_nargs = g_typedef_proto_nargs;
        s->proto_variadic = g_typedef_proto_variadic;
        for (i = 0; i < MAX_PROTO_PARAMS; ++i)
            s->proto_types[i] = g_typedef_proto_types[i];
    }
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
    LexState _ls;
    int r;

    if (g_lex.tok.kind != TOK_ID || find_typedef(g_lex.tok.text) >= 0)
        return 0;

    _ls = lex_save();

    r = 1;
    for (;;) {
        if (g_lex.tok.kind != TOK_ID || find_typedef(g_lex.tok.text) >= 0) {
            r = 0;
            break;
        }
        next_token();
        if (g_lex.tok.kind == ')')
            break;
        if (g_lex.tok.kind != ',') {
            r = 0;
            break;
        }
        next_token();
    }

    lex_restore(&_ls);
    return r;
}

/* Byte offset of the first parameter from IX. IX points at the saved caller
 * IX, so the return address sits at +2 and the first argument at +4; a
 * struct-returning function has a hidden result pointer ahead of them, making
 * it +6. */
static int frame_first_param_offset(void)
{
    return ((parse_function_return_type & TYPE_STRUCT) &&
            type_ptr_depth(parse_function_return_type) == 0) ? 6 : 4;
}

static void recompute_param_offsets(void)
{
    int i;
    int off;
    int sz;

    off = frame_first_param_offset();

    for (i = 0; i < g_frame.nlocals; ++i) {
        if (locals[i].storage != SC_PARAM)
            continue;
        sz = type_size(locals[i].type);
        if (sz < 2) sz = 2;
        locals[i].offset = off;
        locals[i].size = sz;
        off += sz;
    }
    g_frame.param_offset = off;
}

void parse_old_style_param_id_list(void)
{
    char name[64];

    for (;;) {
        if (g_lex.tok.kind != TOK_ID) {
            error_here("parameter name expected");
            break;
        }
        strncpy(name, g_lex.tok.text, sizeof(name) - 1);
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
    int base_is_register;
    int base_is_volatile;
    int base_pointee_is_volatile;
    int type;
    char name[64];
    struct Sym *s;

    while (g_lex.tok.kind != TOK_EOF && g_lex.tok.kind != '{' && starts_type()) {
        base = parse_base_type();
        base_is_register = g_decl.is_register;
        base_is_volatile = g_decl.is_volatile;
        base_pointee_is_volatile = g_decl.pointee_is_volatile;

        for (;;) {
            type = base;
            g_decl.is_volatile = base_is_volatile;
            g_decl.pointee_is_volatile = base_pointee_is_volatile;
            while (accept('*')) {
                g_decl.pointee_is_volatile = g_decl.is_volatile;
                g_decl.is_volatile = skip_type_qualifiers_volatile();
                type = type_add_ptr(type);
            }

            if (g_lex.tok.kind != TOK_ID) {
                error_here("parameter declaration name expected");
                while (g_lex.tok.kind != ';' && g_lex.tok.kind != TOK_EOF && g_lex.tok.kind != '{')
                    next_token();
                break;
            }

            strncpy(name, g_lex.tok.text, sizeof(name) - 1);
            name[sizeof(name) - 1] = 0;
            next_token();

            skip_prototype_array_suffixes(&type);
            if (g_lex.tok.kind == '(') {
                skip_prototype_function_suffix();
                type = type_add_ptr(type);
            }

            s = find_local(name);
            if (!s || s->storage != SC_PARAM) {
                error_here("old-style parameter declaration does not match parameter list");
            } else {
                int pi;
                s->type = type;
                s->is_register = base_is_register;
                s->is_volatile = g_decl.is_volatile;
                s->pointee_is_volatile = g_decl.pointee_is_volatile;
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
    int direct_funcptr;
    char name[64];
    int unnamed_id;

    g_frame.nlocals = 0;
    g_frame.local_size = 0;
    g_frame.param_offset = frame_first_param_offset();
    clear_parsed_prototype();

    if (current_void_is_empty_param_list()) {
        g_proto_has = 1;
        next_token();
        return;
    }

    /* Empty parentheses in C89 mean old-style/no prototype. */
    if (g_lex.tok.kind == ')') return;

    if (old_style_param_list_starts()) {
        parse_old_style_param_id_list();
        return;
    }

    for (;;) {
        if (g_lex.tok.kind == TOK_ELLIPSIS) {
            g_proto_has = 1;
            g_proto_variadic = 1;
            next_token();
            break;
        }

        type = parse_type();
        direct_funcptr = 0;
        if (g_typedef_array_len > 0) {
            type = type_add_ptr(type);
            g_typedef_array_len = 0;
        }
        unnamed_id = 0;

        while (accept('*')) {
            g_decl.pointee_is_volatile = g_decl.is_volatile;
            g_decl.is_volatile = skip_type_qualifiers_volatile();
            type = type_add_ptr(type);
        }
        skip_type_qualifiers();

        if (parse_funcptr_declarator(&type, name, sizeof(name))) {
            direct_funcptr = 1;
        } else if (parse_abstract_funcptr_declarator(&type)) {
            sprintf(name, "__p%d", g_frame.param_offset);
            unnamed_id = 1;
        } else if (g_lex.tok.kind == TOK_ID) {
            strncpy(name, g_lex.tok.text, sizeof(name) - 1);
            name[sizeof(name) - 1] = 0;
            next_token();
        } else {
            /* Prototype declarations may omit parameter names:
             *     int f(int, char *);
             * Give such parameters private dummy names so function
             * definitions using named parameters continue to work exactly
             * as before, while header prototypes are accepted. */
            sprintf(name, "__p%d", g_frame.param_offset);
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
        if (g_lex.tok.kind == '(') {
            skip_prototype_function_suffix();
            type = type_add_ptr(type);
        }

        remember_proto_param_type(type);
        {
            struct Sym *ps;
            int pi;
            add_param_alloc(name, type);
            ps = find_local(name);
            if (ps != NULL) {
                ps->is_register = g_decl.is_register;
                ps->is_volatile = g_decl.is_volatile;
                ps->pointee_is_volatile = g_decl.pointee_is_volatile;
            }
            copy_funcptr_prototype_to_sym(ps, direct_funcptr);
            if (ps && g_ptr_array_dim_count > 0) {
                ps->elem_size = g_ptr_array_elem_size;
                ps->dim_count = g_ptr_array_dim_count;
                for (pi = 0; pi < MAX_ARRAY_DIMS; ++pi)
                    ps->dims[pi] = (pi < g_ptr_array_dim_count) ? g_ptr_array_dims[pi] : 0;
                strncpy(ps->runtime_stride_name, g_ptr_array_runtime_stride_name,
                        sizeof(ps->runtime_stride_name) - 1);
                ps->runtime_stride_name[sizeof(ps->runtime_stride_name) - 1] = 0;
            }
            g_ptr_array_dim_count = 0;
            g_ptr_array_elem_size = 0;
            g_ptr_array_runtime_stride_name[0] = 0;
        }
        (void)unnamed_id;

        if (!accept(',')) break;
    }
}

static char current_debug_function[64];
static char current_debug_function_source_name[64];
static int debug_types_emitted;

static void emit_debug_dims(const int *dims, int count)
{
    int i;
    fputc('"', g_emit_sink.stream);
    for (i = 0; i < count; ++i)
        fprintf(g_emit_sink.stream, "%s%d", i ? "," : "", dims[i]);
    fputc('"', g_emit_sink.stream);
}

void emit_debug_types_once(void)
{
    int i;
    if (!DEBUG_METADATA_ENABLED || scan_mode || debug_types_emitted)
        return;
    debug_types_emitted = 1;
    for (i = 0; i < nstruct_defs; ++i)
        fprintf(g_emit_sink.stream, ";@dcc-struct %d %d %d \"%s\"\n", i + 1,
                struct_defs[i].size, struct_defs[i].is_union,
                struct_defs[i].name);
    for (i = 0; i < nfield_defs; ++i) {
        struct FieldDef *f = &field_defs[i];
        if (f->is_promoted)
            continue;
        fprintf(g_emit_sink.stream, ";@dcc-field %d \"%s\" %d %d %d %d %d %d %d ",
                f->parent_struct_id, f->name, f->type, f->offset, f->size,
                f->is_array, f->elem_size, f->bit_width, f->bit_shift);
        emit_debug_dims(f->dims, f->dim_count);
        fputc('\n', g_emit_sink.stream);
    }
}

void emit_debug_global(struct Sym *s)
{
    if (!DEBUG_METADATA_ENABLED || scan_mode || s == NULL || s->storage == SC_FUNC ||
        s->storage == SC_EXTERN || s->name[0] == '#')
        return;
    emit_debug_types_once();
    fprintf(g_emit_sink.stream, ";@dcc-global \"%s\" \"%s\" %d %d %d %d %d %d ",
            asm_name_for(sym_asm_name(s)), s->name, s->type, s->size,
            s->is_array, s->is_vla, s->elem_size, s->is_funcptr);
    emit_debug_dims(s->dims, s->dim_count);
    fputc('\n', g_emit_sink.stream);
}

void emit_debug_variable(struct Sym *s)
{
    char variable_name[64];
    if (!DEBUG_METADATA_ENABLED || scan_mode || current_debug_function[0] == 0 || s == NULL ||
        (s->storage != SC_LOCAL && s->storage != SC_PARAM) ||
        s->name[0] == '#' || (s->is_const_value && !opt_debug_lines))
        return;
    if (mir_capture_debug_variable(current_debug_function, s, 0))
        return;
    debug_symbol_name(s, variable_name, sizeof(variable_name));
    fprintf(g_emit_sink.stream, ";@dcc-var \"%s\" \"%s\" %d %d %d %d %d %d %d %d ",
            current_debug_function, variable_name, s->type, s->storage,
            s->offset, s->size, s->is_array, s->is_vla, s->elem_size,
            s->is_funcptr);
    emit_debug_dims(s->dims, s->dim_count);
    fputc('\n', g_emit_sink.stream);
}

void emit_debug_variable_end(struct Sym *s)
{
    char variable_name[64];
    if (!DEBUG_METADATA_ENABLED || scan_mode || current_debug_function[0] == 0 || s == NULL ||
        (s->storage != SC_LOCAL && s->storage != SC_PARAM) ||
        s->name[0] == '#' || (s->is_const_value && !opt_debug_lines))
        return;
    if (mir_capture_debug_variable(current_debug_function, s, 1))
        return;
    debug_symbol_name(s, variable_name, sizeof(variable_name));
    fprintf(g_emit_sink.stream, ";@dcc-var-end \"%s\" \"%s\" %d\n",
            current_debug_function, variable_name, s->offset);
}

void begin_function_mir(const char *name, int local_bytes)
{
    struct Sym *s;
    const char *aname;
    int function_type;
    int i;

    flush_pending_asm();

    s = find_global(name);
    function_type = current_return_type != 0
        ? current_return_type : s != NULL ? s->type : TYPE_INT;
    aname = asm_name_for(name);
    emit_debug_types_once();
    strncpy(current_debug_function, aname, sizeof(current_debug_function) - 1);
    current_debug_function[sizeof(current_debug_function) - 1] = 0;
    strncpy(current_debug_function_source_name, name, sizeof(current_debug_function_source_name) - 1);
    current_debug_function_source_name[sizeof(current_debug_function_source_name) - 1] = 0;

    if (DEBUG_METADATA_ENABLED && !scan_mode)
        fprintf(g_emit_sink.stream, ";@dcc-func-begin \"%s\" \"%s\"\n",
                current_debug_function, current_debug_function_source_name);

    if (!s || !s->is_static) {
        asm_name_check_public_collision(name);
        fprintf(g_emit_sink.stream, "\n\tpublic %s\n", aname);
    } else {
        /* File-scope static functions are mangled to avoid M80/L80 short-name
         * collisions.  Emit the original C spelling beside the generated label
         * so .mac listings remain readable during debugging. */
        fprintf(g_emit_sink.stream, "\n; static function %s\n", name);
    }

    fprintf(g_emit_sink.stream, "%s:\n", aname);
    mir_begin_function(
        name, aname, g_emit_sink.purpose, current_function_has_vla, local_bytes,
        strcmp(name, "main") == 0 &&
            (function_type & 15) == TYPE_INT &&
            type_ptr_depth(function_type) == 0);
    for (i = 0; i < g_frame.nlocals; ++i)
        if (locals[i].storage == SC_PARAM)
            emit_debug_variable(&locals[i]);
}

void finish_function_mir(int implicit_zero_return)
{
    (void)implicit_zero_return;
    if (opt_debug_lines) {
        mir_end_function();
        if (!scan_mode && g_func_close_line > 0)
            ast_record_debug_location(g_func_close_file, g_func_close_line);
        g_func_close_line = 0;
        if (!scan_mode && current_debug_function[0])
            fprintf(g_emit_sink.stream, ";@dcc-func-end \"%s\" \"%s\"\n",
                    current_debug_function, current_debug_function_source_name);
        current_debug_function[0] = 0;
        current_debug_function_source_name[0] = 0;
        flush_pending_asm();
        return;
    }
    /* Map the shared return label to the function's closing brace when the
     * body always exits, so an early `return` that jumps here shows the
     * closing brace instead of inheriting the previous statement's line. */
    if (DEBUG_METADATA_ENABLED && !scan_mode && g_func_close_line > 0)
        ast_record_debug_location(g_func_close_file, g_func_close_line);
    g_func_close_line = 0;
    if (DEBUG_METADATA_ENABLED && !scan_mode && current_debug_function[0] &&
        !mir_capture_debug_function_end(
            current_debug_function, current_debug_function_source_name))
        fprintf(g_emit_sink.stream, ";@dcc-func-end \"%s\" \"%s\"\n",
                current_debug_function, current_debug_function_source_name);
    mir_end_function();
    current_debug_function[0] = 0;
    current_debug_function_source_name[0] = 0;
    flush_pending_asm();
}

void skip_initializer_or_decl_tail(void)
{
    int depth;

    depth = 0;

    while (g_lex.tok.kind != TOK_EOF) {
        if (depth == 0 && (g_lex.tok.kind == ',' || g_lex.tok.kind == ';')) return;

        if (g_lex.tok.kind == '(' || g_lex.tok.kind == '[' || g_lex.tok.kind == '{') depth++;
        else if (g_lex.tok.kind == ')' || g_lex.tok.kind == ']' || g_lex.tok.kind == '}') {
            if (depth > 0) depth--;
        }

        next_token();
    }
}

static int scan_compound_literal_if_present(void)
{
    LexState _ls;
    int save_long_suffix;
    int save_unsigned_suffix;
    int type;
    int size;
    int depth;

    if (g_lex.tok.kind != '(' || !paren_starts_cast())
        return 0;

    _ls = lex_save();
    save_long_suffix = g_tok_long_suffix;
    save_unsigned_suffix = g_tok_unsigned_suffix;

    depth = 1;
    next_token();
    while (g_lex.tok.kind != TOK_EOF && depth > 0) {
        if (g_lex.tok.kind == '(')
            depth++;
        else if (g_lex.tok.kind == ')')
            depth--;
        next_token();
    }

    if (g_lex.tok.kind != '{') {
        lex_restore(&_ls);
        g_tok_long_suffix = save_long_suffix;
        g_tok_unsigned_suffix = save_unsigned_suffix;
        return 0;
    }

    lex_restore(&_ls);
    g_tok_long_suffix = save_long_suffix;
    g_tok_unsigned_suffix = save_unsigned_suffix;

    next_token();
    parse_type_name_decl(&type, &size);
    expect(')');

    if (g_lex.tok.kind != '{') {
        lex_restore(&_ls);
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
        if (g_lex.tok.kind == TOK_EOF)
            break;
        if (depth >= 1 && g_lex.tok.kind == '(' && scan_compound_literal_if_present())
            continue;
        /* Non-constant fields are re-parsed at emit time through
         * ast_emit_init_expr, whose AST build allocates a hidden temp for a
         * struct-return call member base (`mk(...).f`); reserve the same
         * slot here so the scan-derived frame size matches. */
        if (depth >= 1 && g_lex.tok.kind == TOK_ID)
            scan_reserve_struct_return_member_temp();
        if (g_lex.tok.kind == '{')
            depth++;
        else if (g_lex.tok.kind == '}')
            depth--;
        next_token();
    } while (depth > 0);

    return 1;
}

static void scan_initializer_or_decl_tail(void)
{
    int depth;

    depth = 0;

    while (g_lex.tok.kind != TOK_EOF) {
        if (depth == 0 && (g_lex.tok.kind == ',' || g_lex.tok.kind == ';')) return;

        if (g_lex.tok.kind == '(' && scan_compound_literal_if_present())
            continue;

        /* A struct-return call member access `mk(...).field` inside a
         * declaration initializer reserves a hidden temp during codegen's
         * AST build (ast_add_struct_return_member_temp); reserve the same
         * slot here so the scan-derived frame size matches, exactly as the
         * statement-level else-branch in scan_function_body does. */
        if (g_lex.tok.kind == TOK_ID)
            scan_reserve_struct_return_member_temp();

        if (g_lex.tok.kind == '(' || g_lex.tok.kind == '[' || g_lex.tok.kind == '{') depth++;
        else if (g_lex.tok.kind == ')' || g_lex.tok.kind == ']' || g_lex.tok.kind == '}') {
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
    p = g_lex.posi;
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
    LexState _ls;
    int depth;
    int prev_was_member_access;
    int found;

    _ls = lex_save();

    depth = 0;
    found = 0;
    prev_was_member_access = 0;
    while (g_lex.tok.kind != TOK_EOF) {
        if (g_lex.tok.kind == '{') {
            depth++;
        } else if (g_lex.tok.kind == '}') {
            if (depth == 0)
                break;
            depth--;
        } else if (g_lex.tok.kind == TOK_ID && !strcmp(g_lex.tok.text, name)) {
            if (!prev_was_member_access) {
                found = 1;
                break;
            }
        }
        prev_was_member_access = (g_lex.tok.kind == '.' || g_lex.tok.kind == TOK_ARROW);
        next_token();
    }

    lex_restore(&_ls);
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
    while (g_lex.tok.kind != TOK_EOF) {
        if (depth == 0 && g_lex.tok.kind == ';') {
            next_token();
            return 1;
        }
        if (depth == 0 && g_lex.tok.kind == '=')
            return 0;
        if (g_lex.tok.kind == '(' || g_lex.tok.kind == '[' || g_lex.tok.kind == '{')
            depth++;
        else if (g_lex.tok.kind == ')' || g_lex.tok.kind == ']' || g_lex.tok.kind == '}') {
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
        if (g_lex.tok.kind == '}' || g_lex.tok.kind == TOK_EOF)
            return seq;
        if (starts_type() && g_lex.tok.kind != TOK_TYPEDEF) {
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

/*
 * Snapshot of the per-function speculative-parse state that the narrow-analysis
 * probes below must leave unchanged. Each probe runs a throwaway forward parse
 * (narrow_build_speculative_scope -> ast_build_stmt) that mutates for-scope
 * sequencing, label, block-scope-depth, compound-literal/LICM sequence, and
 * declaration-qualifier state; capturing and restoring them as one unit keeps
 * every field in lockstep so a newly added piece of speculative state cannot be
 * forgotten at one of the (identical) restore sites. The lexer cursor is a
 * separate concern captured by lex_save()/lex_restore().
 */
typedef struct SpecParseState {
    int nulabels;
    int for_seq;
    int forren_n;
    int for_decl_seq;
    int for_decl_rename_index;
    int for_decl_recording;
    int scope_depth;
    int block_seq;
    int compound_literal_seq;
    int licm_seq;
    int decl_is_volatile;
    int decl_pointee_is_volatile;
} SpecParseState;

static SpecParseState spec_parse_save(void)
{
    SpecParseState s;
    s.nulabels = nulabels;
    s.for_seq = g_func_pass.for_seq;
    s.forren_n = g_func_pass.forren_n;
    s.for_decl_seq = g_func_pass.for_decl_seq;
    s.for_decl_rename_index = g_func_pass.for_decl_rename_index;
    s.for_decl_recording = g_func_pass.for_decl_recording;
    s.scope_depth = g_func_pass.scope_depth;
    s.block_seq = g_func_pass.block_seq;
    s.compound_literal_seq = g_func_pass.compound_literal_seq;
    s.licm_seq = g_func_pass.licm_seq;
    s.decl_is_volatile = g_decl.is_volatile;
    s.decl_pointee_is_volatile = g_decl.pointee_is_volatile;
    return s;
}

static void spec_parse_restore(const SpecParseState *s)
{
    nulabels = s->nulabels;
    g_func_pass.for_seq = s->for_seq;
    g_func_pass.forren_n = s->forren_n;
    g_func_pass.for_decl_seq = s->for_decl_seq;
    g_func_pass.for_decl_rename_index = s->for_decl_rename_index;
    g_func_pass.for_decl_recording = s->for_decl_recording;
    g_func_pass.scope_depth = s->scope_depth;
    g_func_pass.block_seq = s->block_seq;
    g_func_pass.compound_literal_seq = s->compound_literal_seq;
    g_func_pass.licm_seq = s->licm_seq;
    g_decl.is_volatile = s->decl_is_volatile;
    g_decl.pointee_is_volatile = s->decl_pointee_is_volatile;
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
    LexState _ls;
    SpecParseState _sp;
    static struct AstArena narrow_scratch_arena;
    static int narrow_scratch_inited;
    struct AstNode *seq;
    int result;

    if (opt_no_narrow)
        return 0;
    if ((type & 15) != TYPE_INT || type_ptr_depth(type) != 0 || type_is_struct_object(type) ||
        (arrlen <= 0 && total_elems <= 0) || g_lex.tok.kind == '=' || g_last_array_dim_count > 1)
        return 0;

    if (!narrow_scratch_inited) {
        ast_arena_init(&narrow_scratch_arena);
        narrow_scratch_inited = 1;
    }
    ast_arena_reset(&narrow_scratch_arena);

    _ls = lex_save();
    _sp = spec_parse_save();

    /* Same rationale as record_narrow_return_expr_if_simple: this walks
     * forward through code whose later declarations (if any follow) have
     * not been (re-)entered into the symbol table for this pass, so a
     * reference to one can misresolve and trip a false-positive diagnostic;
     * asm_suppress_depth marks the parse as inert so dcc_error_at drops it. */
    asm_suppress_depth++;
    seq = narrow_build_speculative_scope(&narrow_scratch_arena);
    asm_suppress_depth--;
    result = (seq != NULL) ? narrow_array_is_byte_safe(seq, name) : 0;

    lex_restore(&_ls);
    spec_parse_restore(&_sp);

    return result;
}

/* Scalar counterpart of try_narrow_local_int_array: proves a plain
 * register-qualified int local's own value (not an array's elements) is
 * always in [0,255], so its storage can narrow to unsigned char - e.g.
 * e.c's `register int n`, which this same engine already has to bound
 * anyway as a dependency of proving `a[]` narrow-safe (n is a %-divisor).
 * is_register is captured by the caller (from g_decl.is_register) rather
 * than read here, since nothing this function calls is expected to touch
 * that global, but relying on a value already in hand is more robust than
 * re-reading a global after a speculative parse.
 *
 * Tried relaxing this to any plain int local (not just register-qualified)
 * to narrow tests/00040.c's loop counter `i`: the regression suite
 * immediately caught real problems that is_register had incidentally been
 * shielding, not just scope-limiting.
 *   1. tfloat4 silently truncated an unrelated ~60000-valued `unsigned ui`
 *      to a byte - narrow_member_needs_bound (dcc_array_narrow.c) never
 *      required checking the narrowing TARGET's own bound unless some
 *      other write also depended on it. Fixed by making it unconditional
 *      (every group member's writes are checked, matching this file's own
 *      header comment) - kept, only makes the existing path stricter.
 *   2. tpromo32 failed to compile outright ("unsupported AST statement").
 *      Root cause: a dependency (e.g. `u16 = e;`) pulled in by name via
 *      narrow_collect_deps is trusted as bounded with NO write ever
 *      checked when that name was already declared BEFORE the speculative
 *      scan's own starting point (e's `int32_t e = 123456L;` initializer,
 *      declared earlier in the same function, lies outside the
 *      forward-only scan and is invisible to it) - a vacuously "verified"
 *      dependency. Fixed in narrow_is_byte_safe_impl: decline outright if
 *      a newly discovered dependency name already resolves via
 *      find_local() (i.e. was declared before this scan began). Kept.
 *   3. Even with #1 and #2 fixed, a broader regression-suite run still
 *      showed 12 failures, including tests/a1.c (the 6502 emulator test)
 *      hanging outright. This turned out to be a THIRD, unrelated bug -
 *      not in either narrowing proof at all, but in gen_assign_ast
 *      (dcc_ast_gen_expr.c): assigning a constant to a byte-sized ix-direct
 *      local (`byteVar = K;`) took a fast path that stored the byte
 *      directly and returned WITHOUT ever leaving the (possibly
 *      sign/zero-extended) value in HL - fine when the assignment's own
 *      result is unused (the overwhelmingly common case), but wrong when
 *      it's a subexpression of an enclosing one, e.g. exactly
 *      tests/00040.c's `for (r=i=0; ...)` once `i` narrows to a byte: HL
 *      still held unrelated leftover register contents from the frame
 *      setup, and that leaked into `r`. This bug is completely general -
 *      reproduced identically with the plain `register` keyword too - and
 *      was simply never exercised before, since narrowing a byte-sized
 *      scalar used inside a chained assignment was rare. Fixed by emitting
 *      a value reload (emit_load_sym_value_direct) after the store,
 *      whenever expr_result_dead is false. This resolved #3 (a1 and the
 *      rest of the 12 all pass now) with no further fallout found across
 *      the full fast/nopeep/extended-C89/extended-C99 suites.
 * Given all three are understood and fixed, try_narrow_for_counter below
 * takes the narrower, purpose-built path the investigation converged on
 * (see its own comment in dcc_array_narrow.c) rather than reusing this
 * function's general dependency-closure machinery for non-register locals -
 * this function's own trigger stays register-gated. */
int try_narrow_register_scalar(const char *name, int type, int is_register,
                               int arrlen, int total_elems)
{
    LexState _ls;
    SpecParseState _sp;
    static struct AstArena narrow_scalar_scratch_arena;
    static int narrow_scalar_scratch_inited;
    struct AstNode *seq;
    int result;

    if (opt_no_narrow)
        return 0;
    if (!is_register || (type & 15) != TYPE_INT || type_ptr_depth(type) != 0 ||
        type_is_struct_object(type) || arrlen > 0 || total_elems > 0 || g_lex.tok.kind == '=')
        return 0;

    if (!narrow_scalar_scratch_inited) {
        ast_arena_init(&narrow_scalar_scratch_arena);
        narrow_scalar_scratch_inited = 1;
    }
    ast_arena_reset(&narrow_scalar_scratch_arena);

    _ls = lex_save();
    _sp = spec_parse_save();

    asm_suppress_depth++;
    seq = narrow_build_speculative_scope(&narrow_scalar_scratch_arena);
    asm_suppress_depth--;
    result = (seq != NULL) ? narrow_scalar_is_byte_safe(seq, name) : 0;

    lex_restore(&_ls);
    spec_parse_restore(&_sp);

    return result;
}

/* Third narrowing trigger, independent of both of the above: proves a plain
 * int local (register-qualified or not - unlike try_narrow_register_scalar,
 * this does not require the keyword) is used solely as one simple counting
 * for-loop's own induction variable, via narrow_for_counter_is_byte_safe's
 * self-contained structural match (dcc_array_narrow.c) rather than the
 * general dependency-closure proof. Motivated by tests/00040.c's
 * `for (r=i=0; i<8; i++)`, after the general "narrow any plain scalar"
 * relaxation attempt kept surfacing new soundness gaps (see the long
 * comment on try_narrow_register_scalar above) - this is deliberately much
 * smaller in scope than that attempt, so it carries none of that risk. */
int try_narrow_for_counter(const char *name, int type, int arrlen, int total_elems)
{
    LexState _ls;
    SpecParseState _sp;
    static struct AstArena narrow_for_scratch_arena;
    static int narrow_for_scratch_inited;
    struct AstNode *seq;
    int result;

    if (opt_no_narrow)
        return 0;
    if ((type & 15) != TYPE_INT || type_ptr_depth(type) != 0 ||
        type_is_struct_object(type) || arrlen > 0 || total_elems > 0 || g_lex.tok.kind == '=')
        return 0;

    if (!narrow_for_scratch_inited) {
        ast_arena_init(&narrow_for_scratch_arena);
        narrow_for_scratch_inited = 1;
    }
    ast_arena_reset(&narrow_for_scratch_arena);

    _ls = lex_save();
    _sp = spec_parse_save();

    asm_suppress_depth++;
    seq = narrow_build_speculative_scope(&narrow_for_scratch_arena);
    asm_suppress_depth--;
    result = (seq != NULL) ? narrow_for_counter_is_byte_safe(seq, name) : 0;

    lex_restore(&_ls);
    spec_parse_restore(&_sp);

    return result;
}

void scan_local_decl_after_type(int base)
{
    int type, bytes, arrlen;
    int base_is_volatile;
    int base_pointee_is_volatile;
    int total_elems;
    int direct_funcptr;
    char name[64];
    char source_name[64];
    struct Sym *s;

    base_is_volatile = g_decl.is_volatile;
    base_pointee_is_volatile = g_decl.pointee_is_volatile;

    for (;;) {
        type = base;
        g_decl.is_volatile = base_is_volatile;
        g_decl.pointee_is_volatile = base_pointee_is_volatile;
        direct_funcptr = 0;

        while (accept('*')) {
            g_decl.pointee_is_volatile = g_decl.is_volatile;
            g_decl.is_volatile = skip_type_qualifiers_volatile();
            type = type_add_ptr(type);
        }

        if (parse_funcptr_declarator(&type, name, sizeof(name))) {
            direct_funcptr = 1;
        } else {
            if (g_lex.tok.kind != TOK_ID) return;

            strncpy(name, g_lex.tok.text, sizeof(name) - 1);
            name[sizeof(name) - 1] = 0;
            next_token();
        }
        strncpy(source_name, name, sizeof(source_name) - 1);
        source_name[sizeof(source_name) - 1] = 0;

        if (g_lex.tok.kind == '(') {
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
        } else if (g_last_array_dim_count > 0 && g_typedef_array_len > 0 &&
                   g_last_array_dim_count < MAX_ARRAY_DIMS) {
            /* `ARR2 table[2]` composes the typedef's own array length as an
             * extra trailing dimension - see the identical composition in
             * parse_function_or_global (dcc_func.c) and
             * gen_local_decl_after_type (dcc_decl.c), which must reach the
             * same conclusion for the codegen pass. */
            g_last_array_dims[g_last_array_dim_count++] = g_typedef_array_len;
            if (target_size_multiply(arrlen, g_typedef_array_len, &arrlen))
                total_elems = arrlen;
            else
                error_here("object size exceeds 16-bit address space");
            /* current_field_array_elem_size (-> Sym.elem_size) was computed by
             * parse_array_declarator_dims from only this declarator's OWN
             * dims, before the typedef's dimension was appended above; redo
             * it now as the stride across every dim past the first, or the
             * first-dimension row stride stays too narrow (miscomputed as the
             * bare element size instead of the whole row) even though
             * dim_count/dims are now correct. */
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

        if (!g_decl.is_volatile &&
            try_narrow_local_int_array(name, type, arrlen, total_elems)) {
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
        } else if (!g_decl.is_volatile &&
                   try_narrow_register_scalar(name, type, g_decl.is_register, arrlen, total_elems)) {
            type = (type & ~15) | TYPE_CHAR | TYPE_UNSIGNED;
        } else if (!g_decl.is_volatile &&
                   try_narrow_for_counter(name, type, arrlen, total_elems)) {
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
            copy_funcptr_prototype_to_sym(s, direct_funcptr);
            s->is_volatile = g_decl.is_volatile;
            s->pointee_is_volatile = g_decl.pointee_is_volatile;
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
                } else {
                    /* A VLA's slot holds a runtime pointer, not a fixed
                     * address (see emit_load_sym_addr's is_vla branch), so
                     * the address-caching optimization below - which assumes
                     * the array's address never changes for the life of the
                     * function - only applies to ordinary fixed arrays. */
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
            g_frame.nlocals--;
            g_frame.local_size -= bytes;
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
    int base_is_volatile;
    int base_pointee_is_volatile;
    char name[64];
    char source_name[64];
    char backing_name[64];
    struct Sym *g;
    struct Sym *l;

    base_is_volatile = g_decl.is_volatile;
    base_pointee_is_volatile = g_decl.pointee_is_volatile;

    for (;;) {
        type = base;
        g_decl.is_volatile = base_is_volatile;
        g_decl.pointee_is_volatile = base_pointee_is_volatile;

        while (accept('*')) {
            g_decl.pointee_is_volatile = g_decl.is_volatile;
            g_decl.is_volatile = skip_type_qualifiers_volatile();
            type = type_add_ptr(type);
        }

        if (g_lex.tok.kind != TOK_ID) return;

        strncpy(name, g_lex.tok.text, sizeof(name) - 1);
        name[sizeof(name) - 1] = 0;
        strncpy(source_name, name, sizeof(source_name) - 1);
        source_name[sizeof(source_name) - 1] = 0;
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
            error_here("variable length array declaration cannot have static storage duration");
            g_vla_pending = 0;
            arrlen = 0;
        }
        if (arrlen == 0 && g_typedef_array_len > 0)
            arrlen = g_typedef_array_len;
        else if (g_last_array_dim_count > 0 && g_typedef_array_len > 0 &&
                 g_last_array_dim_count < MAX_ARRAY_DIMS) {
            /* `ARR2 table[2]` composes the typedef's own array length as an
             * extra trailing dimension - see the identical composition in
             * parse_function_or_global and scan_local_decl_after_type. */
            g_last_array_dims[g_last_array_dim_count++] = g_typedef_array_len;
            if (!target_size_multiply(arrlen, g_typedef_array_len, &arrlen))
                error_here("object size exceeds 16-bit address space");
            /* current_field_array_elem_size (-> Sym.elem_size) reflected only
             * this declarator's own dims; redo it as the stride across every
             * dim past the first now that the typedef's dimension is appended
             * - see the identical fixup in scan_local_decl_after_type. */
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

        bytes = type_size(type);
        if (arrlen > 0)
            bytes = object_array_size(type, arrlen);
        else if (g_last_array_dim_count > 0)
            bytes = 0;
        else if (arrlen < 0)
            bytes = 0;

        l = find_local_decl(name);
        if (l && l->link_name[0]) {
            strncpy(backing_name, l->link_name, sizeof(backing_name) - 1);
            backing_name[sizeof(backing_name) - 1] = 0;
        } else {
            sprintf(backing_name, "__sl%d_%d", g_func_pass.static_local_func_index,
                    g_func_pass.static_local_seq++);
        }

        {
            const char *renamed = enter_static_local_rename(
                source_name, backing_name);
            strncpy(name, renamed, sizeof(name) - 1);
            name[sizeof(name) - 1] = 0;
            l = find_local_decl(name);
        }

        g = add_global(backing_name, type, SC_GLOBAL);
        g->is_defined = 1;
        g->needs_extrn = 0;
        g->is_static = 1;
        g->is_volatile = g_decl.is_volatile;
        g->pointee_is_volatile = g_decl.pointee_is_volatile;
        g->size = bytes;
        if (arrlen != 0 || g_last_array_dim_count > 0) {
            g->is_array = 1;
            g->array_len = arrlen > 0 ? arrlen : 0;
            g->elem_size = current_field_array_elem_size ? current_field_array_elem_size : type_size(type);
            if (g->elem_size <= 0) g->elem_size = 2;
            copy_last_array_dims_to_sym(g);
        }

        if (!l) {
            l = add_local_known(name, type, SC_GLOBAL, 0, bytes);
            l->is_volatile = g_decl.is_volatile;
            l->pointee_is_volatile = g_decl.pointee_is_volatile;
            strncpy(l->link_name, backing_name, sizeof(l->link_name) - 1);
            l->link_name[sizeof(l->link_name) - 1] = 0;
            if (arrlen != 0 || g_last_array_dim_count > 0) {
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
        if (l != NULL) {
            mir_note_declared_symbol(l);
            mir_note_declared_alias(source_name, l);
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
    g_func_pass.for_seq = 0;
    g_func_pass.forren_n = 0;
    g_func_pass.for_decl_seq = -1;
    g_func_pass.for_decl_rename_index = 0;
    g_func_pass.for_decl_recording = 0;
    g_func_pass.scope_depth = 0;
    g_func_pass.block_seq = 0;
    g_func_pass.compound_literal_seq = 0;
    g_func_pass.licm_seq = 0;
    g_vla_fwd_ngoto = 0;

    expect('{');
    enter_scope();              /* function body block */
    brace = 1;
    can_decl = 1;

    while (g_lex.tok.kind != TOK_EOF && brace > 0) {
        if (g_lex.tok.kind == '{') {
            ast_scan_for_stmt();
            can_decl = 1;
        } else if (g_lex.tok.kind == '}') {
            brace--;
            next_token();
            leave_scope();
            can_decl = 1;
        } else if (g_lex.tok.kind == TOK_FOR || g_lex.tok.kind == TOK_WHILE ||
                   g_lex.tok.kind == TOK_DO ||
                   g_lex.tok.kind == TOK_IF || g_lex.tok.kind == TOK_SWITCH) {
            /*
             * Build and replay the whole statement (header + body) through
             * the AST builder/emitter (ast_scan_for_stmt, output suppressed)
             * instead of hand-walking tokens. This is the exact same
             * builder+emitter the real codegen pass uses, so frame sizing -
             * declarations inside the body, C99 for-init renaming, any
             * AST-level for-loop fast path that reserves extra frame space,
             * and (originally for-only, now also reachable from while/do/if
             * bodies) ast_divmod_fuse_compound's #dmq/#dmr temps - stays in
             * sync with the real pass by construction, rather than needing a
             * hand-written parallel scanner kept in sync by hand. (That
             * hand-written scanner used to live here; see git history for
             * its final form and the cast-vs-declaration bug it once had to
             * work around - both are now moot since this runs the real
             * parser instead of guessing at token shapes.)
             *
             * Bare compounds and while/do/if/switch were added alongside for
             * once ast_divmod_fuse_compound (dcc_ast_gen_support.c) proved
             * that a non-loop-
             * specific AST_COMPOUND hoist can synthesize new frame locals
             * from ANY of these bodies, not just a for-loop's - see
             * tests/e.c's own `while(--n) { a[n]=x%n; x=10*a[n-1]+x/n; }`,
             * which motivated this pass and is not itself inside a for loop.
             * ast_try_emit_statement (the real pass's per-statement
             * dispatcher) treats ast_stmt_supported()==false as a hard
             * compile error for every statement kind uniformly, not just
             * for-loops - so any program that reaches real codegen without
             * a diagnostic is guaranteed to have every top-level compound,
             * while/do/if/switch pass the identical ast_stmt_supported() check
             * this scan uses, meaning this extension can never newly desync
             * from the real pass on already-compiling input.
             *
             * A 0 return (AST build declined) is left alone: it only happens
             * for malformed/unsupported input that the real pass will report
             * with a proper diagnostic anyway.
             */
            ast_scan_for_stmt();
            can_decl = 1;
        } else if (can_decl && g_lex.tok.kind == TOK_STATIC_ASSERT) {
            parse_static_assert_decl();
            can_decl = 1;
        } else if (can_decl && g_lex.tok.kind == TOK_TYPEDEF) {
            parse_typedef_decl();
            can_decl = 1;
        } else if (can_decl && starts_type()) {
            int t;
            int is_static_local;
            g_decl.is_extern = 0;
            g_decl.is_static = 0;
            g_decl.is_inline = 0;
            g_decl.is_noreturn = 0;
            g_decl.is_const = 0;
            is_static_local = (g_lex.tok.kind == TOK_STATIC);
            t = parse_base_type();
            if (g_lex.tok.kind == ';') {
                next_token();
            } else if (is_static_local) {
                scan_static_local_decl_after_type(t);
            } else {
                scan_local_decl_after_type(t);
            }
            can_decl = 1;
        } else {
            int k;
            if (g_lex.tok.kind == '(' && scan_compound_literal_if_present()) {
                can_decl = 0;
                continue;
            }
            k = g_lex.tok.kind;
            if (k == TOK_ID) {
                scan_reserve_struct_return_member_temp();
                next_token();
                if (g_lex.tok.kind == '(')
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
    int base_is_volatile;
    int base_pointee_is_volatile;
    int done;

    expect(TOK_TYPEDEF);

    /* Parse C89 typedef declarator lists with per-declarator pointer and
     * suffix handling:
     *     typedef unsigned long UL, *PUL;
     *     typedef int A4[4], FN(int), (*PF)(int);
     */
    base_type = parse_base_type();
    base_is_volatile = g_decl.is_volatile;
    base_pointee_is_volatile = g_decl.pointee_is_volatile;
    done = 0;

    while (!done && g_lex.tok.kind != TOK_EOF) {
        int type;
        int typedef_array_len;
        int is_func;
        int is_volatile;
        int pointee_is_volatile;
        char name[64];

        type = base_type;
        typedef_array_len = 0;
        is_func = 0;
        is_volatile = base_is_volatile;
        pointee_is_volatile = base_pointee_is_volatile;
        name[0] = 0;

        while (accept('*')) {
            pointee_is_volatile = is_volatile;
            is_volatile = skip_type_qualifiers_volatile();
            type = type_add_ptr(type);
        }

        if (parse_funcptr_declarator(&type, name, sizeof(name))) {
            /* Parenthesized function-pointer typedef. */
            is_volatile = g_decl.is_volatile;
            pointee_is_volatile = g_decl.pointee_is_volatile;
        } else {
            if (g_lex.tok.kind != TOK_ID) {
                error_here("identifier expected in typedef");
                while (g_lex.tok.kind != ';' && g_lex.tok.kind != TOK_EOF) next_token();
                expect(';');
                return;
            }
            strncpy(name, g_lex.tok.text, sizeof(name) - 1);
            name[sizeof(name) - 1] = 0;
            next_token();
        }

        if (g_lex.tok.kind == '[') {
            next_token();
            if (g_lex.tok.kind == ']') {
                typedef_array_len = 0;
                next_token();
            } else {
                typedef_array_len = parse_typed_array_bound_expr();
                expect(']');
            }
            /* Multidimensional array typedefs (typedef T A[2][3]) collapse to a
             * flat element count: fold every inner dimension into the total so
             * sizeof(A) is element_size * product-of-dims, not just the first
             * dimension.  A typedef tracks only a single total length, so the
             * product is the correct flattened size. */
            while (g_lex.tok.kind == '[') {
                next_token();
                if (g_lex.tok.kind != ']') {
                    int inner = parse_typed_array_bound_expr();
                    if (typedef_array_len > 0 && inner > 0)
                        typedef_array_len *= inner;
                }
                expect(']');
            }
        } else if (g_lex.tok.kind == '(') {
            skip_prototype_function_suffix();
            is_func = (type_ptr_depth(type) == 0);
        }

        add_typedef_name_ex(name, type, typedef_array_len, is_func,
                    is_volatile, pointee_is_volatile);

        if (accept(','))
            continue;
        expect(';');
        done = 1;
    }
}

void parse_function_or_global(int base_type)
{
    int done;
    int base_is_volatile;
    int base_pointee_is_volatile;

    done = 0;
    base_is_volatile = g_decl.is_volatile;
    base_pointee_is_volatile = g_decl.pointee_is_volatile;

    while (!done && g_lex.tok.kind != TOK_EOF) {
        int type;
        char name[64];
        int arrlen;
        struct Sym *s;
        LexState _le;
        LexState _ls;
        int saved_nlocals;
        int saved_local_size;
        int saved_param_offset;
        int saved_nenum_consts;
        int saved_nulabels;
        int saved_stack_check;

        int base_is_func_typedef;
        int is_funcret_funcptr_decl;
        int direct_funcptr_decl;
        int object_is_volatile;
        int pointee_is_volatile;

        type = base_type;
        object_is_volatile = base_is_volatile;
        pointee_is_volatile = base_pointee_is_volatile;
        base_is_func_typedef = g_typedef_is_func;
        is_funcret_funcptr_decl = 0;
        direct_funcptr_decl = 0;
        name[0] = 0;
        /* __fastcall is per-declarator (like MSVC placement, between the
         * return type/pointers and the function name), not a shared
         * declaration specifier like static/inline/extern above - reset it
         * fresh for each comma-separated declarator so `int foo(int),
         * __fastcall bar(int);` doesn't leak __fastcall onto foo. */
        g_decl.is_fastcall = 0;

        /* Each declarator starts again from the shared declaration-specifier
         * base type.  This is the important C declarator rule for forms like:
         *     int *a, b, c[10];
         * where only a is a pointer. */
        while (accept('*')) {
            pointee_is_volatile = object_is_volatile;
            object_is_volatile = skip_type_qualifiers_volatile();
            type = type_add_ptr(type);
            base_is_func_typedef = 0;
        }

        /* MSVC placement: RETTYPE __fastcall name(...), after any pointers
         * and before the declarator name - not a storage-class-style prefix
         * specifier, so it is not accepted in parse_base_type. */
        if (g_lex.tok.kind == TOK_FASTCALL) {
            g_decl.is_fastcall = 1;
            next_token();
        }

        if (parse_funcptr_declarator(&type, name, sizeof(name))) {
            direct_funcptr_decl = 1;
            object_is_volatile = g_decl.is_volatile;
            pointee_is_volatile = g_decl.pointee_is_volatile;
        } else {
            if (g_lex.tok.kind != TOK_ID) {
                error_here("identifier expected");
                while (g_lex.tok.kind != ';' && g_lex.tok.kind != TOK_EOF) next_token();
                expect(';');
                return;
            }

            strncpy(name, g_lex.tok.text, sizeof(name) - 1);
            name[sizeof(name) - 1] = 0;
            next_token();
        }

        if (g_funcptr_is_funcret_decl) {
            g_funcptr_is_funcret_decl = 0;
            is_funcret_funcptr_decl = 1;
        }

        /* A typedef-name that denotes a function type can declare a function
         *     typedef int fn_t(int);
         *     extern fn_t foo;
         * Treat this as a function declaration.  Pointer declarators such as
         * fn_t *fp have already cleared base_is_func_typedef above. */
        if (base_is_func_typedef && g_funcptr_decl_array_len == 0) {
            s = add_global(name, type, SC_FUNC);
            s->is_inline |= g_decl.is_inline;
            s->is_noreturn |= g_decl.is_noreturn;
            parse_function_return_type = type;
            if (g_decl.is_static) {
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
            /* Unlike is_inline (an optimization hint dcc tolerates picking up
             * from any one declaration), a __fastcall mismatch between
             * declarations is a real ABI disagreement between call sites
             * compiled against each one - so it's checked, not OR'd in,
             * once a prototype already exists for this symbol. */
            if (s->has_proto && s->is_fastcall != g_decl.is_fastcall)
                error_here("__fastcall specifier does not match previous declaration");
            s->is_inline |= g_decl.is_inline;
            s->is_noreturn |= g_decl.is_noreturn;
            s->is_fastcall |= g_decl.is_fastcall;
            parse_function_return_type = type;
            if (g_ptr_array_dim_count > 0) {
                int pi;
                s->elem_size = g_ptr_array_elem_size;
                s->dim_count = g_ptr_array_dim_count;
                for (pi = 0; pi < MAX_ARRAY_DIMS; ++pi)
                    s->dims[pi] = (pi < g_ptr_array_dim_count) ? g_ptr_array_dims[pi] : 0;
                g_ptr_array_dim_count = 0;
                g_ptr_array_elem_size = 0;
            }
            if (g_decl.is_static) {
                s->is_static = 1;
                s->needs_extrn = 0;
            }
            if (!is_funcret_funcptr_decl)
                parse_param_list();
            copy_parsed_prototype_to_sym(s);
            validate_fastcall_prototype(s);
            if (!is_funcret_funcptr_decl)
                expect(')');

            /* Snapshot nlocals after prototype params are registered but before
             * K&R declarations: used to detect main() with no parameters. */
            int pre_params_nlocals = g_frame.nlocals;

            if (!g_proto_has && g_lex.tok.kind != '{' && g_lex.tok.kind != ';' && g_lex.tok.kind != ',')
                parse_old_style_param_declarations();

            if (g_lex.tok.kind == '{') {
                /* Phase 1 of __fastcall: dcc does not yet generate a
                 * register-argument-aware prologue for a compiled C body, so
                 * a __fastcall function must be declared extern and defined
                 * out of line as hand-written #asm reading HL/DE/BC directly
                 * (see _get_mem in tests/a1.c for the established pattern
                 * this mirrors - an extern prototype with the real body in a
                 * freestanding #asm block, never seen here as a '{' body). */
                if (s->is_fastcall)
                    error_here("__fastcall function bodies are not yet supported; "
                               "declare it extern and define it as a hand-written #asm block");
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
                if (function_body_may_need_inline_temps())
                    reserve_inline_temp_locals();
                scan_function_body_ident_counts();
                opt_stack_check = saved_stack_check;

                _ls = lex_save();
                saved_nlocals = g_frame.nlocals;
                saved_local_size = g_frame.local_size;
                saved_param_offset = g_frame.param_offset;
                saved_nenum_consts = nenum_consts;
                saved_nulabels = nulabels;

                current_return_type = type;
                current_function_has_call = 0;
                current_function_has_vla = 0;
                g_func_pass.static_local_func_index = (int)(s - globals);
                g_func_pass.static_local_seq = 0;
                asm_suppress_depth++;
                scan_function_body();
                asm_suppress_depth--;
                _le = lex_save();
                current_local_bytes = g_frame.local_size;
                if (current_local_bytes > max_function_local_bytes)
                    max_function_local_bytes = current_local_bytes;

                lex_restore(&_ls);
                g_frame.nlocals = saved_nlocals;
                g_frame.local_size = saved_local_size;
                g_frame.param_offset = saved_param_offset;
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

                g_func_pass.static_local_func_index = (int)(s - globals);
                g_func_pass.static_local_seq = 0;
                asm_suppress_depth++;
                scan_function_body();
                asm_suppress_depth--;

                lex_restore(&_ls);
                nenum_consts = saved_nenum_consts;
                opt_stack_check = saved_stack_check;

                s->is_defined = 1;
                s->needs_extrn = 0;
                s->stack_check_enabled = saved_stack_check;

                nulabels = 0;
                (void)new_label();
                current_return_type = type;
                /* Restart the for-loop counter for the codegen pass so it
                 * lines up with the frame-sizing scan. */
                g_func_pass.for_seq = 0;
                g_func_pass.forren_n = 0;
                g_func_pass.for_decl_seq = -1;
                g_func_pass.for_decl_rename_index = 0;
                g_func_pass.for_decl_recording = 0;
                /* Codegen rebuilds the local table exactly as the frame-sizing
                 * scan did - block scopes truncate nlocals as they close - so
                 * restart from just the parameters with an empty scope stack.
                 * Both passes therefore assign identical frame offsets. */
                g_frame.nlocals = saved_nlocals;
                g_frame.local_size = saved_local_size;
                g_func_pass.scope_depth = 0;
                g_func_pass.block_seq = 0;
                g_func_pass.static_local_func_index = (int)(s - globals);
                g_func_pass.static_local_seq = 0;
                g_func_pass.compound_literal_seq = 0;
                g_func_pass.licm_seq = 0;
                opt_stack_check = s->stack_check_enabled;
                if (static_inline_body_can_be_buffered(s) ||
                    plain_static_body_can_be_buffered(s, name)) {
                    EmitSink saved_sink;

                    s->deferred_body_file = tmpfile();
                    if (s->deferred_body_file == NULL)
                        fatal("cannot create deferred body temp file");
                    saved_sink = g_emit_sink;
                    g_emit_sink.stream = s->deferred_body_file;
                    g_emit_sink.purpose = EMIT_SINK_DEFERRED;
                    g_inline_body_buffering++;
                    g_buffering_epoch++;
                    begin_function_mir(name, current_local_bytes);
                    process_compound();
                    check_undefined_user_labels();
                    finish_function_mir(0);
                    g_inline_body_buffering--;
                    g_emit_sink = saved_sink;
                } else {
                    begin_function_mir(name, current_local_bytes);
                    process_compound();
                    check_undefined_user_labels();
                    finish_function_mir(strcmp(name, "main") == 0 &&
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
                    g_main_has_args = has_args;
                    fprintf(g_emit_sink.stream, "\n\tpublic __mrun\n");
                    if (has_args) {
                        fprintf(g_emit_sink.stream, "\textrn __build_argv\n");
                        fprintf(g_emit_sink.stream, "\textrn __argc\n");
                        fprintf(g_emit_sink.stream, "__mrun:\n");
                        fprintf(g_emit_sink.stream, "\tcall __build_argv\n");
                        fprintf(g_emit_sink.stream, "\tld hl,__bsse\n");
                        fprintf(g_emit_sink.stream, "\tpush hl\n");
                        fprintf(g_emit_sink.stream, "\tld hl,(__argc)\n");
                        fprintf(g_emit_sink.stream, "\tpush hl\n");
                        fprintf(g_emit_sink.stream, "\tcall _main\n");
                        fprintf(g_emit_sink.stream, "\tpop de\n");
                        fprintf(g_emit_sink.stream, "\tpop de\n");
                    } else {
                        fprintf(g_emit_sink.stream, "__mrun:\n");
                        fprintf(g_emit_sink.stream, "\tcall _main\n");
                    }
                    fprintf(g_emit_sink.stream, "\tret\n");
                }

                lex_restore(&_le);
                return;
            }

            /*
             * C89: a file-scope function declaration has external linkage even
             * without the 'extern' keyword.  Record it as a possible external,
             * but the M80 EXTRN is emitted only if actually referenced and not
             * later defined in this translation unit.
             */
            if (g_decl.is_static) {
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
            int object_size;

            for (i = 0; i < MAX_ARRAY_DIMS; ++i)
                dims[i] = 0;
            if (base_size <= 0)
                base_size = 2;

            if (g_funcptr_decl_array_len > 0) {
                total_count = g_funcptr_decl_array_len;
                dim_count = 1;
                dims[0] = g_funcptr_decl_array_len;
            }
            g_funcptr_decl_array_len = 0;

            while (g_lex.tok.kind == '[') {
                int d;
                next_token();
                if (g_lex.tok.kind == ']') {
                    d = 0;
                    next_token();
                } else {
                    d = parse_typed_array_bound_expr();
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
                    if (!target_size_multiply(total_count, dims[i], &total_count)) {
                        error_here("object size exceeds 16-bit address space");
                        break;
                    }
                }
            }

            arrlen = first_dim;
            if (arrlen == 0 && dim_count == 0 && g_typedef_array_len > 0) {
                arrlen = g_typedef_array_len;
                first_dim = g_typedef_array_len;
                total_count = g_typedef_array_len;
                dim_count = 1;
                dims[0] = g_typedef_array_len;
            } else if (dim_count > 0 && g_typedef_array_len > 0 &&
                       dim_count < MAX_ARRAY_DIMS) {
                /* `ARR2 table[2]` where `typedef T ARR2[N]` composes the
                 * typedef's own array length as an extra trailing dimension,
                 * so the declaration behaves exactly like `T table[2][N]` (a
                 * typedef name is just another spelling of its type - C89
                 * 6.5.6).  Without this the typedef's dimension is silently
                 * dropped and only this declarator's own `[2]` survives. */
                dims[dim_count++] = g_typedef_array_len;
                if (!target_size_multiply(total_count, g_typedef_array_len, &total_count)) {
                    error_here("object size exceeds 16-bit address space");
                    total_count = 0;
                }
            }

            inner_count = 1;
            if (dim_count > 1) {
                for (i = 1; i < dim_count; ++i) {
                    if (dims[i] <= 0) {
                        inner_count = 1;
                        break;
                    }
                    if (!target_size_multiply(inner_count, dims[i], &inner_count))
                        break;
                }
            }

            object_size = 0;
            if (total_count > 0 &&
                !target_size_multiply(total_count, base_size, &object_size)) {
                error_here("object size exceeds 16-bit address space");
                total_count = 0;
            }

            if (g_decl.is_extern) {
                int already_declared = (find_global(name) != NULL);
                s = add_global(name, type, SC_EXTERN);
                s->is_volatile = object_is_volatile;
                s->pointee_is_volatile = pointee_is_volatile;
                copy_funcptr_prototype_to_sym(s, direct_funcptr_decl);
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
            copy_funcptr_prototype_to_sym(s, direct_funcptr_decl);
            if (s->storage == SC_EXTERN)
                s->storage = SC_GLOBAL;
            s->is_defined = 1;
            s->needs_extrn = 0;
            s->is_volatile = object_is_volatile;
            s->pointee_is_volatile = pointee_is_volatile;
            if (g_decl.is_static)
                s->is_static = 1;

            if (dim_count > 0 || arrlen || total_count == 0) {
                s->is_array = 1;
                s->array_len = arrlen;
                s->dim_count = dim_count;
                for (i = 0; i < MAX_ARRAY_DIMS; ++i)
                    s->dims[i] = (i < dim_count) ? dims[i] : 0;

                if (dim_count > 1) {
                    if (!target_size_multiply(inner_count, base_size, &s->elem_size))
                        s->elem_size = 0;
                } else
                    s->elem_size = base_size;
                if (s->elem_size <= 0) s->elem_size = 2;

                if (total_count > 0)
                    s->size = object_size;
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

    while (g_lex.tok.kind != TOK_EOF) {
        if (g_lex.tok.kind == TOK_STATIC_ASSERT) {
            parse_static_assert_decl();
        } else if (g_lex.tok.kind == TOK_TYPEDEF) {
            parse_typedef_decl();
        } else if (starts_type()) {
            int t;
            g_decl.is_extern = 0;
            g_decl.is_static = 0;
            g_decl.is_inline = 0;
            g_decl.is_noreturn = 0;
            g_decl.is_const = 0;
            t = parse_type();
            if (g_lex.tok.kind == ';') {
                next_token();
            } else {
                parse_function_or_global(t);
            }
        } else if (g_lex.tok.kind == TOK_ID && is_unsupported_target_type_name(g_lex.tok.text)) {
            int t;
            g_decl.is_extern = 0;
            g_decl.is_static = 0;
            g_decl.is_inline = 0;
            g_decl.is_noreturn = 0;
            g_decl.is_const = 0;
            t = parse_type();
            if (g_lex.tok.kind == ';')
                next_token();
            else
                parse_function_or_global(t);
        } else if (g_lex.tok.kind == TOK_ID) {
            /* C89: implicit int return type for function definition/declaration. */
            g_decl.is_extern = 0;
            g_decl.is_static = 0;
            g_decl.is_inline = 0;
            g_decl.is_noreturn = 0;
            g_decl.is_const = 0;
            parse_function_or_global(TYPE_INT);
        } else {
            error_here("external declaration expected");
            next_token();
        }
    }
}
