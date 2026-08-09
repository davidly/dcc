#ifndef DCC_MIR_INTERNAL_H
#define DCC_MIR_INTERNAL_H

/* Internal MIR module header: shared IR types, global compiler state,
 * and prototypes for helpers that cross the dcc_mir_*.c file split.
 * Not part of the public dcc_mir.h API - only the dcc_mir_*.c
 * translation units that implement the MIR backend include this.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dcc.h"
#include "dcc_ast.h"
#include "dcc_mir.h"

#define MIR_AGGREGATE_FORWARD_OFFSET (-32768L)
#define MIR_AGGREGATE_VALUE_DEST_OFFSET (-32767L)
#define MIR_AGGREGATE_GLOBAL_DEST_OFFSET (-32766L)
#define MIR_MAX_ROLLOUT_INSNS 4096

enum MirOpcode {
    MIR_NOP,
    MIR_PARAM,
    MIR_CONST,
    MIR_FLOAT_CONST,
    MIR_STRING_ADDRESS,
    MIR_ADDRESS,
    MIR_COMPOUND_ADDRESS,
    MIR_INDEX_ADDRESS,
    MIR_MEMBER_ADDRESS,
    MIR_VLA_SIZE,
    MIR_LOAD,
    MIR_LOAD_INDIRECT,
    MIR_INDEX_LOAD,
    MIR_STORE,
    MIR_STORE_INDIRECT,
    MIR_COPY_AGGREGATE,
    MIR_VLA_SAVE,
    MIR_VLA_ALLOC,
    MIR_VLA_RESTORE,
    MIR_UNARY,
    MIR_BINARY,
    MIR_ARG,
    MIR_CALL,
    MIR_CALL_AGGREGATE,
    MIR_VA_START,
    MIR_VA_END,
    MIR_VA_ARG,
    MIR_LABEL,
    MIR_JUMP,
    MIR_BRANCH_FALSE,
    MIR_DECL_PLACEHOLDER,
    MIR_OBJECT_MERGE,
    MIR_PHI,
    MIR_RETURN,
    MIR_OPAQUE
};

enum MirCallFlag {
    MIR_CALL_FLAG_FORMAT_HEX = 32,
    MIR_CALL_FLAG_FORMAT_OCTAL = 64,
    MIR_CALL_FLAG_FORMAT_RUNTIME =
        MIR_CALL_FLAG_FORMAT_HEX | MIR_CALL_FLAG_FORMAT_OCTAL,
    MIR_CALL_FLAG_INLINE_SUBSTITUTABLE = 2048,
    MIR_CALL_FLAG_VARIADIC = 4096
};

enum MirMemoryFlag {
    MIR_MEMORY_FLAG_DEREFERENCED_POINTER_ARRAY = 8192,
    MIR_MEMORY_FLAG_DEFERRED_WIDE_CONST = 16384
};

struct MirInsn {
    int opcode;
    int dst;
    int src1;
    int src2;
    int type;
    long immediate;
    int label;
    int phi_pred1;
    int phi_pred2;
    int successors[2];
    int successor_count;
    int object;
    int memory_size;
    int memory_flags;
    int bit_width;
    int bit_shift;
    unsigned int bit_mask;
    int secondary_offset;
    int inline_temp_id;
    char name[64];
    char base_name[64];
};

struct MirObject {
    char name[64];
    int storage;
    int type;
    int offset;
    int entry_value;
};

struct MirSwitchContext {
    long values[MAX_SWITCH_CASES];
    int labels[MAX_SWITCH_CASES];
    int count;
    int default_label;
    int end_label;
};

struct MirFunction {
    struct MirInsn *insns;
    int count;
    int capacity;
    int next_value;
    int next_label;
    int next_call_id;
    int next_inline_temp_id;
    int active;
    int sink_purpose;
    int break_labels[MAX_FLOW];
    int continue_labels[MAX_FLOW];
    int flow_depth;
    int has_vla;
    int has_runtime_stride_param;
    int is_variadic_function;
    char user_label_names[256][64];
    int user_label_ids[256];
    int user_label_count;
    struct MirSwitchContext switches[MAX_FLOW];
    int switch_depth;
    int declaration_placeholders[1024];
    int declaration_scope_ends[1024];
    const struct AstNode *declaration_nodes[1024];
    unsigned char declaration_consumed[1024];
    int declaration_count;
    int declaration_cursor;
    int declaration_capture_start;
    int declaration_placeholder;
    int declaration_active_index;
    int declaration_active;
    int compound_capture_starts[MAX_FLOW];
    int compound_depth;
    int scope_points[1024];
    int scope_count;
    int scope_cursor;
    int scope_replay_points[MAX_FLOW];
    int scope_replay_depth;
    int flow_points[1024];
    int flow_count;
    int flow_cursor;
    int flow_replay_point;
    int flow_replay_active;
    char label_replay_name[64];
    int label_replay_active;
    int emit_mode;
    int report_mode;
    int return_type;
    int local_bytes;
    int dead_local_suffix_bytes;
    int aggregate_temp_bytes;
    int opaque_count;
    int *allocation_colors;
    int *allocation_spills;
    int allocation_capacity;
    int allocation_spill_count;
    int allocation_fixed_moves;
    int allocation_operand_moves;
    int allocation_phi_moves;
    unsigned char *lazy_parameter_values;
    int lazy_parameter_capacity;
    /* Retained past mir_verify_and_dump()'s own scope (Item 20d,
     * mir-migration-plan-to-100pct.md) so a selector's acceptance probe
     * (e.g. mir_try_emit_homed_scalar_cfg's wide-value re-coloring check)
     * can re-run mir_allocate_registers with different parameters using
     * the exact same liveness data verification already computed, without
     * duplicating the dataflow fixed-point loop. Freed once per function in
     * mir_end_function(), after every selector has had a chance to run. */
    unsigned char *live_in;
    unsigned char *live_out;
    struct MirRegion *regions;
    int region_count;
    int region_capacity;
    int *instruction_regions;
    int instruction_region_capacity;
    struct MirRegionalSegment *regional_segments;
    int regional_segment_count;
    int regional_segment_capacity;
    int *regional_segment_heads;
    int regional_segment_head_capacity;
    int regional_spill_slot_count;
    unsigned char *regional_rematerializable;
    int regional_rematerializable_capacity;
    int *backend_slots;
    int backend_slot_capacity;
    int backend_slot_count;
    int *planned_stack_consumers;
    int planned_stack_consumer_capacity;
    int *planned_stack_values;
    int planned_stack_value_capacity;
    unsigned char *planned_stack_emitted;
    int planned_stack_emitted_capacity;
    FILE *capture_stream;
    EmitSink saved_sink;
    struct MirObject objects[256];
    int object_count;
    char declared_names[MAX_LOCALS][64];
    int declared_types[MAX_LOCALS];
    int declared_storage[MAX_LOCALS];
    int declared_offsets[MAX_LOCALS];
    int declared_sizes[MAX_LOCALS];
    int declared_dim_counts[MAX_LOCALS];
    int declared_dims[MAX_LOCALS][MAX_ARRAY_DIMS];
    char declared_link_names[MAX_LOCALS][64];
    int declared_elem_sizes[MAX_LOCALS];
    int declared_vla_size_offsets[MAX_LOCALS];
    int declared_is_vla[MAX_LOCALS];
    int declared_is_array[MAX_LOCALS];
    int declared_dynamic_strides[MAX_LOCALS];
    char declared_runtime_stride_names[MAX_LOCALS][64];
    int declared_is_const[MAX_LOCALS];
    unsigned long declared_const_values[MAX_LOCALS];
    int declared_is_funcptr[MAX_LOCALS];
    int declared_has_proto[MAX_LOCALS];
    int declared_proto_nargs[MAX_LOCALS];
    int declared_proto_types[MAX_LOCALS][MAX_PROTO_PARAMS];
    int declared_count;
    char alias_source_names[MAX_LOCALS][64];
    char alias_internal_names[MAX_LOCALS][64];
    int alias_declaration_indices[MAX_LOCALS];
    int alias_count;
    struct Sym *initializer_target;
    int initializer_capture_start;
    struct Sym *init_expression_target;
    int init_expression_offset;
    int init_expression_type;
    struct Sym *vla_target;
    int vla_capture_start;
    char name[64];
};

