/*
 * dcc_ast_gen.c - Phase 2 AST-driven code generation (off by default).
 *
 * The first consumer of the function-local AST that actually EMITS.  It walks
 * an AstNode built by dcc_ast_build.c and produces Z80 assembly by calling the
 * same primitives the streaming front end uses, so the output is byte-for-byte
 * identical to the streaming path for the constructs that have been ported.
 *
 * The supported subset is intentionally tiny and grows one construct at a time,
 * each addition gated on byte-identical binaries across the whole test corpus
 * (see ast_gen_supported).  Anything outside the subset is handled by the
 * streaming fallback in gen_expr, so the compiler stays correct at every step.
 *
 * Supported so far:
 *   - AST_INT_LIT : an integer / character constant that is the ENTIRE
 *                   expression (16-bit -> HL, 32-bit -> DE:HL), reproducing
 *                   gen_primary's literal emit and g_expr_type exactly.
 *   - AST_FLOAT_LIT : a floating constant (DE:HL), via emit_load_float_bits.
 *   - AST_STR_LIT : a string literal (possibly several adjacent pieces),
 *                   interned with add_string_ex and loaded as ld hl,S<sid>.
 *   - AST_IDENT : a bare identifier that is the ENTIRE expression (no postfix
 *                 [], ., ->, ++, --, or call).  Reproduces gen_primary's
 *                 bare-identifier terminal decision tree exactly: the
 *                 stdin/stdout/stderr immediates, enum constants, folded const
 *                 scalars, function-name decay, ix-direct locals, direct global
 *                 word loads, and the general load-address-then-value (or array
 *                 decay) tail - calling the same emit helpers in the same order.
 *   - AST_UNARY (-, +, ~, !) : a prefix arithmetic/logical operator applied to
 *                 an already-supported operand.  Reproduces gen_unary's
 *                 operand-type-driven fixup sequences exactly (including the
 *                 label allocation order for '!').
 */
#include "dcc.h"
#include "dcc_ast.h"
#include <string.h>

int g_ast_gen_enabled = 0;

static int ident_supported(const char *name)
{
    int ei;
    /* stdin/stdout/stderr are emitted as immediates before any symbol lookup,
     * exactly as the streaming path does. */
    if (!strcmp(name, "stdin") || !strcmp(name, "stdout") ||
        !strcmp(name, "stderr"))
        return 1;
    if (find_sym(name) != NULL)
        return 1;
    /* An unresolved identifier may still be an enum constant. */
    for (ei = 0; ei < nenum_consts; ++ei)
        if (!strcmp(enum_const_names[ei], name))
            return 1;
    /* Truly undefined: leave it to the streaming path (which reports the
     * error and synthesises a symbol) so diagnostics are unchanged. */
    return 0;
}

/* True for the relational / equality operators, whose result type is int. */
static int is_cmp_op(int op)
{
    return op == TOK_EQ || op == TOK_NE || op == '<' || op == '>' ||
           op == TOK_LE || op == TOK_GE;
}

/* The two shift operators, which use a distinct (ld b,l + shift-loop) emit
 * shape and whose result type is the promoted left operand, not a common
 * arithmetic type. */
static int is_shift_op(int op)
{
    return op == TOK_SHL || op == TOK_SHR;
}

/* Binary operators whose plain-int (16-bit) streaming emission is the uniform
 * "push hl / <rhs> / ex de,hl / pop hl / gen_binop_typed" sequence.  Shifts
 * use a different (ld b,l + shift-loop) shape and &&/|| are short-circuit, so
 * both stay on the streaming path for now. */
static int is_supported_binary_op(int op)
{
    switch (op) {
    case '+': case '-': case '*': case '/': case '%':
    case '&': case '^': case '|':
    case '<': case '>': case TOK_LE: case TOK_GE:
    case TOK_EQ: case TOK_NE:
        return 1;
    default:
        return 0;
    }
}

/* A value that codegen leaves in HL as a plain 16-bit int (char/short/int,
 * signed or unsigned) - not a pointer, array decay, struct, long or float. */
static int ast_is_plain_int_type(int t)
{
    if (t & (TYPE_PTR | TYPE_PTR2 | TYPE_STRUCT))
        return 0;
    return (t & 15) == TYPE_CHAR || (t & 15) == TYPE_INT;
}

/* An enum constant or a const-folded scalar: the streaming path may fold a
 * whole-constant expression built from these, so they count as constants. */
static int ast_ident_is_const(const char *name)
{
    int ei;
    struct Sym *s;
    for (ei = 0; ei < nenum_consts; ++ei)
        if (!strcmp(enum_const_names[ei], name))
            return 1;
    s = find_sym(name);
    if (s != NULL && s->is_const_value)
        return 1;
    return 0;
}

/* Forward declaration: a subscript read can be a plain-int value operand. */
static int ast_index_plain_int_read(const struct AstNode *n);

/* Forward declaration: resolve 2-D array/field-array element type. */
static int ast_index_2d_array_elem_type(const struct AstNode *n, int *out_type);

/* Forward declaration: a subscript expression can be emitted by index-only code. */
static int ast_index_subscript_supported(const struct AstNode *idx);

/* Forward declaration: a struct field read can be a plain-int value operand. */
static int ast_member_plain_int_read(const struct AstNode *n);

/* Forward declaration: resolve scalar field lvalue type. */
static int ast_member_lvalue_type(const struct AstNode *n, int *out_type);

/* Forward declaration: resolve the struct object/pointer type for a member base. */
static int ast_member_base_type(const struct AstNode *n, int *out_type);

/* Forward declaration: resolve element type for pointer-valued array fields. */
static int ast_member_pointer_array_field_elem_type(const struct AstNode *n,
                                                    int *out_type);

/* Forward declaration: a pointer deref read can be a plain-int value operand. */
static int ast_deref_plain_int_read(const struct AstNode *n);

/* Forward declaration: a pointer-valued expression supported only as the
 * operand of a dereference lvalue. */
static int ast_pointer_expr_type(const struct AstNode *n, int *out_type,
                                 int *out_no_deref);

/* Forward declaration: a prefix ++/-- of a plain-int lvalue is a value operand. */
static int ast_preincdec_plain_int(const struct AstNode *n);

/* Forward declaration: a postfix ++/-- of a plain-int lvalue is a value operand. */
static int ast_postfix_plain_int(const struct AstNode *n);

/* Forward declaration: emit a subscript element ADDRESS into HL (the address
 * machine shared by the value read and the lvalue store). */
static void gen_index_addr_ast(const struct AstNode *n, int *out_val_type);

/* Forward declaration: emit a struct field ADDRESS into HL (the address machine
 * shared by the value read and the lvalue store). */
static void gen_member_addr_ast(const struct AstNode *n, int *out_val_type);

/* Forward declaration: emit a `*ident` target ADDRESS into HL for an lvalue
 * store (mirrors streaming's try_gen_deref_postinc_lvalue_addr, which differs
 * from the deref value-read path). */
static void gen_deref_addr_ast(const struct AstNode *n, int *out_val_type);

/* Forward declaration: emit a pointer-valued expression into HL for a
 * dereference lvalue address. */
static void gen_pointer_expr_ast(const struct AstNode *n, int *out_type,
                                 int *out_no_deref);

static int ast_field_array_index_stride(int base_size, int dim_count,
                                        const int *dims, int index_count)
{
    int stride;
    int di;
    stride = base_size;
    for (di = index_count + 1; di < dim_count; ++di)
        stride *= dims[di];
    return stride;
}

/* Conservative: returns 1 only when the node is CERTAIN to evaluate to a plain
 * 16-bit int value.  Anything uncertain returns 0 and defers to streaming. */
static int ast_value_is_plain_int(const struct AstNode *n)
{
    struct Sym *s;
    int ei;
    if (n == NULL)
        return 0;
    switch (n->kind) {
    case AST_INT_LIT:
    case AST_SIZEOF_TYPE:
        return ast_is_plain_int_type(n->type);
    case AST_IDENT:
        if (!strcmp(n->sval, "stdin") || !strcmp(n->sval, "stdout") ||
            !strcmp(n->sval, "stderr"))
            return 1;                       /* emitted as TYPE_INT */
        s = find_sym(n->sval);
        if (s == NULL) {
            for (ei = 0; ei < nenum_consts; ++ei)
                if (!strcmp(enum_const_names[ei], n->sval))
                    return 1;               /* enum constant -> int */
            return 0;
        }
        if (s->is_array || s->storage == SC_FUNC)
            return 0;                       /* arrays / functions decay to ptr */
        return ast_is_plain_int_type(s->type);
    case AST_UNARY:
        if (n->op == '!')
            return 1;
        if (n->op == '-' || n->op == '+' || n->op == '~')
            return ast_value_is_plain_int(n->a);
        if (n->op == '*')
            return ast_deref_plain_int_read(n);
        if (n->op == TOK_INC || n->op == TOK_DEC)
            return ast_preincdec_plain_int(n);
        return 0;
    case AST_BINARY:
        if (is_cmp_op(n->op))
            return 1;
        if (is_supported_binary_op(n->op) || is_shift_op(n->op))
            return ast_value_is_plain_int(n->a) &&
                   ast_value_is_plain_int(n->b);
        return 0;
    case AST_INDEX:
        return ast_index_plain_int_read(n);
    case AST_MEMBER:
        return ast_member_plain_int_read(n);
    case AST_CALL: {
        int rt;
        if (n->a == NULL || n->a->kind != AST_IDENT)
            return 0;
        s = find_global(n->a->sval);
        rt = s != NULL ? s->type : TYPE_INT; /* implicit-int undeclared call */
        if (type_ptr_depth(rt) > 0 || type_is_struct_object(rt) ||
            type_is_long(rt) || type_is_float(rt))
            return 0;
        return ast_is_plain_int_type(rt);
    }
    case AST_LOGAND:
    case AST_LOGOR:
        return 1;                       /* short-circuit yields 0/1 int */
    case AST_COND:
        /* `a ? b : c` is plain int iff both arms are. */
        return ast_value_is_plain_int(n->b) && ast_value_is_plain_int(n->c);
    case AST_ASSIGN:
        return ast_gen_supported(n) && ast_value_is_plain_int(n->a);
    case AST_POSTFIX:
        return ast_postfix_plain_int(n);
    default:
        return 0;
    }
}

/* A unary chain bottoming out in a numeric literal is a compile-time
 * constant.  The streaming path folds these (e.g. -1 -> ld hl,65535) via
 * try_gen_const_expr(); the AST walker would instead emit the literal plus a
 * runtime negate, which is larger.  Defer such nodes to streaming so the
 * folded immediate is preserved. */
static int ast_node_is_const(const struct AstNode *n)
{
    if (n == NULL)
        return 0;
    switch (n->kind) {
    case AST_INT_LIT:
    case AST_FLOAT_LIT:
    case AST_SIZEOF_TYPE:
        return 1;
    case AST_IDENT:
        return ast_ident_is_const(n->sval);
    case AST_UNARY:
        if (n->op == '-' || n->op == '+' || n->op == '~' || n->op == '!')
            return ast_node_is_const(n->a);
        return 0;
    case AST_BINARY:
    case AST_LOGAND:
    case AST_LOGOR:
        /* The streaming const folder evaluates && / || too (dcc_fold.c), so a
         * fully constant logical expression folds to a single immediate;
         * defer to preserve it. */
        return ast_node_is_const(n->a) && ast_node_is_const(n->b);
    default:
        return 0;
    }
}

/* A subscript read `base[index]` that codegen leaves in HL as a plain integer
 * value, reproducing the streaming postfix chain's non-const, single-index
 * (non-field-array) branch.  Conservative: only a bare identifier base that is
 * a 1-D plain-int array or a plain int/char pointer (element size 1 or 2),
 * indexed by a supported plain-int expression.  emit_load_from_hl
 * sign/zero-extends a 1-byte element into the full HL, so a char element still
 * yields a valid 16-bit int in HL.  Integer-literal indexes mirror streaming's
 * try_parse_const_subscript / emit_add_const_to_hl fast path; other constant
 * expressions, multi-dimensional or field arrays, the n[ptr] commutative case
 * and any wider/non-int element all defer to streaming. */
static int ast_member_plain_array_field_elem_type(const struct AstNode *n, int *out_type);

static int ast_index_plain_int_read(const struct AstNode *n)
{
    struct Sym *s;
    int decayed;
    int elem;
    int esz;

    if (n == NULL || n->kind != AST_INDEX)
        return 0;
    if (ast_index_2d_array_elem_type(n, &elem))
        return ast_is_plain_int_type(elem) && type_size(elem) == 2;
    if (n->a == NULL)
        return 0;
    if (n->a->kind == AST_IDENT) {
        s = find_sym(n->a->sval);
        if (s == NULL || s->is_const_value || s->storage == SC_FUNC)
            return 0;
        if (type_is_struct_object(s->type))
            return 0;
        if (s->is_array) {
            if (s->dim_count > 1)
                return 0;
            decayed = type_add_ptr(s->type);   /* gen_ident's array decay */
        } else {
            decayed = s->type;                 /* a scalar pointer value */
        }
    } else if (n->a->kind == AST_MEMBER) {
        if (!ast_member_plain_array_field_elem_type(n->a, &elem))
            return 0;
        decayed = type_add_ptr(elem);
    } else {
        return 0;
    }
    /* Exactly one level of indirection over a plain int/char element so the
     * scale is unambiguous (size 2 -> a single `add hl,hl`; size 1 -> none). */
    if (type_ptr_depth(decayed) != 1)
        return 0;
    elem = type_decay_ptr(decayed);
    if (!ast_is_plain_int_type(elem))
        return 0;
    esz = type_size(elem);
    if (esz != 1 && esz != 2)
        return 0;
    if (type_index_elem_size(decayed) != esz)
        return 0;
    /* Index: either a literal constant (streaming folds it into the address)
     * or a supported non-constant plain-int expression. */
    if (n->b == NULL)
        return 0;
    return ast_index_subscript_supported(n->b);
}

static int ast_index_struct_object_type(const struct AstNode *n, int *out_type);
static int ast_member_array_field_elem_type(const struct AstNode *n, int *out_type);

static int ast_index_struct_object_type(const struct AstNode *n, int *out_type)
{
    struct Sym *s;
    int elem_type;

    if (n == NULL || n->kind != AST_INDEX)
        return 0;
    if (n->a == NULL)
        return 0;
    if (n->a->kind == AST_IDENT) {
        s = find_sym(n->a->sval);
        if (s == NULL || s->is_const_value || s->storage == SC_FUNC ||
            !s->is_array || s->dim_count > 1 || !type_is_struct_object(s->type))
            return 0;
        elem_type = s->type;
    } else if (n->a->kind == AST_MEMBER) {
        if (!ast_member_array_field_elem_type(n->a, &elem_type) ||
            !type_is_struct_object(elem_type))
            return 0;
    } else {
        return 0;
    }
    if (n->b == NULL)
        return 0;
    if (n->b->kind == AST_INT_LIT)
        return ast_value_is_plain_int(n->b) ? (*out_type = elem_type, 1) : 0;
    if (!ast_index_subscript_supported(n->b))
        return 0;
    *out_type = elem_type;
    return 1;
}

static int ast_index_struct_object_addr(const struct AstNode *n)
{
    int ignored;
    return ast_index_struct_object_type(n, &ignored);
}

static int ast_index_subscript_binary_literal(const struct AstNode *idx)
{
    if (idx == NULL || idx->kind != AST_BINARY)
        return 0;
    if (idx->op != '+' && idx->op != '-')
        return 0;
    if (idx->b == NULL || idx->b->kind != AST_INT_LIT)
        return 0;
    if (idx->a == NULL || ast_node_is_const(idx->a))
        return 0;
    return ast_gen_supported(idx->a) && ast_value_is_plain_int(idx->a) &&
           ast_value_is_plain_int(idx->b);
}

static int ast_index_subscript_supported(const struct AstNode *idx)
{
    if (idx == NULL)
        return 0;
    if (idx->kind == AST_INT_LIT)
        return ast_value_is_plain_int(idx);
    if (ast_index_subscript_binary_literal(idx))
        return 1;
    if (ast_node_is_const(idx))
        return 0;
    return ast_gen_supported(idx) && ast_value_is_plain_int(idx);
}

static int ast_index_2d_addressable_addr(const struct AstNode *n)
{
    struct Sym *s;
    const struct AstNode *outer;
    int elem;

    if (n == NULL || n->kind != AST_INDEX)
        return 0;
    outer = n->a;
    if (outer == NULL || outer->kind != AST_INDEX || outer->a == NULL ||
        outer->a->kind != AST_IDENT)
        return 0;
    s = find_sym(outer->a->sval);
    if (s == NULL || s->is_const_value || s->storage == SC_FUNC || !s->is_array)
        return 0;
    if (s->dim_count != 2)
        return 0;
    elem = type_size(s->type);
    if (elem <= 0)
        return 0;
    return ast_index_subscript_supported(outer->b) &&
           ast_index_subscript_supported(n->b);
}

