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

#ifndef _WIN32
/* fileno()/ftruncate() (used by emit_function_epilogue's dead-tail-jump
 * elision) are POSIX, not ISO C89, so -std=c89 hides their declarations in
 * <stdio.h>/<unistd.h> unless a POSIX feature-test macro is visible before
 * those headers are first included - which happens via dcc.h below, so this
 * must come first. */
#define _POSIX_C_SOURCE 200809L
#endif

#include "dcc.h"
#include "dcc_ast.h"
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

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
    if (tok.kind == TOK_ID && type_size(type) == 2) {
        strncpy(name, tok.text, sizeof(name) - 1);
        name[sizeof(name) - 1] = 0;
        next_token();
        if (accept('=')) {
            init_expr = ast_build_expr(&g_ast_inline_arena);
            if (init_expr != NULL && tok.kind == ';' &&
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
    int has_local;

    if (s == NULL || !s->is_static || !s->is_inline || tok.kind != '{')
        return;
    if ((s->type & 15) != TYPE_VOID &&
        (!(type_size(s->type) == 1 || type_size(s->type) == 2 || type_size(s->type) == 4) ||
         type_is_bool(s->type) || type_is_struct_object(s->type)))
        return;

    nparams = 0;
    for (i = 0; i < nlocals && nparams < MAX_PROTO_PARAMS; ++i) {
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

    s->inline_return_expr = ret_expr;
}

static void scan_reserve_struct_return_member_temp(void)
{
    long sv_pos;
    long sv_tok_start;
    int sv_line;
    int sv_tok_line;
    struct Token sv_tok;
    struct Sym *fn;
    char name[64];
    int depth;
    int bytes;

    if (tok.kind != TOK_ID)
        return;
    fn = find_global(tok.text);
    if (fn == NULL || fn->storage != SC_FUNC || !type_is_struct_object(fn->type))
        return;

    sv_pos = posi;
    sv_tok_start = tok_start_pos;
    sv_line = line_no;
    sv_tok_line = tok_line;
    sv_tok = tok;

    next_token();
    if (tok.kind != '(') {
        posi = sv_pos;
        tok_start_pos = sv_tok_start;
        line_no = sv_line;
        tok_line = sv_tok_line;
        tok = sv_tok;
        return;
    }

    depth = 0;
    do {
        if (tok.kind == TOK_EOF)
            break;
        if (tok.kind == '(')
            depth++;
        else if (tok.kind == ')')
            depth--;
        next_token();
    } while (depth > 0);

    /* Parentheses around the call are transparent in the AST - `(mk()).f`
     * builds the same member-on-call node as `mk().f` and allocates the same
     * temp - so skip any run of closing parens before looking for the `.`.
     * This can only OVER-reserve (e.g. `f(g(1)).x` also matches at `g`),
     * which merely pads the frame; under-reserving is what corrupts it. */
    while (tok.kind == ')')
        next_token();

    if (tok.kind == '.') {
        bytes = type_size(fn->type);
        if (bytes <= 0)
            bytes = 2;
        sprintf(name, "#sret%d", nlocals);
        add_local_alloc(name, fn->type, bytes);
    }

    posi = sv_pos;
    tok_start_pos = sv_tok_start;
    line_no = sv_line;
    tok_line = sv_tok_line;
    tok = sv_tok;
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
    while (tok.kind != TOK_EOF && depth > 0) {
        if (tok.kind == '(') {
            depth++;
        } else if (tok.kind == ')') {
            depth--;
            if (depth == 0)
                break;
        } else if (tok.kind == TOK_INC || tok.kind == TOK_DEC || tok.kind == '=' ||
                   (tok.kind >= TOK_ADDEQ && tok.kind <= TOK_SHREQ)) {
            return 1;
        } else if (tok.kind == TOK_ID) {
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

struct IdentCount { char name[64]; int count; int addr_taken; int written; };
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
         * that size disparity on strncpy as a possible truncation-without-
         * termination bug even though the following assignment already
         * null-terminates. snprintf would sidestep the warning but is C99,
         * and this project builds its own source as strict C89
         * (build-dcc.sh's default CFLAGS: -std=c89). */
        size_t namelen = strlen(name);
        size_t cap = sizeof(g_ident_counts[0].name) - 1;
        if (namelen > cap) namelen = cap;
        memcpy(g_ident_counts[g_ident_count_n].name, name, namelen);
        g_ident_counts[g_ident_count_n].name[namelen] = 0;
        g_ident_counts[g_ident_count_n].count = 1;
        g_ident_counts[g_ident_count_n].addr_taken = 0;
        g_ident_counts[g_ident_count_n].written = 0;
        g_ident_count_n++;
    }
}

/* Called immediately after bump_ident_count for an identifier reached through
 * an address-of token with only parentheses in between - i.e. its address was
 * taken somewhere in the function body. Used by find_bc_regalloc_candidate to
 * exclude a pointer parameter whose own storage location (not what it
 * points to) might be read/written through that address - a BC-resident
 * copy would silently desync from such an alias. */
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

/* Called when an assignment-like operator ('=', +=/-=/etc., ++, --) is seen
 * immediately following this identifier - i.e. it is written to somewhere in
 * the function body. find_bc_regalloc_candidate restricts round 1 to
 * read-only pointer parameters (only ever indexed/dereferenced/compared,
 * never reassigned), so codegen this round only needs a load-into-BC entry
 * point, never a store-into-BC path. */
static void mark_ident_written(const char *name)
{
    int i;

    for (i = 0; i < g_ident_count_n; ++i) {
        if (strcmp(g_ident_counts[i].name, name) == 0) {
            g_ident_counts[i].written = 1;
            return;
        }
    }
}

static int ident_written_for(const char *name)
{
    int i;

    for (i = 0; i < g_ident_count_n; ++i)
        if (strcmp(g_ident_counts[i].name, name) == 0)
            return g_ident_counts[i].written;
    return 0;
}

static int tok_kind_is_write_op(int kind)
{
    return kind == '=' || (kind >= TOK_INC && kind <= TOK_SHREQ);
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
    would_be_offset = -(local_size + 2);
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
    long sv_pos;
    long sv_tok_start;
    int sv_line;
    int sv_tok_line;
    struct Token sv_tok;
    int depth;
    int prev_kind;
    int address_pending;
    char prev_ident[64];

    g_ident_count_n = 0;
    g_addr_cache_array_count = 0;
    g_addr_cache_calls_exec = 0;

    if (tok.kind != '{')
        return;

    sv_pos = posi;
    sv_tok_start = tok_start_pos;
    sv_line = line_no;
    sv_tok_line = tok_line;
    sv_tok = tok;

    depth = 1;
    prev_kind = 0;
    address_pending = 0;
    prev_ident[0] = 0;
    next_token();
    while (tok.kind != TOK_EOF && depth > 0) {
        if (tok.kind == TOK_ID) {
            bump_ident_count(tok.text);
            if (address_pending)
                mark_ident_addr_taken(tok.text);
            address_pending = 0;
            /* Prefix ++/-- ("++x", not "x++") mutates x exactly like the
             * postfix form the write-op check below already catches - but
             * that check only ever looks BACKWARD (identifier immediately
             * FOLLOWED BY a write-op token), so a parameter mutated only via
             * prefix form was never marked written at all, falsely passing
             * find_bc_regalloc_candidate's "never written" eligibility bar.
             * Confirmed as a real miscompile: a whole-function candidate
             * mutated only via "++p" kept every later read correct (emit_
             * incdec_sym_direct's REG_BC branch updates bc directly), but
             * regalloc_buffer_finalize's reload-repair - built entirely on
             * the assumption a "never written" candidate's own frame slot
             * is a permanent, always-valid shadow copy - reloaded from that
             * now-stale, pre-increment slot the moment anything made bc look
             * untrusted, silently reverting the pointer mid-loop. */
            if (prev_kind == TOK_INC || prev_kind == TOK_DEC)
                mark_ident_written(tok.text);
            if (strcmp(tok.text, "exec") == 0 || strcmp(tok.text, "execv") == 0)
                g_addr_cache_calls_exec = 1;
        } else if (tok.kind == '{')
            depth++;
        else if (tok.kind == '}')
            depth--;
        else if (tok_kind_is_write_op(tok.kind) && prev_kind == TOK_ID && prev_ident[0])
            mark_ident_written(prev_ident);
        if (tok.kind == TOK_ID) {
            size_t pl = strlen(tok.text);
            if (pl > sizeof(prev_ident) - 1) pl = sizeof(prev_ident) - 1;
            memcpy(prev_ident, tok.text, pl);
            prev_ident[pl] = 0;
        } else {
            prev_ident[0] = 0;
        }
        if (tok.kind == '&')
            address_pending = 1;
        else if (address_pending && tok.kind != '(' && tok.kind != ')')
            address_pending = 0;
        prev_kind = tok.kind;
        next_token();
    }

    posi = sv_pos;
    tok_start_pos = sv_tok_start;
    line_no = sv_line;
    tok_line = sv_tok_line;
    tok = sv_tok;
}

/* Round-1 BC register-residency candidate selection: the most-referenced
 * plain 16-bit parameter (pointer or scalar int/unsigned - anything that
 * fits in a register pair and isn't a struct/long/float, matching exactly
 * the "plain 16-bit operand" gate ast_cmp_operand_ok in dcc_ast_gen_cond.c
 * already uses for its own fast comparison path) referenced at least twice
 * in the function body, whose address is never taken. Originally
 * pointer-only; generalized once it became clear every codegen hook this
 * relies on (emit_load_sym_value_direct, gen_ident's reg_alloc check,
 * sym_can_ix_direct's universal reg_alloc bail) treats bc's contents as an
 * opaque 16-bit value and never cared whether it was semantically a
 * pointer - the only pointer-specific hook (gen_index_addr_ast's indexing
 * branch) simply never fires for a non-pointer, which is fine. A `long`
 * parameter (4 bytes) does not fit in bc and is out of scope here - it
 * would need a materially different two-register-pair design.
 *
 * Also considers global/extern word variables, ranked in the SAME pool as
 * parameters (ties favor the parameter, since it's scanned first) - unlike
 * dcc_loop_regalloc.c's loop-scoped mechanism, no separate whole-file
 * address-taken proof is strictly required here: this candidate is only
 * ever acted on when function_qualifies_for_speculative_regalloc's own
 * current_function_has_call==0 gate holds AND regalloc_buffer_finalize's
 * buf_has_unsafe_call independently confirms zero calls (beyond the seven
 * DCCRTL.MAC-contracted runtime helpers) appear anywhere in the generated
 * function body - so nothing else in this single-threaded, non-interrupt-
 * driven program can possibly execute while this function runs, and the
 * only way the global's value could change during that span is a direct
 * write from this SAME function's own text, which ident_written_for
 * already catches (same whole-function lexical scan used for parameters).
 * The whole-file address-taken check is still applied anyway, not because
 * it's load-bearing here the way it is for the loop-scoped mechanism, but
 * because it's cheap, already validated, and removes any need to re-argue
 * the "could an alias reach this from somewhere still-reachable" question
 * for every future change to what counts as a tolerated call.
 *
 * Deliberately restricted to parameters, not locals declared inside the
 * body - a parameter's Sym is added exactly once to locals[] and persists
 * unchanged (same struct instance) across every scan/codegen pass over
 * this function, whereas a body-local's Sym is freshly reallocated at the
 * same offset but as a different struct instance on each pass; carrying
 * reg_alloc across that reallocation would need plumbing this round
 * doesn't build. `params_end` is nlocals right after parameters are
 * registered but before any body-local declaration - exactly the range
 * parse_param_list/parse_old_style_param_declarations populate. */
#define BC_REGALLOC_MIN_REFS 2
static struct Sym *find_bc_regalloc_candidate(int params_end)
{
    int i;
    struct Sym *best;
    int best_count;

    /* Rank by reference count instead of returning the first eligible
     * parameter in declaration order - a function with two qualifying
     * parameters used to always get the textually-first one, even when a
     * later one was referenced far more often. Ties keep the
     * earlier-declared parameter (strict '>', not '>='), matching
     * loop_regalloc_find_bc_candidate's own tie-breaking convention. */
    best = NULL;
    best_count = 0;
    for (i = 0; i < params_end; ++i) {
        struct Sym *p = &locals[i];
        int count;

        if (p->storage != SC_PARAM) continue;
        if (p->is_array) continue;
        if (type_is_struct_object(p->type) || type_is_long(p->type) || type_is_float(p->type)) continue;
        if (type_size(p->type) != 2) continue;
        count = ident_count_for(p->name);
        if (count < BC_REGALLOC_MIN_REFS) continue;
        if (count <= best_count) continue;
        if (p->is_volatile) continue;
        if (ident_addr_taken_for(p->name)) continue;
        if (ident_written_for(p->name)) continue;
        best = p;
        best_count = count;
    }
    for (i = 0; i < nglobals; ++i) {
        struct Sym *g = &globals[i];
        int count;

        if (!is_global_word_sym(g)) continue;
        if (g->is_volatile) continue;
        if (g->reg_alloc != REG_NONE) continue;
        if (type_is_struct_object(g->type) || type_is_long(g->type) || type_is_float(g->type)) continue;
        count = ident_count_for(g->name);
        if (count < BC_REGALLOC_MIN_REFS) continue;
        if (count <= best_count) continue;
        if (global_text_addr_taken_count(g->name) != 0) continue;
        if (ident_written_for(g->name)) continue;
        best = g;
        best_count = count;
    }
    return best;
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
    int rt_count = 0;     /* runtime inner (ndims>0) dimensions seen */
    int rt_dim = -1;      /* dimension index of the first such */
    int rt_simple = 0;    /* first such dimension is a lone identifier `[name]` */
    char rt_name[64];

    rt_name[0] = 0;

    if (tok.kind != '[') return;

    /* Reset: we're taking over array suffix parsing from scratch. */
    g_ptr_array_dim_count = 0;
    g_ptr_array_elem_size = 0;
    g_ptr_array_runtime_stride_name[0] = 0;
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
                    if (tok.kind == TOK_ID) {
                        long s_pos = posi;
                        long s_ts = tok_start_pos;
                        int s_ln = line_no;
                        int s_tl = tok_line;
                        struct Token s_tk = tok;
                        strncpy(rt_name, tok.text, sizeof(rt_name) - 1);
                        rt_name[sizeof(rt_name) - 1] = 0;
                        next_token();
                        rt_simple = (tok.kind == ']');
                        posi = s_pos;
                        tok_start_pos = s_ts;
                        line_no = s_ln;
                        tok_line = s_tl;
                        tok = s_tk;
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
    int base_is_volatile;
    int base_pointee_is_volatile;
    int type;
    char name[64];
    struct Sym *s;

    while (tok.kind != TOK_EOF && tok.kind != '{' && starts_type()) {
        base = parse_base_type();
        base_is_volatile = decl_is_volatile;
        base_pointee_is_volatile = decl_pointee_is_volatile;

        for (;;) {
            type = base;
            decl_is_volatile = base_is_volatile;
            decl_pointee_is_volatile = base_pointee_is_volatile;
            while (accept('*')) {
                decl_pointee_is_volatile = decl_is_volatile;
                decl_is_volatile = skip_type_qualifiers_volatile();
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
                s->is_volatile = decl_is_volatile;
                s->pointee_is_volatile = decl_pointee_is_volatile;
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
        direct_funcptr = 0;
        if (g_typedef_array_len > 0) {
            type = type_add_ptr(type);
            g_typedef_array_len = 0;
        }
        unnamed_id = 0;

        while (accept('*')) {
            decl_pointee_is_volatile = decl_is_volatile;
            decl_is_volatile = skip_type_qualifiers_volatile();
            type = type_add_ptr(type);
        }
        skip_type_qualifiers();

        if (parse_funcptr_declarator(&type, name, sizeof(name))) {
            direct_funcptr = 1;
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
            if (ps != NULL) {
                ps->is_volatile = decl_is_volatile;
                ps->pointee_is_volatile = decl_pointee_is_volatile;
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

static char current_debug_function[64];
static char current_debug_function_source_name[64];
static int debug_types_emitted;

static void emit_debug_dims(const int *dims, int count)
{
    int i;
    fputc('"', outf);
    for (i = 0; i < count; ++i)
        fprintf(outf, "%s%d", i ? "," : "", dims[i]);
    fputc('"', outf);
}

void emit_debug_types_once(void)
{
    int i;
    if (!opt_debug || scan_mode || debug_types_emitted)
        return;
    debug_types_emitted = 1;
    for (i = 0; i < nstruct_defs; ++i)
        fprintf(outf, ";@dcc-struct %d %d %d \"%s\"\n", i + 1,
                struct_defs[i].size, struct_defs[i].is_union,
                struct_defs[i].name);
    for (i = 0; i < nfield_defs; ++i) {
        struct FieldDef *f = &field_defs[i];
        if (f->is_promoted)
            continue;
        fprintf(outf, ";@dcc-field %d \"%s\" %d %d %d %d %d %d %d ",
                f->parent_struct_id, f->name, f->type, f->offset, f->size,
                f->is_array, f->elem_size, f->bit_width, f->bit_shift);
        emit_debug_dims(f->dims, f->dim_count);
        fputc('\n', outf);
    }
}

void emit_debug_global(struct Sym *s)
{
    if (!opt_debug || scan_mode || s == NULL || s->storage == SC_FUNC ||
        s->storage == SC_EXTERN || s->name[0] == '#')
        return;
    emit_debug_types_once();
    fprintf(outf, ";@dcc-global \"%s\" \"%s\" %d %d %d %d %d %d ",
            asm_name_for(sym_asm_name(s)), s->name, s->type, s->size,
            s->is_array, s->is_vla, s->elem_size, s->is_funcptr);
    emit_debug_dims(s->dims, s->dim_count);
    fputc('\n', outf);
}

void emit_debug_variable(struct Sym *s)
{
    if (!opt_debug || scan_mode || current_debug_function[0] == 0 || s == NULL ||
        s->name[0] == '#' || s->reg_alloc != REG_NONE)
        return;
    fprintf(outf, ";@dcc-var \"%s\" \"%s\" %d %d %d %d %d %d %d %d ",
            current_debug_function, s->name, s->type, s->storage,
            s->offset, s->size, s->is_array, s->is_vla, s->elem_size,
            s->is_funcptr);
    emit_debug_dims(s->dims, s->dim_count);
    fputc('\n', outf);
}

void emit_debug_variable_end(struct Sym *s)
{
    if (!opt_debug || scan_mode || current_debug_function[0] == 0 || s == NULL ||
        s->name[0] == '#' || s->reg_alloc != REG_NONE)
        return;
    fprintf(outf, ";@dcc-var-end \"%s\" \"%s\" %d\n",
            current_debug_function, s->name, s->offset);
}

void emit_function_prologue(const char *name, int local_bytes, int omit_ix_frame)
{
    struct Sym *s;
    const char *aname;
    int i;

    flush_pending_asm();

    s = find_global(name);
    aname = asm_name_for(name);
    emit_debug_types_once();
    strncpy(current_debug_function, aname, sizeof(current_debug_function) - 1);
    current_debug_function[sizeof(current_debug_function) - 1] = 0;
    strncpy(current_debug_function_source_name, name, sizeof(current_debug_function_source_name) - 1);
    current_debug_function_source_name[sizeof(current_debug_function_source_name) - 1] = 0;

    if (opt_debug && !scan_mode)
        fprintf(outf, ";@dcc-func-begin \"%s\" \"%s\"\n",
                current_debug_function, current_debug_function_source_name);

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

    for (i = 0; i < nlocals; ++i)
        if (locals[i].storage == SC_PARAM)
            emit_debug_variable(&locals[i]);

    /* -fstack-check: after the frame (saved IX + locals) is allocated, verify
     * the stack has not grown past its reserve into the heap region.  Emitted
     * last so dccpeep's shared-frame-stub pass can still fold the prologue
     * (the call follows the recognised push-ix/locals sequence). */
    if (opt_stack_check)
        emit_runtime_call("__stchk");

    /* Load a BC-resident parameter or global's value exactly once here,
     * right after the frame is established but before any user statement
     * runs - the same "materialize once at entry, dominates every use"
     * placement as the address-cache block just below. g_bc_regalloc_sym is
     * only ever set by try_speculative_bc_regalloc_function_body for the
     * duration of one speculative generation attempt. Prime text must stay
     * in exact lockstep with bc_regalloc_entry_lines, which regalloc_
     * buffer_finalize uses to recognize/reinsert this same text - see
     * try_loop_regalloc_bc's (dcc_loop_regalloc.c) identical comment on why
     * a global needs a 3-instruction sequence instead of a parameter's 2. */
    if (!omit_ix_frame && g_bc_regalloc_sym != NULL) {
        if (g_bc_regalloc_sym->storage == SC_GLOBAL || g_bc_regalloc_sym->storage == SC_EXTERN) {
            emit_extrn_if_needed(g_bc_regalloc_sym);
            fprintf(outf, ";@dcc-regalloc-bc-prime\n");
            fprintf(outf, "\tld hl,(%s)\n", asm_name_for(sym_asm_name(g_bc_regalloc_sym)));
            fprintf(outf, "\tld c,l\n");
            fprintf(outf, "\tld b,h\n");
        } else {
            fprintf(outf, "\tld c,(ix%+d)\n", g_bc_regalloc_sym->offset);
            fprintf(outf, "\tld b,(ix%+d)\n", g_bc_regalloc_sym->offset + 1);
        }
    }

    /* Materialize any address-cached local arrays' addresses exactly once,
     * unconditionally, here - after the recognised prologue sequence above
     * (so as not to disturb dccpeep's shared-frame-stub folding of it) but
     * before any user statement runs. Function entry trivially dominates
     * every use site, so this is always safe regardless of which control-flow
     * path a given call takes - see maybe_reserve_addr_cache_for_array's
     * comment for why a naive "cache at first use" scheme would not be. Only
     * valid when IX is actually this function's frame pointer. */
    if (!omit_ix_frame) {
        int i;
        for (i = 0; i < g_addr_cache_array_count; ++i) {
            emit("\tpush ix\n\tpop hl\n");
            if (g_addr_cache_arrays[i].array_offset != 0)
                fprintf(outf, "\tld de,%d\n\tadd hl,de\n", g_addr_cache_arrays[i].array_offset);
            fprintf(outf, "\tld (ix%+d),l\n", g_addr_cache_arrays[i].cache_slot_offset);
            fprintf(outf, "\tld (ix%+d),h\n", g_addr_cache_arrays[i].cache_slot_offset + 1);
        }
    }
}

/* Every byte in buf[0..n) belongs to a complete comment line: each line
 * starts with ';' and ends with '\n' (a trailing partial line - no final
 * '\n' - fails this, since it means something is still being written). */
static int all_comment_lines(const char *buf, long n)
{
    long i = 0;

    while (i < n) {
        if (buf[i] != ';')
            return 0;
        while (i < n && buf[i] != '\n')
            i++;
        if (i >= n)
            return 0;   /* no closing '\n': last line is incomplete */
        i++;
    }
    return 1;
}

/*
 * If a "jp L<label>\n" sits at file offset jp_pos in `outf`, it is the tail
 * jump gen_return_ast just emitted for a `return` that turned out to be the
 * function's last statement: fall-through already reaches `label` (emitted
 * right after this call returns), so the jump is dead weight. Whatever has
 * been written since jp_pos, if anything, is either nothing, or a run of
 * "@dcc-var-end" scope-exit comments (-g emits one per local as it leaves
 * scope) - either way there is no real code in between. Verify the exact
 * bytes are there before touching anything: any mismatch (real code
 * followed, or the position doesn't line up) leaves the file untouched,
 * forgoing the optimization rather than risking dropping a jump that was
 * actually needed. Trailing comments are preserved (read into `tail`,
 * written back after truncating away just the jp line) so debug-info
 * fidelity is unaffected.
 */
static void elide_redundant_tail_jp(long jp_pos, int label)
{
    char expect[32];
    char actual[32];
    char tail[4096];
    long len, end_pos, tail_len;

    if (jp_pos < 0)
        return;
    sprintf(expect, "\tjp L%d\n", label);
    len = (long)strlen(expect);
    if (len >= (long)sizeof(expect))
        return;

    fflush(outf);
    end_pos = ftell(outf);
    if (end_pos < 0 || end_pos < jp_pos + len)
        return;
    tail_len = end_pos - jp_pos - len;
    if (tail_len >= (long)sizeof(tail))
        return;

    if (fseek(outf, jp_pos, SEEK_SET) != 0)
        return;
    if (fread(actual, 1, (size_t)len, outf) != (size_t)len) {
        fseek(outf, end_pos, SEEK_SET);
        return;
    }
    actual[len] = 0;
    if (strcmp(actual, expect) != 0) {
        fseek(outf, end_pos, SEEK_SET);
        return;
    }

    if (tail_len > 0) {
        if (fread(tail, 1, (size_t)tail_len, outf) != (size_t)tail_len) {
            fseek(outf, end_pos, SEEK_SET);
            return;
        }
        if (!all_comment_lines(tail, tail_len)) {
            fseek(outf, end_pos, SEEK_SET);
            return;
        }
    }

    fflush(outf);
#ifdef _WIN32
    if (_chsize(_fileno(outf), jp_pos) != 0)
        return;
#else
    if (ftruncate(fileno(outf), jp_pos) != 0)
        return;
#endif
    fseek(outf, jp_pos, SEEK_SET);
    if (tail_len > 0)
        fwrite(tail, 1, (size_t)tail_len, outf);
}

void emit_function_epilogue(int implicit_zero_return)
{
    if (implicit_zero_return) {
        emit("\tld hl,0\n");
    } else if (opt_debug && !scan_mode &&
               g_return_jp_check_label == current_return_label) {
        elide_redundant_tail_jp(g_return_jp_check_pos, current_return_label);
    }
    g_return_jp_check_pos = -1;
    g_return_jp_check_label = -1;
    emit_label(current_return_label);
    /* Map the shared return label to the function's closing brace when the
     * body always exits, so an early `return` that jumps here shows the
     * closing brace instead of inheriting the previous statement's line. */
    if (opt_debug && !scan_mode && g_func_close_line > 0)
        ast_emit_debug_location(g_func_close_file, g_func_close_line);
    g_func_close_line = 0;
    /* Always emit ld sp,ix so returns from nested control flow restore the
     * caller stack reliably. pass_elim_ix_frame and pass_shared_frame_stubs clean up the extra
     * instruction for functions that never actually need the stack restore. */
    if (!current_omit_ix_frame) {
        emit("\tld sp,ix\n");
        emit("\tpop ix\n");
    }
    emit("\tret\n");
    if (opt_debug && !scan_mode && current_debug_function[0])
        fprintf(outf, ";@dcc-func-end \"%s\" \"%s\"\n",
                current_debug_function, current_debug_function_source_name);
    current_debug_function[0] = 0;
    current_debug_function_source_name[0] = 0;
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
        /* Non-constant fields are re-parsed at emit time through
         * ast_emit_init_expr, whose AST build allocates a hidden temp for a
         * struct-return call member base (`mk(...).f`); reserve the same
         * slot here so the scan-derived frame size matches. */
        if (depth >= 1 && tok.kind == TOK_ID)
            scan_reserve_struct_return_member_temp();
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

        /* A struct-return call member access `mk(...).field` inside a
         * declaration initializer reserves a hidden temp during codegen's
         * AST build (ast_add_struct_return_member_temp); reserve the same
         * slot here so the scan-derived frame size matches, exactly as the
         * statement-level else-branch in scan_function_body does. */
        if (tok.kind == TOK_ID)
            scan_reserve_struct_return_member_temp();

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
    int sv_decl_is_volatile, sv_decl_pointee_is_volatile;
    static struct AstArena narrow_scratch_arena;
    static int narrow_scratch_inited;
    struct AstNode *seq;
    int result;

    if (opt_no_narrow)
        return 0;
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
    sv_decl_is_volatile = decl_is_volatile;
    sv_decl_pointee_is_volatile = decl_pointee_is_volatile;

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
    decl_is_volatile = sv_decl_is_volatile;
    decl_pointee_is_volatile = sv_decl_pointee_is_volatile;

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
    long sv_pos, sv_tok_start;
    int sv_line, sv_tok_line;
    struct Token sv_tok;
    int sv_nulabels, sv_for_seq, sv_forren_n, sv_for_decl_seq, sv_for_decl_rename_index;
    int sv_for_decl_recording, sv_scope_depth, sv_compound_literal_seq, sv_licm_seq;
    int sv_decl_is_volatile, sv_decl_pointee_is_volatile;
    static struct AstArena narrow_scalar_scratch_arena;
    static int narrow_scalar_scratch_inited;
    struct AstNode *seq;
    int result;

    if (opt_no_narrow)
        return 0;
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
    sv_decl_is_volatile = decl_is_volatile;
    sv_decl_pointee_is_volatile = decl_pointee_is_volatile;

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
    decl_is_volatile = sv_decl_is_volatile;
    decl_pointee_is_volatile = sv_decl_pointee_is_volatile;

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
    long sv_pos, sv_tok_start;
    int sv_line, sv_tok_line;
    struct Token sv_tok;
    int sv_nulabels, sv_for_seq, sv_forren_n, sv_for_decl_seq, sv_for_decl_rename_index;
    int sv_for_decl_recording, sv_scope_depth, sv_compound_literal_seq, sv_licm_seq;
    int sv_decl_is_volatile, sv_decl_pointee_is_volatile;
    static struct AstArena narrow_for_scratch_arena;
    static int narrow_for_scratch_inited;
    struct AstNode *seq;
    int result;

    if (opt_no_narrow)
        return 0;
    if ((type & 15) != TYPE_INT || type_ptr_depth(type) != 0 ||
        type_is_struct_object(type) || arrlen > 0 || total_elems > 0 || tok.kind == '=')
        return 0;

    if (!narrow_for_scratch_inited) {
        ast_arena_init(&narrow_for_scratch_arena);
        narrow_for_scratch_inited = 1;
    }
    ast_arena_reset(&narrow_for_scratch_arena);

    sv_pos = posi; sv_tok_start = tok_start_pos;
    sv_line = line_no; sv_tok_line = tok_line;
    sv_tok = tok;
    sv_nulabels = nulabels;
    sv_for_seq = g_for_seq; sv_forren_n = g_forren_n;
    sv_for_decl_seq = g_for_decl_seq; sv_for_decl_rename_index = g_for_decl_rename_index;
    sv_for_decl_recording = g_for_decl_recording; sv_scope_depth = g_scope_depth;
    sv_compound_literal_seq = g_compound_literal_seq; sv_licm_seq = g_licm_seq;
    sv_decl_is_volatile = decl_is_volatile;
    sv_decl_pointee_is_volatile = decl_pointee_is_volatile;

    asm_suppress_depth++;
    seq = narrow_build_speculative_scope(&narrow_for_scratch_arena);
    asm_suppress_depth--;
    result = (seq != NULL) ? narrow_for_counter_is_byte_safe(seq, name) : 0;

    posi = sv_pos; tok_start_pos = sv_tok_start;
    line_no = sv_line; tok_line = sv_tok_line;
    tok = sv_tok;
    nulabels = sv_nulabels;
    g_for_seq = sv_for_seq; g_forren_n = sv_forren_n;
    g_for_decl_seq = sv_for_decl_seq; g_for_decl_rename_index = sv_for_decl_rename_index;
    g_for_decl_recording = sv_for_decl_recording; g_scope_depth = sv_scope_depth;
    g_compound_literal_seq = sv_compound_literal_seq; g_licm_seq = sv_licm_seq;
    decl_is_volatile = sv_decl_is_volatile;
    decl_pointee_is_volatile = sv_decl_pointee_is_volatile;

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

    base_is_volatile = decl_is_volatile;
    base_pointee_is_volatile = decl_pointee_is_volatile;

    for (;;) {
        type = base;
        decl_is_volatile = base_is_volatile;
        decl_pointee_is_volatile = base_pointee_is_volatile;
        direct_funcptr = 0;

        while (accept('*')) {
            decl_pointee_is_volatile = decl_is_volatile;
            decl_is_volatile = skip_type_qualifiers_volatile();
            type = type_add_ptr(type);
        }

        if (parse_funcptr_declarator(&type, name, sizeof(name))) {
            direct_funcptr = 1;
        } else {
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

        if (!decl_is_volatile &&
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
        } else if (!decl_is_volatile &&
                   try_narrow_register_scalar(name, type, decl_is_register, arrlen, total_elems)) {
            type = (type & ~15) | TYPE_CHAR | TYPE_UNSIGNED;
        } else if (!decl_is_volatile &&
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
            s->is_volatile = decl_is_volatile;
            s->pointee_is_volatile = decl_pointee_is_volatile;
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
    int base_is_volatile;
    int base_pointee_is_volatile;
    char name[64];
    char backing_name[64];
    struct Sym *g;
    struct Sym *l;

    base_is_volatile = decl_is_volatile;
    base_pointee_is_volatile = decl_pointee_is_volatile;

    for (;;) {
        type = base;
        decl_is_volatile = base_is_volatile;
        decl_pointee_is_volatile = base_pointee_is_volatile;

        while (accept('*')) {
            decl_pointee_is_volatile = decl_is_volatile;
            decl_is_volatile = skip_type_qualifiers_volatile();
            type = type_add_ptr(type);
        }

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
            error_here("variable length array declaration cannot have static storage duration");
            g_vla_pending = 0;
            arrlen = 0;
        }
        if (arrlen == 0 && g_typedef_array_len > 0)
            arrlen = g_typedef_array_len;

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
            sprintf(backing_name, "__sl%d_%d", g_static_local_func_index,
                    g_static_local_seq++);
        }

        g = add_global(backing_name, type, SC_GLOBAL);
        g->is_defined = 1;
        g->needs_extrn = 0;
        g->is_static = 1;
        g->is_volatile = decl_is_volatile;
        g->pointee_is_volatile = decl_pointee_is_volatile;
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
            l->is_volatile = decl_is_volatile;
            l->pointee_is_volatile = decl_pointee_is_volatile;
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
            ast_scan_for_stmt();
            can_decl = 1;
        } else if (tok.kind == '}') {
            brace--;
            next_token();
            leave_scope();
            can_decl = 1;
        } else if (tok.kind == TOK_FOR || tok.kind == TOK_WHILE ||
                   tok.kind == TOK_DO ||
                   tok.kind == TOK_IF || tok.kind == TOK_SWITCH) {
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
        } else if (can_decl && tok.kind == TOK_STATIC_ASSERT) {
            parse_static_assert_decl();
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
            decl_is_noreturn = 0;
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
                scan_reserve_struct_return_member_temp();
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
    base_is_volatile = decl_is_volatile;
    base_pointee_is_volatile = decl_pointee_is_volatile;
    done = 0;

    while (!done && tok.kind != TOK_EOF) {
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
            is_volatile = decl_is_volatile;
            pointee_is_volatile = decl_pointee_is_volatile;
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
                typedef_array_len = parse_typed_array_bound_expr();
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
                    int inner = parse_typed_array_bound_expr();
                    if (typedef_array_len > 0 && inner > 0)
                        typedef_array_len *= inner;
                }
                expect(']');
            }
        } else if (tok.kind == '(') {
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

static int parse_global_addr_suffix(int base_type, long *offset)
{
    int cur_type;
    long idx;

    cur_type = base_type;
    while (tok.kind == '[' || tok.kind == '.' || tok.kind == TOK_ARROW) {
        if (tok.kind == '[') {
            int elem_size;
            next_token();
            idx = parse_typed_const_long_expr();
            expect(']');
            elem_size = type_size(cur_type);
            if (elem_size <= 0) elem_size = 2;
            *offset += idx * elem_size;
            continue;
        }
        if (tok.kind == TOK_ARROW)
            cur_type = type_decay_ptr(cur_type);
        next_token();
        if (tok.kind != TOK_ID) {
            error_here("field name expected in address initializer");
            return 0;
        }
        {
            int sid;
            struct FieldDef *fd;
            sid = type_struct_id(cur_type);
            fd = find_field_def(sid, tok.text);
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

    if (tok.kind != '(')
        return 0;
    next_token();
    if (tok.kind != '(')
        return 0;
    next_token();
    base_type = parse_type();
    expect(')');
    if (tok.kind != TOK_NUM || tok.val != 0)
        return 0;
    next_token();
    expect(')');
    if (tok.kind != TOK_ARROW)
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

    if (tok.kind != TOK_ID)
        return 0;
    ls = find_sym(tok.text);
    lname = ls ? sym_asm_name(ls) : tok.text;
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
        val[0] = parse_typed_const_expr_long();
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
            int litlen;
            lit = read_adjacent_string_literals_ex(&is_wide, &litlen);
            sid = add_string_ex(lit, litlen, is_wide);
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
            val[0] = parse_typed_const_expr_long();
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
        {
            long save_pos = posi;
            long save_tok_start = tok_start_pos;
            int save_line = line_no;
            int save_tok_line = tok_line;
            struct Token save_tok = tok;
            if (parse_global_cast_null_member_address(val)) {
                if (label) label[0] = 0;
                return 1;
            }
            posi = save_pos;
            tok_start_pos = save_tok_start;
            line_no = save_line;
            tok_line = save_tok_line;
            tok = save_tok;
        }
        if (parse_global_compound_literal_address(label, labelsz))
            return 2;
        if (parse_global_symbol_member_address(label, labelsz))
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
        tok.kind == TOK_STR) {
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
            unsigned int unit_mask;
            int stop;

            unit_off = fd->offset;
            unit = 0;
            unit_mask = 0;
            stop = 0;
            k = i;
            while (k >= 0 && k < nfield_defs && tok.kind != TOK_EOF && tok.kind != '}') {
                struct FieldDef *bfd;
                bfd = &field_defs[k];
                if (bfd->parent_struct_id == sid && !bfd->is_promoted) {
                    if (bfd->bit_width <= 0 || bfd->offset != unit_off)
                        break;
                    unit &= ~bitfield_field_mask(bfd);
                    unit |= bitfield_init_part(bfd, parse_struct_init_const_value());
                    unit_mask |= bitfield_field_mask(bfd);
                    if (!accept(',')) {
                        stop = 1;
                        break;
                    }
                    if (tok.kind == '}') {
                        stop = 1;
                        break;
                    }
                }
                /*
                 * A designated element (`.field = ...`) may follow.  When it
                 * names another bit-field in the SAME storage unit, keep
                 * accumulating it into `unit` so all designators for the unit
                 * are written with a single store.  A designator for a
                 * different unit (or a non-bit-field) is left for the outer
                 * field loop; the comma has already been consumed.
                 */
                if (tok.kind == '.') {
                    long save_pos = posi;
                    long save_tok_start = tok_start_pos;
                    int save_line = line_no;
                    int save_tok_line = tok_line;
                    struct Token save_tok = tok;
                    struct FieldDef *nf = NULL;

                    next_token();
                    if (tok.kind == TOK_ID)
                        nf = find_field_def(sid, tok.text);
                    if (nf != NULL && nf->bit_width > 0 && nf->offset == unit_off) {
                        next_token();
                        if (tok.kind == '=')
                            next_token();
                        else if (tok.kind != '[' && tok.kind != '.')
                            expect('=');
                        k = field_def_index(nf);
                        continue;
                    }
                    posi = save_pos;
                    tok_start_pos = save_tok_start;
                    line_no = save_line;
                    tok_line = save_tok_line;
                    tok = save_tok;
                    break;
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
            {
                int u;
                int found = -1;
                for (u = 0; u < bf_nunits; ++u) {
                    if (bf_unit_offs[u] == unit_off) {
                        found = u;
                        break;
                    }
                }
                if (found >= 0)
                    unit = (bf_unit_vals[found] & ~unit_mask) | unit;
                else if (bf_nunits < (int)(sizeof(bf_unit_offs) / sizeof(bf_unit_offs[0]))) {
                    found = bf_nunits++;
                    bf_unit_offs[found] = unit_off;
                }
                if (found >= 0)
                    bf_unit_vals[found] = unit;
            }
            global_init_write_value_at(s, baseoff + unit_off, NULL, (long)(unit & 0xffffU), 2, 0);
            if (k > i)
                i = k - 1;
            if (stop)
                break;
            /* Restart the field scan when a designator stopped the packing
             * loop, so it is handled even when this unit's owner was the last
             * declared field. */
            if (tok.kind == '.')
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
    int errors_before;

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
     * static-inline-body-buffering branch above): each buffered attempt gets
     * its own EXTRN dedup scope (reset via g_buffering_epoch, bumped here),
     * so a self-contained attempt's EXTRNs are complete and correct whether
     * it's kept or discarded, instead of relying on the global cache. */
    g_inline_body_buffering++;
    g_buffering_epoch++;
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
    /* Suppress diagnostics for the duration of this possibly-discarded
     * attempt (asm_suppress_depth, checked by dcc_error_at) so a genuine
     * source error isn't shown to the user before we know whether the real
     * fallback pass will re-encounter and correctly report it exactly once
     * - but g_diag_error_count still counts every call regardless of
     * suppression, so a real error occurring here can still force a decline
     * below rather than being silently swallowed if this attempt would
     * otherwise have been kept. */
    errors_before = g_diag_error_count;
    asm_suppress_depth++;
    emit_function_prologue(name, local_bytes, 1);
    gen_compound();
    emit_function_epilogue(implicit_zero_return);
    asm_suppress_depth--;
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
    if (g_diag_error_count == errors_before && !tmpfile_unsafe_for_noix(scratch)) {
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

/* Cheap pre-filter for try_speculative_bc_regalloc_function_body: main() is
 * excluded (never a hot leaf; the __mrun-shim emission after this codegen
 * assumes the ordinary frame shape), and current_function_has_call must be
 * false - this only detects an explicit C call syntactically present in the
 * source (see the scan pass, dcc_func.c ~line 2295), NOT an implicit
 * runtime-helper call (e.g. `call __mulu`) codegen may still insert for a
 * `*`, `/`, `%`, or long/float operation with no visible call syntax at all -
 * so it is only a pre-filter, never the actual safety proof. That proof is
 * regalloc_buffer_finalize below, which also rejects any "call" found in
 * the generated buffer that isn't to one of a small set of DCCRTL.MAC-
 * contracted runtime helpers known to preserve BC (buf_has_unsafe_call),
 * regardless of what this pre-filter guessed.
 *
 * Returns 1 whenever it's worth attempting speculative generation at all -
 * regardless of whether find_bc_regalloc_candidate finds a BC pointer
 * candidate, since an E-counter candidate (find_bc_regalloc_candidate's
 * counterpart in dcc_decl.c's gen_local_decl_after_type, gated on g_e_
 * regalloc_claim_active) is only discovered during the speculative
 * gen_compound() walk itself, not knowable in advance the way a parameter
 * candidate is. */
static int function_qualifies_for_speculative_regalloc(const char *name)
{
    if (strcmp(name, "main") == 0)
        return 0;
    if (current_function_has_call)
        return 0;
    return 1;
}

/* Ported from dccpeep.c's line_touches_reg_pair (proven correct there across
 * today's dccpeep pass work) rather than reinvented: true if `s` references
 * register B, C, or the BC pair as an operand anywhere in the line - as
 * opposed to merely containing the letter 'b' or 'c' as part of some
 * unrelated identifier or label, which a plain strstr would false-positive
 * on. Z80 instructions that touch BC only implicitly (block/repeat opcodes)
 * are matched by mnemonic; dcc's own codegen does not currently emit any of
 * these, but the check costs nothing and avoids silently trusting that fact
 * to remain true forever. */
/* Generalized from a BC-only original (see git history for the fill_record
 * "jp c,LABEL" false-positive this flag-condition exclusion fixes): true if
 * `s` references register `lo`, `hi`, or the pair `pair` as an operand
 * anywhere in the line - as opposed to merely containing that letter as
 * part of some unrelated identifier/label. Z80 instructions that touch a
 * pair only implicitly (block/repeat opcodes) are matched by mnemonic;
 * dcc's own codegen does not currently emit any of these for BC, but the
 * check costs nothing. jp/jr/call's optional leading condition, or ret's
 * sole operand, can be exactly "c"/"nc" meaning the carry flag rather than
 * register C - excluded by tracking token position; harmless (never
 * matches) when checking d/e/de, since Z80 has no letter-named flag
 * condition using those letters. */
static int line_touches_reg_pair(const char *s, const char *lo, const char *hi, const char *pair)
{
    static const char *implicit_pair_mnemonics[] = {
        "djnz ", "ldir", "lddr", "cpir", "cpdr",
        "otir", "otdr", "inir", "indr",
        "ldi", "ldd", "cpi", "cpd", "ini", "ind", "outi", "outd",
        NULL
    };
    static const char *cond_jump_mnemonics[] = { "jp", "jr", "call", "ret", NULL };
    const char *p;
    char tokbuf[16];
    char paren[8];
    int ti, i;
    int tok_index;
    int first_is_cond_mnemonic;

    for (i = 0; implicit_pair_mnemonics[i] != NULL; ++i)
        if (strncmp(s, implicit_pair_mnemonics[i], strlen(implicit_pair_mnemonics[i])) == 0)
            return 1;

    sprintf(paren, "(%s)", pair);
    if (strstr(s, paren) != NULL)
        return 1;

    p = s;
    tok_index = 0;
    first_is_cond_mnemonic = 0;
    while (*p) {
        if (isalpha((unsigned char)*p) || *p == '_') {
            ti = 0;
            while ((isalnum((unsigned char)*p) || *p == '_') && ti < 15)
                tokbuf[ti++] = *p++;
            tokbuf[ti] = 0;
            if (tok_index == 0) {
                for (i = 0; cond_jump_mnemonics[i] != NULL; ++i)
                    if (strcmp(tokbuf, cond_jump_mnemonics[i]) == 0)
                        first_is_cond_mnemonic = 1;
            } else if (tok_index == 1 && first_is_cond_mnemonic &&
                       (strcmp(tokbuf, "c") == 0 || strcmp(tokbuf, "nc") == 0)) {
                /* flag condition, not register C - not a touch */
            } else if (strcmp(tokbuf, lo) == 0 || strcmp(tokbuf, hi) == 0 || strcmp(tokbuf, pair) == 0) {
                return 1;
            }
            tok_index++;
        } else {
            p++;
        }
    }
    return 0;
}

int line_touches_bc_reg(const char *s)
{
    return line_touches_reg_pair(s, "b", "c", "bc");
}

static int line_touches_de_reg(const char *s)
{
    return line_touches_reg_pair(s, "d", "e", "de");
}

/* Runtime helpers DCCRTL.MAC documents (see the CONTRACT comment just above
 * __divs there) as preserving BC across the call: their fast paths never
 * touch b/c/bc at all, and their slow paths explicitly push/pop it. This is
 * the same trust dccpeep's pass_byte_loop_counter_to_reg_c already relies on
 * for __mods/__divs specifically; extended here to the full set DCCRTL.MAC's
 * comment names, since dcc's own codegen (dcc_ops.c, dcc_ast_gen_stmt.c) can
 * emit a call to any of the seven for a plain `*`, `/`, or `%` on int - none
 * of which appear as an AST_CALL node, so no AST-level scan can ever see
 * them; this text-level check is the only place they're visible at all.
 *
 * The twelve ctype.h entries after them (isalpha through tolower) are a
 * second, independently verified group: unlike the arithmetic seven, these
 * DO appear as ordinary AST_CALL nodes (see dcc_licm.c's licm_scan_modified,
 * whose AST_CALL case checks asm_name_is_bc_safe_call below before deciding
 * whether to decline a loop containing one at all - this array alone only
 * gates the text-level re-check once such a loop's speculative attempt has
 * already been allowed to proceed). Confirmed by direct inspection of every
 * one of DCCRTL.MAC's "ctype helpers with short external names" (the block
 * starting at its own "Character classification and conversion" comment):
 * every single one reads its argument through IX/SP-relative addressing and
 * classifies it using only A, HL, and flags - none reference B, C, or BC in
 * any form, so none need the push/pop-around-a-clobber escape the arithmetic
 * seven's slow paths use. Any OTHER runtime helper (float conversions,
 * BDOS/BIOS calls, __stchk, __call_hl, long-math variants, string/memory
 * functions - strcmp and strlen in particular were checked and DO use BC,
 * as a stack-argument/CPIR scratch register respectively - ...) carries no
 * such verified contract and is deliberately left out - a bare call to any
 * of those still fails the whole attempt, exactly as before this whitelist
 * existed. */
static const char *g_safe_runtime_calls[] = {
    "__mulu", "__udivmod", "__divu", "__modu", "__divs", "__mods", "__sdivmod",
    "__caa", "__can", "__csp", "__cdg", "__cup", "__clo",
    "__cxd", "__cpr", "__cct", "__cpu", "__ctu", "__ctl",
    NULL
};

/* True if `name` (an asm-level call target, e.g. "__csp" for isspace - see
 * asm_name_for(sym_asm_name(s))) is on g_safe_runtime_calls above. Exposed
 * non-static for dcc_licm.c's licm_scan_modified, which needs this same
 * verified-safe set at the AST level (by the callee's real asm name, since
 * the DCCRTL.MAC short-name remapping only happens at that point - the C
 * name "isspace" itself is not what appears in g_safe_runtime_calls) to
 * decide whether a loop containing such a call is even eligible to attempt
 * BC promotion in the first place - see that function's own comment. */
int asm_name_is_bc_safe_call(const char *name)
{
    int i;

    for (i = 0; g_safe_runtime_calls[i] != NULL; i++) {
        if (strcmp(name, g_safe_runtime_calls[i]) == 0)
            return 1;
    }
    return 0;
}

/* True if `name` (an asm-level call target, e.g. "_Z0001" for a static
 * function or "_foo" for a public one - see sym_asm_name) is a function
 * declared _Noreturn. Checked in addition to g_safe_runtime_calls: a call
 * to such a function is trustworthy for a completely different reason than
 * the DCCRTL.MAC contract above - not because it preserves bc/de, but
 * because control never returns to any point after it, so whatever it
 * clobbers can never be read back through this speculative attempt's
 * promoted candidate on that path. This is the text-level counterpart to
 * dcc_licm.c's AST-level tolerance for the same case (licm_scan_modified's
 * AST_CALL handling) - that scan makes the loop eligible to ATTEMPT
 * promotion in the first place; this is what makes the attempt actually
 * verify once the call's real asm name is visible in the generated text
 * (found via forint.c's eval_e: get_sym_val inlines cleanly, but its own
 * callee cell_at still has a genuine, non-inlined `call` to die() for its
 * bounds check, which is _Noreturn but not itself inline-substitutable). */
int asm_name_is_noreturn_call(const char *name)
{
    int i;

    for (i = 0; i < nglobals; i++) {
        if (globals[i].storage == SC_FUNC && globals[i].is_noreturn &&
            strcmp(asm_name_for(globals[i].name), name) == 0)
            return 1;
    }
    return 0;
}

/* True if `buf` contains a "\tcall NAME" line whose NAME is neither on
 * g_safe_runtime_calls above nor a call to a _Noreturn function (see
 * asm_name_is_noreturn_call) - i.e. true if there is at least one call this
 * speculative attempt cannot trust. NAME is taken as running from just after
 * "\tcall " to end of line (or a trailing comment/condition would break this,
 * but dcc's own codegen never emits either after a call's target). */
int buf_has_unsafe_call(const char *buf)
{
    static const char prefix[] = "\tcall ";
    const size_t prefix_len = sizeof(prefix) - 1;
    const char *p;

    p = buf;
    for (;;) {
        const char *hit = strstr(p, prefix);
        const char *name_start, *name_end;
        char namebuf[32];
        size_t namelen;
        int whitelisted;

        if (hit == NULL)
            return 0;
        name_start = hit + prefix_len;
        name_end = name_start;
        while (*name_end != '\0' && *name_end != '\n' && *name_end != ' ' &&
               *name_end != '\t' && *name_end != ';')
            name_end++;
        namelen = (size_t)(name_end - name_start);
        if (namelen >= sizeof(namebuf))
            namelen = sizeof(namebuf) - 1;
        memcpy(namebuf, name_start, namelen);
        namebuf[namelen] = 0;

        whitelisted = asm_name_is_bc_safe_call(namebuf) || asm_name_is_noreturn_call(namebuf);
        if (!whitelisted)
            return 1;

        p = name_end;
    }
}

#define MAX_BC_LOOP_LABELS 512

static int bc_label_name_index(char names[][16], int n, const char *name)
{
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(names[i], name))
            return i;
    return -1;
}

/* regalloc_buffer_finalize's bc_trusted tracking below is a single linear
 * scan of the generated text with no notion of control flow: it is sound for
 * straight-line code and for if/else (each branch is its own straight-line
 * span, visited once, correctly reflecting that branch's own history up to
 * that point), but not for a loop. A label that is both fallen into once
 * AND reached again via a backward jump (the loop's back-edge) is only
 * visited ONCE by the linear scan, in file order - any bc-clobbering that
 * happens later in the loop body, between that label and the jump back to
 * it, is invisible to the scan on every visit after the first, because there
 * is no second textual visit to react to. Found via tests/tlongidx.c: `long
 * i` incremented as an array index (`in[i++]`) inside a while loop clobbers
 * bc (used as scratch for the 32-bit increment's address), gets correctly
 * reloaded before the loop's own later, first-encountered use, but the scan
 * has already permanently marked bc "trusted" by the time it reaches the
 * loop header text again on paper - so the second real iteration silently
 * reused stale bc instead of the reloaded value.
 *
 * Scans the whole buffer once up front for every "LNN:" label that is the
 * target of a "jp"/"jr" (conditional or not) appearing AFTER that label's
 * own definition in the text - i.e. a genuine backward jump, not a forward
 * skip - and returns the set of such labels. The caller forces bc_trusted
 * false at every one of them, which costs at most one possibly-unneeded
 * reload on the label's first (fall-through) visit, in exchange for
 * correctness on every subsequent (looped) visit - the same trade-off this
 * whole pass already makes for "push bc ... pop bc" (see the comment above
 * regalloc_buffer_finalize). */
static void bc_regalloc_find_loop_headers(const char *buf, long size,
                                           char headers[][16],
                                           long header_offs[], long body_end_offs[],
                                           int *n_headers)
{
    char seen_names[MAX_BC_LOOP_LABELS][16];
    long seen_offs[MAX_BC_LOOP_LABELS];
    int n_seen;
    const char *p;
    const char *nl;
    char linebuf[64];
    size_t ll;
    long line_off;

    n_seen = 0;
    *n_headers = 0;
    p = buf;
    while (p < buf + size) {
        nl = memchr(p, '\n', (size_t)(buf + size - p));
        ll = nl ? (size_t)(nl - p) : (size_t)(buf + size - p);
        if (ll >= sizeof(linebuf)) ll = sizeof(linebuf) - 1;
        memcpy(linebuf, p, ll);
        linebuf[ll] = 0;
        line_off = (long)(p - buf);

        if (linebuf[0] == 'L' && isdigit((unsigned char)linebuf[1])) {
            char *colon = strchr(linebuf, ':');
            if (colon != NULL && n_seen < MAX_BC_LOOP_LABELS) {
                *colon = 0;
                dcc_copy_str(seen_names[n_seen], sizeof(seen_names[0]), linebuf);
                seen_offs[n_seen] = line_off;
                n_seen++;
            }
        } else if (strncmp(linebuf, "\tjp ", 4) == 0 || strncmp(linebuf, "\tjr ", 4) == 0) {
            char *comma = strrchr(linebuf, ',');
            char *tok = comma ? comma + 1 : linebuf + 4;
            while (*tok == ' ') tok++;
            if (tok[0] == 'L' && isdigit((unsigned char)tok[1])) {
                int si = bc_label_name_index(seen_names, n_seen, tok);
                if (si >= 0) {
                    long end_off = (long)((nl ? nl + 1 : buf + size) - buf);
                    int hi = bc_label_name_index(headers, *n_headers, tok);
                    if (hi < 0 && *n_headers < MAX_BC_LOOP_LABELS) {
                        dcc_copy_str(headers[*n_headers], sizeof(headers[0]), tok);
                        header_offs[*n_headers] = seen_offs[si];
                        body_end_offs[*n_headers] = end_off;
                        (*n_headers)++;
                    } else if (hi >= 0 && end_off > body_end_offs[hi]) {
                        /* a second (e.g. "continue"-style) back-edge to the
                         * same header - widen the body span to cover it too */
                        body_end_offs[hi] = end_off;
                    }
                }
            }
        }

        p = nl ? nl + 1 : buf + size;
    }
}

/* Computes a BC-resident candidate's own fixed priming-instruction text, as
 * up to 3 lines (no trailing newline on any of them - callers add their
 * own). Shared by bc_loop_body_self_consistent and regalloc_buffer_finalize
 * (this file) and loop_regalloc_write_candidate_safe (dcc_loop_regalloc.c)
 * so all three recognize/reinsert exactly the same text a real prime emits -
 * see try_loop_regalloc_bc/try_loop_regalloc_bc_write (dcc_loop_regalloc.c)
 * for the actual emission this must stay in lockstep with.
 *
 * A local/param candidate's frame slot supports the ordinary 2-instruction
 * "ld c,(ix+d)"/"ld b,(ix+d+1)" pair. A global has no such direct absolute
 * BC load on Z80 (there is no "ld bc,(nn)" opcode this codebase's target
 * assembler recognizes for this form) - its prime is a 3-instruction
 * sequence instead, mirroring emit_load_global_word_direct (dcc_symbols.c)
 * plus a transfer into bc. Returns the line count (2 or 3). */
int bc_regalloc_entry_lines(struct Sym *cand, char lines[3][40])
{
    if (cand->storage == SC_GLOBAL || cand->storage == SC_EXTERN) {
        sprintf(lines[0], "\tld hl,(%s)", asm_name_for(sym_asm_name(cand)));
        strcpy(lines[1], "\tld c,l");
        strcpy(lines[2], "\tld b,h");
        return 3;
    }
    sprintf(lines[0], "\tld c,(ix%+d)", cand->offset);
    sprintf(lines[1], "\tld b,(ix%+d)", cand->offset + 1);
    return 2;
}

/* Spill-side counterpart of bc_regalloc_entry_lines, used only by a write
 * candidate's own verifier (loop_regalloc_write_candidate_safe, dcc_loop_
 * regalloc.c) to recognize try_loop_regalloc_bc_write's own spill text.
 * Local/param: "ld (ix+d),c"/"ld (ix+d+1),b" (2 lines). Global: transfer bc
 * into hl then store it absolutely, mirroring emit_store_global_word_direct
 * (dcc_symbols.c) - "ld l,c"/"ld h,b"/"ld (name),hl" (3 lines; the first two
 * are also already part of the generic recognized-line set every write
 * candidate's verifier accepts, so only the final store is genuinely new
 * text, but returning all 3 keeps this and bc_regalloc_entry_lines
 * symmetric and equally simple for callers to use). Returns the line count
 * (2 or 3). */
int bc_regalloc_exit_lines(struct Sym *cand, char lines[3][40])
{
    if (cand->storage == SC_GLOBAL || cand->storage == SC_EXTERN) {
        strcpy(lines[0], "\tld l,c");
        strcpy(lines[1], "\tld h,b");
        sprintf(lines[2], "\tld (%s),hl", asm_name_for(sym_asm_name(cand)));
        return 3;
    }
    sprintf(lines[0], "\tld (ix%+d),c", cand->offset);
    sprintf(lines[1], "\tld (ix%+d),b", cand->offset + 1);
    return 2;
}

/* Precise per-loop refinement of the above: a label only truly NEEDS bc
 * forced untrusted if its own body is not internally self-consistent - i.e.
 * simulating the exact same trust-transition rules regalloc_buffer_finalize
 * uses below, starting from bc_trusted=1 (the state the label is in by the
 * time of its second and later visits, after any reload the real scan
 * already inserts on the first, fall-through visit), the body does NOT end
 * back at bc_trusted=1 by its own back-edge. If it does, every iteration is
 * identical to the first, and forcing a reload at the header is pure waste
 * (found via tests/tbig.c's fill_record: a leaf loop that reads the bc-
 * resident pointer every iteration and never writes b/c/bc at all, yet paid
 * for a fresh two-byte reload on all 124 iterations under the older,
 * unconditional version of this pass).
 *
 * This mirrors, rather than reimplements, regalloc_buffer_finalize's own
 * is_bc_value_read_start / is_bc_recognized_other / line_touches_bc_reg
 * predicates on purpose - a hand-rolled second classifier could silently
 * diverge from what the real scan actually does and reintroduce exactly the
 * kind of blind spot this whole mechanism exists to avoid. */
static int bc_loop_body_self_consistent(const char *buf, long start, long end,
                                         const char entry_lines[3][40], int n_entry_lines)
{
    const char *p, *nl;
    char linebuf[64], prev1[64];
    size_t ll;
    int bc_trusted;
    int is_bc_value_read_start, is_bc_recognized_other;
    int k;

    bc_trusted = 1;
    prev1[0] = 0;
    p = buf + start;
    while (p < buf + end) {
        nl = memchr(p, '\n', (size_t)(buf + end - p));
        ll = nl ? (size_t)(nl - p) : (size_t)(buf + end - p);
        if (ll >= sizeof(linebuf)) ll = sizeof(linebuf) - 1;
        memcpy(linebuf, p, ll);
        linebuf[ll] = 0;

        /* Comment-only lines execute nothing and must not affect trust
         * tracking or the prev1 adjacency state - see the identical guard in
         * regalloc_buffer_finalize's main scan (a ";@dcc-regalloc-bc-prime"
         * marker's bare "bc" token would otherwise trip line_touches_bc_reg).
         * This scan produces no output, so it just advances past the line. */
        {
            const char *cq = linebuf;
            while (*cq == ' ' || *cq == '\t') cq++;
            if (*cq == ';') {
                p = nl ? nl + 1 : buf + end;
                continue;
            }
        }

        is_bc_value_read_start =
            ((strcmp(linebuf, "\tld l,c") == 0 && strcmp(prev1, "\tld h,b") != 0) ||
             (strcmp(linebuf, "\tld e,c") == 0 && strcmp(prev1, "\tld d,b") != 0));
        is_bc_recognized_other =
            (strcmp(linebuf, "\tld h,b") == 0 || strcmp(linebuf, "\tld d,b") == 0 ||
             (strcmp(linebuf, "\tld l,c") == 0 && strcmp(prev1, "\tld h,b") == 0) ||
             (strcmp(linebuf, "\tld e,c") == 0 && strcmp(prev1, "\tld d,b") == 0));
        if (!is_bc_recognized_other) {
            for (k = 0; k < n_entry_lines; k++) {
                if (strcmp(linebuf, entry_lines[k]) == 0) {
                    is_bc_recognized_other = 1;
                    break;
                }
            }
        }

        if (is_bc_value_read_start)
            bc_trusted = 1;
        else if (!is_bc_recognized_other && line_touches_bc_reg(linebuf))
            bc_trusted = 0;

        dcc_copy_str(prev1, sizeof(prev1), linebuf);
        p = nl ? nl + 1 : buf + end;
    }
    return bc_trusted;
}

/* Exact safety verification and, for BC, on-demand-reload REWRITE, for
 * try_speculative_bc_regalloc_function_body.
 *
 * E is unchanged from before: strict decline-only. Every line touching d/e/
 * de (once e_cand's own value is live) must be one of the small recognized
 * shapes emit_store_hl_to_sym_direct/emit_incdec_sym_direct/emit_load_sym_
 * value_direct/ast_byte_operand's kind-1 hooks produce; anything else
 * discards the whole attempt. E has no shadow to fall back on (its frame
 * slot is never kept in sync - see gen_local_decl_after_type), so there is
 * nothing to reload from; a real clobber here is unrecoverable, not just
 * inconvenient.
 *
 * BC is different, and gets a genuinely more permissive treatment: bc_cand
 * is read-only by construction (find_bc_regalloc_candidate excludes any
 * candidate ever written to), so its ORIGINAL incoming-parameter stack slot
 * (ix+off / ix+off+1) never changes for the life of the function - it is
 * already a perfect, always-valid shadow, for free, with no bookkeeping
 * needed to keep it in sync. So instead of declining outright the moment
 * anything else touches b/c/bc (e.g. tbig.c's get_stamp parking a scratch
 * value via push bc/pop bc for unrelated long arithmetic), this pass tracks
 * whether bc is currently "trusted" (untouched by anything but a recognized
 * line since the last known-good point) as it walks forward, and - the
 * moment it's asked to trust bc again at a recognized value-read site while
 * untrusted - REWRITES the buffer, inserting a fresh reload from that
 * always-correct original slot right there, before continuing. This is
 * deliberately conservative in one direction: it does not attempt to prove
 * a "push bc ... pop bc" pair actually restores the original value (which
 * it usually does) and skip the reload in that case - every untrusted point
 * gets a reload whether or not one was strictly needed, trading a few extra
 * instructions for staying exact rather than tracking real stack-balance
 * semantics from flat text.
 *
 * A "call" to anything other than one of g_safe_runtime_calls' seven
 * DCCRTL.MAC-contracted helpers (see buf_has_unsafe_call above) still fails
 * the whole attempt outright, for both candidates: current_function_has_call
 * only detects an explicit C call syntactically present in the source, not
 * an implicit runtime-helper call (e.g. `call __mulu`) codegen may still
 * insert for a `*`, `/`, `%`, or long/float operation with no call syntax
 * visible at all - this feature's leaf-only gate otherwise depends on there
 * being truly zero calls of any kind, and an arbitrary call's effect on
 * bc/de is not something a reload can safely paper over (unlike a same-
 * function scratch use, it's not visible in this text at all); the seven
 * whitelisted helpers are the sole exception, trusted by documented contract
 * rather than by anything this scan itself can verify. Neither candidate's
 * own address may ever be taken either - see g_regalloc_address_escaped
 * (dcc_symbols.c), checked separately by the caller.
 *
 * On success, *out_f is a rewound tmpfile holding the (possibly BC-
 * reload-rewritten) content to commit - the caller must fclose it. On
 * failure, *out_f is untouched.
 *
 * Not static: dcc_loop_regalloc.c calls this directly to verify a single
 * loop's speculatively-generated body, passing e_cand=NULL (loop-scoped
 * promotion only ever targets BC, matching find_bc_regalloc_candidate's own
 * word-sized/read-only/never-address-taken candidate shape - see that
 * file). Nothing about this scan assumes its buffer covers a whole function
 * body rather than one loop's own emitted span - the "any call anywhere"
 * check and the loop-header self-consistency check are exactly as sound,
 * and exactly as needed, scoped to just a loop's own text. */
int regalloc_buffer_finalize(FILE *f, struct Sym *bc_cand, struct Sym *e_cand,
                              FILE **out_f)
{
    long size;
    char *buf;
    char *line, *nl;
    char entry_lines[3][40];
    int n_entry_lines = 0;
    int safe;
    int e_live;
    int bc_trusted;
    char prev1[32], prev2[32];
    FILE *rewritten;
    char loop_headers[MAX_BC_LOOP_LABELS][16];
    int n_loop_headers;
    char fwd_untrusted[MAX_BC_LOOP_LABELS][16];
    int n_fwd_untrusted;

    rewritten = tmpfile();
    if (rewritten == NULL)
        fatal("cannot create speculative regalloc rewrite temp file");

    fseek(f, 0, SEEK_END);
    size = ftell(f);
    rewind(f);
    if (size <= 0) {
        rewind(rewritten);
        *out_f = rewritten;
        return 1;
    }
    buf = (char *)xmalloc((size_t)size + 1);
    if (fread(buf, 1, (size_t)size, f) != (size_t)size)
        fatal("cannot read speculative regalloc temp file");
    buf[size] = 0;

    if (buf_has_unsafe_call(buf)) {
        free(buf);
        fclose(rewritten);
        return 0;
    }

    if (bc_cand != NULL) {
        char all_headers[MAX_BC_LOOP_LABELS][16];
        long header_offs[MAX_BC_LOOP_LABELS];
        long body_end_offs[MAX_BC_LOOP_LABELS];
        int n_all, hi;

        n_entry_lines = bc_regalloc_entry_lines(bc_cand, entry_lines);
        bc_regalloc_find_loop_headers(buf, size, all_headers, header_offs, body_end_offs, &n_all);

        n_loop_headers = 0;
        for (hi = 0; hi < n_all; hi++) {
            if (!bc_loop_body_self_consistent(buf, header_offs[hi], body_end_offs[hi],
                                               entry_lines, n_entry_lines)) {
                dcc_copy_str(loop_headers[n_loop_headers], sizeof(loop_headers[0]), all_headers[hi]);
                n_loop_headers++;
            }
        }
    } else {
        n_loop_headers = 0;
    }

    safe = 1;
    e_live = 0;
    bc_trusted = 1;
    n_fwd_untrusted = 0;
    prev1[0] = 0;
    prev2[0] = 0;
    line = buf;
    while (safe && line < buf + size) {
        int is_bc_value_read_start;
        int is_bc_recognized_other;
        int is_recognized_e_line;
        int is_recognized_e_index_swap;
        int is_universally_safe_de_line;

        nl = strchr(line, '\n');
        if (nl) *nl = 0;

        /* A comment-only line (first non-blank char ';') executes nothing -
         * in particular the ";@dcc-regalloc-bc-prime" marker emit_function_
         * prologue and the loop primers plant purely so dccpeep's own
         * bc_regalloc_claimed_before can tell dcc's global BC prime apart
         * from its identical-looking global_word_cache_store. Such a line
         * must be emitted verbatim but take no part in trust tracking: the
         * token scan line_touches_bc_reg uses would otherwise see the bare
         * "bc" in "regalloc-bc-prime" as register BC and wrongly clear
         * bc_trusted right after the prime, splicing a redundant reload in
         * before the candidate's first real use. It must also leave prev1/
         * prev2 untouched, since those record the last real instruction for
         * the value-read adjacency test below. */
        {
            const char *cq = line;
            while (*cq == ' ' || *cq == '\t') cq++;
            if (*cq == ';') {
                fprintf(rewritten, "%s\n", line);
                line = nl ? nl + 1 : buf + size;
                continue;
            }
        }

        /* The two-line "ld l,c"/"ld h,b" or "ld e,c"/"ld d,b" pairs (emit_
         * load_sym_value_direct/emit_load_sym_de_direct's REG_BC branches)
         * are always emitted back-to-back with nothing in between, so the
         * FIRST line of either pair is the one decision point: if bc is
         * currently untrusted, insert a fresh reload from the candidate's
         * own never-written original parameter slot right before it, and
         * treat bc as trusted again from here on - the second line of the
         * pair, and the two entry-load lines themselves, never need their
         * own check.
         *
         * But "ld l,c"/"ld e,c" are not unique to that pair: gen_post_
         * update_from_addr and several long/pointer helpers (dcc_expr.c,
         * dcc_ast_gen_expr.c, dcc_ast_gen.c, dcc_ops.c) save an unrelated
         * address in BC as scratch and later restore it with "ld h,b"/"ld
         * l,c" (or "ld d,b"/"ld e,c") - textually the SAME two lines as the
         * value-read pair, but in the OPPOSITE order and for a completely
         * different purpose (BC already holds a scratch address there, not
         * the cached parameter). Misreading that restore's second line as a
         * fresh value-read-start inserted a reload mid-restore, splicing the
         * parameter's low byte into what should have been the scratch
         * address's low byte - found via tests/tlongidx.c hanging (long i;
         * ... in[i++] inside a while loop, with `in` the sole candidate
         * parameter). Disambiguate by checking the immediately preceding
         * line: "ld l,c"/"ld e,c" only starts a value-read when it is NOT
         * immediately preceded by "ld h,b"/"ld d,b" respectively - in that
         * case it is the restore pair's own second line, already covered by
         * is_bc_recognized_other so it does not disturb bc_trusted (which
         * was correctly cleared when the scratch address was first loaded
         * into bc). */
        /* Loop back-edge target: force bc untrusted here (see
         * bc_regalloc_find_loop_headers) regardless of how trusted the
         * single linear scan thinks bc is on this, its only textual visit -
         * a later iteration reaching this same label at runtime may not
         * share that history. */
        if ((n_loop_headers > 0 || n_fwd_untrusted > 0) &&
            line[0] == 'L' && isdigit((unsigned char)line[1])) {
            size_t llen = strlen(line);
            if (llen > 0 && line[llen - 1] == ':') {
                char labelbuf[16];
                size_t nlen = llen - 1;
                if (nlen >= sizeof(labelbuf)) nlen = sizeof(labelbuf) - 1;
                memcpy(labelbuf, line, nlen);
                labelbuf[nlen] = 0;
                /* Force bc untrusted at this label if it is a non-self-
                 * consistent loop back-edge target (loop_headers) OR the
                 * target of any forward jump taken while bc was untrusted
                 * (fwd_untrusted).  The latter closes the branch-join hole:
                 * the linear scan reflects only the textually-preceding
                 * (fall-through) edge, so at an if/else join whose then-arm
                 * clobbers bc and jumps here while the else-arm falls through
                 * trusted, the scan would otherwise leave bc "trusted" and
                 * skip the reload the then-arm path needs. */
                if (bc_label_name_index(loop_headers, n_loop_headers, labelbuf) >= 0 ||
                    bc_label_name_index(fwd_untrusted, n_fwd_untrusted, labelbuf) >= 0)
                    bc_trusted = 0;
            }
        }

        is_bc_value_read_start = bc_cand != NULL &&
            ((strcmp(line, "\tld l,c") == 0 && strcmp(prev1, "\tld h,b") != 0) ||
             (strcmp(line, "\tld e,c") == 0 && strcmp(prev1, "\tld d,b") != 0));
        is_bc_recognized_other = bc_cand != NULL &&
            (strcmp(line, "\tld h,b") == 0 || strcmp(line, "\tld d,b") == 0 ||
             (strcmp(line, "\tld l,c") == 0 && strcmp(prev1, "\tld h,b") == 0) ||
             (strcmp(line, "\tld e,c") == 0 && strcmp(prev1, "\tld d,b") == 0));
        if (bc_cand != NULL && !is_bc_recognized_other) {
            int k;
            for (k = 0; k < n_entry_lines; k++) {
                if (strcmp(line, entry_lines[k]) == 0) {
                    is_bc_recognized_other = 1;
                    break;
                }
            }
        }

        if (is_bc_value_read_start) {
            if (!bc_trusted) {
                int k;
                for (k = 0; k < n_entry_lines; k++)
                    fprintf(rewritten, "%s\n", entry_lines[k]);
                bc_trusted = 1;
            }
        } else if (!is_bc_recognized_other && bc_cand != NULL && line_touches_bc_reg(line)) {
            bc_trusted = 0;
        }
        fprintf(rewritten, "%s\n", line);

        /* Forward-branch path-sensitivity (companion to the label barrier
         * above): record the target of any jp/jr taken while bc is
         * untrusted, so when the scan later reaches that label it forces a
         * reload regardless of the (possibly trusted) fall-through edge.
         * bc_trusted is unchanged by a jump line itself - jp/jr never write
         * b/c/bc, and a "c"/"nc" flag condition is excluded by
         * line_touches_bc_reg - so it still reflects the taken edge's trust
         * here.  Backward/loop edges are covered by loop_headers +
         * bc_loop_body_self_consistent, not this. */
        if (bc_cand != NULL && !bc_trusted &&
            (strncmp(line, "\tjp ", 4) == 0 || strncmp(line, "\tjr ", 4) == 0)) {
            const char *comma = strrchr(line, ',');
            const char *jt = comma ? comma + 1 : line + 4;
            while (*jt == ' ') jt++;
            if (jt[0] == 'L' && isdigit((unsigned char)jt[1])) {
                char jname[16];
                int ji = 0;
                while (jt[ji] && jt[ji] != ' ' && jt[ji] != '\t' &&
                       ji < (int)sizeof(jname) - 1) {
                    jname[ji] = jt[ji];
                    ji++;
                }
                jname[ji] = 0;
                if (bc_label_name_index(fwd_untrusted, n_fwd_untrusted, jname) < 0) {
                    if (n_fwd_untrusted >= MAX_BC_LOOP_LABELS) {
                        safe = 0;
                    } else {
                        dcc_copy_str(fwd_untrusted[n_fwd_untrusted++],
                                     sizeof(fwd_untrusted[0]), jname);
                    }
                }
            }
        }

        is_recognized_e_line =
            strcmp(line, "\tld e,l") == 0 || strcmp(line, "\tld l,e") == 0 ||
            strcmp(line, "\tld d,0") == 0 ||
            strcmp(line, "\tinc e") == 0 || strcmp(line, "\tdec e") == 0 ||
            strcmp(line, "\tld a,e") == 0 || strcmp(line, "\tcp e") == 0;

        /* Z80 semantics guarantee these three never write d/e/de - only HL
         * (and flags) - so they can never clobber a live counter regardless
         * of context, and (unlike is_recognized_e_line) must NOT set e_live:
         * add hl,de is the universal last step of every base-plus-index
         * address computation (gen_index_addr_ast) for ANY index, including
         * ones with nothing to do with this counter - counting it as "the
         * counter's own line" let it flip e_live on early, from a completely
         * unrelated b[1]/b[2]/b[3] address computation that runs before the
         * counter is even initialized, which then wrongly flagged the very
         * next unrelated "ld de,2" as a violation. */
        is_universally_safe_de_line =
            strcmp(line, "\tadd hl,de") == 0 || strcmp(line, "\tadc hl,de") == 0 ||
            strcmp(line, "\tsbc hl,de") == 0;

        /* gen_index_addr_ast's generic non-constant-index path (dcc_ast_gen_
         * expr.c) always follows a bare-identifier index load with "ex
         * de,hl" to move the index from HL into DE for the base-plus-index
         * add - including when that index is our own reg_alloc'd counter,
         * whose value was JUST loaded by "ld l,e"/"ld h,0" the two lines
         * before. Swapping HL and DE at that exact point only relocates the
         * value this feature's own emit_load_sym_value_direct put in L (now
         * H:L = 0:e) into D:E - numerically the same value, so e's live
         * content survives, and the line is exactly identifiable as this
         * specific idiom, not a coincidence to gloss over: it is only
         * excluded when immediately preceded by exactly "ld l,e"/"ld h,0" in
         * that order, nothing else. Found the same way as the "ld de,1"
         * false positive above, one line later in the same fill_record
         * dump. */
        is_recognized_e_index_swap =
            strcmp(line, "\tex de,hl") == 0 &&
            strcmp(prev1, "\tld h,0") == 0 && strcmp(prev2, "\tld l,e") == 0;

        /* Before the counter's own first recognized line, it holds no live
         * value yet (try_narrow_for_counter's proof requires it be declared
         * with no initializer - its real first write is exactly one of
         * these lines), so any earlier, unrelated d/e scratch use (e.g. a
         * literal array-offset constant like "ld de,1" for some other
         * variable's b[1]) is harmless and must not be flagged - found via
         * tbig.c's fill_record, whose four explicit b[0..3] stores (using
         * de purely as address-offset scratch) all run before `i`'s own
         * init. Once e_live is set it stays set for the rest of the
         * function - a later unrelated de use IS a real hazard (the
         * counter may still be read again, e.g. after the loop it belongs
         * to, since this scan has no notion of the loop's own extent) -
         * conservative by construction, not by accident. */
        if (safe && e_cand != NULL && e_live &&
            !is_recognized_e_line && !is_recognized_e_index_swap &&
            !is_universally_safe_de_line &&
            line_touches_de_reg(line))
            safe = 0;
        if (is_recognized_e_line)
            e_live = 1;

        memcpy(prev2, prev1, sizeof(prev2));
        {
            size_t ll = strlen(line);
            if (ll > sizeof(prev1) - 1) ll = sizeof(prev1) - 1;
            memcpy(prev1, line, ll);
            prev1[ll] = 0;
        }

        line = nl ? nl + 1 : buf + size;
    }

    free(buf);
    if (!safe) {
        fclose(rewritten);
        return 0;
    }
    rewind(rewritten);
    *out_f = rewritten;
    return 1;
}

/* Tried before find_bc_regalloc_candidate's own whole-function BC candidate
 * gets a chance: dcc_loop_regalloc.c's loop-scoped mechanism finds and
 * ranks a per-loop candidate far more precisely than find_bc_regalloc_
 * candidate's crude "first read-only parameter referenced twice, in
 * declaration order" token-scan does - and, whenever it succeeds, a loop-
 * scoped candidate is essentially always more valuable, since it's
 * referenced inside a loop that (unless proven otherwise) runs more than
 * once, versus a flat whole-function reference count. But the two
 * mechanisms don't know each other's value in advance, and whole-function's
 * own commit happens by wrapping the ENTIRE body in one speculative
 * generate-verify-commit BEFORE any loop inside it is even reached - so
 * whichever claims BC first has always won unconditionally, regardless of
 * actual value (see tests/forint.c's eval_e for a real example: its own
 * best loop-scoped candidate, referenced 30 times inside its hot loop, was
 * losing outright to a parameter referenced 3 times, all outside any
 * loop).
 *
 * Rather than predict which side would win (a lexical heuristic risks
 * declining find_bc_regalloc_candidate's candidate for nothing if the
 * loop-scoped one then fails for a reason the heuristic can't see - e.g. a
 * text-level verifier decline), this generates the body once with NOTHING
 * pre-claimed - exactly like the plain-buffered/final-fallback branches
 * below, where dcc_loop_regalloc.c already gets a fully fair, unimpeded
 * shot today - and checks g_loop_regalloc_bc_claimed (set by dcc_loop_
 * regalloc.c's try_loop_regalloc_bc/try_loop_regalloc_bc_write right where
 * each commits) afterward to learn, empirically, whether any loop actually
 * claimed BC. If one did, this body is kept outright and the whole-function
 * mechanism never runs at all for this function. If not, this attempt is
 * discarded and rewound exactly like a failed speculative attempt anywhere
 * else in this file, and the existing chain proceeds unchanged - no loss
 * relative to today's behavior.
 *
 * Modeled directly on try_speculative_bc_regalloc_function_body's own
 * commit-or-full-rewind shape, just with nothing pre-claimed going in and a
 * different (empirical, not text-scanned) success condition. */
static int try_loop_scoped_regalloc_first(const char *name, int type,
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
    int c;
    int errors_before;

    if (strcmp(name, "main") == 0)
        return 0;

    scratch = tmpfile();
    if (scratch == NULL)
        fatal("cannot create speculative loop-scoped-first temp file");

    saved_outf_ptr = outf;
    saved_stack_check = opt_stack_check;
    outf = scratch;
    opt_stack_check = s->stack_check_enabled;
    g_inline_body_buffering++;
    g_buffering_epoch++;
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
    g_loop_regalloc_bc_claimed = 0;

    errors_before = g_diag_error_count;
    asm_suppress_depth++;
    emit_function_prologue(name, local_bytes, current_function_safe_to_omit_ix(type, local_bytes));
    gen_compound();
    emit_function_epilogue(strcmp(name, "main") == 0 &&
                            (type & 15) == TYPE_INT && type_ptr_depth(type) == 0);
    asm_suppress_depth--;
    g_inline_body_buffering--;
    opt_stack_check = saved_stack_check;
    outf = saved_outf_ptr;

    if (g_diag_error_count == errors_before && g_loop_regalloc_bc_claimed) {
        check_undefined_user_labels();
        if (plain_static_body_can_be_buffered(s, name)) {
            s->deferred_body_file = scratch;
        } else {
            rewind(scratch);
            while ((c = fgetc(scratch)) != EOF)
                fputc(c, outf);
            fclose(scratch);
        }
        return 1;
    }

    fclose(scratch);

    /* Undo every bit of per-function codegen state this discarded attempt
     * touched, exactly like try_speculative_bc_regalloc_function_body's own
     * rewind. */
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

/* Speculatively generate `name`'s already-scanned body with `bc_cand` (a
 * read-only pointer parameter, chosen ahead of time by find_bc_regalloc_
 * candidate - may be NULL) BC-resident, and/or a loop-counter local claimed
 * during the walk itself into E (via g_e_regalloc_claim_active, set here;
 * see gen_local_decl_after_type in dcc_decl.c), instead of occupying a frame
 * slot - and verifies/finalizes both via regalloc_buffer_finalize. Modeled directly on
 * try_speculative_noix_function_body: same tmpfile redirection, same g_
 * inline_body_buffering guard (required for the same EXTRN-dedup-cache-
 * desync reason), same commit-or-full-rewind discipline on failure. Unlike
 * the no-IX-frame optimization this stacks with a normal IX frame - only
 * the claimed candidates' own storage is affected, every other local/param
 * is addressed exactly as before. */
static int try_speculative_bc_regalloc_function_body(const char *name, int type,
                                                       int local_bytes, struct Sym *s,
                                                       struct Sym *bc_cand,
                                                       int attempt_e,
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
    int c;
    int errors_before;

    scratch = tmpfile();
    if (scratch == NULL)
        fatal("cannot create speculative bc-regalloc temp file");

    saved_outf_ptr = outf;
    saved_stack_check = opt_stack_check;
    outf = scratch;
    opt_stack_check = s->stack_check_enabled;
    g_inline_body_buffering++;
    g_buffering_epoch++;
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
    if (bc_cand != NULL) {
        bc_cand->reg_alloc = REG_BC;
        g_bc_regalloc_sym = bc_cand;
    }
    g_e_regalloc_claim_active = attempt_e ? 1 : 0;
    g_e_regalloc_claimed = 0;
    g_e_regalloc_sym = NULL;
    g_regalloc_address_escaped = 0;
    /* Same suppress-but-count discipline as try_speculative_noix_function_
     * body, and for the identical reason: a genuine source error must never
     * be silently swallowed just because this specific attempt happens to
     * pass the regalloc-only safety checks below. */
    errors_before = g_diag_error_count;
    asm_suppress_depth++;
    emit_function_prologue(name, local_bytes, current_function_safe_to_omit_ix(type, local_bytes));
    gen_compound();
    emit_function_epilogue(strcmp(name, "main") == 0 &&
                            (type & 15) == TYPE_INT && type_ptr_depth(type) == 0);
    asm_suppress_depth--;
    g_bc_regalloc_sym = NULL;
    /* Reset unconditionally here, right after codegen finishes, regardless
     * of whether this attempt goes on to succeed or get discarded below -
     * matching try_loop_regalloc_bc/_write's (dcc_loop_regalloc.c) own,
     * already-correct pattern. bc_cand's own reg_alloc field is only ever
     * consulted DURING gen_compound() above; leaving it set to REG_BC past
     * this point serves no purpose and is actively dangerous for a global
     * candidate specifically - unlike a local/param's Sym, which is
     * effectively discarded once this function's compilation moves on, a
     * global's Sym is the SAME, persistent object referenced by every other
     * function in the file. A stale REG_BC left here after a successful
     * commit (the old code only reset it on the discard path, taking the
     * `return 1` below without ever reaching it) meant every later,
     * unrelated function referencing that same global got silently
     * compiled as if it had its own live BC prime, reading whatever
     * leftover garbage happened to be in BC instead of the global's real
     * value - a real, confirmed miscompile (tests/pint.c interpreting
     * TTT.PAS: curproc read as garbage, then appearing to change value
     * across an unrelated call, purely from stale reg_alloc state left on
     * its Sym by an earlier, different function's successful whole-
     * function promotion of it). */
    if (bc_cand != NULL)
        bc_cand->reg_alloc = REG_NONE;
    g_e_regalloc_claim_active = 0;
    g_inline_body_buffering--;
    opt_stack_check = saved_stack_check;
    outf = saved_outf_ptr;

    /* Same rationale as try_speculative_noix_function_body's identical
     * comment: skip check_undefined_user_labels() here on a path that might
     * be discarded, to avoid double-reporting and stale nulabels state. */
    if (g_diag_error_count == errors_before &&
        !g_regalloc_address_escaped &&
        (bc_cand != NULL || g_e_regalloc_claimed)) {
        FILE *finalized = NULL;
        if (regalloc_buffer_finalize(scratch, bc_cand, g_e_regalloc_claimed ? g_e_regalloc_sym : NULL,
                                      &finalized)) {
            check_undefined_user_labels();
            fclose(scratch);
            while ((c = fgetc(finalized)) != EOF)
                fputc(c, outf);
            fclose(finalized);
            return 1;
        }
    }

    fclose(scratch);
    if (g_e_regalloc_claimed && g_e_regalloc_sym != NULL)
        g_e_regalloc_sym->reg_alloc = REG_NONE;
    g_e_regalloc_claimed = 0;
    g_e_regalloc_sym = NULL;

    /* Undo every bit of per-function codegen state this discarded attempt
     * touched, exactly like try_speculative_noix_function_body's own rewind. */
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

/* Wrapper around try_speculative_bc_regalloc_function_body that tries BC+E
 * together first, then falls back to BC-only if that combined attempt is
 * declined and a BC candidate exists - so a genuinely unsafe E-counter
 * candidate (e.g. tbig.c's fill_record: `i` used both as an array index
 * and, separately, as a long-arithmetic operand, with an intervening
 * long-load that clobbers e in between the two uses) never regresses the
 * already-safe BC win back to nothing. Each attempt is a fully independent
 * speculative generation with its own commit-or-rewind, so the retry is
 * exactly as safe as either attempt alone - the second one starts from the
 * identical rewound state the first one's failure already restored. */
static int try_speculative_bc_regalloc_with_e_fallback(const char *name, int type,
                                                        int local_bytes, struct Sym *s,
                                                        struct Sym *bc_cand,
                                                        long body_start_pos,
                                                        long body_start_tok_start,
                                                        int body_start_line,
                                                        int body_start_tok_line,
                                                        struct Token body_start_tok,
                                                        int body_start_nlocals,
                                                        int body_start_local_size)
{
    if (try_speculative_bc_regalloc_function_body(name, type, local_bytes, s, bc_cand, 1,
                                                   body_start_pos, body_start_tok_start,
                                                   body_start_line, body_start_tok_line,
                                                   body_start_tok, body_start_nlocals,
                                                   body_start_local_size))
        return 1;
    if (bc_cand == NULL)
        return 0;
    return try_speculative_bc_regalloc_function_body(name, type, local_bytes, s, bc_cand, 0,
                                                      body_start_pos, body_start_tok_start,
                                                      body_start_line, body_start_tok_line,
                                                      body_start_tok, body_start_nlocals,
                                                      body_start_local_size);
}

void parse_function_or_global(int base_type)
{
    int done;
    int base_is_volatile;
    int base_pointee_is_volatile;

    done = 0;
    base_is_volatile = decl_is_volatile;
    base_pointee_is_volatile = decl_pointee_is_volatile;

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
        struct Sym *bc_regalloc_cand;

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

        if (parse_funcptr_declarator(&type, name, sizeof(name))) {
            direct_funcptr_decl = 1;
            object_is_volatile = decl_is_volatile;
            pointee_is_volatile = decl_pointee_is_volatile;
        } else {
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
         *     typedef int fn_t(int);
         *     extern fn_t foo;
         * Treat this as a function declaration.  Pointer declarators such as
         * fn_t *fp have already cleared base_is_func_typedef above. */
        if (base_is_func_typedef && g_funcptr_decl_array_len == 0) {
            s = add_global(name, type, SC_FUNC);
            s->is_inline |= decl_is_inline;
            s->is_noreturn |= decl_is_noreturn;
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
            s->is_noreturn |= decl_is_noreturn;
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
                if (function_body_may_need_inline_temps())
                    reserve_inline_temp_locals();
                scan_function_body_ident_counts();
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

                current_return_type = type;
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
                bc_regalloc_cand = find_bc_regalloc_candidate(saved_nlocals);
                if (static_inline_body_can_be_buffered(s)) {
                    FILE *saved_outf;

                    s->deferred_body_file = tmpfile();
                    if (s->deferred_body_file == NULL)
                        fatal("cannot create deferred body temp file");
                    saved_outf = outf;
                    outf = s->deferred_body_file;
                    g_inline_body_buffering++;
                    g_buffering_epoch++;
                    emit_function_prologue(name, current_local_bytes, current_function_safe_to_omit_ix(type, current_local_bytes));
                    gen_compound();
                    check_undefined_user_labels();
                    emit_function_epilogue(0);
                    g_inline_body_buffering--;
                    outf = saved_outf;
                } else if (!opt_debug &&
                           function_qualifies_for_speculative_noix(name, current_local_bytes) &&
                           try_speculative_noix_function_body(name, type, current_local_bytes, s,
                                                               saved_pos, saved_tok_start, saved_line,
                                                               saved_tok_line, saved_tok,
                                                               saved_nlocals, saved_local_size)) {
                    /* No-IX-frame body already generated and written to outf
                     * inside try_speculative_noix_function_body. */
                } else if (!opt_debug &&
                           try_loop_scoped_regalloc_first(name, type, current_local_bytes, s,
                                                           saved_pos, saved_tok_start, saved_line,
                                                           saved_tok_line, saved_tok,
                                                           saved_nlocals, saved_local_size)) {
                    /* A loop inside the body claimed BC on its own - see
                     * try_loop_scoped_regalloc_first's header comment for why
                     * that's given priority over find_bc_regalloc_candidate's
                     * own, cruder whole-function candidate below. Body already
                     * generated and written to outf (or deferred). */
                } else if (!opt_debug && function_qualifies_for_speculative_regalloc(name) &&
                           try_speculative_bc_regalloc_with_e_fallback(name, type, current_local_bytes, s,
                                                                        bc_regalloc_cand,
                                                                        saved_pos, saved_tok_start, saved_line,
                                                                        saved_tok_line, saved_tok,
                                                                        saved_nlocals, saved_local_size)) {
                    /* BC/E-resident body already generated and written to
                     * outf inside try_speculative_bc_regalloc_function_body. */
                } else if (plain_static_body_can_be_buffered(s, name)) {
                    FILE *saved_outf;

                    s->deferred_body_file = tmpfile();
                    if (s->deferred_body_file == NULL)
                        fatal("cannot create deferred body temp file");
                    saved_outf = outf;
                    outf = s->deferred_body_file;
                    g_inline_body_buffering++;
                    g_buffering_epoch++;
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

            while (tok.kind == '[') {
                int d;
                next_token();
                if (tok.kind == ']') {
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

            if (decl_is_extern) {
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
            if (decl_is_static)
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

    while (tok.kind != TOK_EOF) {
        if (tok.kind == TOK_STATIC_ASSERT) {
            parse_static_assert_decl();
        } else if (tok.kind == TOK_TYPEDEF) {
            parse_typedef_decl();
        } else if (starts_type()) {
            int t;
            decl_is_extern = 0;
            decl_is_static = 0;
            decl_is_inline = 0;
            decl_is_noreturn = 0;
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
            decl_is_noreturn = 0;
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
            decl_is_noreturn = 0;
            decl_is_const = 0;
            parse_function_or_global(TYPE_INT);
        } else {
            error_here("external declaration expected");
            next_token();
        }
    }
}

