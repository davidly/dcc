/*
 * dcc_ast_build.c - function-local AST builder.
 *
 * dcc lowers function bodies through a function-local AST (see dcc_ast.h).
 *
 * ast_build_expr() is a recursive-descent expression parser that mirrors the
 * source grammar (including dcc's C99 conveniences -
 * it operates purely on the token stream, so for-init/mid-block expressions
 * and // comments are handled transparently by the shared lexer).  It builds
 * an AstNode tree from the current lexer position by calling next_token(); it
 * never emits, interns strings, or allocates symbols, so the only global state
 * it touches is the lexer cursor.  The AST emitter consumes the built tree;
 * unsupported shapes are compiler errors in normal codegen.
 */
#include "dcc.h"
#include "dcc_ast.h"
#include <stdlib.h>
#include <string.h>

int g_ast_build_enabled = 1;
struct AstArena g_ast_arena;

/* Separate arena for declaration-initializer expressions.  Kept distinct from
 * g_ast_arena so that building/emitting an initializer during
 * ast_emit_decl_span (which runs inside the surrounding AST statement walk)
 * never resets the arena still holding that walk's pending nodes. */
struct AstArena g_ast_init_arena;
struct AstArena g_ast_inline_arena;

static int ast_num_text_plain_decimal(const char *s)
{
    const char *p;
    int digits = 0;
    if (s == NULL || *s == '\0')
        return 0;
    for (p = s; *p != '\0'; ++p) {
        if ((*p == 'u' || *p == 'U') && p[1] == '\0')
            break;
        if (*p < '0' || *p > '9')
            return 0;
        digits++;
    }
    if (digits == 0)
        return 0;
    if (s[0] == '0' && digits > 1)
        return 0;
    return 1;
}

static struct FieldDef *ast_member_field_for_sizeof(const struct AstNode *n);

static int ast_expr_is_null_pointer_constant(const struct AstNode *n)
{
    if (n == NULL)
        return 0;
    if (n->kind == AST_INT_LIT && n->ival == 0)
        return 1;
    if (n->kind == AST_UNARY && n->op == '+')
        return ast_expr_is_null_pointer_constant(n->a);
    return 0;
}

static int ast_expr_is_array_decay(const struct AstNode *n)
{
    struct Sym *s;
    if (n == NULL)
        return 0;
    if (n->kind == AST_IDENT) {
        s = find_sym(n->sval);
        return s != NULL && s->is_array;
    }
    if (n->kind == AST_MEMBER) {
        struct FieldDef *fd = ast_member_field_for_sizeof(n);
        return fd != NULL && fd->is_array;
    }
    return 0;
}

static int ast_expr_is_function_designator(const struct AstNode *n)
{
    struct Sym *s;
    if (n == NULL || n->kind != AST_IDENT)
        return 0;
    s = find_sym(n->sval);
    return s != NULL && s->storage == SC_FUNC;
}

static struct FieldDef *ast_member_field_for_sizeof(const struct AstNode *n)
{
    struct Sym *s;
    int base_type = 0;
    int sid;

    if (n == NULL || n->kind != AST_MEMBER || n->sval == NULL || n->a == NULL)
        return NULL;
    if (n->op == TOK_ARROW)
        base_type = type_decay_ptr(ast_expr_type_for_sizeof(n->a));
    else
        base_type = ast_expr_type_for_sizeof(n->a);
    if (base_type == 0 && n->a->kind == AST_IDENT) {
        s = find_sym(n->a->sval);
        if (s != NULL)
            base_type = (n->op == TOK_ARROW) ? type_decay_ptr(s->type) : s->type;
    }
    sid = base_struct_id_from_type(base_type);
    return find_field_def(sid, n->sval);
}

int ast_expr_type_for_sizeof(const struct AstNode *n)
{
    struct Sym *s;
    int lt;
    int rt;
    struct FieldDef *fd;

    if (n == NULL)
        return 0;
    switch (n->kind) {
    case AST_INT_LIT:
        return n->type ? n->type : TYPE_INT;
    case AST_FLOAT_LIT:
        return TYPE_FLOAT;
    case AST_STR_LIT:
        return TYPE_CHAR | TYPE_PTR;
    case AST_IDENT:
        s = find_sym(n->sval);
        if (s == NULL)
            return TYPE_INT;
        return s->type;
    case AST_INDEX:
        lt = ast_expr_type_for_sizeof(n->a);
        if (n->a != NULL && n->a->kind == AST_IDENT) {
            s = find_sym(n->a->sval);
            if (s != NULL && s->is_array)
                return s->type;
        }
        if (type_ptr_depth(lt) > 0)
            return type_decay_ptr(lt);
        return lt;
    case AST_MEMBER:
        fd = ast_member_field_for_sizeof(n);
        if (fd == NULL)
            return 0;
        return fd->is_array ? fd->elem_type : fd->type;
    case AST_UNARY:
        lt = ast_expr_type_for_sizeof(n->a);
        if (n->op == '*') {
            lt = type_decay_ptr(lt);
            return (lt & 15) == TYPE_VOID ? TYPE_CHAR : lt;
        }
        if (n->op == '&')
            return type_add_ptr(lt);
        if (n->op == '!')
            return TYPE_INT;
        return promote_int_type(lt);
    case AST_POSTFIX:
        return ast_expr_type_for_sizeof(n->a);
    case AST_CAST:
        return n->type;
    case AST_BINARY:
        lt = ast_expr_type_for_sizeof(n->a);
        rt = ast_expr_type_for_sizeof(n->b);
        if (n->op == '<' || n->op == '>' || n->op == TOK_LE || n->op == TOK_GE ||
            n->op == TOK_EQ || n->op == TOK_NE)
            return TYPE_INT;
        if (n->op == TOK_SHL || n->op == TOK_SHR)
            return promote_int_type(lt);
        if ((n->op == '+' || n->op == '-') && type_ptr_depth(lt) > 0) {
            if (n->op == '-' && type_ptr_depth(rt) > 0)
                return TYPE_INT;
            return lt;
        }
        if (n->op == '+' && type_ptr_depth(rt) > 0)
            return rt;
        return common_arith_type(lt, rt);
    case AST_LOGAND:
    case AST_LOGOR:
        return TYPE_INT;
    case AST_ASSIGN:
        return ast_expr_type_for_sizeof(n->a);
    case AST_COND:
        lt = ast_expr_type_for_sizeof(n->b);
        rt = ast_expr_type_for_sizeof(n->c);
        if (type_is_float(lt) || type_is_float(rt))
            return TYPE_FLOAT;
        if (type_ptr_depth(lt) > 0)
            return lt;
        if (type_ptr_depth(rt) > 0)
            return rt;
        return common_arith_type(lt, rt);
    case AST_COMMA:
        return ast_expr_type_for_sizeof(n->b);
    case AST_CALL:
        if (n->a != NULL && n->a->kind == AST_IDENT) {
            s = find_sym(n->a->sval);
            if (s != NULL)
                return s->type;
        }
        return TYPE_INT;
    case AST_SIZEOF_EXPR:
    case AST_SIZEOF_TYPE:
        return TYPE_INT;
    default:
        return n->type;
    }
}

