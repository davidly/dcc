/*
 * dcc_ast_gen.c - AST-driven code generation.
 *
 * The function-local AST is now the normal codegen path.  This walker produces
 * Z80 assembly by calling the shared low-level emit helpers; unsupported AST
 * shapes are compiler errors rather than falling back to the old streaming
 * statement/expression generator.
 *
 * Expression and statement lowering is intentionally close to the historical
 * byte sequences so existing peephole patterns and regression baselines stay
 * stable while the AST becomes the source of truth.
 */
#include "dcc.h"
#include "dcc_ast.h"
#include <string.h>

int g_ast_gen_enabled = 1;

static int ident_supported(const char *name)
{
    int ei;
    /* stdin/stdout/stderr are emitted as immediates before any symbol lookup. */
    if (!strcmp(name, "stdin") || !strcmp(name, "stdout") ||
        !strcmp(name, "stderr"))
        return 1;
    if (find_sym(name) != NULL)
        return 1;
    /* An unresolved identifier may still be an enum constant. */
    for (ei = 0; ei < nenum_consts; ++ei)
        if (!strcmp(enum_const_names[ei], name))
            return 1;
    /* Let the AST emitter report the unresolved identifier. */
    return 1;
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

static int is_float_arith_op(int op)
{
    return op == '+' || op == '-' || op == '*' || op == '/';
}

/* Binary operators whose plain-int (16-bit) emission is the uniform
 * "push hl / <rhs> / ex de,hl / pop hl / gen_binop_typed" sequence.  Shifts
 * use a different (ld b,l + shift-loop) shape and &&/|| are short-circuit, so
 * both use their own AST lowering. */
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


/* An enum constant or a const-folded scalar: the AST path may fold a
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
static int ast_index_long_read(const struct AstNode *n);
static int ast_index_float_read(const struct AstNode *n);
static int ast_switch_gate_depth;

#define AST_MAX_SW_NEST 8
struct AstSwCtx { int *vals; int *labs; int n; int def_lab; };
static struct AstSwCtx ast_sw_ctx[AST_MAX_SW_NEST];
static int ast_sw_depth;

/* Forward declaration: resolve 2-D array/field-array element type. */
static int ast_index_2d_array_elem_type(const struct AstNode *n, int *out_type);
static int ast_index_symbol_nd_elem_type(const struct AstNode *n, int *out_type);
static int ast_index_deref_pointer_array_collect(const struct AstNode *n,
                                                 struct Sym **out_sym,
                                                 const struct AstNode **out_base,
                                                 const struct AstNode **idxs,
                                                 int *out_count,
                                                 int *out_type);
static int ast_index_member_array_nd_collect(const struct AstNode *n,
                                             const struct AstNode **out_member,
                                             const struct AstNode **idxs,
                                             int *out_count,
                                             int *out_type);
static int ast_index_pointer_expr_elem_type(const struct AstNode *n, int *out_type);
static int ast_index_reversed_pointer_expr_elem_type(const struct AstNode *n, int *out_type);
static int ast_index_member_pointer_elem_type(const struct AstNode *n, int *out_type);

/* Forward declaration: a subscript expression can be emitted by index-only code. */
static int ast_index_subscript_supported(const struct AstNode *idx);
static int ast_index_struct_object_subscript_supported(const struct AstNode *idx);

/* Forward declaration: a struct field read can be a plain-int value operand. */
static int ast_member_plain_int_read(const struct AstNode *n);
static int ast_member_bitfield_read(const struct AstNode *n);
static int ast_member_array_field_elem_type(const struct AstNode *n, int *out_type);

/* Forward declaration: resolve scalar field lvalue type. */
static int ast_member_lvalue_type(const struct AstNode *n, int *out_type);

/* Forward declaration: resolve the struct object/pointer type for a member base. */
static int ast_member_base_type(const struct AstNode *n, int *out_type);

/* Forward declaration: resolve element type for pointer-valued array fields. */
static int ast_member_pointer_array_field_elem_type(const struct AstNode *n,
                                                    int *out_type);

/* Forward declaration: a pointer deref read can be a plain-int value operand. */
static int ast_deref_plain_int_read(const struct AstNode *n);
static int ast_va_arg_deref_type(const struct AstNode *n, int *out_type);
static void gen_va_arg_deref_ast(const struct AstNode *n, int val_type);

/* Forward declaration: a pointer-valued expression supported only as the
 * operand of a dereference lvalue. */
static int ast_pointer_expr_type(const struct AstNode *n, int *out_type,
                                 int *out_no_deref);

/* Forward declaration: expression yields a 16-bit pointer word in HL. */
static int ast_value_is_pointer_word(const struct AstNode *n);
static int ast_value_is_long_word(const struct AstNode *arg);
static int ast_long_word_type(const struct AstNode *arg, int *out_type);
static int ast_call_star_indirect_supported(const struct AstNode *n);
static int ast_call_indirect_supported(const struct AstNode *n);

/* Forward declaration: supported pointer equality/inequality comparison. */
static int ast_pointer_cmp_supported(const struct AstNode *n);
static int ast_pointer_diff_supported(const struct AstNode *n);

/* Forward declaration: supported long comparison yielding int 0/1. */
static int ast_long_cmp_supported(const struct AstNode *n);
static int ast_long_arith_supported(const struct AstNode *n);
static int ast_mixed_long_rhs_arith_supported(const struct AstNode *n);
static int ast_const_plain_int_binary_supported(const struct AstNode *n);

/* Forward declaration: supported direct struct-return call assignment. */
static int ast_struct_return_call_assign_supported(int lhs_type,
                                                  const struct AstNode *rhs);
static void gen_struct_return_call_assign_ast(const struct AstNode *lhs,
                                              const struct AstNode *rhs);
static int ast_struct_deref_copy_assign_supported(const struct AstNode *n);
static void gen_struct_deref_copy_assign_ast(const struct AstNode *n);
static int ast_struct_member_copy_assign_supported(const struct AstNode *n);
static void gen_struct_member_copy_assign_ast(const struct AstNode *n);
static int ast_struct_addr_expr_supported(const struct AstNode *n, int *out_type);
static void gen_struct_addr_expr_ast(const struct AstNode *n, int *out_type);
static int ast_struct_copy_assign_supported(const struct AstNode *n);
static void gen_struct_copy_assign_ast(const struct AstNode *n);
static void gen_struct_return_call_arg_ast(const struct AstNode *call,
                                           int want_type);
static int ast_address_of_value_type(const struct AstNode *n, int *out_type);
static int ast_index_lvalue_elem_type(const struct AstNode *n, int *out_type);
static int ast_deadincdec_addr_lvalue_type(const struct AstNode *e, int *out_type);
static void gen_deadincdec_addr_lvalue_ast(const struct AstNode *e, int *out_type);

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

/* Forward declaration: gate for va_start/va_end builtin calls. */
static int ast_va_builtin_supported(const struct AstNode *n);

/* Forward declaration: emit a `*ident` target ADDRESS into HL for an lvalue
 * store (mirrors streaming's try_gen_deref_postinc_lvalue_addr, which differs
 * from the deref value-read path). */
static void gen_deref_addr_ast(const struct AstNode *n, int *out_val_type);

/* Forward declaration: emit a pointer-valued expression into HL for a
 * dereference lvalue address. */
static void gen_pointer_expr_ast(const struct AstNode *n, int *out_type,
                                 int *out_no_deref);

/* Forward declaration: emit pointer equality/inequality into HL as 0/1. */
static void gen_pointer_cmp_ast(const struct AstNode *n);
static void gen_pointer_diff_ast(const struct AstNode *n);

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

/* True for streaming's emit_mul_hl_const fast-path multipliers
 * (0,1,3,5,10,pow2).  Streaming applies these only for a non-long multiply
 * whose literal is the RHS, so the AST mirrors that exactly. */
static int ast_mul_const_value_ok(long v)
{
    long m = v & 0xffffL;
    return m == 0 || m == 1 || m == 3 || m == 5 || m == 10 ||
           int_log2_pow2((int)m) >= 0;
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
    case AST_SIZEOF_EXPR:
        return 1;
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
        if (ast_pointer_diff_supported(n))
            return 1;
        if (is_supported_binary_op(n->op) || is_shift_op(n->op))
            return ast_value_is_plain_int(n->a) &&
                   ast_value_is_plain_int(n->b);
        return 0;
    case AST_INDEX:
        return ast_index_plain_int_read(n);
    case AST_MEMBER:
        return ast_member_plain_int_read(n) || ast_member_bitfield_read(n);
    case AST_CALL: {
        int rt;
        int callee_type;
        int no_deref;
        if (ast_call_star_indirect_supported(n))
            return 1;
        if (ast_call_indirect_supported(n) &&
            ast_pointer_expr_type(n->a, &callee_type, &no_deref)) {
            rt = type_decay_ptr(callee_type);
            if (type_ptr_depth(rt) > 0 || type_is_struct_object(rt) ||
                type_is_long(rt) || type_is_float(rt))
                return 0;
            return ast_is_plain_int_type(rt);
        }
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
    case AST_COMMA:
        return ast_value_is_plain_int(n->b);
    case AST_ASSIGN:
        return ast_gen_supported(n) && ast_value_is_plain_int(n->a);
    case AST_POSTFIX:
        return ast_postfix_plain_int(n);
    case AST_CAST:
        return ast_is_plain_int_type(n->type) && ast_gen_supported(n);
    default:
        return 0;
    }
}

static int ast_unary_int_const_fold(const struct AstNode *n, long *out);
static int ast_int_const_cast_fold(const struct AstNode *n, long *out);
static int ast_unary_long_const_fold(const struct AstNode *n, long *out);
static long ast_const_apply_int_cast(long v, int type);
static int ast_const_condition_fold(const struct AstNode *n, long *out);
static int ast_global_byte_array_const_store(const struct AstNode *n,
                                             struct Sym **out_arr,
                                             long *out_idx,
                                             long *out_rhs);
static int ast_global_byte_array_fast_store(const struct AstNode *n,
                                            struct Sym **out_arr,
                                            struct Sym **out_idx_sym,
                                            long *out_idx_const,
                                            int *out_idx_has_const,
                                            struct Sym **out_rhs_sym,
                                            long *out_rhs_const,
                                            int *out_rhs_kind);

/* A unary chain bottoming out in a numeric literal is a compile-time constant.
 * The AST walker folds these so the compact immediate form is preserved. */
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
    if (ast_index_symbol_nd_elem_type(n, &elem))
        return ast_is_plain_int_type(elem) &&
               (type_size(elem) == 1 || type_size(elem) == 2);
    if (ast_index_deref_pointer_array_collect(n, &s, NULL, NULL, NULL, &elem))
        return ast_is_plain_int_type(elem) &&
               (type_size(elem) == 1 || type_size(elem) == 2);
    if (ast_index_2d_array_elem_type(n, &elem))
        return ast_is_plain_int_type(elem) &&
               (type_size(elem) == 1 || type_size(elem) == 2);
    if (ast_index_pointer_expr_elem_type(n, &elem))
        return ast_is_plain_int_type(elem) &&
               (type_size(elem) == 1 || type_size(elem) == 2);
    if (ast_index_reversed_pointer_expr_elem_type(n, &elem))
        return ast_is_plain_int_type(elem) &&
               (type_size(elem) == 1 || type_size(elem) == 2);
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
        if (ast_member_plain_array_field_elem_type(n->a, &elem)) {
            decayed = type_add_ptr(elem);
        } else {
            if (!ast_member_lvalue_type(n->a, &decayed))
                return 0;
            if (type_ptr_depth(decayed) <= 0)
                return 0;
        }
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

static int ast_index_long_read(const struct AstNode *n)
{
    struct Sym *s;
    int decayed;
    int elem;

    if (n == NULL || n->kind != AST_INDEX || n->a == NULL)
        return 0;
    if (ast_index_symbol_nd_elem_type(n, &elem))
        return type_is_long(elem);
    if (ast_index_deref_pointer_array_collect(n, &s, NULL, NULL, NULL, &elem))
        return type_is_long(elem);
    if (ast_index_2d_array_elem_type(n, &elem))
        return type_is_long(elem);
    if (ast_index_pointer_expr_elem_type(n, &elem))
        return type_is_long(elem);
    if (ast_index_reversed_pointer_expr_elem_type(n, &elem))
        return type_is_long(elem);
    if (ast_index_member_pointer_elem_type(n, &elem))
        return type_is_long(elem);
    if (n->a->kind == AST_IDENT) {
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
        if (type_ptr_depth(decayed) != 1)
            return 0;
        elem = type_decay_ptr(decayed);
    } else if (n->a->kind == AST_MEMBER) {
        if (!ast_member_array_field_elem_type(n->a, &elem))
            return 0;
    } else {
        return 0;
    }
    return type_is_long(elem) && ast_index_subscript_supported(n->b);
}

static int ast_index_float_read(const struct AstNode *n)
{
    struct Sym *s;
    int decayed;
    int elem;

    if (n == NULL || n->kind != AST_INDEX || n->a == NULL)
        return 0;
    if (ast_index_symbol_nd_elem_type(n, &elem))
        return type_is_float(elem);
    if (ast_index_deref_pointer_array_collect(n, &s, NULL, NULL, NULL, &elem))
        return type_is_float(elem);
    if (ast_index_2d_array_elem_type(n, &elem))
        return type_is_float(elem);
    if (ast_index_pointer_expr_elem_type(n, &elem))
        return type_is_float(elem);
    if (ast_index_reversed_pointer_expr_elem_type(n, &elem))
        return type_is_float(elem);
    if (n->a->kind == AST_IDENT) {
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
        if (type_ptr_depth(decayed) != 1)
            return 0;
        elem = type_decay_ptr(decayed);
    } else if (n->a->kind == AST_MEMBER) {
        if (!ast_member_array_field_elem_type(n->a, &elem))
            return 0;
    } else {
        return 0;
    }
    return type_is_float(elem) && ast_index_subscript_supported(n->b);
}

static int ast_index_struct_object_type(const struct AstNode *n, int *out_type);
static int ast_member_array_field_elem_type(const struct AstNode *n, int *out_type);

static int ast_index_array_row_ptr_type(const struct AstNode *n, int *out_type)
{
    const struct AstNode *cur;
    const struct AstNode *root;
    struct Sym *s;
    int count;

    if (n == NULL || n->kind != AST_INDEX)
        return 0;
    count = 0;
    root = n;
    while (root != NULL && root->kind == AST_INDEX) {
        ++count;
        root = root->a;
    }
    if (count != 1 || root == NULL || root->kind != AST_IDENT)
        return 0;
    s = find_sym(root->sval);
    if (s == NULL || s->is_const_value || s->storage == SC_FUNC || !s->is_array)
        return 0;
    if (s->dim_count != 2 || type_size(s->type) <= 0)
        return 0;
    for (cur = n; cur != root; cur = cur->a) {
        if (cur == NULL || cur->kind != AST_INDEX || !ast_index_subscript_supported(cur->b))
            return 0;
    }
    *out_type = type_add_ptr(s->type);
    return 1;
}

static int ast_index_struct_object_type(const struct AstNode *n, int *out_type)
{
    struct Sym *s;
    int elem_type;

    if (n == NULL || n->kind != AST_INDEX)
        return 0;
    if (n->a == NULL)
        return 0;
    if (ast_index_symbol_nd_elem_type(n, &elem_type)) {
        if (!type_is_struct_object(elem_type))
            return 0;
        if (out_type)
            *out_type = elem_type;
        return 1;
    }
    if (ast_index_2d_array_elem_type(n, &elem_type)) {
        if (!type_is_struct_object(elem_type))
            return 0;
        if (out_type)
            *out_type = elem_type;
        return 1;
    }
    if (ast_index_pointer_expr_elem_type(n, &elem_type)) {
        if (!type_is_struct_object(elem_type))
            return 0;
        if (out_type)
            *out_type = elem_type;
        return 1;
    }
    if (n->a->kind == AST_IDENT) {
        s = find_sym(n->a->sval);
        if (s == NULL || s->is_const_value || s->storage == SC_FUNC)
            return 0;
        if (s->is_array) {
            if (s->dim_count > 1 || !type_is_struct_object(s->type))
                return 0;
            elem_type = s->type;
        } else {
            /* pointer-to-struct: e.g. struct Ins *code; code[i].field */
            if (type_ptr_depth(s->type) != 1 ||
                !type_is_struct_object(type_decay_ptr(s->type)))
                return 0;
            elem_type = type_decay_ptr(s->type);
        }
    } else if (n->a->kind == AST_MEMBER) {
        if (ast_member_array_field_elem_type(n->a, &elem_type)) {
            if (!type_is_struct_object(elem_type))
                return 0;
        } else {
            if (!ast_member_lvalue_type(n->a, &elem_type))
                return 0;
            if (type_ptr_depth(elem_type) <= 0)
                return 0;
            elem_type = type_decay_ptr(elem_type);
            if (!type_is_struct_object(elem_type))
                return 0;
        }
    } else {
        return 0;
    }
    if (n->b == NULL)
        return 0;
    if (!ast_index_struct_object_subscript_supported(n->b))
        return 0;
    *out_type = elem_type;
    return 1;
}

static int ast_index_struct_object_addr(const struct AstNode *n)
{
    int ignored;
    return ast_index_struct_object_type(n, &ignored);
}

static int ast_index_struct_object_subscript_supported(const struct AstNode *idx)
{
    if (idx == NULL)
        return 0;
    if (ast_value_is_plain_int(idx))
        return 1;
    return ast_index_subscript_supported(idx);
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
    if (idx->kind == AST_SIZEOF_EXPR || idx->kind == AST_SIZEOF_TYPE)
        return ast_value_is_plain_int(idx);
    if (ast_index_subscript_binary_literal(idx))
        return 1;
    if (ast_const_plain_int_binary_supported(idx))
        return 1;
    if (ast_value_is_long_word(idx))
        return 1;
    if (ast_node_is_const(idx))
        return 0;
    /* A long-valued subscript (e.g. `src[pos]` with `long pos`) is truncated to
     * its low 16-bit word for the address computation: the emitter evaluates it
     * into DE:HL and the index machine uses HL only (scale_hl_by_elem_size acts
     * on HL, and the non-power-of-2 __mulu path overwrites the stale high word
     * in DE).  This matches the 16-bit address space exactly. */
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

static int ast_index_symbol_nd_collect(const struct AstNode *n, struct Sym **out_sym,
                                       const struct AstNode **idxs, int *out_count)
{
    const struct AstNode *cur;
    const struct AstNode *rev[8];
    struct Sym *s;
    int count;
    int i;

    cur = n;
    count = 0;
    while (cur != NULL && cur->kind == AST_INDEX) {
        if (count >= 8 || cur->b == NULL)
            return 0;
        rev[count++] = cur->b;
        cur = cur->a;
    }
    if (cur == NULL || cur->kind != AST_IDENT || count < 2)
        return 0;
    s = find_sym(cur->sval);
    if (s == NULL || s->is_const_value || s->storage == SC_FUNC)
        return 0;
    if (s->is_array) {
        if (s->dim_count != count)
            return 0;
    } else {
        if (type_ptr_depth(s->type) <= 0 || s->dim_count + 1 != count)
            return 0;
    }
    for (i = 0; i < count; ++i) {
        idxs[i] = rev[count - 1 - i];
        if (!ast_index_subscript_supported(idxs[i]))
            return 0;
    }
    *out_sym = s;
    *out_count = count;
    return 1;
}

static int ast_index_symbol_nd_elem_type(const struct AstNode *n, int *out_type)
{
    const struct AstNode *idxs[8];
    struct Sym *s;
    int count;

    if (!ast_index_symbol_nd_collect(n, &s, idxs, &count))
        return 0;
    *out_type = s->is_array ? s->type : type_decay_ptr(s->type);
    return type_size(*out_type) > 0;
}

static int ast_index_symbol_nd_addressable_addr(const struct AstNode *n)
{
    int elem;
    return ast_index_symbol_nd_elem_type(n, &elem);
}

static int ast_index_deref_pointer_array_collect(const struct AstNode *n,
                                                 struct Sym **out_sym,
                                                 const struct AstNode **out_base,
                                                 const struct AstNode **idxs,
                                                 int *out_count,
                                                 int *out_type)
{
    const struct AstNode *rev[8];
    const struct AstNode *root;
    const struct AstNode *base;
    struct Sym *s;
    int count;
    int elem;
    int i;

    if (n == NULL || n->kind != AST_INDEX)
        return 0;
    count = 0;
    root = n;
    while (root != NULL && root->kind == AST_INDEX) {
        if (count >= 8 || root->b == NULL)
            return 0;
        rev[count++] = root->b;
        root = root->a;
    }
    if (count < 1 || root == NULL || root->kind != AST_UNARY || root->op != '*' ||
        root->a == NULL || root->a->kind != AST_IDENT)
        return 0;
    base = root->a;
    s = find_sym(base->sval);
    if (s == NULL || s->is_const_value || s->storage == SC_FUNC || s->is_array)
        return 0;
    if (type_ptr_depth(s->type) <= 0 || s->dim_count != count)
        return 0;
    elem = type_decay_ptr(s->type);
    if ((elem & 15) == TYPE_VOID || type_size(elem) <= 0)
        return 0;
    for (i = 0; i < count; ++i) {
        if (idxs != NULL)
            idxs[i] = rev[count - 1 - i];
        if (!ast_index_subscript_supported(rev[count - 1 - i]))
            return 0;
    }
    if (out_sym != NULL)
        *out_sym = s;
    if (out_base != NULL)
        *out_base = base;
    if (out_count != NULL)
        *out_count = count;
    if (out_type != NULL)
        *out_type = elem;
    return 1;
}

static int ast_index_member_array_nd_collect(const struct AstNode *n,
                                             const struct AstNode **out_member,
                                             const struct AstNode **idxs,
                                             int *out_count,
                                             int *out_type)
{
    const struct AstNode *rev[4];
    const struct AstNode *root;
    struct FieldDef *fd;
    int cur_type;
    int sid;
    int count;
    int i;

    if (n == NULL || n->kind != AST_INDEX)
        return 0;
    count = 0;
    root = n;
    while (root != NULL && root->kind == AST_INDEX) {
        if (count >= 4)
            return 0;
        rev[count++] = root->b;
        root = root->a;
    }
    if (count <= 1 || root == NULL || root->kind != AST_MEMBER)
        return 0;
    if (!ast_member_base_type(root, &cur_type))
        return 0;
    if (root->op == TOK_ARROW) {
        if (type_ptr_depth(cur_type) != 1)
            return 0;
    } else if (!type_is_struct_object(cur_type)) {
        return 0;
    }
    sid = base_struct_id_from_type(cur_type);
    fd = find_field_def(sid, root->sval);
    if (fd == NULL || !fd->is_array || fd->bit_width > 0 || fd->dim_count != count)
        return 0;
    for (i = 0; i < count; ++i) {
        idxs[i] = rev[count - 1 - i];
        if (!ast_index_subscript_supported(idxs[i]))
            return 0;
    }
    *out_member = root;
    *out_count = count;
    *out_type = fd->elem_type;
    return type_size(*out_type) > 0;
}

static int ast_index_2d_array_elem_type(const struct AstNode *n, int *out_type)
{
    const struct AstNode *outer;
    const struct AstNode *member;
    const struct AstNode *idxs[4];
    struct Sym *s;
    struct FieldDef *fd;
    int cur_type;
    int count;
    int sid;

    if (ast_index_member_array_nd_collect(n, &member, idxs, &count, out_type))
        return 1;

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
    if (ast_index_symbol_nd_addressable_addr(n))
        return 1;
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

static int ast_index_pointer_expr_elem_type(const struct AstNode *n, int *out_type)
{
    int ptr_type;
    int no_deref;
    int elem;

    if (n == NULL || n->kind != AST_INDEX || n->a == NULL || n->b == NULL)
        return 0;
    if (n->a->kind == AST_IDENT || n->a->kind == AST_MEMBER)
        return 0;
    if (!ast_pointer_expr_type(n->a, &ptr_type, &no_deref) || no_deref)
        return 0;
    if (type_ptr_depth(ptr_type) <= 0)
        return 0;
    elem = type_decay_ptr(ptr_type);
    if ((elem & 15) == TYPE_VOID || type_size(elem) <= 0)
        return 0;
    if (!ast_index_subscript_supported(n->b))
        return 0;
    *out_type = elem;
    return 1;
}

static int ast_index_reversed_pointer_expr_elem_type(const struct AstNode *n, int *out_type)
{
    int ptr_type;
    int no_deref;
    int elem;

    if (n == NULL || n->kind != AST_INDEX || n->a == NULL || n->b == NULL)
        return 0;
    if (!ast_index_subscript_supported(n->a))
        return 0;
    if (!ast_pointer_expr_type(n->b, &ptr_type, &no_deref) || no_deref)
        return 0;
    if (type_ptr_depth(ptr_type) <= 0)
        return 0;
    elem = type_decay_ptr(ptr_type);
    if ((elem & 15) == TYPE_VOID || type_size(elem) <= 0)
        return 0;
    *out_type = elem;
    return 1;
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

static int ast_index_member_pointer_elem_type(const struct AstNode *n, int *out_type)
{
    int member_type;
    int elem_type;

    if (n == NULL || n->kind != AST_INDEX || n->a == NULL || n->b == NULL)
        return 0;
    if (n->a->kind != AST_MEMBER)
        return 0;
    if (!ast_member_lvalue_type(n->a, &member_type))
        return 0;
    if (type_ptr_depth(member_type) <= 0)
        return 0;
    elem_type = type_decay_ptr(member_type);
    if ((elem_type & 15) == TYPE_VOID || type_size(elem_type) <= 0)
        return 0;
    if (!ast_index_subscript_supported(n->b))
        return 0;
    *out_type = elem_type;
    return 1;
}

/* `p[i]` where p is a scalar pointer identifier (NOT an array) whose element is
 * itself a pointer (e.g. `char **argv` -> `char *`).  The element address is
 * p's pointer VALUE plus i*elem_size, which gen_index_addr_ast computes exactly
 * as it does for the plain-int char/int element case; emit_load_from_hl then
 * reads the 2-byte pointer element.  Plain-int (char/int) elements are already
 * covered by ast_index_plain_int_read, so this only opens the pointer-element
 * case (a pointer base of depth >= 2). */
static int ast_index_scalar_pointer_elem_type(const struct AstNode *n, int *out_type)
{
    struct Sym *s;
    int elem;

    if (n == NULL || n->kind != AST_INDEX || n->a == NULL || n->b == NULL)
        return 0;
    if (n->a->kind != AST_IDENT)
        return 0;
    s = find_sym(n->a->sval);
    if (s == NULL || s->is_const_value || s->storage == SC_FUNC || s->is_array)
        return 0;
    if (type_ptr_depth(s->type) < 2)        /* element must itself be a pointer */
        return 0;
    elem = type_decay_ptr(s->type);
    if (type_size(elem) != 2)
        return 0;
    if (!ast_index_subscript_supported(n->b))
        return 0;
    *out_type = elem;
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
    int elem_size;

    if (n == NULL)
        return 0;

    switch (n->kind) {
    case AST_STR_LIT:
        *out_type = TYPE_CHAR | TYPE_PTR;
        *out_no_deref = 0;
        return 1;

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

    case AST_POSTFIX:
        if (n->op != TOK_INC && n->op != TOK_DEC)
            return 0;
        if (n->a == NULL)
            return 0;
        if (n->a->kind == AST_MEMBER) {
            if (!ast_member_lvalue_type(n->a, &member_type))
                return 0;
            if (type_ptr_depth(member_type) <= 0 || type_size(member_type) != 2)
                return 0;
            *out_type = member_type;
            *out_no_deref = 0;
            return 1;
        }
        if (n->a->kind != AST_IDENT)
            return 0;
        s = find_sym(n->a->sval);
        if (s == NULL || s->is_const_value || s->storage == SC_FUNC || s->is_array)
            return 0;
        if (type_ptr_depth(s->type) <= 0 || type_size(s->type) != 2)
            return 0;
        *out_type = s->type;
        *out_no_deref = 0;
        return 1;

    case AST_UNARY:
        if (n->op == '&') {
            int elem;
            if (n->a == NULL)
                return 0;
            if (!ast_address_of_value_type(n->a, &elem))
                return 0;
            *out_type = type_add_ptr(elem);
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
        if (n->op != '+' && n->op != '-')
            return 0;
        if (!ast_pointer_expr_type(n->a, &ptr_type, &no_deref)) {
            if (n->op != '+' || !ast_pointer_expr_type(n->b, &ptr_type, &no_deref))
                return 0;
            if (no_deref)
                return 0;
            elem_size = type_index_elem_size(ptr_type);
            if (!ast_index_subscript_supported(n->a) &&
                !(elem_size == 1 && ast_value_is_long_word(n->a)))
                return 0;
            *out_type = ptr_type;
            *out_no_deref = 0;
            return 1;
        }
        if (n->op == '-' && ast_pointer_expr_type(n->b, &ptr_type, &no_deref))
            return 0;
        if (no_deref)
            return 0;
        elem_size = type_index_elem_size(ptr_type);
        if (!ast_index_subscript_supported(n->b) &&
            !(elem_size == 1 && ast_value_is_long_word(n->b)))
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
        if (ast_index_array_row_ptr_type(n, &member_type)) {
            *out_type = member_type;
            *out_no_deref = 1;
            return 1;
        }
        if (ast_index_scalar_pointer_elem_type(n, &member_type)) {
            *out_type = member_type;
            *out_no_deref = 0;
            return 1;
        }
        if (ast_index_pointer_array_elem_type(n, &member_type) ||
            ast_index_pointer_expr_elem_type(n, &member_type) ||
            (n->a != NULL && n->a->kind == AST_MEMBER &&
             (ast_member_pointer_array_field_elem_type(n->a, &member_type) ||
              (ast_member_lvalue_type(n->a, &member_type) &&
               type_ptr_depth(member_type) > 0 &&
               ast_index_subscript_supported(n->b) &&
               (member_type = type_decay_ptr(member_type),
                type_ptr_depth(member_type) > 0 && type_size(member_type) == 2))))) {
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

    case AST_ASSIGN:
        if (n->op != '=' || n->a == NULL || n->a->kind != AST_IDENT)
            return 0;
        s = find_sym(n->a->sval);
        if (s == NULL || s->is_const_value || s->storage == SC_FUNC ||
            s->is_array || type_ptr_depth(s->type) <= 0 || type_size(s->type) != 2)
            return 0;
        if (!ast_gen_supported(n))
            return 0;
        *out_type = s->type;
        *out_no_deref = 0;
        return 1;

    case AST_CAST:
        if (type_ptr_depth(n->type) <= 0 || type_size(n->type) != 2)
            return 0;
        if (ast_pointer_expr_type(n->a, &ptr_type, &no_deref)) {
            if (no_deref)
                return 0;
            *out_type = n->type;
            *out_no_deref = 0;
            return 1;
        }
        if (!ast_gen_supported(n->a))
            return 0;
        if (!ast_value_is_plain_int(n->a) &&
            !ast_value_is_pointer_word(n->a))
            return 0;
        *out_type = n->type;
        *out_no_deref = 0;
        return 1;

    case AST_COND: {
        int true_type;
        int false_type;
        int true_no_deref;
        int false_no_deref;
        if (n->a == NULL || !ast_gen_supported(n->a) ||
            (!ast_value_is_plain_int(n->a) && !ast_value_is_pointer_word(n->a)))
            return 0;
        if (!ast_pointer_expr_type(n->b, &true_type, &true_no_deref))
            return 0;
        if (!ast_pointer_expr_type(n->c, &false_type, &false_no_deref))
            return 0;
        *out_type = type_ptr_depth(true_type) > 0 ? true_type : false_type;
        *out_no_deref = true_no_deref && false_no_deref;
        return 1;
    }

    default:
        return 0;
    }
}

static int ast_deref_lvalue_plain_int_type(const struct AstNode *n, int *out_type)
{
    struct Sym *s;
    int ptr_type;
    int no_deref;
    int base;
    int sz;

    if (n == NULL || n->kind != AST_UNARY || n->op != '*')
        return 0;
    if (n->a != NULL && n->a->kind == AST_POSTFIX &&
        (n->a->op == TOK_INC || n->a->op == TOK_DEC) &&
        n->a->a != NULL && n->a->a->kind == AST_IDENT) {
        s = find_sym(n->a->a->sval);
        if (s == NULL || s->is_const_value || s->storage == SC_FUNC || s->is_array)
            return 0;
        if (type_ptr_depth(s->type) != 1)
            return 0;
        base = type_decay_ptr(s->type);
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

static int ast_deref_lvalue_type(const struct AstNode *n, int *out_type)
{
    struct Sym *s;
    int ptr_type;
    int no_deref;
    int base;

    if (n == NULL || n->kind != AST_UNARY || n->op != '*')
        return 0;
    if (n->a != NULL && n->a->kind == AST_POSTFIX &&
        (n->a->op == TOK_INC || n->a->op == TOK_DEC) &&
        n->a->a != NULL && n->a->a->kind == AST_IDENT) {
        s = find_sym(n->a->a->sval);
        if (s == NULL || s->is_const_value || s->storage == SC_FUNC || s->is_array)
            return 0;
        if (type_ptr_depth(s->type) != 1)
            return 0;
        base = type_decay_ptr(s->type);
        if ((base & 15) == TYPE_VOID)
            base = TYPE_CHAR;
        *out_type = base;
        return 1;
    }
    if (!ast_pointer_expr_type(n->a, &ptr_type, &no_deref))
        return 0;
    base = no_deref ? ptr_type : type_decay_ptr(ptr_type);
    if ((base & 15) == TYPE_VOID)
        base = TYPE_CHAR;
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
    if (n->op == '.' && n->a->kind == AST_MEMBER &&
        ast_member_lvalue_type(n->a, out_type))
        return type_is_struct_object(*out_type);
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

/* A plain-int bitfield struct field read `s.f` / `p->f`.  gen_member_ast loads
 * the storage unit and calls emit_extract_bitfield to mask/shift (and
 * sign-extend a signed field), yielding a plain 16-bit int value, so this is a
 * supported plain-int rvalue.  Kept separate from ast_member_plain_int_read so
 * the special non-extracting assign/subscript paths still decline bitfields. */
static int ast_member_bitfield_read(const struct AstNode *n)
{
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
    if (sid <= 0)
        return 0;
    fd = find_field_def(sid, n->sval);
    if (fd == NULL || fd->is_array || fd->bit_width <= 0)
        return 0;
    return ast_is_plain_int_type(fd->type);
}

static int ast_member_bitfield_lvalue_type(const struct AstNode *n, int *out_type)
{
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
    if (sid <= 0)
        return 0;
    fd = find_field_def(sid, n->sval);
    if (fd == NULL || fd->is_array || fd->bit_width <= 0)
        return 0;
    if (!ast_is_plain_int_type(fd->type))
        return 0;
    *out_type = fd->type;
    return 1;
}

static int ast_member_lvalue_type(const struct AstNode *n, int *out_type);

/* A long-typed struct field read `s.f` / `p->f` is a 32-bit value operand. */
static int ast_member_long_read(const struct AstNode *n)
{
    int t;
    if (n == NULL || n->kind != AST_MEMBER)
        return 0;
    if (!ast_member_lvalue_type(n, &t))
        return 0;
    return type_is_long(t);
}

static int ast_member_float_read(const struct AstNode *n)
{
    int t;
    if (n == NULL || n->kind != AST_MEMBER)
        return 0;
    if (!ast_member_lvalue_type(n, &t))
        return 0;
    return type_is_float(t);
}

/* A pointer-typed struct field read `s.f` / `p->f` (a 2-byte pointer value).
 * gen_member_ast loads it with emit_load_from_hl(field_type) exactly as for a
 * plain-int field, so the value lands in HL identically; only the gate needs to
 * recognise it as a pointer-word rvalue. */
static int ast_member_pointer_read(const struct AstNode *n)
{
    int t;
    if (n == NULL || n->kind != AST_MEMBER)
        return 0;
    if (!ast_member_lvalue_type(n, &t))
        return 0;
    return type_ptr_depth(t) > 0 && type_size(t) == 2;
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
    if (ast_va_arg_deref_type(n, &base))
        return ast_is_plain_int_type(base) &&
               (type_size(base) == 1 || type_size(base) == 2);
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

static int ast_va_arg_deref_type(const struct AstNode *n, int *out_type)
{
    const struct AstNode *cast;
    const struct AstNode *call;
    int val_type;

    if (n == NULL || n->kind != AST_UNARY || n->op != '*' || n->a == NULL)
        return 0;
    cast = n->a;
    if (cast->kind != AST_CAST || cast->a == NULL)
        return 0;
    call = cast->a;
    if (call->kind != AST_CALL || call->a == NULL || call->a->kind != AST_IDENT ||
        strcmp(call->a->sval, "__va_arg") || call->list_len != 2 ||
        call->list[0] == NULL || call->list[0]->kind != AST_IDENT ||
        call->list[1] == NULL ||
        (call->list[1]->kind != AST_SIZEOF_TYPE &&
         call->list[1]->kind != AST_SIZEOF_EXPR) ||
        find_sym(call->list[0]->sval) == NULL)
        return 0;
    if (type_ptr_depth(cast->type) > 0 && type_size(cast->type) == 2)
        val_type = cast->type & ~(TYPE_PTR | TYPE_PTR2);
    else if (type_size(cast->type) == 4)
        val_type = cast->type;
    else if (call->list[1]->ival > 2)
        val_type = TYPE_LONG;
    else
        val_type = call->list[1]->ival > 2 ? TYPE_LONG : TYPE_INT;
    if (out_type != NULL)
        *out_type = val_type;
    return 1;
}

static void gen_va_arg_deref_ast(const struct AstNode *n, int val_type)
{
    const struct AstNode *call = n->a->a;
    struct Sym *ap = find_sym(call->list[0]->sval);
    int sz = type_size(val_type);

    if (sz < 2)
        sz = 2;
    emit_load_sym_addr(ap);          /* HL = &ap */
    emit("\tpush hl\n");
    emit_load_from_hl(ap->type);     /* HL = old ap */
    emit("\tpush hl\n");          /* save old ap as result */
    emit_add_const_to_hl(sz);        /* HL = new ap */
    emit("\tex de,hl\n");
    emit("\tpop bc\n");           /* BC = old ap */
    emit("\tpop hl\n");           /* HL = &ap */
    emit_store_de_to_addr_hl(ap->type);
    emit("\tld h,b\n\tld l,c\n"); /* HL = old ap */
    emit_load_from_hl(val_type);
    g_expr_type = val_type;
    g_long_from16 = 0;
}

static int ast_long_va_arg_self_assign_supported(const struct AstNode *n,
                                                 const struct AstNode **out_va)
{
    struct Sym *s;
    const struct AstNode *rhs;
    const struct AstNode *lhs_term;
    const struct AstNode *va_term;
    int ignored;

    if (n == NULL || n->kind != AST_ASSIGN || n->op != '=' ||
        n->a == NULL || n->a->kind != AST_IDENT || n->b == NULL ||
        n->b->kind != AST_BINARY || n->b->op != '+')
        return 0;
    s = find_sym(n->a->sval);
    if (s == NULL || !sym_can_ix_direct(s) || !type_is_long(s->type))
        return 0;
    rhs = n->b;
    lhs_term = rhs->a;
    va_term = rhs->b;
    if (lhs_term == NULL || lhs_term->kind != AST_IDENT ||
        strcmp(lhs_term->sval, n->a->sval)) {
        lhs_term = rhs->b;
        va_term = rhs->a;
    }
    if (lhs_term == NULL || lhs_term->kind != AST_IDENT ||
        strcmp(lhs_term->sval, n->a->sval))
        return 0;
    if (!ast_va_arg_deref_type(va_term, &ignored))
        return 0;
    if (out_va != NULL)
        *out_va = va_term;
    return 1;
}

static void gen_long_va_arg_self_assign_ast(const struct AstNode *n)
{
    const struct AstNode *va_term;
    struct Sym *s = find_sym(n->a->sval);
    int saved_dead;

    ast_long_va_arg_self_assign_supported(n, &va_term);
    emit_load_sym_value_direct(s);
    emit("\tpush de\n\tpush hl\n");
    saved_dead = expr_result_dead;
    expr_result_dead = 0;
    gen_va_arg_deref_ast(va_term, s->type);
    expr_result_dead = saved_dead;
    gen_binop32_typed('+', s->type);
    emit_store_hl_to_sym_direct(s);
    g_expr_type = s->type;
    g_long_from16 = 0;
}

static int ast_deref_pointer_word_read(const struct AstNode *n)
{
    int ptr_type;
    int no_deref;
    int base;

    if (n == NULL || n->kind != AST_UNARY || n->op != '*' || n->a == NULL)
        return 0;
    if (ast_va_arg_deref_type(n, &base))
        return type_is_long(base);
    if (!ast_pointer_expr_type(n->a, &ptr_type, &no_deref))
        return 0;
    base = no_deref ? ptr_type : type_decay_ptr(ptr_type);
    if (type_ptr_depth(base) <= 0 || type_size(base) != 2)
        return 0;
    return 1;
}

static int ast_deref_long_read(const struct AstNode *n)
{
    int ptr_type;
    int no_deref;
    int base;

    if (n == NULL || n->kind != AST_UNARY || n->op != '*' || n->a == NULL)
        return 0;
    if (ast_va_arg_deref_type(n, &base))
        return type_is_float(base);
    if (!ast_pointer_expr_type(n->a, &ptr_type, &no_deref))
        return 0;
    base = no_deref ? ptr_type : type_decay_ptr(ptr_type);
    return type_is_long(base);
}

static int ast_deref_float_read(const struct AstNode *n)
{
    int ptr_type;
    int no_deref;
    int base;

    if (n == NULL || n->kind != AST_UNARY || n->op != '*' || n->a == NULL)
        return 0;
    if (!ast_pointer_expr_type(n->a, &ptr_type, &no_deref))
        return 0;
    base = no_deref ? ptr_type : type_decay_ptr(ptr_type);
    return type_is_float(base);
}

/* A prefix `++lv` / `--lv` on a plain-int identifier or struct member.
 * Streaming gen_unary emits gen_lvalue_addr(&t) + emit_pre_incdec_lvalue(t, op)
 * unconditionally for prefix ++/--.  Restrict to a plain int/char scalar (size
 * 1 or 2) so emit_pre_incdec_lvalue takes its integer branch. */
static int ast_preincdec_plain_int(const struct AstNode *n)
{
    struct Sym *s;
    int val_type;
    int sz;

    if (n == NULL || n->kind != AST_UNARY)
        return 0;
    if (n->op != TOK_INC && n->op != TOK_DEC)
        return 0;
    if (n->a == NULL)
        return 0;
    if (n->a->kind == AST_INDEX) {
        if (!ast_index_lvalue_elem_type(n->a, &val_type))
            return 0;
        if (!ast_is_plain_int_type(val_type))
            return 0;
        sz = type_size(val_type);
        return sz == 1 || sz == 2;
    }
    if (n->a->kind == AST_MEMBER) {
        if (!ast_member_lvalue_type(n->a, &val_type))
            return 0;
        if (!ast_is_plain_int_type(val_type))
            return 0;
        sz = type_size(val_type);
        return sz == 1 || sz == 2;
    }
    if (n->a->kind != AST_IDENT)
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

/* A postfix `lv++` / `lv--` on a bare plain-int identifier, struct member, or
 * dereferenced pointer expression.
 * Bare identifiers use streaming's try_emit_post_update_sym_direct fast path
 * deterministically (it returns 1 for any non-array, non-long, non-float scalar
 * of size <= 2 that is IX-direct or a global word).  Member lvalues go through
 * gen_lvalue_addr + gen_post_update_from_addr; dereferenced pointer lvalues use
 * the same address+update tail.  Restrict to plain int/char (size 1 or 2, not
 * a pointer) so the non-pointer inc/dec branch applies. */
static int ast_postfix_plain_int(const struct AstNode *n)
{
    struct Sym *s;
    int val_type;
    int sz;

    if (n == NULL || n->kind != AST_POSTFIX)
        return 0;
    if (n->op != TOK_INC && n->op != TOK_DEC)
        return 0;
    if (n->a == NULL)
        return 0;
    if (n->a->kind == AST_INDEX) {
        if (!ast_index_lvalue_elem_type(n->a, &val_type))
            return 0;
        if (!ast_is_plain_int_type(val_type))
            return 0;
        sz = type_size(val_type);
        return sz == 1 || sz == 2;
    }
    if (n->a->kind == AST_MEMBER) {
        if (!ast_member_lvalue_type(n->a, &val_type))
            return 0;
        if (!ast_is_plain_int_type(val_type))
            return 0;
        sz = type_size(val_type);
        return sz == 1 || sz == 2;
    }
    if (n->a->kind == AST_UNARY && n->a->op == '*') {
        if (!ast_deref_lvalue_plain_int_type(n->a, &val_type))
            return 0;
        sz = type_size(val_type);
        return sz == 1 || sz == 2;
    }
    if (n->a->kind != AST_IDENT)
        return 0;
    s = find_sym(n->a->sval);
    if (s == NULL || s->is_const_value || s->storage == SC_FUNC || s->is_array)
        return 0;
    if (!ast_is_plain_int_type(s->type))
        return 0;
    sz = type_size(s->type);
    if (sz != 1 && sz != 2)
        return 0;
    if (!sym_can_ix_direct(s) && !is_global_word_sym(s) && s->storage != SC_LOCAL)
        return 0;
    return 1;
}

static int ast_address_of_supported(const struct AstNode *n)
{
    int elem;
    return ast_address_of_value_type(n, &elem);
}

static int ast_address_of_value_type(const struct AstNode *n, int *out_type)
{
    struct Sym *s;
    int elem;

    if (n == NULL)
        return 0;
    if (n->kind == AST_IDENT) {
        s = find_sym(n->sval);
        if (s == NULL || s->is_const_value || s->storage == SC_FUNC)
            return 0;
        if (out_type)
            *out_type = s->type;
        return ident_supported(n->sval);
    }
    if (n->kind == AST_INDEX) {
        if (!ast_index_lvalue_elem_type(n, &elem))
            return 0;
        if (out_type)
            *out_type = elem;
        return 1;
    }
    if (n->kind == AST_MEMBER) {
        if (ast_member_array_field_elem_type(n, &elem)) {
            if (out_type)
                *out_type = elem;
            return 1;
        }
        if (!ast_member_lvalue_type(n, &elem))
            return 0;
        if (out_type)
            *out_type = elem;
        return 1;
    }
    return 0;
}

static int ast_call_arg_word_supported(const struct AstNode *arg);
static int ast_call_arg_supported(struct Sym *fn_sym, int arg_index,
                                  const struct AstNode *arg);
static int ast_value_is_long_word(const struct AstNode *arg);
static int ast_value_is_float_word(const struct AstNode *arg);
static int ast_pointer_assign_rhs_supported(const struct AstNode *n);
static int ast_call_named_args_supported(const struct AstNode *n);

static int ast_numeric_value_supported(const struct AstNode *n)
{
    return ast_value_is_plain_int(n) || ast_value_is_long_word(n) ||
           ast_value_is_float_word(n);
}

static int ast_cond_numeric_supported(const struct AstNode *n)
{
    if (n == NULL || n->kind != AST_COND)
        return 0;
    if (!ast_gen_supported(n->a) ||
        (!ast_value_is_plain_int(n->a) && !ast_value_is_float_word(n->a) &&
         !ast_value_is_pointer_word(n->a)))
        return 0;
    if (!ast_gen_supported(n->b) || !ast_numeric_value_supported(n->b))
        return 0;
    if (!ast_gen_supported(n->c) || !ast_numeric_value_supported(n->c))
        return 0;
    return 1;
}

static int ast_cond_result_is_float(const struct AstNode *n)
{
    return ast_cond_numeric_supported(n) &&
           (ast_value_is_float_word(n->b) || ast_value_is_float_word(n->c));
}

static int ast_cond_result_is_long(const struct AstNode *n)
{
    return ast_cond_numeric_supported(n) && !ast_cond_result_is_float(n) &&
           (ast_value_is_long_word(n->b) || ast_value_is_long_word(n->c));
}

static int ast_void_expr_supported(const struct AstNode *n)
{
    struct Sym *s;

    if (n == NULL)
        return 0;
    if (n->kind == AST_CAST && (n->type & 15) == TYPE_VOID)
        return n->a != NULL && ast_gen_supported(n->a);
    if (n->kind == AST_CALL && n->a != NULL && n->a->kind == AST_IDENT) {
        s = find_global(n->a->sval);
        return s != NULL && (s->type & 15) == TYPE_VOID && ast_gen_supported(n);
    }
    return 0;
}

static int ast_cond_void_supported(const struct AstNode *n)
{
    if (n == NULL || n->kind != AST_COND)
        return 0;
    if (!ast_gen_supported(n->a) ||
        (!ast_value_is_plain_int(n->a) && !ast_value_is_float_word(n->a) &&
         !ast_value_is_pointer_word(n->a)))
        return 0;
    return ast_void_expr_supported(n->b) && ast_void_expr_supported(n->c);
}

static int ast_index_cmp_cond_supported(const struct AstNode *n)
{
    int lhs_index;
    int rhs_index;

    if (n == NULL || n->kind != AST_BINARY || !is_cmp_op(n->op))
        return 0;
    lhs_index = ast_index_plain_int_read(n->a);
    rhs_index = ast_index_plain_int_read(n->b);
    if (!lhs_index && !rhs_index)
        return 0;
    if (lhs_index && rhs_index)
        return 1;
    if (lhs_index)
        return ast_gen_supported(n->b) && ast_value_is_plain_int(n->b);
    return ast_gen_supported(n->a) && ast_value_is_plain_int(n->a);
}

/* True when a value may be a `&&` / `||` operand: streaming's gen_land/gen_lor
 * only needs the operand evaluated and tested for nonzero, so any supported
 * scalar value works - plain ints, long words, comparisons (already plain int),
 * pointer-word values, floats, and nested logicals.  Comparisons are accepted even
 * when they carry a literal operand (the test-and-branch shape matches without
 * the binary value gate's literal restriction). */
static int ast_logical_operand_ok(const struct AstNode *n)
{
    if (n == NULL)
        return 0;
    if (n->kind == AST_LOGAND || n->kind == AST_LOGOR)
        return ast_logical_operand_ok(n->a) && ast_logical_operand_ok(n->b);
    if (n->kind == AST_BINARY && is_cmp_op(n->op))
        return (ast_gen_supported(n) || ast_index_cmp_cond_supported(n)) &&
               ast_value_is_plain_int(n);
    if (ast_gen_supported(n) &&
        (ast_value_is_plain_int(n) || ast_value_is_pointer_word(n) ||
         ast_value_is_float_word(n) || ast_value_is_long_word(n)))
        return 1;
    return 0;
}

static int ast_null_pointer_const(const struct AstNode *n)
{
    long v;
    if (n == NULL)
        return 0;
    if (n->kind == AST_INT_LIT)
        return n->ival == 0;
    if (ast_unary_int_const_fold(n, &v))
        return v == 0;
    return 0;
}

static int ast_pointer_cmp_operand_ok(const struct AstNode *n)
{
    int ptr_type;
    int no_deref;
    if (ast_pointer_expr_type(n, &ptr_type, &no_deref))
        return 1;
    return ast_null_pointer_const(n);
}

static int ast_pointer_cmp_supported(const struct AstNode *n)
{
    int lhs_type;
    int rhs_type;
    int lhs_no_deref;
    int rhs_no_deref;
    int lhs_ptr;
    int rhs_ptr;

    if (n == NULL || n->kind != AST_BINARY)
        return 0;
    if (!is_cmp_op(n->op))
        return 0;
    lhs_ptr = ast_pointer_expr_type(n->a, &lhs_type, &lhs_no_deref);
    rhs_ptr = ast_pointer_expr_type(n->b, &rhs_type, &rhs_no_deref);
    if (!lhs_ptr && !rhs_ptr)
        return 0;
    return ast_pointer_cmp_operand_ok(n->a) && ast_pointer_cmp_operand_ok(n->b);
}

static int ast_pointer_diff_supported(const struct AstNode *n)
{
    int lhs_type;
    int rhs_type;
    int lhs_no_deref;
    int rhs_no_deref;

    if (n == NULL || n->kind != AST_BINARY || n->op != '-')
        return 0;
    if (!ast_pointer_expr_type(n->a, &lhs_type, &lhs_no_deref) ||
        !ast_pointer_expr_type(n->b, &rhs_type, &rhs_no_deref))
        return 0;
    if (lhs_no_deref || rhs_no_deref)
        return 0;
    return type_index_elem_size(lhs_type) >= 1 &&
           type_index_elem_size(lhs_type) == type_index_elem_size(rhs_type);
}

static int ast_long_cmp_supported(const struct AstNode *n)
{
    int lhs_long;
    int rhs_long;

    if (n == NULL || n->kind != AST_BINARY || !is_cmp_op(n->op))
        return 0;
    lhs_long = ast_value_is_long_word(n->a);
    rhs_long = ast_value_is_long_word(n->b);
    if (!lhs_long && !rhs_long)
        return 0;
    return (lhs_long || ast_value_is_plain_int(n->a)) &&
           (rhs_long || ast_value_is_plain_int(n->b));
}

static int ast_long_arith_supported(const struct AstNode *n)
{
    int lhs_long;

    if (n == NULL || n->kind != AST_BINARY)
        return 0;
    if (n->op != '+' && n->op != '-' && n->op != '*' &&
        n->op != '/' && n->op != '%' &&
        n->op != '&' && n->op != '|' && n->op != '^')
        return 0;
    lhs_long = ast_value_is_long_word(n->a);
    return lhs_long && (ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b));
}

static int ast_mixed_long_rhs_arith_supported(const struct AstNode *n)
{
    if (n == NULL || n->kind != AST_BINARY)
        return 0;
    if (!is_supported_binary_op(n->op) || is_cmp_op(n->op))
        return 0;
    return ast_value_is_plain_int(n->a) && ast_value_is_long_word(n->b);
}

static int ast_const_plain_int_binary_supported(const struct AstNode *n)
{
    long rhs;

    if (n == NULL || n->kind != AST_BINARY)
        return 0;
    if (!is_supported_binary_op(n->op) && !is_shift_op(n->op))
        return 0;
    if ((n->op == '/' || n->op == '%') &&
        ast_unary_int_const_fold(n->b, &rhs) && rhs == 0)
        return 0;
    return ast_node_is_const(n) && ast_gen_supported(n->a) &&
           ast_gen_supported(n->b) && ast_value_is_plain_int(n->a) &&
           ast_value_is_plain_int(n->b);
}

static int ast_struct_return_call_assign_supported(int lhs_type,
                                                  const struct AstNode *rhs)
{
    struct Sym *fn;

    if (!type_is_struct_object(lhs_type))
        return 0;
    if (rhs == NULL || rhs->kind != AST_CALL || rhs->a == NULL ||
        rhs->a->kind != AST_IDENT)
        return 0;
    fn = find_global(rhs->a->sval);
    if (fn == NULL || fn->storage != SC_FUNC)
        return 0;
    if (!same_struct_type(lhs_type, fn->type))
        return 0;
    return ast_call_named_args_supported(rhs);
}

static int ast_struct_deref_copy_assign_supported(const struct AstNode *n)
{
    int lhs_type;
    int rhs_type;

    if (n == NULL || n->kind != AST_ASSIGN || n->op != '=' || !expr_result_dead)
        return 0;
    if (n->a == NULL || n->a->kind != AST_UNARY || n->a->op != '*')
        return 0;
    if (n->b == NULL || n->b->kind != AST_UNARY || n->b->op != '*')
        return 0;
    if (!ast_deref_lvalue_type(n->a, &lhs_type) ||
        !ast_deref_lvalue_type(n->b, &rhs_type))
        return 0;
    return type_is_struct_object(lhs_type) && same_struct_type(lhs_type, rhs_type);
}

static int ast_struct_member_copy_assign_supported(const struct AstNode *n)
{
    int lhs_type;
    struct Sym *rhs;

    if (n == NULL || n->kind != AST_ASSIGN || n->op != '=' || !expr_result_dead)
        return 0;
    if (n->a == NULL || n->a->kind != AST_MEMBER)
        return 0;
    if (n->b == NULL || n->b->kind != AST_IDENT)
        return 0;
    if (!ast_member_lvalue_type(n->a, &lhs_type) || !type_is_struct_object(lhs_type))
        return 0;
    rhs = find_sym(n->b->sval);
    return rhs != NULL && !rhs->is_const_value && rhs->storage != SC_FUNC &&
           !rhs->is_array && type_is_struct_object(rhs->type) &&
           same_struct_type(lhs_type, rhs->type);
}

static int ast_struct_addr_expr_supported(const struct AstNode *n, int *out_type)
{
    struct Sym *s;
    int t;

    if (n == NULL)
        return 0;
    switch (n->kind) {
    case AST_IDENT:
        s = find_sym(n->sval);
        if (s == NULL || s->is_const_value || s->storage == SC_FUNC || s->is_array ||
            !type_is_struct_object(s->type))
            return 0;
        if (out_type)
            *out_type = s->type;
        return 1;
    case AST_INDEX:
        if (!ast_index_struct_object_type(n, &t))
            return 0;
        if (out_type)
            *out_type = t;
        return 1;
    case AST_UNARY:
        if (n->op != '*' || !ast_deref_lvalue_type(n, &t) ||
            !type_is_struct_object(t))
            return 0;
        if (out_type)
            *out_type = t;
        return 1;
    case AST_MEMBER:
        if (!ast_member_lvalue_type(n, &t) || !type_is_struct_object(t))
            return 0;
        if (out_type)
            *out_type = t;
        return 1;
    default:
        return 0;
    }
}

static int ast_struct_copy_assign_supported(const struct AstNode *n)
{
    int lhs_type;
    int rhs_type;

    if (n == NULL || n->kind != AST_ASSIGN || n->op != '=' || !expr_result_dead)
        return 0;
    if (!ast_struct_addr_expr_supported(n->a, &lhs_type) ||
        !ast_struct_addr_expr_supported(n->b, &rhs_type))
        return 0;
    return same_struct_type(lhs_type, rhs_type);
}

static int ast_is_const_zero_condition(const struct AstNode *n)
{
    long v;
    if (n == NULL)
        return 0;
    if (ast_const_condition_fold(n, &v))
        return v == 0;
    if (n->kind == AST_INT_LIT)
        return n->ival == 0;
    if (ast_unary_int_const_fold(n, &v))
        return v == 0;
    return 0;
}

static int ast_is_const_nonzero_condition(const struct AstNode *n)
{
    long v;
    if (n == NULL)
        return 0;
    if (ast_const_condition_fold(n, &v))
        return v != 0;
    if (n->kind == AST_INT_LIT)
        return n->ival != 0;
    if (ast_unary_int_const_fold(n, &v))
        return v != 0;
    return 0;
}

int ast_gen_supported(const struct AstNode *n)
{
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
        return ident_supported(n->sval);
    case AST_UNARY:
        if (n->op == '&')
            return ast_address_of_supported(n->a);
        if (n->op == '-' || n->op == '+' || n->op == '~' || n->op == '!') {
            /* A unary chain over a single int literal is folded to the same
             * immediate streaming emits; allow it.  Other constant operands
             * (binary const, float) still defer to keep their folded form. */
            long fv;
            if (ast_unary_long_const_fold(n, &fv))
                return n->op != '!';
            if (ast_unary_int_const_fold(n, &fv))
                return n->op != '!';
            if ((n->op == '-' || n->op == '+') && n->a != NULL &&
                n->a->kind == AST_FLOAT_LIT)
                return 1;
            /* `-PI` / `+PI` where PI is a const-qualified float identifier:
             * streaming folds it to an immediate, but the AST path loads the
             * value (exactly as a bare `PI` operand, which is already
             * supported) and negates it with gen_unary_ast's float sign-bit
             * flip - equally correct.  Allow it so float expressions like
             * `-PI - x` and `x < -PI` can be emitted. */
            if ((n->op == '-' || n->op == '+') && n->a != NULL &&
                n->a->kind == AST_IDENT) {
                struct Sym *fs = find_sym(n->a->sval);
                if (fs != NULL && fs->storage != SC_FUNC && !fs->is_array &&
                    type_is_float(fs->type))
                    return 1;
            }
            if (n->op == '~' && ast_const_plain_int_binary_supported(n->a))
                return 1;
            if (n->op == '!' && n->a != NULL && n->a->kind == AST_BINARY &&
                is_cmp_op(n->a->op) && ast_gen_supported(n->a) &&
                (ast_value_is_float_word(n->a->a) || ast_value_is_float_word(n->a->b)))
                return 1;
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
            return ast_deref_plain_int_read(n) || ast_deref_pointer_word_read(n) ||
                   ast_deref_long_read(n) || ast_deref_float_read(n);
        if (n->op == TOK_INC || n->op == TOK_DEC)
            return ast_preincdec_plain_int(n);
        return 0;
    case AST_BINARY:
        if (!is_supported_binary_op(n->op) && !is_shift_op(n->op))
            return 0;
        {
            int ptr_type;
            int no_deref;
            if ((n->op == '+' || n->op == '-') &&
                ast_pointer_expr_type(n, &ptr_type, &no_deref))
                return 1;
        }
        if (ast_const_plain_int_binary_supported(n))
            return 1;
        if (ast_long_cmp_supported(n))
            return 1;
        if (ast_node_is_const(n) && ast_long_arith_supported(n))
            return 1;
        if (ast_node_is_const(n) && ast_mixed_long_rhs_arith_supported(n)) {
            long rhs;
            if ((n->op == '/' || n->op == '%') &&
                ast_unary_long_const_fold(n->b, &rhs) && rhs == 0)
                return 0;
            return 1;
        }
        if (ast_node_is_const(n) && is_shift_op(n->op) &&
            ast_value_is_long_word(n->a))
            return ast_value_is_plain_int(n->b);
        if (ast_node_is_const(n) &&
            (is_cmp_op(n->op) || is_float_arith_op(n->op)) &&
            (ast_value_is_float_word(n->a) || ast_value_is_float_word(n->b)))
            return (ast_value_is_float_word(n->a) || ast_value_is_plain_int(n->a) ||
                    ast_value_is_long_word(n->a)) &&
                   (ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b) ||
                    ast_value_is_long_word(n->b));
        /* Most fully constant expressions should fold to a single immediate;
         * defer unless a narrow plain-int binary slice above has opted in. */
        if (ast_node_is_const(n))
            return 0;
        /* A float literal operand triggers streaming's float const fast paths;
         * defer.  An integer literal triggers streaming const specialisations
         * for *, /, %, shifts and comparisons (e.g. x*10 becomes a shift/add
         * chain, x<0 a sign-bit test); allow a literal only for +, -, &, |, ^,
         * whose AST emit matches streaming byte-for-byte. */
        if (ast_pointer_cmp_supported(n))
            return 1;
        if (ast_pointer_diff_supported(n))
            return 1;
        if (ast_long_arith_supported(n))
            return 1;
        if (ast_mixed_long_rhs_arith_supported(n))
            return 1;
        if (!ast_gen_supported(n->a) || !ast_gen_supported(n->b))
            return 0;
        if (is_cmp_op(n->op) &&
            (ast_value_is_float_word(n->a) || ast_value_is_float_word(n->b)))
            return (ast_value_is_float_word(n->a) || ast_value_is_plain_int(n->a) ||
                    ast_value_is_long_word(n->a)) &&
                   (ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b) ||
                    ast_value_is_long_word(n->b));
        if (is_float_arith_op(n->op) &&
            (ast_value_is_float_word(n->a) || ast_value_is_float_word(n->b)))
            return (ast_value_is_float_word(n->a) || ast_value_is_plain_int(n->a) ||
                    ast_value_is_long_word(n->a)) &&
                   (ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b) ||
                    ast_value_is_long_word(n->b));
        if (n->a->kind == AST_FLOAT_LIT || n->b->kind == AST_FLOAT_LIT)
            return 0;
        if (is_shift_op(n->op) && ast_value_is_long_word(n->a))
            return ast_value_is_plain_int(n->b);
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
                           n->op == TOK_OREQ  || n->op == TOK_XOREQ ||
                           n->op == TOK_SHLEQ || n->op == TOK_SHREQ);
        if (n->op != '=' && !is_compound)
            return 0;                     /* shift-assign etc. defer */
        if (ast_global_byte_array_const_store(n, NULL, NULL, NULL))
            return 1;
        if (ast_global_byte_array_fast_store(n, NULL, NULL, NULL, NULL,
                                             NULL, NULL, NULL))
            return 1;
        if (n->op == '=' && n->a->kind == AST_IDENT) {
            s = find_sym(n->a->sval);
            if (s != NULL && !s->is_array && !s->is_const_value &&
                type_is_struct_object(s->type) &&
                ast_struct_return_call_assign_supported(s->type, n->b))
                return 1;
        }
        if (ast_struct_deref_copy_assign_supported(n))
            return 1;
        if (ast_struct_member_copy_assign_supported(n))
            return 1;
        if (ast_struct_copy_assign_supported(n))
            return 1;
        if (ast_long_va_arg_self_assign_supported(n, NULL))
            return 1;
        {
            int lhs_type;
            if (n->op == '=' && ast_struct_addr_expr_supported(n->a, &lhs_type) &&
                ast_struct_return_call_assign_supported(lhs_type, n->b))
                return 1;
        }
        {
            int rhs_ptr_type;
            int rhs_no_deref;
            if (!ast_gen_supported(n->b) && n->b->kind != AST_CAST &&
                !ast_pointer_expr_type(n->b, &rhs_ptr_type, &rhs_no_deref) &&
                !(n->b->kind == AST_CALL && ast_value_is_pointer_word(n->b) &&
                  ast_call_named_args_supported(n->b)) &&
                !ast_value_is_long_word(n->b))
                return 0;
        }
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
            if (ast_index_symbol_nd_elem_type(n->a, &elem)) {
                if (type_is_long(elem))
                    return (n->op == '=' || (is_compound && expr_result_dead)) &&
                           (ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b));
                if (type_is_float(elem))
                    return (n->op == '=' || n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                            n->op == TOK_MULEQ || n->op == TOK_DIVEQ) &&
                           (ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b));
                if (n->op != '=' && !(is_compound && expr_result_dead))
                    return 0;
                if (type_ptr_depth(elem) > 0)
                    return n->op == '=' && type_size(elem) == 2 &&
                           ast_pointer_assign_rhs_supported(n->b);
                if (!ast_is_plain_int_type(elem))
                    return 0;
                return (type_size(elem) == 1 || type_size(elem) == 2) &&
                       ast_value_is_plain_int(n->b);
            }
            if (ast_index_pointer_expr_elem_type(n->a, &elem)) {
                if (type_is_long(elem))
                    return n->op == '=' &&
                           (ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b));
                if (type_is_float(elem))
                    return (n->op == '=' || n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                            n->op == TOK_MULEQ || n->op == TOK_DIVEQ) &&
                           (ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b));
                if (n->op != '=')
                    return 0;
                if (type_ptr_depth(elem) > 0)
                    return type_size(elem) == 2 && ast_pointer_assign_rhs_supported(n->b);
                if (!ast_is_plain_int_type(elem))
                    return 0;
                return (type_size(elem) == 1 || type_size(elem) == 2) &&
                       ast_value_is_plain_int(n->b);
            }
            if (n->op == '=' && ast_index_addressable_addr(n->a)) {
                if (ast_index_2d_array_elem_type(n->a, &elem)) {
                    /* elem set by helper */
                } else if (n->a->a != NULL && n->a->a->kind == AST_IDENT) {
                    base = find_sym(n->a->a->sval);
                    if (base == NULL)
                        return 0;
                    decayed = base->is_array ? type_add_ptr(base->type) : base->type;
                    elem = type_decay_ptr(decayed);
                } else {
                    elem = 0;
                }
                if (type_is_long(elem))
                    return ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b);
                if (type_is_float(elem))
                    return ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b);
                if (type_ptr_depth(elem) > 0)
                    return type_size(elem) == 2 && ast_pointer_assign_rhs_supported(n->b);
            }
            if (is_compound && ast_index_addressable_addr(n->a)) {
                if (ast_index_2d_array_elem_type(n->a, &elem)) {
                    /* elem set by helper */
                } else if (n->a->a != NULL && n->a->a->kind == AST_IDENT) {
                    base = find_sym(n->a->a->sval);
                    if (base == NULL)
                        return 0;
                    decayed = base->is_array ? type_add_ptr(base->type) : base->type;
                    elem = type_decay_ptr(decayed);
                } else {
                    elem = 0;
                }
                if (type_is_long(elem))
                    return expr_result_dead &&
                           (ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b));
                if (type_is_float(elem))
                    return (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                            n->op == TOK_MULEQ || n->op == TOK_DIVEQ) &&
                           expr_result_dead &&
                           (ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b));
            }
            if (ast_index_2d_array_elem_type(n->a, &elem)) {
                if (type_is_long(elem))
                    return n->op == '=' &&
                           (ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b));
                if (type_is_float(elem))
                    return (n->op == '=' || n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                            n->op == TOK_MULEQ || n->op == TOK_DIVEQ) &&
                           (ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b));
                if (n->op != '=')
                    return 0;
                if (type_ptr_depth(elem) > 0)
                    return type_size(elem) == 2 && ast_pointer_assign_rhs_supported(n->b);
                if (!ast_is_plain_int_type(elem))
                    return 0;
                return (type_size(elem) == 1 || type_size(elem) == 2) &&
                       ast_value_is_plain_int(n->b);
            }
            if (ast_index_pointer_array_elem_type(n->a, &elem)) {
                if (n->op != '=')
                    return 0;
                return ast_pointer_assign_rhs_supported(n->b);
            }
            if (ast_index_member_pointer_elem_type(n->a, &elem)) {
                if (type_is_long(elem))
                    return n->op == '=' &&
                           (ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b));
                if (type_is_float(elem))
                    return n->op == '=' &&
                           (ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b));
                if (type_ptr_depth(elem) > 0)
                    return n->op == '=' && type_size(elem) == 2 &&
                           ast_pointer_assign_rhs_supported(n->b);
                if (!ast_is_plain_int_type(elem))
                    return 0;
                return (type_size(elem) == 2 || (n->op == '=' && type_size(elem) == 1)) &&
                       ast_value_is_plain_int(n->b);
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
            if (n->a->a->kind == AST_MEMBER &&
                ast_member_array_field_elem_type(n->a->a, &elem)) {
                if (n->op != '=')
                    return 0;
                if (!ast_index_subscript_supported(n->a->b))
                    return 0;
                if (type_is_float(elem))
                    return ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b);
                if (type_is_long(elem))
                    return ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b);
                if (type_ptr_depth(elem) > 0)
                    return type_size(elem) == 2 && ast_pointer_assign_rhs_supported(n->b);
                if (!ast_is_plain_int_type(elem))
                    return 0;
                return type_size(elem) == 1 || type_size(elem) == 2;
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
                    return expr_result_dead && n->op == '=';
            } else if (n->a->a->kind == AST_MEMBER) {
                if (ast_member_plain_array_field_elem_type(n->a->a, &elem)) {
                    if (!ast_value_is_plain_int(n->b))
                        return 0;
                    if (type_size(elem) != 2 && (n->op != '=' || type_size(elem) != 1))
                        return 0;
                } else {
                    int field_type;
                    if (!ast_member_lvalue_type(n->a->a, &field_type))
                        return 0;
                    if (type_ptr_depth(field_type) <= 0)
                        return 0;
                    elem = type_decay_ptr(field_type);
                    if (!ast_is_plain_int_type(elem))
                        return 0;
                    if (type_size(elem) != 2 && (n->op != '=' || type_size(elem) != 1))
                        return 0;
                    if (!ast_value_is_plain_int(n->b))
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
            if (ast_member_bitfield_lvalue_type(n->a, &field_type))
                return n->op == '=' && ast_value_is_plain_int(n->b);
            if (!ast_member_lvalue_type(n->a, &field_type))
                return 0;
            if (type_is_long(field_type)) {
                if (n->op == '=')
                    return ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b);
                if (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                    n->op == TOK_MULEQ || n->op == TOK_DIVEQ || n->op == TOK_MODEQ ||
                    n->op == TOK_ANDEQ || n->op == TOK_OREQ  || n->op == TOK_XOREQ)
                    return ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b);
                return 0;
            }
            if (type_is_float(field_type))
                return n->op == '=' &&
                       (ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b));
            if (type_ptr_depth(field_type) > 0) {
                if (n->op == '=')
                    return type_size(field_type) == 2 &&
                           ast_pointer_assign_rhs_supported(n->b);
                /* char* (size-1 element) compound += / -= needs no scaling, so
                 * the unscaled compound tail reproduces streaming exactly. */
                if ((n->op == TOK_ADDEQ || n->op == TOK_SUBEQ) &&
                    type_index_elem_size(field_type) == 1)
                    return ast_value_is_plain_int(n->b);
                return 0;
            }
            if (!ast_value_is_plain_int(n->b))
                return 0;
            if (type_size(field_type) == 1)
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
            if (!ast_deref_lvalue_type(n->a, &deref_type))
                return 0;
            if (ast_is_plain_int_type(deref_type) &&
                (type_size(deref_type) == 1 || type_size(deref_type) == 2))
                return ast_value_is_plain_int(n->b);
            if (n->op != '=') {
                if (type_is_long(deref_type) &&
                    (n->op == TOK_SHLEQ || n->op == TOK_SHREQ))
                    return ast_value_is_plain_int(n->b) || ast_value_is_long_word(n->b);
                if (type_is_long(deref_type) && expr_result_dead &&
                    (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                     n->op == TOK_MULEQ || n->op == TOK_DIVEQ ||
                     n->op == TOK_MODEQ || n->op == TOK_ANDEQ ||
                     n->op == TOK_OREQ  || n->op == TOK_XOREQ))
                    return ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b);
                return type_is_float(deref_type) &&
                       (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                        n->op == TOK_MULEQ || n->op == TOK_DIVEQ) &&
                       (ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b));
            }
            if (type_is_long(deref_type))
                return ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b);
            if (type_is_float(deref_type))
                return ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b);
            if (type_ptr_depth(deref_type) > 0)
                return type_size(deref_type) == 2 && ast_pointer_assign_rhs_supported(n->b);
            return 0;
        }
        if (n->a->kind != AST_IDENT)
            return 0;
        s = find_sym(n->a->sval);
        if (s == NULL)
            return 0;
        if (s->is_array || s->is_const_value)
            return 0;
        if (type_is_struct_object(s->type))
            return n->op == '=' &&
                   ast_struct_return_call_assign_supported(s->type, n->b);
        if (type_is_float(s->type)) {
            if (n->op == '=')
                return (sym_can_ix_direct(s) || expr_result_dead) &&
                       (ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b) ||
                        ast_value_is_long_word(n->b));
            if (!sym_can_ix_direct(s))
                return expr_result_dead &&
                       (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                        n->op == TOK_MULEQ || n->op == TOK_DIVEQ) &&
                       (ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b) ||
                        ast_value_is_long_word(n->b));
            if (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                n->op == TOK_MULEQ || n->op == TOK_DIVEQ)
                return ast_value_is_float_word(n->b) || ast_value_is_plain_int(n->b) ||
                       ast_value_is_long_word(n->b);
            return 0;
        }
        if (type_is_long(s->type)) {
            if (n->op == '=')
                return (sym_can_ix_direct(s) || expr_result_dead) &&
                       (ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b) ||
                        ast_value_is_float_word(n->b));
            if ((n->op == TOK_SHLEQ || n->op == TOK_SHREQ) &&
                !sym_can_ix_direct(s) && expr_result_dead)
                return ast_value_is_plain_int(n->b) || ast_value_is_long_word(n->b);
            if ((n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                 n->op == TOK_MULEQ || n->op == TOK_DIVEQ || n->op == TOK_MODEQ ||
                 n->op == TOK_ANDEQ || n->op == TOK_OREQ  || n->op == TOK_XOREQ) &&
                !sym_can_ix_direct(s) && expr_result_dead)
                return ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b);
            if (!sym_can_ix_direct(s))
                return 0;
            if (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                n->op == TOK_MULEQ || n->op == TOK_DIVEQ || n->op == TOK_MODEQ ||
                n->op == TOK_ANDEQ || n->op == TOK_OREQ  || n->op == TOK_XOREQ)
                return ast_value_is_long_word(n->b) || ast_value_is_plain_int(n->b);
            if (n->op == TOK_SHLEQ || n->op == TOK_SHREQ)
                return ast_value_is_plain_int(n->b) || ast_value_is_long_word(n->b);
            return 0;
        }
        if (!sym_can_ix_direct(s) && !is_global_word_sym(s) &&
            !(n->op == '=' && type_size(s->type) == 1 &&
                            (s->storage == SC_GLOBAL || s->storage == SC_EXTERN)) &&
                        !(expr_result_dead && type_size(s->type) == 1 &&
                            (n->op == TOK_ANDEQ || n->op == TOK_OREQ ||
                             n->op == TOK_XOREQ))) {
            if (n->op != '=') {
                if (expr_result_dead &&
                    (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
                     n->op == TOK_ANDEQ || n->op == TOK_OREQ ||
                     n->op == TOK_XOREQ) &&
                    ast_is_plain_int_type(s->type) &&
                    (type_size(s->type) == 1 || type_size(s->type) == 2))
                    return ast_value_is_plain_int(n->b);
                return 0;
            }
            if (type_ptr_depth(s->type) > 0)
                return expr_result_dead && type_size(s->type) == 2 &&
                       ast_pointer_assign_rhs_supported(n->b);
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
                return sym_can_ix_direct(rs) || is_global_word_sym(rs);
            }
            if (ast_const_plain_int_binary_supported(n->b))
                return 1;
            if (n->b->kind == AST_MEMBER && ast_member_plain_int_read(n->b))
                return 1;
        }
        if (type_ptr_depth(s->type) > 0) {
            if (n->op != '=' || type_size(s->type) != 2)
                return 0;
            return ast_pointer_assign_rhs_supported(n->b);
        }
        if (!ast_is_plain_int_type(s->type))
            return 0;
        if (type_size(s->type) == 2 && n->op == '=' &&
            ast_value_is_float_word(n->b))
            return 1;
        if (type_size(s->type) == 2 && n->op == '=' &&
            ast_value_is_long_word(n->b))
            return 1;
        if (type_size(s->type) == 2 &&
            (n->op == TOK_MULEQ || n->op == TOK_DIVEQ || n->op == TOK_MODEQ) &&
            ast_long_word_type(n->b, NULL))
            return 1;
        if (type_size(s->type) == 2 && n->op == '=' &&
            n->b->kind == AST_CAST && n->b->a != NULL) {
            long fv;
            if (ast_int_const_cast_fold(n->b, &fv))
                return 1;
        }
        if (type_size(s->type) == 1 && n->op == '=' &&
            ast_value_is_float_word(n->b))
            return 1;
        if (type_size(s->type) == 1 && n->op == '=') {
            long fv;
            if (ast_int_const_cast_fold(n->b, &fv))
                return 1;
        }
        if (type_size(s->type) == 1 && n->op == '=' &&
            n->b->kind == AST_CALL && ast_value_is_long_word(n->b))
            return 1;
        if (!ast_value_is_plain_int(n->b))
            return 0;
        if (type_size(s->type) == 1) {
            long fv;
            if (n->op != '=') {
                if (expr_result_dead &&
                    (s->storage == SC_GLOBAL || s->storage == SC_EXTERN) &&
                    (n->op == TOK_ANDEQ || n->op == TOK_OREQ ||
                     n->op == TOK_XOREQ))
                    return ast_value_is_plain_int(n->b);
                return sym_can_ix_direct(s);
            }
            if (s->storage == SC_GLOBAL || s->storage == SC_EXTERN)
                return 1;
            if (!sym_can_ix_direct(s))
                return 0;
            /* Slice 3: store a supported plain-int rvalue (comparison,
             * arithmetic, ...) into a size-1 (char/bool) ix-direct local.  The
             * general size-1 `=` emit tail evaluates the rhs into HL and
             * emit_store_hl_to_sym_direct stores L only, truncating correctly.
             * Calls use a dedicated emitter below that mirrors streaming's
             * truncating store-from-call tail exactly (no byte->int promote).
             * A long-returning call truncates to the low byte the same way
             * (handled by the early long-call return above). */
            if (ast_gen_supported(n->b) && ast_value_is_plain_int(n->b))
                return 1;
            /* Constant casts outside the general support gate can still fold
             * to an immediate store. */
            if (n->b->kind == AST_INT_LIT && n->b->ival >= 0 &&
                n->b->ival <= 255)
                return 1;
            if (ast_unary_int_const_fold(n->b, &fv))
                return 1;
            if (n->b->kind == AST_CAST && n->b->a != NULL &&
                ast_unary_int_const_fold(n->b->a, &fv))
                return 1;
            return 0;
        }
        if ((s->type & 15) != TYPE_INT || type_size(s->type) != 2)
            return 0;
        if (is_compound && expr_result_dead &&
            (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ)) {
            /* True dead-result fast RHS shapes (`i += 1`, `i += j`,
             * `i += s.f`) return above and use the dedicated fast emitter.
             * Other supported plain-int RHS expressions follow streaming's
             * general direct compound tail, which gen_assign_ast mirrors. */
            return ast_value_is_plain_int(n->b);
        }
        return 1;
    }
    case AST_INDEX:
        return ast_index_plain_int_read(n) || ast_index_long_read(n) ||
               ast_index_float_read(n);
    case AST_CALL: {
        /* Direct, named call `f(a, b, ...)` whose arguments are all 16-bit
         * word values (plain ints or string-literal pointers) pushed as one
         * word each.  This reproduces the
         * streaming named-call tail (reverse-order arg push + `call name` +
         * emit_cleanup_stack_bytes) for the common case.  Deferred (left to
         * streaming): calls through a function POINTER (indirect __call_hl);
         * the name-recognised builtins with argument fast paths
         * (__va_start/__va_arg/__va_end, strcpy, strlen, strchr, cb_is_zero(p));
         * any prototype that widens an argument to float/long/struct (the push
         * is then 4 bytes / an address, not a single word); and any other
         * non-word argument (a float/long/struct actual, a pointer expression
         * not yet covered by the AST, etc.). */
        int rt;
        struct Sym *cs;
        if (ast_call_star_indirect_supported(n))
            return 1;
        if (ast_call_indirect_supported(n))
            return 1;
        if (ast_va_builtin_supported(n))
            return 1;
        if (!ast_call_named_args_supported(n))
            return 0;
        cs = find_global(n->a->sval);
        rt = cs != NULL ? cs->type : TYPE_INT;
        if (type_is_struct_object(rt))
            return 0;
        return 1;
    }
    case AST_POSTFIX:
        return ast_postfix_plain_int(n);
    case AST_MEMBER:
        {
            int elem_type;
            return ast_member_plain_int_read(n) || ast_member_bitfield_read(n) ||
                   ast_member_long_read(n) || ast_member_float_read(n) ||
                   ast_member_pointer_read(n) ||
                   ast_member_array_field_elem_type(n, &elem_type);
        }
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
        if (!ast_logical_operand_ok(n->a) || !ast_logical_operand_ok(n->b))
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
        {
            int ptr_type;
            int no_deref;
            if (ast_pointer_expr_type(n, &ptr_type, &no_deref))
                return 1;
        }
        if (ast_cond_void_supported(n))
            return 1;
        return ast_cond_numeric_supported(n);
    case AST_CAST:
        /* `(type)expr` to an integer target.  Mirrors streaming's gen_unary
         * cast tail: evaluate the operand, then widen/narrow.  Constant
         * operands are folded by streaming, so defer to keep the immediate.
         * Float/long/struct/pointer targets defer (specialised handling). */
        {
            long folded;
            if (ast_int_const_cast_fold(n, &folded))
                return 1;
        }
        if (n->a == NULL)
            return 0;
        if ((n->type & 15) == TYPE_VOID)
            return ast_gen_supported(n->a);
        if (type_is_float(n->type)) {
            if (!ast_gen_supported(n->a))
                return 0;
            return ast_value_is_float_word(n->a) || ast_value_is_plain_int(n->a) ||
                   ast_value_is_long_word(n->a);
        }
        if (type_is_long(n->type)) {
            if (!ast_gen_supported(n->a))
                return 0;
            return ast_value_is_plain_int(n->a) || ast_value_is_long_word(n->a) ||
                   ast_value_is_float_word(n->a) || ast_value_is_pointer_word(n->a);
        }
        if (type_ptr_depth(n->type) > 0 && type_size(n->type) == 2) {
            /* `(T *)expr`: pointers are 16-bit words, so gen_cast_ast just
             * evaluates the operand and retags the type - no narrowing or
             * conversion.  Accept pointer-typed operands (mirroring
             * ast_pointer_expr_type's cast rule), integer constants, and
             * plain-int / pointer-word values. */
            int pt, nd;
            if (ast_node_is_const(n->a))
                return ast_value_is_plain_int(n->a);
            if (ast_pointer_expr_type(n->a, &pt, &nd))
                return !nd;
            if (!ast_gen_supported(n->a))
                return 0;
            return ast_value_is_plain_int(n->a) || ast_value_is_pointer_word(n->a);
        }
        if (!ast_is_plain_int_type(n->type) || type_size(n->type) > 2)
            return 0;
        if (ast_node_is_const(n->a))
            return ast_gen_supported(n->a) &&
                   (ast_value_is_plain_int(n->a) || ast_value_is_long_word(n->a) ||
                    ast_value_is_float_word(n->a));
        if (n->a->kind != AST_CALL && n->a->kind != AST_IDENT &&
            n->a->kind != AST_INDEX && n->a->kind != AST_MEMBER &&
            n->a->kind != AST_BINARY && n->a->kind != AST_UNARY &&
            n->a->kind != AST_COMMA && n->a->kind != AST_COND &&
            n->a->kind != AST_CAST &&
            n->a->kind != AST_SIZEOF_EXPR && n->a->kind != AST_SIZEOF_TYPE)
            return 0;                  /* avoid ptr-sub / folded const operands */
        if (n->a->a != NULL && n->a->a->kind == AST_IDENT &&
            n->a->a->sval != NULL && !strcmp(n->a->a->sval, "__offsetof"))
            return 0;                  /* const-folded by streaming */
        return ast_gen_supported(n->a);
    case AST_COMMA:
        /* `a , b`: streaming gen_expr evaluates the left operand (value
         * discarded), then the right, whose value/type is the result.  A flat
         * recursive walk reproduces that exactly when both sides are. */
        return ast_gen_supported(n->a) && ast_gen_supported(n->b);
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
    return ast_value_is_plain_int(arg);
}

