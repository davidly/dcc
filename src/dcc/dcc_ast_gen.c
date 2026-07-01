/*
 * dcc_ast_gen.c - AST-driven code generation (classifiers / type & lvalue
 * resolvers).
 *
 * The function-local AST is the codegen path.  This walker produces Z80
 * assembly by calling the shared low-level emit helpers; unsupported AST shapes
 * are compiler errors.
 *
 * Expression and statement lowering emits tight, peephole-friendly byte
 * sequences that the dccpeep patterns and regression baselines depend on.
 *
 * The AST codegen module is split across several translation units that share
 * prototypes via dcc_ast_gen_internal.h:
 *   - dcc_ast_gen.c         classifiers / type & lvalue resolvers (this file)
 *   - dcc_ast_gen_support.c ast_gen_supported dispatch, call/struct gates, folds
 *   - dcc_ast_gen_expr.c    expression emitters (ast_gen_expr)
 *   - dcc_ast_gen_cond.c    statement-support gates, comparison/branch emitters
 *   - dcc_ast_gen_stmt.c    switch/for/statement emitters, ast_try_emit_statement
 */
#include "dcc.h"
#include "dcc_ast.h"
#include <string.h>
#include "dcc_ast_gen_internal.h"

int ident_supported(const char *name)
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
int is_cmp_op(int op)
{
    return op == TOK_EQ || op == TOK_NE || op == '<' || op == '>' ||
           op == TOK_LE || op == TOK_GE;
}

/* The two shift operators, which use a distinct (ld b,l + shift-loop) emit
 * shape and whose result type is the promoted left operand, not a common
 * arithmetic type. */
int is_shift_op(int op)
{
    return op == TOK_SHL || op == TOK_SHR;
}

int is_float_arith_op(int op)
{
    return op == '+' || op == '-' || op == '*' || op == '/';
}

/* Binary operators whose plain-int (16-bit) emission is the uniform
 * "push hl / <rhs> / ex de,hl / pop hl / gen_binop_typed" sequence.  Shifts
 * use a different (ld b,l + shift-loop) shape and &&/|| are short-circuit, so
 * both use their own AST lowering. */
int is_supported_binary_op(int op)
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
int ast_is_plain_int_type(int t)
{
    if (t & (TYPE_PTR | TYPE_PTR2 | TYPE_STRUCT))
        return 0;
    return (t & 15) == TYPE_CHAR || (t & 15) == TYPE_INT;
}


/* An enum constant or a const-folded scalar: the AST path may fold a
 * whole-constant expression built from these, so they count as constants. */
int ast_ident_is_const(const char *name)
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

/* Switch codegen state (type/macro/externs declared in dcc_ast_gen_internal.h);
 * defined here, shared across the split AST codegen translation units. */
int ast_switch_gate_depth;
struct AstSwCtx ast_sw_ctx[AST_MAX_SW_NEST];
int ast_sw_depth;

/* Forward declaration: resolve 2-D array/field-array element type. */

/* Forward declaration: a subscript expression can be emitted by index-only code. */

/* Forward declaration: a struct field read can be a plain-int value operand. */

/* Forward declaration: resolve scalar field lvalue type. */

/* Forward declaration: resolve the struct object/pointer type for a member base. */

/* Forward declaration: resolve element type for pointer-valued array fields. */

/* Forward declaration: a pointer deref read can be a plain-int value operand. */

/* Forward declaration: a pointer-valued expression supported only as the
 * operand of a dereference lvalue. */

/* Forward declaration: expression yields a 16-bit pointer word in HL. */

/* Forward declaration: supported pointer equality/inequality comparison. */

/* Forward declaration: supported long comparison yielding int 0/1. */

/* Forward declaration: supported direct struct-return call assignment. */

/* Forward declaration: a prefix ++/-- of a plain-int lvalue is a value operand. */

/* Forward declaration: a postfix ++/-- of a plain-int lvalue is a value operand. */

/* Forward declaration: emit a subscript element ADDRESS into HL (the address
 * machine shared by the value read and the lvalue store). */