static int ast_index_root_and_count(const struct AstNode *n,
                                    const struct AstNode **root)
{
    int count = 0;

    while (n != NULL && n->kind == AST_INDEX) {
        ++count;
        n = n->a;
    }
    if (root != NULL)
        *root = n;
    return count;
}

static int ast_expr_is_array_row(const struct AstNode *n)
{
    const struct AstNode *root;
    int index_count;

    if (n == NULL || n->kind != AST_INDEX)
        return 0;
    index_count = ast_index_root_and_count(n, &root);
    if (root != NULL && root->kind == AST_IDENT) {
        struct Sym *s = find_sym(root->sval);
        if (s == NULL)
            return 0;
        if (s->is_array)
            return s->dim_count > index_count;
        return type_ptr_depth(s->type) > 0 && s->dim_count >= index_count;
    }
    if (root != NULL && root->kind == AST_MEMBER) {
        struct FieldDef *fd = ast_member_field_for_sizeof(root);
        return fd != NULL && fd->is_array && fd->dim_count > index_count;
    }
    return 0;
}

static int ast_expr_is_pointer_assignment_rhs(const struct AstNode *n)
{
    const struct AstNode *cast;
    const struct AstNode *call;
    if (n == NULL)
        return 0;
    if (n->kind == AST_UNARY && n->op == '*' && n->a != NULL &&
        n->a->kind == AST_CAST) {
        cast = n->a;
        call = cast->a;
        if (call != NULL && call->kind == AST_CALL && call->a != NULL &&
            call->a->kind == AST_IDENT && !strcmp(call->a->sval, "__va_arg") &&
            type_ptr_depth(type_decay_ptr(cast->type)) > 0)
            return 1;
    }
    if (ast_expr_is_null_pointer_constant(n))
        return 1;
    if (type_ptr_depth(ast_expr_type_for_sizeof(n)) > 0)
        return 1;
    if (ast_expr_is_array_decay(n) || ast_expr_is_array_row(n) ||
        ast_expr_is_function_designator(n))
        return 1;
    if (n->kind == AST_COND)
        return ast_expr_is_pointer_assignment_rhs(n->b) ||
               ast_expr_is_pointer_assignment_rhs(n->c);
    return 0;
}

int ast_sizeof_expr_value(const struct AstNode *n)
{
    struct Sym *s;
    struct FieldDef *fd;
    const struct AstNode *root;
    int index_count;
    int total;
    int t;

    if (n == NULL)
        return 0;
    switch (n->kind) {
    case AST_IDENT:
        s = find_sym(n->sval);
        if (s != NULL && s->is_array) {
            total = sym_array_total_elems(s);
            if (total <= 0)
                total = 1;
            return type_size(s->type) * total;
        }
        break;
    case AST_STR_LIT:
        return (int)strlen(n->sval) + 1;
    case AST_INDEX:
        index_count = ast_index_root_and_count(n, &root);
        if (root != NULL && root->kind == AST_IDENT) {
            s = find_sym(root->sval);
            if (s != NULL && s->is_array) {
                total = sym_array_elems_from_level(s, index_count);
                if (total <= 0)
                    total = 1;
                return type_size(s->type) * total;
            }
        }
        break;
    case AST_MEMBER:
        fd = ast_member_field_for_sizeof(n);
        if (fd != NULL && fd->is_array)
            return fd->elem_size * fd->array_len;
        break;
    default:
        break;
    }
    t = ast_expr_type_for_sizeof(n);
    return type_size(t);
}

struct Sym *ast_sizeof_whole_vla_sym(const struct AstNode *n)
{
    struct Sym *s;

    if (n == NULL || n->kind != AST_IDENT)
        return NULL;
    s = find_sym(n->sval);
    if (s == NULL || !s->is_vla)
        return NULL;
    return s;
}

static void ast_skip_braced_initializer(void)
{
    int depth;

    depth = 0;
    do {
        if (tok.kind == TOK_EOF)
            break;
        if (tok.kind == '{')
            depth++;
        else if (tok.kind == '}')
            depth--;
        next_token();
    } while (depth > 0);
}