struct MirResolvedNamedAddress {
    const struct MirInsn *root;
    int storage;
    long offset;
    int has_index;
    int member_depth;
    char base_name[64];
    char leaf_member_name[64];
};

struct MirRegion {
    int first;
    int last;
    int boundary_after;
};

enum MirRegionalSegmentFlag {
    MIR_REGIONAL_LIVE_IN = 1,
    MIR_REGIONAL_LIVE_OUT = 2,
    MIR_REGIONAL_DEFINES = 4
};

enum MirRegionalRematerialization {
    MIR_REGIONAL_REMAT_NONE,
    MIR_REGIONAL_REMAT_PARAMETER,
    MIR_REGIONAL_REMAT_ADDRESS
};

struct MirRegionalSegment {
    int value;
    int region;
    int first_use;
    int last_use;
    int use_count;
    int available_colors;
    int allocatable_colors;
    int color;
    int spill_slot;
    int flags;
    int next_for_value;
};

enum MirPhysicalColor {
    MIR_COLOR_HL,
    MIR_COLOR_DE,
    MIR_COLOR_BC,
    MIR_COLOR_IY,
    /* Reserved for a future value-width (4-byte) allocator extension
     * (mir-migration-plan-to-100pct.md Item 20): a wide value would occupy
     * two adjacent single-register-pair slots simultaneously. Never
     * assigned by mir_allocate_registers today - MIR_COLOR_COUNT below,
     * and every array indexed directly by an allocation_colors value (the
     * MirAllocationSummary.colors[] field and the DCC_MIR_REPORT "homes"
     * printer), is sized to tolerate these codes so that a later patch can
     * introduce them without an audit of every consumer. */
    MIR_COLOR_HL_DE,
    MIR_COLOR_BC_IY,
    MIR_COLOR_COUNT
};