static int ast_index_2d_array_elem_type(const struct AstNode *n, int *out_type)
{
    const struct AstNode *outer;
    struct Sym *s;
    struct FieldDef *fd;
    int cur_type;
    int sid;

    outer = n->a;
    if (ast_index_2d_addressable_addr(n)) {
        s = find_sym(outer->a->sval);
        if (s == NULL)
            return 0;
        *out_type = s->type;
        return 1;
    }

    if (n == NULL || n->kind != AST_INDEX || outer == NULL ||
        outer->kind != AST_INDEX || outer->a == NULL ||
        outer->a->kind != AST_MEMBER)
        return 0;
    if (!ast_member_base_type(outer->a, &cur_type))
        return 0;
    if (outer->a->op == TOK_ARROW) {
        if (type_ptr_depth(cur_type) != 1)
            return 0;
    } else if (!type_is_struct_object(cur_type)) {
        return 0;
    }
    sid = base_struct_id_from_type(cur_type);
    fd = find_field_def(sid, outer->a->sval);
    if (fd == NULL || !fd->is_array || fd->bit_width > 0 || fd->dim_count != 2)
        return 0;
    if (!ast_index_subscript_supported(outer->b) ||
        !ast_index_subscript_supported(n->b))
        return 0;
    *out_type = fd->elem_type;
    return 1;
}

static int ast_index_addressable_addr(const struct AstNode *n)
{
    struct Sym *s;
    int decayed;
    int elem;

    if (n == NULL || n->kind != AST_INDEX)
        return 0;
    if (ast_index_2d_addressable_addr(n))
        return 1;
    if (n->a == NULL || n->a->kind != AST_IDENT)
        return 0;
    s = find_sym(n->a->sval);
    if (s == NULL || s->is_const_value || s->storage == SC_FUNC)
        return 0;
    if (s->is_array) {
        if (s->dim_count > 1)
            return 0;
        decayed = type_add_ptr(s->type);
    } else {
        decayed = s->type;
    }
    if (type_ptr_depth(decayed) < 1)
        return 0;
    elem = type_decay_ptr(decayed);
    if ((elem & 15) == TYPE_VOID || type_size(elem) <= 0)
        return 0;
    return ast_index_subscript_supported(n->b);
}

static int ast_index_pointer_array_elem_type(const struct AstNode *n, int *out_type)
{
    struct Sym *s;
    int elem_type;

    if (n == NULL || n->kind != AST_INDEX)
        return 0;
    if (n->a == NULL || n->a->kind != AST_IDENT)
        return 0;
    s = find_sym(n->a->sval);
    if (s == NULL || s->is_const_value || s->storage == SC_FUNC || !s->is_array)
        return 0;
    if (s->dim_count > 1)
        return 0;
    elem_type = s->type;
    if (type_ptr_depth(elem_type) <= 0 || type_size(elem_type) != 2)
        return 0;
    if (n->b == NULL)
        return 0;
    if (n->b->kind == AST_INT_LIT) {
        if (!ast_value_is_plain_int(n->b))
            return 0;
    } else {
        if (ast_node_is_const(n->b))
            return 0;
        if (!ast_gen_supported(n->b) || !ast_value_is_plain_int(n->b))
            return 0;
    }
    *out_type = elem_type;
    return 1;
}

static int ast_pointer_expr_type(const struct AstNode *n, int *out_type,
                                 int *out_no_deref)
{
    struct Sym *s;
    int ptr_type;
    int no_deref;
    int base;
    int member_type;

    if (n == NULL)
        return 0;

    switch (n->kind) {
    case AST_IDENT:
        s = find_sym(n->sval);
        if (s == NULL || s->is_const_value || s->storage == SC_FUNC)
            return 0;
        if (s->is_array) {
            *out_type = type_add_ptr(s->type);
            *out_no_deref = 0;
            return 1;
        }
        if (type_ptr_depth(s->type) <= 0)
            return 0;
        *out_type = s->type;
        *out_no_deref = 0;
        return 1;

    case AST_UNARY:
        if (n->op == '&') {
            int elem;
            if (n->a == NULL)
                return 0;
            if (n->a->kind == AST_INDEX) {
                if (!ast_index_2d_array_elem_type(n->a, &elem))
                    return 0;
                *out_type = type_add_ptr(elem);
                *out_no_deref = 0;
                return 1;
            }
            if (n->a->kind != AST_IDENT)
                return 0;
            s = find_sym(n->a->sval);
            if (s == NULL || s->is_const_value || s->storage == SC_FUNC || s->is_array)
                return 0;
            *out_type = type_add_ptr(s->type);
            *out_no_deref = 0;
            return 1;
        }
        if (n->op != '*')
            return 0;
        if (!ast_pointer_expr_type(n->a, &ptr_type, &no_deref))
            return 0;
        base = no_deref ? ptr_type : type_decay_ptr(ptr_type);
        if ((base & 15) == TYPE_VOID)
            base = TYPE_CHAR;
        if (type_ptr_depth(base) <= 0)
            return 0;
        *out_type = base;
        *out_no_deref = 0;
        return 1;

    case AST_BINARY:
        if (n->op != '+')
            return 0;
        if (!ast_pointer_expr_type(n->a, &ptr_type, &no_deref))
            return 0;
        if (no_deref)
            return 0;
        if (!ast_index_subscript_supported(n->b))
            return 0;
        *out_type = ptr_type;
        *out_no_deref = 0;
        if (n->a->kind == AST_IDENT) {
            s = find_sym(n->a->sval);
            if (s != NULL && s->is_array && s->dim_count > 1)
                *out_no_deref = 1;
        }
        return 1;

    case AST_MEMBER:
        if (ast_member_array_field_elem_type(n, &member_type)) {
            if (type_size(member_type) <= 0)
                return 0;
            *out_type = type_add_ptr(member_type);
            *out_no_deref = 0;
            return 1;
        }
        if (!ast_member_lvalue_type(n, &member_type))
            return 0;
        if (type_ptr_depth(member_type) <= 0 || type_size(member_type) != 2)
            return 0;
        *out_type = member_type;
        *out_no_deref = 0;
        return 1;

    case AST_INDEX:
        if (ast_index_pointer_array_elem_type(n, &member_type) ||
            (n->a != NULL && n->a->kind == AST_MEMBER &&
             ast_member_pointer_array_field_elem_type(n->a, &member_type))) {
            *out_type = member_type;
            *out_no_deref = 0;
            return 1;
        }
        return 0;

    case AST_CALL:
        if (!ast_gen_supported(n) || n->a == NULL || n->a->kind != AST_IDENT)
            return 0;
        s = find_global(n->a->sval);
        if (s == NULL || type_ptr_depth(s->type) <= 0 || type_size(s->type) != 2)
            return 0;
        *out_type = s->type;
        *out_no_deref = 0;
        return 1;

    default:
        return 0;
    }
}

static int ast_deref_lvalue_plain_int_type(const struct AstNode *n, int *out_type)
{
    int ptr_type;
    int no_deref;
    int base;
    int sz;

    if (n == NULL || n->kind != AST_UNARY || n->op != '*')
        return 0;
    if (!ast_pointer_expr_type(n->a, &ptr_type, &no_deref))
        return 0;
    base = no_deref ? ptr_type : type_decay_ptr(ptr_type);
    if ((base & 15) == TYPE_VOID)
        base = TYPE_CHAR;
    if (!ast_is_plain_int_type(base))
        return 0;
    sz = type_size(base);
    if (sz != 1 && sz != 2)
        return 0;
    *out_type = base;
    return 1;
}

static int ast_member_base_type(const struct AstNode *n, int *out_type)
{
    struct Sym *s;
    int no_deref;

    if (n == NULL || n->kind != AST_MEMBER || n->a == NULL)
        return 0;
    if (n->a->kind == AST_IDENT) {
        s = find_sym(n->a->sval);
        if (s == NULL || s->is_const_value || s->storage == SC_FUNC || s->is_array)
            return 0;
        *out_type = s->type;
        return 1;
    }
    if (n->a->kind == AST_INDEX) {
        if (n->op == TOK_ARROW && ast_pointer_expr_type(n->a, out_type, &no_deref))
            return !no_deref;
        return ast_index_struct_object_type(n->a, out_type);
    }
    if (n->op == TOK_ARROW && ast_pointer_expr_type(n->a, out_type, &no_deref))
        return !no_deref;
    if (n->op == '.' && n->a->kind == AST_UNARY && n->a->op == '*' &&
        ast_pointer_expr_type(n->a->a, out_type, &no_deref)) {
        if (no_deref)
            return 0;
        *out_type = type_decay_ptr(*out_type);
        if ((*out_type & 15) == TYPE_VOID)
            *out_type = TYPE_CHAR;
        return type_is_struct_object(*out_type);
    }
    return 0;
}

static int ast_member_array_field_elem_type(const struct AstNode *n, int *out_type)
{
    struct FieldDef *fd;
    int cur_type;
    int sid;

    if (!ast_member_base_type(n, &cur_type))
        return 0;
    if (n->op == TOK_ARROW) {
        if (type_ptr_depth(cur_type) != 1)
            return 0;
    } else if (!type_is_struct_object(cur_type)) {
        return 0;
    }
    sid = base_struct_id_from_type(cur_type);
    fd = find_field_def(sid, n->sval);
    if (fd == NULL || !fd->is_array || fd->bit_width > 0)
        return 0;
    *out_type = fd->elem_type;
    return 1;
}

static int ast_member_plain_array_field_elem_type(const struct AstNode *n, int *out_type)
{
    struct FieldDef *fd;
    int cur_type;
    int sid;
    int elem_type;
    int elem_size;

    if (!ast_member_base_type(n, &cur_type))
        return 0;
    if (n->op == TOK_ARROW) {
        if (type_ptr_depth(cur_type) != 1)
            return 0;
    } else if (!type_is_struct_object(cur_type)) {
        return 0;
    }
    sid = base_struct_id_from_type(cur_type);
    fd = find_field_def(sid, n->sval);
    if (fd == NULL || !fd->is_array || fd->bit_width > 0 || fd->dim_count != 1)
        return 0;
    elem_type = fd->elem_type;
    if (!ast_is_plain_int_type(elem_type))
        return 0;
    elem_size = type_size(elem_type);
    if (elem_size != 1 && elem_size != 2)
        return 0;
    *out_type = elem_type;
    return 1;
}

static int ast_member_pointer_array_field_elem_type(const struct AstNode *n, int *out_type)
{
    struct FieldDef *fd;
    int cur_type;
    int sid;
    int elem_type;

    if (!ast_member_base_type(n, &cur_type))
        return 0;
    if (n->op == TOK_ARROW) {
        if (type_ptr_depth(cur_type) != 1)
            return 0;
    } else if (!type_is_struct_object(cur_type)) {
        return 0;
    }
    sid = base_struct_id_from_type(cur_type);
    fd = find_field_def(sid, n->sval);
    if (fd == NULL || !fd->is_array || fd->bit_width > 0 || fd->dim_count != 1)
        return 0;
    elem_type = fd->elem_type;
    if (type_ptr_depth(elem_type) <= 0 || type_size(elem_type) != 2)
        return 0;
    *out_type = elem_type;
    return 1;
}

/* A struct field read `id.f` / `id->f` that codegen leaves in HL as a plain
 * 16-bit int value, reproducing the streaming identifier-rooted field machine
 * (apply_field_access_from_addr) for a SINGLE field access.  Conservative: a
 * bare-identifier base that is a struct object (for `.`) or a depth-1 pointer
 * to a struct (for `->`), and a field that is a plain int/char SCALAR -
 * non-array, non-bitfield.  emit_load_from_hl sign/zero-extends a 1-byte field
 * into the full HL, so a char field still yields a valid 16-bit int.  Field
 * arrays, bitfields, nested/chained accesses, and wider/non-int field types all
 * defer to streaming. */
static int ast_member_plain_int_read(const struct AstNode *n)
{
    struct Sym *s;
    struct FieldDef *fd;
    int arrow;
    int cur_type;
    int sid;
    int fsz;

    if (n == NULL || n->kind != AST_MEMBER || n->sval == NULL)
        return 0;
    arrow = (n->op == TOK_ARROW);
    if (n->a == NULL)
        return 0;
    if (!ast_member_base_type(n, &cur_type))
        return 0;
    if (arrow) {
        if (type_ptr_depth(cur_type) != 1)
            return 0;                      /* `->` needs a single pointer level */
    } else {
        if (!type_is_struct_object(cur_type))
            return 0;                      /* `.` needs a struct object */
    }
    sid = base_struct_id_from_type(cur_type);
    if (sid <= 0)
        return 0;
    fd = find_field_def(sid, n->sval);
    if (fd == NULL)
        return 0;
    if (fd->is_array || fd->bit_width > 0)
        return 0;                          /* field arrays / bitfields defer */
    if (!ast_is_plain_int_type(fd->type))
        return 0;
    fsz = type_size(fd->type);
    if (fsz != 1 && fsz != 2)
        return 0;
    return 1;
}

static int ast_member_lvalue_type(const struct AstNode *n, int *out_type)
{
    struct Sym *s;
    struct FieldDef *fd;
    int cur_type;
    int sid;

    if (n == NULL || n->kind != AST_MEMBER || n->sval == NULL)
        return 0;
    if (n->a == NULL)
        return 0;
    if (!ast_member_base_type(n, &cur_type))
        return 0;
    if (n->op == TOK_ARROW) {
        if (type_ptr_depth(cur_type) != 1)
            return 0;
    } else if (!type_is_struct_object(cur_type)) {
        return 0;
    }
    sid = base_struct_id_from_type(cur_type);
    fd = find_field_def(sid, n->sval);
    if (fd == NULL || fd->is_array || fd->bit_width > 0)
        return 0;
    *out_type = fd->type;
    return 1;
}

/* A pointer deref read `*p` that streaming's try_gen_simple_deref_value fast
 * path leaves in HL as a plain int value.  That fast path fires only for `*`
 * applied to a BARE identifier NOT followed by [ . -> ( or ++ / -- - which is
 * exactly an AST_UNARY '*' whose operand is a bare AST_IDENT (any trailing
 * postfix would reparent the operand into an INDEX/MEMBER/CALL/POSTFIX node).
 * Restrict to a single-level pointer to a plain int/char element (size 1 or 2)
 * so the load is the simple emit_load_from_hl(base).  Function pointers,
 * arrays, const/enum symbols, wider/non-int and void elements all defer. */
static int ast_deref_plain_int_read(const struct AstNode *n)
{
    struct Sym *s;
    int ptr_type;
    int no_deref;
    int base;

    if (n == NULL || n->kind != AST_UNARY || n->op != '*')
        return 0;
    if (n->a == NULL)
        return 0;
    if (n->a->kind != AST_IDENT) {
        if (!ast_pointer_expr_type(n->a, &ptr_type, &no_deref))
            return 0;
        base = no_deref ? ptr_type : type_decay_ptr(ptr_type);
        if ((base & 15) == TYPE_VOID)
            base = TYPE_CHAR;
        if (!ast_is_plain_int_type(base))
            return 0;
        if (type_size(base) != 1 && type_size(base) != 2)
            return 0;
        return 1;
    }
    s = find_sym(n->a->sval);
    if (s == NULL || s->is_const_value || s->storage == SC_FUNC || s->is_array)
        return 0;
    if (type_ptr_depth(s->type) != 1)
        return 0;
    base = type_decay_ptr(s->type);
    if (!ast_is_plain_int_type(base))
        return 0;
    if (type_size(base) != 1 && type_size(base) != 2)
        return 0;
    return 1;
}

static int ast_deref_pointer_word_read(const struct AstNode *n)
{
    int ptr_type;
    int no_deref;
    int base;

    if (n == NULL || n->kind != AST_UNARY || n->op != '*' || n->a == NULL)
        return 0;
    if (!ast_pointer_expr_type(n->a, &ptr_type, &no_deref))
        return 0;
    base = no_deref ? ptr_type : type_decay_ptr(ptr_type);
    if (type_ptr_depth(base) <= 0 || type_size(base) != 2)
        return 0;
    return 1;
}

/* A prefix `++lv` / `--lv` on a bare plain-int identifier.  Streaming gen_unary
 * emits gen_lvalue_addr(&t) + emit_pre_incdec_lvalue(t, op) unconditionally for
 * prefix ++/--, and for a bare identifier gen_lvalue_addr reduces to
 * emit_load_sym_addr(s) with t = s->type (no [ ] / . / -> follow).  Restrict to
 * a plain int/char scalar (size 1 or 2) so emit_pre_incdec_lvalue takes its
 * 16-bit branch (the long branch stores DE:HL; pointers add an element size). */
static int ast_preincdec_plain_int(const struct AstNode *n)
{
    struct Sym *s;
    int sz;

    if (n == NULL || n->kind != AST_UNARY)
        return 0;
    if (n->op != TOK_INC && n->op != TOK_DEC)
        return 0;
    if (n->a == NULL || n->a->kind != AST_IDENT)
        return 0;
    s = find_sym(n->a->sval);
    if (s == NULL || s->is_const_value || s->storage == SC_FUNC || s->is_array)
        return 0;
    if (!ast_is_plain_int_type(s->type))
        return 0;
    sz = type_size(s->type);
    if (sz != 1 && sz != 2)
        return 0;
    return 1;
}

