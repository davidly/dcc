#ifndef DCC_REGALLOC_INTERNAL_H
#define DCC_REGALLOC_INTERNAL_H

#include "dcc.h"

extern struct Sym *g_bc_regalloc_sym;
extern int g_regalloc_address_escaped;
extern int g_e_regalloc_claim_active;
extern int g_e_regalloc_claimed;
extern struct Sym *g_e_regalloc_sym;
extern int g_loop_regalloc_bc_claimed;

int regalloc_buffer_finalize(FILE *f, struct Sym *bc_cand, struct Sym *e_cand,
                             FILE **out_f);
int bc_regalloc_entry_lines(struct Sym *cand, char lines[3][40]);
int bc_regalloc_exit_lines(struct Sym *cand, char lines[3][40]);
int line_touches_bc_reg(const char *s);
int buf_has_unsafe_call(const char *buf);
int asm_name_is_bc_safe_call(const char *name);
int asm_name_is_noreturn_call(const char *name);

struct Sym *find_bc_regalloc_candidate(int params_end);
int plain_static_body_can_be_buffered(struct Sym *s, const char *name);
int function_qualifies_for_speculative_noix(const char *name, int local_bytes);
int function_qualifies_for_speculative_regalloc(const char *name);
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