static struct AstNode *ast_build_compound_literal(struct AstArena *ar, int type)
{
    struct AstNode *n;
    struct AstCompoundLitSpan *sp;

    sp = (struct AstCompoundLitSpan *)ast_arena_alloc(ar, sizeof(struct AstCompoundLitSpan));
    sp->posi = posi;
    sp->tok_start_pos = tok_start_pos;
    sp->line_no = line_no;
    sp->tok_line = tok_line;
    sp->tok = tok;

    n = ast_new(ar, AST_COMPOUND_LITERAL);
    n->type = type;
    n->sym = add_compound_literal_local(type);
    n->aux = sp;
    n->line = sp->tok_line;

    ast_skip_braced_initializer();
    return n;
}

/* Forward declarations (mutually recursive grammar). */
static struct AstNode *p_assign(struct AstArena *ar);

/* Copy the current token's text into the arena. */
static char *cur_text(struct AstArena *ar)
{
    return ast_arena_strdup(ar, tok.text);
}

static struct AstNode *p_primary(struct AstArena *ar)
{
    struct AstNode *n;

    switch (tok.kind) {
    case TOK_NUM:
    case TOK_CHARLIT:
        {
            long v = (tok.kind == TOK_NUM) ? (long)strtoul(tok.text, NULL, 0) : tok.val;
            int ty;
            /* Mirror gen_primary's literal classification so the codegen
             * walker can reproduce its emit exactly. */
            int is_long = (v > 0xffffL || v < -32768L ||
                           (tok.kind == TOK_NUM && g_tok_long_suffix));
            ty = is_long ? TYPE_LONG : TYPE_INT;
            if (tok.kind == TOK_NUM && g_tok_unsigned_suffix)
                ty |= TYPE_UNSIGNED;
            n = ast_int_lit(ar, v, ty);
            if (tok.kind == TOK_CHARLIT)
                n->uval = AST_INT_UVAL_CHARLIT;
            else if (ast_num_text_plain_decimal(tok.text))
                n->uval = AST_INT_UVAL_PLAIN_DECIMAL;
            next_token();
            return n;
        }
    case TOK_FLOATLIT:
        n = ast_float_lit(ar, parse_float_literal_bits(tok.text), TYPE_FLOAT);
        next_token();
        return n;
    case TOK_STR:
    case TOK_WSTR:
        {
            /* Concatenate adjacent string literals exactly like gen_primary;
             * this also advances the lexer past every piece.  Interning is a
             * codegen side effect, so it is deferred to the walker (the build
             * must stay free of codegen side effects); ival carries is_wide. */
            int is_wide = 0;
            char *lit = read_adjacent_string_literals_ex(&is_wide);
            n = ast_new(ar, AST_STR_LIT);
            n->sval = ast_arena_strdup(ar, lit);
            n->ival = is_wide;
            n->type = TYPE_CHAR | TYPE_PTR;
            free(lit);
            return n;
        }
    case TOK_ID:
        if (!strcmp(tok.text, "__offsetof")) {
            long v = parse_offsetof_value();
            return ast_int_lit(ar, v, TYPE_INT);
        }
        n = ast_new(ar, AST_IDENT);
        n->sval = cur_text(ar);
        /* read-only resolution; both lookups only scan existing tables */
        n->sym = find_local_decl(tok.text);
        if (n->sym == NULL)
            n->sym = find_global(tok.text);
        next_token();
        return n;
    case '(':
        next_token();
        n = ast_build_expr(ar);          /* comma operator allowed in parens */
        if (tok.kind == ')')
            next_token();
        return n;
    default:
        return NULL;
    }
}

static struct AstNode *p_postfix_tail(struct AstArena *ar, struct AstNode *n)
{
    for (;;) {
        if (tok.kind == '[') {
            struct AstNode *m = ast_new(ar, AST_INDEX);
            int base_type = ast_expr_type_for_sizeof(n);
            if (n != NULL && n->kind == AST_IDENT) {
                struct Sym *base_sym = find_sym(n->sval);
                if (base_sym != NULL && !base_sym->is_array && type_ptr_depth(base_type) == 0)
                    error_here("subscripted value is not an array or pointer");
            } else if (n != NULL && n->kind == AST_MEMBER && base_type != 0 &&
                       type_ptr_depth(base_type) == 0) {
                struct FieldDef *base_field = (n != NULL && n->kind == AST_MEMBER) ?
                    ast_member_field_for_sizeof(n) : NULL;
                if (base_field == NULL || !base_field->is_array)
                    error_here("subscripted value is not an array or pointer");
            }
            next_token();
            m->a = n;
            m->b = ast_build_expr(ar);
            if (tok.kind == ']')
                next_token();
            n = m;
        } else if (tok.kind == '(') {
            struct AstNode *call = ast_call(ar, n, 0);
            next_token();
            if (tok.kind != ')') {
                for (;;) {
                    struct AstNode *arg = p_assign(ar);
                    if (arg != NULL)
                        ast_list_push(ar, call, arg);
                    if (tok.kind == ',') {
                        next_token();
                        continue;
                    }
                    break;
                }
            }
            if (tok.kind == ')')
                next_token();
            if (n->kind == AST_IDENT && n->sym != NULL && n->sym->has_proto && !n->sym->proto_variadic) {
                if (call->list_len < n->sym->proto_nargs)
                    error_here("too few arguments to function call");
                else if (call->list_len > n->sym->proto_nargs)
                    error_here("too many arguments to function call");
            }
            n = call;
        } else if (tok.kind == '.' || tok.kind == TOK_ARROW) {
            struct AstNode *m = ast_new(ar, AST_MEMBER);
            m->op = tok.kind;
            m->a = n;
            next_token();
            if (tok.kind == TOK_ID) {
                m->sval = cur_text(ar);
                next_token();
            }
            n = m;
        } else if (tok.kind == TOK_INC || tok.kind == TOK_DEC) {
            struct AstNode *m = ast_new(ar, AST_POSTFIX);
            m->op = tok.kind;
            m->a = n;
            next_token();
            n = m;
        } else {
            break;
        }
    }
    return n;
}