/* A postfix `lv++` / `lv--` on a bare plain-int identifier.  For such an lvalue
 * streaming's gen_primary takes the try_emit_post_update_sym_direct fast path
 * deterministically (it returns 1 for any non-array, non-long, non-float scalar
 * of size <= 2 that is IX-direct or a global word), which loads the OLD value,
 * pushes it, increments/decrements in HL, stores back, and pops the old value
 * as the expression result.  Restrict to a plain int/char (size 1 or 2, not a
 * pointer) so the non-pointer inc/dec branch applies. */
static int ast_postfix_plain_int(const struct AstNode *n)
{
    struct Sym *s;
    int sz;

    if (n == NULL || n->kind != AST_POSTFIX)
        return 0;
    if (n->op != TOK_INC && n->op != TOK_DEC)
        return 0;
    if (n->a == NULL || n->a->kind != AST_IDENT)
        return 0;
    s = find_sym(n->a->sval);
    if (s == NULL || s->is_const_value || s->storage == SC_FUNC || s->is_array)
        return 0;
    if (!ast_is_plain_int_type(s->type))
        return 0;
    sz = type_size(s->type);
    if (sz != 1 && sz != 2)
        return 0;
    if (!sym_can_ix_direct(s) && !is_global_word_sym(s))
        return 0;
    return 1;
}

static int ast_address_of_supported(const struct AstNode *n)
{
    int elem;
    if (n == NULL)
        return 0;
    if (n->kind == AST_IDENT)
        return ident_supported(n->sval);
    if (n->kind == AST_INDEX)
        return ast_index_addressable_addr(n) || ast_index_struct_object_addr(n) ||
               ast_index_2d_array_elem_type(n, &elem);
    return 0;
}

static int ast_call_arg_word_supported(const struct AstNode *arg);
static int ast_call_arg_supported(struct Sym *fn_sym, int arg_index,
                                  const struct AstNode *arg);
static int ast_value_is_long_word(const struct AstNode *arg);
static int ast_value_is_float_word(const struct AstNode *arg);
static int ast_value_is_pointer_word(const struct AstNode *n);
static int ast_pointer_assign_rhs_supported(const struct AstNode *n);

int ast_gen_supported(const struct AstNode *n)
{
    if (n == NULL)
        return 0;
    switch (n->kind) {
    case AST_INT_LIT:
    case AST_FLOAT_LIT:
    case AST_STR_LIT:
    case AST_SIZEOF_TYPE:
        return 1;
    case AST_IDENT:
        return ident_supported(n->sval);
    case AST_UNARY:
        if (n->op == '&')
            return ast_address_of_supported(n->a);
        if (n->op == '-' || n->op == '+' || n->op == '~' || n->op == '!') {
            /* Constant operands are folded by the streaming path; deferring
             * keeps the folded immediate (byte-identical, smaller). */
            if (ast_node_is_const(n->a))
                return 0;
            return ast_gen_supported(n->a);
        }
        /* Address-of a bare addressable identifier reduces - in gen_lvalue_addr
         * with no trailing [ ] / . / -> - to emit_load_sym_addr(s) plus a
         * pointer-to-element result type.  The deref / phantom-deref / global
         * pointer-subscript preload special cases all need a following token,
         * so a bare-id operand cannot reach them.  Exclude const-value (enum /
         * folded) and function symbols, which are not plain addressable
         * objects. */
        if (n->op == '&') {
            struct Sym *as;
            if (n->a == NULL || n->a->kind != AST_IDENT)
                return 0;
            as = find_sym(n->a->sval);
            if (as == NULL || as->is_const_value || as->storage == SC_FUNC)
                return 0;
            return 1;
        }
        if (n->op == '*')
            return ast_deref_plain_int_read(n) || ast_deref_pointer_word_read(n);
        if (n->op == TOK_INC || n->op == TOK_DEC)
            return ast_preincdec_plain_int(n);
        return 0;
    case AST_BINARY:
        if (!is_supported_binary_op(n->op) && !is_shift_op(n->op))
            return 0;
        /* A fully constant expression is folded by the streaming path
         * (try_gen_const_expr) into a single immediate; defer to keep it. */
        if (ast_node_is_const(n))
            return 0;
        /* A literal operand triggers streaming's const fast paths (const*x
         * prefix, gen_mul / gen_rel / gen_shift literal specialisations);
         * defer so those byte sequences are preserved. */
        if (n->a->kind == AST_INT_LIT || n->a->kind == AST_FLOAT_LIT ||
            n->b->kind == AST_INT_LIT || n->b->kind == AST_FLOAT_LIT)
            return 0;
        if (!ast_gen_supported(n->a) || !ast_gen_supported(n->b))
            return 0;
        /* Only the plain-int branch reproduces the uniform sequence exactly;
         * pointers, longs, floats and structs take other streaming shapes. */
        if (!ast_value_is_plain_int(n->a) || !ast_value_is_plain_int(n->b))
            return 0;
        /* Shifts derive their result/branch shape from the (already plain-int)
         * left operand; the arithmetic/comparison group instead depends on the
         * stored rhs peek staying on the 16-bit branch. */
        if (!is_shift_op(n->op) &&
            (type_is_long(n->peek_type) || type_is_float(n->peek_type)))
            return 0;
        return 1;
    case AST_ASSIGN: {
        /* Narrow slice: `lhs = rhs` and compound `lhs OP= rhs` where lhs is a
         * bare plain-int (16-bit, signed or unsigned) scalar reachable by the
         * direct store helper - an IX-direct local/param or a non-array
         * global/extern word.  Plain `=` is streaming's GENERAL `=` tail; the
         * compound ops (+=,-=,*=,/=,%=,&=,|=,^=) reproduce streaming's GENERAL
         * compound tail (load lhs, push, eval rhs, combine, store).  The
         * dead-result `i += const` / `i -= ix_local` fast paths are deferred so
         * the AST never emits a longer sequence.  A plain `=` to a SUBSCRIPT
         * lvalue `arr[i] = rhs` (int element) is also supported via streaming's
         * normal_assign address+store tail.  Shift-assigns, other non-identifier
         * lvalues, char-element subscripts, arrays, consts, structs, pointers,
         * chars, longs and floats all defer to streaming. */
        struct Sym *s;
        int is_compound = (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                           n->op == TOK_MULEQ || n->op == TOK_DIVEQ ||
                           n->op == TOK_MODEQ || n->op == TOK_ANDEQ ||
                           n->op == TOK_OREQ  || n->op == TOK_XOREQ);
        if (n->op != '=' && !is_compound)
            return 0;                     /* shift-assign etc. defer */
        if (!ast_gen_supported(n->b) && n->b->kind != AST_CAST)
            return 0;
        /* Subscript lvalue store: arr[i] = rhs / arr[i] OP= rhs.  Restricted to
         * an INT element (size 2) so streaming reaches its normal_assign
         * address+store tail with no byte-store fast path intervening (the
         * global byte-array fast path needs a size-1 element).  The
         * element/index/base constraints are the same as a plain-int subscript
         * READ. */
        if (n->a->kind == AST_INDEX) {
            struct Sym *base;
            int decayed;
            int elem;
            if (ast_index_2d_array_elem_type(n->a, &elem)) {
                if (n->op != '=')
                    return 0;
                if (!ast_value_is_plain_int(n->b))
                    return 0;
                if (!ast_is_plain_int_type(elem) || type_size(elem) != 2)
                    return 0;
                return 1;
            }
            if (ast_index_pointer_array_elem_type(n->a, &elem)) {
                if (n->op != '=')
                    return 0;
                return ast_pointer_assign_rhs_supported(n->b);
            }
            if (n->a->a->kind == AST_MEMBER &&
                ast_member_pointer_array_field_elem_type(n->a->a, &elem)) {
                if (n->op != '=')
                    return 0;
                if (n->a->b == NULL)
                    return 0;
                if (n->a->b->kind == AST_INT_LIT) {
                    if (!ast_value_is_plain_int(n->a->b))
                        return 0;
                } else {
                    if (ast_node_is_const(n->a->b))
                        return 0;
                    if (!ast_gen_supported(n->a->b) || !ast_value_is_plain_int(n->a->b))
                        return 0;
                }
                return ast_pointer_assign_rhs_supported(n->b);
            }
            if (!ast_index_plain_int_read(n->a))
                return 0;
            if (n->a->a->kind == AST_IDENT) {
                if (!ast_value_is_plain_int(n->b))
                    return 0;
                base = find_sym(n->a->a->sval);
                decayed = base->is_array ? type_add_ptr(base->type) : base->type;
                elem = type_decay_ptr(decayed);
                if (type_size(elem) != 2 && (n->op != '=' || type_size(elem) != 1))
                    return 0;
                if (type_size(elem) == 1 && base->is_array &&
                    (base->storage == SC_GLOBAL || base->storage == SC_EXTERN))
                    return 0;
            } else if (n->a->a->kind == AST_MEMBER) {
                if (ast_member_plain_array_field_elem_type(n->a->a, &elem)) {
                    if (!ast_value_is_plain_int(n->b))
                        return 0;
                    if (type_size(elem) != 2 && (n->op != '=' || type_size(elem) != 1))
                        return 0;
                } else {
                    return 0;
                }
            } else {
                return 0;
            }
            return 1;
        }
        /* Member lvalue store: s.f = rhs / p->f OP= rhs.  Restricted to an INT
         * (size 2) plain scalar field so streaming reaches normal_assign's
         * store tail with no byte-field fast path intervening (the global
         * struct byte-field fast path needs a size-1 field). */
        if (n->a->kind == AST_MEMBER) {
            int field_type;
            if (!ast_member_lvalue_type(n->a, &field_type))
                return 0;
            if (type_ptr_depth(field_type) > 0) {
                if (n->op != '=' || type_size(field_type) != 2)
                    return 0;
                return ast_pointer_assign_rhs_supported(n->b);
            }
            if (!ast_value_is_plain_int(n->b))
                return 0;
            if (type_size(field_type) == 1 && n->op == '=' &&
                n->a->a != NULL &&
                (n->a->a->kind == AST_INDEX ||
                 (n->a->a->kind == AST_UNARY && n->a->a->op == '*')))
                return 1;
            if (type_size(field_type) != 2)
                return 0;
            return 1;
        }
        /* Deref lvalue store: *p = rhs / *p OP= rhs.  `*ident` always reaches
         * streaming's normal_assign (no byte fast path matches a leading `*`),
         * so both char* and int* targets are safe.  ast_deref_plain_int_read
         * validates a single-level pointer to a plain int/char (size 1 or 2). */
        if (n->a->kind == AST_UNARY && n->a->op == '*') {
            int deref_type;
            if (!ast_value_is_plain_int(n->b))
                return 0;
            if (!ast_deref_lvalue_plain_int_type(n->a, &deref_type))
                return 0;
            return 1;
        }
        if (n->a->kind != AST_IDENT)
            return 0;
        s = find_sym(n->a->sval);
        if (s == NULL)
            return 0;
        if (s->is_array || s->is_const_value)
            return 0;
        if (type_is_struct_object(s->type) || type_is_float(s->type))
            return 0;
        if (type_is_long(s->type)) {
            if (n->op != '=' || !sym_can_ix_direct(s))
                return 0;
            return ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b);
        }
        if (!sym_can_ix_direct(s) && !is_global_word_sym(s) &&
            !(n->op == '=' && type_size(s->type) == 1 &&
              (s->storage == SC_GLOBAL || s->storage == SC_EXTERN))) {
            if (n->op != '=')
                return 0;
            if (!ast_value_is_plain_int(n->b))
                return 0;
            if (!ast_is_plain_int_type(s->type))
                return 0;
            if (type_size(s->type) != 1 && type_size(s->type) != 2)
                return 0;
            return 1;
        }
        if (expr_result_dead &&
            (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ) &&
            !type_is_long(s->type) && !type_is_float(s->type)) {
            if (n->b->kind == AST_INT_LIT)
                return 1;
            if (n->b->kind == AST_IDENT) {
                struct Sym *rs = find_sym(n->b->sval);
                return sym_can_ix_direct(rs);
            }
        }
        if (type_ptr_depth(s->type) > 0) {
            if (n->op != '=' || type_size(s->type) != 2)
                return 0;
            return ast_pointer_assign_rhs_supported(n->b);
        }
        if (!ast_value_is_plain_int(n->b))
            return 0;
        if (!ast_is_plain_int_type(s->type))
            return 0;
        if (type_size(s->type) == 1) {
            struct Sym *rs;
            if (n->op != '=')
                return 0;
            if (s->storage == SC_GLOBAL || s->storage == SC_EXTERN)
                return 1;
            if (!sym_can_ix_direct(s))
                return 0;
            if (n->b->kind == AST_IDENT) {
                rs = find_sym(n->b->sval);
                if (!sym_can_ix_direct(rs) || type_size(rs->type) != 1)
                    return 0;
                return 1;
            }
            if (n->b->kind == AST_INT_LIT && n->b->ival >= 0 &&
                n->b->ival <= 255 &&
                (n->b->uval == AST_INT_UVAL_CHARLIT ||
                 n->b->uval == AST_INT_UVAL_PLAIN_DECIMAL))
                return 1;
            return 0;
        }
        if ((s->type & 15) != TYPE_INT || type_size(s->type) != 2)
            return 0;
        if (is_compound && expr_result_dead &&
            (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ)) {
            /* In a dead-result context streaming routes `+=`/`-=` through
             * either the IX-direct fast path or the address-machine
             * normal_assign tail - never the general direct compound path the
             * AST reproduces.  Defer so the AST never emits a divergent (and
             * sometimes longer) sequence. */
            return 0;
        }
        return 1;
    }
    case AST_INDEX:
        return ast_index_plain_int_read(n);
    case AST_CALL: {
        /* Direct, named call `f(a, b, ...)` whose arguments are all 16-bit
         * word values (plain ints or string-literal pointers) pushed as one
         * word each.  This reproduces the
         * streaming named-call tail (reverse-order arg push + `call name` +
         * emit_cleanup_stack_bytes) for the common case.  Deferred (left to
         * streaming): calls through a function POINTER (indirect __call_hl);
         * the name-recognised builtins with argument fast paths
         * (__va_start/__va_arg/__va_end, strcpy, strlen, strchr, cb_is_zero);
         * any prototype that widens an argument to float/long/struct (the push
         * is then 4 bytes / an address, not a single word); and any other
         * non-word argument (a float/long/struct actual, a pointer expression
         * not yet covered by the AST, etc.). */
        struct Sym *call_sym;
        struct Sym *fn_sym;
        const char *cname;
        int i;
        if (n->a == NULL || n->a->kind != AST_IDENT)
            return 0;
        cname = n->a->sval;
        if (!strcmp(cname, "__va_start") || !strcmp(cname, "__va_arg") ||
            !strcmp(cname, "__va_end") || !strcmp(cname, "strcpy") ||
            !strcmp(cname, "strlen") || !strcmp(cname, "strchr") ||
            !strcmp(cname, "cb_is_zero"))
            return 0;                      /* builtin argument fast paths */
        call_sym = find_sym(cname);
        if (call_sym != NULL && call_sym->storage != SC_FUNC &&
            type_ptr_depth(call_sym->type) > 0)
            return 0;                      /* indirect call through fn pointer */
        fn_sym = find_global(cname);       /* read-only: gate must not mutate */
        for (i = 0; i < n->list_len; ++i) {
            if (!ast_call_arg_supported(fn_sym, i, n->list[i]))
                return 0;
        }
        return 1;
    }
    case AST_POSTFIX:
        return ast_postfix_plain_int(n);
    case AST_MEMBER:
        return ast_member_plain_int_read(n);
    case AST_LOGAND:
    case AST_LOGOR:
        /* Short-circuit `a && b` / `a || b`: pure test-and-branch producing a
         * 0/1 int, no arithmetic conversion.  The builder nests these
         * left-associatively, so a recursive walk reproduces streaming's flat
         * gen_land/gen_lor loop label-for-label.  A wholly constant logical is
         * folded by the streaming const evaluator -> defer.  Operands are
         * required to be supported plain-int values so the tested type (and the
         * emitted operand bytes) match the streaming gen_bor result exactly. */
        if (ast_node_is_const(n))
            return 0;
        if (!ast_gen_supported(n->a) || !ast_gen_supported(n->b))
            return 0;
        if (!ast_value_is_plain_int(n->a) || !ast_value_is_plain_int(n->b))
            return 0;
        return 1;
    case AST_COND:
        /* `cond ? a : b` with plain-int condition and both arms plain int.
         * For this case streaming's gen_conditional takes neither the float
         * nor the long-result path: result_is_float is false (no float arm)
         * and need_long_result is false (no long arm), so the emit reduces to
         * test + true-arm + speculative emit_extend_to_long(true arm) + jp end
         * + false-arm + common_arith_type result.  Restricting all three parts
         * to supported plain-int values keeps that shape exact and avoids the
         * float-arm oracle / long-widening branches entirely. */
        if (!ast_gen_supported(n->a) || !ast_value_is_plain_int(n->a))
            return 0;
        if (!ast_gen_supported(n->b) || !ast_value_is_plain_int(n->b))
            return 0;
        if (!ast_gen_supported(n->c) || !ast_value_is_plain_int(n->c))
            return 0;
        return 1;
    default:
        return 0;
    }
}