/* Forward declaration: emit a struct field ADDRESS into HL (the address machine
 * shared by the value read and the lvalue store). */

/* Forward declaration: gate for va_start/va_end builtin calls. */

/* Forward declaration: emit a `*ident` target ADDRESS into HL for an lvalue
 * store (differs from the deref value-read path). */

/* Forward declaration: emit a pointer-valued expression into HL for a
 * dereference lvalue address. */

/* Forward declaration: emit pointer equality/inequality into HL as 0/1. */

int ast_field_array_index_stride(int base_size, int dim_count,
                                        const int *dims, int index_count)
{
    int stride;
    int di;
    stride = base_size;
    for (di = index_count + 1; di < dim_count; ++di)
        stride *= dims[di];
    return stride;
}

/* True for the emit_mul_hl_const fast-path multipliers (0,1,3,5,10,pow2).
 * Applied only for a non-long multiply whose literal is the RHS. */
int ast_mul_const_value_ok(long v)
{
    long m = v & 0xffffL;
    return m == 0 || m == 1 || m == 3 || m == 5 || m == 10 ||
           int_log2_pow2((int)m) >= 0;
}

/* Conservative: returns 1 only when the node is CERTAIN to evaluate to a plain
 * 16-bit int value.  Anything uncertain returns 0 (not supported here). */
int ast_value_is_plain_int(const struct AstNode *n)
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


/* A unary chain bottoming out in a numeric literal is a compile-time constant.
 * The AST walker folds these so the compact immediate form is preserved. */
int ast_node_is_const(const struct AstNode *n)
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
        /* The constant folder evaluates && / || too (dcc_fold.c), so a fully
         * constant logical expression folds to a single immediate; decline to
         * preserve it. */
        return ast_node_is_const(n->a) && ast_node_is_const(n->b);
    default:
        return 0;
    }
}

/* A subscript read `base[index]` that codegen leaves in HL as a plain integer
 * value (the non-const, single-index, non-field-array case).  Conservative:
 * only a bare identifier base that is a 1-D plain-int array or a plain int/char
 * pointer (element size 1 or 2), indexed by a supported plain-int expression.
 * emit_load_from_hl sign/zero-extends a 1-byte element into the full HL, so a
 * char element still yields a valid 16-bit int in HL.  Integer-literal indexes
 * are folded into the address; other constant expressions, multi-dimensional or
 * field arrays, the n[ptr] commutative case and any wider/non-int element are
 * not handled here. */

/* Shared collector battery for the index read gates: recognises the composite
 * index lvalue shapes (N-dim symbol, deref pointer-array, 2-D array, pointer
 * expression, reversed pointer expression) and yields the element type.
 * Returns 1 and sets *out_elem on a match.  The bare-identifier and struct
 * member tails are intentionally left to each caller. */
int ast_index_composite_elem_type(const struct AstNode *n, int *out_elem)
{
    struct Sym *s;
    if (ast_index_symbol_nd_elem_type(n, out_elem))
        return 1;
    if (ast_index_deref_pointer_array_collect(n, &s, NULL, NULL, NULL, out_elem))
        return 1;
    if (ast_index_2d_array_elem_type(n, out_elem))
        return 1;
    if (ast_index_pointer_expr_elem_type(n, out_elem))
        return 1;
    if (ast_index_reversed_pointer_expr_elem_type(n, out_elem))
        return 1;
    return 0;
}

int ast_index_plain_int_read(const struct AstNode *n)
{
    struct Sym *s;
    int decayed;
    int elem;
    int esz;

    if (n == NULL || n->kind != AST_INDEX)
        return 0;
    if (ast_index_composite_elem_type(n, &elem))
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
    /* Index: either a literal constant (folded into the address) or a
     * supported non-constant plain-int expression. */
    if (n->b == NULL)
        return 0;
    return ast_index_subscript_supported(n->b);
}