static struct AstNode *p_postfix(struct AstArena *ar)
{
    struct AstNode *n = p_primary(ar);
    if (n == NULL)
        return NULL;
    return p_postfix_tail(ar, n);
}

static struct AstNode *p_unary(struct AstArena *ar)
{
    int k = tok.kind;

    if (k == '-' || k == '+' || k == '!' || k == '~' ||
        k == '*' || k == '&' || k == TOK_INC || k == TOK_DEC) {
        struct AstNode *operand;
        next_token();
        operand = p_unary(ar);
        return ast_unary(ar, k, operand, 0);
    }

    if (k == TOK_SIZEOF) {
        next_token();
        if (tok.kind == '(' && paren_starts_cast()) {
            struct AstNode *n = ast_new(ar, AST_SIZEOF_TYPE);
            int ty;
            int sz;
            next_token();
            parse_type_name_decl(&ty, &sz);
            expect(')');
            n->type = TYPE_INT;
            n->ival = sz;
            return n;
        } else {
            struct AstNode *n = ast_new(ar, AST_SIZEOF_EXPR);
            n->a = p_unary(ar);
            n->type = TYPE_INT;
            /* The operand's size is resolved at EMIT time (see
             * gen_sizeof_expr_ast): a local declared in a nested block only
             * enters the symbol table when its declaration span is emitted,
             * which is after this node is built but before it is walked. */
            return n;
        }
    }

    if (k == '(' && paren_starts_cast()) {
        struct AstNode *operand;
        int cty;
        int csz;
        next_token();                    /* consume '(' */
        parse_type_name_decl(&cty, &csz); /* parse ( type-name */
        expect(')');
        if (tok.kind == '{')
            return p_postfix_tail(ar, ast_build_compound_literal(ar, cty));
        operand = p_unary(ar);
        return ast_cast(ar, cty, operand);
    }

    return p_postfix(ar);
}

/* Binary precedence: higher binds tighter; 0 means "not a binary operator". */
static int binop_level(int k)
{
    switch (k) {
    case '*': case '/': case '%':       return 10;
    case '+': case '-':                 return 9;
    case TOK_SHL: case TOK_SHR:         return 8;
    case '<': case '>':
    case TOK_LE: case TOK_GE:           return 7;
    case TOK_EQ: case TOK_NE:           return 6;
    case '&':                           return 5;
    case '^':                           return 4;
    case '|':                           return 3;
    case TOK_ANDAND:                    return 2;
    case TOK_OROR:                      return 1;
    default:                            return 0;
    }
}

static struct AstNode *p_binary(struct AstArena *ar, int min_level)
{
    struct AstNode *lhs = p_unary(ar);
    if (lhs == NULL)
        return NULL;

    for (;;) {
        int k = tok.kind;
        int lev = binop_level(k);
        struct AstNode *rhs;
        int peek = 0;
        if (lev == 0 || lev < min_level)
            break;
        next_token();
        /*
         * For ordinary binary operators the arithmetic conversion branch is
         * selected from peek_simple_unary_type() taken at the RHS position.
         * The lexer is at exactly that position now, so capture that value into
         * the node; the AST walker reuses it to compute the common type.
         * && / || are short-circuit control flow and do not consult the peek.
         */
        if (k != TOK_ANDAND && k != TOK_OROR)
            peek = peek_simple_unary_type();
        rhs = p_binary(ar, lev + 1);     /* left-associative */
        if (k == TOK_ANDAND)
            lhs = ast_binary(ar, AST_LOGAND, k, lhs, rhs, 0);
        else if (k == TOK_OROR)
            lhs = ast_binary(ar, AST_LOGOR, k, lhs, rhs, 0);
        else {
            lhs = ast_binary(ar, AST_BINARY, k, lhs, rhs, 0);
            lhs->peek_type = peek;
        }
    }
    return lhs;
}

static struct AstNode *p_conditional(struct AstArena *ar)
{
    struct AstNode *c = p_binary(ar, 1);
    if (tok.kind == '?') {
        struct AstNode *then_e;
        struct AstNode *else_e;
        next_token();
        then_e = ast_build_expr(ar);     /* full expression before ':' */
        if (tok.kind == ':')
            next_token();
        else_e = p_conditional(ar);
        return ast_cond(ar, c, then_e, else_e, 0);
    }
    return c;
}

static int is_assign_op(int k)
{
    return k == '=' ||
           k == TOK_ADDEQ || k == TOK_SUBEQ || k == TOK_MULEQ ||
           k == TOK_DIVEQ || k == TOK_MODEQ || k == TOK_ANDEQ ||
           k == TOK_OREQ  || k == TOK_XOREQ || k == TOK_SHLEQ ||
           k == TOK_SHREQ;
}

