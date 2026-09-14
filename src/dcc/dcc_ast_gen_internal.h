/**
 * @file dcc_ast_gen_internal.h
 * @brief Declares the private contract shared by split AST helper modules.
 *
 * @par Role
 * Exposes cross-file type/lvalue classifiers, support gates, constant folds,
 * condition analysis, retained expression helpers, and shared statement state
 * used by dcc_ast_gen*.c and dcc_ast_stmt_meta.c.
 *
 * @par Module split
 * - dcc_ast_gen.c: type, value, lvalue, pointer, member, and index resolution.
 * - dcc_ast_gen_support.c: support dispatch, call gates, folds, and proofs.
 * - dcc_ast_gen_expr.c: initializer/inline handling and expression helpers.
 * - dcc_ast_gen_cond.c: statement/condition gates and branch-shape helpers.
 * - dcc_ast_stmt_meta.c: statement capture, sizing, and control metadata.
 *
 * @par Boundary
 * Do not include this header outside the AST helper module. Public AST data
 * and entry points belong in dcc_ast.h; production body emission belongs to
 * MIR.
 */
#ifndef DCC_AST_GEN_INTERNAL_H
#define DCC_AST_GEN_INTERNAL_H

#include "dcc.h"
#include "dcc_ast.h"

/* Switch support-gate nesting. */
extern int ast_switch_gate_depth;

/* Maximum length of a pointer-to-array dereference chain
 * (*(*(...(p + i0)...) + iN)).  A chain of N layers indexes a pointer whose
 * element has N-1 array dimensions; the compiler caps array rank at
 * MAX_ARRAY_DIMS, so the longest possible chain is MAX_ARRAY_DIMS + 1 layers. */
#define DCC_MAX_DEREF_CHAIN MAX_INDEX_DEPTH


int ident_supported(const char *name);
int is_cmp_op(int op);
int is_shift_op(int op);
int is_float_arith_op(int op);
int is_supported_binary_op(int op);
int ast_is_plain_int_type(int t);
int ast_ident_is_const(const char *name);
int ast_value_is_plain_int(const struct AstNode *n);
int ast_node_is_const(const struct AstNode *n);
int ast_index_composite_elem_type(const struct AstNode *n, int *out_elem);
int ast_index_plain_int_read(const struct AstNode *n);
int ast_index_long_read(const struct AstNode *n);
int ast_index_float_read(const struct AstNode *n);
int ast_index_array_row_ptr_type(const struct AstNode *n, int *out_type);
int ast_index_struct_object_type(const struct AstNode *n, int *out_type);
int ast_index_struct_object_subscript_supported(const struct AstNode *idx);
int ast_index_subscript_binary_literal(const struct AstNode *idx);
int ast_index_subscript_supported(const struct AstNode *idx);
int ast_index_2d_addressable_addr(const struct AstNode *n);
int ast_index_symbol_nd_collect(const struct AstNode *n, struct Sym **out_sym,
                                       const struct AstNode **idxs, int *out_count);
int ast_index_symbol_nd_elem_type(const struct AstNode *n, int *out_type);
int ast_index_symbol_nd_addressable_addr(const struct AstNode *n);
int ast_index_deref_pointer_array_collect(const struct AstNode *n,
                                                 struct Sym **out_sym,
                                                 const struct AstNode **out_base,
                                                 const struct AstNode **idxs,
                                                 int *out_count,
                                                 int *out_type);
int ast_deref_pointer_array_chain_collect(const struct AstNode *n,
                                                 struct Sym **out_sym,
                                                 const struct AstNode **out_base,
                                                 const struct AstNode **idxs,
                                                 int *out_count,
                                                 int *out_type);
int ast_deref_pointer_array_decay(const struct AstNode *n, int *out_type,
                                  int *out_stride);
int ast_index_member_array_nd_collect(const struct AstNode *n,
                                             const struct AstNode **out_member,
                                             const struct AstNode **idxs,
                                             int *out_count,
                                             int *out_type);