static int ast_call_arg_word_supported(const struct AstNode *arg)
{
    struct Sym *s;
    int ptr_type;
    int no_deref;

    if (arg == NULL)
        return 0;
    if (ast_pointer_expr_type(arg, &ptr_type, &no_deref))
        return 1;
    if (!ast_gen_supported(arg))
        return 0;
    if (arg->kind == AST_STR_LIT)
        return 1;                         /* char * literal in HL */
    if (arg->kind == AST_UNARY && arg->op == '&')
        return 1;                         /* object address in HL */
    if (arg->kind == AST_IDENT) {
        s = find_sym(arg->sval);
        if (s != NULL && !s->is_const_value && s->storage == SC_FUNC)
            return 1;                     /* function designator in HL */
        if (s != NULL && !s->is_const_value && s->storage != SC_FUNC &&
            (s->is_array || type_ptr_depth(s->type) > 0))
            return 1;                     /* pointer/array value in HL */
    }
    if (arg->kind == AST_INT_LIT && arg->uval != AST_INT_UVAL_PLAIN_DECIMAL)
        return 0;                         /* streaming preserves non-decimal spelling */
    return ast_value_is_plain_int(arg);
}

static int ast_value_is_long_word(const struct AstNode *arg)
{
    struct Sym *s;

    if (arg == NULL || !ast_gen_supported(arg))
        return 0;
    if (arg->kind == AST_INT_LIT)
        return type_is_long(arg->type);
    if (arg->kind == AST_IDENT) {
        s = find_sym(arg->sval);
        return s != NULL && !s->is_const_value && s->storage != SC_FUNC &&
               !s->is_array && type_is_long(s->type);
    }
    return 0;
}

static int ast_call_arg_supported(struct Sym *fn_sym, int arg_index,
                                  const struct AstNode *arg)
{
    int want_type;
    int ptr_type;
    int no_deref;

    if (arg == NULL)
        return 0;
    if (ast_pointer_expr_type(arg, &ptr_type, &no_deref))
        return 1;
    if (!ast_gen_supported(arg))
        return 0;
    if (expected_arg_type(fn_sym, arg_index, &want_type)) {
        if (type_is_struct_object(want_type))
            return 0;
        if (type_is_float(want_type))
            return ast_value_is_float_word(arg) || ast_value_is_plain_int(arg);
        if (type_is_long(want_type))
            return ast_value_is_long_word(arg) || ast_value_is_plain_int(arg);
        return ast_call_arg_word_supported(arg);
    }
    return ast_call_arg_word_supported(arg) || ast_value_is_long_word(arg) ||
           ast_value_is_float_word(arg);
}

static int ast_value_is_float_word(const struct AstNode *arg)
{
    struct Sym *s;

    if (arg == NULL || !ast_gen_supported(arg))
        return 0;
    if (arg->kind == AST_FLOAT_LIT)
        return 1;
    if (arg->kind == AST_IDENT) {
        s = find_sym(arg->sval);
        return s != NULL && !s->is_const_value && s->storage != SC_FUNC &&
               !s->is_array && type_is_float(s->type);
    }
    return 0;
}

static int ast_value_is_pointer_word(const struct AstNode *n)
{
    struct Sym *s;
    if (n == NULL)
        return 0;
    switch (n->kind) {
    case AST_STR_LIT:
        return 1;
    case AST_UNARY:
        if (n->op == '&')
            return ast_address_of_supported(n->a);
        if (n->op == '*')
            return ast_deref_pointer_word_read(n);
        return 0;
    case AST_IDENT:
        s = find_sym(n->sval);
        return s != NULL && !s->is_const_value && s->storage != SC_FUNC &&
               (s->is_array || type_ptr_depth(s->type) > 0);
    case AST_CALL:
        if (n->a == NULL || n->a->kind != AST_IDENT)
            return 0;
        s = find_global(n->a->sval);
        return s != NULL && type_ptr_depth(s->type) > 0;
    default:
        return 0;
    }
}

static int ast_pointer_assign_rhs_supported(const struct AstNode *n)
{
    const struct AstNode *value;
    if (n == NULL)
        return 0;
    value = (n->kind == AST_CAST) ? n->a : n;
    return ast_gen_supported(value) && ast_value_is_pointer_word(value);
}

static void gen_int_lit(const struct AstNode *n)
{
    if (n->type & TYPE_LONG) {
        /* 32-bit literal: low half in HL, high half in DE. */
        fprintf(outf, "\tld hl,%ld\n", n->ival & 0xffffL);
        fprintf(outf, "\tld de,%ld\n", (n->ival >> 16) & 0xffffL);
    } else {
        fprintf(outf, "\tld hl,%ld\n", n->ival & 0xffffL);
    }
    g_expr_type = n->type;
}

static void gen_str_lit(const struct AstNode *n)
{
    /* Intern at emit time (the build deferred this codegen side effect); the
     * 1:1 substitution at gen_expr preserves source order, so the assigned
     * string id matches what the streaming path would have produced. */
    int sid = add_string_ex(n->sval, (int)n->ival);
    fprintf(outf, "\tld hl,S%d\n", sid);
    g_expr_type = TYPE_CHAR | TYPE_PTR;
}

static void gen_ident(const struct AstNode *n)
{
    const char *name = n->sval;
    struct Sym *s;

    /* Mirror gen_primary's prologue resets so the load-address tail observes
     * the same global state (notably current_field_bit_width == 0, which gates
     * the trailing bitfield extract). */
    current_field_bit_width = 0;
    current_field_bit_shift = 0;
    current_field_bit_mask = 0;
    g_array_decay_stride = 0;
    g_expr_no_deref = 0;

    /* stdin/stdout/stderr -> immediate FILE values 0/1/2, checked (as in the
     * streaming path) before symbol resolution. */
    if (!strcmp(name, "stdin")) {
        emit("\tld hl,0\n");
        g_expr_type = TYPE_INT;
        return;
    }
    if (!strcmp(name, "stdout")) {
        emit("\tld hl,1\n");
        g_expr_type = TYPE_INT;
        return;
    }
    if (!strcmp(name, "stderr")) {
        emit("\tld hl,2\n");
        g_expr_type = TYPE_INT;
        return;
    }

    s = find_sym(name);
    if (s == NULL) {
        int ei;
        for (ei = 0; ei < nenum_consts; ++ei) {
            if (!strcmp(enum_const_names[ei], name)) {
                long ev = (long)(int)enum_const_values[ei];
                fprintf(outf, "\tld hl,%ld\n", ev & 0xffffL);
                g_expr_type = TYPE_INT;
                return;
            }
        }
        /* ast_gen_supported() filters undefined idents to the streaming path. */
        fatal("ast_gen_expr: unresolved identifier");
        return;
    }

    /* Folded local const scalar -> immediate (helper sets g_expr_type). */
    if (s->is_const_value) {
        emit_load_const_sym_value(s);
        return;
    }

    /* A function name used as a value decays to its address. */
    if (s->storage == SC_FUNC) {
        fprintf(outf, "\tld hl,%s\n", asm_name_for(name));
        g_expr_type = type_add_ptr(s->type);
        return;
    }

    /* Local scalar reachable with a direct ix-relative load. */
    if (sym_can_ix_direct(s)) {
        emit_load_sym_value_direct(s);
        g_expr_type = s->type;
        return;
    }

    /* 16-bit global/extern: ld hl,(nn) direct load. */
    if (is_global_word_sym(s)) {
        emit_load_global_word_direct(s);
        g_expr_type = s->type;
        return;
    }

    /* General case: load the symbol's address, then either decay an array to a
     * pointer-to-element or load the scalar value. */
    emit_load_sym_addr(s);
    if (s->is_array) {
        g_expr_type = type_add_ptr(s->type);
        if (s->dim_count > 1)
            g_array_decay_stride = sym_array_index_elem_size(s, 0);
    } else {
        g_expr_type = s->type;
        emit_load_from_hl(s->type);
    }
}

static void gen_unary_ast(const struct AstNode *n)
{
    int op = n->op;

    /* gen_unary clears the "freshly widened from 16-bit" marker on entry; the
     * long negate/complement paths below re-clear it after producing a value
     * that is no longer a faithful widening. */
    g_long_from16 = 0;

    if (op == '!') {
        /* Labels are allocated BEFORE the operand in gen_unary; preserve that
         * order so the global label counter (and emitted label numbers) match. */
        int lt = new_label();
        int le = new_label();
        ast_gen_expr(n->a);
        emit_test_expr_nonzero(g_expr_type, lt, 0);
        emit("\tld hl,0\n");
        emit_jp_label("jp", le);
        emit_label(lt);
        emit("\tld hl,1\n");
        emit_label(le);
        g_expr_type = TYPE_INT;
        return;
    }

    if (op == '&') {
        /* gen_lvalue_addr for a bare identifier: reset the field-bitfield
         * state, load the symbol's address, and yield pointer-to-element. */
        struct Sym *s;
        int val_type;
        current_field_bit_width = 0;
        current_field_bit_shift = 0;
        current_field_bit_mask = 0;
        if (n->a->kind == AST_INDEX) {
            gen_index_addr_ast(n->a, &val_type);
            g_expr_type = type_add_ptr(val_type);
            return;
        }
        s = find_sym(n->a->sval);
        emit_load_sym_addr(s);
        g_expr_type = type_add_ptr(s->type);
        return;
    }

    if (op == '*') {
        /* try_gen_simple_deref_value fast path for `*bare_ident`: load the
         * pointer value (global word direct, else address + deref), then load
         * the pointed-to plain-int element. */
        struct Sym *s;
        int ptr_type;
        int no_deref;
        int base;
        if (n->a->kind != AST_IDENT) {
            gen_pointer_expr_ast(n->a, &ptr_type, &no_deref);
            base = no_deref ? ptr_type : type_decay_ptr(ptr_type);
            if ((base & 15) == TYPE_VOID)
                base = TYPE_CHAR;
            if (!type_is_struct_object(base))
                emit_load_from_hl(base);
            g_expr_type = base;
            g_long_from16 = 0;
            return;
        }
        s = find_sym(n->a->sval);
        if (is_global_word_sym(s)) {
            emit_load_global_word_direct(s);
        } else {
            emit_load_sym_addr(s);
            emit_load_from_hl(s->type);
        }
        base = type_decay_ptr(s->type);
        if ((base & 15) == TYPE_VOID)
            base = TYPE_CHAR;
        emit_load_from_hl(base);
        g_expr_type = base;
        return;
    }

    if (op == TOK_INC || op == TOK_DEC) {
        /* gen_lvalue_addr(bare ident) + emit_pre_incdec_lvalue: load the
         * object's address, then in-place increment/decrement, leaving the
         * updated value in HL with g_expr_type = the object type. */
        struct Sym *s = find_sym(n->a->sval);
        current_field_bit_width = 0;
        current_field_bit_shift = 0;
        current_field_bit_mask = 0;
        emit_load_sym_addr(s);
        emit_pre_incdec_lvalue(s->type, op);
        return;
    }

    ast_gen_expr(n->a);

    if (op == '+') {
        if (!type_is_float(g_expr_type) && !type_is_long(g_expr_type))
            g_expr_type = promote_int_type(g_expr_type);
        return;
    }

    if (op == '-') {
        if (type_is_float(g_expr_type)) {
            emit("\tld a,d\n\txor 80h\n\tld d,a\n");
        } else if (type_is_long(g_expr_type)) {
            int lneg_skip = new_label();
            emit("\tld a,l\n\tcpl\n\tld l,a\n");
            emit("\tld a,h\n\tcpl\n\tld h,a\n");
            emit("\tld a,e\n\tcpl\n\tld e,a\n");
            emit("\tld a,d\n\tcpl\n\tld d,a\n");
            emit("\tinc hl\n");
            emit("\tld a,h\n\tor l\n");
            emit_jp_label("jp nz,", lneg_skip);
            emit("\tinc de\n");
            emit_label(lneg_skip);
            g_long_from16 = 0;
        } else {
            emit("\txor a\n\tsub l\n\tld l,a\n\tld a,0\n\tsbc a,h\n\tld h,a\n");
            g_expr_type = promote_int_type(g_expr_type);
        }
        return;
    }

    /* op == '~' */
    if (type_is_long(g_expr_type)) {
        emit("\tld a,h\n\tcpl\n\tld h,a\n\tld a,l\n\tcpl\n\tld l,a\n");
        emit("\tld a,d\n\tcpl\n\tld d,a\n\tld a,e\n\tcpl\n\tld e,a\n");
        g_long_from16 = 0;
    } else {
        emit("\tld a,h\n\tcpl\n\tld h,a\n\tld a,l\n\tcpl\n\tld l,a\n");
        g_expr_type = promote_int_type(g_expr_type);
    }
}

/* Emit a plain-int binary operator, reproducing the streaming path's uniform
 * 16-bit sequence: evaluate lhs into HL, promote, capture the common type from
 * the rhs's stored peek, then push / eval rhs / ex de,hl / pop hl / dispatch.
 * Result type matches streaming: int for comparisons, common type otherwise. */
static void gen_binary_ast(const struct AstNode *n)
{
    int lhs_type;
    int common_type;

    ast_gen_expr(n->a);
    lhs_type = promote_int_type(g_expr_type);
    common_type = common_arith_type(lhs_type, n->peek_type);

    emit("\tpush hl\n");
    ast_gen_expr(n->b);
    emit("\tex de,hl\n\tpop hl\n");
    gen_binop_typed(n->op, common_type);

    if (is_cmp_op(n->op))
        g_expr_type = TYPE_INT;
    else
        g_expr_type = common_type;
    g_long_from16 = 0;
}

/* Emit a plain-int shift, reproducing the streaming path's non-literal shape:
 * evaluate lhs into HL, promote it (C89 integer promotion of the left operand;
 * the right operand does not participate in the usual conversions), push it,
 * evaluate rhs, move its low byte into B, restore HL, then run the shift loop.
 * Result type is the promoted left operand. */
static void gen_shift_ast(const struct AstNode *n)
{
    int lhs_type;

    ast_gen_expr(n->a);
    lhs_type = promote_int_type(g_expr_type);
    emit("\tpush hl\n");
    ast_gen_expr(n->b);
    emit("\tld b,l\n\tpop hl\n");
    emit_shift_loop(n->op, lhs_type);
    g_expr_type = lhs_type;
    g_long_from16 = 0;
}

static void gen_index_subscript_expr_ast(const struct AstNode *n)
{
    if (ast_index_subscript_binary_literal(n)) {
        int lhs_type;
        int common_type;
        ast_gen_expr(n->a);
        lhs_type = promote_int_type(g_expr_type);
        common_type = common_arith_type(lhs_type, n->b->type);
        emit("\tpush hl\n");
        gen_int_lit(n->b);
        emit("\tex de,hl\n\tpop hl\n");
        gen_binop_typed(n->op, common_type);
        g_expr_type = common_type;
        g_long_from16 = 0;
        return;
    }
    ast_gen_expr(n);
}

/* Emit a plain `int_local = rhs` reproducing streaming gen_assign's general
 * (non-fast-path) `=` tail for a 16-bit int lhs: evaluate the rhs into HL,
 * apply integer promotion to a byte rhs, then store with the same direct
 * helper.  The gate guarantees the lhs is an IX-direct plain-int scalar and
 * the rhs is a byte-identical supported plain-int expression. */