static int ast_call_struct_arg_supported(int want_type, const struct AstNode *arg)
{
    int arg_type;

    if (!type_is_struct_object(want_type) || arg == NULL)
        return 0;
    if (ast_struct_addr_expr_supported(arg, &arg_type))
        return same_struct_type(want_type, arg_type);
    if (arg->kind == AST_CALL)
        return ast_struct_return_call_assign_supported(want_type, arg);
    return 0;
}

static void gen_call_struct_arg_ast(const struct AstNode *arg, int want_type)
{
    int arg_type;

    if (arg->kind == AST_CALL) {
        gen_struct_return_call_arg_ast(arg, want_type);
        return;
    }
    gen_struct_addr_expr_ast(arg, &arg_type);
    (void)arg_type;
    emit_push_struct_arg_from_hl(type_size(want_type));
}

static void gen_struct_return_call_arg_ast(const struct AstNode *call,
                                           int want_type)
{
    const char *name = call->a->sval;
    struct Sym *fn_sym = find_global(name);
    int struct_bytes = type_size(want_type);
    int arg_bytes = 0;
    int old_dead;
    int i;

    fprintf(outf, "\tld hl,-%d\n", struct_bytes);
    emit("\tadd hl,sp\n");
    emit("\tld sp,hl\n");
    emit("\tpush hl\n");

    old_dead = expr_result_dead;
    expr_result_dead = 0;
    for (i = call->list_len - 1; i >= 0; --i) {
        int actual_type;
        int inner_want;
        int have_want;
        int ptr_type;
        int no_deref;

        have_want = expected_arg_type(fn_sym, i, &inner_want);
        if (have_want && type_is_struct_object(inner_want)) {
            gen_call_struct_arg_ast(call->list[i], inner_want);
            arg_bytes += type_size(inner_want);
            continue;
        }

        if (ast_pointer_expr_type(call->list[i], &ptr_type, &no_deref))
            gen_pointer_expr_ast(call->list[i], &ptr_type, &no_deref);
        else
            ast_gen_expr(call->list[i]);
        actual_type = g_expr_type;
        if (have_want && type_is_float(inner_want)) {
            if (!type_is_float(actual_type))
                emit_convert_int_to_float(actual_type);
            emit("\tpush de\n\tpush hl\n");
            arg_bytes += 4;
        } else if (have_want && type_is_long(inner_want)) {
            if (!type_is_long(actual_type))
                emit_promote_int_to_long(actual_type, inner_want);
            emit("\tpush de\n\tpush hl\n");
            arg_bytes += 4;
        } else if (have_want && !type_is_long(inner_want) &&
                   !type_is_float(inner_want)) {
            emit("\tpush hl\n");
            arg_bytes += 2;
        } else if (type_is_long(actual_type) || type_is_float(actual_type)) {
            emit("\tpush de\n\tpush hl\n");
            arg_bytes += 4;
        } else {
            emit("\tpush hl\n");
            arg_bytes += 2;
        }
    }
    expr_result_dead = old_dead;

    emit_load_hl_from_sp_offset(arg_bytes);
    emit("\tpush hl\n");
    emit_extrn_if_needed(fn_sym);
    fprintf(outf, "\tcall %s\n", asm_name_for(name));
    emit_cleanup_stack_bytes(arg_bytes + 2);
    emit("\tpop bc\n");
    g_expr_type = want_type;
    g_long_from16 = 0;
}

