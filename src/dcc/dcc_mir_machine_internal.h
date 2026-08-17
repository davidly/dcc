/**
 * @file dcc_mir_machine_internal.h
 * @brief Declares the private interface shared by exact-schedule modules.
 *
 * @par Role
 * Exposes semantic proof helpers, common Z80 emission helpers, exact-family
 * dispatchers, and phase/profile contracts used by dcc_mir_machine_emit.c and
 * the dcc_mir_machine_*.c family modules. It is not a frontend or general MIR
 * API.
 */
#ifndef DCC_MIR_MACHINE_INTERNAL_H
#define DCC_MIR_MACHINE_INTERNAL_H

#include "dcc_mir_internal.h"

/* Forward declarations only: each is fully (and identically) defined in
 * every dcc_mir_machine_*.c/dcc_mir_emit_common.c file that needs it, as a
 * small duplicated plan-descriptor type rather than one shared definition.
 * Declaring the tags here first ensures every translation unit that
 * includes this header resolves them to the same file-scope tag, instead
 * of each function declaration below implicitly creating its own
 * prototype-scope (and therefore conflicting) incomplete type. */
struct MirMachineForm;
struct MirStateMember;

int mir_machine_reject(const char *template_name, const char *reason);
int mir_machine_named_nonvolatile(const struct MirInsn *insn);
int mir_machine_constant_equals(int value, long expected);
int mir_machine_evaluate_constant(int value, long *result, int depth);
int mir_machine_same_location(const struct MirInsn *left,
                              const struct MirInsn *right);
int mir_machine_unobservable_local_store(const struct MirInsn *store);
int mir_machine_parameter_value_offset(int value, int *stack_offset);
int mir_machine_pointee_is_volatile(const struct MirInsn *parameter);
int mir_machine_global_address_offset(int value, struct Sym **root_out,
                                      long *offset_out, int depth);
int mir_match_final_call_integer_type(int type, int width);
int mir_match_math_symbol_target(const struct MirInsn *call,
                                 struct Sym *function);
int mir_match_action_decode_pointer_type(int type);
int mir_match_action_decode_word_type(int type);
int mir_match_buffered_declaration_buffer(
    int instruction, const struct MirInsn *first, int *offset_out);
void mir_machine_emit_global_address_de(MirStream *out, struct Sym *symbol,
                                        int offset);
void mir_machine_emit_hl_offset(MirStream *out, int offset, int preserve_bc);
void mir_machine_emit_global_word(MirStream *out, struct Sym *symbol, int offset);
void mir_machine_emit_global_word_store(
    MirStream *out, struct Sym *symbol, int offset);
void mir_machine_emit_vla_allocate_rows(
    MirStream *out, unsigned long row_bytes);
void mir_machine_emit_float_bits(MirStream *out, unsigned long bits);
void mir_machine_emit_symbol_call(MirStream *out, struct Sym *symbol);
void mir_machine_emit_ix_wide_load(MirStream *out, int offset);
void mir_machine_emit_ix_wide_store(MirStream *out, int offset);
void mir_emit_final_call_constant(MirStream *out, unsigned long value, int width);
void mir_emit_final_call_cleanup(MirStream *out, int words);

/* Promoted from dcc_mir_machine_emit.c during the family split below:
 * used by more than one dcc_mir_machine_*.c family, so they moved from
 * static file-local helpers to shared definitions in dcc_mir_emit_common.c. */
void mir_emit_byte_parameter_word(MirStream *out, int stack_offset,
                                  int is_unsigned);
void mir_emit_fixed_point_constant(MirStream *out, unsigned long value);
void mir_emit_local_address(MirStream *out, int offset);
void mir_emit_local_wide_argument(MirStream *out, int offset);
void mir_emit_wide_parameter(MirStream *out, int stack_offset);
int mir_machine_boolean_merge(int phi_index, int true_value_index,
                              int false_value_index, int true_label_index,
                              int false_label_index);
int mir_machine_convert_integer(long value, int type, long *result);
void mir_machine_emit_global_byte_a(MirStream *out, struct Sym *symbol,
                                    int offset, int is_store);