extern struct MirFunction mir;
extern int mir_virtual_iy_base;
extern int mir_virtual_iy_frame_bytes;
extern int mir_emit_instruction_index;
extern int mir_forwarded_hl_value;
extern int mir_forwarded_hl_instruction;
extern int mir_forwarded_wide_value;
extern int mir_forwarded_wide_instruction;
extern int mir_forwarded_stack_value;
extern int mir_forwarded_stack_instruction;
extern int mir_cached_call_value;
extern int mir_cached_call_instruction;
extern int mir_cached_wide_call_value;
extern int mir_cached_wide_call_instruction;

/* Cross-file helper prototypes (defined in exactly one dcc_mir_*.c file,
 * used from at least one other). */
int mir_affine_value(int value, const struct MirInsn **parameter,
                            long *constant, int depth);
int mir_block_label_before(int instruction);
int mir_call_is_bdos_family_fastcall(int call_index,
                                           const char **rtl_name,
                                           int *fn_value, int *dearg_value);
int mir_call_is_de_hl_fastcall(int call_index, const char **rtl_name,
                                     int *arg0_value, int *arg1_value);
int mir_call_is_memchr_fastcall(int call_index, int *s_value,
                                      int *c_value, int *n_value);
int mir_call_is_memcmp_fastcall(int call_index, int *s1_value,
                                       int *s2_value, int *n_value);