int ast_index_2d_array_elem_type(const struct AstNode *n, int *out_type);
int ast_index_addressable_addr(const struct AstNode *n);
int ast_index_pointer_expr_elem_type(const struct AstNode *n, int *out_type);
int ast_index_reversed_pointer_expr_elem_type(const struct AstNode *n, int *out_type);
int ast_index_pointer_array_elem_type(const struct AstNode *n, int *out_type);
int ast_index_member_pointer_elem_type(const struct AstNode *n, int *out_type);
int ast_index_scalar_pointer_elem_type(const struct AstNode *n, int *out_type);
int ast_pointer_expr_type(const struct AstNode *n, int *out_type,
                                 int *out_no_deref);
int ast_deref_lvalue_plain_int_type(const struct AstNode *n, int *out_type);
int ast_deref_lvalue_type(const struct AstNode *n, int *out_type);
int ast_member_base_type(const struct AstNode *n, int *out_type);
int ast_member_array_field_elem_type(const struct AstNode *n, int *out_type);
int ast_member_plain_array_field_elem_type(const struct AstNode *n, int *out_type);
int ast_member_pointer_array_field_elem_type(const struct AstNode *n, int *out_type);
int ast_member_plain_int_read(const struct AstNode *n);
int ast_member_bitfield_read(const struct AstNode *n);
int ast_member_bitfield_lvalue_type(const struct AstNode *n, int *out_type);
int ast_member_long_read(const struct AstNode *n);
int ast_member_float_read(const struct AstNode *n);
int ast_member_pointer_read(const struct AstNode *n);
int ast_member_lvalue_type(const struct AstNode *n, int *out_type);
struct FieldDef *ast_unique_field_by_name(const char *name);
int ast_deref_plain_int_read(const struct AstNode *n);
int ast_va_arg_deref_type(const struct AstNode *n, int *out_type);
int ast_long_va_arg_self_assign_supported(const struct AstNode *n,
                                                 const struct AstNode **out_va);
int ast_deref_pointer_word_read(const struct AstNode *n);
int ast_deref_long_read(const struct AstNode *n);
int ast_deref_float_read(const struct AstNode *n);
int ast_preincdec_plain_int(const struct AstNode *n);
int ast_preincdec_pointer_word(const struct AstNode *n);
int ast_postfix_plain_int(const struct AstNode *n);
int ast_address_of_supported(const struct AstNode *n);
int ast_address_of_value_type(const struct AstNode *n, int *out_type);
int ast_numeric_value_supported(const struct AstNode *n);
int ast_cond_numeric_supported(const struct AstNode *n);
int ast_cond_result_is_float(const struct AstNode *n);
int ast_cond_result_is_long(const struct AstNode *n);
int ast_void_expr_supported(const struct AstNode *n);
int ast_cond_void_supported(const struct AstNode *n);
int ast_index_cmp_cond_supported(const struct AstNode *n);
int ast_logical_operand_ok(const struct AstNode *n);
int ast_null_pointer_const(const struct AstNode *n);
int ast_pointer_cmp_operand_ok(const struct AstNode *n);
int ast_pointer_cmp_supported(const struct AstNode *n);
int ast_pointer_diff_supported(const struct AstNode *n);
int ast_long_cmp_supported(const struct AstNode *n);
int ast_long_arith_supported(const struct AstNode *n);
int ast_mixed_long_rhs_arith_supported(const struct AstNode *n);
int ast_const_plain_int_binary_supported(const struct AstNode *n);
int ast_struct_return_call_assign_supported(int lhs_type,
                                                  const struct AstNode *rhs);