static int ast_value_is_long_word(const struct AstNode *arg)
{
    struct Sym *s;

    if (arg == NULL)
        return 0;
    if (arg->kind == AST_INT_LIT)
        return type_is_long(arg->type);
    if (arg->kind == AST_UNARY) {
        long fv;
        if (ast_unary_long_const_fold(arg, &fv))
            return 1;
        if (arg->op == '*')
            return ast_deref_long_read(arg);
        if ((arg->op == '-' || arg->op == '~' || arg->op == '+') &&
            arg->a != NULL && ast_value_is_long_word(arg->a))
            return 1;
    }
    if (arg->kind == AST_POSTFIX &&
        (arg->op == TOK_INC || arg->op == TOK_DEC) &&
        arg->a != NULL) {
        /* `ul++` / `ul--` as a long-word value (e.g. `old = ul++`).  The
         * postfix emitter now updates the full 32-bit word with carry and
         * returns the old value in DE:HL. */
        if (arg->a->kind == AST_MEMBER) {
            int member_type;
            return ast_member_lvalue_type(arg->a, &member_type) &&
                   type_is_long(member_type);
        }
        if (arg->a->kind != AST_IDENT)
            return 0;
        s = find_sym(arg->a->sval);
        if (s != NULL && !s->is_const_value && s->storage != SC_FUNC &&
            !s->is_array && type_is_long(s->type))
            return 1;
    }
    if (!ast_gen_supported(arg))
        return 0;
    if (arg->kind == AST_IDENT) {
        s = find_sym(arg->sval);
         return s != NULL && s->storage != SC_FUNC && !s->is_array &&
             type_is_long(s->type);
    }
    if (arg->kind == AST_CALL && arg->a != NULL && arg->a->kind == AST_IDENT) {
        s = find_global(arg->a->sval);
        return s != NULL && type_is_long(s->type);
    }
    if (arg->kind == AST_CALL && ast_call_indirect_supported(arg)) {
        int callee_type;
        int no_deref;
        if (ast_pointer_expr_type(arg->a, &callee_type, &no_deref))
            return type_is_long(type_decay_ptr(callee_type));
    }
    if (arg->kind == AST_ASSIGN && arg->a != NULL) {
        int lhs_type;
        if (arg->a->kind == AST_IDENT) {
            s = find_sym(arg->a->sval);
            return s != NULL && !s->is_const_value && s->storage != SC_FUNC &&
                   !s->is_array && type_is_long(s->type);
        }
        if (arg->a->kind == AST_UNARY && arg->a->op == '*' &&
            ast_deref_lvalue_type(arg->a, &lhs_type))
            return type_is_long(lhs_type);
        if (arg->a->kind == AST_INDEX &&
            (ast_index_symbol_nd_elem_type(arg->a, &lhs_type) ||
             ast_index_pointer_expr_elem_type(arg->a, &lhs_type) ||
             ast_index_2d_array_elem_type(arg->a, &lhs_type)))
            return type_is_long(lhs_type);
        if (arg->a->kind == AST_MEMBER && ast_member_lvalue_type(arg->a, &lhs_type))
            return type_is_long(lhs_type);
    }
    if (arg->kind == AST_CAST && type_is_long(arg->type))
        return ast_gen_supported(arg);
    if (arg->kind == AST_COMMA)
        return ast_value_is_long_word(arg->b);
    if (arg->kind == AST_COND)
        return ast_cond_result_is_long(arg);
    if (arg->kind == AST_BINARY && is_shift_op(arg->op))
        return ast_value_is_long_word(arg->a) && ast_value_is_plain_int(arg->b);
    if (arg->kind == AST_BINARY && ast_long_arith_supported(arg))
        return 1;
    if (arg->kind == AST_BINARY && ast_mixed_long_rhs_arith_supported(arg))
        return 1;
    if (arg->kind == AST_INDEX)
        return ast_index_long_read(arg);
    return ast_member_long_read(arg);
}

