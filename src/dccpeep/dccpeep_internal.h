/**
 * @file dccpeep_internal.h
 * @brief Defines the private line-program and pass contract for dccpeep.
 *
 * @par Role
 * Declares shared options, statistics, line/effect/CFG representations,
 * mutation transactions, parsing and analysis helpers, and pass entry points.
 *
 * @par Module map
 * - dccpeep.c: CLI, pass scheduling, core rewrites, and register claims.
 * - peep_lines.c: line ownership, opaque user assembly, mutation, and I/O.
 * - peep_parse.c: exact Z80/M80 text parsers and formatters.
 * - peep_analyze.c: lightweight register and function safety queries.
 * - peep_control_flow.c: label, jump, function, and loop-bound queries.
 * - peep_effects.c: cached line classification and machine effects.
 * - peep_dataflow.c: versioned CFG, basic blocks, and liveness.
 * - peep_frame_alloc.c: analysis-only frame-slot promotion census.
 * - peep_pass_once.c: ordered single-scan local rewrites.
 * - peep_pass_control_flow.c: label and branch rewrite passes.
 * - peep_pass_inline_temp.c: tagged inline-temporary spill rewrites.
 * - peep_pass_loops.c: loop-scoped register promotion.
 * - peep_pass_minmax.c: exact board-game and minimax rewrites.
 * - peep_pass_stubs.c: size-mode shared-helper factoring.
 * - peep_pass_final.c: terminal relaxation and dead-state cleanup.
 *
 * @par Boundary
 * This header is private to dccpeep. Passes mutate only through the line API,
 * user-assembly entries remain opaque, and dccpeep.c alone orders passes.
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

typedef struct PeepOptions {
    int optimize_size;
    int allow_undocumented_z80;
    int stats_enabled;
} PeepOptions;

typedef struct PeepRunStats {
    unsigned long lines_inserted;
    unsigned long lines_deleted;
    int iterations;
} PeepRunStats;

typedef struct PeepLabelIndexEntry {
    const char *name;
    int line;
} PeepLabelIndexEntry;

typedef enum PeepLineKind {
    PEEP_LINE_BLANK,
    PEEP_LINE_COMMENT,
    PEEP_LINE_LABEL,
    PEEP_LINE_DIRECTIVE,
    PEEP_LINE_INSTRUCTION,
    PEEP_LINE_OPAQUE
} PeepLineKind;

typedef enum PeepOpcode {
    PEEP_OPCODE_UNKNOWN,
    PEEP_OPCODE_LD,
    PEEP_OPCODE_JP,
    PEEP_OPCODE_JR,
    PEEP_OPCODE_CALL,
    PEEP_OPCODE_RET,
    PEEP_OPCODE_PUSH,
    PEEP_OPCODE_POP,
    PEEP_OPCODE_ALU,
    PEEP_OPCODE_BLOCK
} PeepOpcode;

enum PeepRegisterMask {
    PEEP_REG_A  = 1u << 0,
    PEEP_REG_B  = 1u << 1,
    PEEP_REG_C  = 1u << 2,
    PEEP_REG_D  = 1u << 3,
    PEEP_REG_E  = 1u << 4,
    PEEP_REG_H  = 1u << 5,
    PEEP_REG_L  = 1u << 6,
    PEEP_REG_IX = 1u << 7,
    PEEP_REG_IY = 1u << 8,
    PEEP_REG_SP = 1u << 9,
    PEEP_REG_BC = PEEP_REG_B | PEEP_REG_C,
    PEEP_REG_DE = PEEP_REG_D | PEEP_REG_E,
    PEEP_REG_HL = PEEP_REG_H | PEEP_REG_L
};

enum PeepFlagMask {
    PEEP_FLAG_C = 1u << 0,
    PEEP_FLAG_Z = 1u << 1,
    PEEP_FLAG_S = 1u << 2,
    PEEP_FLAG_PV = 1u << 3
};

typedef enum PeepOperandKind {
    PEEP_OPERAND_NONE,
    PEEP_OPERAND_REGISTER,
    PEEP_OPERAND_IMMEDIATE,
    PEEP_OPERAND_FRAME,
    PEEP_OPERAND_GLOBAL,
    PEEP_OPERAND_INDIRECT,
    PEEP_OPERAND_LABEL
} PeepOperandKind;

enum PeepMemoryMask {
    PEEP_MEM_NONE     = 0,
    PEEP_MEM_FRAME    = 1u << 0,
    PEEP_MEM_GLOBAL   = 1u << 1,
    PEEP_MEM_STACK    = 1u << 2,
    PEEP_MEM_INDIRECT = 1u << 3,
    PEEP_MEM_OPAQUE   = 1u << 4
};

typedef struct PeepOperand {
    PeepOperandKind kind;
    unsigned registers;
    long immediate;
    int frame_offset;
    int immediate_valid;
    int frame_offset_valid;
} PeepOperand;

typedef struct PeepEffects {
    unsigned reads;
    unsigned writes;
    unsigned flags_read;
    unsigned flags_written;
    unsigned memory_read;
    unsigned memory_written;
    int unknown;
    int control_flow;
} PeepEffects;

typedef struct PeepLineInfo {
    PeepLineKind kind;
    PeepOpcode opcode;
    char mnemonic[8];
    PeepOperand left;
    PeepOperand right;
    PeepEffects effects;
} PeepLineInfo;

typedef struct PeepFlowLine {
    int successors[2];
    int successor_count;
    int block;
    unsigned live_in;
    unsigned live_out;
    unsigned flags_live_in;
    unsigned flags_live_out;
} PeepFlowLine;

typedef struct PeepBasicBlock {
    int start;
    int end;
    int function_start;
    int function_end;
} PeepBasicBlock;

typedef struct PeepIndexes {
    PeepLabelIndexEntry *labels;
    int label_count;
    int label_capacity;
    int *public_functions;
    int public_function_count;
    int *all_functions;
    int all_function_count;
    int function_capacity;
    PeepLineInfo *line_info;
    int line_info_capacity;
    PeepFlowLine *flow_lines;
    int flow_line_capacity;
    PeepBasicBlock *blocks;
    int block_count;
    int block_capacity;
    unsigned long version;
    unsigned long line_info_version;
    unsigned long flow_version;
} PeepIndexes;

typedef struct PeepContext {
    char **lines;
    char **user_asm_original;
    int *line_count;
    unsigned long program_version;
    PeepOptions options;
    PeepRunStats stats;
    PeepIndexes indexes;
} PeepContext;

typedef struct PeepEditTransaction {
    char **lines;
    char **user_asm_original;
    int line_count;
    unsigned long version;
    PeepRunStats stats;
    int active;
} PeepEditTransaction;

extern PeepContext peep_context;
void peep_context_init(void);
void peep_edit_begin(PeepEditTransaction *transaction);
void peep_edit_commit(PeepEditTransaction *transaction);
void peep_edit_rollback(PeepEditTransaction *transaction);

extern char *lines[MAX_LINES];
extern char *user_asm_original[MAX_LINES];
extern int nlines;
extern int input_is_dcc_generated;

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
int peep_parse_ld_de_paren_sym(const char *s, char *sym);
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
const PeepLineInfo *peep_line_info(int line);
const PeepFlowLine *peep_flow_line(int line);
const PeepBasicBlock *peep_basic_block(int block);
int peep_basic_block_count(void);
int peep_registers_dead_after(int line, unsigned registers);
int peep_flags_dead_after(int line, unsigned flags);

/* Analysis-only frame-slot register-allocation census. Runs after structural
 * convergence under -fstats; changes no program text. */
