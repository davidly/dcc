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
void mir_machine_emit_global_address_de(FILE *out, struct Sym *symbol,
                                        int offset);
void mir_machine_emit_global_word(FILE *out, struct Sym *symbol, int offset);
void mir_machine_emit_float_bits(FILE *out, unsigned long bits);
void mir_machine_emit_symbol_call(FILE *out, struct Sym *symbol);
void mir_machine_emit_ix_wide_load(FILE *out, int offset);
void mir_machine_emit_ix_wide_store(FILE *out, int offset);
void mir_emit_final_call_constant(FILE *out, unsigned long value, int width);
void mir_emit_final_call_cleanup(FILE *out, int words);

/* Returns -1 when neither family matches, otherwise the selector result. */
int mir_try_emit_float_reports(FILE *out);

/* Returns -1 when no attention kernel matches, otherwise the selector result. */
int mir_try_emit_attention_kernels(FILE *out);

#endif