static int ast_long_word_type(const struct AstNode *arg, int *out_type)
{
    struct Sym *rhs_sym;

    if (!ast_value_is_long_word(arg))
        return 0;

    switch (arg->kind) {
    case AST_INT_LIT:
    case AST_CAST:
        if (out_type)
            *out_type = arg->type;
        return type_is_long(arg->type);
    case AST_IDENT:
        rhs_sym = find_sym(arg->sval);
        if (rhs_sym == NULL || rhs_sym->is_const_value || rhs_sym->storage == SC_FUNC ||
            rhs_sym->is_array || !type_is_long(rhs_sym->type))
            return 0;
        if (out_type)
            *out_type = rhs_sym->type;
        return 1;
    case AST_POSTFIX:
        if (arg->a == NULL)
            return 0;
        if (arg->a->kind == AST_MEMBER) {
            int member_type;
            if (!ast_member_lvalue_type(arg->a, &member_type) ||
                !type_is_long(member_type))
                return 0;
            if (out_type)
                *out_type = member_type;
            return 1;
        }
        if (arg->a->kind != AST_IDENT)
            return 0;
        rhs_sym = find_sym(arg->a->sval);
        if (rhs_sym == NULL || rhs_sym->is_const_value || rhs_sym->storage == SC_FUNC ||
            rhs_sym->is_array || !type_is_long(rhs_sym->type))
            return 0;
        if (out_type)
            *out_type = rhs_sym->type;
        return 1;
    case AST_CALL:
        if (arg->a == NULL || arg->a->kind != AST_IDENT)
            return 0;
        rhs_sym = find_global(arg->a->sval);
        if (rhs_sym == NULL || !type_is_long(rhs_sym->type))
            return 0;
        if (out_type)
            *out_type = rhs_sym->type;
        return 1;
    default:
        return 0;
    }
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
    if (expected_arg_type(fn_sym, arg_index, &want_type)) {
        if (type_is_struct_object(want_type))
            return ast_call_struct_arg_supported(want_type, arg);
        if (type_is_float(want_type))
            return ast_value_is_float_word(arg) || ast_call_arg_word_supported(arg) ||
                   ast_value_is_long_word(arg);
        if (type_is_long(want_type))
            return ast_value_is_long_word(arg) || ast_call_arg_word_supported(arg);
        return ast_call_arg_word_supported(arg) || ast_value_is_long_word(arg) ||
               ast_value_is_float_word(arg);
    }
    return ast_call_arg_word_supported(arg) || ast_value_is_long_word(arg) ||
           ast_value_is_float_word(arg);
}

/* `va_start(ap, last)` / `va_end(ap)` after macro expansion to the builtin
 * calls `__va_start(ap, last)` / `__va_end(ap)`.  Both take bare identifier
 * arguments naming a va_list cursor (and, for va_start, the last fixed
 * parameter).  gen_call_ast emits the same address arithmetic the streaming
 * builtin does.  va_arg is deferred (its `type` argument is a sizeof the AST
 * does not preserve as a simple operand here). */
static int ast_va_builtin_supported(const struct AstNode *n)
{
    const char *cname;
    if (n == NULL || n->kind != AST_CALL || n->a == NULL ||
        n->a->kind != AST_IDENT)
        return 0;
    cname = n->a->sval;
    if (!strcmp(cname, "__va_end"))
        return n->list_len == 1 && n->list[0] != NULL &&
               n->list[0]->kind == AST_IDENT &&
               find_sym(n->list[0]->sval) != NULL;
    if (!strcmp(cname, "__va_start"))
        return n->list_len == 2 &&
               n->list[0] != NULL && n->list[0]->kind == AST_IDENT &&
               n->list[1] != NULL && n->list[1]->kind == AST_IDENT &&
               find_sym(n->list[0]->sval) != NULL &&
               find_sym(n->list[1]->sval) != NULL;
    return 0;
}

/* Direct named call with all arguments supported (return type ignored). */
static int ast_call_named_args_supported(const struct AstNode *n)
{
    struct Sym *call_sym;
    struct Sym *fn_sym;
    const char *cname;
    int i;
    if (n == NULL || n->kind != AST_CALL || n->a == NULL ||
        n->a->kind != AST_IDENT)
        return 0;
    cname = n->a->sval;
    if (!strcmp(cname, "__va_start") || !strcmp(cname, "__va_arg") ||
        !strcmp(cname, "__va_end") ||
        (!strcmp(cname, "cb_is_zero") && n->list_len == 1))
        return 0;
    call_sym = find_sym(cname);
    if (call_sym != NULL && call_sym->storage != SC_FUNC &&
        type_ptr_depth(call_sym->type) > 0)
        return 0;
    fn_sym = find_global(cname);
    for (i = 0; i < n->list_len; ++i) {
        if (!ast_call_arg_supported(fn_sym, i, n->list[i]))
            return 0;
    }
    return 1;
}

static const struct AstNode *ast_call_star_indirect_base(const struct AstNode *n)
{
    const struct AstNode *callee;
    int saw_star;

    if (n == NULL || n->kind != AST_CALL || n->a == NULL)
        return NULL;
    callee = n->a;
    saw_star = 0;
    while (callee != NULL && callee->kind == AST_UNARY && callee->op == '*') {
        saw_star = 1;
        callee = callee->a;
    }
    return saw_star ? callee : NULL;
}

static int ast_call_star_indirect_supported(const struct AstNode *n)
{
    const struct AstNode *base;
    struct Sym *s;
    int i;

    base = ast_call_star_indirect_base(n);
    if (base == NULL || base->kind != AST_IDENT)
        return 0;
    s = find_sym(base->sval);
    if (s == NULL || s->is_const_value || s->is_array)
        return 0;
    if (s->storage != SC_FUNC && type_ptr_depth(s->type) <= 0)
        return 0;
    for (i = 0; i < n->list_len; ++i) {
        if (!ast_call_arg_supported(NULL, i, n->list[i]))
            return 0;
    }
    return 1;
}

static int ast_call_indirect_supported(const struct AstNode *n)
{
    struct Sym *callee_sym;
    int callee_type;
    int no_deref;
    int i;

    if (n == NULL || n->kind != AST_CALL || n->a == NULL)
        return 0;
    if (n->a->kind == AST_IDENT) {
        callee_sym = find_sym(n->a->sval);
        if (callee_sym == NULL || callee_sym->storage == SC_FUNC ||
            type_ptr_depth(callee_sym->type) <= 0)
            return 0;
    }
    if (!ast_pointer_expr_type(n->a, &callee_type, &no_deref) || no_deref)
        return 0;
    if (type_ptr_depth(callee_type) <= 0 || type_size(callee_type) != 2)
        return 0;
    if (type_is_struct_object(type_decay_ptr(callee_type)))
        return 0;
    for (i = 0; i < n->list_len; ++i) {
        if (!ast_call_arg_supported(NULL, i, n->list[i]))
            return 0;
    }
    return 1;
}

static int ast_value_is_float_word(const struct AstNode *arg)
{
    struct Sym *s;

    if (arg == NULL || !ast_gen_supported(arg))
        return 0;
    if (arg->kind == AST_FLOAT_LIT)
        return 1;
    if (arg->kind == AST_UNARY && (arg->op == '-' || arg->op == '+') &&
        arg->a != NULL)
        return ast_value_is_float_word(arg->a);
    if (arg->kind == AST_UNARY && arg->op == '*')
        return ast_deref_float_read(arg);
    if (arg->kind == AST_IDENT) {
        s = find_sym(arg->sval);
        return s != NULL && s->storage != SC_FUNC && !s->is_array &&
               type_is_float(s->type);
    }
    if (arg->kind == AST_CALL && arg->a != NULL && arg->a->kind == AST_IDENT) {
        s = find_global(arg->a->sval);
        return s != NULL && type_is_float(s->type);
    }
    if (arg->kind == AST_CALL && ast_call_indirect_supported(arg)) {
        int callee_type;
        int no_deref;
        if (ast_pointer_expr_type(arg->a, &callee_type, &no_deref))
            return type_is_float(type_decay_ptr(callee_type));
    }
    if (arg->kind == AST_CAST && type_is_float(arg->type))
        return ast_gen_supported(arg);
    if (arg->kind == AST_ASSIGN && arg->a != NULL && arg->a->kind == AST_IDENT) {
        s = find_sym(arg->a->sval);
        return s != NULL && !s->is_const_value && s->storage != SC_FUNC &&
               !s->is_array && type_is_float(s->type);
    }
    if (arg->kind == AST_COND)
        return ast_cond_result_is_float(arg);
    if (arg->kind == AST_BINARY && is_float_arith_op(arg->op)) {
        int lhs_float = ast_value_is_float_word(arg->a);
        int rhs_float = ast_value_is_float_word(arg->b);
        if (!lhs_float && !rhs_float)
            return 0;
        return (lhs_float || ast_value_is_plain_int(arg->a) ||
                ast_value_is_long_word(arg->a)) &&
               (rhs_float || ast_value_is_plain_int(arg->b) ||
                ast_value_is_long_word(arg->b));
    }
    if (arg->kind == AST_INDEX)
        return ast_index_float_read(arg);
    if (arg->kind == AST_MEMBER)
        return ast_member_float_read(arg);
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
         return s != NULL && !s->is_const_value &&
             (s->storage == SC_FUNC || s->is_array || type_ptr_depth(s->type) > 0);
    case AST_CALL:
        if (n->a == NULL || n->a->kind != AST_IDENT)
            return 0;
        s = find_global(n->a->sval);
        return s != NULL && type_ptr_depth(s->type) > 0;
    case AST_MEMBER:
        return ast_member_pointer_read(n);
    default:
        return 0;
    }
}

static int ast_pointer_assign_rhs_supported(const struct AstNode *n)
{
    const struct AstNode *value;
    int ptr_type;
    int no_deref;
    if (n == NULL)
        return 0;
    value = (n->kind == AST_CAST) ? n->a : n;
    if (ast_null_pointer_const(value))
        return 1;
    if (n->kind == AST_CAST && ast_gen_supported(value) &&
        ast_value_is_plain_int(value))
        return 1;
    if (ast_pointer_expr_type(value, &ptr_type, &no_deref))
        return 1;
    if (value != NULL && value->kind == AST_CALL &&
        ast_value_is_pointer_word(value) && ast_call_named_args_supported(value))
        return 1;                      /* pointer-returning direct call */
    return ast_gen_supported(value) && ast_value_is_pointer_word(value);
}

/* A unary chain (-, +, ~) bottoming out in a single non-long INT_LIT folds to
 * a 16-bit immediate exactly as streaming's try_gen_const_expr does.  Returns 1
 * and stores the folded value when foldable; 0 otherwise. */
static int ast_unary_int_const_fold(const struct AstNode *n, long *out)
{
    long v;
    if (n == NULL)
        return 0;
    if (n->kind == AST_INT_LIT) {
        if (type_is_long(n->type))
            return 0;
        *out = n->ival;
        return 1;
    }
    if (n->kind == AST_UNARY && !type_is_long(n->type) &&
        (n->op == '-' || n->op == '+' || n->op == '~') &&
        ast_unary_int_const_fold(n->a, &v)) {
        *out = (n->op == '-') ? -v : (n->op == '~') ? ~v : v;
        return 1;
    }
    return 0;
}

static int ast_int_const_cast_fold(const struct AstNode *n, long *out)
{
    long v;

    if (ast_unary_int_const_fold(n, out))
        return 1;
    if (n == NULL || n->kind != AST_CAST || n->a == NULL)
        return 0;
    if (!ast_is_plain_int_type(n->type) || type_size(n->type) > 2)
        return 0;
    if (!ast_int_const_cast_fold(n->a, &v))
        return 0;
    *out = v;
    return 1;
}

static int ast_unary_long_const_fold(const struct AstNode *n, long *out)
{
    long v;
    if (n == NULL)
        return 0;
    if (n->kind == AST_INT_LIT) {
        if (!type_is_long(n->type))
            return 0;
        *out = n->ival;
        return 1;
    }
    if (n->kind == AST_UNARY &&
        (type_is_long(n->type) || (n->a != NULL && type_is_long(n->a->type))) &&
        (n->op == '-' || n->op == '+' || n->op == '~') &&
        ast_unary_long_const_fold(n->a, &v)) {
        *out = (n->op == '-') ? -v : (n->op == '~') ? ~v : v;
        return 1;
    }
    return 0;
}

static int ast_const_scalar_fold(const struct AstNode *n, long *out)
{
    long a;
    long b;
    struct Sym *s;
    int ei;

    if (n == NULL)
        return 0;
    switch (n->kind) {
    case AST_INT_LIT:
        *out = n->ival;
        return 1;
    case AST_IDENT:
        for (ei = 0; ei < nenum_consts; ++ei) {
            if (!strcmp(enum_const_names[ei], n->sval)) {
                *out = (long)(int)enum_const_values[ei];
                return 1;
            }
        }
        s = find_sym(n->sval);
        if (s != NULL && s->is_const_value && !type_is_float(s->type)) {
            *out = (long)s->const_value;
            return 1;
        }
        return 0;
    case AST_UNARY:
        if (!ast_const_scalar_fold(n->a, &a))
            return 0;
        switch (n->op) {
        case '+': *out = a; return 1;
        case '-': *out = -a; return 1;
        case '~': *out = ~a; return 1;
        case '!': *out = !a; return 1;
        default: return 0;
        }
    case AST_CAST:
        if (!ast_const_scalar_fold(n->a, &a))
            return 0;
        if (type_is_float(n->type) || type_ptr_depth(n->type) > 0)
            return 0;
        *out = ast_const_apply_int_cast(a, n->type);
        return 1;
    case AST_BINARY:
    case AST_LOGAND:
    case AST_LOGOR:
        if (!ast_const_scalar_fold(n->a, &a) || !ast_const_scalar_fold(n->b, &b))
            return 0;
        switch (n->kind == AST_BINARY ? n->op : n->kind) {
        case '+': *out = a + b; return 1;
        case '-': *out = a - b; return 1;
        case '*': *out = a * b; return 1;
        case '/': if (b == 0) return 0; *out = a / b; return 1;
        case '%': if (b == 0) return 0; *out = a % b; return 1;
        case TOK_SHL: *out = a << b; return 1;
        case TOK_SHR: *out = a >> b; return 1;
        case '&': *out = a & b; return 1;
        case '^': *out = a ^ b; return 1;
        case '|': *out = a | b; return 1;
        case '<': *out = a < b; return 1;
        case '>': *out = a > b; return 1;
        case TOK_LE: *out = a <= b; return 1;
        case TOK_GE: *out = a >= b; return 1;
        case TOK_EQ: *out = a == b; return 1;
        case TOK_NE: *out = a != b; return 1;
        case AST_LOGAND: *out = (a != 0) && (b != 0); return 1;
        case AST_LOGOR: *out = (a != 0) || (b != 0); return 1;
        default: return 0;
        }
    default:
        return 0;
    }
}

static long ast_const_apply_int_cast(long v, int type)
{
    unsigned long u;

    if (type_is_float(type) || type_ptr_depth(type) > 0)
        return v;
    if (type_size(type) <= 1) {
        u = ((unsigned long)v) & 0xffUL;
        if (!(type & TYPE_UNSIGNED) && (u & 0x80UL))
            return (long)(u | ~0xffUL);
        return (long)u;
    }
    if (type_size(type) <= 2) {
        u = ((unsigned long)v) & 0xffffUL;
        if (!(type & TYPE_UNSIGNED) && (u & 0x8000UL))
            return (long)(u | ~0xffffUL);
        return (long)u;
    }
    u = ((unsigned long)v) & 0xffffffffUL;
    if (!(type & TYPE_UNSIGNED) && (u & 0x80000000UL))
        return (long)(u | ~0xffffffffUL);
    return (long)u;
}

static int ast_const_condition_fold(const struct AstNode *n, long *out)
{
    return ast_const_scalar_fold(n, out);
}

static int ast_global_byte_array_const_store(const struct AstNode *n,
                                             struct Sym **out_arr,
                                             long *out_idx,
                                             long *out_rhs)
{
    struct Sym *arr;
    long rhs;

    if (n == NULL || n->kind != AST_ASSIGN || n->op != '=' || !expr_result_dead)
        return 0;
    if (n->a == NULL || n->a->kind != AST_INDEX || n->a->a == NULL ||
        n->a->a->kind != AST_IDENT || n->a->b == NULL ||
        n->a->b->kind != AST_INT_LIT)
        return 0;
    arr = find_sym(n->a->a->sval);
    if (arr == NULL || arr->storage != SC_GLOBAL || !arr->is_array ||
        type_size(arr->type) != 1)
        return 0;
    if (!ast_int_const_cast_fold(n->b, &rhs))
        return 0;
    if (rhs < 0 || rhs > 255)
        return 0;
    if (out_arr != NULL)
        *out_arr = arr;
    if (out_idx != NULL)
        *out_idx = n->a->b->ival;
    if (out_rhs != NULL)
        *out_rhs = rhs;
    return 1;
}