static void gen_assign_ast(const struct AstNode *n)
{
    struct Sym *s;
    int common_type;
    int binop;
    int saved_dead;

    /* Non-identifier lvalue store: a subscript element `arr[i]`, a struct field
     * `s.f` / `p->f`, or a deref `*p`.  Reproduce streaming's normal_assign
     * tail: the address machine differs per lvalue kind (factored helpers), but
     * the store tail is uniform.  Handles both plain `=` and the
     * arithmetic/bitwise compound operators; shift-assigns and any
     * wider/pointer element are excluded by the gate. */
    if (n->a->kind == AST_INDEX || n->a->kind == AST_MEMBER ||
        (n->a->kind == AST_UNARY && n->a->op == '*')) {
        int val_type;
        int want_dead = expr_result_dead;

        if (n->a->kind == AST_INDEX)
            gen_index_addr_ast(n->a, &val_type);    /* HL = element address */
        else if (n->a->kind == AST_MEMBER)
            gen_member_addr_ast(n->a, &val_type);   /* HL = field address */
        else
            gen_deref_addr_ast(n->a, &val_type);    /* HL = target address */

        if (n->op == '=') {
            emit("\tpush hl\n");

            saved_dead = expr_result_dead;
            expr_result_dead = 0;
            if (type_ptr_depth(val_type) > 0 && n->b->kind == AST_CAST)
                ast_gen_expr(n->b->a);              /* rhs -> HL */
            else
                ast_gen_expr(n->b);                 /* rhs -> HL */
            expr_result_dead = saved_dead;

            emit("\tex de,hl\n\tpop hl\n");         /* DE = value, HL = address */
            emit_store_de_to_addr_hl(val_type);
            if (!want_dead)
                emit("\tex de,hl\n");
            g_long_from16 = 0;
            return;
        }

        /* Compound assignment to a non-identifier lvalue.  Streaming's general
         * normal_assign compound tail: save the address, load the current
         * value, save it, evaluate the RHS, combine, then store back (also
         * leaving the result in HL when the statement value is live). */
        switch (n->op) {
        case TOK_ADDEQ: binop = '+'; break;
        case TOK_SUBEQ: binop = '-'; break;
        case TOK_MULEQ: binop = '*'; break;
        case TOK_DIVEQ: binop = '/'; break;
        case TOK_MODEQ: binop = '%'; break;
        case TOK_ANDEQ: binop = '&'; break;
        case TOK_OREQ:  binop = '|'; break;
        default:        binop = '^'; break;   /* TOK_XOREQ */
        }

        emit("\tpush hl\n");                    /* save lvalue address */
        emit_load_from_hl(val_type);            /* HL = current value */
        emit("\tpush hl\n");                    /* save current value */

        saved_dead = expr_result_dead;
        expr_result_dead = 0;
        ast_gen_expr(n->b);                     /* rhs -> HL */
        expr_result_dead = saved_dead;

        emit("\tex de,hl\n\tpop hl\n");         /* DE = rhs, HL = current value */
        common_type = common_arith_type(val_type, g_expr_type);
        gen_binop_typed(binop, common_type);    /* HL = result */
        emit("\tex de,hl\n\tpop hl\n");         /* DE = result, HL = address */
        emit_store_de_to_addr_hl(val_type);
        if (!want_dead)
            emit("\tex de,hl\n");
        g_long_from16 = 0;
        return;
    }

    s = find_sym(n->a->sval);

    if (n->op == '=' && type_is_long(s->type) && sym_can_ix_direct(s)) {
        saved_dead = expr_result_dead;
        expr_result_dead = 0;
        ast_gen_expr(n->b);
        expr_result_dead = saved_dead;
        if (!type_is_long(g_expr_type))
            emit_extend_to_long_typed(g_expr_type);
        emit_store_hl_to_sym_direct(s);
        g_long_from16 = 0;
        return;
    }

    if (n->op == '=' && !sym_can_ix_direct(s) && !is_global_word_sym(s) &&
        ast_is_plain_int_type(s->type) &&
        (type_size(s->type) == 1 || type_size(s->type) == 2)) {
        int want_dead = expr_result_dead;

        emit_load_sym_addr(s);
        emit("\tpush hl\n");
        saved_dead = expr_result_dead;
        expr_result_dead = 0;
        ast_gen_expr(n->b);
        expr_result_dead = saved_dead;
        if (type_size(s->type) > 1)
            emit_promote_byte_to_int(g_expr_type);
        emit("\tex de,hl\n\tpop hl\n");
        emit_store_de_to_addr_hl(s->type);
        if (!want_dead)
            emit("\tex de,hl\n");
        g_long_from16 = 0;
        return;
    }

    if (expr_result_dead &&
        (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ) &&
        !type_is_long(s->type) && !type_is_float(s->type) &&
        (n->b->kind == AST_INT_LIT || n->b->kind == AST_IDENT)) {
        emit_load_sym_value_direct(s);
        if (n->b->kind == AST_INT_LIT) {
            long scaled = n->b->ival;
            if (s->type & (TYPE_PTR | TYPE_PTR2))
                scaled *= type_index_elem_size(s->type);
            emit_ld_de_const(scaled);
        } else {
            struct Sym *rs = find_sym(n->b->sval);
            emit_load_sym_de_direct(rs);
            if (s->type & (TYPE_PTR | TYPE_PTR2)) {
                int elem = type_index_elem_size(s->type);
                if (elem > 1) {
                    emit("\tpush hl\n");
                    emit("\tex de,hl\n");
                    scale_hl_by_elem_size(elem);
                    emit("\tex de,hl\n");
                    emit("\tpop hl\n");
                }
            }
        }
        if (n->op == TOK_ADDEQ)
            emit("\tadd hl,de\n");
        else
            emit("\tor a\n\tsbc hl,de\n");
        emit_store_hl_to_sym_direct(s);
        g_long_from16 = 0;
        return;
    }

    if (n->op == '=') {
        if (type_size(s->type) == 1 &&
            (s->storage == SC_GLOBAL || s->storage == SC_EXTERN)) {
            int want_dead = expr_result_dead;
            int saved_dead;
            emit_load_sym_addr(s);
            emit("\tpush hl\n");
            saved_dead = expr_result_dead;
            expr_result_dead = 0;
            ast_gen_expr(n->b);
            expr_result_dead = saved_dead;
            emit("\tex de,hl\n\tpop hl\n");
            emit_store_de_to_addr_hl(s->type);
            if (!want_dead)
                emit("\tex de,hl\n");
            g_expr_type = s->type;
            g_long_from16 = 0;
            return;
        }
        if (type_size(s->type) == 1 && n->b->kind == AST_IDENT) {
            struct Sym *rs = find_sym(n->b->sval);
            fprintf(outf, "\tld a,(ix%+d)\n", rs->offset);
            fprintf(outf, "\tld (ix%+d),a\n", s->offset);
            g_expr_type = s->type;
            g_long_from16 = 0;
            return;
        }
        if (type_size(s->type) == 1 && n->b->kind == AST_INT_LIT) {
            fprintf(outf, "\tld (ix%+d),%ld\n", s->offset, n->b->ival & 255);
            g_expr_type = s->type;
            g_long_from16 = 0;
            return;
        }
        if (type_ptr_depth(s->type) > 0 && n->b->kind == AST_CAST)
            ast_gen_expr(n->b->a);
        else
            ast_gen_expr(n->b);
        emit_promote_byte_to_int(g_expr_type);
        emit_store_hl_to_sym_direct(s);
        g_long_from16 = 0;
        return;
    }

    /* Compound assignment to a plain-int 16-bit scalar.  Reproduces streaming's
     * GENERAL compound tail in gen_assign: load the LHS value, push it,
     * evaluate the RHS, then combine with the usual arithmetic conversions and
     * store the result back (also leaving it in HL). */
    switch (n->op) {
    case TOK_ADDEQ: binop = '+'; break;
    case TOK_SUBEQ: binop = '-'; break;
    case TOK_MULEQ: binop = '*'; break;
    case TOK_DIVEQ: binop = '/'; break;
    case TOK_MODEQ: binop = '%'; break;
    case TOK_ANDEQ: binop = '&'; break;
    case TOK_OREQ:  binop = '|'; break;
    default:        binop = '^'; break;   /* TOK_XOREQ */
    }

    emit_load_sym_value_direct(s);
    emit("\tpush hl\n");

    saved_dead = expr_result_dead;
    expr_result_dead = 0;
    ast_gen_expr(n->b);
    expr_result_dead = saved_dead;

    emit("\tex de,hl\n\tpop hl\n");
    common_type = common_arith_type(s->type, g_expr_type);
    gen_binop_typed(binop, common_type);
    emit_store_hl_to_sym_direct(s);
    g_long_from16 = 0;
}

/* Emit a plain-int subscript read `base[index]`, reproducing the streaming
 * path's IDENTIFIER-ROOTED subscript machine in gen_primary (NOT the postfix
 * chain - the two use different base loads and element-size helpers).  The
 * gate (ast_index_plain_int_read) guarantees a bare-identifier base that is a
 * 1-D plain-int array or an int* pointer, with a supported, non-constant
 * plain-int index, so exactly one subscript iteration runs and the element
 * load is a plain 16-bit load. */
static void gen_index_addr_ast(const struct AstNode *n, int *out_val_type)
{
    struct Sym *s = NULL;
    int cur_type;
    int val_type;
    int elem_size;
    int global_ptr_preloaded = 0;
    int field_array = 0;
    int fa_dimc = 0;
    int fa_dims[4];
    int di;

    for (di = 0; di < 4; ++di)
        fa_dims[di] = 0;

    if (ast_index_2d_addressable_addr(n)) {
        const struct AstNode *outer = n->a;
        s = find_sym(outer->a->sval);
        emit_load_sym_addr(s);
        elem_size = sym_array_index_elem_size(s, 0);
        if (outer->b->kind == AST_INT_LIT) {
            emit_add_const_to_hl(outer->b->ival * elem_size);
        } else {
            emit("\tpush hl\n");
            gen_index_subscript_expr_ast(outer->b);
            scale_hl_by_elem_size(elem_size);
            emit("\tex de,hl\n");
            emit("\tpop hl\n");
            emit("\tadd hl,de\n");
        }
        elem_size = sym_array_index_elem_size(s, 1);
        if (n->b->kind == AST_INT_LIT) {
            emit_add_const_to_hl(n->b->ival * elem_size);
        } else {
            emit("\tpush hl\n");
            gen_index_subscript_expr_ast(n->b);
            scale_hl_by_elem_size(elem_size);
            emit("\tex de,hl\n");
            emit("\tpop hl\n");
            emit("\tadd hl,de\n");
        }
        *out_val_type = s->type;
        return;
    }

    if (n->a != NULL && n->a->kind == AST_INDEX &&
        n->a->a != NULL && n->a->a->kind == AST_MEMBER) {
        const struct AstNode *outer = n->a;
        int dimc;
        int dims[4];
        if (ast_index_2d_array_elem_type(n, &val_type)) {
            gen_member_addr_ast(outer->a, &cur_type);
            dimc = current_field_array_dim_count;
            for (di = 0; di < 4; ++di)
                dims[di] = current_field_array_dims[di];
            elem_size = ast_field_array_index_stride(type_size(cur_type), dimc, dims, 0);
            if (outer->b->kind == AST_INT_LIT) {
                emit_add_const_to_hl(outer->b->ival * elem_size);
            } else {
                emit("\tpush hl\n");
                gen_index_subscript_expr_ast(outer->b);
                scale_hl_by_elem_size(elem_size);
                emit("\tex de,hl\n");
                emit("\tpop hl\n");
                emit("\tadd hl,de\n");
            }
            elem_size = ast_field_array_index_stride(type_size(cur_type), dimc, dims, 1);
            if (n->b->kind == AST_INT_LIT) {
                emit_add_const_to_hl(n->b->ival * elem_size);
            } else {
                emit("\tpush hl\n");
                gen_index_subscript_expr_ast(n->b);
                scale_hl_by_elem_size(elem_size);
                emit("\tex de,hl\n");
                emit("\tpop hl\n");
                emit("\tadd hl,de\n");
            }
            *out_val_type = val_type;
            return;
        }
    }

    if (n->a->kind == AST_MEMBER) {
        gen_member_addr_ast(n->a, &val_type);
        cur_type = val_type;
        field_array = 1;
        fa_dimc = current_field_array_dim_count;
        for (di = 0; di < 4; ++di)
            fa_dims[di] = current_field_array_dims[di];
    } else {
        s = find_sym(n->a->sval);

        /* Base load: a global pointer immediately subscripted loads its value with
         * a direct ld hl,(nn); arrays and local pointers load their address. */
        if (is_global_word_sym(s) && !s->is_array && type_ptr_depth(s->type) > 0) {
            emit_load_global_word_direct(s);
            global_ptr_preloaded = 1;
        } else {
            emit_load_sym_addr(s);
        }

        val_type = s->type;
        cur_type = val_type;

        /* A pointer variable is dereferenced to its target before indexing; an
         * array symbol already denotes the address of its first element. */
        if (!s->is_array && type_ptr_depth(cur_type) > 0) {
            if (!global_ptr_preloaded)
                emit_load_from_hl(cur_type);
        }
    }

    if (field_array)
        elem_size = ast_field_array_index_stride(type_size(cur_type), fa_dimc, fa_dims, 0);
    else if (s->is_array)
        elem_size = sym_array_index_elem_size(s, 0);
    else
        elem_size = sym_pointer_array_index_elem_size(s, cur_type, 0);

    if (n->b->kind == AST_INT_LIT) {
        emit_add_const_to_hl(n->b->ival * elem_size);
    } else {
        emit("\tpush hl\n");
        gen_index_subscript_expr_ast(n->b); /* index -> HL (== streaming gen_expr()) */
        scale_hl_by_elem_size(elem_size);
        emit("\tex de,hl\n");
        emit("\tpop hl\n");
        emit("\tadd hl,de\n");        /* HL = element address */
    }

    if (field_array)
        val_type = cur_type;
    else if (s->is_array)
        val_type = cur_type;          /* element type of the 1-D array */
    else
        val_type = type_decay_ptr(cur_type);

    *out_val_type = val_type;
}

/* Emit a plain-int subscript read `base[index]`, reproducing the streaming
 * path's IDENTIFIER-ROOTED subscript machine in gen_primary (NOT the postfix
 * chain - the two use different base loads and element-size helpers).  The
 * gate (ast_index_plain_int_read) guarantees a bare-identifier base that is a
 * 1-D plain-int array or an int* pointer, with a supported, non-constant
 * plain-int index, so exactly one subscript iteration runs and the element
 * load is a plain 16-bit load. */
static void gen_index_ast(const struct AstNode *n)
{
    int val_type;

    gen_index_addr_ast(n, &val_type);
    g_expr_type = val_type;
    emit_load_from_hl(val_type);
    if (current_field_bit_width > 0)
        emit_extract_bitfield();
}

/* Emit a direct named call `f(a, b, ...)`, reproducing streaming gen_primary's
 * named-call tail: the C89 implicit-declaration side effect, reverse-order
 * argument evaluation and push (one 16-bit word each), the deferred-EXTRN
 * bookkeeping, `call <asm-name>`, and the stack cleanup.  The gate guarantees a
 * direct (non function pointer) callee, all-plain-int arguments and no
 * prototype-driven widening, so each argument pushes exactly one word and none
 * of the builtin fast paths apply. */
static void gen_call_ast(const struct AstNode *n)
{
    const char *name = n->a->sval;
    struct Sym *fn_sym = find_global(name);
    int arg_bytes = 0;
    int old_dead;
    int i;

    /* A call to an undeclared identifier declares an external int-returning
     * function; replicate the emit-time mutation so deferred EXTRN output is
     * identical to the streaming path. */
    if (fn_sym == NULL) {
        fn_sym = add_global(name, TYPE_INT, SC_FUNC);
        fn_sym->needs_extrn = 1;
    }

    /* Push arguments right-to-left, one 16-bit word each (matching the
     * streaming prototype-16-bit / default-int push), with call arguments
     * forced live across evaluation exactly as streaming does. */
    old_dead = expr_result_dead;
    expr_result_dead = 0;
    for (i = n->list_len - 1; i >= 0; --i) {
        int actual_type;
        int want_type;
        int have_want;
        int ptr_type;
        int no_deref;

        if (ast_pointer_expr_type(n->list[i], &ptr_type, &no_deref))
            gen_pointer_expr_ast(n->list[i], &ptr_type, &no_deref);
        else
            ast_gen_expr(n->list[i]);
        actual_type = g_expr_type;
        have_want = expected_arg_type(fn_sym, i, &want_type);
        if (have_want && type_is_float(want_type)) {
            if (!type_is_float(actual_type))
                emit_convert_int_to_float(actual_type);
            emit("\tpush de\n\tpush hl\n");
            arg_bytes += 4;
        } else if (have_want && type_is_long(want_type)) {
            if (!type_is_long(actual_type))
                emit_promote_int_to_long(actual_type, want_type);
            emit("\tpush de\n\tpush hl\n");
            arg_bytes += 4;
        } else if (type_is_long(actual_type) || type_is_float(actual_type)) {
            emit("\tpush de\n\tpush hl\n");
            arg_bytes += 4;
        } else {
            emit("\tpush hl\n");
            arg_bytes += 2;
        }
    }
    expr_result_dead = old_dead;

    emit_extrn_if_needed(fn_sym);
    fprintf(outf, "\tcall %s\n", asm_name_for(name));
    g_expr_type = fn_sym->type;
    g_long_from16 = 0;

    emit_cleanup_stack_bytes(arg_bytes);
}

/* Emit a single struct field read `id.f` / `id->f`, reproducing the streaming
 * identifier-rooted field machine: load the base address, dereference once for
 * `->`, add the field offset, publish the field metadata into the current_field_*
 * globals exactly as apply_field_access_from_addr does (so subsequent state is
 * identical), then load the scalar value.  The gate guarantees a plain 16-bit
 * int, non-array, non-bitfield field, so the load is a plain load and the
 * bitfield extract never fires. */
static void gen_member_addr_ast(const struct AstNode *n, int *out_val_type)
{
    struct Sym *s;
    int arrow = (n->op == TOK_ARROW);
    int cur_type;
    int no_deref;
    struct FieldDef *fd;
    int sid;
    int di;
    int val_type;

    if (arrow && n->a->kind == AST_INDEX &&
        ast_pointer_expr_type(n->a, &cur_type, &no_deref)) {
        gen_pointer_expr_ast(n->a, &cur_type, &no_deref);
    } else if (n->a->kind == AST_INDEX) {
        gen_index_addr_ast(n->a, &cur_type);
    } else if (arrow && n->a->kind != AST_IDENT &&
               ast_pointer_expr_type(n->a, &cur_type, &no_deref)) {
        gen_pointer_expr_ast(n->a, &cur_type, &no_deref);
    } else if (!arrow && n->a->kind == AST_UNARY && n->a->op == '*' &&
               ast_pointer_expr_type(n->a->a, &cur_type, &no_deref)) {
        gen_pointer_expr_ast(n->a->a, &cur_type, &no_deref);
        cur_type = type_decay_ptr(cur_type);
        if ((cur_type & 15) == TYPE_VOID)
            cur_type = TYPE_CHAR;
    } else {
        s = find_sym(n->a->sval);
        cur_type = s->type;
        emit_load_sym_addr(s);

        if (arrow)
            emit_load_from_hl(cur_type);
    }

    sid = base_struct_id_from_type(cur_type);
    fd = find_field_def(sid, n->sval);
    emit_add_field_offset(fd);            /* HL = field address */

    /* Mirror apply_field_access_from_addr's field-metadata publication so the
     * global state after this node matches the streaming path byte-for-byte. */
    current_field_array_elem_size = fd->elem_size ? fd->elem_size
                                                  : type_size(fd->type);
    if (fd->is_array && type_is_float(fd->elem_type))
        current_field_array_elem_size = 4;
    else if (!fd->is_array && type_is_float(fd->type))
        current_field_array_elem_size = 4;
    current_field_array_dim_count = fd->dim_count;
    for (di = 0; di < 4; ++di)
        current_field_array_dims[di] = fd->dims[di];
    current_field_bit_width = fd->bit_width;
    current_field_bit_shift = fd->bit_shift;
    current_field_bit_mask = fd->bit_mask;

    val_type = fd->is_array ? fd->elem_type : fd->type;
    *out_val_type = val_type;
}

