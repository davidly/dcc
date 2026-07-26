/* dccpeep_internal.h - private line-program contract for dccpeep modules.
 *
 * Passes may mutate only through this API: user-assembly entries are opaque,
 * and the scheduler in dccpeep.c remains the sole owner of pass ordering.
 */
#ifndef DCCPEEP_INTERNAL_H
#define DCCPEEP_INTERNAL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#define MAX_LINES 400000
#define MAX_LINE  512

extern char *lines[MAX_LINES];
extern char *user_asm_original[MAX_LINES];
extern int nlines;
extern int input_is_dcc_generated;
extern unsigned long peep_lines_inserted;
extern unsigned long peep_lines_deleted;

char *xstrdup2(const char *s);
int eq(int i, const char *s);
int starts_label(const char *s);
int is_blank_or_comment(const char *s);
int is_global_asm_label_line(int i);
void strip_peep_comment_copy(char *dst, const char *src);
void strip_peep_comment_lower_copy(char *dst, const char *src);
void replace1(int i, const char *s);
void replace1_tagged(int i, const char *s, const char *tag);
void delete_n(int i, int count);
void insert_line(int i, const char *s);
void insert_line_tagged(int i, const char *s, const char *tag);
void read_file(const char *name);
void write_file(const char *name);

/* Stateless instruction/operand parsing. */
int parse_ld_hl_imm(const char *s, char *val, size_t val_size);
int parse_ld_de_imm(const char *s, char *val, size_t val_size);
int parse_nonneg_int(const char *s, int *out);
int parse_jp_cond_label(const char *s, const char *cond, char *label);
int parse_jp_z_label(const char *s, char *label);
int parse_jp_nz_label(const char *s, char *label);
int parse_jp_c_label(const char *s, char *label);
int parse_jp_nc_label(const char *s, char *label);
int parse_ld_de_positive_imm(const char *s, long *out);
int peep_parse_jp_cond_label(const char *s, const char *cond, char *lab);
int peep_parse_jp_uncond_label(const char *s, char *lab);
void peep_make_cond_jump(char *out, size_t size, const char *cond, const char *lab);
int peep_parse_any_cond_jump(const char *s, char *cond, char *lab);
const char *peep_inverse_cond(const char *cond);
int peep_parse_ld_l_ix(const char *s, char *off);
int peep_is_jp_z_or_nz(const char *s);
void peep_make_ld_a_ix(char *out, const char *off);
int peep_parse_ld_ix_a(const char *s, char *off);
int peep_parse_ld_a_ix(const char *s, char *off);
int peep_parse_ld_de_0_to_255(const char *s, int *out);
int peep_parse_ld_de_signed(const char *s, int *out);
void peep_format_ix_off(char *buf, int off);
int peep_parse_ld_e_imm8(const char *s, int *out);
int peep_parse_ld_hl_0_to_255(const char *s, int *out);
int peep_parse_ld_h_ix(const char *s, char *off);
int peep_parse_ld_e_ix(const char *s, char *off);
int peep_parse_ld_d_ix(const char *s, char *off);
int peep_parse_ld_ix_pair(const char *s1, const char *s2, int *off);
int peep_parse_st_ix_pair(const char *s1, const char *s2, int *off);
int peep_parse_jp_same_z_c(int iz, int ic, char *lab);
int peep_parse_dec_ix_byte(const char *s, int *off);
int peep_parse_ld_ix_byte_imm(const char *s, int *off, int *val);
int peep_parse_inc_ix_byte(const char *s, int *off);
int peep_parse_cp_const(const char *s, int *val);
int peep_parse_st_ix_de_pair(const char *s1, const char *s2, int *off);
int peep_parse_ld_hl_paren_sym(const char *s, char *sym);
int peep_parse_ld_paren_sym_hl(const char *s, char *sym);
int parse_ix_off_numeric(const char *off, int *val);
int parse_ld_reg16_dest(const char *s, char *out);

/* Shared conservative analysis helpers. */
int line_clobbers_bc(const char *line);
int line_could_use_bc(const char *line);
void find_function_bounds(int from, int *func_start, int *func_end);
void find_function_bounds_any(int from, int *func_start, int *func_end);
void scan_local_func_labels(void);
int is_local_func_label(const char *name);
int line_touches_reg_pair(const char *s, const char *lo, const char *hi,
                                 const char *pair);
int line_touches_bc(const char *s);
int line_touches_de(const char *s);
int line_touches_hl(const char *s);
int line_touches_a(const char *s);

/* Size-mode shared-helper passes. */
int pass_shared_frame_stubs(void);
int pass_lvar_stubs(void);
int pass_svar_stubs(void);
int pass_larg_stubs(void);
int pass_phix_stub(void);
int pass_larg_direct_store(void);
int pass_ldwl_stub(void);
int pass_wand_stub(void);
int pass_icmp_stub(void);
int pass_sxde_stub(void);
int pass_sxhl_stub(void);

/* Terminal relaxation and cleanup passes. */
int jump_target(const char *s, char *out);
int pass_jp_to_jr(void);
int pass_fold_const_sign_extend(void);
int pass_elim_dead_reg16_reload(void);

/* Single-scan micro-pattern dispatcher (peep_pass_once.c). */
int pass_once(void);

/* Shared helpers used across the optimizer and the board passes. */
int is_uncond_jp(const char *s);
int is_jump_line(const char *s);
int label_name_at(int i, char *out);
int line_is_label_name(int i, const char *name);
int peep_is_public_line(const char *s);
int bc_regalloc_claimed_before(int at);
int stride_parse_ld_r_ix_neg(const char *s, char r, int *n);
void strip_label_colon(char *s);
int jump_target_any(const char *s, char *out);
int find_last_loop_back(int body_start, const char *label, int any);
int loop_body_internal_labels_safe(int lo, int hi);
int find_label_line_in_range(const char *name, int lo, int hi);

/* Application-specific board/game passes (peep_pass_minmax.c). */
int peep_in_function_range(const char *func, int *startp, int *endp);
int peep_range_has_debug_annotations(int start, int end);
int pass_posfunc_b_cache(void);
int pass_minmax_winner_result_no_temp(void);
int pass_minmax_score_b_cache(void);
int pass_minmax_loop_ctr_b(void);
int pass_minmax_value_c(void);
int pass_minmax_board_ptr_loop(void);
int pass_minmax_byte_returns(void);
int pass_minmax_pack_frame(void);
int pass_minmax_pack_call(void);
int pass_minmax_save_board_addr(void);
int pass_reuse_board_addr_for_zero_store(void);
int pass_minmax_elim_label_reload(void);
int pass_winner_check_dec_a(void);
int pass_global_board_const_offsets(void);

/* Loop-scoped registerization passes (peep_pass_loops.c). */
int pass_byte_loop_counter_to_reg_c(void);
int pass_word_loop_var_to_reg_bc(void);
int pass_byte_loop_var_to_reg_c(void);
int pass_byte_for_counter_to_reg_c(void);
int pass_byte_for_counter_to_reg_e(void);
int pass_byte_loop_counter_to_reg_iyl(void);
int pass_byte_incr_loop_counter_to_reg_iyl(void);

/* Compiler-tagged temporary spill passes (peep_pass_inline_temp.c). */
int pass_inline_temp_spill_to_stack(void);
int pass_remove_inline_temp_markers(void);

#endif