int mir_call_is_memcpy_fastcall(int call_index, int *dst_value,
                                      int *src_value, int *n_value);
int mir_call_is_memset_fastcall(int call_index, int *dest_value,
                                       int *fill_value, int *count_value);
int mir_call_is_strchr_fastcall(int call_index, int *s_value,
                                      int *c_value);
int mir_call_is_strlen_fastcall(int call_index, int *s_value);
int mir_call_is_strrchr_fastcall(int call_index, int *s_value,
                                       int *c_value);
int mir_call_uses_value(const struct MirInsn *call, int value);
int mir_compare_definition_for_branch(int instruction);
int mir_direct_branch_for_unary_not(int instruction);
int mir_unary_not_definition_for_branch(int instruction);
void mir_compute_dead_local_suffix(void);
int mir_current_frame_bytes(void);
int mir_effective_local_bytes(void);
void mir_report_dead_local_suffix(void);
int mir_declared_is_vla_object(const char *name);
const char *mir_declared_link_name(const char *name);
int mir_declared_location(const char *name, int *type, int *storage,
                                 int *offset);
int mir_direct_branch_for_comparison(int instruction);
int mir_edge_phi_names_predecessor(int predecessor, int successor);
void mir_begin_strict_phi_fallthrough(void);
void mir_end_strict_phi_fallthrough(void);
int mir_instruction_has_phi_fallthrough(int instruction,
                                        int require_next_label);
int mir_strict_phi_fallthrough_was_used(void);
int mir_emit_constant_to_home(FILE *out, int value, long immediate);
int mir_emit_hl_to_home(FILE *out, int value);
int mir_home_spill_offset(int value, int *offset);
int mir_home_spill_bytes(void);
void mir_extrn_begin_attempt(void);
int mir_extrn_should_emit(struct Sym *sym);
int mir_extrn_should_emit_name(const char *name);
void mir_emit_runtime_call(FILE *out, const char *name);
void mir_emit_home_epilogue(FILE *out, int uses_iy);
void mir_emit_home_prologue(FILE *out, int uses_iy);
int mir_emit_home_push(FILE *out, int value);
int mir_emit_home_to_hl(FILE *out, int value);
int mir_emit_homed_binary_instruction(FILE *out,
                                             const struct MirInsn *insn,
                                             int allow_comparison);
int mir_type_uses_unsigned_comparison(int type);
int mir_emit_homed_constant_binary_instruction(FILE *out,
                                                       const struct MirInsn *insn,
                                                       int operation, long value);
int mir_emit_homed_compare_false(FILE *out,
                                        const struct MirInsn *compare,
                                        int false_label);
int mir_emit_homed_phi_copies(FILE *out, int predecessor,
                                     int successor);
int mir_emit_homed_unary_instruction(FILE *out,
                                            const struct MirInsn *insn);
int mir_emit_ix_offset_address_to_home(FILE *out, int value,
                                               int offset);
void mir_emit_iy_prologue(FILE *out);
int mir_emit_label_address_to_home(FILE *out, int value,
                                           const char *assembly_name);
int mir_emit_load_param(FILE *out, const struct MirInsn *param);
int mir_emit_load_param_de(FILE *out, const struct MirInsn *param);
int mir_emit_load_param_wide(FILE *out, const struct MirInsn *param);
int mir_emit_pointer_offset_address_to_home(FILE *out, int dst,
                                                    int base, long offset);
void mir_emit_prologue(FILE *out);
int mir_emit_wide_operation(FILE *out, const struct MirInsn *insn);
void mir_emit_scalar_compare(FILE *out, int operation, int is_unsigned);
void mir_emit_scalar_compare_biased_right(FILE *out, int operation);
void mir_emit_signed_byte_extend(FILE *out);
void mir_emit_hl_and_const(FILE *out, unsigned int mask);
void mir_emit_hl_or_const(FILE *out, unsigned int mask);
void mir_emit_bitfield_extract(FILE *out, const struct MirInsn *insn);
void mir_emit_scalar_shift(FILE *out, int operation, int is_unsigned,
                           int count_value);