int ast_struct_deref_copy_assign_supported(const struct AstNode *n);
int ast_struct_member_copy_assign_supported(const struct AstNode *n);
int ast_struct_chain_copy_assign_supported(const struct AstNode *n);
int ast_struct_addr_expr_supported(const struct AstNode *n, int *out_type);
int ast_struct_copy_assign_supported(const struct AstNode *n);
int ast_is_const_zero_condition(const struct AstNode *n);
int ast_is_const_nonzero_condition(const struct AstNode *n);
int ast_expr_yields_bool01(const struct AstNode *n);
void ast_support_cache_begin(void);
int ast_gen_supported(const struct AstNode *n);
int ast_call_arg_word_supported(const struct AstNode *arg);
int ast_call_struct_arg_supported(int want_type, const struct AstNode *arg);
int ast_value_is_long_word(const struct AstNode *arg);
int ast_long_word_type(const struct AstNode *arg, int *out_type);
int ast_call_arg_supported(struct Sym *fn_sym, int arg_index,
                                  const struct AstNode *arg);
int ast_va_builtin_supported(const struct AstNode *n);
int ast_call_named_args_supported(const struct AstNode *n);
const struct AstNode *ast_call_star_indirect_base(const struct AstNode *n);
int ast_call_star_indirect_supported(const struct AstNode *n);
int ast_call_indirect_supported(const struct AstNode *n);
struct Sym *ast_indirect_call_proto_sym(const struct AstNode *n);
int ast_value_is_float_word(const struct AstNode *arg);
int ast_value_is_pointer_word(const struct AstNode *n);
int ast_pointer_assign_rhs_supported(const struct AstNode *n);
int ast_unary_int_const_fold(const struct AstNode *n, long *out);
int ast_int_const_cast_fold(const struct AstNode *n, long *out);
int ast_unary_long_const_fold(const struct AstNode *n, long *out);
int ast_const_scalar_fold(const struct AstNode *n, long *out);
long ast_const_apply_int_cast(long v, int type);
int ast_const_condition_fold(const struct AstNode *n, long *out);
int ast_global_byte_array_const_store(const struct AstNode *n,
                                             struct Sym **out_arr,
                                             long *out_idx,
                                             long *out_rhs);
int ast_global_byte_array_fast_store(const struct AstNode *n,
                                            struct Sym **out_arr,
                                            struct Sym **out_idx_sym,
                                            long *out_idx_const,
                                            int *out_idx_has_const,
                                            struct Sym **out_rhs_sym,
                                            long *out_rhs_const,
                                            int *out_rhs_kind);
int ast_member_field_value_type(const struct AstNode *n);
int ast_return_stmt_supported(const struct AstNode *n);
int ast_cmp_operand_ok(const struct AstNode *e);
int ast_is_simple_cmp_cond(const struct AstNode *n);
int ast_const_cmp_extract(const struct AstNode *n, struct Sym **sp,
                                 int *opp, long *cp);
int ast_is_const_cmp_cond(const struct AstNode *n);
int ast_is_const_plain_int_cmp_cond(const struct AstNode *n);
int ast_byte_operand(const struct AstNode *e, struct ByteOperand *op);
int ast_is_byte_cmp_cond(const struct AstNode *n);
int ast_is_direct_byte_bitand_cond(const struct AstNode *n);
int ast_is_direct_wide_bitand_cond(const struct AstNode *n);
int ast_is_direct_long_const_eq_cond(const struct AstNode *n);
int ast_global_char_index_cond(const struct AstNode *n, struct Sym **out_sym);
int ast_is_float_cmp_cond(const struct AstNode *n);
int ast_cond_generic(const struct AstNode *n);
int ast_is_local_self_add_stmt(const struct AstNode *e);
struct Sym *ast_deadincdec_sym_direct(const struct AstNode *e);
int ast_deadincdec_member_ok(const struct AstNode *e);
int ast_incdec_addr_type_ok(int t);
int ast_index_lvalue_elem_type(const struct AstNode *n, int *out_type);
int ast_deadincdec_addr_lvalue_type(const struct AstNode *e, int *out_type);
int ast_dead_expr_supported(const struct AstNode *e);
int ast_for_init_expr_supported(const struct AstNode *e);
int ast_expr_stmt_supported(const struct AstNode *n);
int ast_stmt_supported(const struct AstNode *n);
int ast_for_decl_storage_supported(const struct AstNode *n);
int ast_process_statement(void);

#endif /* DCC_AST_GEN_INTERNAL_H */