static struct AstNode *p_assign(struct AstArena *ar)
{
    struct AstNode *lhs = p_conditional(ar);
    if (is_assign_op(tok.kind)) {
        int op = tok.kind;
        struct AstNode *rhs;
        next_token();
        rhs = p_assign(ar);              /* right-associative */
        if (op == '=' && type_ptr_depth(ast_expr_type_for_sizeof(lhs)) > 0 &&
            !ast_expr_is_pointer_assignment_rhs(rhs))
            error_here("incompatible integer to pointer assignment");
        return ast_assign(ar, op, lhs, rhs, 0);
    }
    return lhs;
}

struct AstNode *ast_build_expr(struct AstArena *ar)
{
    struct AstNode *n = p_assign(ar);
    while (tok.kind == ',') {
        struct AstNode *rhs;
        next_token();
        rhs = p_assign(ar);
        n = ast_binary(ar, AST_COMMA, ',', n, rhs, 0);
    }
    return n;
}

/* Build a single assignment-expression (no top-level comma operator).  Used for
 * declaration initializers, where a comma separates declarators rather than
 * acting as the comma operator. */
struct AstNode *ast_build_assign_expr(struct AstArena *ar)
{
    return p_assign(ar);
}

/* Build a `return [expr] ;` statement node.  The caller guarantees the current
 * token is TOK_RETURN.  Consumes the keyword, an optional expression, and the
 * terminating ';'.  Returns NULL (declining, lexer left mid-statement for the
 * caller to restore) if the closing ';' is missing. */
static struct AstNode *ast_build_return_stmt(struct AstArena *ar)
{
    struct AstNode *n;
    struct AstNode *val = NULL;

    next_token();                        /* consume 'return' */
    if (tok.kind != ';')
        val = ast_build_expr(ar);
    if (tok.kind != ';')
        return NULL;                     /* malformed: decline */
    next_token();                        /* consume ';' */

    n = ast_new(ar, AST_RETURN);
    n->a = val;
    return n;
}

/* Build a bare `break ;` / `continue ;` jump.  The caller guarantees the
 * leading keyword.  Declines (NULL) if the ';' is missing. */
static struct AstNode *ast_build_jump_stmt(struct AstArena *ar, int kind)
{
    next_token();                        /* consume 'break' / 'continue' */
    if (tok.kind != ';')
        return NULL;
    next_token();                        /* consume ';' */
    return ast_new(ar, kind);
}

/* Build `goto label ;`.  The caller guarantees the leading TOK_GOTO.  Stores
 * the label name in sval.  Declines (NULL) if the name or ';' is missing. */
static struct AstNode *ast_build_goto_stmt(struct AstArena *ar)
{
    struct AstNode *n;
    char *name;

    next_token();                        /* consume 'goto' */
    if (tok.kind != TOK_ID)
        return NULL;
    name = ast_arena_strdup(ar, tok.text);
    next_token();                        /* consume label name */
    if (tok.kind != ';')
        return NULL;
    next_token();                        /* consume ';' */

    n = ast_new(ar, AST_GOTO);
    n->sval = name;
    return n;
}

/* Build an ordinary expression statement `expr ;`.  The caller has positioned
 * the lexer at the start of the expression.  Declines (NULL) if the expression
 * is malformed or not terminated by ';' (the outer caller restores the lexer). */
static struct AstNode *ast_build_expr_stmt(struct AstArena *ar)
{
    struct AstNode *n;
    struct AstNode *e;

    e = ast_build_expr(ar);
    if (e == NULL || tok.kind != ';')
        return NULL;
    next_token();                        /* consume ';' */

    n = ast_new(ar, AST_EXPR_STMT);
    n->a = e;
    return n;
}

/* Build a labeled statement `name : stmt`.  The caller has verified the current
 * token is TOK_ID.  Peeks for the ':' ; if absent this is an ordinary
 * expression statement, so we rewind and build that instead. */
static struct AstNode *ast_build_label_stmt(struct AstArena *ar)
{
    struct AstNode *n;
    struct AstNode *body;
    char *name;
    long save_posi;
    long save_tok_start_pos;
    int save_line_no;
    int save_tok_line;
    struct Token save_tok;

    save_posi = posi;
    save_tok_start_pos = tok_start_pos;
    save_line_no = line_no;
    save_tok_line = tok_line;
    save_tok = tok;

    name = ast_arena_strdup(ar, tok.text);
    next_token();                        /* consume the identifier */
    if (tok.kind != ':') {
        /* Not a label: rewind to the identifier and build an expression
         * statement starting there. */
        posi = save_posi;
        tok_start_pos = save_tok_start_pos;
        line_no = save_line_no;
        tok_line = save_tok_line;
        tok = save_tok;
        return ast_build_expr_stmt(ar);
    }
    next_token();                        /* consume ':' */

    body = ast_build_stmt(ar);
    if (body == NULL)
        return NULL;                     /* unsupported labeled statement */

    n = ast_new(ar, AST_LABEL);
    n->sval = name;
    n->b = body;
    return n;
}

/* Build `if ( cond ) then [else else] `.  The caller guarantees TOK_IF.
 * Declines (NULL) on malformed syntax or when a branch statement is itself
 * unsupported, leaving the lexer for the outer caller to restore. */
static struct AstNode *ast_build_if_stmt(struct AstArena *ar)
{
    struct AstNode *n;
    struct AstNode *cond;
    struct AstNode *then_s;
    struct AstNode *else_s = NULL;

    next_token();                        /* consume 'if' */
    if (tok.kind != '(')
        return NULL;
    next_token();                        /* consume '(' */

    cond = ast_build_expr(ar);
    if (cond == NULL || tok.kind != ')')
        return NULL;
    next_token();                        /* consume ')' */