void mir_emit_scalar_shift_by_constant(FILE *out, int operation,
                                       int is_unsigned, long count);
int mir_emit_stack_word_param_to_home(FILE *out, int value, int offset);
int mir_emit_stack_byte_param_to_home(FILE *out, int value, int offset,
                                      int type);
int mir_ulong_log2_pow2(unsigned long v);
int mir_mul_const_fast_path_eligible(unsigned long multiplier, int dst);
void mir_emit_mul_hl_const(FILE *out, unsigned long multiplier);
void mir_emit_wide_shift_by_constant(FILE *out, int is_left,
                                     int is_unsigned, long count);
void mir_emit_word_and_constant(FILE *out, char hi_reg, char lo_reg,
                                 unsigned int word_mask);
int mir_emit_wide_constant_to_home(FILE *out, int value, long immediate);
int mir_emit_wide_home_to_hl_de(FILE *out, int value);
int mir_emit_hl_de_to_wide_home(FILE *out, int value);
int mir_emit_wide_home_to_stack(FILE *out, int value);
int mir_emit_cast(FILE *out, int source_type, int target_type);
int mir_emit_word_param_to_home(FILE *out, int value, int offset);
int mir_emit_byte_param_to_home(FILE *out, int value, int offset, int type);
int mir_find_label(int label);
int mir_label_is_jump_target(int label);
int mir_insn_is_reachable(int i);
int mir_target_is_noop_fallthrough(int instruction, int target);
int mir_first_nonlabel_successor(int successor);
int mir_first_phi_or_block_end(int successor);
int mir_fold_constant_binary(int op, long left, long right,
                                    int operand_type, long *result);
int mir_fold_constant_compare(int op, long left, long right,
                                     int operand_type, long *result);
int mir_float_identity_unary(const struct MirInsn *insn);
int mir_homed_string_call_argument(int value);
int mir_has_phi_instruction(void);
int mir_has_cfg_backedge(void);
int mir_general_comparison_count(void);
int mir_home_color_live_across(int instruction, int color);
int mir_home_uses_iy(void);
int mir_constant_absolute_access_supported(const struct MirInsn *insn);
int mir_constant_absolute_address_has_index(int value);
int mir_value_is_constant_absolute_address(int value);
int mir_value_only_used_by_constant_absolute_address(int value);
int mir_value_only_used_by_absolute_access(
    int value, int (*access_supported)(const struct MirInsn *));
int mir_resolve_named_address(
    int value, struct MirResolvedNamedAddress *out);
int mir_resolve_isolated_global_field_address(
    int value, struct MirResolvedNamedAddress *out);
int mir_prepare_constant_absolute_operand(
    FILE *out, int value, char *operand, size_t operand_size);
int mir_object_is_fully_promoted(int object);
int mir_object_address_taken(int object);
int mir_store_is_dead(int instruction);
int mir_value_only_used_by_dead_stores(int value);
const char *mir_opcode_name(int opcode);
int mir_phi_source_for_edge(const struct MirInsn *phi,
                                   int predecessor_label, int edge_label,
                                   int successor, int phi_instruction);
int mir_begin_lazy_parameter_allocation(void);
void mir_end_lazy_parameter_allocation(void);
int mir_begin_rematerialized_home_allocation(void);
void mir_end_rematerialized_home_allocation(void);
int mir_rematerialized_home_allocation_is_active(void);
int mir_has_lazy_parameters(void);
int mir_is_lazy_parameter(int value);
int mir_lazy_parameter_count(void);
int mir_lazy_byte_parameter_count(void);
int mir_lazy_parameter_offset(int value, int *offset, int *type);
int mir_probe_wide_colors_for_homed(
    const unsigned char *rematerializable, int bounded_hybrid);
