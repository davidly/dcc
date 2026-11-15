/**
 * @file dcc_ast_sizeof.c
 * @brief Proven signedness demotion for sizeof-derived unsigned expressions.
 *
 * @par Role
 * `sizeof` yields `size_t` (unsigned int). Most uses - array-count loop
 * bounds, `memset(p, 0, sizeof buf)`, `sizeof(x) - 1` - never observe that
 * signedness, yet typing them unsigned would perturb every usual-arithmetic
 * conversion they take part in and with it the MIR shape the exact kernels
 * and schedules key on. This module runs on each full expression just before
 * MIR lowering and retypes a constant sizeof subterm (value in [0, 0x7fff])
 * to plain int exactly when the retyping is unobservable: every
 * sign-sensitive operation (ordered compare, `/`, `%`, `>>`, widening to
 * long/float) that would see the subterm has both operands proven to lie in
 * [0, 0x7fff], or already has a genuinely unsigned operand. Unproven
 * contexts keep the true unsigned typing.
 *
 * @par Key entry points
 * ast_sizeof_demote_expr(), ast_sizeof_reset_function(),
 * ast_sizeof_reset_stmt(), ast_sizeof_note_decl(),
 * ast_sizeof_loop_push()/ast_sizeof_loop_pop()/ast_sizeof_loop_set_header().
 *
 * @par Boundary
 * Pure AST metadata refinement: it mutates node types only toward int, never
 * emits MIR, and never consults application or function names. Range facts
 * come from constants, narrow unsigned types, and a structural counting-loop
 * proof; dcc_ast_build.c owns the declaration-span facts it consumes.
 */

#include "dcc.h"
#include "dcc_ast.h"
#include "dcc_ast_internal.h"
#include <string.h>

#define SZ_LIMIT 0x7fffL
#define SZ_MAX_LOOPS 32
#define SZ_MAX_ADDR_NAMES 96

enum SzKind {
    SZ_KIND_UNKNOWN,
    SZ_KIND_SIZEOF,     /* unsigned solely through a constant sizeof */
    SZ_KIND_SIGNED16,   /* int, or char/short/bool promoted to int */
    SZ_KIND_UNSIGNED16, /* genuinely unsigned int */
    SZ_KIND_WIDE,       /* long or float */
    SZ_KIND_POINTER
};

struct SzLoop {
    char name[64];
    const struct Sym *sym;  /* NULL for a for-init declaration counter */
    int header;             /* 1 while lowering init/cond/post */
    int body_safe;          /* no body declaration mentions the counter */
};

static struct SzLoop sz_loops[SZ_MAX_LOOPS];
static int sz_loop_depth;

/* ------------------------------------------------------------------------- *
 * Function-wide facts
 * ------------------------------------------------------------------------- */

#define SZ_MAX_NOTED_DECLS 64

/* Declaration spans lowered so far in the current top-level statement. Their
 * objects are not yet in the symbol table when a nested for-loop is lowered,
 * so a counter they may declare cannot be trusted to the outer binding
 * find_local() would return. */
static const struct AstNode *sz_noted_decls[SZ_MAX_NOTED_DECLS];
static int sz_noted_count;
static int sz_noted_overflow;

void ast_sizeof_reset_function(void)
{
    sz_loop_depth = 0;
    ast_sizeof_reset_stmt();
}

void ast_sizeof_reset_stmt(void)
{
    sz_noted_count = 0;
    sz_noted_overflow = 0;
}

void ast_sizeof_note_decl(const struct AstNode *decl)
{
    if (sz_noted_count < SZ_MAX_NOTED_DECLS)
        sz_noted_decls[sz_noted_count++] = decl;
    else
        sz_noted_overflow = 1;
}

static int sz_noted_decl_declares(const char *name)
{
    int i;

    if (sz_noted_overflow)
        return 1;
    for (i = 0; i < sz_noted_count; ++i)
        if (ast_decl_span_declares_name(sz_noted_decls[i], name))
            return 1;
    return 0;
}

/* Declared scalar type of an identifier whose declaration is a nested span
 * not yet replayed into the symbol table: known only when exactly one noted
 * span declares the name and does so as a simple scalar. */