int ast_index_long_read(const struct AstNode *n)
{
    struct Sym *s;
    int decayed;
    int elem;

    if (n == NULL || n->kind != AST_INDEX || n->a == NULL)
        return 0;
    if (ast_index_composite_elem_type(n, &elem))
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

int ast_index_float_read(const struct AstNode *n)
{
    struct Sym *s;
    int decayed;
    int elem;

    if (n == NULL || n->kind != AST_INDEX || n->a == NULL)
        return 0;
    if (ast_index_composite_elem_type(n, &elem))
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


int ast_index_array_row_ptr_type(const struct AstNode *n, int *out_type)
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

int ast_index_struct_object_type(const struct AstNode *n, int *out_type)
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

int ast_index_struct_object_subscript_supported(const struct AstNode *idx)
{
    if (idx == NULL)
        return 0;
    if (ast_value_is_plain_int(idx))
        return 1;
    return ast_index_subscript_supported(idx);
}

int ast_index_subscript_binary_literal(const struct AstNode *idx)
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

int ast_index_subscript_supported(const struct AstNode *idx)
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
    /* A constant unary applied to a plain-int literal (e.g. `p[-1]`) is not a
     * bare literal but folds to a plain int; the emitter evaluates it through
     * gen_index_subscript_expr_ast (correct two's-complement scaling), so allow
     * it before the generic const rejection below. */
    if (idx->kind == AST_UNARY &&
        (idx->op == '-' || idx->op == '+' || idx->op == '~') &&
        idx->a != NULL && idx->a->kind == AST_INT_LIT &&
        ast_value_is_plain_int(idx->a))
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

int ast_index_2d_addressable_addr(const struct AstNode *n)
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

int ast_index_symbol_nd_collect(const struct AstNode *n, struct Sym **out_sym,
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

int ast_index_symbol_nd_elem_type(const struct AstNode *n, int *out_type)
{
    const struct AstNode *idxs[8];
    struct Sym *s;
    int count;

    if (!ast_index_symbol_nd_collect(n, &s, idxs, &count))
        return 0;
    *out_type = s->is_array ? s->type : type_decay_ptr(s->type);
    return type_size(*out_type) > 0;
}

int ast_index_symbol_nd_addressable_addr(const struct AstNode *n)
{
    int elem;
    return ast_index_symbol_nd_elem_type(n, &elem);
}

int ast_index_deref_pointer_array_collect(const struct AstNode *n,
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

int ast_index_member_array_nd_collect(const struct AstNode *n,
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

int ast_index_2d_array_elem_type(const struct AstNode *n, int *out_type)
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

int ast_index_addressable_addr(const struct AstNode *n)
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

int ast_index_pointer_expr_elem_type(const struct AstNode *n, int *out_type)
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

int ast_index_reversed_pointer_expr_elem_type(const struct AstNode *n, int *out_type)
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

int ast_index_pointer_array_elem_type(const struct AstNode *n, int *out_type)
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

int ast_index_member_pointer_elem_type(const struct AstNode *n, int *out_type)
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
int ast_index_scalar_pointer_elem_type(const struct AstNode *n, int *out_type)
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

int ast_pointer_expr_type(const struct AstNode *n, int *out_type,
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

int ast_deref_lvalue_plain_int_type(const struct AstNode *n, int *out_type)
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

int ast_deref_lvalue_type(const struct AstNode *n, int *out_type)
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

int ast_member_base_type(const struct AstNode *n, int *out_type)
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

int ast_member_array_field_elem_type(const struct AstNode *n, int *out_type)
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

int ast_member_plain_array_field_elem_type(const struct AstNode *n, int *out_type)
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

int ast_member_pointer_array_field_elem_type(const struct AstNode *n, int *out_type)
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
 * 16-bit int value, via the identifier-rooted field machine for a SINGLE field
 * access.  Conservative: a bare-identifier base that is a struct object (for
 * `.`) or a depth-1 pointer to a struct (for `->`), and a field that is a plain
 * int/char SCALAR - non-array, non-bitfield.  emit_load_from_hl sign/zero-
 * extends a 1-byte field into the full HL, so a char field still yields a valid
 * 16-bit int.  Field arrays, bitfields, nested/chained accesses, and
 * wider/non-int field types are not handled here. */
int ast_member_plain_int_read(const struct AstNode *n)
{
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
int ast_member_bitfield_read(const struct AstNode *n)
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

int ast_member_bitfield_lvalue_type(const struct AstNode *n, int *out_type)
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


/* A long-typed struct field read `s.f` / `p->f` is a 32-bit value operand. */
int ast_member_long_read(const struct AstNode *n)
{
    int t;
    if (n == NULL || n->kind != AST_MEMBER)
        return 0;
    if (!ast_member_lvalue_type(n, &t))
        return 0;
    return type_is_long(t);
}

int ast_member_float_read(const struct AstNode *n)
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
int ast_member_pointer_read(const struct AstNode *n)
{
    int t;
    if (n == NULL || n->kind != AST_MEMBER)
        return 0;
    if (!ast_member_lvalue_type(n, &t))
        return 0;
    return type_ptr_depth(t) > 0 && type_size(t) == 2;
}

int ast_member_lvalue_type(const struct AstNode *n, int *out_type)
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
    fd = find_field_def(sid, n->sval);
    if (fd == NULL || fd->is_array || fd->bit_width > 0)
        return 0;
    *out_type = fd->type;
    return 1;
}

/* A pointer deref read `*p` that codegen leaves in HL as a plain int value.
 * Handled only for `*` applied to a BARE identifier NOT followed by [ . -> (
 * or ++ / -- - which is exactly an AST_UNARY '*' whose operand is a bare
 * AST_IDENT (any trailing postfix would reparent the operand into an
 * INDEX/MEMBER/CALL/POSTFIX node).  Restrict to a single-level pointer to a
 * plain int/char element (size 1 or 2) so the load is the simple
 * emit_load_from_hl(base).  Function pointers, arrays, const/enum symbols,
 * wider/non-int and void elements are not handled here. */
int ast_deref_plain_int_read(const struct AstNode *n)
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

int ast_va_arg_deref_type(const struct AstNode *n, int *out_type)
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

void gen_va_arg_deref_ast(const struct AstNode *n, int val_type)
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

int ast_long_va_arg_self_assign_supported(const struct AstNode *n,
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

void gen_long_va_arg_self_assign_ast(const struct AstNode *n)
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

int ast_deref_pointer_word_read(const struct AstNode *n)
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

int ast_deref_long_read(const struct AstNode *n)
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

int ast_deref_float_read(const struct AstNode *n)
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
int ast_preincdec_plain_int(const struct AstNode *n)
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
        if (type_is_long(val_type))
            return 1;
        if (!ast_is_plain_int_type(val_type))
            return 0;
        sz = type_size(val_type);
        return sz == 1 || sz == 2;
    }
    if (n->a->kind == AST_MEMBER) {
        if (!ast_member_lvalue_type(n->a, &val_type))
            return 0;
        if (type_is_long(val_type))
            return 1;
        if (!ast_is_plain_int_type(val_type))
            return 0;
        sz = type_size(val_type);
        return sz == 1 || sz == 2;
    }
    if (n->a->kind == AST_UNARY && n->a->op == '*') {
        if (!ast_deref_lvalue_type(n->a, &val_type))
            return 0;
        if (type_is_long(val_type))
            return 1;
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
    if (type_is_long(s->type))
        return 1;
    if (!ast_is_plain_int_type(s->type))
        return 0;
    sz = type_size(s->type);
    if (sz != 1 && sz != 2)
        return 0;
    return 1;
}

/* A postfix `lv++` / `lv--` on a bare plain-int identifier, struct member, or
 * dereferenced pointer expression.
 * Bare identifiers use the try_emit_post_update_sym_direct fast path
 * deterministically (it returns 1 for any non-array, non-long, non-float scalar
 * of size <= 2 that is IX-direct or a global word).  Member lvalues compute the
 * field address then use gen_post_update_from_addr; dereferenced pointer
 * lvalues use the same address+update tail.  Restrict to plain int/char (size 1
 * or 2, not a pointer) so the non-pointer inc/dec branch applies. */
int ast_postfix_plain_int(const struct AstNode *n)
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

int ast_address_of_supported(const struct AstNode *n)
{
    int elem;
    return ast_address_of_value_type(n, &elem);
}

int ast_address_of_value_type(const struct AstNode *n, int *out_type)
{
    struct Sym *s;
    int elem;

    if (n == NULL)
        return 0;
    if (n->kind == AST_IDENT) {
        s = find_sym(n->sval);
        if (s == NULL || s->is_const_value)
            return 0;
        if (s->storage == SC_FUNC) {
            if (out_type)
                *out_type = s->type;
            return 1;
        }
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
    if (n->kind == AST_UNARY && n->op == '*') {
        /* &*X collapses to the pointer value X. */
        if (!ast_deref_lvalue_type(n, &elem))
            return 0;
        if (out_type)
            *out_type = elem;
        return 1;
    }
    if (n->kind == AST_COMPOUND_LITERAL) {
        if (out_type)
            *out_type = n->type;
        return 1;
    }
    return 0;
}


int ast_numeric_value_supported(const struct AstNode *n)
{
    return ast_value_is_plain_int(n) || ast_value_is_long_word(n) ||
           ast_value_is_float_word(n);
}

int ast_cond_numeric_supported(const struct AstNode *n)
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

int ast_cond_result_is_float(const struct AstNode *n)
{
    return ast_cond_numeric_supported(n) &&
           (ast_value_is_float_word(n->b) || ast_value_is_float_word(n->c));
}

int ast_cond_result_is_long(const struct AstNode *n)
{
    return ast_cond_numeric_supported(n) && !ast_cond_result_is_float(n) &&
           (ast_value_is_long_word(n->b) || ast_value_is_long_word(n->c));
}

int ast_void_expr_supported(const struct AstNode *n)
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

int ast_cond_void_supported(const struct AstNode *n)
{
    if (n == NULL || n->kind != AST_COND)
        return 0;
    if (!ast_gen_supported(n->a) ||
        (!ast_value_is_plain_int(n->a) && !ast_value_is_float_word(n->a) &&
         !ast_value_is_pointer_word(n->a)))
        return 0;
    return ast_void_expr_supported(n->b) && ast_void_expr_supported(n->c);
}

int ast_index_cmp_cond_supported(const struct AstNode *n)
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

/* True when a value may be a `&&` / `||` operand: the short-circuit emit only
 * needs the operand evaluated and tested for nonzero, so any supported scalar
 * value works - plain ints, long words, comparisons (already plain int),
 * pointer-word values, floats, and nested logicals.  Comparisons are accepted
 * even when they carry a literal operand (the test-and-branch shape matches
 * without the binary value gate's literal restriction). */
int ast_logical_operand_ok(const struct AstNode *n)
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

int ast_null_pointer_const(const struct AstNode *n)
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

int ast_pointer_cmp_operand_ok(const struct AstNode *n)
{
    int ptr_type;
    int no_deref;
    if (ast_pointer_expr_type(n, &ptr_type, &no_deref))
        return 1;
    return ast_null_pointer_const(n);
}

int ast_pointer_cmp_supported(const struct AstNode *n)
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

int ast_pointer_diff_supported(const struct AstNode *n)
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

int ast_long_cmp_supported(const struct AstNode *n)
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

int ast_long_arith_supported(const struct AstNode *n)
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

int ast_mixed_long_rhs_arith_supported(const struct AstNode *n)
{
    if (n == NULL || n->kind != AST_BINARY)
        return 0;
    if (!is_supported_binary_op(n->op) || is_cmp_op(n->op))
        return 0;
    return ast_value_is_plain_int(n->a) && ast_value_is_long_word(n->b);
}

int ast_const_plain_int_binary_supported(const struct AstNode *n)
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

int ast_struct_return_call_assign_supported(int lhs_type,
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

int ast_struct_deref_copy_assign_supported(const struct AstNode *n)
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

int ast_struct_member_copy_assign_supported(const struct AstNode *n)
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

int ast_struct_addr_expr_supported(const struct AstNode *n, int *out_type)
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

int ast_struct_copy_assign_supported(const struct AstNode *n)
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

int ast_is_const_zero_condition(const struct AstNode *n)
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

int ast_is_const_nonzero_condition(const struct AstNode *n)
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