int mir_machine_five_call_arguments(const struct MirInsn *call,
                                    int arguments[5]);
int mir_machine_four_call_arguments(const struct MirInsn *call,
                                    int arguments[4]);
int mir_machine_name_nonvolatile(const char *name);
int mir_machine_parameter_address(int value, int *stack_offset,
                                  long *offset, int depth);
int mir_machine_parameter_offset(int value, int *stack_offset);
int mir_machine_phi_merge(int phi_index, int true_value_index,
                          int false_value_index, int true_label_index,
                          int false_label_index);
int mir_machine_pointer_form(int value, int before,
                             struct MirMachineForm *form, int depth);
const struct MirInsn *mir_machine_resolve_local_alias(int value);
int mir_machine_six_call_arguments(const struct MirInsn *call,
                                   int arguments[6]);
int mir_machine_ten_call_arguments(const struct MirInsn *call,
                                   int arguments[10]);
int mir_machine_transparent_pointer_unary(const struct MirInsn *unary);
int mir_machine_wide_parameter_offset(int value, int *stack_offset);

/* Returns -1 when neither family matches, otherwise the selector result. */
int mir_try_emit_float_reports(MirStream *out);

/* Returns -1 when no attention kernel matches, otherwise the selector result. */
int mir_try_emit_attention_kernels(MirStream *out);

/* The late phase preserves the symbol-search selector's existing position. */
int mir_try_emit_scanner_kernels(MirStream *out, int late);

/* Returns -1 when no aggregate check schedule matches. */
int mir_try_emit_aggregate_checks(MirStream *out);

enum MirStrictSpilledProfile {
    MIR_STRICT_SPILLED_ADDRESS_REMAT = 1,
    MIR_STRICT_SPILLED_GLOBAL_ARGUMENT,
    MIR_STRICT_SPILLED_PHI_SLOT
};

/* Preserves the call/control orchestration selector band. */
int mir_try_emit_call_runners(MirStream *out);

/* Phase 0 preserves the runtime/file/system band; phase 2 profiles spills. */
int mir_try_emit_runtime_runners(MirStream *out, int phase);

/* Preserves the interpreter/parser selector band. */
int mir_try_emit_interpreter_runners(MirStream *out);

/* Each phase preserves the validation runner's existing selector position. */
int mir_try_emit_validation_runners(MirStream *out, int phase);

/* Each phase preserves the moved endgame schedule's selector position. */
int mir_try_emit_endgame_runners(MirStream *out, int phase);

/* The phase preserves each numeric schedule's existing selector position. */
int mir_try_emit_numeric_kernels(MirStream *out, int phase);

/* Returns -1 when no container kernel matches, otherwise the selector
 * result. Array/stack/append/row-store plan kernels plus early float and
 * constant comparison checks. */
int mir_try_emit_container_kernels(MirStream *out);

/* Returns -1 when no wide/record kernel matches. Wide-value and
 * aggregate-member arithmetic, record append, and byte mismatch/arithmetic
 * report kernels. */
int mir_try_emit_wide_record_kernels(MirStream *out);

/* Returns -1 when no structural check matches. Bitset/sieve/wrapper init,
 * task array and literal/compound check runners, sort/crc/string mismatch
 * reports, and float/struct/bitfield field checks. */
int mir_try_emit_structural_checks(MirStream *out);

/* Returns -1 when no float/recursion kernel matches. Float polynomial
 * kernels and recursive frame/wide-product/tree-sum kernels. */
int mir_try_emit_float_recursion_kernels(MirStream *out);

/* Returns -1 when no byte-scan kernel matches. Byte/row scanning and fill
 * kernels, prediction-count call kernels, hash and file-line loops. */
int mir_try_emit_byte_scan_kernels(MirStream *out);

/* Never returns -1 (its final kernel is the original dispatcher's absolute
 * fallback). Constant-folding and result-switch kernels. */
int mir_try_emit_constant_folding_kernels(MirStream *out);

#endif