    then_s = ast_build_stmt(ar);
    if (then_s == NULL)
        return NULL;

    if (tok.kind == TOK_ELSE) {
        next_token();                    /* consume 'else' */
        else_s = ast_build_stmt(ar);
        if (else_s == NULL)
            return NULL;
    }

    n = ast_new(ar, AST_IF);
    n->a = cond;
    n->b = then_s;
    n->c = else_s;
    return n;
}

/* Build `while ( cond ) body`.  The caller guarantees TOK_WHILE.  Declines
 * (NULL) on malformed syntax or an unsupported body statement. */
static struct AstNode *ast_build_while_stmt(struct AstArena *ar)
{
    struct AstNode *n;
    struct AstNode *cond;
    struct AstNode *body;

    next_token();                        /* consume 'while' */
    if (tok.kind != '(')
        return NULL;
    next_token();                        /* consume '(' */

    cond = ast_build_expr(ar);
    if (cond == NULL || tok.kind != ')')
        return NULL;
    next_token();                        /* consume ')' */

    body = ast_build_stmt(ar);
    if (body == NULL)
        return NULL;

    n = ast_new(ar, AST_WHILE);
    n->a = cond;
    n->b = body;
    return n;
}

/* Build `do body while ( cond ) ;`.  The caller guarantees TOK_DO.  Declines
 * (NULL) on malformed syntax or an unsupported body statement. */
static struct AstNode *ast_build_do_stmt(struct AstArena *ar)
{
    struct AstNode *n;
    struct AstNode *cond;
    struct AstNode *body;

    next_token();                        /* consume 'do' */

    body = ast_build_stmt(ar);
    if (body == NULL)
        return NULL;

    if (tok.kind != TOK_WHILE)
        return NULL;
    next_token();                        /* consume 'while' */
    if (tok.kind != '(')
        return NULL;
    next_token();                        /* consume '(' */

    cond = ast_build_expr(ar);
    if (cond == NULL || tok.kind != ')')
        return NULL;
    next_token();                        /* consume ')' */
    if (tok.kind != ';')
        return NULL;
    next_token();                        /* consume ';' */

    n = ast_new(ar, AST_DOWHILE);
    n->a = cond;
    n->b = body;
    return n;
}

static struct AstNode *ast_build_case_stmt(struct AstArena *ar)
{
    struct AstNode *n;
    long cv;

    next_token();                        /* consume 'case' */
    cv = parse_const_long_expr();
    if (tok.kind != ':')
        return NULL;
    next_token();                        /* consume ':' */

    n = ast_new(ar, AST_CASE);
    n->ival = cv;
    n->b = ast_build_stmt(ar);
    if (n->b == NULL)
        return NULL;
    return n;
}

static struct AstNode *ast_build_default_stmt(struct AstArena *ar)
{
    struct AstNode *n;

    next_token();                        /* consume 'default' */
    if (tok.kind != ':')
        return NULL;
    next_token();                        /* consume ':' */

    n = ast_new(ar, AST_DEFAULT);
    n->b = ast_build_stmt(ar);
    if (n->b == NULL)
        return NULL;
    return n;
}

/* Build a narrow structural `for` slice: for ([init] ; [cond] ; [inc]) body.
 * A C99 for-init declaration is captured as an AST_DECL span; an expression
 * init is gated separately.  Increment is gated like a dead-result expression
 * statement so snippet fast-path shapes still defer. */
static struct AstNode *ast_build_decl_span(struct AstArena *ar);

static struct AstNode *ast_build_for_stmt(struct AstArena *ar)
{
    struct AstNode *n;
    struct AstNode *init = NULL;
    struct AstNode *cond = NULL;
    struct AstNode *inc = NULL;
    struct AstNode *body;

    next_token();                        /* consume 'for' */
    if (tok.kind != '(')
        return NULL;
    next_token();                        /* consume '(' */

    if (starts_type()) {
        /* C99 for-init declaration `for (int i = 0; ...)`: capture it as an
         * AST_DECL span (consumes through the first ';') and let
         * ast_gen_for_stmt replay it via declaration codegen and for-scope
         * rename machinery. */
        init = ast_build_decl_span(ar);
        if (init == NULL)
            return NULL;
    } else {
        if (tok.kind != ';')
            init = ast_build_expr(ar);
        if (tok.kind != ';')
            return NULL;
        next_token();                    /* consume first ';' */
    }

    if (tok.kind != ';')
        cond = ast_build_expr(ar);
    if (tok.kind != ';')
        return NULL;
    next_token();                        /* consume second ';' */

    if (tok.kind != ')')
        inc = ast_build_expr(ar);
    if (tok.kind != ')')
        return NULL;
    next_token();                        /* consume ')' */

    body = ast_build_stmt(ar);
    if (body == NULL)
        return NULL;

    n = ast_new(ar, AST_FOR);
    n->a = init;
    n->b = cond;
    n->c = inc;
    n->d = body;

    /* The cyclic-byte-fill fast path (ast_gen_for_stmt) needs a one-byte
     * frame slot for its rolling counter. Local frame layout is finalised
     * during this build pass (add_local_alloc grows the running frame size
     * as declarations/temporaries are encountered), before codegen emits the
     * function prologue - so the slot must be reserved here, not later at
     * codegen time. Stash the resulting Sym* on the node (AST_FOR does not
     * otherwise use n->sym) for ast_gen_for_stmt to pick up. */
    if (ast_for_mod_fill_supported(n, NULL, NULL, NULL, NULL, NULL))
        n->sym = add_local_alloc("#modfill", TYPE_CHAR | TYPE_UNSIGNED, 1);