static void gen_member_ast(const struct AstNode *n)
{
    int val_type;

    gen_member_addr_ast(n, &val_type);
    g_expr_type = val_type;
    emit_load_from_hl(val_type);
    if (current_field_bit_width > 0)
        emit_extract_bitfield();
}

static void gen_pointer_expr_ast(const struct AstNode *n, int *out_type,
                                 int *out_no_deref)
{
    int ptr_type;
    int no_deref;
    int base;

    if (n->kind == AST_IDENT) {
        ast_gen_expr(n);
        *out_type = g_expr_type;
        *out_no_deref = 0;
        return;
    }

    if (n->kind == AST_UNARY && n->op == '*') {
        gen_pointer_expr_ast(n->a, &ptr_type, &no_deref);
        if (no_deref) {
            *out_type = ptr_type;
            *out_no_deref = 0;
            return;
        }
        base = type_decay_ptr(ptr_type);
        if ((base & 15) == TYPE_VOID)
            base = TYPE_CHAR;
        if (!type_is_struct_object(base))
            emit_load_from_hl(base);
        g_expr_type = base;
        *out_type = base;
        *out_no_deref = 0;
        return;
    }

    if (n->kind == AST_UNARY && n->op == '&') {
        ast_gen_expr(n);
        *out_type = g_expr_type;
        *out_no_deref = 0;
        return;
    }

    if (n->kind == AST_BINARY && n->op == '+') {
        int elem;
        int was_row_ptr;
        int saved_dead;

        gen_pointer_expr_ast(n->a, &ptr_type, &no_deref);
        was_row_ptr = (g_array_decay_stride > 0);
        elem = was_row_ptr ? g_array_decay_stride : type_index_elem_size(ptr_type);
        g_array_decay_stride = 0;

        emit("\tpush hl\n");
        saved_dead = expr_result_dead;
        expr_result_dead = 0;
        ast_gen_expr(n->b);
        expr_result_dead = saved_dead;
        scale_hl_by_elem_size(elem);
        emit("\tex de,hl\n");
        emit("\tpop hl\n");
        emit("\tadd hl,de\n");
        g_expr_type = ptr_type;
        g_long_from16 = 0;
        *out_type = ptr_type;
        *out_no_deref = was_row_ptr;
        return;
    }

    if (n->kind == AST_MEMBER) {
        int member_type;
        gen_member_addr_ast(n, &member_type);
        if (ast_member_array_field_elem_type(n, &member_type)) {
            g_expr_type = type_add_ptr(member_type);
            g_long_from16 = 0;
            *out_type = g_expr_type;
            *out_no_deref = 0;
            return;
        }
        emit_load_from_hl(member_type);
        g_expr_type = member_type;
        g_long_from16 = 0;
        *out_type = member_type;
        *out_no_deref = 0;
        return;
    }

    if (n->kind == AST_INDEX) {
        int member_type;
        gen_index_addr_ast(n, &member_type);
        emit_load_from_hl(member_type);
        g_expr_type = member_type;
        g_long_from16 = 0;
        *out_type = member_type;
        *out_no_deref = 0;
        return;
    }

    if (n->kind == AST_CALL) {
        ast_gen_expr(n);
        *out_type = g_expr_type;
        *out_no_deref = 0;
        return;
    }
}

/* Emit the target ADDRESS of `*ident` into HL for an lvalue store, mirroring
 * streaming's try_gen_deref_postinc_lvalue_addr (non-postinc branch): an
 * IX-direct local pointer or a global word pointer loads its value directly;
 * otherwise the symbol address is loaded and dereferenced.  This is NOT the
 * same byte sequence as the deref value-read path (gen_unary_ast '*'), which is
 * why the store needs its own helper. */
static void gen_deref_addr_ast(const struct AstNode *n, int *out_val_type)
{
    struct Sym *s;
    int no_deref;
    int ptr_type;
    int base;

    if (n->a->kind != AST_IDENT) {
        gen_pointer_expr_ast(n->a, &ptr_type, &no_deref);
        base = no_deref ? ptr_type : type_decay_ptr(ptr_type);
        if ((base & 15) == TYPE_VOID)
            base = TYPE_CHAR;
        *out_val_type = base;
        return;
    }

    s = find_sym(n->a->sval);

    if (sym_can_ix_direct(s) || is_global_word_sym(s)) {
        emit_load_sym_value_direct(s);
    } else {
        emit_load_sym_addr(s);
        emit_load_from_hl(s->type);
    }

    base = type_decay_ptr(s->type);
    if ((base & 15) == TYPE_VOID)
        base = TYPE_CHAR;
    *out_val_type = base;
}

/* Emit a short-circuit `a && b` (AST_LOGAND) or `a || b` (AST_LOGOR),
 * reproducing one iteration of streaming's gen_land / gen_lor loop: evaluate
 * the left operand, test it, conditionally short-circuit, otherwise evaluate
 * and test the right operand, and materialise a 0/1 result in HL.  The builder
 * nests chained operators left-associatively, so the recursive evaluation of
 * the left operand allocates its labels first - exactly as the streaming loop
 * does across successive iterations - keeping label numbers identical. */
static void gen_logical_ast(const struct AstNode *n)
{
    int lhs_type;
    int le;

    if (n->kind == AST_LOGAND) {
        int lf = 0;
        ast_gen_expr(n->a);
        lhs_type = g_expr_type;
        lf = new_label();
        le = new_label();
        emit_test_expr_nonzero(lhs_type, lf, 0);
        ast_gen_expr(n->b);
        emit_test_expr_nonzero(g_expr_type, lf, 0);
        emit("\tld hl,1\n");
        emit_jp_label("jp", le);
        emit_label(lf);
        emit("\tld hl,0\n");
        emit_label(le);
    } else {
        int lt = 0;
        ast_gen_expr(n->a);
        lhs_type = g_expr_type;
        lt = new_label();
        le = new_label();
        emit_test_expr_nonzero(lhs_type, lt, 1);
        ast_gen_expr(n->b);
        emit_test_expr_nonzero(g_expr_type, lt, 1);
        emit("\tld hl,0\n");
        emit_jp_label("jp", le);
        emit_label(lt);
        emit("\tld hl,1\n");
        emit_label(le);
    }
    g_expr_type = TYPE_INT;
}

/* Emit a plain-int conditional `cond ? a : b` (AST_COND), reproducing streaming
 * gen_conditional's neither-float-nor-long path: evaluate the condition (which,
 * being evaluated before the labels are allocated, matches gen_lor's label
 * ordering), test it, emit the true arm and speculatively widen it to long
 * exactly as streaming does (consumers read HL and ignore the stale DE for a
 * 16-bit result), jump to the end, then emit the false arm.  The result type is
 * the C89-balanced common type of the two arms. */
static void gen_cond_ast(const struct AstNode *n)
{
    int lfalse;
    int lend;
    int true_type;
    int false_type;

    ast_gen_expr(n->a);                 /* condition (== streaming gen_lor) */
    lfalse = new_label();
    lend = new_label();
    emit_test_expr_nonzero(g_expr_type, lfalse, 0);

    ast_gen_expr(n->b);                 /* true arm */
    true_type = g_expr_type;
    emit_extend_to_long((true_type & TYPE_UNSIGNED) ||
                        (true_type & (TYPE_PTR | TYPE_PTR2)));
    emit_jp_label("jp", lend);

    emit_label(lfalse);
    ast_gen_expr(n->c);                 /* false arm */
    false_type = g_expr_type;

    emit_label(lend);
    g_expr_type = common_arith_type(true_type, false_type);
    g_long_from16 = 0;
}

/* Emit a postfix `lv++` / `lv--` on a bare plain-int identifier, reproducing
 * try_emit_post_update_sym_direct's non-pointer branch: load the old value
 * (the expression result), save it, increment/decrement in HL, store the new
 * value back, then restore the old value as the result. */
static void gen_postfix_ast(const struct AstNode *n)
{
    struct Sym *s = find_sym(n->a->sval);

    emit_load_sym_value_direct(s);      /* HL = old value (result) */
    emit("\tpush hl\n");
    if (n->op == TOK_INC)
        emit("\tinc hl\n");
    else
        emit("\tdec hl\n");
    emit_store_hl_to_sym_direct(s);     /* store new value */
    emit("\tpop hl\n");                 /* old value = expression result */
    g_expr_type = s->type;
}

void ast_gen_expr(const struct AstNode *n)
{
    switch (n->kind) {
    case AST_INT_LIT:
        gen_int_lit(n);
        break;
    case AST_SIZEOF_TYPE:
        fprintf(outf, "\tld hl,%ld\n", n->ival & 0xffffL);
        g_expr_type = TYPE_INT;
        break;
    case AST_FLOAT_LIT:
        emit_load_float_bits(n->uval);
        g_expr_type = TYPE_FLOAT;
        break;
    case AST_STR_LIT:
        gen_str_lit(n);
        break;
    case AST_IDENT:
        gen_ident(n);
        break;
    case AST_UNARY:
        gen_unary_ast(n);
        break;
    case AST_BINARY:
        if (is_shift_op(n->op))
            gen_shift_ast(n);
        else
            gen_binary_ast(n);
        break;
    case AST_ASSIGN:
        gen_assign_ast(n);
        break;
    case AST_INDEX:
        gen_index_ast(n);
        break;
    case AST_CALL:
        gen_call_ast(n);
        break;
    case AST_MEMBER:
        gen_member_ast(n);
        break;
    case AST_LOGAND:
    case AST_LOGOR:
        gen_logical_ast(n);
        break;
    case AST_COND:
        gen_cond_ast(n);
        break;
    case AST_POSTFIX:
        gen_postfix_ast(n);
        break;
    default:
        /* ast_gen_supported() gates entry; reaching here is a bug. */
        fatal("ast_gen_expr: unsupported node");
    }
}

/* ------------------------------------------------------------------------- *
 * Phase 4: statement-level AST codegen (first slice: `return [expr] ;`).
 *
 * A statement hook in gen_statement (gated on g_ast_gen_enabled) builds the
 * upcoming statement from the token stream and, when the whole statement is in
 * the supported subset, emits it from the AST - consuming the same tokens the
 * streaming path would.  Anything unsupported restores the lexer snapshot so
 * the streaming statement codegen runs unchanged.
 * ------------------------------------------------------------------------- */

/* Gate for `return [expr] ;`.  Restricted to return types where streaming's
 * gen_return emits exactly `evaluate value; jp <return label>` (or just the
 * jump for a bare `return;`): plain 16-bit int scalars and pointer words.
 * Structs, bytes, longs and floats all have return-specific handling and defer
 * to streaming. */
static int ast_return_stmt_supported(const struct AstNode *n)
{
    int rt = current_return_type;

    if (type_is_struct_object(rt) || type_is_float(rt) || type_is_long(rt))
        return 0;
    if ((rt & 15) == TYPE_VOID)
        return n->a == NULL;
    if (rt & (TYPE_PTR | TYPE_PTR2)) {
        int ptr_type;
        int no_deref;
        if (n->a == NULL)
            return 1;
        if (ast_pointer_assign_rhs_supported(n->a))
            return 1;
        return ast_pointer_expr_type(n->a, &ptr_type, &no_deref);
    }
    if (type_size(rt) == 1) {
        if (n->a == NULL)
            return 1;
        if (n->a->kind == AST_IDENT) {
            struct Sym *rs = find_sym(n->a->sval);
            return sym_can_ix_direct(rs) && type_size(rs->type) == 1;
        }
        return n->a->kind == AST_INT_LIT && n->a->ival >= 0 &&
               n->a->ival <= 255 &&
               (n->a->uval == AST_INT_UVAL_CHARLIT ||
                n->a->uval == AST_INT_UVAL_PLAIN_DECIMAL);
    }
    if ((rt & 15) != TYPE_INT || type_size(rt) != 2)
        return 0;

    if (n->a != NULL) {
        if (!ast_gen_supported(n->a) || !ast_value_is_plain_int(n->a))
            return 0;
    }
    return 1;
}

/* Emit `return [expr] ;` for a plain-int return type, reproducing gen_return's
 * general (non-fast-path) tail: evaluate the value into HL (when present), then
 * jump to the function's shared return label. */
static void gen_return_ast(const struct AstNode *n)
{
    if (n->a != NULL && type_size(current_return_type) == 1) {
        if (n->a->kind == AST_IDENT) {
            struct Sym *rs = find_sym(n->a->sval);
            fprintf(outf, "\tld l,(ix%+d)\n", rs->offset);
            if (current_return_type & TYPE_UNSIGNED)
                emit("\tld h,0\n");
            else
                emit("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n");
            g_expr_type = current_return_type;
        } else {
            fprintf(outf, "\tld hl,%ld\n", n->a->ival & 255);
            g_expr_type = current_return_type;
        }
    } else if (n->a != NULL) {
        int ptr_type;
        int no_deref;
        if ((current_return_type & (TYPE_PTR | TYPE_PTR2)) &&
            n->a->kind == AST_CAST)
            ast_gen_expr(n->a->a);
        else if ((current_return_type & (TYPE_PTR | TYPE_PTR2)) &&
                 ast_pointer_expr_type(n->a, &ptr_type, &no_deref))
            gen_pointer_expr_ast(n->a, &ptr_type, &no_deref);
        else
            ast_gen_expr(n->a);
    }
    emit_jp_label("jp", current_return_label);
}

/* A comparison operand that reaches streaming's plain-16-bit direct-branch
 * path: a non-const, non-array, size-2 plain-int (signed/unsigned) or pointer
 * identifier reachable by the direct load (IX-direct local/param or global
 * word).  A size-1 (char/byte) operand would instead trigger the byte
 * relational fast path, and a constant operand the small-const-int relational
 * fast path, so both are excluded here. */
static int ast_cmp_operand_ok(const struct AstNode *e)
{
    struct Sym *s;
    if (e == NULL)
        return 0;
    /* A struct field read `s.f` / `p->f` of a plain INT (size-2) field also
     * reaches streaming's general plain-16-bit compare path: the leading token
     * is a struct/pointer identifier immediately followed by `.`/`->`, so
     * parse_byte_operand_fast declines (a member is neither a bare byte ident
     * nor a global byte array), gen_direct_small_const_int_rel declines (the
     * token after the base ident is `.`/`->`, not a relop), gen_direct_byte_
     * bitand declines (no `&`), and gen_if's try_fast_global_char_array_
     * condition declines without emitting (a struct base is not a global char
     * array followed by `[`).  Restricted to a 2-byte field so no byte
     * relational path can intervene; ast_member_plain_int_read already excludes
     * pointer/struct/array/bitfield fields, so the value is a true 16-bit int
     * (ptr_cmp detection stays 0, matching snippet_is_single_pointer_id). */
    if (e->kind == AST_MEMBER) {
        int field_type;
        if (!ast_member_plain_int_read(e))
            return 0;
        if (!ast_member_lvalue_type(e, &field_type) || type_size(field_type) != 2)
            return 0;
        return 1;
    }
    /* A pointer deref read `*p` of a 2-byte (int) element also reaches the
     * general plain-16-bit compare: the leading `*` is not TOK_ID, so the
     * global-char-array probe, the byte relational, and the small-const-int
     * relational fast paths all decline at their first-token test with no emit.
     * In the while context a comparison `*p OP x` is an AST_BINARY (the bare
     * `*ptr` truthiness handled by gen_while's parse_while_deref_nonzero_id is
     * excluded separately and needs `)`/`!= 0` after the deref), so the
     * bc-pointer fast paths decline without emitting too.  Restricted to a
     * size-2 element so no byte path intervenes; char* (size-1) defers. */
    if (e->kind == AST_UNARY && e->op == '*') {
        int base;
        if (!ast_deref_plain_int_read(e))
            return 0;
        base = type_decay_ptr(find_sym(e->a->sval)->type);
        if (type_size(base) != 2)
            return 0;
        return 1;
    }
    /* A subscript read `arr[i]` of a 2-byte (int) element reaches the general
     * compare as well.  A size-1 element would be a global *char* array (the
     * only subscript shape with a dead-probe in gen_if's try_fast_global_char_
     * array_condition) or a byte relational operand, so we require size 2: an
     * int array / int* base whose is_global_char_array_sym test is false, so
     * the global-char-array probe declines at is_global_char_array_sym with no
     * emit.  ast_index_plain_int_read already requires a bare-identifier base
     * and a non-constant index. */
    if (e->kind == AST_INDEX) {
        struct Sym *base;
        int decayed;
        int elem;
        if (!ast_index_plain_int_read(e))
            return 0;
        if (e->a->kind == AST_IDENT) {
            base = find_sym(e->a->sval);
            decayed = base->is_array ? type_add_ptr(base->type) : base->type;
            elem = type_decay_ptr(decayed);
        } else if (e->a->kind == AST_MEMBER) {
            if (!ast_member_plain_array_field_elem_type(e->a, &elem))
                return 0;
        } else {
            return 0;
        }
        if (type_size(elem) != 2)
            return 0;
        return 1;
    }
    if (e->kind != AST_IDENT)
        return 0;
    s = find_sym(e->sval);
    if (s == NULL)
        return 0;
    if (s->is_array || s->is_const_value || s->storage == SC_FUNC)
        return 0;
    if (type_is_struct_object(s->type) || type_is_long(s->type) ||
        type_is_float(s->type))
        return 0;
    if (type_size(s->type) != 2)
        return 0;                          /* excludes char (byte rel path) */
    if (!sym_can_ix_direct(s) && !is_global_word_sym(s))
        return 0;
    return 1;
}