static int sz_deferred_ident_type(const struct AstNode *n)
{
    int i;
    int type = 0;
    int declarers = 0;

    if (n == NULL || n->kind != AST_IDENT || n->sval == NULL || sz_noted_overflow)
        return 0;
    for (i = 0; i < sz_noted_count; ++i) {
        if (!ast_decl_span_declares_name(sz_noted_decls[i], n->sval))
            continue;
        if (++declarers > 1)
            return 0;
        type = ast_decl_span_simple_type(sz_noted_decls[i], n->sval);
    }
    return declarers == 1 ? type : 0;
}

static int sz_name_address_taken(const char *name)
{
    return local_name_address_taken_or_unknown(name);
}

/* ------------------------------------------------------------------------- *
 * Constant evaluation and range facts
 * ------------------------------------------------------------------------- */

static int sz_const_value(const struct AstNode *n, long *out);

static int sz_sizeof_value(const struct AstNode *n, long *out)
{
    long value;

    if (n->kind == AST_SIZEOF_TYPE)
        value = n->ival;
    else if (n->kind == AST_SIZEOF_EXPR) {
        if (n->a == NULL || ast_sizeof_whole_vla_sym(n->a) != NULL)
            return 0;
        if (n->a->kind == AST_IDENT && n->a->sym == NULL &&
            n->a->sval != NULL &&
            find_sym(resolve_local_rename(n->a->sval)) == NULL) {
            /* The operand's declaration span is not replayed yet, so only a
             * noted simple scalar declaration has a trustworthy size. */
            int type = sz_deferred_ident_type(n->a);
            if (type == 0)
                return 0;
            value = type_size(type);
        } else
            value = ast_sizeof_expr_value(n->a);
    } else
        return 0;
    if (value <= 0 || value > SZ_LIMIT)
        return 0;
    *out = value;
    return 1;
}

/* A sizeof whose operand is a whole VLA, or a function-local object whose
 * declaration span is not replayed yet, has no compile-time value here, but
 * both live in the C stack, so the result is bounded by the reserved stack
 * size: a non-negative 16-bit value whenever that reservation fits. */
static int sz_sizeof_stack_bounded(const struct AstNode *n)
{
    const struct AstNode *operand;

    if (n == NULL || n->kind != AST_SIZEOF_EXPR || (operand = n->a) == NULL ||
        opt_stack_size <= 0 || opt_stack_size > SZ_LIMIT)
        return 0;
    if (ast_sizeof_whole_vla_sym(operand) != NULL)
        return 1;
    return operand->kind == AST_IDENT && operand->sym == NULL &&
           operand->sval != NULL &&
           find_sym(resolve_local_rename(operand->sval)) == NULL &&
           !sz_noted_overflow && sz_noted_decl_declares(operand->sval);
}

static int sz_const_binary(int op, long a, long b, long *out)
{
    switch (op) {
    case '+': *out = (a + b) & 0xffffL; return 1;
    case '-': *out = (a - b) & 0xffffL; return 1;
    case '*': *out = (a * b) & 0xffffL; return 1;
    case '&': *out = a & b; return 1;
    case '|': *out = a | b; return 1;
    case '^': *out = a ^ b; return 1;
    case TOK_SHL:
        if (b < 0 || b > 15)
            return 0;
        *out = (a << b) & 0xffffL;
        return 1;
    case '/':
    case '%':
    case TOK_SHR:
        /* Signed and unsigned readings agree only below the sign bit. */
        if (a > SZ_LIMIT || b > SZ_LIMIT || b <= 0)
            return 0;
        *out = op == '/' ? a / b : op == '%' ? a % b : a >> b;
        return 1;
    default:
        return 0;
    }
}

static int sz_const_value(const struct AstNode *n, long *out)
{
    long a;
    long b;

    if (n == NULL)
        return 0;
    switch (n->kind) {
    case AST_INT_LIT:
        if (type_ptr_depth(n->type) > 0 || (n->type & 15) == TYPE_LONG)
            return 0;
        *out = n->ival & 0xffffL;
        return 1;
    case AST_SIZEOF_TYPE:
    case AST_SIZEOF_EXPR:
        return sz_sizeof_value(n, out);
    case AST_UNARY:
        if (!sz_const_value(n->a, &a))
            return 0;
        if (n->op == '-') { *out = (-a) & 0xffffL; return 1; }
        if (n->op == '~') { *out = (~a) & 0xffffL; return 1; }
        if (n->op == '+') { *out = a; return 1; }
        return 0;
    case AST_BINARY:
        if (!sz_const_value(n->a, &a) || !sz_const_value(n->b, &b))
            return 0;
        return sz_const_binary(n->op, a, b, out);
    case AST_CAST:
        if (type_ptr_depth(n->type) > 0 ||
            ((n->type & 15) != TYPE_INT && (n->type & 15) != TYPE_CHAR))
            return 0;
        if (!sz_const_value(n->a, &a))
            return 0;
        *out = (n->type & 15) == TYPE_CHAR
            ? ((n->type & TYPE_UNSIGNED) ? (a & 0xff)
                                         : (((a & 0xff) ^ 0x80) - 0x80) & 0xffffL)
            : a;
        return 1;
    default:
        return 0;
    }
}