static int ast_global_byte_array_fast_store(const struct AstNode *n,
                                            struct Sym **out_arr,
                                            struct Sym **out_idx_sym,
                                            long *out_idx_const,
                                            int *out_idx_has_const,
                                            struct Sym **out_rhs_sym,
                                            long *out_rhs_const,
                                            int *out_rhs_kind)
{
    struct Sym *arr;
    struct Sym *idx_sym;
    struct Sym *rhs_sym;
    long rhs_const;
    long idx_const;
    int idx_has_const;
    int rhs_kind;

    if (n == NULL || n->kind != AST_ASSIGN || n->op != '=' || !expr_result_dead)
        return 0;
    if (n->a == NULL || n->a->kind != AST_INDEX || n->a->a == NULL ||
        n->a->a->kind != AST_IDENT || n->a->b == NULL)
        return 0;
    arr = find_sym(n->a->a->sval);
    if (arr == NULL || arr->storage != SC_GLOBAL || !arr->is_array ||
        type_size(arr->type) != 1)
        return 0;

    idx_sym = NULL;
    idx_const = 0;
    idx_has_const = 0;
    if (n->a->b->kind == AST_INT_LIT) {
        idx_const = n->a->b->ival;
        idx_has_const = 1;
    } else if (n->a->b->kind == AST_IDENT) {
        idx_sym = find_sym(n->a->b->sval);
        if (!sym_can_ix_direct(idx_sym) || type_size(idx_sym->type) != 1)
            return 0;
    } else {
        return 0;
    }

    rhs_sym = NULL;
    rhs_const = 0;
    rhs_kind = 0;
    if (n->b != NULL && n->b->kind == AST_IDENT) {
        rhs_sym = find_sym(n->b->sval);
        if (!sym_can_ix_direct(rhs_sym) || type_size(rhs_sym->type) != 1)
            return 0;
        rhs_kind = 1;
    } else if (n->b != NULL && n->b->kind == AST_INT_LIT) {
        rhs_const = n->b->ival;
        if (rhs_const < 0 || rhs_const > 255)
            return 0;
        rhs_kind = 2;
    } else {
        return 0;
    }

    if (out_arr != NULL)
        *out_arr = arr;
    if (out_idx_sym != NULL)
        *out_idx_sym = idx_sym;
    if (out_idx_const != NULL)
        *out_idx_const = idx_const;
    if (out_idx_has_const != NULL)
        *out_idx_has_const = idx_has_const;
    if (out_rhs_sym != NULL)
        *out_rhs_sym = rhs_sym;
    if (out_rhs_const != NULL)
        *out_rhs_const = rhs_const;
    if (out_rhs_kind != NULL)
        *out_rhs_kind = rhs_kind;
    return 1;
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

/* Cast `(type)expr` to a 16-bit integer target, mirroring streaming's gen_unary
 * cast tail (float/long/pointer targets excluded by the gate): evaluate the
 * operand, then drop a long high word, sign/zero-extend a byte, or no-op. */
static void gen_cast_ast(const struct AstNode *n)
{
    int t = n->type;
    ast_gen_expr(n->a);
    if (type_is_float(t)) {
        if (!type_is_float(g_expr_type))
            emit_convert_int_to_float(g_expr_type);
        g_expr_type = t;
        g_long_from16 = 0;
        return;
    }
    if (type_is_long(t)) {
        if (type_is_float(g_expr_type))
            emit_convert_float_to_intlike(t);
        else if (!type_is_long(g_expr_type))
            emit_extend_to_long_typed(g_expr_type);
        g_expr_type = t;
        g_long_from16 = 0;
        return;
    }
    if (type_is_float(g_expr_type)) {
        emit_convert_float_to_intlike(t);
        g_expr_type = t;
        g_long_from16 = 0;
        return;
    }
    if (type_size(t) == 1) {
        if (t & TYPE_UNSIGNED)
            emit("\tld h,0\n");
        else
            emit("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n");
    }
    g_expr_type = t;
    g_long_from16 = 0;
}

static void gen_str_lit(const struct AstNode *n)
{
    /* Intern at emit time (the build deferred this codegen side effect); the
     * 1:1 substitution at gen_expr preserves source order, so the assigned
    * string id remains stable. */
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

    /* stdin/stdout/stderr -> immediate FILE values 0/1/2, checked before
     * symbol resolution. */
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
    long fv;

    /* A unary chain over a single int literal folds to one immediate, exactly
     * as streaming's try_gen_const_expr does. */
    if ((op == '-' || op == '+' || op == '~') &&
        ast_unary_long_const_fold(n, &fv)) {
        fprintf(outf, "\tld hl,%ld\n", fv & 0xffffL);
        fprintf(outf, "\tld de,%ld\n", (fv >> 16) & 0xffffL);
        g_expr_type = type_is_long(n->type) ? n->type : n->a->type;
        g_long_from16 = 0;
        return;
    }
    if ((op == '-' || op == '+' || op == '~') &&
        ast_unary_int_const_fold(n, &fv)) {
        fprintf(outf, "\tld hl,%ld\n", fv & 0xffffL);
        g_expr_type = TYPE_INT;
        return;
    }

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
        if (n->a->kind == AST_MEMBER) {
            gen_member_addr_ast(n->a, &val_type);
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
        if (ast_va_arg_deref_type(n, &base)) {
            gen_va_arg_deref_ast(n, base);
            return;
        }
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
        /* gen_lvalue_addr + emit_pre_incdec_lvalue: load the object's address,
         * then in-place increment/decrement, leaving the updated value in HL
         * with g_expr_type = the object type. */
        struct Sym *s;
        int val_type;
        current_field_bit_width = 0;
        current_field_bit_shift = 0;
        current_field_bit_mask = 0;
        if (n->a->kind == AST_MEMBER) {
            gen_member_addr_ast(n->a, &val_type);
            emit_pre_incdec_lvalue(val_type, op);
        } else {
            s = find_sym(n->a->sval);
            emit_load_sym_addr(s);
            emit_pre_incdec_lvalue(s->type, op);
        }
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

static void gen_pointer_cmp_operand_ast(const struct AstNode *n)
{
    int ptr_type;
    int no_deref;

    if (ast_pointer_expr_type(n, &ptr_type, &no_deref)) {
        gen_pointer_expr_ast(n, &ptr_type, &no_deref);
    } else {
        ast_gen_expr(n);
    }
}

static void gen_pointer_cmp_ast(const struct AstNode *n)
{
    gen_pointer_cmp_operand_ast(n->a);
    emit("\tpush hl\n");
    gen_pointer_cmp_operand_ast(n->b);
    emit("\tex de,hl\n\tpop hl\n");
    gen_binop_typed(n->op, TYPE_INT | TYPE_UNSIGNED);
    g_expr_type = TYPE_INT;
    g_long_from16 = 0;
}

static void gen_pointer_diff_ast(const struct AstNode *n)
{
    int lhs_type;
    int no_deref;
    int elem = 1;

    gen_pointer_cmp_operand_ast(n->a);
    emit("\tpush hl\n");
    gen_pointer_cmp_operand_ast(n->b);
    emit("\tex de,hl\n\tpop hl\n");
    gen_binop('-');
    if (ast_pointer_expr_type(n->a, &lhs_type, &no_deref))
        elem = type_index_elem_size(lhs_type);
    divide_hl_by_elem_size(elem);
    g_expr_type = TYPE_INT;
    g_long_from16 = 0;
}

static void gen_binop32_promote_16lhs_ast(int op, int lhs_type, int common_type);

static void gen_long_cmp_ast(const struct AstNode *n)
{
    int lhs_type;
    int rhs_type;
    int common_type;

    ast_gen_expr(n->a);
    lhs_type = promote_int_type(g_expr_type);
    if (!type_is_long(lhs_type)) {
        emit("\tpush hl\n");
        ast_gen_expr(n->b);
        rhs_type = promote_int_type(g_expr_type);
        common_type = common_arith_type(lhs_type, rhs_type);
        if (type_is_long(rhs_type)) {
            gen_binop32_promote_16lhs_ast(n->op, lhs_type, common_type);
        } else {
            emit("\tex de,hl\n\tpop hl\n");
            gen_binop_typed(n->op, common_type);
        }
        g_expr_type = TYPE_INT;
        g_long_from16 = 0;
        return;
    }

    emit("\tpush de\n\tpush hl\n");
    ast_gen_expr(n->b);
    rhs_type = promote_int_type(g_expr_type);
    common_type = common_arith_type(lhs_type, rhs_type);
    emit_cast_16_to_common(rhs_type, common_type);
    gen_binop32_typed(n->op, common_type);
    g_expr_type = TYPE_INT;
    g_long_from16 = 0;
}

static void gen_long_arith_ast(const struct AstNode *n)
{
    int lhs_type;
    int common_type;

    ast_gen_expr(n->a);
    lhs_type = promote_int_type(g_expr_type);
    common_type = common_arith_type(lhs_type, n->peek_type);
    emit_cast_16_to_common(lhs_type, common_type);
    emit("\tpush de\n\tpush hl\n");
    ast_gen_expr(n->b);
    emit_cast_16_to_common(g_expr_type, common_type);
    gen_binop32_typed(n->op, common_type);
    g_expr_type = common_type;
    g_long_from16 = 0;
}

static void gen_binop32_promote_16lhs_ast(int op, int lhs_type, int common_type)
{
    emit("\tpop bc\n");
    emit("\tpush de\n\tpush hl\n");
    emit("\tld h,b\n\tld l,c\n");
    emit_cast_16_to_common(lhs_type, common_type);
    emit("\tpush de\n\tpush hl\n");
    emit("\tld hl,4\n\tadd hl,sp\n");
    emit("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n");
    emit("\tinc hl\n");
    emit("\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n");
    emit("\tex de,hl\n");
    gen_binop32_typed(op, common_type);
    emit("\tpop bc\n\tpop bc\n");
    g_long_from16 = 0;
}

/* Emit a plain-int binary operator with the uniform 16-bit sequence: evaluate
 * lhs into HL, promote, capture the common type from the rhs's stored peek,
 * then push / eval rhs / ex de,hl / pop hl / dispatch. Result type is int for
 * comparisons, common type otherwise. */
static void gen_binary_ast(const struct AstNode *n)
{
    int lhs_type;
    int common_type;
    const char *float_helper;

    if (n->op == '+' || n->op == '-') {
        int ptr_type;
        int no_deref;
        if (ast_pointer_expr_type(n, &ptr_type, &no_deref)) {
            gen_pointer_expr_ast(n, &ptr_type, &no_deref);
            return;
        }
    }

    if (ast_pointer_cmp_supported(n)) {
        gen_pointer_cmp_ast(n);
        return;
    }

    if (ast_pointer_diff_supported(n)) {
        gen_pointer_diff_ast(n);
        return;
    }

    if (ast_long_cmp_supported(n)) {
        gen_long_cmp_ast(n);
        return;
    }

    if (ast_long_arith_supported(n)) {
        gen_long_arith_ast(n);
        return;
    }

    ast_gen_expr(n->a);
    lhs_type = promote_int_type(g_expr_type);

    if (is_cmp_op(n->op) &&
        (type_is_float(g_expr_type) || ast_value_is_float_word(n->b))) {
        if (!type_is_float(g_expr_type))
            emit_convert_int_to_float(g_expr_type);
        emit("\tpush de\n\tpush hl\n");
        ast_gen_expr(n->b);
        if (!type_is_float(g_expr_type))
            emit_convert_int_to_float(g_expr_type);
        emit("\tpush de\n\tpush hl\n");
        emit_float_compare_call(n->op);
        g_expr_type = TYPE_INT;
        g_long_from16 = 0;
        return;
    }

    if (is_float_arith_op(n->op) &&
        (type_is_float(g_expr_type) || ast_value_is_float_word(n->b))) {
        if (!type_is_float(g_expr_type))
            emit_convert_int_to_float(g_expr_type);
        emit("\tpush de\n\tpush hl\n");
        ast_gen_expr(n->b);
        if (!type_is_float(g_expr_type))
            emit_convert_int_to_float(g_expr_type);
        emit("\tpush de\n\tpush hl\n");
        float_helper = (n->op == '+') ? "__fadd" :
                       (n->op == '-') ? "__fsub" :
                       (n->op == '*') ? "__fmul" : "__fdiv";
        emit_runtime_call(float_helper);
        emit("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n");
        g_expr_type = TYPE_FLOAT;
        g_long_from16 = 0;
        return;
    }

    common_type = common_arith_type(lhs_type, n->peek_type);

    /* `lhs * <const>` fast path: mirror streaming's emit_mul_hl_const (lhs in
     * HL, single const-mul, no push/pop or __mulu). */
    if (n->op == '*' && n->b->kind == AST_INT_LIT &&
        !type_is_long(common_type) && ast_mul_const_value_ok(n->b->ival)) {
        emit_mul_hl_const(n->b->ival & 0xffffL);
        g_expr_type = common_type;
        g_long_from16 = 0;
        return;
    }

    /* unsigned `lhs / pow2` -> logical shift; `lhs % pow2` -> mask.  Mirrors
     * streaming dcc_ops.c (lhs already in HL, no __divu/__remu call). */
    if ((n->op == '/' || n->op == '%') && n->b->kind == AST_INT_LIT &&
        (common_type & TYPE_UNSIGNED) && !type_is_long(common_type)) {
        int sh = int_log2_pow2((int)(n->b->ival & 0xffffL));
        if (sh >= 0) {
            if (n->op == '/')
                emit_logical_shift_right_hl_const(sh);
            else
                emit_and_hl_const((unsigned int)((n->b->ival & 0xffffL) - 1));
            g_expr_type = common_type;
            g_long_from16 = 0;
            return;
        }
    }

    emit("\tpush hl\n");
    ast_gen_expr(n->b);
    if (type_is_long(g_expr_type)) {
        common_type = common_arith_type(lhs_type, g_expr_type);
        gen_binop32_promote_16lhs_ast(n->op, lhs_type, common_type);
        g_expr_type = common_type;
        g_long_from16 = 0;
        return;
    }
    emit("\tex de,hl\n\tpop hl\n");
    gen_binop_typed(n->op, common_type);

    if (is_cmp_op(n->op))
        g_expr_type = TYPE_INT;
    else
        g_expr_type = common_type;
    g_long_from16 = 0;
}

/* Emit a plain-int shift with the non-literal shape:
 * evaluate lhs into HL, promote it (C89 integer promotion of the left operand;
 * the right operand does not participate in the usual conversions), push it,
 * evaluate rhs, move its low byte into B, restore HL, then run the shift loop.
 * Result type is the promoted left operand. */
static void gen_shift_ast(const struct AstNode *n)
{
    int lhs_type;

    ast_gen_expr(n->a);
    lhs_type = promote_int_type(g_expr_type);
    if (type_is_long(lhs_type)) {
        if (n->b->kind == AST_INT_LIT && ast_value_is_plain_int(n->b)) {
            if (!emit_shift_const_long(n->op, lhs_type, n->b->ival)) {
                fprintf(outf, "\tld b,%ld\n", n->b->ival & 255L);
                emit_shift_loop(n->op, lhs_type);
            }
        } else {
            emit("\tpush de\n\tpush hl\n");
            ast_gen_expr(n->b);
            emit("\tld b,l\n\tpop hl\n\tpop de\n");
            emit_shift_loop(n->op, lhs_type);
        }
        g_expr_type = lhs_type;
        g_long_from16 = 0;
        return;
    }
    emit("\tpush hl\n");
    ast_gen_expr(n->b);
    emit("\tld b,l\n\tpop hl\n");
    emit_shift_loop(n->op, lhs_type);
    g_expr_type = lhs_type;
    g_long_from16 = 0;
}

static void gen_index_subscript_expr_ast(const struct AstNode *n)
{
    int saved_dead = expr_result_dead;
    expr_result_dead = 0;
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
        expr_result_dead = saved_dead;
        return;
    }
    ast_gen_expr(n);
    expr_result_dead = saved_dead;
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

    if (ast_struct_deref_copy_assign_supported(n)) {
        gen_struct_deref_copy_assign_ast(n);
        return;
    }

    if (ast_struct_member_copy_assign_supported(n)) {
        gen_struct_member_copy_assign_ast(n);
        return;
    }

    if (ast_struct_copy_assign_supported(n)) {
        gen_struct_copy_assign_ast(n);
        return;
    }

    if (ast_long_va_arg_self_assign_supported(n, NULL)) {
        gen_long_va_arg_self_assign_ast(n);
        return;
    }

    {
        int lhs_type;
        if (n->op == '=' && ast_struct_addr_expr_supported(n->a, &lhs_type) &&
            ast_struct_return_call_assign_supported(lhs_type, n->b)) {
            gen_struct_return_call_assign_ast(n->a, n->b);
            return;
        }
    }

    /* Non-identifier lvalue store: a subscript element `arr[i]`, a struct field
     * `s.f` / `p->f`, or a deref `*p`.  Reproduce streaming's normal_assign
     * tail: the address machine differs per lvalue kind (factored helpers), but
     * the store tail is uniform.  Handles both plain `=` and the
     * arithmetic/bitwise compound operators; shift-assigns and any
     * wider/pointer element are excluded by the gate. */
    if (n->a->kind == AST_INDEX || n->a->kind == AST_MEMBER ||
        (n->a->kind == AST_UNARY && n->a->op == '*')) {
        struct Sym *byte_arr;
        struct Sym *byte_idx_sym;
        struct Sym *byte_rhs_sym;
        long byte_idx;
        long byte_rhs;
        int byte_idx_has_const;
        int byte_rhs_kind;
        int val_type;
        int want_dead = expr_result_dead;
        int bf_width;
        int bf_shift;
        unsigned int bf_mask;

        if (ast_global_byte_array_const_store(n, &byte_arr, &byte_idx, &byte_rhs)) {
            emit_global_byte_array_index_addr(byte_arr, NULL, byte_idx, 1);
            fprintf(outf, "\tld (hl),%ld\n", byte_rhs & 255);
            g_expr_type = byte_arr->type;
            g_long_from16 = 0;
            return;
        }

        if (ast_global_byte_array_fast_store(n, &byte_arr, &byte_idx_sym,
                                             &byte_idx, &byte_idx_has_const,
                                             &byte_rhs_sym, &byte_rhs,
                                             &byte_rhs_kind)) {
            emit_global_byte_array_index_addr(byte_arr, byte_idx_sym, byte_idx,
                                              byte_idx_has_const);
            if (byte_rhs_kind == 1) {
                fprintf(outf, "\tld a,(ix%+d)\n", byte_rhs_sym->offset);
                emit("\tld (hl),a\n");
            } else {
                fprintf(outf, "\tld (hl),%ld\n", byte_rhs & 255);
            }
            g_expr_type = byte_arr->type;
            g_long_from16 = 0;
            return;
        }

        if (n->a->kind == AST_INDEX)
            gen_index_addr_ast(n->a, &val_type);    /* HL = element address */
        else if (n->a->kind == AST_MEMBER)
            gen_member_addr_ast(n->a, &val_type);   /* HL = field address */
        else
            gen_deref_addr_ast(n->a, &val_type);    /* HL = target address */

        bf_width = current_field_bit_width;
        bf_shift = current_field_bit_shift;
        bf_mask = current_field_bit_mask;

        if (n->op == '=') {
            emit("\tpush hl\n");

            saved_dead = expr_result_dead;
            expr_result_dead = 0;
            if (type_ptr_depth(val_type) > 0 && n->b->kind == AST_CAST) {
                ast_gen_expr(n->b->a);              /* rhs -> HL */
            } else if (type_ptr_depth(val_type) > 0) {
                int ptr_type;
                int no_deref;
                if (ast_pointer_expr_type(n->b, &ptr_type, &no_deref))
                    gen_pointer_expr_ast(n->b, &ptr_type, &no_deref);
                else
                    ast_gen_expr(n->b);
            } else {
                ast_gen_expr(n->b);                 /* rhs -> HL */
            }
            expr_result_dead = saved_dead;
            if (type_size(val_type) == 4) {
                if (type_is_float(val_type)) {
                    if (!type_is_float(g_expr_type))
                        emit_convert_int_to_float(g_expr_type);
                } else if (!type_is_long(g_expr_type)) {
                    emit_extend_to_long_typed(g_expr_type);
                }
                emit_store_de_to_addr_hl(val_type);  /* pops address itself */
                g_long_from16 = 0;
                return;
            }
            if (bf_width > 0) {
                current_field_bit_width = bf_width;
                current_field_bit_shift = bf_shift;
                current_field_bit_mask = bf_mask;
                emit_store_bitfield_from_hl();
                g_long_from16 = 0;
                return;
            }
            emit("\tex de,hl\n\tpop hl\n");         /* DE = value, HL = address */
            emit_store_de_to_addr_hl(val_type);
            if (!want_dead)
                emit("\tex de,hl\n");
            g_long_from16 = 0;
            return;
        }

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

        if (type_size(val_type) == 4) {
            emit("\tpush hl\n");                    /* save lvalue address */
            emit_load_from_hl(val_type);             /* DE:HL = current value */

            if (type_is_long(val_type) &&
                (n->op == TOK_SHLEQ || n->op == TOK_SHREQ) &&
                n->b->kind == AST_INT_LIT) {
                if (!emit_shift_const_long(n->op, val_type, n->b->ival)) {
                    fprintf(outf, "\tld b,%ld\n", n->b->ival & 255L);
                    emit_shift_loop(n->op, val_type);
                }
                emit("\tld b,d\n\tld c,e\n");
                emit("\tpop de\n");
                emit("\tex de,hl\n");
                emit("\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tinc hl\n\tld (hl),c\n\tinc hl\n\tld (hl),b\n");
                if (!want_dead) {
                    emit("\tex de,hl\n");
                    emit("\tld d,b\n\tld e,c\n");
                }
                g_expr_type = val_type;
                g_long_from16 = 0;
                return;
            }

            emit("\tpush de\n\tpush hl\n");         /* save current value */

            saved_dead = expr_result_dead;
            expr_result_dead = 0;
            ast_gen_expr(n->b);                      /* rhs -> DE:HL or HL */
            expr_result_dead = saved_dead;

            if (type_is_long(val_type) &&
                (n->op == TOK_SHLEQ || n->op == TOK_SHREQ)) {
                emit("\tld b,l\n\tpop hl\n\tpop de\n");
                emit_shift_loop(n->op, val_type);
                emit("\tld b,d\n\tld c,e\n");
                emit("\tpop de\n");
                emit("\tex de,hl\n");
                emit("\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tinc hl\n\tld (hl),c\n\tinc hl\n\tld (hl),b\n");
                if (!want_dead) {
                    emit("\tex de,hl\n");
                    emit("\tld d,b\n\tld e,c\n");
                }
                g_expr_type = val_type;
                g_long_from16 = 0;
                return;
            }

            if (type_is_float(val_type)) {
                if (!type_is_float(g_expr_type))
                    emit_convert_int_to_float(g_expr_type);
                emit("\tpush de\n\tpush hl\n");
                emit_runtime_call(n->op == TOK_ADDEQ ? "__fadd" :
                                  n->op == TOK_SUBEQ ? "__fsub" :
                                  n->op == TOK_MULEQ ? "__fmul" : "__fdiv");
                emit("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n");
                emit_store_de_to_addr_hl(val_type);
                g_expr_type = val_type;
                g_long_from16 = 0;
                return;
            }

            common_type = common_arith_type(val_type, g_expr_type);
            emit_cast_16_to_common(g_expr_type, common_type);
            gen_binop32_typed(binop, common_type);
            emit_store_de_to_addr_hl(val_type);
            g_expr_type = val_type;
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

        if (n->op == TOK_SHLEQ || n->op == TOK_SHREQ) {
            emit("\tld b,l\n\tpop hl\n");
            emit_shift_loop(n->op, val_type);
            emit("\tex de,hl\n\tpop hl\n");
            emit_store_de_to_addr_hl(val_type);
            if (!want_dead)
                emit("\tex de,hl\n");
            g_long_from16 = 0;
            return;
        }

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

    if (n->op == '=' && type_is_struct_object(s->type) &&
        ast_struct_return_call_assign_supported(s->type, n->b)) {
        gen_struct_return_call_assign_ast(n->a, n->b);
        return;
    }

    if (n->op == '=' && type_ptr_depth(s->type) > 0 && !sym_can_ix_direct(s) &&
        !is_global_word_sym(s) && expr_result_dead) {
        saved_dead = expr_result_dead;
        emit_load_sym_addr(s);
        emit("\tpush hl\n");
        expr_result_dead = 0;
        if (n->b->kind == AST_CAST) {
            ast_gen_expr(n->b->a);
        } else {
            int ptr_type;
            int no_deref;
            if (ast_pointer_expr_type(n->b, &ptr_type, &no_deref))
                gen_pointer_expr_ast(n->b, &ptr_type, &no_deref);
            else
                ast_gen_expr(n->b);
        }
        expr_result_dead = saved_dead;
        emit("\tex de,hl\n\tpop hl\n");
        emit_store_de_to_addr_hl(s->type);
        g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    if (n->op == '=' && type_is_float(s->type) && !sym_can_ix_direct(s) &&
        expr_result_dead) {
        saved_dead = expr_result_dead;
        emit_load_sym_addr(s);
        emit("\tpush hl\n");
        expr_result_dead = 0;
        ast_gen_expr(n->b);
        expr_result_dead = saved_dead;
        if (!type_is_float(g_expr_type))
            emit_convert_int_to_float(g_expr_type);
        emit_store_de_to_addr_hl(s->type);
        g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    if (n->op == '=' && type_is_float(s->type) && sym_can_ix_direct(s)) {
        saved_dead = expr_result_dead;
        expr_result_dead = 0;
        ast_gen_expr(n->b);
        expr_result_dead = saved_dead;
        if (!type_is_float(g_expr_type))
            emit_convert_int_to_float(g_expr_type);
        emit_store_hl_to_sym_direct(s);
        g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    if ((n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
         n->op == TOK_MULEQ || n->op == TOK_DIVEQ) &&
        type_is_float(s->type) && !sym_can_ix_direct(s) && expr_result_dead) {
        saved_dead = expr_result_dead;
        emit_load_sym_addr(s);
        emit("\tpush hl\n");
        emit_load_from_hl(s->type);
        emit("\tpush de\n\tpush hl\n");
        expr_result_dead = 0;
        ast_gen_expr(n->b);
        expr_result_dead = saved_dead;
        if (!type_is_float(g_expr_type))
            emit_convert_int_to_float(g_expr_type);
        emit("\tpush de\n\tpush hl\n");
        emit_runtime_call(n->op == TOK_ADDEQ ? "__fadd" :
                          n->op == TOK_SUBEQ ? "__fsub" :
                          n->op == TOK_MULEQ ? "__fmul" : "__fdiv");
        emit("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n");
        emit_store_de_to_addr_hl(s->type);
        g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    if (n->op == '=' && type_is_long(s->type) && !sym_can_ix_direct(s) &&
        expr_result_dead) {
        saved_dead = expr_result_dead;
        emit_load_sym_addr(s);
        emit("\tpush hl\n");
        expr_result_dead = 0;
        ast_gen_expr(n->b);
        expr_result_dead = saved_dead;
        if (type_is_float(g_expr_type))
            emit_convert_float_to_intlike(s->type);
        else if (!type_is_long(g_expr_type))
            emit_extend_to_long_typed(g_expr_type);
        emit_store_de_to_addr_hl(s->type);
        g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    if ((n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
         n->op == TOK_MULEQ || n->op == TOK_DIVEQ) &&
        type_is_float(s->type) && sym_can_ix_direct(s)) {
        saved_dead = expr_result_dead;
        emit_load_sym_value_direct(s);
        emit("\tpush de\n\tpush hl\n");
        expr_result_dead = 0;
        ast_gen_expr(n->b);
        expr_result_dead = saved_dead;
        if (!type_is_float(g_expr_type))
            emit_convert_int_to_float(g_expr_type);
        emit("\tpush de\n\tpush hl\n");
        emit_runtime_call(n->op == TOK_ADDEQ ? "__fadd" :
                          n->op == TOK_SUBEQ ? "__fsub" :
                          n->op == TOK_MULEQ ? "__fmul" : "__fdiv");
        emit("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n");
        emit_store_hl_to_sym_direct(s);
        g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    if (n->op == '=' && type_is_long(s->type) && sym_can_ix_direct(s)) {
        saved_dead = expr_result_dead;
        expr_result_dead = 0;
        ast_gen_expr(n->b);
        expr_result_dead = saved_dead;
        if (type_is_float(g_expr_type))
            emit_convert_float_to_intlike(s->type);
        else if (!type_is_long(g_expr_type))
            emit_extend_to_long_typed(g_expr_type);
        emit_store_hl_to_sym_direct(s);
        g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    if ((n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
         n->op == TOK_MULEQ || n->op == TOK_DIVEQ || n->op == TOK_MODEQ ||
         n->op == TOK_ANDEQ || n->op == TOK_OREQ  || n->op == TOK_XOREQ) &&
        type_is_long(s->type) && sym_can_ix_direct(s)) {
        int b32;
        saved_dead = expr_result_dead;
        emit_load_sym_value_direct(s);
        emit("\tpush de\n\tpush hl\n");
        expr_result_dead = 0;
        ast_gen_expr(n->b);
        expr_result_dead = saved_dead;
        if (!type_is_long(g_expr_type))
            emit_extend_to_long_typed(g_expr_type);
        switch (n->op) {
        case TOK_ADDEQ: b32 = '+'; break;
        case TOK_SUBEQ: b32 = '-'; break;
        case TOK_MULEQ: b32 = '*'; break;
        case TOK_DIVEQ: b32 = '/'; break;
        case TOK_MODEQ: b32 = '%'; break;
        case TOK_ANDEQ: b32 = '&'; break;
        case TOK_OREQ:  b32 = '|'; break;
        default:        b32 = '^'; break;   /* TOK_XOREQ */
        }
        gen_binop32(b32, s->type);
        emit_store_hl_to_sym_direct(s);
        g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    if ((n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
         n->op == TOK_MULEQ || n->op == TOK_DIVEQ || n->op == TOK_MODEQ ||
         n->op == TOK_ANDEQ || n->op == TOK_OREQ  || n->op == TOK_XOREQ) &&
        type_is_long(s->type) && !sym_can_ix_direct(s) && expr_result_dead) {
        int b32;
        saved_dead = expr_result_dead;
        emit_load_sym_addr(s);
        emit("\tpush hl\n");
        emit_load_from_hl(s->type);
        emit("\tpush de\n\tpush hl\n");
        expr_result_dead = 0;
        ast_gen_expr(n->b);
        expr_result_dead = saved_dead;
        if (!type_is_long(g_expr_type))
            emit_extend_to_long_typed(g_expr_type);
        switch (n->op) {
        case TOK_ADDEQ: b32 = '+'; break;
        case TOK_SUBEQ: b32 = '-'; break;
        case TOK_MULEQ: b32 = '*'; break;
        case TOK_DIVEQ: b32 = '/'; break;
        case TOK_MODEQ: b32 = '%'; break;
        case TOK_ANDEQ: b32 = '&'; break;
        case TOK_OREQ:  b32 = '|'; break;
        default:        b32 = '^'; break;   /* TOK_XOREQ */
        }
        gen_binop32(b32, s->type);
        emit_store_de_to_addr_hl(s->type);
        g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    if ((n->op == TOK_SHLEQ || n->op == TOK_SHREQ) &&
        type_is_long(s->type) && !sym_can_ix_direct(s) && expr_result_dead) {
        saved_dead = expr_result_dead;
        emit_load_sym_addr(s);
        emit("\tpush hl\n");
        emit_load_from_hl(s->type);
        if (n->b->kind == AST_INT_LIT && ast_value_is_plain_int(n->b)) {
            if (!emit_shift_const_long(n->op, s->type, n->b->ival)) {
                fprintf(outf, "\tld b,%ld\n", n->b->ival & 255L);
                emit_shift_loop(n->op, s->type);
            }
        } else {
            emit("\tpush de\n\tpush hl\n");
            expr_result_dead = 0;
            ast_gen_expr(n->b);
            expr_result_dead = saved_dead;
            emit("\tld b,l\n\tpop hl\n\tpop de\n");
            emit_shift_loop(n->op, s->type);
        }
        emit("\tld b,d\n\tld c,e\n");
        emit("\tpop de\n");
        emit("\tex de,hl\n");
        emit("\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tinc hl\n\tld (hl),c\n\tinc hl\n\tld (hl),b\n");
        g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    if ((n->op == TOK_SHLEQ || n->op == TOK_SHREQ) &&
        type_is_long(s->type) && sym_can_ix_direct(s)) {
        saved_dead = expr_result_dead;
        emit_load_sym_value_direct(s);
        expr_result_dead = 0;
        if (n->b->kind == AST_INT_LIT && ast_value_is_plain_int(n->b)) {
            if (!emit_shift_const_long(n->op, s->type, n->b->ival)) {
                fprintf(outf, "\tld b,%ld\n", n->b->ival & 255L);
                emit_shift_loop(n->op, s->type);
            }
        } else {
            emit("\tpush de\n\tpush hl\n");
            ast_gen_expr(n->b);
            emit("\tld b,l\n\tpop hl\n\tpop de\n");
            emit_shift_loop(n->op, s->type);
        }
        expr_result_dead = saved_dead;
        emit_store_hl_to_sym_direct(s);
        g_expr_type = s->type;
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

    if (expr_result_dead && !sym_can_ix_direct(s) && !is_global_word_sym(s) &&
        (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ ||
         n->op == TOK_ANDEQ || n->op == TOK_OREQ || n->op == TOK_XOREQ) &&
        ast_is_plain_int_type(s->type) &&
        (type_size(s->type) == 1 || type_size(s->type) == 2)) {
        int want_dead = expr_result_dead;
        if (n->op == TOK_ADDEQ)
            binop = '+';
        else if (n->op == TOK_SUBEQ)
            binop = '-';
        else if (n->op == TOK_ANDEQ)
            binop = '&';
        else if (n->op == TOK_OREQ)
            binop = '|';
        else
            binop = '^';

        emit_load_sym_addr(s);
        emit("\tpush hl\n");
        emit_load_from_hl(s->type);
        emit("\tpush hl\n");

        saved_dead = expr_result_dead;
        expr_result_dead = 0;
        ast_gen_expr(n->b);
        expr_result_dead = saved_dead;

        emit("\tex de,hl\n\tpop hl\n");
        common_type = common_arith_type(s->type, g_expr_type);
        gen_binop_typed(binop, common_type);
        emit("\tex de,hl\n\tpop hl\n");
        emit_store_de_to_addr_hl(s->type);
        if (!want_dead)
            emit("\tex de,hl\n");
        g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    if (expr_result_dead &&
        (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ) &&
        !type_is_long(s->type) && !type_is_float(s->type) &&
        type_ptr_depth(s->type) == 0 &&
                sym_can_ix_direct(s) && ast_value_is_plain_int(n->b) &&
                !(n->b->kind == AST_INT_LIT ||
                    (n->b->kind == AST_IDENT && sym_can_ix_direct(find_sym(n->b->sval))))) {
                binop = (n->op == TOK_ADDEQ) ? '+' : '-';

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
                g_expr_type = s->type;
                g_long_from16 = 0;
                return;
        }

        if (expr_result_dead &&
                (n->op == TOK_ADDEQ || n->op == TOK_SUBEQ) &&
                !type_is_long(s->type) && !type_is_float(s->type) &&
        (n->b->kind == AST_INT_LIT || n->b->kind == AST_IDENT ||
         ast_const_plain_int_binary_supported(n->b))) {
        emit_load_sym_value_direct(s);
        if (n->b->kind == AST_INT_LIT) {
            long scaled = n->b->ival;
            if (s->type & (TYPE_PTR | TYPE_PTR2))
                scaled *= type_index_elem_size(s->type);
            emit_ld_de_const(scaled);
        } else if (n->b->kind == AST_IDENT) {
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
        } else {
            emit("\tpush hl\n");
            saved_dead = expr_result_dead;
            expr_result_dead = 0;
            ast_gen_expr(n->b);
            expr_result_dead = saved_dead;
            if (s->type & (TYPE_PTR | TYPE_PTR2))
                scale_hl_by_elem_size(type_index_elem_size(s->type));
            emit("\tex de,hl\n\tpop hl\n");
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
            if (rs != NULL && sym_can_ix_direct(rs) &&
                !type_is_float(rs->type) && !type_is_long(rs->type)) {
                fprintf(outf, "\tld a,(ix%+d)\n", rs->offset);
                fprintf(outf, "\tld (ix%+d),a\n", s->offset);
                g_expr_type = s->type;
                g_long_from16 = 0;
                return;
            }
        }
        if (type_size(s->type) == 1 && n->b->kind == AST_INT_LIT) {
            fprintf(outf, "\tld (ix%+d),%ld\n", s->offset, n->b->ival & 255);
            g_expr_type = s->type;
            g_long_from16 = 0;
            return;
        }
        if (type_size(s->type) == 1) {
            long fv;
            if (ast_int_const_cast_fold(n->b, &fv)) {
                fprintf(outf, "\tld (ix%+d),%ld\n", s->offset, fv & 255);
                g_expr_type = s->type;
                g_long_from16 = 0;
                return;
            }
        }
        if (type_size(s->type) == 1 && sym_can_ix_direct(s) &&
            n->b->kind == AST_CALL) {
            /* Mirror streaming's size-1 store-from-call tail exactly: evaluate
             * the call into HL and store L only (no byte->int promote, which
             * would diverge from streaming). */
            saved_dead = expr_result_dead;
            expr_result_dead = 0;
            ast_gen_expr(n->b);
            expr_result_dead = saved_dead;
            emit_store_hl_to_sym_direct(s);
            g_expr_type = s->type;
            g_long_from16 = 0;
            return;
        }
        if (type_ptr_depth(s->type) > 0 && n->b->kind == AST_CAST) {
            ast_gen_expr(n->b->a);
        } else if (type_ptr_depth(s->type) > 0) {
            int ptr_type;
            int no_deref;
            if (ast_pointer_expr_type(n->b, &ptr_type, &no_deref))
                gen_pointer_expr_ast(n->b, &ptr_type, &no_deref);
            else
                ast_gen_expr(n->b);
        } else {
            ast_gen_expr(n->b);
        }
        if (type_is_float(g_expr_type))
            emit_convert_float_to_intlike(s->type);
        else if (!type_is_long(g_expr_type))
            emit_promote_byte_to_int(g_expr_type);
        emit_store_hl_to_sym_direct(s);
        g_long_from16 = 0;
        return;
    }

    if (expr_result_dead && type_size(s->type) == 1 && !sym_can_ix_direct(s) &&
        (n->op == TOK_ANDEQ || n->op == TOK_OREQ || n->op == TOK_XOREQ)) {
        if (n->op == TOK_ANDEQ)
            binop = '&';
        else if (n->op == TOK_OREQ)
            binop = '|';
        else
            binop = '^';
        emit_load_sym_addr(s);
        emit("\tpush hl\n");
        emit_load_from_hl(s->type);
        emit("\tpush hl\n");
        saved_dead = expr_result_dead;
        expr_result_dead = 0;
        ast_gen_expr(n->b);
        expr_result_dead = saved_dead;
        emit("\tex de,hl\n\tpop hl\n");
        common_type = common_arith_type(s->type, g_expr_type);
        gen_binop_typed(binop, common_type);
        emit("\tex de,hl\n\tpop hl\n");
        emit_store_de_to_addr_hl(s->type);
        g_expr_type = s->type;
        g_long_from16 = 0;
        return;
    }

    if (n->op == TOK_SHLEQ || n->op == TOK_SHREQ) {
        emit_load_sym_value_direct(s);
        emit("\tpush hl\n");
        saved_dead = expr_result_dead;
        expr_result_dead = 0;
        ast_gen_expr(n->b);
        expr_result_dead = saved_dead;
        emit("\tld b,l\n\tpop hl\n");
        emit_shift_loop(n->op, s->type);
        emit_store_hl_to_sym_direct(s);
        g_long_from16 = 0;
        return;
    }

    if (type_size(s->type) == 2 &&
        (n->op == TOK_MULEQ || n->op == TOK_DIVEQ || n->op == TOK_MODEQ) &&
        ast_long_word_type(n->b, &common_type)) {
        int b32;
        saved_dead = expr_result_dead;
        emit_load_sym_value_direct(s);
        common_type = common_arith_type(s->type, common_type);
        emit_cast_16_to_common(s->type, common_type);
        emit("\tpush de\n\tpush hl\n");
        expr_result_dead = 0;
        ast_gen_expr(n->b);
        expr_result_dead = saved_dead;
        emit_cast_16_to_common(g_expr_type, common_type);
        switch (n->op) {
        case TOK_MULEQ: b32 = '*'; break;
        case TOK_DIVEQ: b32 = '/'; break;
        default:        b32 = '%'; break;
        }
        gen_binop32(b32, common_type);
        emit_store_hl_to_sym_direct(s);
        g_expr_type = s->type;
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
    int no_deref;
    int global_ptr_preloaded = 0;
    int field_array = 0;
    int member_pointer = 0;
    int fa_dimc = 0;
    int fa_dims[4];
    int di;

    for (di = 0; di < 4; ++di)
        fa_dims[di] = 0;

    if (ast_index_reversed_pointer_expr_elem_type(n, &val_type)) {
        gen_pointer_expr_ast(n->b, &cur_type, &no_deref);
        elem_size = type_index_elem_size(cur_type);
        if (n->a->kind == AST_INT_LIT) {
            emit_add_const_to_hl(n->a->ival * elem_size);
        } else {
            emit("\tpush hl\n");
            gen_index_subscript_expr_ast(n->a);
            scale_hl_by_elem_size(elem_size);
            emit("\tex de,hl\n");
            emit("\tpop hl\n");
            emit("\tadd hl,de\n");
        }
        *out_val_type = val_type;
        return;
    }

    if (ast_index_symbol_nd_addressable_addr(n)) {
        const struct AstNode *idxs[8];
        struct Sym *ns;
        int count;
        int idx;
        ast_index_symbol_nd_collect(n, &ns, idxs, &count);
        emit_load_sym_addr(ns);
        cur_type = ns->type;
        if (!ns->is_array && type_ptr_depth(cur_type) > 0)
            emit_load_from_hl(cur_type);
        for (idx = 0; idx < count; ++idx) {
            if (ns->is_array)
                elem_size = sym_array_index_elem_size(ns, idx);
            else
                elem_size = sym_pointer_array_index_elem_size(ns, cur_type, idx);
            if (idxs[idx]->kind == AST_INT_LIT) {
                emit_add_const_to_hl(idxs[idx]->ival * elem_size);
            } else {
                emit("\tpush hl\n");
                gen_index_subscript_expr_ast(idxs[idx]);
                scale_hl_by_elem_size(elem_size);
                emit("\tex de,hl\n");
                emit("\tpop hl\n");
                emit("\tadd hl,de\n");
            }
            if (!ns->is_array)
                cur_type = type_decay_ptr(cur_type);
        }
        *out_val_type = ns->is_array ? ns->type : type_decay_ptr(ns->type);
        return;
    }

    {
        const struct AstNode *idxs[8];
        const struct AstNode *base;
        struct Sym *ps;
        int count;
        int idx;
        int di2;

        if (ast_index_deref_pointer_array_collect(n, &ps, &base, idxs, &count,
                                                  &val_type)) {
            gen_pointer_expr_ast(base, &cur_type, &no_deref);
            for (idx = 0; idx < count; ++idx) {
                elem_size = type_size(val_type);
                if (elem_size <= 0)
                    elem_size = 2;
                for (di2 = idx + 1; di2 < ps->dim_count; ++di2)
                    if (ps->dims[di2] > 0)
                        elem_size *= ps->dims[di2];
                if (idxs[idx]->kind == AST_INT_LIT) {
                    emit_add_const_to_hl(idxs[idx]->ival * elem_size);
                } else {
                    emit("\tpush hl\n");
                    gen_index_subscript_expr_ast(idxs[idx]);
                    scale_hl_by_elem_size(elem_size);
                    emit("\tex de,hl\n");
                    emit("\tpop hl\n");
                    emit("\tadd hl,de\n");
                }
            }
            *out_val_type = val_type;
            return;
        }
    }

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

    {
        const struct AstNode *member;
        const struct AstNode *idxs[4];
        int count;
        int dimc;
        int dims[4];
        int idx;

        if (ast_index_member_array_nd_collect(n, &member, idxs, &count, &val_type)) {
            gen_member_addr_ast(member, &cur_type);
            dimc = current_field_array_dim_count;
            for (di = 0; di < 4; ++di)
                dims[di] = current_field_array_dims[di];
            for (idx = 0; idx < count; ++idx) {
                elem_size = ast_field_array_index_stride(type_size(cur_type), dimc,
                                                         dims, idx);
                if (idxs[idx]->kind == AST_INT_LIT) {
                    emit_add_const_to_hl(idxs[idx]->ival * elem_size);
                } else {
                    emit("\tpush hl\n");
                    gen_index_subscript_expr_ast(idxs[idx]);
                    scale_hl_by_elem_size(elem_size);
                    emit("\tex de,hl\n");
                    emit("\tpop hl\n");
                    emit("\tadd hl,de\n");
                }
            }
            *out_val_type = val_type;
            return;
        }
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

    if (ast_index_pointer_expr_elem_type(n, &val_type)) {
        gen_pointer_expr_ast(n->a, &cur_type, &no_deref);
        elem_size = type_index_elem_size(cur_type);
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

    if (n->a->kind == AST_MEMBER) {
        gen_member_addr_ast(n->a, &val_type);
        cur_type = val_type;
        if (type_ptr_depth(cur_type) > 0 && current_field_array_dim_count == 0) {
            emit_load_from_hl(cur_type);
            member_pointer = 1;
        } else {
            field_array = 1;
            fa_dimc = current_field_array_dim_count;
            for (di = 0; di < 4; ++di)
                fa_dims[di] = current_field_array_dims[di];
        }
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
    else if (member_pointer)
        elem_size = type_index_elem_size(cur_type);
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
    else if (member_pointer)
        val_type = type_decay_ptr(cur_type);
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

static void gen_call_star_indirect_ast(const struct AstNode *n)
{
    const struct AstNode *base;
    int arg_bytes;
    int old_dead;
    int i;

    base = ast_call_star_indirect_base(n);
    arg_bytes = 0;

    old_dead = expr_result_dead;
    expr_result_dead = 0;
    ast_gen_expr(base);
    emit("\tpush hl\n");
    for (i = n->list_len - 1; i >= 0; --i) {
        int actual_type;
        int ptr_type;
        int no_deref;

        if (ast_pointer_expr_type(n->list[i], &ptr_type, &no_deref))
            gen_pointer_expr_ast(n->list[i], &ptr_type, &no_deref);
        else
            ast_gen_expr(n->list[i]);
        actual_type = g_expr_type;
        if (type_is_long(actual_type) || type_is_float(actual_type)) {
            emit("\tpush de\n\tpush hl\n");
            arg_bytes += 4;
        } else {
            emit("\tpush hl\n");
            arg_bytes += 2;
        }
    }
    expr_result_dead = old_dead;

    emit_call_hl_from_stack_offset(arg_bytes);
    emit_cleanup_stack_bytes(arg_bytes + 2);
    g_expr_type = TYPE_INT;
    g_long_from16 = 0;
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
    const char *name;
    struct Sym *fn_sym;
    int arg_bytes = 0;
    int old_dead;
    int i;

    if (ast_call_star_indirect_supported(n)) {
        gen_call_star_indirect_ast(n);
        return;
    }

    if (ast_call_indirect_supported(n)) {
        int callee_type;
        int no_deref;
        gen_pointer_expr_ast(n->a, &callee_type, &no_deref);
        emit("\tpush hl\n");
        old_dead = expr_result_dead;
        expr_result_dead = 0;
        for (i = n->list_len - 1; i >= 0; --i) {
            int actual_type;
            int ptr_type;
            int arg_no_deref;

            if (ast_pointer_expr_type(n->list[i], &ptr_type, &arg_no_deref))
                gen_pointer_expr_ast(n->list[i], &ptr_type, &arg_no_deref);
            else
                ast_gen_expr(n->list[i]);
            actual_type = g_expr_type;
            if (type_is_long(actual_type) || type_is_float(actual_type)) {
                emit("\tpush de\n\tpush hl\n");
                arg_bytes += 4;
            } else {
                emit("\tpush hl\n");
                arg_bytes += 2;
            }
        }
        expr_result_dead = old_dead;
        emit_call_hl_from_stack_offset(arg_bytes);
        g_expr_type = type_decay_ptr(callee_type);
        g_long_from16 = 0;
        emit_cleanup_stack_bytes(arg_bytes + 2);
        return;
    }

    name = n->a->sval;
    fn_sym = find_global(name);

    /* va_start(ap, last) / va_end(ap) builtins: reproduce the streaming
     * __va_start / __va_end address arithmetic.  va_start sets ap to the
     * address just past the last fixed parameter; va_end clears ap. */
    if (ast_va_builtin_supported(n)) {
        struct Sym *ap = find_sym(n->list[0]->sval);
        if (!strcmp(name, "__va_start")) {
            struct Sym *last = find_sym(n->list[1]->sval);
            int sz = type_size(last->type);
            if (sz < 2)
                sz = 2;
            emit_load_sym_addr(last);       /* HL = &last */
            emit_add_const_to_hl(sz);       /* HL = first unnamed arg */
            emit("\tex de,hl\n");
            emit_load_sym_addr(ap);         /* HL = &ap */
            emit_store_de_to_addr_hl(ap->type);
        } else {                            /* __va_end */
            emit("\tld de,0\n");
            emit_load_sym_addr(ap);         /* HL = &ap */
            emit_store_de_to_addr_hl(ap->type);
        }
        emit("\tld hl,0\n");
        g_expr_type = TYPE_INT;
        g_long_from16 = 0;
        return;
    }


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

        have_want = expected_arg_type(fn_sym, i, &want_type);
        if (have_want && type_is_struct_object(want_type)) {
            gen_call_struct_arg_ast(n->list[i], want_type);
            arg_bytes += type_size(want_type);
            continue;
        }

        if (ast_pointer_expr_type(n->list[i], &ptr_type, &no_deref))
            gen_pointer_expr_ast(n->list[i], &ptr_type, &no_deref);
        else
            ast_gen_expr(n->list[i]);
        actual_type = g_expr_type;
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
        } else if (have_want && !type_is_long(want_type) &&
                   !type_is_float(want_type)) {
            emit("\tpush hl\n");
            arg_bytes += 2;
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

static void gen_struct_return_call_assign_ast(const struct AstNode *lhs,
                                              const struct AstNode *rhs)
{
    const char *name = rhs->a->sval;
    struct Sym *fn_sym = find_global(name);
    int lhs_type;
    int arg_bytes = 0;
    int old_dead;
    int i;

    gen_struct_addr_expr_ast(lhs, &lhs_type);
    emit("\tpush hl\n");

    old_dead = expr_result_dead;
    expr_result_dead = 0;
    for (i = rhs->list_len - 1; i >= 0; --i) {
        int actual_type;
        int want_type;
        int have_want;
        int ptr_type;
        int no_deref;

        have_want = expected_arg_type(fn_sym, i, &want_type);
        if (have_want && type_is_struct_object(want_type)) {
            gen_call_struct_arg_ast(rhs->list[i], want_type);
            arg_bytes += type_size(want_type);
            continue;
        }

        if (ast_pointer_expr_type(rhs->list[i], &ptr_type, &no_deref))
            gen_pointer_expr_ast(rhs->list[i], &ptr_type, &no_deref);
        else
            ast_gen_expr(rhs->list[i]);
        actual_type = g_expr_type;
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
        } else if (have_want && !type_is_long(want_type) &&
                   !type_is_float(want_type)) {
            emit("\tpush hl\n");
            arg_bytes += 2;
        } else if (type_is_long(actual_type) || type_is_float(actual_type)) {
            emit("\tpush de\n\tpush hl\n");
            arg_bytes += 4;
        } else {
            emit("\tpush hl\n");
            arg_bytes += 2;
        }
    }
    expr_result_dead = old_dead;

    emit_load_hl_from_sp_offset(arg_bytes);
    emit("\tpush hl\n");
    emit_extrn_if_needed(fn_sym);
    fprintf(outf, "\tcall %s\n", asm_name_for(name));
    emit_cleanup_stack_bytes(arg_bytes + 2);
    emit("\tpop bc\n");
    g_expr_type = lhs_type;
    g_long_from16 = 0;
}

static void gen_struct_addr_expr_ast(const struct AstNode *n, int *out_type)
{
    struct Sym *s;

    switch (n->kind) {
    case AST_IDENT:
        s = find_sym(n->sval);
        emit_load_sym_addr(s);
        *out_type = s->type;
        return;
    case AST_INDEX:
        gen_index_addr_ast(n, out_type);
        return;
    case AST_UNARY:
        gen_deref_addr_ast(n, out_type);
        return;
    case AST_MEMBER:
        gen_member_addr_ast(n, out_type);
        return;
    default:
        fatal("gen_struct_addr_expr_ast: unsupported node");
    }
}

static void gen_struct_copy_assign_ast(const struct AstNode *n)
{
    int lhs_type;
    int rhs_type;

    gen_struct_addr_expr_ast(n->a, &lhs_type);  /* HL = destination */
    emit("\tpush hl\n");
    gen_struct_addr_expr_ast(n->b, &rhs_type);  /* HL = source */
    (void)rhs_type;
    emit("\tex de,hl\n\tpop hl\n");
    emit_copy_de_to_hl_bytes(type_size(lhs_type));
    g_expr_type = lhs_type;
    g_long_from16 = 0;
}

static void gen_struct_deref_copy_assign_ast(const struct AstNode *n)
{
    int lhs_type;
    int rhs_type;

    gen_deref_addr_ast(n->a, &lhs_type);     /* HL = destination */
    emit("\tpush hl\n");
    gen_deref_addr_ast(n->b, &rhs_type);     /* HL = source */
    (void)rhs_type;
    emit("\tex de,hl\n\tpop hl\n");       /* DE = source, HL = dest */
    emit_copy_de_to_hl_bytes(type_size(lhs_type));
    g_expr_type = lhs_type;
    g_long_from16 = 0;
}

static void gen_struct_member_copy_assign_ast(const struct AstNode *n)
{
    int lhs_type;
    struct Sym *rhs;

    gen_member_addr_ast(n->a, &lhs_type);  /* HL = destination */
    emit("\tpush hl\n");
    rhs = find_sym(n->b->sval);
    emit_load_sym_addr(rhs);               /* HL = source */
    emit("\tex de,hl\n\tpop hl\n");
    emit_copy_de_to_hl_bytes(type_size(lhs_type));
    g_expr_type = lhs_type;
    g_long_from16 = 0;
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
    } else if (!arrow && n->a->kind == AST_MEMBER) {
        gen_member_addr_ast(n->a, &cur_type);
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
    * global state after this node matches the helper contract byte-for-byte. */
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
    int elem_type;

    gen_member_addr_ast(n, &val_type);
    if (ast_member_array_field_elem_type(n, &elem_type)) {
        g_expr_type = type_add_ptr(elem_type);
        g_long_from16 = 0;
        return;
    }
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

    if (n->kind == AST_STR_LIT) {
        ast_gen_expr(n);
        *out_type = g_expr_type;
        *out_no_deref = 0;
        return;
    }

    if (n->kind == AST_POSTFIX) {
        if (n->a->kind == AST_MEMBER) {
            int val_type;
            gen_member_addr_ast(n->a, &val_type);
            gen_post_update_from_addr(val_type, n->op);
            *out_type = val_type;
        } else {
            struct Sym *s = find_sym(n->a->sval);
            gen_post_update_symbol_addr_value(s, n->op);
            *out_type = s->type;
        }
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

    if (n->kind == AST_CAST && type_ptr_depth(n->type) > 0) {
        if (ast_pointer_expr_type(n->a, &ptr_type, &no_deref))
            gen_pointer_expr_ast(n->a, &ptr_type, &no_deref);
        else
            ast_gen_expr(n->a);
        g_expr_type = n->type;
        g_long_from16 = 0;
        *out_type = n->type;
        *out_no_deref = 0;
        return;
    }

    if (n->kind == AST_COND) {
        int lfalse = new_label();
        int lend = new_label();
        int true_type;
        int false_type;
        int true_no_deref;
        int false_no_deref;
        ast_gen_expr(n->a);
        emit_test_expr_nonzero(g_expr_type, lfalse, 0);
        gen_pointer_expr_ast(n->b, &true_type, &true_no_deref);
        emit_jp_label("jp", lend);
        emit_label(lfalse);
        gen_pointer_expr_ast(n->c, &false_type, &false_no_deref);
        emit_label(lend);
        g_expr_type = type_ptr_depth(true_type) > 0 ? true_type : false_type;
        g_long_from16 = 0;
        *out_type = g_expr_type;
        *out_no_deref = true_no_deref && false_no_deref;
        return;
    }

    if (n->kind == AST_BINARY && (n->op == '+' || n->op == '-')) {
        int elem;
        int was_row_ptr;
        int saved_dead;

        if (n->op == '+' && !ast_pointer_expr_type(n->a, &ptr_type, &no_deref)) {
            gen_pointer_expr_ast(n->b, &ptr_type, &no_deref);
            was_row_ptr = (g_array_decay_stride > 0);
            elem = was_row_ptr ? g_array_decay_stride : type_index_elem_size(ptr_type);
            g_array_decay_stride = 0;

            emit("\tpush hl\n");
            saved_dead = expr_result_dead;
            expr_result_dead = 0;
            ast_gen_expr(n->a);
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
        if (n->op == '+')
            emit("\tadd hl,de\n");
        else
            emit("\tor a\n\tsbc hl,de\n");
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
        if (ast_pointer_expr_type(n, &member_type, &no_deref)) {
            int addr_type;
            gen_index_addr_ast(n, &addr_type);
            if (!no_deref)
                emit_load_from_hl(addr_type);
            g_expr_type = no_deref ? member_type : addr_type;
            g_long_from16 = 0;
            *out_type = g_expr_type;
            *out_no_deref = no_deref;
            return;
        }
        gen_index_addr_ast(n, &member_type);
        emit_load_from_hl(member_type);
        g_expr_type = member_type;
        g_long_from16 = 0;
        *out_type = member_type;
        *out_no_deref = 0;
        return;
    }

    if (n->kind == AST_ASSIGN) {
        ast_gen_expr(n);
        *out_type = g_expr_type;
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

    if (n->a->kind == AST_POSTFIX &&
        (n->a->op == TOK_INC || n->a->op == TOK_DEC) &&
        n->a->a != NULL && n->a->a->kind == AST_IDENT) {
        s = find_sym(n->a->a->sval);
        gen_post_update_symbol_addr_value(s, n->a->op);
        base = type_decay_ptr(s->type);
        if ((base & 15) == TYPE_VOID)
            base = TYPE_CHAR;
        *out_val_type = base;
        return;
    }

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
    int need_long_result;
    int result_is_float;
    int no_deref;

    if (ast_pointer_expr_type(n, &true_type, &no_deref)) {
        gen_pointer_expr_ast(n, &true_type, &no_deref);
        return;
    }

    ast_gen_expr(n->a);                 /* condition (== streaming gen_lor) */
    lfalse = new_label();
    lend = new_label();
    emit_test_expr_nonzero(g_expr_type, lfalse, 0);

    if (ast_cond_void_supported(n)) {
        ast_gen_expr(n->b);
        emit_jp_label("jp", lend);
        emit_label(lfalse);
        ast_gen_expr(n->c);
        emit_label(lend);
        g_expr_type = TYPE_VOID;
        g_long_from16 = 0;
        return;
    }

    result_is_float = ast_cond_result_is_float(n);

    ast_gen_expr(n->b);                 /* true arm */
    true_type = g_expr_type;
    need_long_result = 0;
    if (result_is_float) {
        if (!type_is_float(true_type))
            emit_convert_int_to_float(true_type);
    } else {
        need_long_result = type_is_long(true_type);
        if (!type_is_long(true_type))
            emit_extend_to_long((true_type & TYPE_UNSIGNED) ||
                                (true_type & (TYPE_PTR | TYPE_PTR2)));
    }
    emit_jp_label("jp", lend);

    emit_label(lfalse);
    ast_gen_expr(n->c);                 /* false arm */
    false_type = g_expr_type;

    if (result_is_float) {
        if (!type_is_float(false_type))
            emit_convert_int_to_float(false_type);
    } else {
        if (type_is_long(false_type))
            need_long_result = 1;
        if (need_long_result && !type_is_long(false_type)) {
            emit_extend_to_long((false_type & TYPE_UNSIGNED) ||
                                (false_type & (TYPE_PTR | TYPE_PTR2)));
            false_type = (false_type & TYPE_UNSIGNED) ?
                         (TYPE_LONG | TYPE_UNSIGNED) : TYPE_LONG;
        }
    }

    emit_label(lend);
    if (result_is_float) {
        g_expr_type = TYPE_FLOAT;
    } else if (need_long_result) {
        if ((true_type & TYPE_UNSIGNED) || (false_type & TYPE_UNSIGNED))
            g_expr_type = TYPE_LONG | TYPE_UNSIGNED;
        else
            g_expr_type = TYPE_LONG;
    } else {
        g_expr_type = common_arith_type(true_type, false_type);
    }
    g_long_from16 = 0;
}

/* Emit a postfix `lv++` / `lv--` on a plain-int identifier/member. */
static void gen_postfix_ast(const struct AstNode *n)
{
    struct Sym *s;
    int val_type;

    if (n->a->kind == AST_MEMBER) {
        gen_member_addr_ast(n->a, &val_type);
        gen_post_update_from_addr(val_type, n->op);
        g_long_from16 = 0;
        return;
    }

    if (n->a->kind == AST_INDEX) {
        gen_index_addr_ast(n->a, &val_type);
        gen_post_update_from_addr(val_type, n->op);
        g_long_from16 = 0;
        return;
    }

    if (n->a->kind == AST_UNARY && n->a->op == '*') {
        gen_deref_addr_ast(n->a, &val_type);
        gen_post_update_from_addr(val_type, n->op);
        g_long_from16 = 0;
        return;
    }

    s = find_sym(n->a->sval);

    /* Pointer and plain-int identifiers: the streaming post-update helper
     * advances pointers by their element size, stores the full value, and
     * returns the old value in HL.  It deliberately bails (returns 0) on
     * long/float and on symbols it cannot address directly. */
    if (try_emit_post_update_sym_direct(s, n->op)) {
        g_long_from16 = 0;
        return;
    }

    if (s != NULL && s->storage == SC_LOCAL && ast_is_plain_int_type(s->type) &&
        (type_size(s->type) == 1 || type_size(s->type) == 2)) {
        emit_load_sym_addr(s);
        gen_post_update_from_addr(s->type, n->op);
        g_long_from16 = 0;
        return;
    }

    /* long identifiers: update the full 32-bit value with carry ripple across
     * all four bytes via the address-based helper, which also selects an
     * in-place statement-context fast path when the result is dead (e.g. a
     * for-loop increment).  A plain `inc hl` would corrupt the high word. */
    if (s != NULL && type_is_long(s->type)) {
        emit_load_sym_addr(s);
        gen_post_update_from_addr(s->type, n->op);
        g_long_from16 = 0;
        return;
    }

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
    case AST_SIZEOF_EXPR:
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
    case AST_CAST:
        gen_cast_ast(n);
        break;
    case AST_COMMA:
        ast_gen_expr(n->a);
        ast_gen_expr(n->b);
        break;
    default:
        /* ast_gen_supported() gates entry; reaching here is a bug. */
        fatal("ast_gen_expr: unsupported node");
    }
}

/* ------------------------------------------------------------------------- *
 * Statement-level AST codegen.
 *
 * A statement hook in gen_statement builds the upcoming statement from the
 * token stream and emits it from the AST.  Unsupported shapes are reported as
 * compiler errors in normal codegen.
 * ------------------------------------------------------------------------- */

/* Gate for `return [expr] ;`. */
static int ast_return_stmt_supported(const struct AstNode *n)
{
    int rt = current_return_type;

    if (type_is_struct_object(rt)) {
        struct Sym *rs;
        int src_type;
        if (n->a == NULL)
            return 0;
        if (n->a->kind == AST_IDENT) {
            rs = find_sym(n->a->sval);
            return rs != NULL && !rs->is_const_value && rs->storage != SC_FUNC &&
                   !rs->is_array && type_is_struct_object(rs->type) &&
                   same_struct_type(rt, rs->type);
        }
        if (n->a->kind == AST_UNARY && n->a->op == '*' &&
            ast_deref_lvalue_type(n->a, &src_type))
            return type_is_struct_object(src_type) && same_struct_type(rt, src_type);
        return 0;
    }
    if (rt & (TYPE_PTR | TYPE_PTR2)) {
        int ptr_type;
        int no_deref;
        if (n->a == NULL)
            return 1;
        if (ast_pointer_assign_rhs_supported(n->a))
            return 1;
        return ast_pointer_expr_type(n->a, &ptr_type, &no_deref);
    }
    if ((rt & 15) == TYPE_VOID)
        return n->a == NULL;
    if (type_is_float(rt)) {
        if (n->a == NULL)
            return 0;
        return ast_value_is_float_word(n->a) || ast_value_is_plain_int(n->a);
    }
    if (type_is_long(rt)) {
        if (n->a == NULL)
            return 0;
        return ast_value_is_long_word(n->a) || ast_value_is_plain_int(n->a);
    }
    if (type_size(rt) == 1) {
        if (n->a == NULL)
            return 1;
        if (n->a->kind == AST_IDENT) {
            struct Sym *rs = find_sym(n->a->sval);
            return sym_can_ix_direct(rs) && type_size(rs->type) == 1;
        }
        if (n->a->kind == AST_INT_LIT)
            return n->a->ival >= 0 && n->a->ival <= 255;
        return ast_gen_supported(n->a) && ast_value_is_plain_int(n->a);
    }
    if ((rt & 15) != TYPE_INT || type_size(rt) != 2)
        return 0;

    if (n->a != NULL) {
        if (!ast_gen_supported(n->a) || !ast_value_is_plain_int(n->a))
            return 0;
    }
    return 1;
}

/* Emit `return [expr] ;`: evaluate the value into the ABI return registers
 * when present, then jump to the function's shared return label. */
static void gen_return_ast(const struct AstNode *n)
{
    if (n->a != NULL && type_is_struct_object(current_return_type)) {
        int src_type;
        if (n->a->kind == AST_IDENT) {
            struct Sym *rs = find_sym(n->a->sval);
            emit_load_sym_addr(rs);
        } else {
            gen_deref_addr_ast(n->a, &src_type);
            (void)src_type;
        }
        emit("\tex de,hl\n");
        emit("\tld l,(ix+4)\n\tld h,(ix+5)\n");
        emit_copy_de_to_hl_bytes(type_size(current_return_type));
        g_expr_type = current_return_type;
    } else if (n->a != NULL && type_size(current_return_type) == 1) {
        if (n->a->kind == AST_IDENT) {
            struct Sym *rs = find_sym(n->a->sval);
            fprintf(outf, "\tld l,(ix%+d)\n", rs->offset);
            if (current_return_type & TYPE_UNSIGNED)
                emit("\tld h,0\n");
            else
                emit("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n");
            g_expr_type = current_return_type;
        } else if (n->a->kind == AST_INT_LIT) {
            fprintf(outf, "\tld hl,%ld\n", n->a->ival & 255);
            g_expr_type = current_return_type;
        } else {
            ast_gen_expr(n->a);
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
    if (n->a != NULL) {
        if (type_is_float(current_return_type) && !type_is_float(g_expr_type)) {
            emit_convert_int_to_float(g_expr_type);
            g_expr_type = current_return_type;
        } else if (!type_is_float(current_return_type) && type_is_float(g_expr_type)) {
            emit_convert_float_to_intlike(current_return_type);
            g_expr_type = current_return_type;
        } else if (type_size(current_return_type) == 1 && !type_is_long(g_expr_type)) {
            if (current_return_type & TYPE_UNSIGNED)
                emit("\tld h,0\n");
            else
                emit("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n");
            g_expr_type = current_return_type;
        } else if (type_is_long(current_return_type) && !type_is_long(g_expr_type)) {
            emit_promote_int_to_long(g_expr_type, current_return_type);
            g_expr_type = current_return_type;
        }
    }
    emit_jp_label("jp", current_return_label);
}

/* A comparison operand that reaches the plain-16-bit direct-branch
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
    * reaches the general plain-16-bit compare path: the leading token
     * is a struct/pointer identifier immediately followed by `.`/`->`, so
     * parse_byte_operand_fast declines (a member is neither a bare byte ident
     * nor a global byte array), gen_direct_small_const_int_rel declines (the
     * token after the base ident is `.`/`->`, not a relop), gen_direct_byte_
    * bitand declines (no `&`), and the global-char-array condition probe
    * declines without emitting (a struct base is not a global char
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
    * In the while context a comparison `*p OP x` is an AST_BINARY (not the
    * bare `while (*ptr)` truthiness fast path), so pointer-walk fast paths do
    * not apply.  Restricted to a
     * size-2 element so no byte path intervenes; char* (size-1) defers. */
    if (e->kind == AST_UNARY && e->op == '*') {
        int base;
        struct Sym *ps;
        if (!ast_deref_plain_int_read(e))
            return 0;
        if (e->a->kind != AST_IDENT)
            return 0;
        ps = find_sym(e->a->sval);
        if (ps == NULL)
            return 0;
        base = type_decay_ptr(ps->type);
        if (type_size(base) != 2)
            return 0;
        return 1;
    }
    /* A subscript read `arr[i]` of a 2-byte (int) element reaches the general
     * compare as well.  A size-1 element would be a global *char* array (the
    * only subscript shape with a dead-probe in the global-char-array condition
    * probe) or a byte relational operand, so we require size 2: an
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
            if (base == NULL)
                return 0;
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

static int ast_is_const_plain_int_cmp_cond(const struct AstNode *n)
{
    return n != NULL && n->kind == AST_BINARY && is_cmp_op(n->op) &&
           ast_const_plain_int_binary_supported(n);
}

/* Translate a comparison operand expression into a streaming ByteOperand (the
 * same struct parse_byte_operand_fast builds), or return 0.  We reproduce only
 * the two register/immediate kinds: kind 1 (IX-direct UNSIGNED char local/param)
 * and kind 2 (0..255 constant).  Kind 3 (global byte array[index]) is DEFERRED:
 * in an `if`, the global-char-array condition probe emits a DEAD
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

static int ast_is_direct_byte_bitand_cond(const struct AstNode *n)
{
    struct Sym *s;

    if (n == NULL || n->kind != AST_BINARY || n->op != '&')
        return 0;
    if (n->a == NULL || n->a->kind != AST_IDENT ||
        n->b == NULL || n->b->kind != AST_INT_LIT)
        return 0;
    s = find_sym(n->a->sval);
    if (s == NULL || !sym_can_ix_direct(s) || type_size(s->type) != 1)
        return 0;
    return n->b->ival >= 0 && n->b->ival <= 255;
}

static int ast_global_char_index_cond(const struct AstNode *n, struct Sym **out_sym)
{
    struct Sym *s;

    if (n == NULL || n->kind != AST_INDEX || n->a == NULL || n->b == NULL ||
        n->a->kind != AST_IDENT)
        return 0;
    s = find_global(n->a->sval);
    if (!is_global_char_array_sym(s))
        return 0;
    if (!ast_index_subscript_supported(n->b))
        return 0;
    if (out_sym != NULL)
        *out_sym = s;
    return 1;
}

static void ast_gen_global_char_index_branch(const struct AstNode *n, int label,
                                             int branch_when_true)
{
    struct Sym *s;
    int saved_dead;

    ast_global_char_index_cond(n, &s);
    saved_dead = expr_result_dead;
    expr_result_dead = 0;
    ast_gen_expr(n->b);
    expr_result_dead = saved_dead;
    if (!branch_when_true) {
        emit_test_global_char_index_zero(s, label);
    } else {
        int lzero = new_label();
        emit_test_global_char_index_zero(s, lzero);
        emit_jp_label("jp", label);
        emit_label(lzero);
    }
}

static int ast_is_float_cmp_cond(const struct AstNode *n)
{
    int lhs_float;
    int rhs_float;

    if (n == NULL || n->kind != AST_BINARY || !is_cmp_op(n->op))
        return 0;
    lhs_float = ast_value_is_float_word(n->a);
    rhs_float = ast_value_is_float_word(n->b);
    if (!lhs_float && !rhs_float)
        return 0;
    return (lhs_float || ast_value_is_plain_int(n->a) ||
            ast_value_is_long_word(n->a)) &&
           (rhs_float || ast_value_is_plain_int(n->b) ||
            ast_value_is_long_word(n->b));
}

/* Is the controlling expression of an `if` / `while` one that AST should lower
 * via its GENERIC condition path (gen_expr + emit_test_expr_nonzero), rather
 * through the generic condition path rather than one of the specialised
 * direct-branch fast paths? Those
 * decline for a condition that has no top-level relational/logical/conditional
 * operator, is not a constant, is not a global-char-array subscript, and is not
 * the `char_ixvar & byteconst` bitand shape (the latter is already excluded
 * because a binary with a literal operand is not ast_gen_supported).  We accept
 * only a conservative whitelist proven to reach the generic path; anything else
 * defers (always safe).  The while gate additionally excludes bare deref
 * conditions that belong to pointer-walk fast paths. */
static int ast_cond_generic(const struct AstNode *n)
{
    long cv;
    if (n == NULL)
        return 0;
    if (ast_const_condition_fold(n, &cv))
        return 1;
    if (ast_is_const_cmp_cond(n))
        return 1;
    if (ast_is_const_plain_int_cmp_cond(n))
        return 1;
    if (ast_is_byte_cmp_cond(n))
        return 1;
    if (ast_is_direct_byte_bitand_cond(n))
        return 1;
    if (ast_global_char_index_cond(n, NULL))
        return 1;
    if (ast_is_float_cmp_cond(n))
        return 1;
    if (ast_long_cmp_supported(n))
        return 1;
    if (ast_index_cmp_cond_supported(n))
        return 1;
    if (ast_gen_supported(n) &&
        (ast_value_is_float_word(n) || ast_value_is_pointer_word(n) ||
         ast_value_is_long_word(n)) &&
        !ast_node_is_const(n))
        return 1;
    if (!ast_gen_supported(n) || !ast_value_is_plain_int(n))
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
    case AST_INDEX: {
        struct Sym *gs;
        if (n->a != NULL && n->a->kind == AST_IDENT) {
            gs = find_global(n->a->sval);
            if (is_global_char_array_sym(gs))
                return 0;
        }
        return 1;
    }
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
         * exactly.  Other supported comparisons whose operands are not direct
         * branch fast-path shapes (for example struct-member comparisons) reach
         * streaming's generic gen_expr + nonzero-test path, which the AST can
         * reproduce by value-emitting the comparison and testing it. */
        if (is_cmp_op(n->op))
            return ast_is_simple_cmp_cond(n) || ast_gen_supported(n);
        if (n->op == '&')
            return !ast_is_direct_byte_bitand_cond(n);
        return 1;
    case AST_LOGAND:
    case AST_LOGOR:
        /* if (a && b) / while (a || b): condition fast paths all
         * decline for a top-level &&/|| (simple_direct_condition_until requires
         * no logical operator), so AST falls to the
         * generic gen_expr + emit_test_expr_nonzero - which the AST reproduces
         * via ast_gen_cond_branch's generic fallback (ast_gen_expr emits the
         * same short-circuit 0/1 value, then the same nonzero test).  The guard
         * above already required ast_gen_supported && plain-int && non-const. */
        return 1;
    case AST_COND:
        /* A top-level ?: controlling expression has no condition fast path in
         * the AST direct-branch helpers; supported plain-int conditionals reach the generic
         * gen_expr + nonzero-test path that ast_gen_cond_branch emits. */
        return 1;
    case AST_ASSIGN:
        /* Assignment in a controlling expression is excluded from all direct
         * condition probes, so AST evaluates it normally and tests the
         * resulting value. */
        return 1;
    default:
        return 0;
    }
}

/* Recognise the `lhs = rhs1 +/- rhs2` simple-local self-add statement shape.
 * The AST walker emits the compact historical sequence directly when lhs,
 * rhs1 and rhs2 are all ix-direct 2-byte locals. */
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

static void ast_emit_local_self_add_stmt(const struct AstNode *e)
{
    const struct AstNode *lhs = e->a;
    const struct AstNode *rhs = e->b;
    const struct AstNode *rhs1 = rhs->a;
    const struct AstNode *rhs2 = rhs->b;
    struct Sym *lhs_sym = find_sym(lhs->sval);
    struct Sym *rhs1_sym = find_sym(rhs1->sval);
    struct Sym *rhs2_sym = find_sym(rhs2->sval);

    fprintf(outf, "\tld l,(ix%+d)\n", rhs1_sym->offset);
    fprintf(outf, "\tld h,(ix%+d)\n", rhs1_sym->offset + 1);
    fprintf(outf, "\tld e,(ix%+d)\n", rhs2_sym->offset);
    fprintf(outf, "\tld d,(ix%+d)\n", rhs2_sym->offset + 1);
    if ((rhs1_sym->type & (TYPE_PTR | TYPE_PTR2)) &&
        !(rhs2_sym->type & (TYPE_PTR | TYPE_PTR2))) {
        int elem = type_index_elem_size(rhs1_sym->type);
        if (elem > 1) {
            emit("\tpush hl\n");
            emit("\tex de,hl\n");
            scale_hl_by_elem_size(elem);
            emit("\tex de,hl\n");
            emit("\tpop hl\n");
        }
    }
    if (rhs->op == '+') {
        emit("\tadd hl,de\n");
    } else {
        emit("\tor a\n\tsbc hl,de\n");
        if ((rhs1_sym->type & (TYPE_PTR | TYPE_PTR2)) &&
            (rhs2_sym->type & (TYPE_PTR | TYPE_PTR2))) {
            int elem = type_index_elem_size(rhs1_sym->type);
            if (elem > 1) {
                fprintf(outf, "\tld de,%d\n", elem);
                emit_runtime_call("__divs");
            }
        }
    }
    fprintf(outf, "\tld (ix%+d),l\n", lhs_sym->offset);
    fprintf(outf, "\tld (ix%+d),h\n", lhs_sym->offset + 1);
    g_expr_type = lhs_sym->type;
}

/* For a dead-result top-level ++/-- statement on a bare identifier, return the
 * symbol if it matches emit_incdec_sym_direct's fast path (ix-direct
 * or global word, any scalar/pointer/long size), else NULL. */
static struct Sym *ast_deadincdec_sym_direct(const struct AstNode *e)
{
    struct Sym *s;
    if (e->a == NULL || e->a->kind != AST_IDENT)
        return NULL;
    s = find_sym(e->a->sval);
    if (s == NULL || s->is_const_value || s->storage == SC_FUNC || s->is_array)
        return NULL;
    if (!sym_can_ix_direct(s) && !is_global_word_sym(s))
        return NULL;
    return s;
}

/* Dead-result ++/-- on a struct member lvalue: AST computes the field
 * address (gen_lvalue_addr) then emit_incdec_addr, which inc/decs in place by
 * 1 for sizes 1/2/4.  Pointers are advanced by 1 byte here, so only elem-size-1
 * pointers (e.g. char*) match; wider element pointers stay deferred. */
static int ast_deadincdec_member_ok(const struct AstNode *e)
{
    int t;
    if (e->a == NULL || e->a->kind != AST_MEMBER)
        return 0;
    if (!ast_member_lvalue_type(e->a, &t))
        return 0;
    if (type_ptr_depth(t) > 0)
        return type_index_elem_size(t) == 1;
    return ast_is_plain_int_type(t) ||
           (type_size(t) == 4 && type_ptr_depth(t) == 0);
}

static int ast_incdec_addr_type_ok(int t)
{
    if (type_ptr_depth(t) > 0)
        return type_index_elem_size(t) == 1;
    return ast_is_plain_int_type(t) || type_size(t) == 4;
}

static int ast_index_lvalue_elem_type(const struct AstNode *n, int *out_type)
{
    struct Sym *s;
    int decayed;
    int elem;

    if (n == NULL || n->kind != AST_INDEX || n->a == NULL)
        return 0;
    if (!ast_index_subscript_supported(n->b))
        return 0;
    if (ast_index_symbol_nd_elem_type(n, &elem) ||
        ast_index_deref_pointer_array_collect(n, &s, NULL, NULL, NULL, &elem) ||
        ast_index_2d_array_elem_type(n, &elem) ||
        ast_index_pointer_expr_elem_type(n, &elem) ||
        ast_index_reversed_pointer_expr_elem_type(n, &elem) ||
        ast_index_pointer_array_elem_type(n, &elem) ||
        ast_index_member_pointer_elem_type(n, &elem)) {
        *out_type = elem;
        return 1;
    }
    if (n->a->kind == AST_IDENT) {
        s = find_sym(n->a->sval);
        if (s == NULL || s->is_const_value || s->storage == SC_FUNC)
            return 0;
        decayed = s->is_array ? type_add_ptr(s->type) : s->type;
    } else if (n->a->kind == AST_MEMBER) {
        if (ast_member_array_field_elem_type(n->a, &elem)) {
            *out_type = elem;
            return 1;
        }
        if (!ast_member_lvalue_type(n->a, &decayed))
            return 0;
    } else if (n->a->kind == AST_INDEX) {
        int no_deref;
        if (!ast_pointer_expr_type(n->a, &decayed, &no_deref))
            return 0;
    } else {
        return 0;
    }
    if (type_ptr_depth(decayed) <= 0)
        return 0;
    *out_type = type_decay_ptr(decayed);
    return 1;
}

static int ast_deadincdec_addr_lvalue_type(const struct AstNode *e, int *out_type)
{
    const struct AstNode *lv;
    struct Sym *s;
    int t;

    if (e == NULL || (e->kind != AST_UNARY && e->kind != AST_POSTFIX))
        return 0;
    if (e->op != TOK_INC && e->op != TOK_DEC)
        return 0;
    lv = e->a;
    if (lv == NULL)
        return 0;
    switch (lv->kind) {
    case AST_IDENT:
        s = find_sym(lv->sval);
        if (s == NULL || s->is_const_value || s->storage == SC_FUNC || s->is_array)
            return 0;
        t = s->type;
        break;
    case AST_MEMBER:
        if (!ast_member_lvalue_type(lv, &t))
            return 0;
        break;
    case AST_UNARY:
        if (lv->op != '*' || !ast_deref_lvalue_type(lv, &t))
            return 0;
        break;
    case AST_INDEX:
        if (!ast_index_lvalue_elem_type(lv, &t))
            return 0;
        break;
    default:
        return 0;
    }
    if (!ast_incdec_addr_type_ok(t))
        return 0;
    *out_type = t;
    return 1;
}

static void gen_deadincdec_addr_lvalue_ast(const struct AstNode *e, int *out_type)
{
    const struct AstNode *lv = e->a;
    struct Sym *s;

    switch (lv->kind) {
    case AST_IDENT:
        s = find_sym(lv->sval);
        emit_load_sym_addr(s);
        *out_type = s->type;
        return;
    case AST_MEMBER:
        gen_member_addr_ast(lv, out_type);
        return;
    case AST_UNARY:
        gen_deref_addr_ast(lv, out_type);
        return;
    case AST_INDEX:
        gen_index_addr_ast(lv, out_type);
        return;
    default:
        fatal("gen_deadincdec_addr_lvalue_ast: unsupported lvalue");
    }
}

static int ast_dead_expr_supported(const struct AstNode *e)
{
    int old_dead;
    int ok;
    if (e == NULL)
        return 0;
    if ((e->kind == AST_UNARY || e->kind == AST_POSTFIX) &&
        (e->op == TOK_INC || e->op == TOK_DEC)) {
        /* Dead-result ++/-- statement: mirror the sym-direct fast path
         * (emit_incdec_sym_direct) for an ix-direct or global-word ident.
         * Other lvalues (members, deref) stay deferred. */
        if (ast_deadincdec_sym_direct(e) != NULL)
            return 1;
        if (ast_deadincdec_member_ok(e))
            return 1;
        if (ast_deadincdec_addr_lvalue_type(e, &ok))
            return 1;
        return 0;
    }
    if (e->kind == AST_CAST && (e->type & 15) == TYPE_VOID)
        return e->a != NULL && ast_gen_supported(e->a);
    if (ast_is_local_self_add_stmt(e))
        return 0;
    /* Evaluate the support gate in the SAME dead-result context the walker will
    * emit under: expression statements set expr_result_dead = 1 before lowering,
     * and ast_gen_supported's AST_ASSIGN case defers the dead-result `+=`/`-=`
     * fast paths only when expr_result_dead is set.  Without this, those shapes
     * (e.g. `x += 5;`) would wrongly pass the gate here and the walker would
    * emit a divergent (longer) sequence instead of the compact one. */
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
    if (e->kind == AST_COMMA)
        return ast_gen_supported(e);
    return e->kind == AST_CALL && ast_gen_supported(e);
}

/* Is the expression-statement node `n` (n->a is the expression) AST-emittable?
 * An expression statement is emitted with dead-result semantics.  The AST path
 * handles the compact top-level inc/dec and simple local self-add shapes
 * directly; global char-array stores and CRC-update byte idioms are excluded by
 * the ordinary expression support gates. */
static int ast_expr_stmt_supported(const struct AstNode *n)
{
    const struct AstNode *e = n->a;
    if (e == NULL)
        return 0;
    if (ast_is_local_self_add_stmt(e))
        return 1;
    return ast_dead_expr_supported(e);
}

/* Is statement node `n` within the AST-emittable subset? */
static int ast_stmt_supported(const struct AstNode *n)
{
    switch (n->kind) {
    case AST_EMPTY:
        return 1;                         /* `;` emits nothing */
    case AST_DECL:
        /* A captured declaration span is always emittable: it delegates to
         * declaration codegen (which rebuilds locals[] / frame
         * offsets exactly as the frame-sizing scan did). */
        return 1;
    case AST_EXPR_STMT:
        return ast_expr_stmt_supported(n);
    case AST_RETURN:
        return ast_return_stmt_supported(n);
    case AST_BREAK:
    case AST_CONTINUE:
        /* A bare jump to the innermost loop/switch exit/continue label. */
        return nflow > 0;
    case AST_GOTO:
        /* Unconditional jump to a named user label. */
        return n->sval != NULL;
    case AST_LABEL:
        /* A user label is emittable when its labeled statement is too. */
        return n->sval != NULL && n->b != NULL && ast_stmt_supported(n->b);
    case AST_CASE:
    case AST_DEFAULT:
        return ast_switch_gate_depth > 0 && n->b != NULL && ast_stmt_supported(n->b);
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
        /* Generic-path condition + emittable body. */
        {
            int old_nflow;
            int ok;
            if (!ast_is_const_nonzero_condition(n->a) && !ast_cond_generic(n->a))
                return 0;
            if (n->b == NULL)
                return 0;
            old_nflow = nflow;
            nflow++;
            ok = ast_stmt_supported(n->b);
            nflow = old_nflow;
            return ok;
        }
    case AST_DOWHILE:
        /* Generic-path condition + emittable body.  A bare `*ptr` condition
         * reaches the generic path here (no deref fast path), so no extra
         * exclusion is needed.  The `do{}while(0)` macro-wrapper idiom keeps
         * labels but emits no condition/back-edge code, so accept that
         * const-zero case too. */
        {
            int old_nflow;
            int ok;
            if (!ast_is_const_zero_condition(n->a) &&
                !ast_is_const_nonzero_condition(n->a) && !ast_cond_generic(n->a))
                return 0;
            if (n->b == NULL)
                return 0;
            old_nflow = nflow;
            nflow++;
            ok = ast_stmt_supported(n->b);
            nflow = old_nflow;
            return ok;
        }
    case AST_FOR: {
        int old_nflow;
        int ok;
        int for_seq;
        int rename_count;
        /* Narrow slice: for ([init] ; [cond] ; [inc]) body.  The builder
         * stores init in a, cond in b, inc in c, and body in d.  A C99 for-init
         * declaration arrives as an AST_DECL span; ast_gen_for_stmt replays it
         * through declaration codegen and for-scope rename
         * machinery.  An expression init excludes the transform-prone constant
         * assignment shape and must have no recorded for-scope renames.
         *
         * This gate mirrors ast_gen_for_stmt's pre-order g_for_seq numbering.
         * For a for-init declaration the loop variable's local slot does not
         * exist yet (codegen creates it only when the declaration is emitted),
         * so we replay the declaration with emission suppressed (scan_mode) to
         * materialise the slot and push its rename, gate the
         * condition/increment/body while they can resolve, then roll back the
         * local table and rename stack.  g_for_seq is intentionally left at the
         * post-order cursor for sibling gates; the top-level AST statement
         * probe restores it before real emission. */
        if (n->d == NULL)
            return 0;
        if (g_for_seq >= MAX_FOR_SCOPES)
            return 0;
        for_seq = g_for_seq++;
        rename_count = g_for_rename_count[for_seq];

        if (n->a != NULL && n->a->kind == AST_DECL) {
            int s_nlocals = nlocals;
            int s_local_size = local_size;
            int s_forren_n = g_forren_n;
            int s_nulabels = nulabels;
            int s_static_seq = g_static_local_seq;
            int s_scope_depth = g_scope_depth;
            int s_has_call = current_function_has_call;
            int s_decl_seq = g_for_decl_seq;
            int s_decl_index = g_for_decl_rename_index;
            int s_decl_recording = g_for_decl_recording;
            int s_scan_mode = scan_mode;
            FILE *s_outf = outf;
            static FILE *sink = NULL;

            ok = 1;
            /* Redirect emission to a throwaway sink so the suppressed replay
             * cannot leak partial output (scan_mode guards most but not every
             * emit path), and set scan_mode so nested AST build/gen and the
             * remaining guarded emits stay quiet. */
            if (sink == NULL)
                sink = fopen("/dev/null", "w");
            if (sink != NULL)
                outf = sink;
            scan_mode = 1;
            g_for_decl_seq = for_seq;
            g_for_decl_rename_index = 0;
            g_for_decl_recording = 0;
            ast_emit_decl_span(n->a);
            g_for_decl_seq = s_decl_seq;
            g_for_decl_rename_index = s_decl_index;
            g_for_decl_recording = s_decl_recording;

            if (n->b != NULL && !ast_cond_generic(n->b)) {
                ok = 0;
            }
            if (ok && n->c != NULL && !ast_dead_expr_supported(n->c)) {
                ok = 0;
            }
            if (ok) {
                old_nflow = nflow;
                nflow++;
                ok = ast_stmt_supported(n->d);
                nflow = old_nflow;
            }
            scan_mode = s_scan_mode;
            outf = s_outf;

            nlocals = s_nlocals;
            local_size = s_local_size;
            g_forren_n = s_forren_n;
            nulabels = s_nulabels;
            g_static_local_seq = s_static_seq;
            g_scope_depth = s_scope_depth;
            current_function_has_call = s_has_call;
            return ok;
        }

        if (rename_count != 0) {
            return 0;
        }
        if (!ast_for_init_expr_supported(n->a)) {
            return 0;
        }
        if (n->b != NULL && !ast_cond_generic(n->b)) {
            return 0;
        }
        if (n->c != NULL && !ast_dead_expr_supported(n->c)) {
            return 0;
        }
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
        if (n->a == NULL || !ast_gen_supported(n->a) ||
            (!ast_value_is_plain_int(n->a) && !ast_value_is_long_word(n->a)))
            return 0;
        if (n->list_len <= 0)
            return 0;
        old_nflow = nflow;
        nflow++;
        ast_switch_gate_depth++;
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
        ast_switch_gate_depth--;
        nflow = old_nflow;
        return ok;
    }
    case AST_COMPOUND: {
        /* A brace block is emittable when every child statement is.  Block
         * declarations are AST_DECL spans (always emittable themselves); but a
         * *later* sibling referencing a block-local name cannot resolve it at
         * gate time because codegen only creates the local when the decl is
         * emitted.  So replay each declaration with emission suppressed
         * (scan_mode + a throwaway outf sink) to materialise its local slots
         * and scope, gate the remaining children while they resolve, then roll
         * back every mutated codegen counter (ast_gen_stmt re-emits the decls
         * for real). */
        int i;
        int ok;
        int has_decl;

        has_decl = 0;
        for (i = 0; i < n->list_len; ++i) {
            if (n->list[i]->kind == AST_DECL) {
                has_decl = 1;
                break;
            }
        }
        if (!has_decl) {
            for (i = 0; i < n->list_len; ++i)
                if (!ast_stmt_supported(n->list[i]))
                    return 0;
            return 1;
        }

        {
            int s_nlocals = nlocals;
            int s_local_size = local_size;
            int s_scope_depth = g_scope_depth;
            int s_forren_n = g_forren_n;
            int s_nulabels = nulabels;
            int s_static_seq = g_static_local_seq;
            int s_has_call = current_function_has_call;
            int s_scan_mode = scan_mode;
            FILE *s_outf = outf;
            static FILE *sink = NULL;

            if (sink == NULL)
                sink = fopen("/dev/null", "w");
            if (sink != NULL)
                outf = sink;
            scan_mode = 1;
            enter_scope();
            ok = 1;
            for (i = 0; i < n->list_len; ++i) {
                struct AstNode *c = n->list[i];
                if (c->kind == AST_DECL) {
                    ast_emit_decl_span(c);
                } else if (!ast_stmt_supported(c)) {
                    ok = 0;
                    break;
                }
            }
            leave_scope();
            scan_mode = s_scan_mode;
            outf = s_outf;

            nlocals = s_nlocals;
            local_size = s_local_size;
            g_scope_depth = s_scope_depth;
            g_forren_n = s_forren_n;
            nulabels = s_nulabels;
            g_static_local_seq = s_static_seq;
            current_function_has_call = s_has_call;
            return ok;
        }
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
 * emitter used by snippet replay, so they match.
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

/* Emit `ident OP const` (gated by ast_is_const_cmp_cond) by calling the shared
 * emitter.  The internal label it allocates lands at the same relative point
 * because the AST if/while/do-while walkers allocate their loop labels up front
 * before emitting the condition. */
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

static void ast_gen_direct_byte_bitand_branch(const struct AstNode *n, int label,
                                             int branch_when_true)
{
    struct Sym *s;
    long mask;

    s = find_sym(n->a->sval);
    mask = n->b->ival & 255;
    fprintf(outf, "\tld a,(ix%+d)\n", s->offset);
    fprintf(outf, "\tand %ld\n", mask);
    if (branch_when_true)
        emit_jp_label("jp nz,", label);
    else
        emit_jp_label("jp z,", label);
}

static void ast_gen_float_cmp_branch(const struct AstNode *n, int label,
                                     int branch_when_true)
{
    ast_gen_expr(n->a);
    if (!type_is_float(g_expr_type))
        emit_convert_int_to_float(g_expr_type);
    emit("\tpush de\n\tpush hl\n");
    ast_gen_expr(n->b);
    if (!type_is_float(g_expr_type))
        emit_convert_int_to_float(g_expr_type);
    emit("\tpush de\n\tpush hl\n");
    emit_float_compare_call(n->op);
    emit_branch_on_bool_hl(label, branch_when_true);
}

static void ast_gen_long_cmp_branch(const struct AstNode *n, int label,
                                    int branch_when_true)
{
    gen_long_cmp_ast(n);
    emit_branch_on_bool_hl(label, branch_when_true);
}

/* Emit the controlling expression of an if/while/do-while as a branch to
 * `label` taken when the condition is true (branch_when_true=1) or false (0).
 * A simple relational comparison uses the direct compare/branch; everything
 * else falls back to the generic value-test (gen_expr + emit_test_expr_nonzero)
 * the streaming generic condition path also uses. */
static void ast_gen_cond_branch(const struct AstNode *n, int label,
                                int branch_when_true)
{
    long cv;
    if (ast_const_condition_fold(n, &cv)) {
        if ((cv != 0) == branch_when_true)
            emit_jp_label("jp", label);
        return;
    }
    if (n != NULL && n->kind == AST_LOGAND && ast_const_condition_fold(n->a, &cv)) {
        if (cv == 0) {
            if (!branch_when_true)
                emit_jp_label("jp", label);
        } else {
            ast_gen_cond_branch(n->b, label, branch_when_true);
        }
        return;
    }
    if (n != NULL && n->kind == AST_LOGOR && ast_const_condition_fold(n->a, &cv)) {
        if (cv != 0) {
            if (branch_when_true)
                emit_jp_label("jp", label);
        } else {
            ast_gen_cond_branch(n->b, label, branch_when_true);
        }
        return;
    }
    if (ast_is_const_cmp_cond(n)) {
        ast_gen_const_cmp_branch(n, label, branch_when_true);
        return;
    }
    if (ast_is_byte_cmp_cond(n)) {
        ast_gen_byte_cmp_branch(n, label, branch_when_true);
        return;
    }
    if (ast_is_direct_byte_bitand_cond(n)) {
        ast_gen_direct_byte_bitand_branch(n, label, branch_when_true);
        return;
    }
    if (ast_global_char_index_cond(n, NULL)) {
        ast_gen_global_char_index_branch(n, label, branch_when_true);
        return;
    }
    if (ast_is_float_cmp_cond(n)) {
        ast_gen_float_cmp_branch(n, label, branch_when_true);
        return;
    }
    if (ast_long_cmp_supported(n)) {
        ast_gen_long_cmp_branch(n, label, branch_when_true);
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
    if (minv < 0 || maxv > 32767)
        return 0;
    if ((maxv - minv) > 255)
        return 0;
    if ((maxv - minv + 1) > ncase * 2)
        return 0;
    minp[0] = minv;
    maxp[0] = maxv;
    return 1;
}

static void ast_switch_collect_stmt(const struct AstNode *n, int *case_vals,
                                    int *ncasep, int *have_defaultp)
{
    int i;

    if (n == NULL || n->kind == AST_SWITCH)
        return;
    if (n->kind == AST_CASE) {
        case_vals[(*ncasep)++] = (int)n->ival;
        ast_switch_collect_stmt(n->b, case_vals, ncasep, have_defaultp);
        return;
    }
    if (n->kind == AST_DEFAULT) {
        *have_defaultp = 1;
        ast_switch_collect_stmt(n->b, case_vals, ncasep, have_defaultp);
        return;
    }
    if (n->kind == AST_COMPOUND) {
        for (i = 0; i < n->list_len; ++i)
            ast_switch_collect_stmt(n->list[i], case_vals, ncasep, have_defaultp);
        return;
    }
    if (n->kind == AST_IF) {
        ast_switch_collect_stmt(n->b, case_vals, ncasep, have_defaultp);
        ast_switch_collect_stmt(n->c, case_vals, ncasep, have_defaultp);
        return;
    }
    if (n->kind == AST_WHILE || n->kind == AST_DOWHILE) {
        ast_switch_collect_stmt(n->b, case_vals, ncasep, have_defaultp);
        return;
    }
    if (n->kind == AST_FOR) {
        ast_switch_collect_stmt(n->d, case_vals, ncasep, have_defaultp);
        return;
    }
    if (n->kind == AST_LABEL)
        ast_switch_collect_stmt(n->b, case_vals, ncasep, have_defaultp);
}

static void ast_switch_collect(const struct AstNode *n, int *case_vals,
                               int *ncasep, int *have_defaultp)
{
    int i;
    int j;
    *ncasep = 0;
    *have_defaultp = 0;
    for (i = 0; i < n->list_len; ++i) {
        const struct AstNode *sec = n->list[i];
        if (sec->kind == AST_CASE)
            case_vals[(*ncasep)++] = (int)sec->ival;
        else if (sec->kind == AST_DEFAULT)
            *have_defaultp = 1;
        for (j = 0; j < sec->list_len; ++j)
            ast_switch_collect_stmt(sec->list[j], case_vals, ncasep, have_defaultp);
    }
}

static void ast_switch_consume_scan_labels_stmt(const struct AstNode *n)
{
    int i;

    if (n == NULL || n->kind == AST_SWITCH)
        return;
    if (n->kind == AST_CASE || n->kind == AST_DEFAULT) {
        (void)new_label();
        ast_switch_consume_scan_labels_stmt(n->b);
        return;
    }
    if (n->kind == AST_COMPOUND) {
        for (i = 0; i < n->list_len; ++i)
            ast_switch_consume_scan_labels_stmt(n->list[i]);
        return;
    }
    if (n->kind == AST_IF) {
        ast_switch_consume_scan_labels_stmt(n->b);
        ast_switch_consume_scan_labels_stmt(n->c);
        return;
    }
    if (n->kind == AST_WHILE || n->kind == AST_DOWHILE) {
        ast_switch_consume_scan_labels_stmt(n->b);
        return;
    }
    if (n->kind == AST_FOR) {
        ast_switch_consume_scan_labels_stmt(n->d);
        return;
    }
    if (n->kind == AST_LABEL)
        ast_switch_consume_scan_labels_stmt(n->b);
}

static void ast_switch_consume_scan_labels(const struct AstNode *n)
{
    int i;
    int j;
    for (i = 0; i < n->list_len; ++i) {
        const struct AstNode *sec = n->list[i];
        (void)new_label();
        for (j = 0; j < sec->list_len; ++j)
            ast_switch_consume_scan_labels_stmt(sec->list[j]);
    }
}

static void ast_switch_assign_labels_stmt(const struct AstNode *n, int *case_vals,
                                          int *case_labs, int ncase,
                                          int *default_labp)
{
    int i;

    if (n == NULL || n->kind == AST_SWITCH)
        return;
    if (n->kind == AST_CASE) {
        i = ast_switch_find_case((int)n->ival, case_vals, ncase);
        if (i >= 0)
            case_labs[i] = new_label();
        ast_switch_assign_labels_stmt(n->b, case_vals, case_labs, ncase, default_labp);
        return;
    }
    if (n->kind == AST_DEFAULT) {
        *default_labp = new_label();
        ast_switch_assign_labels_stmt(n->b, case_vals, case_labs, ncase, default_labp);
        return;
    }
    if (n->kind == AST_COMPOUND) {
        for (i = 0; i < n->list_len; ++i)
            ast_switch_assign_labels_stmt(n->list[i], case_vals, case_labs,
                                          ncase, default_labp);
        return;
    }
    if (n->kind == AST_IF) {
        ast_switch_assign_labels_stmt(n->b, case_vals, case_labs, ncase, default_labp);
        ast_switch_assign_labels_stmt(n->c, case_vals, case_labs, ncase, default_labp);
        return;
    }
    if (n->kind == AST_WHILE || n->kind == AST_DOWHILE) {
        ast_switch_assign_labels_stmt(n->b, case_vals, case_labs, ncase, default_labp);
        return;
    }
    if (n->kind == AST_FOR) {
        ast_switch_assign_labels_stmt(n->d, case_vals, case_labs, ncase, default_labp);
        return;
    }
    if (n->kind == AST_LABEL)
        ast_switch_assign_labels_stmt(n->b, case_vals, case_labs, ncase, default_labp);
}

static void ast_switch_assign_labels(const struct AstNode *n, int *case_vals,
                                     int *case_labs, int ncase,
                                     int *default_labp)
{
    int i;
    int j;
    for (i = 0; i < n->list_len; ++i) {
        const struct AstNode *sec = n->list[i];
        if (sec->kind == AST_CASE) {
            int ci = ast_switch_find_case((int)sec->ival, case_vals, ncase);
            if (ci >= 0)
                case_labs[ci] = new_label();
        } else {
            *default_labp = new_label();
        }
        for (j = 0; j < sec->list_len; ++j)
            ast_switch_assign_labels_stmt(sec->list[j], case_vals, case_labs,
                                          ncase, default_labp);
    }
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

    /* Preserve the historical label allocation order: consume scan labels for
     * every top-level case/default before expression code can allocate labels.
     * The scan allocates in source order, so `default:` before later cases
     * matters. */
    ast_switch_consume_scan_labels(n);

    default_lab = -1;
    if (!table_ok)
        ast_switch_assign_labels(n, case_vals, case_labs, ncase, &default_lab);

    ast_gen_expr(n->a);

    if (table_ok)
        ast_switch_assign_labels(n, case_vals, case_labs, ncase, &default_lab);

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

    if (ast_sw_depth < AST_MAX_SW_NEST) {
        ast_sw_ctx[ast_sw_depth].vals    = case_vals;
        ast_sw_ctx[ast_sw_depth].labs    = case_labs;
        ast_sw_ctx[ast_sw_depth].n       = ncase;
        ast_sw_ctx[ast_sw_depth].def_lab = default_lab;
        ast_sw_depth++;
    }

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

    if (ast_sw_depth > 0)
        ast_sw_depth--;
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
    int rename_count;

    for_seq = g_for_seq++;
    if (for_seq >= MAX_FOR_SCOPES)
        fatal("too many for statements");
    rename_count = g_for_rename_count[for_seq];

    ltop = new_label();
    linc = new_label();
    lend = new_label();

    if (n->a != NULL && n->a->kind == AST_DECL) {
        /* C99 for-init declaration: drive declaration codegen
         * through the captured span with the for-scope rename context set, so
         * the loop variable is renamed to its unique internal slot and pushed
         * onto the active rename stack for the body/cond/inc to resolve. */
        int old_for_decl_seq = g_for_decl_seq;
        int old_for_decl_rename_index = g_for_decl_rename_index;
        int old_for_decl_recording = g_for_decl_recording;
        g_for_decl_seq = for_seq;
        g_for_decl_rename_index = 0;
        g_for_decl_recording = 0;
        ast_emit_decl_span(n->a);
        if (g_for_decl_rename_index != rename_count)
            fatal("for-init scope mismatch");
        g_for_decl_seq = old_for_decl_seq;
        g_for_decl_rename_index = old_for_decl_rename_index;
        g_for_decl_recording = old_for_decl_recording;
    } else {
        if (rename_count != 0)
            fatal("unsupported AST for-init scope");
        if (n->a != NULL)
            ast_gen_expr(n->a);
    }

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
        if ((n->c->kind == AST_UNARY || n->c->kind == AST_POSTFIX) &&
            (n->c->op == TOK_INC || n->c->op == TOK_DEC)) {
            struct Sym *s = ast_deadincdec_sym_direct(n->c);
            if (s != NULL) {
                emit_incdec_sym_direct(s, n->c->op);
            } else {
                int vt;
                gen_deadincdec_addr_lvalue_ast(n->c, &vt);
                emit_incdec_addr(vt, n->c->op);
            }
        } else {
            ast_gen_expr(n->c);
        }
        expr_result_dead = old_dead;
    }
    emit_jp_label("jp", ltop);
    emit_label(lend);

    /* Close the for-init scope so source names resolve to outer symbols. */
    while (rename_count > 0) {
        pop_for_rename();
        rename_count--;
    }
}

/* Emit statement node `n` (gated by ast_stmt_supported). */
static void ast_gen_stmt(const struct AstNode *n)
{
    switch (n->kind) {
    case AST_EMPTY:
        break;                            /* empty statement: emit nothing */
    case AST_DECL:
        ast_emit_decl_span(n);            /* declaration codegen replay */
        break;
    case AST_EXPR_STMT: {
        /* Expression statement results are dead, so emit with
         * expr_result_dead set. */
        int old_dead = expr_result_dead;
        expr_result_dead = 1;
        if ((n->a->kind == AST_UNARY || n->a->kind == AST_POSTFIX) &&
            (n->a->op == TOK_INC || n->a->op == TOK_DEC)) {
            struct Sym *s = ast_deadincdec_sym_direct(n->a);
            if (s != NULL) {
                emit_incdec_sym_direct(s, n->a->op);
            } else {
                int vt;
                gen_deadincdec_addr_lvalue_ast(n->a, &vt);
                emit_incdec_addr(vt, n->a->op);
            }
        } else if (ast_is_local_self_add_stmt(n->a)) {
            ast_emit_local_self_add_stmt(n->a);
        } else {
            ast_gen_expr(n->a);
        }
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
    case AST_CASE: {
        long cv;
        int i;
        int lab;
        struct AstSwCtx *sw;

        if (ast_sw_depth <= 0)
            fatal("case label outside switch");
        cv = n->ival;
        lab = -1;
        sw = &ast_sw_ctx[ast_sw_depth - 1];
        for (i = 0; i < sw->n; ++i) {
            if ((sw->vals[i] & 0xffff) == ((int)cv & 0xffff)) {
                lab = sw->labs[i];
                break;
            }
        }
        if (lab >= 0)
            emit_label(lab);
        ast_gen_stmt(n->b);
        break;
    }
    case AST_DEFAULT: {
        struct AstSwCtx *sw;

        if (ast_sw_depth <= 0)
            fatal("default label outside switch");
        sw = &ast_sw_ctx[ast_sw_depth - 1];
        if (sw->def_lab >= 0)
            emit_label(sw->def_lab);
        ast_gen_stmt(n->b);
        break;
    }
    case AST_IF: {
        /* Generic if/else condition shape, including the label allocation
         * order (lelse, lend before the condition). */
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
        /* Generic while shape: ltop, lend allocated up front;
         * label(ltop); test condition -> lend; body inside nflow scope;
         * jp ltop; label(lend). */
        int ltop = new_label();
        int lend = new_label();
        emit_label(ltop);
        if (ast_is_const_nonzero_condition(n->a)) {
            ast_gen_expr(n->a);
            emit_test_expr_nonzero(g_expr_type, lend, 0);
        } else {
            ast_gen_cond_branch(n->a, lend, 0);
        }
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
        /* Generic do-while shape: ltop, lcont, lend
         * allocated up front; label(ltop); body inside nflow scope (break->
         * lend, continue->lcont); label(lcont); test condition -> ltop when
         * TRUE (branch sense 1); label(lend).  For `while(0)`, streaming keeps
         * the labels but omits the test/back-edge. */
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
        if (ast_is_const_nonzero_condition(n->a))
            emit_jp_label("jp", ltop);
        else if (!ast_is_const_zero_condition(n->a))
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
        /* Mirror gen_compound: enter_scope(); emit each child (statements and
         * AST_DECL declaration spans, the latter re-running the streaming
         * declaration codegen); leave_scope().  enter/leave emit nothing. */
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
    int sv_for_seq;
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
    sv_for_seq = g_for_seq;
    sv_tok = tok;

    n = ast_build_stmt(&g_ast_arena);

    if (n != NULL && ast_stmt_supported(n)) {
        g_for_seq = sv_for_seq;
        ast_gen_stmt(n);
        if (g_ast_gen_enabled == 2)
            fprintf(stderr, "; AST-emit %s\n", ast_kind_name(n->kind));
        ast_arena_reset(&g_ast_arena);
        return 1;
    }

    if (report) {
        if (n == NULL) {
                fprintf(stderr, "; AST-unsupported stmt build token=%d text='%s' line=%d\n",
                    sv_tok.kind, sv_tok.text, sv_tok_line);
        } else {
            fprintf(stderr, "; AST-unsupported stmt gate kind=%s line=%d\n",
                    ast_kind_name(n->kind), sv_tok_line);
        }
    }

    posi = sv_pos;
    tok_start_pos = sv_tok_start;
    line_no = sv_line;
    tok_line = sv_tok_line;
    tok = sv_tok;
    g_for_seq = sv_for_seq;
    ast_arena_reset(&g_ast_arena);
    return 0;
}