int mir_homed_rematerializable_wide_candidate_count(void);
int mir_homed_value_is_rematerializable(int value);
void mir_begin_hybrid_homed_selection(void);
void mir_end_hybrid_homed_selection(void);
int mir_begin_regional_home_plan(void);
void mir_end_regional_home_plan(void);
int mir_regional_home_plan_is_active(void);
int mir_regional_rematerialization_kind(int value);
int mir_regional_parameter_location(int value, int *offset, int *type);
int mir_regional_object_home_offset(int value, int *offset);
int mir_regional_store_uses_object_home(const struct MirInsn *store);
const struct MirRegionalSegment *mir_regional_segment_for(
    int value, int instruction);
void mir_regional_begin_emission(void);
int mir_regional_before_instruction(FILE *out, int instruction);
void mir_regional_after_instruction(int instruction);
void mir_resolve_deferred_metadata(void);
int mir_extended_integer_constant_conversion_folds(void);
int mir_scalar_memory_location(const struct MirInsn *insn, int *type,
                                      int *storage, int *offset);
int mir_inline_temp_slot(const char *name);
int mir_iy_home_live_across_caller_clobber(void);
const char *mir_sink_name(int purpose);
void mir_thread_jumps(void);
int mir_value_number_global_field_loads(void);
int mir_global_field_value_numbering_count(void);
int mir_repeated_named_pointer_load_count(void);
int mir_eliminate_common_block_expressions(void);
int mir_common_block_expression_elimination_count(void);
int mir_eliminate_common_region_expressions(void);
void mir_simplify_boolean_phi_branches(void);
int mir_boolean_phi_branch_simplification_count(void);
void mir_reset_boolean_phi_branch_simplification_count(void);
void mir_forward_immediate_phi_returns(void);
int mir_phi_return_forwarding_count_value(void);
void mir_reset_phi_return_forwarding_count(void);
int mir_try_emit_homed_scalar_cfg(FILE *out);
int mir_homed_cfg_depends_on_unary_not_branch(void);
int mir_homed_cfg_was_frameless(void);
int mir_cfg_block_count(void);
int mir_homed_cfg_depends_on_word_store(void);
int mir_homed_cfg_depends_on_dynamic_index(void);
int mir_homed_cfg_depends_on_constant_absolute(void);
long mir_stream_size(FILE *stream);
int mir_stream_instruction_count(FILE *stream);
int mir_try_emit_homed_scalar_dag(FILE *out);
int mir_try_emit_scalar_dag(FILE *out);
int mir_spilled_cfg_depends_on_constant_absolute(void);
int mir_spilled_cfg_depends_on_dynamic_index_base_forwarding(void);
int mir_spilled_cfg_depends_on_direct_byte_param(void);
int mir_spilled_cfg_depends_on_constant_index_absolute(void);
int mir_spilled_cfg_depends_on_wide_constant_rematerialization(void);
int mir_spilled_cfg_depends_only_on_unsigned_wide_constant_relational(void);
int mir_spilled_cfg_has_wide_mulmod_fusion(void);
int mir_spilled_cfg_has_divmod_pair(void);
int mir_spilled_cfg_depends_on_unary_not_branch_fusion(void);
int mir_spilled_cfg_depends_on_planned_stack_handoff(void);
int mir_spilled_cfg_depends_on_planned_index_base_handoff(void);
int mir_spilled_cfg_depends_on_stable_pointer_local_home(void);
int mir_spilled_cfg_depends_on_stable_pointer_local_slot(void);
void mir_begin_stable_pointer_local_homes(void);
void mir_end_stable_pointer_local_homes(void);
int mir_spilled_cfg_depends_on_rhs_stack_forwarding(void);
int mir_spilled_cfg_depends_on_binary_load_pair_forwarding(void);
int mir_spilled_cfg_depends_on_dense_byte_switch(void);
int mir_spilled_cfg_dense_byte_switch_case_count(void);
int mir_spilled_cfg_dense_byte_switch_width(void);
int mir_spilled_cfg_dense_byte_switch_uses_direct_condition(void);
int mir_spilled_cfg_dense_byte_switch_uses_postincrement_index(void);
int mir_spilled_cfg_inline_postincrement_uses(void);
int mir_spilled_cfg_inline_indexed_stack_store_uses(void);
int mir_spilled_cfg_small_selfstore_add_uses(void);
int mir_spilled_cfg_uses_exact_semantic_kernel(void);
void mir_begin_general_rhs_stack_forwarding(void);
void mir_end_general_rhs_stack_forwarding(void);
int mir_spilled_cfg_depends_on_indirect_store_value_forwarding(void);
int mir_spilled_cfg_indirect_store_value_forwarding_uses(void);
void mir_begin_indirect_store_value_forwarding(void);
void mir_end_indirect_store_value_forwarding(void);
int mir_spilled_cfg_depends_on_branch_condition_forwarding(void);
int mir_spilled_cfg_branch_condition_forwarding_uses(void);
void mir_begin_branch_condition_forwarding(void);
void mir_end_branch_condition_forwarding(void);
void mir_begin_address_rematerialization(void);
void mir_end_address_rematerialization(void);
void mir_begin_block_cse_address_rematerialization(void);
void mir_end_block_cse_address_rematerialization(void);
int mir_address_rematerialization_candidate_count(void);
void mir_begin_phi_slot_cleanup(void);
void mir_end_phi_slot_cleanup(void);
int mir_spilled_cfg_depends_on_indirect_store_address_forwarding(void);
int mir_spilled_cfg_indirect_store_address_forwarding_uses(void);
void mir_begin_indirect_store_address_forwarding(void);
void mir_end_indirect_store_address_forwarding(void);
void mir_begin_wide_binary_lhs_forwarding(void);
void mir_end_wide_binary_lhs_forwarding(void);
void mir_begin_wide_binary_rhs_forwarding(void);
void mir_end_wide_binary_rhs_forwarding(void);
int mir_wide_binary_rhs_forwarding_use_count(void);
void mir_begin_wide_store_forwarding(void);
void mir_end_wide_store_forwarding(void);
void mir_begin_stable_pointer_argument_rematerialization(void);
void mir_end_stable_pointer_argument_rematerialization(void);
void mir_begin_global_argument_rematerialization(void);
void mir_end_global_argument_rematerialization(void);
void mir_begin_wide_first_argument_stack_cache(void);
void mir_end_wide_first_argument_stack_cache(void);
void mir_begin_narrow_argument_direct_push(void);
void mir_end_narrow_argument_direct_push(void);
void mir_begin_constant_argument_prepacking(void);
void mir_end_constant_argument_prepacking(void);
void mir_begin_promoted_local_slot_reuse(void);
void mir_end_promoted_local_slot_reuse(void);
int mir_spilled_cfg_depends_on_promoted_local_slot_reuse(void);
int mir_spilled_cfg_depends_on_wide_store_forwarding(void);
int mir_try_emit_spilled_scalar_cfg(FILE *out);
int mir_spilled_cfg_depends_on_dead_store_forwarding(void);
int mir_value_has_use(int value);
int mir_value_has_use_after(int value, int instruction);
int mir_value_use_count(int value);
int mir_verify_and_dump(void);
const struct MirInsn *mir_definition(int value);
struct MirInsn *mir_mutable_definition(int value);
int mir_load_is_single_call_argument(int value, int size);
void mir_emit_virtual_load(FILE *out, int value);
int mir_value_is_wide(int value);
int mir_value_is_selfstore_incdec(int value);
extern long mir_spilled_scalar_cfg_elided_epilogue_bytes;

#endif /* DCC_MIR_INTERNAL_H */