    return n;
}

/* Build `switch (expr) statement`.  The statement may be a brace block or any
 * single statement; case/default labels may be nested inside the body, as C
 * permits.  Switch codegen scans the body recursively for labels. */
static struct AstNode *ast_build_switch_stmt(struct AstArena *ar)
{
    struct AstNode *n;
    struct AstNode *ctrl;
    struct AstNode *body;

    next_token();                        /* consume 'switch' */
    if (tok.kind != '(')
        return NULL;
    next_token();                        /* consume '(' */

    ctrl = ast_build_expr(ar);
    if (ctrl == NULL || tok.kind != ')')
        return NULL;
    next_token();                        /* consume ')' */

    body = ast_build_stmt(ar);
    if (body == NULL)
        return NULL;

    n = ast_new(ar, AST_SWITCH);
    n->a = ctrl;
    n->b = body;
    return n;
}

/* A captured local-declaration token span.  The AST records where a local
 * declaration begins so the emitter can re-seek the lexer and replay the
 * declaration parser/codegen (rebuilding locals[] / frame offsets exactly as
 * the frame-sizing scan did), instead of duplicating declaration lowering in
 * the AST walker. */
struct DeclSpan {
    long posi;
    long tok_start_pos;
    int line_no;
    int tok_line;
    struct Token tok;
};

/* Capture the declaration that begins at the current lexer position as an
 * AST_DECL span node and advance past it so sibling parsing can continue.  A
 * declaration ends at the first ';' seen at bracket depth zero; ( [ {
 * (initializers, struct/union bodies, function-pointer parens) nest and are
 * skipped wholesale.  String/char literals are single tokens, so an embedded
 * ';' never trips the scan.  Returns NULL on a truncated declaration. */
static struct AstNode *ast_build_decl_span(struct AstArena *ar)
{
    struct AstNode *n;
    struct DeclSpan *sp;
    int depth;

    sp = (struct DeclSpan *)ast_arena_alloc(ar, sizeof(struct DeclSpan));
    sp->posi = posi;
    sp->tok_start_pos = tok_start_pos;
    sp->line_no = line_no;
    sp->tok_line = tok_line;
    sp->tok = tok;

    depth = 0;
    for (;;) {
        if (tok.kind == TOK_EOF)
            return NULL;
        if (tok.kind == '(' || tok.kind == '[' || tok.kind == '{') {
            depth++;
        } else if (tok.kind == ')' || tok.kind == ']' || tok.kind == '}') {
            if (depth > 0)
                depth--;
        } else if (tok.kind == ';' && depth == 0) {
            next_token();                /* consume the terminating ';' */
            break;
        }
        next_token();
    }

    n = ast_new(ar, AST_DECL);
    n->aux = sp;
    n->line = sp->tok_line;
    return n;
}

/* Re-emit a captured local-declaration span (see ast_build_decl_span).  The
 * lexer is transiently re-seeked to the declaration, declaration codegen runs
 * (mirroring gen_compound's declaration branch exactly, so locals[] / frame
 * offsets match the frame-sizing scan), then the
 * lexer is restored - leaving no net cursor movement for the surrounding AST
 * walk. */
void ast_emit_decl_span(const struct AstNode *n)
{
    struct DeclSpan *sp = (struct DeclSpan *)n->aux;
    long sv_posi = posi;
    long sv_tok_start = tok_start_pos;
    int sv_line = line_no;
    int sv_tok_line = tok_line;
    struct Token sv_tok = tok;

    posi = sp->posi;
    tok_start_pos = sp->tok_start_pos;
    line_no = sp->line_no;
    tok_line = sp->tok_line;
    tok = sp->tok;

    /* Drive the declaration through the declaration codegen.  Initializer
     * expressions are emitted via ast_emit_init_expr, which builds into the
     * isolated g_ast_init_arena and so never disturbs the shared g_ast_arena
     * that still holds the surrounding AST statement's pending sibling nodes. */
    if (tok.kind == TOK_TYPEDEF) {
        parse_typedef_decl();
    } else {
        int t;
        int is_static_local;
        decl_is_extern = 0;
        is_static_local = (tok.kind == TOK_STATIC);
        t = parse_base_type();
        if (tok.kind == ';')
            next_token();
        else if (is_static_local)
            scan_static_local_decl_after_type(t);
        else
            gen_local_decl_after_type(t);
    }

    posi = sv_posi;
    tok_start_pos = sv_tok_start;
    line_no = sv_line;
    tok_line = sv_tok_line;
    tok = sv_tok;
}

/* Build a brace-delimited block `{ stmt* }`.  Local declarations (and
 * typedefs) are captured as AST_DECL span nodes that the emitter re-runs
 * through declaration codegen, so the symbol scope/frame the
 * frame-sizing scan built is reproduced exactly.  Declines (NULL) on any
 * unsupported child statement or a truncated declaration.  An empty block
 * `{}` is accepted (emits only a balanced enter/leave scope). */
static struct AstNode *ast_build_compound_stmt(struct AstArena *ar)
{
    struct AstNode *n;

    next_token();                        /* consume '{' */
    n = ast_new(ar, AST_COMPOUND);

    while (tok.kind != '}' && tok.kind != TOK_EOF) {
        struct AstNode *child;

        /* A typedef or any declaration is captured as a span and re-emitted
         * by declaration codegen at emit time (which rebuilds
         * locals[] / frame offsets identically to the frame-sizing scan). */
        if (tok.kind == TOK_TYPEDEF || starts_type()) {
            child = ast_build_decl_span(ar);
            if (child == NULL)
                return NULL;
            ast_list_push(ar, n, child);
            continue;
        }

        child = ast_build_stmt(ar);
        if (child == NULL)
            return NULL;
        ast_list_push(ar, n, child);
    }

