#ifndef DCC_MIR_MACHINE_INTERNAL_H
#define DCC_MIR_MACHINE_INTERNAL_H

#include "dcc_mir_internal.h"

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

#endif