/* Does identifier operand `e` name a pointer object?  Mirrors streaming's
 * snippet_is_single_pointer_id, which selects the unsigned compare/branch for a
 * single-pointer-identifier operand. */
static int ast_operand_is_ptr_ident(const struct AstNode *e)
{
    struct Sym *s;
    if (e == NULL || e->kind != AST_IDENT)
        return 0;
    s = find_sym(e->sval);
    return s != NULL && type_ptr_depth(s->type) > 0;
}

/* Is `n` a relational comparison `a OP b` of two qualifying identifier operands
 * that streaming lowers via the plain-16-bit direct-branch path (and which the
 * AST can reproduce exactly with ast_gen_cmp_branch)?  Equality and ordering
 * ops only; '&' is not a relational op. */
static int ast_is_simple_cmp_cond(const struct AstNode *n)
{
    if (n == NULL || n->kind != AST_BINARY || !is_cmp_op(n->op))
        return 0;
    return ast_cmp_operand_ok(n->a) && ast_cmp_operand_ok(n->b);
}

/* If `n` is a relational comparison that streaming lowers via the small-const-int
 * signed-local16 fast path (emit_cmp_const_branch_for_signed_local16 in dcc_cmp.c,
 * reached through gen_direct_small_const_int_rel_branch_until), fill sp/opp/cp
 * with the (sym, effective-op, const) to hand that emitter and return 1; else 0.
 * The emitter accepts ONLY an IX-direct SIGNED 16-bit local/param compared with a
 * 0..255 constant, for `var < const` (any 0..255) or `var >= 0`.  Streaming reaches
 * it in two shapes: the DIRECT form `var OP const`, and the FLIPPED const-on-left
 * form handled by its try_const_op_var branch which accepts only `const > var`
 * (=> var < const) and `const <= var` (=> var >= const).  Everything else - a
 * global var (streaming sends it to the general plain path), unsigned/char/long/
 * float/struct var, const out of range, or any other operator - is not recognised
 * and defers (always safe).  The earlier byte relational and byte-bitand fast
 * paths decline for a size-2 operand, so this is the path that fires. */
static int ast_const_cmp_extract(const struct AstNode *n, struct Sym **sp,
                                 int *opp, long *cp)
{
    const struct AstNode *idn;
    const struct AstNode *cn;
    struct Sym *s;
    long c;
    int op;

    if (n == NULL || n->kind != AST_BINARY)
        return 0;

    if (n->a != NULL && n->a->kind == AST_IDENT &&
        n->b != NULL && n->b->kind == AST_INT_LIT) {
        /* direct: var OP const */
        idn = n->a;
        cn = n->b;
        op = n->op;
    } else if (n->a != NULL && n->a->kind == AST_INT_LIT &&
               n->b != NULL && n->b->kind == AST_IDENT) {
        /* flipped: const OP var - streaming try_const_op_var accepts only
         * '>' (=> var < const) and TOK_LE (=> var >= const). */
        idn = n->b;
        cn = n->a;
        if (n->op == '>')
            op = '<';
        else if (n->op == TOK_LE)
            op = TOK_GE;
        else
            return 0;
    } else {
        return 0;
    }

    if (op != '<' && op != TOK_GE)
        return 0;

    s = find_sym(idn->sval);
    if (s == NULL)
        return 0;
    if (s->is_array || s->is_const_value || s->storage == SC_FUNC)
        return 0;
    if (type_is_struct_object(s->type) || type_is_long(s->type) ||
        type_is_float(s->type))
        return 0;
    if (type_size(s->type) != 2)
        return 0;
    if (s->type & TYPE_UNSIGNED)
        return 0;
    if (!sym_can_ix_direct(s))
        return 0;

    c = cn->ival;
    if (c < 0 || c > 255)
        return 0;
    if (op == TOK_GE && c != 0)           /* GE handled only for c == 0 */
        return 0;

    *sp = s;
    *opp = op;
    *cp = c;
    return 1;
}

static int ast_is_const_cmp_cond(const struct AstNode *n)
{
    struct Sym *s;
    int op;
    long c;
    return ast_const_cmp_extract(n, &s, &op, &c);
}

/* Translate a comparison operand expression into a streaming ByteOperand (the
 * same struct parse_byte_operand_fast builds), or return 0.  We reproduce only
 * the two register/immediate kinds: kind 1 (IX-direct UNSIGNED char local/param)
 * and kind 2 (0..255 constant).  Kind 3 (global byte array[index]) is DEFERRED:
 * in an `if`, gen_if's try_fast_global_char_array_condition probe emits a DEAD
 * index load (e.g. `ld hl,5`) before bailing when a relational operator follows
 * the `]` (it only handles the bare `if (arr[i])` form), and that already-emitted
 * dead code is NOT rolled back by the probe's lexer-snapshot restore.  The AST
 * has no such artifact, so reproducing `if (arr[idx] OP ...)` byte-for-byte is
 * impossible; deferring is always safe. */
static int ast_byte_operand(const struct AstNode *e, struct ByteOperand *op)
{
    struct Sym *s;

    memset(op, 0, sizeof(*op));
    if (e == NULL)
        return 0;
    if (e->kind == AST_IDENT) {
        s = find_sym(e->sval);
        if (s != NULL && sym_can_ix_direct(s) && type_size(s->type) == 1 &&
            (s->type & TYPE_UNSIGNED)) {
            op->kind = 1;
            op->sym = s;
            return 1;
        }
        return 0;
    }
    if (e->kind == AST_INT_LIT) {
        if (e->ival >= 0 && e->ival <= 255) {
            op->kind = 2;
            op->val = e->ival;
            return 1;
        }
        return 0;
    }
    return 0;
}

/* Is `n` a relational comparison of two byte operands that streaming lowers via
 * gen_direct_byte_rel_branch_until?  That path needs a real byte value in A for
 * the `cp`, so at least one operand must be a byte lvalue (kind 1/3); a compare
 * of two constants declines there (and falls through), so we defer it too.  The
 * earlier const-&&-byte and byte-bitand paths require a leading `const &&` or a
 * `bytevar & mask` shape respectively, neither of which is a bare relational
 * comparison, so for a two-byte-operand relation this byte path is what fires. */
static int ast_is_byte_cmp_cond(const struct AstNode *n)
{
    struct ByteOperand lhs;
    struct ByteOperand rhs;

    if (n == NULL || n->kind != AST_BINARY || !is_cmp_op(n->op))
        return 0;
    if (!ast_byte_operand(n->a, &lhs) || !ast_byte_operand(n->b, &rhs))
        return 0;
    /* streaming succeeds iff a byte lvalue can supply A (after the optional
     * const/lvalue swap): at least one operand must be kind 1/3. */
    return byte_operand_can_be_lhs(&lhs) || byte_operand_can_be_lhs(&rhs);
}

/* Is the controlling expression of an `if` / `while` one that streaming lowers
 * via its GENERIC condition path (gen_expr + emit_test_expr_nonzero), rather
 * than one of the specialised direct-branch fast paths?  gen_if/gen_while only
 * reach the generic path when their condition fast paths decline.  Those
 * decline for a condition that has no top-level relational/logical/conditional
 * operator, is not a constant, is not a global-char-array subscript, and is not
 * the `char_ixvar & byteconst` bitand shape (the latter is already excluded
 * because a binary with a literal operand is not ast_gen_supported).  We accept
 * only a conservative whitelist proven to reach the generic path; anything else
 * defers (always safe).  NOTE: gen_while additionally fast-paths `while (*ptr)`
 * deref loops, so the while gate excludes deref conditions on top of this. */
static int ast_cond_generic(const struct AstNode *n)
{
    if (n == NULL)
        return 0;
    if (ast_is_const_cmp_cond(n))
        return 1;
    if (ast_is_byte_cmp_cond(n))
        return 1;
    if (!ast_gen_supported(n) || !ast_value_is_plain_int(n))
        return 0;
    if (ast_node_is_const(n))
        return 0;

    switch (n->kind) {
    case AST_IDENT:
        /* if (x): no '[' follows (a subscript would be AST_INDEX), so the
         * global-char-array and byte fast paths all decline. */
        return 1;
    case AST_CALL:
        /* if (f(...)): a call result, never a fast-path shape. */
        return 1;
    case AST_MEMBER:
    case AST_POSTFIX:
        /* if (s.f) / if (p++): leading TOK_ID but not a global-char-array
         * subscript nor a char-ix '& mask'; parse_byte_operand_fast only
         * matches byte consts / unsigned-char ix vars / global byte arrays,
         * so the byte and relational fast paths all decline. */
        return 1;
    case AST_UNARY:
        /* if (!x) / if (*p) / if (-x) etc.: the leading token is the operator
         * (not TOK_ID/NUM), so every condition fast path declines.  '&'
         * address-of yields a pointer and is already filtered above by the
         * ast_value_is_plain_int check. */
        return 1;
    case AST_BINARY:
        /* A relational comparison of two plain-int-16 (or pointer) identifiers
         * reaches streaming's general gen_direct_rel_branch_until plain-16-bit
         * path - the byte and small-const-int relational fast paths decline for
         * two size-2 non-const operands - which ast_gen_cmp_branch reproduces
         * exactly.  Any other comparison (the `& mask` byte-bitand shape, or a
         * const / byte / compound operand) stays on streaming. */
        if (is_cmp_op(n->op))
            return ast_is_simple_cmp_cond(n);
        if (n->op == '&')
            return 0;
        return 1;
    case AST_LOGAND:
    case AST_LOGOR:
        /* if (a && b) / while (a || b): streaming's condition fast paths all
         * decline for a top-level &&/|| (simple_direct_condition_until requires
         * no logical operator), so gen_if/gen_while/gen_do_while fall to the
         * generic gen_expr + emit_test_expr_nonzero - which the AST reproduces
         * via ast_gen_cond_branch's generic fallback (ast_gen_expr emits the
         * same short-circuit 0/1 value, then the same nonzero test).  The guard
         * above already required ast_gen_supported && plain-int && non-const. */
        return 1;
    default:
        return 0;
    }
}

/* Recognise the `lhs = rhs1 +/- rhs2` simple-local self-add statement that
 * streaming routes through try_fast_local_self_add_statement (dcc_stmt_fast.c)
 * BEFORE gen_expr.  That fast path fires when lhs, rhs1 and rhs2 are all
 * ix-direct 2-byte locals (the const-rhs2 variant is already excluded because a
 * binary with a literal operand is not ast_gen_supported).  We must defer this
 * exact shape so the AST walker does not emit the generic assign sequence in
 * its place. */
static int ast_is_local_self_add_stmt(const struct AstNode *e)
{
    const struct AstNode *lhs;
    const struct AstNode *rhs;
    const struct AstNode *a;
    const struct AstNode *b;
    struct Sym *s;

    if (e->kind != AST_ASSIGN || e->op != '=')
        return 0;
    lhs = e->a;
    if (lhs == NULL || lhs->kind != AST_IDENT)
        return 0;
    s = find_sym(lhs->sval);
    if (s == NULL || !sym_can_ix_direct(s) || type_size(s->type) != 2)
        return 0;
    rhs = e->b;
    if (rhs == NULL || rhs->kind != AST_BINARY)
        return 0;
    if (rhs->op != '+' && rhs->op != '-')
        return 0;
    a = rhs->a;
    b = rhs->b;
    if (a == NULL || a->kind != AST_IDENT || b == NULL || b->kind != AST_IDENT)
        return 0;
    s = find_sym(a->sval);
    if (s == NULL || !sym_can_ix_direct(s) || type_size(s->type) != 2)
        return 0;
    s = find_sym(b->sval);
    if (s == NULL || !sym_can_ix_direct(s) || type_size(s->type) != 2)
        return 0;
    return 1;
}

static int ast_dead_expr_supported(const struct AstNode *e)
{
    int old_dead;
    int ok;
    if (e == NULL)
        return 0;
    if ((e->kind == AST_UNARY || e->kind == AST_POSTFIX) &&
        (e->op == TOK_INC || e->op == TOK_DEC))
        return 0;
    if (ast_is_local_self_add_stmt(e))
        return 0;
    /* Evaluate the support gate in the SAME dead-result context the walker will
     * emit under: gen_expr_statement sets expr_result_dead = 1 before gen_expr,
     * and ast_gen_supported's AST_ASSIGN case defers the dead-result `+=`/`-=`
     * fast paths only when expr_result_dead is set.  Without this, those shapes
     * (e.g. `x += 5;`) would wrongly pass the gate here and the walker would
     * emit a divergent (longer) sequence instead of streaming's optimised one. */
    old_dead = expr_result_dead;
    expr_result_dead = 1;
    ok = ast_gen_supported(e);
    expr_result_dead = old_dead;
    return ok;
}

static int ast_for_init_expr_supported(const struct AstNode *e)
{
    if (e == NULL)
        return 1;
    if (e->kind == AST_ASSIGN)
        return e->op == '=' && ast_gen_supported(e);
    return e->kind == AST_CALL && ast_gen_supported(e);
}

/* Is the expression-statement node `n` (n->a is the expression) AST-emittable?
 * An expression statement reaches gen_expr_statement (dcc_stmt_fast.c), which
 * runs four fast paths before gen_expr and otherwise does
 * `expr_result_dead = 1; gen_expr();`.  For the non-fast-path case gen_expr
 * already routes the expression through the same inner AST hook, so the bytes
 * are identical to ast_gen_expr by construction -- we only have to DEFER the
 * four fast-path shapes:
 *   - prefix/postfix ++/-- statement  (try_gen_incdec_statement: fires for ANY
 *     top-level inc/dec statement)
 *   - lhs = rhs1 +/- rhs2 local self-add  (try_fast_local_self_add_statement)
 *   - global char-array store g[i] = c  (try_fast_global_char_array_store):
 *     auto-excluded, the subscript-store gate requires a 2-byte element
 *   - crc = crc_update_byte(...)  (try_fast_crc_update_byte_statement):
 *     auto-excluded, that fast path needs a long lhs (not ast_gen_supported). */
static int ast_expr_stmt_supported(const struct AstNode *n)
{
    const struct AstNode *e = n->a;
    if (e == NULL)
        return 0;
    return ast_dead_expr_supported(e);
}