void peep_frame_alloc_analyze(void);

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
int pass_elim_dead_register_loads(void);
int pass_elim_dead_epilogue_cleanup_pops(void);
int pass_elim_redundant_carry_clear(void);

/* Single-scan micro-pattern dispatcher (peep_pass_once.c). */
int pass_once(void);

/* Guard for local-alloc rewrites: HL (the fresh allocation's address) must
 * be provably dead before deleting its definition. Shared between
 * peep_pass_once.c's early N=1/2 rewrite and peep_pass_final.c's
 * post-convergence N=3/4 rewrite (peep_pass_once.c). */
int local_alloc_hl_result_dead(int start);
int pass_local_alloc_wide(void);

/* Shared helpers used across the optimizer and the board passes. */
int is_uncond_jp(const char *s);
int is_jump_line(const char *s);
int label_name_at(int i, char *out);
int line_is_label_name(int i, const char *name);
int peep_is_public_line(const char *s);
int bc_regalloc_claimed_before(int at);
int peep_register_claimed_in_range(unsigned mask, int begin, int end);
int peep_register_claimed_from(unsigned mask, int at);
int peep_register_claimed_in_file(unsigned mask);
int peep_register_available_in_range(
    unsigned mask, int begin, int end, const char *own_tag);
int dcc_iy_claimed_in_file(void);
int iy_loop_borrow_safe(int loop_start, int loop_end,
                        const char *header, const char *exit_target);
/* Interval forms of the same question. dcc publishes its own BC claims as
 * paired "@dcc.reg claim=bc" / "@dcc.reg free=bc" directives, so ownership
 * is a set of intervals rather than a single "claimed from here onward"
 * point; a pass must ask about the span it actually intends to modify. */
int bc_regalloc_claimed_in_range(int begin, int end);
int bc_regalloc_claimed_from(int at);
/* Shared "is this register already spoken for anywhere in this function?"
 * scan. `own_tag`, when non-NULL, names the peep tag the CALLING pass writes,
 * so its own earlier segment-scoped claims in the same function do not veto a
 * later unrelated one. See the definition in dccpeep.c for why everything
 * else that touches the register still counts. */
int peep_reg_used_in_function(int at, const char *own_tag,
                              int (*line_uses_reg)(const char *));
int stride_parse_ld_r_ix_neg(const char *s, char r, int *n);
void strip_label_colon(char *s);
int jump_target_any(const char *s, char *out);
int find_last_loop_back(int body_start, const char *label, int any);
int loop_body_internal_labels_safe(int lo, int hi);
int find_label_line_in_range(const char *name, int lo, int hi);
void peep_indexed_function_bounds(int from, int include_static,
                                  int *func_start, int *func_end);
int is_label_referenced(const char *label);

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
int pass_narrow_bc_loop_bound_to_reg_c(void);
int pass_byte_loop_var_to_reg_c(void);
int pass_byte_for_counter_to_reg_c(void);
int pass_byte_for_counter_to_reg_e(void);
int pass_byte_loop_counter_to_reg_iyl(void);
int pass_byte_incr_loop_counter_to_reg_iyl(void);

/* Compiler-tagged temporary spill passes (peep_pass_inline_temp.c). */
int pass_inline_temp_spill_to_stack(void);
int pass_remove_inline_temp_markers(void);

/* Label and branch rewrite passes (peep_pass_control_flow.c). */
int pass_labels(void);
int pass_branch_over_jump(void);
int pass_jump_thread(void);
int pass_cond_skip_shortcut(void);
int pass_jp_to_plain_ret(void);

#endif