static int sz_ident_is_counter(const struct AstNode *n)
{
    int i;
    const struct Sym *sym;

    if (n->kind != AST_IDENT || n->sval == NULL)
        return 0;
    for (i = sz_loop_depth - 1; i >= 0; --i) {
        const struct SzLoop *loop = &sz_loops[i];
        if (strcmp(loop->name, n->sval) != 0)
            continue;
        if (!loop->header && !loop->body_safe)
            return 0;
        if (loop->sym == NULL)
            return 1;
        sym = find_local(n->sval);
        return sym == loop->sym;
    }
    return 0;
}

/* Is n's value proven to lie in [0, 0x7fff], independent of how its
 * sizeof-derived subterms are typed? */
static int sz_nonneg(const struct AstNode *n)
{
    long value;
    int type;

    if (n == NULL)
        return 0;
    if (sz_const_value(n, &value))
        return value <= SZ_LIMIT;
    if (sz_sizeof_stack_bounded(n))
        return 1;
    if (n->kind == AST_MEMBER) {
        const struct FieldDef *field = ast_member_field_for_sizeof(n);
        if (field != NULL && field->bit_width > 0)
            return (field->type & TYPE_UNSIGNED) != 0 && field->bit_width <= 15;
    }
    type = ast_types_deferred(n) ? sz_deferred_ident_type(n)
                                 : ast_expr_type_for_sizeof(n);
    if (type_ptr_depth(type) == 0 && !type_is_struct_object(type) &&
        (((type & 15) == TYPE_CHAR && (type & TYPE_UNSIGNED) != 0) ||
         (type & 15) == TYPE_BOOL) &&
        n->kind != AST_UNARY && n->kind != AST_BINARY)
        return 1;
    switch (n->kind) {
    case AST_IDENT:
        return sz_ident_is_counter(n);
    case AST_CAST:
        if (type_ptr_depth(n->type) > 0 || (n->type & 15) != TYPE_INT)
            return 0;
        return sz_nonneg(n->a);
    case AST_UNARY:
        return n->op == '!';
    case AST_LOGAND:
    case AST_LOGOR:
        return 1;
    case AST_COND:
        return sz_nonneg(n->b) && sz_nonneg(n->c);
    case AST_BINARY:
        switch (n->op) {
        case '<': case '>': case TOK_LE: case TOK_GE: case TOK_EQ: case TOK_NE:
            return 1;
        case '&':
            return sz_nonneg(n->a) || sz_nonneg(n->b);
        case TOK_SHR:
            return sz_nonneg(n->a);
        case '/':
        case '%':
            return sz_nonneg(n->a) && sz_nonneg(n->b);
        default:
            return 0;
        }
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------------- *
 * Operand classification
 * ------------------------------------------------------------------------- */

static int sz_is_sizeof_origin(const struct AstNode *n);

static enum SzKind sz_kind(const struct AstNode *n)
{
    int type;

    if (n == NULL)
        return SZ_KIND_UNKNOWN;
    if (sz_is_sizeof_origin(n))
        return SZ_KIND_SIZEOF;
    if (ast_types_deferred(n)) {
        type = sz_deferred_ident_type(n);
        if (type == 0)
            return SZ_KIND_UNKNOWN;
        type = promote_int_type(type);
    } else if (n->kind == AST_CALL && ast_expr_type_for_sizeof(n) == 0)
        return SZ_KIND_UNKNOWN;
    else
        type = ast_promoted_operand_type(n);
    if (type == 0 || type_is_struct_object(type))
        return SZ_KIND_UNKNOWN;
    if (type_ptr_depth(type) > 0)
        return SZ_KIND_POINTER;
    if ((type & 15) == TYPE_LONG || (type & 15) == TYPE_FLOAT)
        return SZ_KIND_WIDE;
    if ((type & TYPE_UNSIGNED) != 0)
        return SZ_KIND_UNSIGNED16;
    return SZ_KIND_SIGNED16;
}

/* An operand that is, or may turn out to be, a plain int: an unresolved
 * nested-block local is assumed int by the parser and retyped by MIR repair,
 * so a sizeof meeting it must still be treated as reaching the consumer. */
static int sz_kind_is_int16(enum SzKind kind)
{
    return kind == SZ_KIND_SIZEOF || kind == SZ_KIND_SIGNED16 ||
           kind == SZ_KIND_UNKNOWN;
}

/* Does n owe an unsigned int type solely to still-unsigned constant sizeof
 * subterms (so that demoting those would make it plain int)? */
static int sz_is_sizeof_origin(const struct AstNode *n)
{
    long value;

    if (n == NULL || (n->type & TYPE_UNSIGNED) == 0)
        return 0;
    switch (n->kind) {
    case AST_SIZEOF_TYPE:
    case AST_SIZEOF_EXPR:
        return sz_sizeof_value(n, &value) || sz_sizeof_stack_bounded(n);
    case AST_UNARY:
        return (n->op == '-' || n->op == '~' || n->op == '+') &&
               sz_is_sizeof_origin(n->a);
    case AST_BINARY:
        if (type_ptr_depth(n->type) > 0 || (n->type & 15) != TYPE_INT)
            return 0;
        if (n->op == TOK_SHL || n->op == TOK_SHR)
            return sz_is_sizeof_origin(n->a);
        {
            enum SzKind a = sz_kind(n->a);
            enum SzKind b = sz_kind(n->b);
            return sz_kind_is_int16(a) && sz_kind_is_int16(b) &&
                   (a == SZ_KIND_SIZEOF || b == SZ_KIND_SIZEOF);
        }
    case AST_COND:
        {
            enum SzKind b = sz_kind(n->b);
            enum SzKind c = sz_kind(n->c);
            return sz_kind_is_int16(b) && sz_kind_is_int16(c) &&
                   (b == SZ_KIND_SIZEOF || c == SZ_KIND_SIZEOF);
        }
    default:
        return 0;
    }
}

static int sz_op_is_sensitive(int op)
{
    return op == '<' || op == '>' || op == TOK_LE || op == TOK_GE ||
           op == '/' || op == '%';
}

static int sz_compound_operator(int op)
{
    switch (op) {
    case TOK_ADDEQ: return '+';
    case TOK_SUBEQ: return '-';
    case TOK_MULEQ: return '*';
    case TOK_DIVEQ: return '/';
    case TOK_MODEQ: return '%';
    case TOK_ANDEQ: return '&';
    case TOK_OREQ: return '|';
    case TOK_XOREQ: return '^';
    case TOK_SHLEQ: return TOK_SHL;
    case TOK_SHREQ: return TOK_SHR;
    default: return 0;
    }
}

/* May the sizeof-origin operand `self` of operator `op` be typed int given
 * the other operand's kind? Insensitive operators only need a widening
 * check; sensitive ones need both operands proven non-negative unless the
 * other is genuinely unsigned (then the operation is unsigned regardless). */
static int sz_operand_allowed(int op, const struct AstNode *self,
                              const struct AstNode *other, enum SzKind other_kind,
                              int self_is_left)
{
    int sensitive = sz_op_is_sensitive(op) ||
                    (op == TOK_SHR && self_is_left);

    if (op == TOK_SHL || op == TOK_SHR) {
        if (!self_is_left)
            return 1;   /* a shift count is used modulo the width */
        return !sensitive || sz_nonneg(self);
    }
    switch (other_kind) {
    case SZ_KIND_UNSIGNED16:
    case SZ_KIND_POINTER:
        return 1;
    case SZ_KIND_SIGNED16:
    case SZ_KIND_SIZEOF:
        return !sensitive || (sz_nonneg(self) && sz_nonneg(other));
    case SZ_KIND_WIDE:
        return sz_nonneg(self);
    default:
        return !sensitive && sz_nonneg(self);
    }
}

/* Conversion of a sizeof-origin value to `target`: narrow/equal widths keep
 * the bit pattern; wider or unknown targets need the value proven. */
static int sz_target_allows(int target, const struct AstNode *value)
{
    if (target == 0 || type_is_struct_object(target))
        return sz_nonneg(value);
    if (type_ptr_depth(target) > 0)
        return 1;
    if ((target & 15) == TYPE_LONG || (target & 15) == TYPE_FLOAT)
        return sz_nonneg(value);
    return 1;
}

static int sz_call_param_type(const struct AstNode *call, int index)
{
    const struct Sym *fn = NULL;

    if (call->a != NULL && call->a->kind == AST_IDENT) {
        fn = call->a->sym != NULL && call->a->sym->storage == SC_FUNC
            ? call->a->sym : find_global(call->a->sval);
        if (fn != NULL && fn->storage != SC_FUNC)
            fn = NULL;
    }
    if (fn == NULL)
        fn = ast_indirect_call_proto_sym(call);
    if (fn == NULL || !fn->has_proto || index >= fn->proto_nargs)
        return TYPE_INT;   /* default argument promotion: 16-bit */
    return fn->proto_types[index];
}

/* ------------------------------------------------------------------------- *
 * Top-down demotion
 * ------------------------------------------------------------------------- */

static void sz_visit(struct AstNode *n, int allow);

static void sz_visit_children(struct AstNode *n, int allow)
{
    int i;

    sz_visit(n->a, allow);
    sz_visit(n->b, allow);
    sz_visit(n->c, allow);
    sz_visit(n->d, allow);
    for (i = 0; i < n->list_len; ++i)
        sz_visit(n->list[i], allow);
}

/* Does a still-unsigned sizeof child feed n's operand type?  Only then may
 * the type be recomputed after the children are visited: the recomputation
 * mirrors the parser's rule and stays unsigned if the child was denied. */
static int sz_operands_from_sizeof(const struct AstNode *n)
{
    if (n->operand_type != (TYPE_INT | TYPE_UNSIGNED))
        return 0;
    if (sz_kind(n->a) == SZ_KIND_SIZEOF)
        return 1;
    return n->op != TOK_SHL && n->op != TOK_SHR &&
           sz_kind(n->b) == SZ_KIND_SIZEOF;
}

static void sz_retype_binary(struct AstNode *n)
{
    int left;
    int right;
    int operand_type;

    /* Unresolved operands promote to int here exactly as the parser assumed;
     * MIR repair retypes the operation once the declaration is replayed. */
    left = ast_promoted_operand_type(n->a);
    right = ast_promoted_operand_type(n->b);
    if (n->op == TOK_SHL || n->op == TOK_SHR)
        operand_type = left;
    else
        operand_type = common_arith_type(left, right);
    if (operand_type != TYPE_INT)
        return;
    n->operand_type = TYPE_INT;
    if (n->type == (TYPE_INT | TYPE_UNSIGNED))
        n->type = TYPE_INT;
}

static void sz_visit_binary(struct AstNode *n, int allow)
{
    enum SzKind a = sz_kind(n->a);
    enum SzKind b = sz_kind(n->b);
    int retype = sz_operands_from_sizeof(n);
    int allow_a = allow;
    int allow_b = allow;

    if (a == SZ_KIND_SIZEOF)
        allow_a = allow && sz_operand_allowed(n->op, n->a, n->b, b, 1);
    if (b == SZ_KIND_SIZEOF)
        allow_b = allow && sz_operand_allowed(n->op, n->b, n->a, a, 0);
    /* A sign-sensitive operation on two sizeof-origin operands must demote
     * both or neither: a mixed result would be unsigned anyway. */
    if (a == SZ_KIND_SIZEOF && b == SZ_KIND_SIZEOF && allow_a != allow_b)
        allow_a = allow_b = 0;
    /* A non-sizeof operand carries no demotable sizeof whose typing could
     * still reach a sensitive operation here; it is only visited for the
     * sizeof terms nested inside it, which see their own contexts. */
    if (a != SZ_KIND_SIZEOF)
        allow_a = 1;
    if (b != SZ_KIND_SIZEOF)
        allow_b = 1;
    sz_visit(n->a, allow_a);
    sz_visit(n->b, allow_b);
    if (retype)
        sz_retype_binary(n);
}

static void sz_visit_assign(struct AstNode *n, int allow)
{
    int op = sz_compound_operator(n->op);
    enum SzKind rhs = sz_kind(n->b);
    int allow_rhs = 1;
    int target;

    (void)allow;
    sz_visit(n->a, 1);
    if (rhs == SZ_KIND_SIZEOF) {
        target = ast_expr_type_for_sizeof(n->a);
        if (ast_types_deferred(n->a))
            target = 0;
        if (op == 0)
            allow_rhs = sz_target_allows(target, n->b);
        else if (op == TOK_SHL || op == TOK_SHR)
            allow_rhs = 1;
        else {
            enum SzKind lhs = sz_kind(n->a);
            allow_rhs = sz_operand_allowed(op, n->b, n->a, lhs, 0);
        }
    }
    sz_visit(n->b, allow_rhs);
}

static void sz_visit_cond(struct AstNode *n, int allow)
{
    enum SzKind b = sz_kind(n->b);
    enum SzKind c = sz_kind(n->c);
    int retype = b == SZ_KIND_SIZEOF || c == SZ_KIND_SIZEOF;
    int allow_b = 1;
    int allow_c = 1;

    sz_visit(n->a, 1);
    if (b == SZ_KIND_SIZEOF)
        allow_b = allow && (c == SZ_KIND_WIDE || c == SZ_KIND_UNKNOWN
                                ? sz_nonneg(n->b) : 1);
    if (c == SZ_KIND_SIZEOF)
        allow_c = allow && (b == SZ_KIND_WIDE || b == SZ_KIND_UNKNOWN
                                ? sz_nonneg(n->c) : 1);
    if (b == SZ_KIND_SIZEOF && c == SZ_KIND_SIZEOF && allow_b != allow_c)
        allow_b = allow_c = 0;
    sz_visit(n->b, allow_b);
    sz_visit(n->c, allow_c);
    if (retype && n->type == (TYPE_INT | TYPE_UNSIGNED) &&
        common_arith_type(ast_promoted_operand_type(n->b),
                          ast_promoted_operand_type(n->c)) == TYPE_INT)
        n->type = TYPE_INT;
}

static void sz_visit_call(struct AstNode *n)
{
    int i;

    sz_visit(n->a, 1);
    for (i = 0; i < n->list_len; ++i) {
        struct AstNode *argument = n->list[i];
        int allow = 1;
        if (sz_kind(argument) == SZ_KIND_SIZEOF)
            allow = sz_target_allows(sz_call_param_type(n, i), argument);
        sz_visit(argument, allow);
    }
}

static void sz_visit(struct AstNode *n, int allow)
{
    if (n == NULL)
        return;
    switch (n->kind) {
    case AST_SIZEOF_TYPE:
    case AST_SIZEOF_EXPR:
        /* The operand is unevaluated; only the result type is at stake. */
        if (allow && sz_is_sizeof_origin(n))
            n->type = TYPE_INT;
        return;
    case AST_UNARY:
        if (n->op == '-' || n->op == '~' || n->op == '+') {
            int origin = sz_is_sizeof_origin(n);
            sz_visit(n->a, origin ? allow : 1);
            if (origin && allow && sz_kind(n->a) == SZ_KIND_SIGNED16)
                n->type = TYPE_INT;
            return;
        }
        sz_visit(n->a, 1);
        return;
    case AST_BINARY:
        if (!sz_is_sizeof_origin(n) || allow)
            sz_visit_binary(n, 1);
        /* else: a denied sizeof-origin result stays unsigned throughout. */
        return;
    case AST_ASSIGN:
        sz_visit_assign(n, allow);
        return;
    case AST_COND:
        if (!sz_is_sizeof_origin(n) || allow)
            sz_visit_cond(n, 1);
        return;
    case AST_CALL:
        sz_visit_call(n);
        return;
    case AST_CAST:
        sz_visit(n->a, sz_kind(n->a) == SZ_KIND_SIZEOF
                           ? sz_target_allows(n->type, n->a) : 1);
        return;
    case AST_COMMA:
        sz_visit(n->a, 1);
        sz_visit(n->b, allow);
        return;
    case AST_COMPOUND_LITERAL:
        {
            int i;
            for (i = 0; i < n->list_len; ++i)
                sz_visit(n->list[i], sz_nonneg(n->list[i]));
            sz_visit(n->a, 1);
        }
        return;
    default:
        /* Index, member, postfix, logical, literals: 16-bit or zero-test
         * consumers, so nested sizeof terms are free to demote. */
        sz_visit_children(n, 1);
        return;
    }
}

void ast_sizeof_demote_expr(const struct AstNode *root, int target_type)
{
    /* AST nodes are arena-owned function-local metadata; this pass refines
     * their recorded types before lowering reads them. */
    struct AstNode *n = (struct AstNode *)root;
    int allow = 1;

    if (n == NULL)
        return;
    if (sz_kind(n) == SZ_KIND_SIZEOF)
        allow = sz_target_allows(target_type, n);
    sz_visit(n, allow);
}

/* ------------------------------------------------------------------------- *
 * Counting-loop range proof
 * ------------------------------------------------------------------------- */

static const struct AstNode *sz_strip_sign(const struct AstNode *n)
{
    while (n != NULL && n->kind == AST_UNARY && n->op == '+')
        n = n->a;
    return n;
}

static int sz_is_name(const struct AstNode *n, const char *name)
{
    return n != NULL && n->kind == AST_IDENT && n->sval != NULL &&
           strcmp(n->sval, name) == 0;
}

/* `name = K` possibly chained (`r = name = K`); K a non-negative constant. */
static int sz_for_init(const struct AstNode *init, const char *name, long *out)
{
    if (init == NULL || init->kind != AST_ASSIGN || init->op != '=')
        return 0;
    if (sz_is_name(init->a, name)) {
        long value;
        if (!sz_const_value(init->b, &value) || value > SZ_LIMIT)
            return 0;
        *out = value;
        return 1;
    }
    return sz_for_init(init->b, name, out);
}

/* Finds the counter in the init expression when the loop uses a comma list
 * or a chained assignment and the caller does not yet know the name. */
static const struct AstNode *sz_for_init_target(const struct AstNode *init)
{
    while (init != NULL) {
        if (init->kind == AST_COMMA) {
            init = init->a;
            continue;
        }
        if (init->kind == AST_ASSIGN && init->op == '=') {
            if (init->a != NULL && init->a->kind == AST_IDENT)
                return init->a;
            return NULL;
        }
        return NULL;
    }
    return NULL;
}

/* name < E, name <= E, E > name, E >= name. The bound must either fold to a
 * constant or be a plain signed 16-bit value (so it is <= 0x7fff itself). */
static int sz_for_bound(const struct AstNode *cond, const char *name,
                        int *inclusive, long *bound, int *bound_is_const)
{
    const struct AstNode *other;

    if (cond == NULL || cond->kind != AST_BINARY)
        return 0;
    if (sz_is_name(cond->a, name) && (cond->op == '<' || cond->op == TOK_LE)) {
        other = cond->b;
        *inclusive = cond->op == TOK_LE;
    } else if (sz_is_name(cond->b, name) &&
               (cond->op == '>' || cond->op == TOK_GE)) {
        other = cond->a;
        *inclusive = cond->op == TOK_GE;
    } else
        return 0;
    other = sz_strip_sign(other);
    if (sz_const_value(other, bound)) {
        *bound_is_const = 1;
        return *bound <= SZ_LIMIT;
    }
    *bound_is_const = 0;
    *bound = SZ_LIMIT;
    return sz_kind(other) == SZ_KIND_SIGNED16;
}

/* name++, ++name, name += K, name = name + K with constant K > 0. */
static int sz_for_step(const struct AstNode *post, const char *name, long *step)
{
    long value;

    if (post == NULL)
        return 0;
    if ((post->kind == AST_POSTFIX || post->kind == AST_UNARY) &&
        post->op == TOK_INC && sz_is_name(post->a, name)) {
        *step = 1;
        return 1;
    }
    if (post->kind == AST_ASSIGN && sz_is_name(post->a, name)) {
        if (post->op == TOK_ADDEQ && sz_const_value(post->b, &value) &&
            value >= 1 && value <= SZ_LIMIT) {
            *step = value;
            return 1;
        }
        if (post->op == '=' && post->b != NULL && post->b->kind == AST_BINARY &&
            post->b->op == '+') {
            const struct AstNode *k = NULL;
            if (sz_is_name(post->b->a, name))
                k = post->b->b;
            else if (sz_is_name(post->b->b, name))
                k = post->b->a;
            if (k != NULL && sz_const_value(k, &value) && value >= 1 &&
                value <= SZ_LIMIT) {
                *step = value;
                return 1;
            }
        }
    }
    return 0;
}

static int sz_tree_modifies(const struct AstNode *n, const char *name)
{
    int i;

    if (n == NULL)
        return 0;
    switch (n->kind) {
    case AST_DECL:
        return ast_decl_span_touches_name(n, name);
    case AST_ASSIGN:
        if (sz_is_name(n->a, name))
            return 1;
        break;
    case AST_UNARY:
        if ((n->op == TOK_INC || n->op == TOK_DEC || n->op == '&') &&
            sz_is_name(n->a, name))
            return 1;
        break;
    case AST_POSTFIX:
        if ((n->op == TOK_INC || n->op == TOK_DEC) && sz_is_name(n->a, name))
            return 1;
        break;
    default:
        break;
    }
    if (sz_tree_modifies(n->a, name) || sz_tree_modifies(n->b, name) ||
        sz_tree_modifies(n->c, name) || sz_tree_modifies(n->d, name))
        return 1;
    for (i = 0; i < n->list_len; ++i)
        if (sz_tree_modifies(n->list[i], name))
            return 1;
    return 0;
}

static int sz_tree_decl_mentions(const struct AstNode *n, const char *name)
{
    int i;

    if (n == NULL)
        return 0;
    if (n->kind == AST_DECL)
        return ast_decl_span_declares_name(n, name);
    if (sz_tree_decl_mentions(n->a, name) || sz_tree_decl_mentions(n->b, name) ||
        sz_tree_decl_mentions(n->c, name) || sz_tree_decl_mentions(n->d, name))
        return 1;
    for (i = 0; i < n->list_len; ++i)
        if (sz_tree_decl_mentions(n->list[i], name))
            return 1;
    return 0;
}

int ast_sizeof_loop_push(const struct AstNode *for_node)
{
    const struct Sym *sym = NULL;
    const char *name;
    long init = 0;
    long bound;
    long step;
    int inclusive;
    int bound_is_const;
    struct SzLoop *loop;

    if (for_node == NULL || for_node->kind != AST_FOR ||
        sz_loop_depth >= SZ_MAX_LOOPS || for_node->a == NULL)
        return 0;
    if (for_node->a->kind == AST_DECL) {
        name = ast_decl_span_int_counter(for_node->a, &init);
        if (name == NULL)
            return 0;
    } else {
        const struct AstNode *target = sz_for_init_target(for_node->a);
        if (target == NULL || target->sval == NULL)
            return 0;
        name = target->sval;
        if (sz_noted_decl_declares(name))
            return 0;
        sym = find_local(name);
        if (sym == NULL || sym->type != TYPE_INT || sym->is_array ||
            sym->is_static || sym->is_volatile ||
            (sym->storage != SC_LOCAL && sym->storage != SC_PARAM))
            return 0;
        if (!sz_for_init(for_node->a, name, &init))
            return 0;
    }
    if (sz_name_address_taken(name))
        return 0;
    if (!sz_for_bound(for_node->b, name, &inclusive, &bound, &bound_is_const))
        return 0;
    if (!sz_for_step(for_node->c, name, &step))
        return 0;
    /* The counter's last value before exit is below bound + step (or
     * bound + step inclusive); a non-constant signed bound only admits a
     * unit step, which keeps the counter <= 0x7fff. */
    if (!bound_is_const) {
        if (inclusive || step != 1)
            return 0;
    } else if (bound + step - (inclusive ? 0 : 1) > SZ_LIMIT)
        return 0;
    if (init > bound)
        return 0;
    if (sz_tree_modifies(for_node->b, name) ||
        sz_tree_modifies(for_node->d, name))
        return 0;
    if (for_node->c != NULL && for_node->c->kind != AST_POSTFIX &&
        for_node->c->kind != AST_UNARY && for_node->c->kind != AST_ASSIGN)
        return 0;
    loop = &sz_loops[sz_loop_depth++];
    strncpy(loop->name, name, 63);
    loop->name[63] = 0;
    loop->sym = sym;
    loop->header = 1;
    loop->body_safe = !sz_tree_decl_mentions(for_node->d, name);
    return 1;
}

void ast_sizeof_loop_set_header(int in_header)
{
    if (sz_loop_depth > 0)
        sz_loops[sz_loop_depth - 1].header = in_header;
}

void ast_sizeof_loop_pop(void)
{
    if (sz_loop_depth > 0)
        --sz_loop_depth;
}
