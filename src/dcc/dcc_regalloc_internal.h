/* dcc_regalloc_internal.h - private state and entry points shared by
 * speculative whole-function/loop register allocation and its callers. */
#ifndef DCC_REGALLOC_INTERNAL_H
#define DCC_REGALLOC_INTERNAL_H

#include "dcc.h"

extern struct Sym *g_bc_regalloc_sym;
extern int g_regalloc_address_escaped;
extern int g_e_regalloc_claim_active;
extern int g_e_regalloc_claimed;
extern struct Sym *g_e_regalloc_sym;
extern int g_loop_regalloc_bc_claimed;
/* The IY-resident whole-function candidate, live only for the duration of
 * one speculative generation attempt (try_speculative_iy_regalloc_function_
 * body). Read by emit_function_prologue/epilogue, which emit the callee-save
 * push/pop, and by recompute_param_offsets, which shifts every parameter by
 * the 2 bytes that push occupies. */
extern struct Sym *g_iy_regalloc_sym;
extern int g_iy_regalloc_escaped;
/* Whether the function being compiled contained a call, as observed by the
 * frame-sizing scan - before any speculative codegen pass could overwrite
 * current_function_has_call with a substituted callee's value. */
extern int current_function_had_call_at_scan;

/* Verifies generated text in `f`. On success, returns 1 and transfers a newly
 * allocated, rewound commit stream through out_f; the caller owns that FILE.
 * On failure, returns 0 and leaves out_f untouched. */
int regalloc_buffer_finalize(FILE *f, struct Sym *bc_cand, struct Sym *e_cand,
                             FILE **out_f);
int bc_regalloc_entry_lines(struct Sym *cand, char lines[3][40]);
int bc_regalloc_exit_lines(struct Sym *cand, char lines[3][40]);
/* Machine-readable claim/free directives consumed by dccpeep's claim
 * registry. A claim is live until its matching free, or to the end of the
 * enclosing function if none is emitted. See emit_regalloc_claim's own
 * comment (dcc_regalloc.c) for the contract. */
void emit_regalloc_claim(const char *reg, const char *scope,
                         struct Sym *cand, const char *kind, long value);
void emit_regalloc_free(const char *reg);
/* Estimated dynamic cycles saved by keeping `cand` in a 16-bit register
 * over `refs` references at loop nesting depth `depth`. Published in the
 * claim directive so dccpeep can compare it against its own candidates. */
long regalloc_estimate_value(struct Sym *cand, int refs, int depth);
int line_touches_bc_reg(const char *s);
int buf_has_unsafe_call(const char *buf);
int asm_name_is_bc_safe_call(const char *name);
int asm_name_is_noreturn_call(const char *name);
/* True if `s` has a captured, codegen-time-substitutable inline body. The
 * regalloc and LICM scans recurse into that body instead of declining on the
 * AST_CALL node, matching what codegen will substitute. Defined in dcc_func.c. */
int is_inline_substitutable(struct Sym *s);

struct Sym *find_bc_regalloc_candidate(int params_end);
/* Whole-function IY candidate search and speculative promotion. Applies only
 * to functions containing calls - the ones every caller-saved register is
 * disqualified from - and only to word-sized read-only parameters. */
struct Sym *find_iy_regalloc_candidate(int params_end);
int function_qualifies_for_speculative_iy_regalloc(const char *name);
int try_speculative_iy_regalloc_function_body(const char *name, int type,
                                             int local_bytes, struct Sym *s,
                                             struct Sym *iy_cand,
                                             long body_start_pos,
                                             long body_start_tok_start,
                                             int body_start_line,
                                             int body_start_tok_line,
                                             struct Token body_start_tok,
                                             int body_start_nlocals,
                                             int body_start_local_size);
extern int g_iy_regalloc_last_ref_count;
int frame_first_param_offset(void);
int plain_static_body_can_be_buffered(struct Sym *s, const char *name);
int function_qualifies_for_speculative_noix(const char *name, int local_bytes);
int function_qualifies_for_speculative_regalloc(const char *name);
/* Trial generators return 1 only after committing a complete body to the
 * current sink. A 0 return means the caller must emit the normal fallback;
 * declined trials restore lexer/frame/function-pass state before returning. */
int try_speculative_noix_function_body(const char *name, int type,
                                       int local_bytes, struct Sym *s,
                                       long body_start_pos, long body_start_tok_start,
                                       int body_start_line, int body_start_tok_line,
                                       struct Token body_start_tok, int body_start_nlocals,
                                       int body_start_local_size);
int try_loop_scoped_regalloc_first(const char *name, int type,
                                   int local_bytes, struct Sym *s,
                                   long body_start_pos, long body_start_tok_start,
                                   int body_start_line, int body_start_tok_line,
                                   struct Token body_start_tok, int body_start_nlocals,
                                   int body_start_local_size);
int try_speculative_bc_regalloc_function_body(const char *name, int type,
                                              int local_bytes, struct Sym *s,
                                              struct Sym *bc_cand, int attempt_e,
                                              long body_start_pos, long body_start_tok_start,
                                              int body_start_line, int body_start_tok_line,
                                              struct Token body_start_tok, int body_start_nlocals,
                                              int body_start_local_size);
int try_speculative_bc_regalloc_with_e_fallback(const char *name, int type,
                                                int local_bytes, struct Sym *s,
                                                struct Sym *bc_cand,
                                                long body_start_pos, long body_start_tok_start,
                                                int body_start_line, int body_start_tok_line,
                                                struct Token body_start_tok, int body_start_nlocals,
                                                int body_start_local_size);

#endif