    if (tok.kind != '}')
        return NULL;
    next_token();                        /* consume '}' */
    return n;
}

/* Build one statement from the current lexer position.  Returns NULL (without
 * committing to a partial parse for unrecognised leads).  In normal codegen a
 * NULL result is reported as an unsupported AST shape. */
struct AstNode *ast_build_stmt(struct AstArena *ar)
{
    switch (tok.kind) {
    case '{':          return ast_build_compound_stmt(ar);
    case ';':          next_token(); return ast_new(ar, AST_EMPTY);
    case TOK_RETURN:   return ast_build_return_stmt(ar);
    case TOK_BREAK:    return ast_build_jump_stmt(ar, AST_BREAK);
    case TOK_CONTINUE: return ast_build_jump_stmt(ar, AST_CONTINUE);
    case TOK_GOTO:     return ast_build_goto_stmt(ar);
    case TOK_IF:       return ast_build_if_stmt(ar);
    case TOK_WHILE:    return ast_build_while_stmt(ar);
    case TOK_DO:       return ast_build_do_stmt(ar);
    case TOK_FOR:      return ast_build_for_stmt(ar);
    case TOK_SWITCH:   return ast_build_switch_stmt(ar);
    case TOK_CASE:     return ast_build_case_stmt(ar);
    case TOK_DEFAULT:  return ast_build_default_stmt(ar);
    case TOK_ID:       return ast_build_label_stmt(ar);
    /* Expression statements that do not begin with an identifier: a deref
     * store `*p = x;`, a parenthesised expression `(expr);`, an address-of or
     * unary-led expression, or a prefix ++/-- statement.  ast_build_expr_stmt
     * declines (NULL) on anything that is not a complete `expr ;`, and the
     * outer ast_try_emit_statement restores the lexer snapshot on NULL, so
     * mis-routing a non-expression lead is harmless. */
    case '*': case '(': case '&': case '-': case '+': case '!': case '~':
    case TOK_INC: case TOK_DEC: case TOK_SIZEOF:
                       return ast_build_expr_stmt(ar);
    default:           return NULL;
    }
}

/* ------------------------------------------------------------------------- *
 * Initialisation + debug dump.
 * ------------------------------------------------------------------------- */
void ast_build_init(void)
{
    const char *e = getenv("DCC_AST_BUILD");
    g_ast_build_enabled = (e != NULL && e[0] == '2') ? 2 : 1;

    ast_arena_init(&g_ast_arena);
    ast_arena_init(&g_ast_init_arena);
    ast_arena_init(&g_ast_inline_arena);
}

const char *ast_kind_name(int kind)
{
    switch (kind) {
    case AST_NONE:        return "none";
    case AST_INT_LIT:     return "int";
    case AST_FLOAT_LIT:   return "float";
    case AST_STR_LIT:     return "str";
    case AST_IDENT:       return "ident";
    case AST_CALL:        return "call";
    case AST_INDEX:       return "index";
    case AST_MEMBER:      return "member";
    case AST_UNARY:       return "unary";
    case AST_POSTFIX:     return "postfix";
    case AST_BINARY:      return "binary";
    case AST_LOGAND:      return "logand";
    case AST_LOGOR:       return "logor";
    case AST_ASSIGN:      return "assign";
    case AST_COND:        return "cond";
    case AST_CAST:        return "cast";
    case AST_COMPOUND_LITERAL: return "compound-literal";
    case AST_COMMA:       return "comma";
    case AST_SIZEOF_EXPR: return "sizeof-expr";
    case AST_SIZEOF_TYPE: return "sizeof-type";
    case AST_EXPR_STMT:   return "expr-stmt";
    case AST_COMPOUND:    return "compound";
    case AST_DECL:        return "decl";
    case AST_IF:          return "if";
    case AST_WHILE:       return "while";
    case AST_DOWHILE:     return "do-while";
    case AST_FOR:         return "for";
    case AST_SWITCH:      return "switch";
    case AST_CASE:        return "case";
    case AST_DEFAULT:     return "default";
    case AST_RETURN:      return "return";
    case AST_BREAK:       return "break";
    case AST_CONTINUE:    return "continue";
    case AST_GOTO:        return "goto";
    case AST_LABEL:       return "label";
    case AST_EMPTY:       return "empty";
    default:              return "?";
    }
}

void ast_dump(const struct AstNode *n, int depth)
{
    int i;
    if (n == NULL) {
        for (i = 0; i < depth; i++)
            fputs("  ", stderr);
        fputs("<null>\n", stderr);
        return;
    }
    for (i = 0; i < depth; i++)
        fputs("  ", stderr);
    fprintf(stderr, "%s", ast_kind_name(n->kind));
    if (n->kind == AST_INT_LIT)
        fprintf(stderr, " %ld", n->ival);
    else if (n->kind == AST_FLOAT_LIT)
        fprintf(stderr, " 0x%08lx", n->uval);
    else if (n->sval != NULL)
        fprintf(stderr, " '%s'", n->sval);
    if (n->op != 0)
        fprintf(stderr, " op=%d", n->op);
    fputc('\n', stderr);

    ast_dump(n->a, depth + 1);
    ast_dump(n->b, depth + 1);
    ast_dump(n->c, depth + 1);
    ast_dump(n->d, depth + 1);
    for (i = 0; i < n->list_len; i++)
        ast_dump(n->list[i], depth + 1);
}