/* Is statement node `n` within the AST-emittable subset? */
static int ast_stmt_supported(const struct AstNode *n)
{
    switch (n->kind) {
    case AST_EMPTY:
        return 1;                         /* `;` emits nothing */
    case AST_EXPR_STMT:
        return ast_expr_stmt_supported(n);
    case AST_RETURN:
        return ast_return_stmt_supported(n);
    case AST_BREAK:
    case AST_CONTINUE:
        /* A bare jump to the innermost loop/switch exit/continue label.  Defer
         * when there is no enclosing flow so streaming emits its diagnostic. */
        return nflow > 0;
    case AST_GOTO:
        /* Unconditional jump to a named user label. */
        return n->sval != NULL;
    case AST_LABEL:
        /* A user label is emittable when its labeled statement is too. */
        return n->sval != NULL && n->b != NULL && ast_stmt_supported(n->b);
    case AST_IF:
        /* Generic-path condition + both branch statements emittable. */
        if (!ast_cond_generic(n->a))
            return 0;
        if (n->b == NULL || !ast_stmt_supported(n->b))
            return 0;
        if (n->c != NULL && !ast_stmt_supported(n->c))
            return 0;
        return 1;
    case AST_WHILE:
        /* Generic-path condition + emittable body.  Exclude a bare `*ptr`
         * deref condition: gen_while fast-paths those into pointer-walking
         * loops (try_gen_bc_pointer_* all start with parse_while_deref_nonzero
         * _id), which the AST hook would otherwise bypass. */
        if (!ast_cond_generic(n->a))
            return 0;
        if (n->a->kind == AST_UNARY && n->a->op == '*')
            return 0;
        if (n->b == NULL || !ast_stmt_supported(n->b))
            return 0;
        return 1;
    case AST_DOWHILE:
        /* Generic-path condition + emittable body.  gen_do_while has NO
         * loop-specific fast path (unlike gen_while); it goes straight to
         * gen_condition_branch_true, whose decline predicate ast_cond_generic
         * already captures.  A bare `*ptr` condition reaches the generic path
         * here (no deref fast path), so no extra exclusion is needed.  The
         * `do{}while(0)` idiom has a const condition -> ast_cond_generic
         * returns 0 -> deferred (streaming's const_zero special-case). */
        if (!ast_cond_generic(n->a))
            return 0;
        if (n->b == NULL || !ast_stmt_supported(n->b))
            return 0;
        return 1;
    case AST_FOR: {
        int old_nflow;
        int ok;
        /* Narrow slice: for ([init] ; [cond] ; [inc]) body.  The builder
         * stores init in a, cond in b, inc in c, and body in d.  Init support
         * excludes the transform-prone constant assignment shape below. */
        if (n->d == NULL)
            return 0;
        if (g_for_seq >= MAX_FOR_SCOPES || g_for_rename_count[g_for_seq] != 0)
            return 0;
        if (!ast_for_init_expr_supported(n->a))
            return 0;
        if (n->a != NULL && n->a->kind == AST_ASSIGN && n->a->op == '=' &&
            n->a->b != NULL && ast_node_is_const(n->a->b))
            return 0;
        if (n->b != NULL && !ast_cond_generic(n->b))
            return 0;
        if (n->c != NULL && !ast_dead_expr_supported(n->c))
            return 0;
        old_nflow = nflow;
        nflow++;
        ok = ast_stmt_supported(n->d);
        nflow = old_nflow;
        return ok;
    }
    case AST_SWITCH: {
        int i;
        int old_nflow;
        int ok;
        if (n->a == NULL || !ast_gen_supported(n->a) || !ast_value_is_plain_int(n->a))
            return 0;
        if (n->list_len <= 0)
            return 0;
        old_nflow = nflow;
        nflow++;
        ok = 1;
        for (i = 0; i < n->list_len; ++i) {
            const struct AstNode *sec = n->list[i];
            int j;
            if (sec == NULL || (sec->kind != AST_CASE && sec->kind != AST_DEFAULT)) {
                ok = 0;
                break;
            }
            for (j = 0; j < sec->list_len; ++j) {
                if (!ast_stmt_supported(sec->list[j])) {
                    ok = 0;
                    break;
                }
            }
            if (!ok)
                break;
        }
        nflow = old_nflow;
        return ok;
    }
    case AST_COMPOUND: {
        /* A brace block is emittable when every child statement is.  An empty
         * block is fine (just a balanced enter/leave scope).  Declarations are
         * already filtered at build time (ast_build_compound_stmt declines a
         * block containing a typedef or a declaration). */
        int i;
        for (i = 0; i < n->list_len; ++i)
            if (!ast_stmt_supported(n->list[i]))
                return 0;
        return 1;
    }
    default:
        return 0;
    }
}

/* Emit a relational comparison `a OP b` as a direct conditional branch to
 * `label`, reproducing streaming's gen_direct_rel_branch_until plain-16-bit
 * path byte-for-byte: load LHS into HL, push it, load RHS into HL, ex de,hl /
 * pop hl (HL=lhs, DE=rhs), then the signed or unsigned compare/branch for the
 * operator and branch sense.  The operand loads come from the same ast_gen_expr
 * emitter the streaming path reaches through gen_snippet_expr, so they match.
 * Caller guarantees ast_is_simple_cmp_cond(n). */
static void ast_gen_cmp_branch(const struct AstNode *n, int label,
                               int branch_when_true)
{
    int lhs_type;
    int rhs_type;
    int common_type;
    int ptr_cmp;
    int op = n->op;

    ptr_cmp = ast_operand_is_ptr_ident(n->a) || ast_operand_is_ptr_ident(n->b);
    ast_gen_expr(n->a);
    lhs_type = g_expr_type;
    emit("\tpush hl\n");
    ast_gen_expr(n->b);
    rhs_type = g_expr_type;
    common_type = common_arith_type(lhs_type, rhs_type);
    emit("\tex de,hl\n\tpop hl\n");
    if ((common_type & TYPE_UNSIGNED) || ptr_cmp) {
        if (branch_when_true)
            emit_cmp_branch_true_unsigned(op, label);
        else
            emit_cmp_branch_false_unsigned(op, label);
    } else {
        if (branch_when_true)
            emit_cmp_branch_true(op, label);
        else
            emit_cmp_branch_false(op, label);
    }
}

/* Emit `ident OP const` (gated by ast_is_const_cmp_cond) by calling the SAME
 * streaming emitter, guaranteeing byte-identical output.  The internal label it
 * allocates lands at the same relative point as in streaming because the
 * if/while/do-while walkers allocate their loop labels up front (matching
 * gen_if/gen_while/gen_do_while) before emitting the condition. */
static void ast_gen_const_cmp_branch(const struct AstNode *n, int label,
                                     int branch_when_true)
{
    struct Sym *s;
    int op;
    long c;
    ast_const_cmp_extract(n, &s, &op, &c);
    emit_cmp_const_branch_for_signed_local16(s, op, c, label, branch_when_true);
}

/* Emit a two-byte-operand relational comparison (gated by ast_is_byte_cmp_cond)
 * by building the same ByteOperands and replaying streaming's exact sequence in
 * gen_direct_byte_rel_branch_until: optional const/lvalue swap (inverting the
 * relop), load LHS to A, `cp` RHS, then the byte compare/branch.  All three
 * emit helpers are the streaming ones, so the bytes match. */
static void ast_gen_byte_cmp_branch(const struct AstNode *n, int label,
                                    int branch_when_true)
{
    struct ByteOperand lhs;
    struct ByteOperand rhs;
    struct ByteOperand tmp;
    int op = n->op;

    ast_byte_operand(n->a, &lhs);
    ast_byte_operand(n->b, &rhs);
    if (!byte_operand_can_be_lhs(&lhs) && byte_operand_can_be_lhs(&rhs)) {
        op = invert_relop_for_swap(op);
        tmp = lhs;
        lhs = rhs;
        rhs = tmp;
    }
    emit_byte_operand_to_a(&lhs);
    emit_cp_byte_operand(&rhs);
    emit_byte_cmp_branch_after_cp(op, label, branch_when_true);
}

/* Emit the controlling expression of an if/while/do-while as a branch to
 * `label` taken when the condition is true (branch_when_true=1) or false (0).
 * A simple relational comparison uses the direct compare/branch; everything
 * else falls back to the generic value-test (gen_expr + emit_test_expr_nonzero)
 * the streaming generic condition path also uses. */
static void ast_gen_cond_branch(const struct AstNode *n, int label,
                                int branch_when_true)
{
    if (ast_is_const_cmp_cond(n)) {
        ast_gen_const_cmp_branch(n, label, branch_when_true);
        return;
    }
    if (ast_is_byte_cmp_cond(n)) {
        ast_gen_byte_cmp_branch(n, label, branch_when_true);
        return;
    }
    if (ast_is_simple_cmp_cond(n)) {
        ast_gen_cmp_branch(n, label, branch_when_true);
        return;
    }
    ast_gen_expr(n);
    emit_test_expr_nonzero(g_expr_type, label, branch_when_true);
}

static void ast_gen_stmt(const struct AstNode *n);

static int ast_switch_find_case(int value, int *vals, int ncase)
{
    int i;
    for (i = 0; i < ncase; ++i)
        if (vals[i] == value)
            return i;
    return -1;
}

static int ast_switch_table_ok(int *case_vals, int ncase, int *minp, int *maxp)
{
    int i;
    int minv;
    int maxv;
    if (ncase < 3)
        return 0;
    minv = case_vals[0];
    maxv = case_vals[0];
    for (i = 1; i < ncase; ++i) {
        if (case_vals[i] < minv) minv = case_vals[i];
        if (case_vals[i] > maxv) maxv = case_vals[i];
    }
    if ((maxv - minv) > 255)
        return 0;
    if ((maxv - minv + 1) > ncase * 2)
        return 0;
    minp[0] = minv;
    maxp[0] = maxv;
    return 1;
}

static void ast_switch_collect(const struct AstNode *n, int *case_vals,
                               int *ncasep, int *have_defaultp)
{
    int i;
    int ncase = 0;
    int have_default = 0;
    for (i = 0; i < n->list_len; ++i) {
        const struct AstNode *sec = n->list[i];
        if (sec->kind == AST_CASE)
            case_vals[ncase++] = (int)sec->ival;
        else if (sec->kind == AST_DEFAULT)
            have_default = 1;
    }
    ncasep[0] = ncase;
    have_defaultp[0] = have_default;
}

static void ast_gen_switch_stmt(const struct AstNode *n)
{
    int case_vals[MAX_SWITCH_CASES];
    int case_labs[MAX_SWITCH_CASES];
    int ncase;
    int have_default;
    int default_lab;
    int minv;
    int maxv;
    int table_ok;
    int lend;
    int i;

    ast_switch_collect(n, case_vals, &ncase, &have_default);
    table_ok = ast_switch_table_ok(case_vals, ncase, &minv, &maxv);

    /* gen_switch() always performs an initial table scan before choosing the
     * real lowering.  That scan allocates labels for every top-level case and
     * default it accepts, then those labels are discarded.  Consume the same
     * labels here before any expression code can allocate labels.  The scan
     * allocates in source order, so `default:` before later cases matters. */
    for (i = 0; i < n->list_len; ++i)
        (void)new_label();

    default_lab = -1;
    if (!table_ok) {
        int ci = 0;
        for (i = 0; i < n->list_len; ++i) {
            const struct AstNode *sec = n->list[i];
            if (sec->kind == AST_CASE)
                case_labs[ci++] = new_label();
            else
                default_lab = new_label();
        }
    }

    ast_gen_expr(n->a);

    if (table_ok) {
        int ci = 0;
        for (i = 0; i < n->list_len; ++i) {
            const struct AstNode *sec = n->list[i];
            if (sec->kind == AST_CASE)
                case_labs[ci++] = new_label();
            else
                default_lab = new_label();
        }
    }

    lend = new_label();
    if (table_ok) {
        emit_switch_jump_table(minv, maxv, case_vals, case_labs, ncase,
                               default_lab, lend);
    } else {
        emit("\tex de,hl\n");
        for (i = 0; i < ncase; ++i) {
            fprintf(outf, "\tld hl,%ld\n", (long)(case_vals[i] & 0xffff));
            emit("\tor a\n\tsbc hl,de\n");
            emit_jp_label("jp z,", case_labs[i]);
        }
        emit_jp_label("jp", default_lab >= 0 ? default_lab : lend);
    }

    enter_scope();
    break_stack[nflow] = lend;
    cont_stack[nflow] = (nflow > 0) ? cont_stack[nflow - 1] : lend;
    nflow++;

    for (i = 0; i < n->list_len; ++i) {
        const struct AstNode *sec = n->list[i];
        int lab;
        int j;
        if (sec->kind == AST_CASE) {
            int ci = ast_switch_find_case((int)sec->ival, case_vals, ncase);
            lab = ci >= 0 ? case_labs[ci] : -1;
        } else {
            lab = default_lab;
        }
        if (lab >= 0)
            emit_label(lab);
        for (j = 0; j < sec->list_len; ++j)
            ast_gen_stmt(sec->list[j]);
    }

    nflow--;
    leave_scope();
    emit_label(lend);
}

static void ast_gen_for_stmt(const struct AstNode *n)
{
    int ltop;
    int linc;
    int lend;
    int for_seq;

    for_seq = g_for_seq++;
    if (for_seq >= MAX_FOR_SCOPES)
        fatal("too many for statements");
    if (g_for_rename_count[for_seq] != 0)
        fatal("unsupported AST for-init scope");

    ltop = new_label();
    linc = new_label();
    lend = new_label();

    if (n->a != NULL)
        ast_gen_expr(n->a);

    emit_label(ltop);
    if (n->b != NULL)
        ast_gen_cond_branch(n->b, lend, 0);

    break_stack[nflow] = lend;
    cont_stack[nflow] = linc;
    nflow++;
    ast_gen_stmt(n->d);
    nflow--;

    emit_label(linc);
    if (n->c != NULL) {
        int old_dead = expr_result_dead;
        expr_result_dead = 1;
        ast_gen_expr(n->c);
        expr_result_dead = old_dead;
    }
    emit_jp_label("jp", ltop);
    emit_label(lend);
}

/* Emit statement node `n` (gated by ast_stmt_supported). */
static void ast_gen_stmt(const struct AstNode *n)
{
    switch (n->kind) {
    case AST_EMPTY:
        break;                            /* empty statement: emit nothing */
    case AST_EXPR_STMT: {
        /* Mirror gen_expr_statement's non-fast-path tail exactly:
         * expr_result_dead = 1; gen_expr() -> (inner hook) ast_gen_expr.
         * We call ast_gen_expr directly, which is the SAME emitter the inner
         * hook would invoke, so the bytes are identical. */
        int old_dead = expr_result_dead;
        expr_result_dead = 1;
        ast_gen_expr(n->a);
        expr_result_dead = old_dead;
        break;
    }
    case AST_RETURN:
        gen_return_ast(n);
        break;
    case AST_BREAK:
        emit_jp_label("jp", break_stack[nflow - 1]);
        break;
    case AST_CONTINUE:
        emit_jp_label("jp", cont_stack[nflow - 1]);
        break;
    case AST_GOTO:
        emit_jp_label("jp", mark_user_label_reference(n->sval));
        break;
    case AST_LABEL:
        emit_label(define_user_label(n->sval));
        ast_gen_stmt(n->b);
        break;
    case AST_IF: {
        /* Mirror gen_if's generic-condition shape exactly, including the
         * label allocation order (lelse, lend BEFORE the condition). */
        int lelse = new_label();
        int lend = new_label();
        ast_gen_cond_branch(n->a, lelse, 0);
        ast_gen_stmt(n->b);
        emit_jp_label("jp", lend);
        emit_label(lelse);
        if (n->c != NULL)
            ast_gen_stmt(n->c);
        emit_label(lend);
        break;
    }
    case AST_WHILE: {
        /* Mirror gen_while's generic shape: ltop, lend allocated up front;
         * label(ltop); test condition -> lend; body inside nflow scope;
         * jp ltop; label(lend). */
        int ltop = new_label();
        int lend = new_label();
        emit_label(ltop);
        ast_gen_cond_branch(n->a, lend, 0);
        break_stack[nflow] = lend;
        cont_stack[nflow] = ltop;
        nflow++;
        ast_gen_stmt(n->b);
        nflow--;
        emit_jp_label("jp", ltop);
        emit_label(lend);
        break;
    }
    case AST_DOWHILE: {
        /* Mirror gen_do_while's non-const generic shape: ltop, lcont, lend
         * allocated up front; label(ltop); body inside nflow scope (break->
         * lend, continue->lcont); label(lcont); test condition -> ltop when
         * TRUE (branch sense 1); label(lend). */
        int ltop = new_label();
        int lcont = new_label();
        int lend = new_label();
        emit_label(ltop);
        break_stack[nflow] = lend;
        cont_stack[nflow] = lcont;
        nflow++;
        ast_gen_stmt(n->b);
        nflow--;
        emit_label(lcont);
        ast_gen_cond_branch(n->a, ltop, 1);
        emit_label(lend);
        break;
    }
    case AST_FOR:
        ast_gen_for_stmt(n);
        break;
    case AST_SWITCH:
        ast_gen_switch_stmt(n);
        break;
    case AST_COMPOUND: {
        /* Mirror gen_compound for a declaration-free block: enter_scope();
         * emit each child; leave_scope().  enter/leave emit nothing (pure
         * symbol-table bookkeeping), so the bytes equal the streamed block. */
        int i;
        enter_scope();
        for (i = 0; i < n->list_len; ++i)
            ast_gen_stmt(n->list[i]);
        leave_scope();
        break;
    }
    default:
        fatal("ast_gen_stmt: unsupported node");
    }
}

int ast_try_emit_statement(void)
{
    long sv_pos;
    long sv_tok_start;
    int sv_line;
    int sv_tok_line;
    struct Token sv_tok;
    struct AstNode *n;
    int report;

    if (!g_ast_gen_enabled || scan_mode)
        return 0;

    report = getenv("DCC_AST_REPORT") != NULL;

    sv_pos = posi;
    sv_tok_start = tok_start_pos;
    sv_line = line_no;
    sv_tok_line = tok_line;
    sv_tok = tok;

    n = ast_build_stmt(&g_ast_arena);

    if (n != NULL && ast_stmt_supported(n)) {
        ast_gen_stmt(n);
        if (g_ast_gen_enabled == 2)
            fprintf(stderr, "; AST-emit %s\n", ast_kind_name(n->kind));
        ast_arena_reset(&g_ast_arena);
        return 1;
    }

    if (report) {
        if (n == NULL) {
            fprintf(stderr, "; AST-fallback stmt build token=%d text='%s' line=%d\n",
                    sv_tok.kind, sv_tok.text, sv_tok_line);
        } else {
            fprintf(stderr, "; AST-fallback stmt gate kind=%s line=%d\n",
                    ast_kind_name(n->kind), sv_tok_line);
        }
    }

    posi = sv_pos;
    tok_start_pos = sv_tok_start;
    line_no = sv_line;
    tok_line = sv_tok_line;
    tok = sv_tok;
    ast_arena_reset(&g_ast_arena);
    return 0;
}
