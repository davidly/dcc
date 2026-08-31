/**
 * @file dcc_mir_machine_call_runners.c
 * @brief Emits exact schedules for call-heavy orchestration functions.
 *
 * @par Role
 * Matches functions dominated by argument packing, ABI-sensitive calls,
 * allocation/file/system call sequences, aggregate-value calls, and
 * call-containing control loops, then emits one complete schedule.
 *
 * @par Key entry point
 * mir_try_emit_call_runners().
 */

#include "dcc_mir_machine_internal.h"

struct MirInlineCallSumSchedule {
    struct Sym *index_function;
    int values_stack_offset;
    int count_stack_offset;
};

enum MirCallSafeLoopKind {
    MIR_CALL_SAFE_COUNTDOWN_SUM,
    MIR_CALL_SAFE_MEMBER_SUM
};

struct MirCallSafeWordLoopSchedule {
    enum MirCallSafeLoopKind kind;
    struct Sym *call_function;
    int first_stack_offset;
    int second_stack_offset;
    int member_offsets[3];
};

struct MirConstantDoWhileSchedule {
    struct Sym *check_function;
    int second_count;
    int third_initial;
    int break_count;
    int fourth_count;
};

struct MirArrowPathSchedule {
    struct Sym *cave;
    struct Sym *adjacent_function;
    struct Sym *random_function;
    struct Sym *flush_function;
    struct Sym *wake_function;
    int game_stack_offset;
    int path_stack_offset;
    int length_stack_offset;
    int cave_offset;
    int location_offset;
    int arrows_offset;
    int string_ids[4];
    char print_name[64];
};

struct MirRoomResolutionSchedule {
    struct Sym *flush_function;
    struct Sym *wake_function;
    struct Sym *random_room_function;
    int game_stack_offset;
    int location_offset;
    int string_ids[3];
    char print_name[64];
};

struct MirGlobalMemsetSchedule {
    struct Sym *function;
    struct Sym *buffer;
    int buffer_offset;
    int size_stack_offset;
    int value_stack_offset;
};

struct MirWordTableRunnerSchedule {
    struct Sym *print_function;
    struct Sym *run_function;
    struct Sym *table;
    int table_offset;
    int start_string_id;
    int done_string_id;
    char print_name[64];
};

struct MirSeekCheckSchedule {
    struct Sym *seek_function;
    struct Sym *fail_function;
    int fd_stack_offset;
    int offset_stack_offset;
    int where_stack_offset;
    int format_string_id;
    char print_name[64];
};

struct MirFileRoundtripSchedule {
    struct Sym *print_function;
    struct Sym *open_function;
    struct Sym *fail_function;
    struct Sym *pattern_function;
    struct Sym *reverse_pattern_function;
    struct Sym *fill_function;
    struct Sym *clear_function;
    struct Sym *check_function;
    struct Sym *write_function;
    struct Sym *read_function;
    struct Sym *sync_function;
    struct Sym *close_function;
    struct Sym *seek_function;
    struct Sym *unlink_function;
    struct Sym *buffer;
    int buffer_offset;
    int size_stack_offset;
    int start_string_id;
    int strings[20];
    char print_names[6][64];
};

struct MirIntelHexLoadSchedule {
    struct Sym *line_function;
    struct Sym *length_function;
    struct Sym *error_function;
    struct Sym *hex_function;
    struct Sym *eof_function;
    struct Sym *memory_function;
    struct Sym *close_function;
    int file_stack_offset;
    int strings[3];
};

struct MirLegalMoveFilterSchedule {
    struct Sym *generate_function;
    struct Sym *copy_function;
    struct Sym *apply_function;
    struct Sym *check_function;
    struct Sym *undo_function;
    struct Sym *move_counts;
    struct Sym *moves;
    struct Sym *temporary_moves;
    struct Sym *side;
    int ply_stack_offset;
    int ply_stride;
    int move_stride;
};

struct MirFormatWalkSchedule {
    struct Sym *print_function;
    struct Sym *putchar_function;
    int format_stack_offset;
    int argument_count_stack_offset;
    int values_stack_offset;
    int integer_format_string_id;
};

struct MirStatementVmSchedule {
    struct Sym *statements;
    struct Sym *statement_count;
    struct Sym *program_counter;
    struct Sym *halted;
    struct Sym *calls;
    struct Sym *call_count;
    struct Sym *loops;
    struct Sym *loop_count;
    struct Sym *symbols;
    struct Sym *memory;
    struct Sym *memory_capacity;
    struct Sym *return_function;
    struct Sym *write_function;
    struct Sym *die_function;
    struct Sym *evaluate_function;
    struct Sym *bump_function;
    struct Sym *assign_function;
    int statement_stride;
    int statement_op_offset;
    int statement_target_offset;
    int statement_symbol_offset;
    int statement_ae_offset;
    int statement_be_offset;
    int statement_ce_offset;
    int statement_action_offset;
    int statement_action_target_offset;
    int statement_action_symbol_offset;
    int statement_action_index_offset;
    int statement_action_rhs_offset;
    int statement_target_count_offset;
    int statement_targets_offset;
    int loop_stride;
    int loop_label_offset;
    int loop_pc_offset;
    int loop_symbol_offset;
    int loop_end_offset;
    int loop_step_offset;
    int symbol_stride;
    int symbol_type_offset;
    int symbol_base_offset;
    int byte_type;
    int call_limit;
    int loop_limit;
    int opcode_count;
    int action_goto;
    int action_return;
    int action_assign;
    int bad_call_string_id;
    int call_stack_string_id;
    int loop_stack_string_id;
    int bounds_string_id;
    int bad_label_string_id;
};

enum {
    MIR_STRADDR = MIR_STRING_ADDRESS,
    MIR_BRFALSE = MIR_BRANCH_FALSE
};

struct MirReadValidateRunner {
    struct Sym *read_function;
    struct Sym *print_function;
    struct Sym *buffer;
    struct Sym *error_object;
    char print_names[9][64];
    int strings[8];
    int offset_stack_offset;
    int chunk_stack_offset;
    int stream_stack_offset;
};

#define MIR_PROMOTION_CHECK_COUNT 49
struct MirPromotionCallRunner {
    struct Sym *check_function;
    struct Sym *print_function;
    struct Sym *failures;
    unsigned long values[MIR_PROMOTION_CHECK_COUNT];
    int check_strings[MIR_PROMOTION_CHECK_COUNT];
    int intro_string;
    int failure_string;
    int success_string;
    char print_names[3][64];
};

#define MIR_LONG_SUBTRACTION_CHECK_COUNT 26
#define MIR_LONG_SUBTRACTION_DIRECT_COUNT 20
struct MirLongSubtractionRunner {
    struct Sym *check_function;
    struct Sym *helper_function;
    struct Sym *print_function;
    struct Sym *global_array;
    struct Sym *failures;
    unsigned long initial_values[4];
    unsigned long addition_values[2];
    unsigned long pointer_values[2];
    unsigned long global_values[2];
    unsigned long while_values[2];
    unsigned long for_values[2];
    unsigned long helper_values[2];
    unsigned long direct_addends[7];
    unsigned long while_condition_addend;
    unsigned long while_step;
    unsigned long for_step;
    unsigned long helper_addend;
    int while_limit;
    int for_limit;
    int check_strings[MIR_LONG_SUBTRACTION_CHECK_COUNT];
    int check_wants[MIR_LONG_SUBTRACTION_CHECK_COUNT];
    int success_string;
    int failure_string;
    char print_names[2][64];
};

struct MirLongSubtractionEdge {
    int instruction;
    int first;
    int second;
};

struct MirLongSubtractionIndex {
    int instruction;
    int index;
};

struct MirLongSubtractionBinary {
    int instruction;
    int operation;
    int operand_width;
};

static const unsigned char mir_long_subtraction_opcodes[] = {
    MIR_LABEL, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_CONST,
    MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST,
    MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_CONST, MIR_NOP,
    MIR_STORE, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
    MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP,
    MIR_STORE, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
    MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP,
    MIR_STORE, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
    MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP,
    MIR_STORE, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
    MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP,
    MIR_STORE, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
    MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP,
    MIR_STORE, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
    MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP,
    MIR_STORE, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
    MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP,
    MIR_STORE, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
    MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP,
    MIR_STORE, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
    MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP,
    MIR_STORE, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
    MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_CONST,
    MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT,
    MIR_CONST, MIR_NOP, MIR_STORE, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP,
    MIR_CONST, MIR_BINARY, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE,
    MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG,
    MIR_CONST, MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_ADDRESS, MIR_CONST,
    MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
    MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP,
    MIR_STORE, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_CONST, MIR_BINARY,
    MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP,
    MIR_STORE, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG,
    MIR_CALL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
    MIR_NOP, MIR_CONST, MIR_BINARY, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY,
    MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD,
    MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_ADDRESS,
    MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_ADDRESS, MIR_CONST,
    MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL,
    MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_ADDRESS,
    MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST,
    MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_NOP, MIR_STORE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LOAD,
    MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_LOAD, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY,
    MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD,
    MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LOAD,
    MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_LOAD, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY,
    MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD,
    MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LOAD,
    MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_LOAD, MIR_CONST,
    MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL,
    MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_ADDRESS,
    MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST,
    MIR_STORE_INDIRECT, MIR_CONST, MIR_NOP, MIR_STORE, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
    MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP,
    MIR_STORE, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG,
    MIR_CALL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
    MIR_NOP, MIR_CONST, MIR_BINARY, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY,
    MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD,
    MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST,
    MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_CONST, MIR_NOP,
    MIR_STORE, MIR_LABEL, MIR_NOP, MIR_PHI, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
    MIR_CONST, MIR_BINARY, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE,
    MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
    MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP,
    MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG,
    MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_CONST,
    MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT,
    MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_PHI, MIR_ADDRESS,
    MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY,
    MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT,
    MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_NOP,
    MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
    MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
    MIR_CONST, MIR_STORE_INDIRECT, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
    MIR_ARG, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL, MIR_ARG,
    MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
    MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL,
    MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_CONST,
    MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_ARG, MIR_ADDRESS, MIR_CONST,
    MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
    MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
    MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_LABEL,
    MIR_LOAD, MIR_RETURN,
};

static const struct MirLongSubtractionEdge
mir_long_subtraction_edges[] = {
    {34, 38, -1}, {58, 62, -1}, {82, 86, -1}, {106, 110, -1},
    {130, 134, -1}, {154, 158, -1}, {178, 182, -1},
    {202, 206, -1}, {226, 230, -1}, {250, 254, -1},
    {287, 291, -1}, {314, 318, -1}, {341, 345, -1},
    {368, 372, -1}, {395, 399, -1}, {432, 436, -1},
    {456, 460, -1}, {483, 487, -1}, {517, 521, -1},
    {544, 548, -1}, {571, 548, 603}, {583, 605, -1},
    {598, 601, -1}, {600, 605, -1}, {604, 569, -1},
    {638, 605, 664}, {648, 670, -1}, {659, 662, -1},
    {661, 670, -1}, {669, 635, -1}, {747, 753, -1},
    {752, 759, -1}
};

static const struct MirLongSubtractionIndex
mir_long_subtraction_indices[] = {
    {3, 0}, {8, 1}, {13, 2}, {19, 3}, {27, 0}, {31, 1},
    {51, 1}, {55, 0}, {75, 1}, {79, 0}, {99, 0}, {103, 1},
    {123, 0}, {127, 1}, {147, 0}, {151, 0}, {171, 1},
    {175, 0}, {195, 0}, {199, 1}, {219, 2}, {223, 0},
    {243, 1}, {247, 3}, {264, 0}, {269, 1}, {277, 0},
    {284, 1}, {304, 0}, {311, 1}, {331, 1}, {338, 0},
    {358, 0}, {365, 1}, {385, 0}, {392, 1}, {409, 0},
    {414, 1}, {425, 0}, {429, 1}, {449, 1}, {453, 0},
    {473, 0}, {480, 1}, {497, 0}, {502, 1}, {510, 0},
    {514, 1}, {534, 0}, {541, 1}, {558, 0}, {563, 1},
    {574, 0}, {580, 1}, {590, 0}, {624, 0}, {629, 1},
    {641, 1}, {645, 0}, {651, 1}, {682, 0}, {687, 1},
    {694, 0}, {699, 1}, {711, 1}, {716, 0}, {728, 0},
    {736, 1}
};

static const struct MirLongSubtractionBinary
mir_long_subtraction_binaries[] = {
    {33, '<', 4}, {57, '<', 4}, {81, '>', 4},
    {105, '>', 4}, {129, TOK_LE, 4}, {153, TOK_LE, 4},
    {177, TOK_GE, 4}, {201, TOK_GE, 4}, {225, '<', 4},
    {249, '<', 4}, {281, '+', 4}, {286, '<', 4},
    {308, '+', 4}, {313, '>', 4}, {335, '+', 4},
    {340, '<', 4}, {362, '+', 4}, {367, TOK_LE, 4},
    {389, '+', 4}, {394, TOK_GE, 4}, {431, '<', 4},
    {455, '<', 4}, {477, '+', 4}, {482, '<', 4},
    {516, '<', 4}, {538, '+', 4}, {543, '<', 4},
    {577, '+', 4}, {582, '<', 4}, {586, '+', 2},
    {593, '+', 4}, {597, '>', 2}, {610, '>', 2},
    {647, '<', 4}, {654, '+', 4}, {658, '>', 2},
    {667, '+', 2}, {675, '>', 2}, {732, '+', 4},
    {746, TOK_EQ, 2}
};

struct MirPromotionOperation {
    int instruction;
    int type;
    int operand_type;
    int operation;
};

enum MirStatementVmFrameOffset {
    MIR_STMT_END = -2,
    MIR_STMT_POINTER = -4,
    MIR_STMT_VALUE = -6,
    MIR_STMT_INDEX = -8,
    MIR_STMT_TEMP = -10
};

struct MirNullableStringCheckSchedule {
    struct Sym *compare_function;
    struct Sym *print_function;
    struct Sym *failure_count;
    int name_stack_offset;
    int got_stack_offset;
    int want_stack_offset;
    int null_string;
    int format_string;
    char compare_name[64];
    char print_name[64];
};

struct MirNullableStringFailureSchedule {
    struct Sym *compare_function;
    struct Sym *print_function;
    struct Sym *failure_count;
    int label_stack_offset;
    int got_stack_offset;
    int want_stack_offset;
    int null_string;
    int format_string;
    char compare_name[64];
    char print_name[64];
};

struct MirConditionalParameterSchedule {
    int condition_stack_offset;
    int value_stack_offset;
    int assigned_value;
    int addend;
};

struct MirArgvPrintSchedule {
    struct Sym *print_function;
    int argc_stack_offset;
    int argv_stack_offset;
    int item_string;
    int count_string;
    int done_string;
    char item_name[64];
    char count_name[64];
    char done_name[64];
};

struct MirListPrependSchedule {
    struct Sym *allocate_function;
    int head_stack_offset;
    int value_stack_offset;
    int allocation_size;
    int value_offset;
    int next_offset;
};

struct MirListReverseSchedule {
    int head_stack_offset;
    int next_offset;
};

struct MirStructReturnMemberSchedule {
    struct Sym *pair_function;
    struct Sym *member_function;
    struct Sym *print_function;
    int pair_size;
    int low_offset;
    int high_offset;
    int format_string;
    char print_name[64];
};

struct MirPointerTableRunnerSchedule {
    struct Sym *table;
    struct Sym *total;
    struct Sym *consume_function;
    struct Sym *print_function;
    int table_offset;
    int total_offset;
    int format_string;
    char print_name[64];
};

struct MirAddressCheckRunnerSchedule {
    struct Sym *check_function;
    struct Sym *print_function;
    struct Sym *failure_count;
    int strings[12];
    int values[12];
    int success_string;
    char print_name[64];
};

struct MirQualifierRunnerSchedule {
    struct Sym *check_function;
    struct Sym *add_function;
    struct Sym *print_function;
    struct Sym *failure_count;
    int check_strings[7];
    int failure_string;
    int success_string;
    char failure_name[64];
    char success_name[64];
};

struct MirUnionValueRunnerSchedule {
    struct Sym *global_union;
    struct Sym *global_name;
    struct Sym *global_array;
    struct Sym *sum_function;
    struct Sym *make_function;
    struct Sym *copy_function;
    struct Sym *print_function;
    int strings[8];
    char print_names[8][64];
};

struct MirStructValueRunnerSchedule {
    struct Sym *make_pair;
    struct Sym *make_wrap;
    struct Sym *make_big;
    struct Sym *sum_pair;
    struct Sym *sum_two;
    struct Sym *sum_mix;
    struct Sym *sum_wrap;
    struct Sym *sum_big;
    struct Sym *copy_pair;
    struct Sym *return_pair;
    struct Sym *assign_return_pair;
    struct Sym *copy_wrap;
    struct Sym *return_wrap;
    struct Sym *fill_big;
    struct Sym *return_big;
    struct Sym *print_function;
    struct Sym *global_pair;
    struct Sym *global_pair_array;
    struct Sym *global_wrap;
    struct Sym *global_big;
    struct Sym *global_big_array;
    int strings[17];
    char print_names[17][64];
};

struct MirInlineNestRunnerSchedule {
    struct Sym *buffer;
    struct Sym *count;
    struct Sym *give_function;
    int first_value;
    int second_value;
    int scale;
};

struct MirInlineParameterCallSchedule {
    struct Sym *value_function;
    struct Sym *array;
    struct Sym *print_function;
    int first_arguments[2];
    int second_arguments[2];
    int array_offset;
    int store_value;
    int format_string;
    char print_name[64];
};

struct MirZeroAllocationSchedule {
    struct Sym *allocate_function;
    struct Sym *free_function;
    struct Sym *failure_function;
    int failure_string;
    int stored_byte;
};

struct MirAllocatorReuseSchedule {
    struct Sym *allocate_function;
    struct Sym *free_function;
    struct Sym *failure_function;
    int sizes[3];
    int failure_strings[3];
};

struct MirAllocationGuardSchedule {
    struct Sym *allocate_function;
    struct Sym *free_function;
    struct Sym *failure_function;
    int guard_size;
    int rejected_size;
    int failure_strings[2];
};

struct MirAllocatorTrimSchedule {
    struct Sym *allocate_function;
    struct Sym *free_function;
    struct Sym *failure_function;
    int first_size;
    int final_size;
    int failure_strings[3];
};

enum MirAllocatorCoalesceKind {
    MIR_ALLOCATOR_BRIDGE = 1,
    MIR_ALLOCATOR_REVERSE
};

struct MirAllocatorCoalesceSchedule {
    enum MirAllocatorCoalesceKind kind;
    struct Sym *allocate_function;
    struct Sym *free_function;
    struct Sym *failure_function;
    struct Sym *fill_function;
    int allocation_size;
    int merged_size;
    int fill_value;
    int failure_strings[2];
};

struct MirBiosCallSchedule {
    struct Sym *bios_function;
    struct Sym *bioshl_function;
    struct Sym *print_function;
    int strings[7];
    int status_function;
    int output_function;
    int output_characters[5];
    char bios_name[64];
    char bioshl_name[64];
    char print_name[64];
};

struct MirExecArgumentSchedule {
    struct Sym *compare_function;
    struct Sym *child_function;
    struct Sym *print_function;
    struct Sym *exec_function;
    struct Sym *execv_function;
    int argc_stack_offset;
    int argv_stack_offset;
    int strings[11];
    char compare_name[64];
    char print_name[64];
    char exec_name[64];
    char execv_name[64];
};

struct MirTemporaryFileSchedule {
    struct Sym *tmpnam_function;
    struct Sym *print_function;
    struct Sym *check_function;
    struct Sym *compare_function;
    struct Sym *tmpfile_function;
    struct Sym *puts_function;
    struct Sym *rewind_function;
    struct Sym *gets_function;
    struct Sym *close_function;
    struct Sym *failure_count;
    int strings[11];
    int frame_bytes;
    int buffer_offset;
    int read_buffer_offset;
    int first_name_offset;
    int second_name_offset;
    int stream_offset;
    char tmpnam_name[64];
    char print_name[64];
    char check_name[64];
    char compare_name[64];
    char tmpfile_name[64];
    char puts_name[64];
    char rewind_name[64];
    char gets_name[64];
    char close_name[64];
};

struct MirCallRecoveryEdge {
    int instruction;
    int target;
};

struct MirCallRecoveryPhi {
    int instruction;
    int first;
    int second;
    int first_predecessor;
    int second_predecessor;
};

static void mir_temp_store_hl(MirStream *out, int offset);

static void mir_temp_load_hl(MirStream *out, int offset);

static int mir_machine_constant_value(
    int value, long *constant_out, int depth)
{
    const struct MirInsn *definition;
    long left;
    long right;

    if (depth > 16)
        return 0;
    definition = mir_definition(value);
    if (definition == NULL)
        return 0;
    if (definition->opcode == MIR_CONST) {
        *constant_out = definition->immediate;
        return 1;
    }
    if (definition->opcode == MIR_UNARY &&
        definition->immediate == 0)
        return mir_machine_constant_value(
            definition->src1, constant_out, depth + 1);
    if (definition->opcode != MIR_BINARY ||
        !mir_machine_constant_value(
            definition->src1, &left, depth + 1) ||
        !mir_machine_constant_value(
            definition->src2, &right, depth + 1))
        return 0;
    return mir_fold_constant_binary(
        (int)definition->immediate, left, right,
        definition->type, constant_out);
}

static int mir_machine_single_call_argument(
    const struct MirInsn *call, int *argument)
{
    int count = 0;
    int instruction;

    *argument = -1;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *arg = &mir.insns[instruction];

        if (arg->opcode != MIR_ARG ||
            arg->secondary_offset != call->secondary_offset)
            continue;
        if (arg->immediate != 0 || *argument >= 0)
            return 0;
        *argument = arg->src1;
        ++count;
    }
    return count == 1;
}

static int mir_machine_three_call_arguments(
    const struct MirInsn *call, int arguments[3])
{
    int count = 0;
    int instruction;

    arguments[0] = arguments[1] = arguments[2] = -1;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *arg = &mir.insns[instruction];
        int index;

        if (arg->opcode != MIR_ARG ||
            arg->secondary_offset != call->secondary_offset)
            continue;
        index = (int)arg->immediate;
        if (index < 0 || index >= 3 || arguments[index] >= 0)
            return 0;
        arguments[index] = arg->src1;
        ++count;
    }
    return count == 3;
}

static int mir_machine_two_call_arguments(
    const struct MirInsn *call, int arguments[2])
{
    int count = 0;
    int instruction;

    arguments[0] = arguments[1] = -1;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *arg = &mir.insns[instruction];
        int index;

        if (arg->opcode != MIR_ARG ||
            arg->secondary_offset != call->secondary_offset)
            continue;
        index = (int)arg->immediate;
        if (index < 0 || index >= 2 || arguments[index] >= 0)
            return 0;
        arguments[index] = arg->src1;
        ++count;
    }
    return count == 2;
}

static int mir_machine_call_arguments(
    const struct MirInsn *call, int expected_count, int *arguments)
{
    int count = 0;
    int instruction;
    int item;

    for (item = 0; item < expected_count; ++item)
        arguments[item] = -1;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *arg = &mir.insns[instruction];
        int index;

        if (arg->opcode != MIR_ARG ||
            arg->secondary_offset != call->secondary_offset)
            continue;
        index = (int)arg->immediate;
        if (index < 0 || index >= expected_count ||
            arguments[index] >= 0)
            return 0;
        arguments[index] = arg->src1;
        ++count;
    }
    return count == expected_count;
}

static int mir_memory_runner_word_type(int type, int is_unsigned)
{
    return type_ptr_depth(type) == 0 &&
           (type & 15) == TYPE_INT &&
           ((type & TYPE_UNSIGNED) != 0) == is_unsigned &&
           type_size(type) == 2;
}

static int mir_call_runner_has_volatile_memory(void)
{
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_ADDRESS:
        case MIR_LOAD:
        case MIR_STORE:
            if ((insn->memory_flags & (1 | 8)) != 0 ||
                !mir_machine_named_nonvolatile(insn))
                return 1;
            break;
        case MIR_MEMBER_ADDRESS:
        case MIR_INDEX_ADDRESS:
        case MIR_LOAD_INDIRECT:
        case MIR_STORE_INDIRECT:
        case MIR_COPY_AGGREGATE:
            if ((insn->memory_flags & (1 | 8)) != 0)
                return 1;
            break;
        default:
            break;
        }
    }
    return 0;
}

static int mir_memory_runner_pointer_type(
    int type, int depth, int base_type)
{
    return type_ptr_depth(type) == depth &&
           (type & 15) == base_type &&
           type_size(type) == 2;
}

static struct Sym *mir_memory_runner_call_function(
    int instruction, int variadic, int argument_count)
{
    const struct MirInsn *call = &mir.insns[instruction];
    struct Sym *function;
    const char *assembly_name;

    if (call->opcode != MIR_CALL || call->src1 >= 0 ||
        ((call->memory_flags & MIR_CALL_FLAG_VARIADIC) != 0) != variadic ||
        (call->memory_flags & MIR_CALL_FLAG_FORMAT_RUNTIME) != 0 ||
        (function = find_global(call->name)) == NULL ||
        function->storage != SC_FUNC || function->is_funcptr ||
        function->is_noreturn || !function->has_proto ||
        function->proto_variadic != variadic ||
        function->proto_nargs != argument_count)
        return NULL;
    assembly_name = asm_name_for(sym_asm_name(function));
    if (call->base_name[0] != 0 &&
        strcmp(call->base_name, assembly_name))
        return NULL;
    return function;
}

static struct Sym *mir_abort_runner_function(
    int instruction, int variadic, int argument_count,
    int noreturn)
{
    const struct MirInsn *call = &mir.insns[instruction];
    struct Sym *function;
    const char *assembly_name;

    if (call->opcode != MIR_CALL || call->src1 >= 0 ||
        call->memory_flags !=
            (variadic ? MIR_CALL_FLAG_VARIADIC : 0) ||
        (function = find_global(call->name)) == NULL ||
        function->storage != SC_FUNC ||
        function->is_funcptr ||
        function->is_noreturn != noreturn ||
        !function->has_proto ||
        function->proto_variadic != variadic ||
        function->proto_nargs != argument_count ||
        call->type != function->type)
        return NULL;
    assembly_name = asm_name_for(sym_asm_name(function));
    if (call->base_name[0] != 0 &&
        strcmp(call->base_name, assembly_name))
        return NULL;
    return function;
}

static int mir_read_validate_word_type(int type, int is_unsigned)
{
    return type_ptr_depth(type) == 0 &&
           (type & 15) == TYPE_INT &&
           ((type & TYPE_UNSIGNED) != 0) == is_unsigned &&
           type_size(type) == 2;
}

static int mir_read_validate_long_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           (type & 15) == TYPE_LONG &&
           (type & TYPE_UNSIGNED) == 0 &&
           type_size(type) == 4;
}

static int mir_read_validate_pointer_type(int type, int base_type)
{
    return type_ptr_depth(type) == 1 &&
           (type & 15) == base_type &&
           type_size(type) == 2;
}

static int mir_read_validate_parameter(
    int instruction, int expected_size, int *stack_offset)
{
    int type;
    int storage;
    int memory_offset;

    if (mir.insns[instruction].opcode != MIR_PARAM ||
        !mir_scalar_memory_location(
            &mir.insns[instruction], &type, &storage, &memory_offset) ||
        storage != SC_PARAM || type_size(type) != expected_size)
        return 0;
    *stack_offset = memory_offset - 2;
    return *stack_offset >= 0;
}

static int mir_read_validate_effective_call(
    struct MirReadValidateRunner *plan, int slot, int instruction,
    int ordinal, int argument_count, const int *definitions)
{
    const struct MirInsn *call = &mir.insns[instruction];
    const char *call_name;
    int arguments[3];
    int item;

    if (slot < 0 || slot >= 9 || argument_count < 1 ||
        argument_count > 3 ||
        call->opcode != MIR_CALL || call->src1 >= 0 ||
        call->secondary_offset != ordinal ||
        call->memory_flags != MIR_CALL_FLAG_VARIADIC ||
        !mir_read_validate_word_type(call->type, 0) ||
        !mir_machine_call_arguments(
            call, argument_count, arguments))
        return 0;
    if (plan->print_function == NULL) {
        plan->print_function = find_global(call->name);
        if (plan->print_function == NULL ||
            plan->print_function->storage != SC_FUNC ||
            plan->print_function->is_funcptr ||
            plan->print_function->is_noreturn ||
            !plan->print_function->has_proto ||
            plan->print_function->proto_nargs != 1 ||
            !plan->print_function->proto_variadic ||
            !mir_read_validate_word_type(
                plan->print_function->type, 0) ||
            !mir_read_validate_pointer_type(
                plan->print_function->proto_types[0], TYPE_CHAR))
            return 0;
    } else if (find_global(call->name) != plan->print_function) {
        return 0;
    }
    if (call->type != plan->print_function->type)
        return 0;
    for (item = 0; item < argument_count; ++item)
        if (arguments[item] != mir.insns[definitions[item]].dst)
            return 0;
    call_name = call->base_name[0] != 0
        ? call->base_name
        : asm_name_for(sym_asm_name(plan->print_function));
    snprintf(plan->print_names[slot],
             sizeof(plan->print_names[slot]), "%s", call_name);
    return 1;
}

static int mir_read_validate_buffer_address(
    int instruction, struct Sym *buffer)
{
    struct Sym *symbol;
    long offset;

    return mir.insns[instruction].opcode == MIR_ADDRESS &&
           mir_machine_global_address_offset(
               mir.insns[instruction].dst, &symbol, &offset, 0) &&
           symbol == buffer && offset == 0 &&
           mir_read_validate_pointer_type(
               mir.insns[instruction].type, TYPE_CHAR) &&
           (mir.insns[instruction].memory_flags & (1 | 8)) == 0;
}

static int mir_read_validate_byte_access(
    int address, int index_constant, int index_address, int load,
    struct Sym *buffer, int expected_index)
{
    return mir_read_validate_buffer_address(address, buffer) &&
           mir_machine_constant_equals(
               mir.insns[index_constant].dst, expected_index) &&
           mir.insns[index_address].src1 ==
               mir.insns[address].dst &&
           mir.insns[index_address].src2 ==
               mir.insns[index_constant].dst &&
           mir.insns[index_address].immediate == 1 &&
           mir.insns[index_address].memory_size == 1 &&
           mir.insns[index_address].memory_flags == 0 &&
           mir.insns[load].src1 ==
               mir.insns[index_address].dst &&
           mir.insns[load].memory_size == 1 &&
           mir.insns[load].memory_flags == 0 &&
           type_ptr_depth(mir.insns[load].type) == 0 &&
           (mir.insns[load].type & 15) == TYPE_CHAR &&
           (mir.insns[load].type & TYPE_UNSIGNED) == 0 &&
           type_size(mir.insns[load].type) == 1;
}

static int mir_read_validate_byte_comparison(
    int unary, int constant, int comparison,
    int load, int expected, int constant_first)
{
    const struct MirInsn *convert = &mir.insns[unary];
    const struct MirInsn *compare = &mir.insns[comparison];

    return convert->immediate == 0 &&
           convert->src1 == mir.insns[load].dst &&
           mir_read_validate_word_type(convert->type, 0) &&
           mir_machine_constant_equals(
               mir.insns[constant].dst, expected) &&
           compare->immediate == TOK_NE &&
           compare->src1 ==
               mir.insns[constant_first ? constant : unary].dst &&
           compare->src2 ==
               mir.insns[constant_first ? unary : constant].dst &&
           mir_read_validate_word_type(compare->type, 0);
}

static int mir_read_validate_branch(int instruction, int target)
{
    return mir.insns[instruction].src1 ==
               mir.insns[instruction - 1].dst &&
           mir.insns[instruction].label ==
               mir.insns[target].label;
}

static int mir_match_read_validate_runner(
    struct MirReadValidateRunner *plan)
{
    static const int expected_opcodes[151] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_ADDRESS, MIR_NOP,
        MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG, MIR_NOP, MIR_NOP,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_UNARY, MIR_STORE,
        MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_NOP, MIR_BINARY, MIR_BRFALSE, MIR_LABEL,
        MIR_STRADDR, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRFALSE,
        MIR_LABEL, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRFALSE,
        MIR_STRADDR, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_BRFALSE, MIR_STRADDR, MIR_ARG, MIR_CALL, MIR_LABEL,
        MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRFALSE, MIR_STRADDR,
        MIR_ARG, MIR_CALL, MIR_LABEL, MIR_NOP, MIR_JUMP, MIR_LABEL,
        MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRFALSE, MIR_LABEL,
        MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRFALSE, MIR_STRADDR,
        MIR_ARG, MIR_CALL, MIR_LABEL, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_BRFALSE, MIR_STRADDR, MIR_ARG, MIR_CALL, MIR_LABEL,
        MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_BINARY,
        MIR_BRFALSE, MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_LABEL, MIR_CONST, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_BINARY, MIR_BRFALSE, MIR_STRADDR,
        MIR_ARG, MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_ARG,
        MIR_CALL, MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_LABEL, MIR_NOP,
        MIR_LABEL
    };
    static const int string_instructions[8] = {
        18, 30, 51, 63, 75, 96, 108, 123
    };
    static const struct {
        int instruction;
        int ordinal;
        int argument_count;
        int definitions[3];
    } print_calls[9] = {
        {24, 1, 3, {18, 1, 16}},
        {34, 2, 2, {30, 32, 0}},
        {53, 3, 1, {51, 0, 0}},
        {65, 4, 1, {63, 0, 0}},
        {77, 5, 1, {75, 0, 0}},
        {98, 6, 1, {96, 0, 0}},
        {110, 7, 1, {108, 0, 0}},
        {127, 8, 2, {123, 1, 0}},
        {144, 9, 2, {137, 142, 0}}
    };
    static const struct {
        int address;
        int index_constant;
        int index_address;
        int load;
        int expected_index;
    } byte_accesses[7] = {
        {43, 44, 45, 46, 0},
        {55, 56, 57, 58, 127},
        {67, 68, 69, 70, 128},
        {88, 89, 90, 91, 0},
        {100, 101, 102, 103, 127},
        {116, 117, 118, 119, 0},
        {130, 131, 132, 133, 511}
    };
    static const struct {
        int unary;
        int constant;
        int comparison;
        int load;
        int expected;
        int constant_first;
    } byte_comparisons[7] = {
        {48, 47, 49, 46, 107, 0},
        {60, 59, 61, 58, 107, 0},
        {72, 71, 73, 70, 0, 0},
        {93, 92, 94, 91, 106, 0},
        {105, 104, 106, 103, 26, 0},
        {120, 115, 121, 119, 0, 1},
        {134, 129, 135, 133, 0, 1}
    };
    int arguments[4];
    int memory_type;
    int memory_storage;
    int memory_offset;
    long global_offset;
    int instruction;
    int item;
    int previous;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 151 || mir_cfg_block_count() != 17 ||
        mir.has_vla || mir.local_bytes != 2 ||
        mir.aggregate_temp_bytes != 0 ||
        type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_VOID)
        return mir_machine_reject(
            "read-validate-runner", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
                expected_opcodes[instruction])
            return mir_machine_reject(
                "read-validate-runner", "opcode");

    if (!mir_read_validate_parameter(
            1, 4, &plan->offset_stack_offset) ||
        !mir_read_validate_parameter(
            2, 2, &plan->chunk_stack_offset) ||
        !mir_read_validate_parameter(
            3, 2, &plan->stream_stack_offset) ||
        plan->offset_stack_offset != 2 ||
        plan->chunk_stack_offset != 6 ||
        plan->stream_stack_offset != 8 ||
        !mir_read_validate_long_type(mir.insns[1].type) ||
        !mir_read_validate_word_type(mir.insns[2].type, 0) ||
        !mir_read_validate_pointer_type(
            mir.insns[3].type, TYPE_INT))
        return mir_machine_reject(
            "read-validate-runner", "parameters");

    plan->read_function =
        mir_abort_runner_function(15, 0, 4, 0);
    if (plan->read_function == NULL)
        return mir_machine_reject(
            "read-validate-runner", "read-function");
    if (!mir_read_validate_word_type(
            plan->read_function->type, 1) ||
        !mir_read_validate_pointer_type(
            plan->read_function->proto_types[0], TYPE_VOID) ||
        !mir_read_validate_word_type(
            plan->read_function->proto_types[1], 1) ||
        !mir_read_validate_word_type(
            plan->read_function->proto_types[2], 1) ||
        !mir_read_validate_pointer_type(
            plan->read_function->proto_types[3], TYPE_INT))
        return mir_machine_reject(
            "read-validate-runner", "read-type");
    if (!mir_machine_call_arguments(
            &mir.insns[15], 4, arguments))
        return mir_machine_reject(
            "read-validate-runner", "read-argument-count");
    if (arguments[0] != mir.insns[4].dst ||
        arguments[1] != mir.insns[7].dst ||
        arguments[2] != mir.insns[2].dst ||
        arguments[3] != mir.insns[13].dst)
        return mir_machine_reject(
            "read-validate-runner", "read-argument-identity");
    if (!mir_machine_constant_equals(
            mir.insns[7].dst, 1) ||
        !mir_machine_same_location(
            &mir.insns[3], &mir.insns[13]))
        return mir_machine_reject(
            "read-validate-runner", "read-argument-value");

    for (item = 0; item < 9; ++item)
        if (!mir_read_validate_effective_call(
                plan, item, print_calls[item].instruction,
                print_calls[item].ordinal,
                print_calls[item].argument_count,
                print_calls[item].definitions))
            return mir_machine_reject(
                "read-validate-runner", "print-call");
    if (plan->print_function == plan->read_function)
        return mir_machine_reject(
            "read-validate-runner", "function-alias");

    for (item = 0; item < 8; ++item) {
        const struct MirInsn *string =
            &mir.insns[string_instructions[item]];

        if (!mir_read_validate_pointer_type(
                string->type, TYPE_CHAR) ||
            string->immediate < 0)
            return mir_machine_reject(
                "read-validate-runner", "string");
        plan->strings[item] = (int)string->immediate;
        for (previous = 0; previous < item; ++previous)
            if (plan->strings[item] ==
                    plan->strings[previous])
                return mir_machine_reject(
                    "read-validate-runner", "string-alias");
    }
    if (mir.insns[137].immediate != plan->strings[7] ||
        !mir_read_validate_pointer_type(
            mir.insns[137].type, TYPE_CHAR))
        return mir_machine_reject(
            "read-validate-runner", "string-reuse");

    if (!mir_machine_global_address_offset(
            mir.insns[4].dst, &plan->buffer,
            &global_offset, 0) ||
        global_offset != 0 || plan->buffer == NULL ||
        plan->buffer->storage == SC_FUNC ||
        !plan->buffer->is_defined ||
        plan->buffer->is_volatile ||
        !plan->buffer->is_array ||
        plan->buffer->is_vla ||
        plan->buffer->dim_count != 1 ||
        plan->buffer->dims[0] != 512 ||
        plan->buffer->array_len != 512 ||
        plan->buffer->elem_size != 1 ||
        plan->buffer->size != 512 ||
        type_ptr_depth(plan->buffer->type) != 0 ||
        (plan->buffer->type & 15) != TYPE_CHAR ||
        (plan->buffer->type & TYPE_UNSIGNED) != 0 ||
        !mir_read_validate_buffer_address(4, plan->buffer) ||
        type_size(plan->buffer->type) != 1)
        return mir_machine_reject(
            "read-validate-runner", "buffer");
    for (item = 0; item < 7; ++item)
        if (!mir_read_validate_byte_access(
                byte_accesses[item].address,
                byte_accesses[item].index_constant,
                byte_accesses[item].index_address,
                byte_accesses[item].load,
                plan->buffer,
                byte_accesses[item].expected_index))
            return mir_machine_reject(
                "read-validate-runner", "buffer-access");
    for (item = 0; item < 7; ++item)
        if (!mir_read_validate_byte_comparison(
                byte_comparisons[item].unary,
                byte_comparisons[item].constant,
                byte_comparisons[item].comparison,
                byte_comparisons[item].load,
                byte_comparisons[item].expected,
                byte_comparisons[item].constant_first))
            return mir_machine_reject(
                "read-validate-runner", "buffer-check");

    if (!mir_scalar_memory_location(
            &mir.insns[17], &memory_type,
            &memory_storage, &memory_offset) ||
        memory_storage != SC_LOCAL || memory_offset != -2 ||
        !mir_read_validate_word_type(memory_type, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[17]) ||
        mir.insns[16].immediate != 0 ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        !mir_read_validate_word_type(mir.insns[16].type, 0) ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        !mir_machine_same_location(
            &mir.insns[17], &mir.insns[22]) ||
        !mir_machine_same_location(
            &mir.insns[17], &mir.insns[26]) ||
        !mir_machine_constant_equals(
            mir.insns[25].dst, 0) ||
        mir.insns[27].immediate != TOK_EQ ||
        mir.insns[27].src1 != mir.insns[25].dst ||
        mir.insns[27].src2 != mir.insns[16].dst ||
        !mir_read_validate_word_type(mir.insns[27].type, 0))
        return mir_machine_reject(
            "read-validate-runner", "result");

    if (!mir_machine_named_nonvolatile(&mir.insns[32]) ||
        (plan->error_object =
             find_global(mir.insns[32].name)) == NULL ||
        plan->error_object->storage == SC_FUNC ||
        plan->error_object->is_array ||
        plan->error_object->is_volatile ||
        !mir_read_validate_word_type(
            plan->error_object->type, 0) ||
        mir.insns[32].type != plan->error_object->type)
        return mir_machine_reject(
            "read-validate-runner", "error-object");

    if (!mir_machine_constant_equals(
            mir.insns[39].dst, 512) ||
        mir.insns[40].immediate != TOK_EQ ||
        mir.insns[40].src1 != mir.insns[39].dst ||
        mir.insns[40].src2 != mir.insns[1].dst ||
        !mir_machine_constant_equals(
            mir.insns[84].dst, 8192) ||
        mir.insns[85].immediate != TOK_EQ ||
        mir.insns[85].src1 != mir.insns[84].dst ||
        mir.insns[85].src2 != mir.insns[1].dst ||
        !mir_machine_constant_equals(
            mir.insns[141].dst, 511) ||
        mir.insns[142].immediate != '+' ||
        mir.insns[142].src1 != mir.insns[1].dst ||
        mir.insns[142].src2 != mir.insns[141].dst ||
        !mir_read_validate_long_type(mir.insns[142].type))
        return mir_machine_reject(
            "read-validate-runner", "offset-flow");

    if (!mir_read_validate_branch(28, 36) ||
        mir.insns[35].label != mir.insns[150].label ||
        !mir_read_validate_branch(41, 81) ||
        !mir_read_validate_branch(50, 54) ||
        !mir_read_validate_branch(62, 66) ||
        !mir_read_validate_branch(74, 148) ||
        mir.insns[80].label != mir.insns[148].label ||
        !mir_read_validate_branch(86, 114) ||
        !mir_read_validate_branch(95, 99) ||
        !mir_read_validate_branch(107, 147) ||
        mir.insns[113].label != mir.insns[147].label ||
        !mir_read_validate_branch(122, 128) ||
        !mir_read_validate_branch(136, 145))
        return mir_machine_reject(
            "read-validate-runner", "control-flow");
    return 1;
}

static void mir_read_validate_emit_call(
    MirStream *out, struct Sym *function,
    const char *call_name, int words)
{
    if (!strcmp(
            call_name,
            asm_name_for(sym_asm_name(function))))
        mir_machine_emit_symbol_call(out, function);
    else
        mir_stream_printf(out, "\tcall %s\n", call_name);
    mir_emit_final_call_cleanup(out, words);
}

static void mir_read_validate_push_long_parameter(
    MirStream *out, const struct MirReadValidateRunner *plan)
{
    int offset = plan->offset_stack_offset + 2;

    mir_stream_printf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld e,(ix+%d)\n\tld d,(ix+%d)\n"
            "\tpush de\n\tpush hl\n",
            offset, offset + 1, offset + 2, offset + 3);
}

static void mir_read_validate_print(
    MirStream *out, const struct MirReadValidateRunner *plan,
    int call_slot, int string_slot)
{
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[string_slot]);
    mir_read_validate_emit_call(
        out, plan->print_function,
        plan->print_names[call_slot], 1);
}

static void mir_read_validate_print_long(
    MirStream *out, const struct MirReadValidateRunner *plan,
    int call_slot, int string_slot, int addend)
{
    int no_carry = new_label();
    int offset = plan->offset_stack_offset + 2;

    mir_stream_printf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld e,(ix+%d)\n\tld d,(ix+%d)\n",
            offset, offset + 1, offset + 2, offset + 3);
    if (addend != 0) {
        mir_stream_printf(out,
                "\tld bc,%d\n\tadd hl,bc\n"
                "\tjp nc,L%d\n\tinc de\nL%d:\n",
                addend, no_carry, no_carry);
    }
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[string_slot]);
    mir_read_validate_emit_call(
        out, plan->print_function,
        plan->print_names[call_slot], 3);
}

static void mir_read_validate_buffer_check(
    MirStream *out, const struct MirReadValidateRunner *plan,
    int buffer_offset, int expected,
    int call_slot, int string_slot)
{
    int equal = new_label();

    mir_stream_printf(out, "\tld a,(%s+%d)\n\tcp %d\n",
            asm_name_for(sym_asm_name(plan->buffer)),
            buffer_offset, expected);
    mir_stream_printf(out, "\tjp z,L%d\n", equal);
    mir_read_validate_print(
        out, plan, call_slot, string_slot);
    mir_stream_printf(out, "L%d:\n", equal);
}

static void mir_read_validate_offset_mismatch(
    MirStream *out, const struct MirReadValidateRunner *plan,
    int value, int mismatch)
{
    int offset = plan->offset_stack_offset + 2;

    if ((value & ~0xffff) == 0) {
        mir_stream_printf(out,
                "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
                "\tld e,(ix+%d)\n\tld d,(ix+%d)\n"
                "\tld bc,%d\n\tor a\n\tsbc hl,bc\n"
                "\tjp nz,L%d\n\tld a,d\n\tor e\n\tjp nz,L%d\n",
                offset, offset + 1, offset + 2, offset + 3,
                value, mismatch, mismatch);
        return;
    }
    mir_stream_printf(out,
            "\tld a,(ix+%d)\n\tcp %d\n\tjp nz,L%d\n"
            "\tld a,(ix+%d)\n\tcp %d\n\tjp nz,L%d\n"
            "\tld a,(ix+%d)\n\tcp %d\n\tjp nz,L%d\n"
            "\tld a,(ix+%d)\n\tcp %d\n\tjp nz,L%d\n",
            offset, value & 255, mismatch,
            offset + 1, (value >> 8) & 255, mismatch,
            offset + 2, (value >> 16) & 255, mismatch,
            offset + 3, (value >> 24) & 255, mismatch);
}

static void mir_emit_read_validate_runner(
    MirStream *out, const struct MirReadValidateRunner *plan)
{
    int result_nonzero = new_label();
    int not_fixed_middle = new_label();
    int not_file_end = new_label();
    int done = new_label();
    int stream = plan->stream_stack_offset + 2;
    int chunk = plan->chunk_stack_offset + 2;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-2\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    mir_stream_printf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n"
            "\tld hl,1\n\tpush hl\n"
            "\tld hl,%s\n\tpush hl\n",
            stream, stream + 1, chunk, chunk + 1,
            asm_name_for(sym_asm_name(plan->buffer)));
    mir_machine_emit_symbol_call(out, plan->read_function);
    mir_emit_final_call_cleanup(out, 4);
    mir_stream_puts("\tld (ix-2),l\n\tld (ix-1),h\n"
          "\tpush hl\n", out);
    mir_read_validate_push_long_parameter(out, plan);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[0]);
    mir_read_validate_emit_call(
        out, plan->print_function,
        plan->print_names[0], 4);

    mir_stream_puts("\tld a,(ix-2)\n\tor (ix-1)\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", result_nonzero);
    mir_machine_emit_global_word(out, plan->error_object, 0);
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[1]);
    mir_read_validate_emit_call(
        out, plan->print_function,
        plan->print_names[1], 2);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n",
            done, result_nonzero);

    mir_read_validate_offset_mismatch(
        out, plan, 512, not_fixed_middle);
    mir_read_validate_buffer_check(
        out, plan, 0, 107, 2, 2);
    mir_read_validate_buffer_check(
        out, plan, 127, 107, 3, 3);
    mir_read_validate_buffer_check(
        out, plan, 128, 0, 4, 4);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n",
            done, not_fixed_middle);

    mir_read_validate_offset_mismatch(
        out, plan, 8192, not_file_end);
    mir_read_validate_buffer_check(
        out, plan, 0, 106, 5, 5);
    mir_read_validate_buffer_check(
        out, plan, 127, 26, 6, 6);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n",
            done, not_file_end);

    {
        int first_zero = new_label();
        int last_zero = new_label();

        mir_stream_printf(out, "\tld a,(%s+0)\n\tor a\n",
                asm_name_for(sym_asm_name(plan->buffer)));
        mir_stream_printf(out, "\tjp z,L%d\n", first_zero);
        mir_read_validate_print_long(
            out, plan, 7, 7, 0);
        mir_stream_printf(out, "L%d:\n", first_zero);
        mir_stream_printf(out, "\tld a,(%s+511)\n\tor a\n",
                asm_name_for(sym_asm_name(plan->buffer)));
        mir_stream_printf(out, "\tjp z,L%d\n", last_zero);
        mir_read_validate_print_long(
            out, plan, 8, 7, 511);
        mir_stream_printf(out, "L%d:\n", last_zero);
    }

    mir_stream_printf(out,
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            done);
}

static int mir_promotion_opcode_sequence(void)
{
    static const unsigned char expected[660] = {
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_CONST, MIR_STORE, MIR_NOP, MIR_CONST, MIR_STORE,
        MIR_NOP, MIR_CONST, MIR_STORE, MIR_CONST, MIR_STORE, MIR_CONST, MIR_STORE, MIR_CONST,
        MIR_STORE, MIR_NOP, MIR_CONST, MIR_STORE, MIR_STRADDR, MIR_ARG, MIR_CALL, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_UNARY,
        MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_NOP, MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_NOP, MIR_NOP, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_NOP, MIR_NOP, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_NOP, MIR_NOP, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_UNARY,
        MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_NOP, MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_UNARY,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_NOP,
        MIR_UNARY, MIR_BINARY, MIR_NOP, MIR_NOP, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_NOP, MIR_NOP, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_CONST, MIR_NOP,
        MIR_BINARY, MIR_NOP, MIR_NOP, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR,
        MIR_ARG, MIR_NOP, MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_NOP, MIR_UNARY, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_NOP, MIR_UNARY,
        MIR_BINARY, MIR_NOP, MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR,
        MIR_ARG, MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_UNARY,
        MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_NOP,
        MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_UNARY, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_NOP, MIR_UNARY, MIR_UNARY,
        MIR_BINARY, MIR_UNARY, MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR,
        MIR_ARG, MIR_NOP, MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_UNARY,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_NOP,
        MIR_UNARY, MIR_BINARY, MIR_NOP, MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_UNARY,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_NOP,
        MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_NOP, MIR_NOP,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_BINARY, MIR_NOP, MIR_NOP, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_UNARY,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_NOP,
        MIR_UNARY, MIR_UNARY, MIR_BINARY, MIR_NOP, MIR_BINARY, MIR_UNARY, MIR_UNARY, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_UNARY, MIR_UNARY,
        MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_NOP,
        MIR_UNARY, MIR_UNARY, MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR,
        MIR_ARG, MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_UNARY, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_UNARY, MIR_UNARY,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_UNARY,
        MIR_NOP, MIR_NOP, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG,
        MIR_NOP, MIR_UNARY, MIR_NOP, MIR_NOP, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_UNARY, MIR_NOP, MIR_NOP, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_UNARY, MIR_NOP, MIR_NOP,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_NOP,
        MIR_UNARY, MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_UNARY, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_BINARY, MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG,
        MIR_NOP, MIR_NOP, MIR_BINARY, MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_STRADDR, MIR_ARG, MIR_CONST, MIR_BRFALSE, MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_NOP, MIR_UNARY, MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_NOP, MIR_UNARY, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_CONST, MIR_BRFALSE, MIR_NOP,
        MIR_UNARY, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_LABEL, MIR_PHI,
        MIR_NOP, MIR_NOP, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG,
        MIR_CONST, MIR_BRFALSE, MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_NOP,
        MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_NOP, MIR_NOP, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_CONST, MIR_BRFALSE, MIR_NOP, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_NOP, MIR_NOP, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_UNARY, MIR_STORE, MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_NOP, MIR_UNARY, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP, MIR_UNARY, MIR_STORE, MIR_STRADDR, MIR_ARG,
        MIR_LOAD, MIR_UNARY, MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_UNARY, MIR_STORE, MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_NOP, MIR_UNARY, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP, MIR_UNARY, MIR_STORE, MIR_STRADDR, MIR_ARG,
        MIR_LOAD, MIR_UNARY, MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_CONST, MIR_STORE, MIR_NOP, MIR_CONST, MIR_STORE, MIR_NOP, MIR_CONST,
        MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_UNARY, MIR_STORE, MIR_STRADDR, MIR_ARG,
        MIR_NOP, MIR_NOP, MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_NOP,
        MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_STRADDR, MIR_ARG, MIR_NOP, MIR_NOP, MIR_NOP, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP, MIR_LOAD, MIR_BRFALSE, MIR_STRADDR, MIR_ARG,
        MIR_LOAD, MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN, MIR_NOP, MIR_LABEL, MIR_STRADDR,
        MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN
    };
    int instruction;

    if (mir.count != (int)(sizeof(expected) / sizeof(expected[0])))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected[instruction] &&
            !((instruction == 540 || instruction == 564) &&
              expected[instruction] == MIR_NOP &&
              mir.insns[instruction].opcode == MIR_LOAD))
            return 0;
    return 1;
}

static int mir_promotion_scalar_type(
    int type, int base, int is_unsigned, int bytes)
{
    return type_ptr_depth(type) == 0 &&
           (type & 15) == base &&
           ((type & TYPE_UNSIGNED) != 0) == is_unsigned &&
           type_size(type) == bytes;
}

static int mir_promotion_char_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
           (type & 15) == TYPE_CHAR &&
           type_size(type) == 2;
}

static int mir_promotion_integer_type(int type)
{
    int base = type & 15;
    int bytes = type_size(type);

    return type_ptr_depth(type) == 0 &&
           (base == TYPE_CHAR || base == TYPE_INT ||
            base == TYPE_LONG) &&
           (bytes == 1 || bytes == 2 || bytes == 4);
}

static int mir_promotion_normalize(
    long value, int type, unsigned long *bits_out)
{
    unsigned long mask;
    int bytes;

    if (bits_out == NULL || type_ptr_depth(type) != 0)
        return 0;
    bytes = type_size(type);
    if (bytes == 1)
        mask = 0xffUL;
    else if (bytes == 2)
        mask = 0xffffUL;
    else if (bytes == 4)
        mask = 0xffffffffUL;
    else
        return 0;
    *bits_out = (unsigned long)value & mask;
    return 1;
}

static int mir_promotion_denormalize(
    unsigned long bits, int type, long *value_out)
{
    int bytes = type_size(type);

    if (value_out == NULL || !mir_promotion_integer_type(type))
        return 0;
    bits &= bytes == 1 ? 0xffUL
          : bytes == 2 ? 0xffffUL : 0xffffffffUL;
    if ((type & TYPE_UNSIGNED) == 0) {
        if (bytes == 1 && (bits & 0x80UL) != 0)
            *value_out = (long)bits - 0x100L;
        else if (bytes == 2 && (bits & 0x8000UL) != 0)
            *value_out = (long)bits - 0x10000L;
        else if (bytes == 4 && (bits & 0x80000000UL) != 0)
            *value_out = (long)bits - 0x100000000L;
        else
            *value_out = (long)bits;
    } else {
        *value_out = (long)bits;
    }
    return 1;
}

static int mir_promotion_evaluate(
    int value, unsigned long *bits_out, int depth)
{
    const struct MirInsn *definition;
    long value_out;
    int instruction;

    if (depth > 16 ||
        (definition = mir_definition(value)) == NULL)
        return 0;
    if (mir_machine_evaluate_constant(
            value, &value_out, 0))
        return mir_promotion_normalize(
            value_out, definition->type, bits_out);
    instruction = (int)(definition - mir.insns);
    if (definition->opcode == MIR_PHI) {
        int source;

        if (instruction == 468 || instruction == 506)
            source = definition->src1;
        else if (instruction == 487 || instruction == 524)
            source = definition->src2;
        else
            return 0;
        return mir_promotion_evaluate(
            source, bits_out, depth + 1);
    }
    if (definition->opcode == MIR_UNARY &&
        definition->immediate == 0) {
        const struct MirInsn *source =
            mir_definition(definition->src1);
        unsigned long source_bits;

        if (source == NULL ||
            !mir_promotion_evaluate(
                definition->src1, &source_bits, depth + 1) ||
            !mir_promotion_denormalize(
                source_bits, source->type, &value_out))
            return 0;
        return mir_promotion_normalize(
            value_out, definition->type, bits_out);
    }
    if (definition->opcode == MIR_LOAD) {
        int store;

        for (store = instruction - 1; store >= 0; --store)
            if (mir.insns[store].opcode == MIR_STORE &&
                mir_machine_same_location(
                    &mir.insns[store], definition))
                return mir_promotion_evaluate(
                    mir.insns[store].src1,
                    bits_out, depth + 1);
    }
    return 0;
}

static int mir_promotion_operations(void)
{
    static const struct MirPromotionOperation unary[] = {
        {31, 34, 0, 0}, {32, 36, 0, 0}, {43, 36, 0, 0},
        {85, 2, 0, 0}, {87, 34, 0, 0}, {88, 36, 0, 0},
        {99, 36, 0, 0}, {108, 2, 0, 0}, {110, 33, 0, 0},
        {111, 36, 0, 0}, {120, 36, 0, 0}, {155, 34, 0, 0},
        {158, 36, 0, 0}, {167, 34, 0, 0}, {170, 36, 0, 0},
        {178, 2, 0, '~'}, {179, 33, 0, 0}, {180, 36, 0, 0},
        {189, 2, 0, 0}, {191, 33, 0, 0}, {192, 36, 0, 0},
        {201, 2, 0, 0}, {202, 2, 0, 0}, {204, 34, 0, 0},
        {205, 36, 0, 0}, {214, 2, 0, 0}, {215, 2, 0, 0},
        {217, 33, 0, 0}, {218, 36, 0, 0}, {227, 2, 0, 0},
        {228, 2, 0, 0}, {230, 34, 0, 0}, {231, 36, 0, 0},
        {240, 34, 0, 0}, {243, 36, 0, 0}, {252, 2, 0, 0},
        {254, 34, 0, 0}, {255, 36, 0, 0}, {264, 4, 0, 0},
        {266, 34, 0, 0}, {267, 36, 0, 0}, {276, 36, 0, 0},
        {300, 2, 0, 0}, {302, 34, 0, 0}, {303, 36, 0, 0},
        {312, 2, 0, 0}, {313, 2, 0, 0}, {317, 34, 0, 0},
        {318, 36, 0, 0}, {326, 2, 0, '+'}, {327, 33, 0, 0},
        {328, 36, 0, 0}, {336, 2, 0, '-'}, {337, 33, 0, 0},
        {338, 36, 0, 0}, {346, 2, 0, '-'}, {347, 33, 0, 0},
        {348, 36, 0, 0}, {356, 2, 0, '~'}, {357, 33, 0, 0},
        {358, 36, 0, 0}, {366, 2, 0, '!'}, {367, 36, 0, 0},
        {375, 36, 0, '-'}, {385, 36, 0, '~'}, {395, 4, 0, '-'},
        {405, 4, 0, '~'}, {416, 2, 0, 0}, {417, 2, 0, 0},
        {419, 36, 0, 0}, {430, 36, 0, 0}, {441, 36, 0, 0},
        {451, 36, 0, 0}, {465, 34, 0, 0}, {470, 36, 0, 0},
        {480, 36, 0, 0}, {536, 33, 0, 0}, {542, 36, 0, 0},
        {548, 1, 0, 0}, {553, 33, 0, 0}, {554, 36, 0, 0},
        {560, 34, 0, 0}, {566, 36, 0, 0}, {572, 2, 0, 0},
        {577, 34, 0, 0}, {578, 36, 0, 0}, {604, 33, 0, 0},
        {610, 36, 0, 0}, {624, 36, 0, 0}
    };
    static const struct MirPromotionOperation binary[] = {
        {30, 2, 2, TOK_SHR}, {41, 34, 34, TOK_SHR},
        {52, 4, 4, TOK_SHR}, {63, 4, 4, TOK_SHR},
        {74, 36, 36, TOK_SHR}, {86, 2, 2, TOK_SHL},
        {97, 34, 34, TOK_SHL}, {109, 2, 2, TOK_SHR},
        {121, 36, 36, '&'}, {132, 36, 36, '|'},
        {144, 36, 36, '&'}, {156, 34, 34, '|'},
        {168, 34, 34, '^'}, {190, 2, 2, '&'},
        {203, 2, 2, '+'}, {216, 2, 2, '+'},
        {229, 2, 2, '*'}, {241, 34, 34, '*'},
        {253, 2, 2, '/'}, {265, 4, 4, '/'},
        {277, 36, 36, '+'}, {289, 36, 36, '+'},
        {301, 2, 2, '+'}, {314, 2, 2, '*'},
        {316, 2, 2, '+'}, {418, 2, 2, '<'},
        {429, 2, 36, '>'}, {440, 2, 36, '<'},
        {450, 2, 4, '<'}, {603, 2, 2, '+'},
        {617, 34, 34, '+'}, {631, 36, 36, '+'}
    };
    size_t item;

    for (item = 0; item < sizeof(unary) / sizeof(unary[0]); ++item) {
        const struct MirInsn *insn =
            &mir.insns[unary[item].instruction];

        if (insn->opcode != MIR_UNARY ||
            insn->type != unary[item].type ||
            insn->immediate != unary[item].operation)
            return 0;
    }
    for (item = 0; item < sizeof(binary) / sizeof(binary[0]); ++item) {
        const struct MirInsn *insn =
            &mir.insns[binary[item].instruction];

        if (insn->opcode != MIR_BINARY ||
            insn->type != binary[item].type ||
            insn->secondary_offset != binary[item].operand_type ||
            insn->immediate != binary[item].operation)
            return 0;
    }
    return 1;
}

static int mir_promotion_initial_state(void)
{
    static const struct {
        int instruction;
        int type;
        long value;
    } constants[] = {
        {3, 1, -10L}, {6, 33, 200L}, {9, 2, 62536L},
        {11, 34, 50000L}, {13, 4, 123456L},
        {15, 36, 4000000000L}, {18, 4, 4293967296L},
        {23, 2, 0L}, {458, 2, 1L}, {477, 2, 0L},
        {496, 2, 1L}, {515, 2, 0L}, {593, 33, 250L},
        {596, 34, 60000L}, {599, 36, 4294967280L}
    };
    static const int object_types[10] = {
        1, 33, 2, 34, 4, 36, 4, 33, 34, 36
    };
    static const struct {
        int instruction;
        int object;
        int bytes;
    } stores[] = {
        {4, 0, 1}, {7, 1, 1}, {10, 2, 2}, {12, 3, 2},
        {14, 4, 4}, {16, 5, 4}, {19, 6, 4}, {537, 7, 1},
        {561, 8, 2}, {594, 7, 1}, {597, 8, 2}, {600, 9, 4},
        {605, 7, 1}, {619, 8, 2}, {633, 9, 4}
    };
    size_t item;

    if (mir.object_count != 10 || mir.local_bytes != 31 ||
        mir.aggregate_temp_bytes != 0)
        return 0;
    for (item = 0; item < 10; ++item)
        if (mir.objects[item].storage != SC_LOCAL ||
            mir.objects[item].type != object_types[item] ||
            mir.objects[item].is_register)
            return 0;
    for (item = 0; item < sizeof(constants) / sizeof(constants[0]);
         ++item) {
        const struct MirInsn *insn =
            &mir.insns[constants[item].instruction];

        if (insn->opcode != MIR_CONST ||
            insn->type != constants[item].type ||
            insn->immediate != constants[item].value)
            return 0;
    }
    for (item = 0; item < sizeof(stores) / sizeof(stores[0]); ++item) {
        const struct MirInsn *insn =
            &mir.insns[stores[item].instruction];

        if (insn->opcode != MIR_STORE ||
            (insn->object != stores[item].object &&
             !((stores[item].instruction == 537 ||
                stores[item].instruction == 561) &&
               insn->object < 0)) ||
            insn->memory_size != stores[item].bytes ||
            (insn->memory_flags & (1 | 8)) != 0)
            return 0;
    }
    return mir_machine_same_location(
               &mir.insns[537], &mir.insns[540]) &&
           mir_machine_same_location(
               &mir.insns[561], &mir.insns[564]) &&
           mir_machine_same_location(
               &mir.insns[549], &mir.insns[552]) &&
           mir_machine_same_location(
               &mir.insns[573], &mir.insns[576]) &&
           mir.insns[549].object < 0 &&
           mir.insns[573].object < 0 &&
           mir_promotion_scalar_type(
               mir.insns[548].type, TYPE_CHAR, 0, 1) &&
           mir_promotion_scalar_type(
               mir.insns[552].type, TYPE_CHAR, 0, 1) &&
           mir_promotion_scalar_type(
               mir.insns[572].type, TYPE_INT, 0, 2) &&
           mir_promotion_scalar_type(
               mir.insns[576].type, TYPE_INT, 0, 2);
}

static int mir_promotion_direct_call(
    const struct MirInsn *call, struct Sym **function_out,
    int variadic, int fixed_arguments)
{
    struct Sym *function;

    if (call->opcode != MIR_CALL || call->src1 >= 0 ||
        ((call->memory_flags & MIR_CALL_FLAG_VARIADIC) != 0) != variadic ||
        (function = find_global(call->name)) == NULL ||
        function->storage != SC_FUNC || function->is_funcptr ||
        function->is_noreturn || !function->has_proto ||
        function->proto_nargs != fixed_arguments ||
        function->proto_variadic != variadic ||
        call->type != function->type)
        return 0;
    *function_out = function;
    return 1;
}

static int mir_promotion_string_argument(
    int value, int *string_out)
{
    const struct MirInsn *definition = mir_definition(value);

    if (definition == NULL ||
        definition->opcode != MIR_STRING_ADDRESS ||
        !mir_promotion_char_pointer_type(definition->type) ||
        definition->immediate <= 0)
        return 0;
    *string_out = (int)definition->immediate;
    return 1;
}

static void mir_promotion_capture_call_name(
    char destination[64], const struct MirInsn *call,
    struct Sym *function)
{
    const char *name = call->base_name[0] != 0
        ? call->base_name
        : asm_name_for(sym_asm_name(function));

    snprintf(destination, 64, "%s", name);
}

static int mir_long_subtraction_word_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           (type & 15) == TYPE_INT &&
           (type & TYPE_UNSIGNED) == 0 &&
           type_size(type) == 2;
}

static int mir_long_subtraction_wide_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           (type & 15) == TYPE_LONG &&
           (type & TYPE_UNSIGNED) == 0 &&
           type_size(type) == 4;
}

static int mir_long_subtraction_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
           (type & 15) == TYPE_LONG &&
           (type & TYPE_UNSIGNED) == 0 &&
           type_size(type) == 2;
}

static int mir_long_subtraction_capture_wide(
    int instruction, unsigned long *value)
{
    const struct MirInsn *constant = &mir.insns[instruction];

    if (constant->opcode != MIR_CONST ||
        !mir_long_subtraction_wide_type(constant->type))
        return 0;
    *value = (unsigned long)constant->immediate & 0xffffffffUL;
    return 1;
}

static int mir_long_subtraction_capture_stored_wide(
    int store_instruction, int constant_instruction,
    unsigned long *value)
{
    const struct MirInsn *store = &mir.insns[store_instruction];
    const struct MirInsn *constant = &mir.insns[constant_instruction];

    return mir_long_subtraction_capture_wide(
               constant_instruction, value) &&
           store->opcode == MIR_STORE_INDIRECT &&
           store->src2 == constant->dst &&
           store->memory_size == 4 &&
           (store->memory_flags & (1 | 8)) == 0;
}

static int mir_long_subtraction_same_location_set(
    const int *instructions, size_t count, int anchor)
{
    size_t item;

    for (item = 0; item < count; ++item) {
        const struct MirInsn *insn =
            &mir.insns[instructions[item]];

        if (strcmp(mir.insns[anchor].name, insn->name) ||
            (insn->memory_flags & (1 | 8)) != 0)
            return 0;
    }
    return 1;
}

static int mir_long_subtraction_match_roots(
    struct MirLongSubtractionRunner *plan)
{
    static const int local_addresses[] = {
        1, 6, 11, 17, 25, 29, 49, 53, 73, 77, 97, 101,
        121, 125, 145, 149, 169, 173, 193, 197, 217, 221,
        241, 245, 262, 267, 275, 282, 302, 309, 329, 336,
        356, 363, 383, 390, 407, 412, 417, 556, 561, 572,
        578, 588, 622, 627, 639, 643, 649, 680, 685, 692,
        697, 709, 714, 726, 734
    };
    static const int global_addresses[] = {
        495, 500, 508, 512, 532, 539
    };
    static const int r_accesses[] = {
        24, 37, 41, 48, 61, 65, 72, 85, 89, 96, 109, 113,
        120, 133, 137, 144, 157, 161, 168, 181, 185, 192,
        205, 209, 216, 229, 233, 240, 253, 257, 274, 290,
        294, 301, 317, 321, 328, 344, 348, 355, 371, 375,
        382, 398, 402, 422, 435, 439, 446, 459, 463, 470,
        486, 490, 507, 520, 524, 531, 547, 551
    };
    static const int p_accesses[] = {
        419, 423, 427, 447, 451, 471, 478
    };
    static const int n_accesses[] = {568, 587, 608, 617};
    static const int i_accesses[] = {634, 668};
    size_t item;

    if (!mir_long_subtraction_pointer_type(mir.insns[1].type))
        return mir_machine_reject(
            "long-subtraction-roots", "local-type");
    for (item = 0;
         item < sizeof(local_addresses) /
                    sizeof(local_addresses[0]);
         ++item) {
        const struct MirInsn *address =
            &mir.insns[local_addresses[item]];

        if (address->opcode != MIR_ADDRESS ||
            strcmp(address->name, mir.insns[1].name) ||
            !mir_long_subtraction_pointer_type(address->type))
            return mir_machine_reject(
                "long-subtraction-roots", "local-address");
    }

    plan->global_array = find_global(mir.insns[495].name);
    if (plan->global_array == NULL ||
        plan->global_array->storage == SC_FUNC ||
        !plan->global_array->is_defined ||
        plan->global_array->is_volatile ||
        !plan->global_array->is_array ||
        plan->global_array->array_len != 4 ||
        plan->global_array->elem_size != 4)
        return mir_machine_reject(
            "long-subtraction-roots", "global-symbol");
    for (item = 0;
         item < sizeof(global_addresses) /
                    sizeof(global_addresses[0]);
         ++item) {
        const struct MirInsn *address =
            &mir.insns[global_addresses[item]];

        if (find_global(address->name) != plan->global_array ||
            !mir_long_subtraction_pointer_type(address->type))
            return mir_machine_reject(
                "long-subtraction-roots", "global-address");
    }

    if (!mir_long_subtraction_same_location_set(
            r_accesses,
            sizeof(r_accesses) / sizeof(r_accesses[0]), 24))
        return mir_machine_reject(
            "long-subtraction-roots", "r-access");
    if (!mir_long_subtraction_same_location_set(
            p_accesses,
            sizeof(p_accesses) / sizeof(p_accesses[0]), 419))
        return mir_machine_reject(
            "long-subtraction-roots", "p-access");
    if (!mir_long_subtraction_same_location_set(
            n_accesses,
            sizeof(n_accesses) / sizeof(n_accesses[0]), 568))
        return mir_machine_reject(
            "long-subtraction-roots", "n-access");
    if (!mir_long_subtraction_same_location_set(
            i_accesses,
            sizeof(i_accesses) / sizeof(i_accesses[0]), 634))
        return mir_machine_reject(
            "long-subtraction-roots", "i-access");
    if (!mir_long_subtraction_word_type(mir.insns[41].type) ||
        !mir_long_subtraction_pointer_type(mir.insns[423].type) ||
        !mir_long_subtraction_word_type(mir.insns[608].type) ||
        !mir_long_subtraction_word_type(mir.insns[638].type) ||
        !strcmp(mir.insns[24].name, mir.insns[419].name) ||
        !strcmp(mir.insns[24].name, mir.insns[568].name) ||
        !strcmp(mir.insns[24].name, mir.insns[634].name) ||
        !strcmp(mir.insns[419].name, mir.insns[568].name) ||
        !strcmp(mir.insns[419].name, mir.insns[634].name) ||
        !strcmp(mir.insns[568].name, mir.insns[634].name) ||
        mir.insns[419].src1 != mir.insns[417].dst)
        return mir_machine_reject(
            "long-subtraction-roots", "local-properties");
    return 1;
}

static int mir_long_subtraction_match_graph(void)
{
    size_t item;

    for (item = 0;
         item < sizeof(mir_long_subtraction_edges) /
                    sizeof(mir_long_subtraction_edges[0]);
         ++item) {
        const struct MirLongSubtractionEdge *edge =
            &mir_long_subtraction_edges[item];
        const struct MirInsn *insn =
            &mir.insns[edge->instruction];

        if (insn->opcode == MIR_PHI) {
            if (edge->second < 0 ||
                insn->phi_pred1 !=
                    mir.insns[edge->first].label ||
                insn->phi_pred2 !=
                    mir.insns[edge->second].label)
                return 0;
        } else if (edge->second >= 0 ||
                   insn->label !=
                       mir.insns[edge->first].label) {
            return 0;
        }
    }
    for (item = 0;
         item < sizeof(mir_long_subtraction_indices) /
                    sizeof(mir_long_subtraction_indices[0]);
         ++item) {
        const struct MirLongSubtractionIndex *index =
            &mir_long_subtraction_indices[item];
        const struct MirInsn *insn =
            &mir.insns[index->instruction];
        const struct MirInsn *base = mir_definition(insn->src1);

        if (insn->opcode != MIR_INDEX_ADDRESS ||
            insn->immediate != 4 || insn->memory_size != 4 ||
            (insn->memory_flags & (1 | 8)) != 0 ||
            !mir_long_subtraction_pointer_type(insn->type) ||
            !mir_machine_constant_equals(
                insn->src2, index->index) ||
            base == NULL ||
            (base->opcode != MIR_ADDRESS &&
             base->opcode != MIR_LOAD))
            return 0;
    }
    for (item = 0;
         item < sizeof(mir_long_subtraction_binaries) /
                    sizeof(mir_long_subtraction_binaries[0]);
         ++item) {
        const struct MirLongSubtractionBinary *binary =
            &mir_long_subtraction_binaries[item];
        const struct MirInsn *insn =
            &mir.insns[binary->instruction];
        int comparison =
            binary->operation != '+';

        if (insn->opcode != MIR_BINARY ||
            insn->immediate != binary->operation ||
            type_size(insn->secondary_offset) !=
                binary->operand_width ||
            (insn->secondary_offset & TYPE_UNSIGNED) != 0 ||
            (binary->operand_width == 4
                 ? (insn->secondary_offset & 15) != TYPE_LONG
                 : (insn->secondary_offset & 15) != TYPE_INT) ||
            (comparison
                 ? !mir_long_subtraction_word_type(insn->type)
                 : binary->operand_width == 4
                       ? !mir_long_subtraction_wide_type(insn->type)
                       : !mir_long_subtraction_word_type(insn->type)) ||
            mir_definition(insn->src1) == NULL ||
            mir_definition(insn->src2) == NULL)
            return 0;
    }
    for (item = 0; item < (size_t)mir.count; ++item) {
        const struct MirInsn *insn = &mir.insns[item];

        if ((insn->opcode == MIR_LOAD_INDIRECT ||
             insn->opcode == MIR_STORE_INDIRECT) &&
            (insn->memory_size != 4 ||
             (insn->memory_flags & (1 | 8)) != 0 ||
             mir_definition(insn->src1) == NULL ||
             mir_definition(insn->src1)->opcode !=
                 MIR_INDEX_ADDRESS))
            return 0;
    }
    return mir.insns[571].src1 == mir.insns[566].dst &&
           mir.insns[571].src2 == mir.insns[586].dst &&
           mir.insns[582].src1 == mir.insns[577].dst &&
           mir.insns[582].src2 == mir.insns[581].dst &&
           mir.insns[586].src1 == mir.insns[571].dst &&
           mir.insns[593].src1 == mir.insns[591].dst &&
           mir.insns[594].src1 == mir.insns[590].dst &&
           mir.insns[594].src2 == mir.insns[593].dst &&
           mir.insns[638].src1 == mir.insns[632].dst &&
           mir.insns[638].src2 == mir.insns[667].dst &&
           mir.insns[647].src1 == mir.insns[642].dst &&
           mir.insns[647].src2 == mir.insns[646].dst &&
           mir.insns[654].src1 == mir.insns[652].dst &&
           mir.insns[655].src1 == mir.insns[651].dst &&
           mir.insns[655].src2 == mir.insns[654].dst &&
           mir.insns[667].src1 == mir.insns[638].dst;
}

static int mir_long_subtraction_match_constants(
    struct MirLongSubtractionRunner *plan)
{
    static const int initial_stores[][2] = {
        {5, 4}, {10, 9}, {16, 15}, {21, 20}
    };
    static const int addition_stores[][2] = {
        {266, 265}, {271, 270}
    };
    static const int pointer_stores[][2] = {
        {411, 410}, {416, 415}
    };
    static const int global_stores[][2] = {
        {499, 498}, {504, 503}
    };
    static const int while_stores[][2] = {
        {560, 559}, {565, 564}
    };
    static const int for_stores[][2] = {
        {626, 625}, {631, 630}
    };
    static const int helper_stores[][2] = {
        {684, 683}, {689, 688}
    };
    static const int add_constants[] = {
        280, 307, 334, 361, 388, 476, 537
    };
    long while_limit;
    long for_limit;
    size_t item;

    for (item = 0; item < 4; ++item)
        if (!mir_long_subtraction_capture_stored_wide(
                initial_stores[item][0],
                initial_stores[item][1],
                &plan->initial_values[item]))
            return 0;
    for (item = 0; item < 2; ++item) {
        if (!mir_long_subtraction_capture_stored_wide(
                addition_stores[item][0],
                addition_stores[item][1],
                &plan->addition_values[item]) ||
            !mir_long_subtraction_capture_stored_wide(
                pointer_stores[item][0],
                pointer_stores[item][1],
                &plan->pointer_values[item]) ||
            !mir_long_subtraction_capture_stored_wide(
                global_stores[item][0],
                global_stores[item][1],
                &plan->global_values[item]) ||
            !mir_long_subtraction_capture_stored_wide(
                while_stores[item][0],
                while_stores[item][1],
                &plan->while_values[item]) ||
            !mir_long_subtraction_capture_stored_wide(
                for_stores[item][0],
                for_stores[item][1],
                &plan->for_values[item]) ||
            !mir_long_subtraction_capture_stored_wide(
                helper_stores[item][0],
                helper_stores[item][1],
                &plan->helper_values[item]))
            return 0;
    }
    for (item = 0; item < 7; ++item)
        if (!mir_long_subtraction_capture_wide(
                add_constants[item],
                &plan->direct_addends[item]))
            return 0;
    if (!mir_long_subtraction_capture_wide(
            576, &plan->while_condition_addend) ||
        !mir_long_subtraction_capture_wide(
            592, &plan->while_step) ||
        !mir_long_subtraction_capture_wide(
            653, &plan->for_step) ||
        !mir_long_subtraction_capture_wide(
            731, &plan->helper_addend) ||
        !mir_machine_constant_value(
            mir.insns[596].dst, &while_limit, 0) ||
        !mir_machine_constant_value(
            mir.insns[657].dst, &for_limit, 0) ||
        while_limit < 0 || while_limit > 32766 ||
        for_limit < 0 || for_limit > 32766 ||
        !mir_machine_constant_equals(mir.insns[566].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[585].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[632].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[666].dst, 1))
        return 0;
    plan->while_limit = (int)while_limit;
    plan->for_limit = (int)for_limit;
    return 1;
}

static int mir_long_subtraction_match_calls(
    struct MirLongSubtractionRunner *plan)
{
    static const int check_calls[MIR_LONG_SUBTRACTION_CHECK_COUNT] = {
        45, 69, 93, 117, 141, 165, 189, 213, 237, 261,
        298, 325, 352, 379, 406, 443, 467, 494, 528, 555,
        614, 621, 679, 706, 723, 743
    };
    static const int helper_calls[] = {702, 719, 739};
    struct Sym *function;
    int all_strings[MIR_LONG_SUBTRACTION_CHECK_COUNT + 2];
    int arguments[3];
    int call_count = 0;
    int check;
    int item;

    for (check = 0;
         check < MIR_LONG_SUBTRACTION_CHECK_COUNT;
         ++check) {
        const struct MirInsn *call =
            &mir.insns[check_calls[check]];
        const struct MirInsn *got;
        const struct MirInsn *want;
        int string_id;

        if (!mir_promotion_direct_call(
                call, &function, 0, 3) ||
            (function->type & 15) != TYPE_VOID ||
            type_ptr_depth(function->type) != 0 ||
            !mir_promotion_char_pointer_type(
                function->proto_types[0]) ||
            !mir_promotion_scalar_type(
                function->proto_types[1],
                TYPE_INT, 0, 2) ||
            !mir_promotion_scalar_type(
                function->proto_types[2],
                TYPE_INT, 0, 2) ||
            !mir_machine_call_arguments(
                call, 3, arguments) ||
            !mir_promotion_string_argument(
                arguments[0], &string_id) ||
            (got = mir_definition(arguments[1])) == NULL ||
            (want = mir_definition(arguments[2])) == NULL ||
            !mir_long_subtraction_word_type(got->type) ||
            want->opcode != MIR_CONST ||
            !mir_long_subtraction_word_type(want->type))
            return 0;
        if (plan->check_function == NULL)
            plan->check_function = function;
        else if (plan->check_function != function)
            return 0;
        plan->check_strings[check] = string_id;
        plan->check_wants[check] =
            (int)((unsigned long)want->immediate & 0xffffUL);
        all_strings[check] = string_id;
    }

    for (item = 0; item < 3; ++item) {
        const struct MirInsn *call =
            &mir.insns[helper_calls[item]];
        const struct MirInsn *left;
        const struct MirInsn *right;

        if (!mir_promotion_direct_call(
                call, &function, 0, 2) ||
            !mir_long_subtraction_word_type(function->type) ||
            !mir_long_subtraction_wide_type(
                function->proto_types[0]) ||
            !mir_long_subtraction_wide_type(
                function->proto_types[1]) ||
            !mir_machine_call_arguments(
                call, 2, arguments) ||
            (left = mir_definition(arguments[0])) == NULL ||
            (right = mir_definition(arguments[1])) == NULL ||
            !mir_long_subtraction_wide_type(left->type) ||
            !mir_long_subtraction_wide_type(right->type))
            return 0;
        if (plan->helper_function == NULL)
            plan->helper_function = function;
        else if (plan->helper_function != function)
            return 0;
    }

    if (!mir_promotion_direct_call(
            &mir.insns[751], &plan->print_function, 1, 1) ||
        !mir_long_subtraction_word_type(
            plan->print_function->type) ||
        !mir_promotion_char_pointer_type(
            plan->print_function->proto_types[0]) ||
        !mir_machine_call_arguments(
            &mir.insns[751], 1, arguments) ||
        !mir_promotion_string_argument(
            arguments[0], &plan->success_string) ||
        !mir_promotion_direct_call(
            &mir.insns[758], &function, 1, 1) ||
        function != plan->print_function ||
        !mir_machine_call_arguments(
            &mir.insns[758], 2, arguments) ||
        !mir_promotion_string_argument(
            arguments[0], &plan->failure_string))
        return 0;
    mir_promotion_capture_call_name(
        plan->print_names[0], &mir.insns[751],
        plan->print_function);
    mir_promotion_capture_call_name(
        plan->print_names[1], &mir.insns[758],
        plan->print_function);
    all_strings[MIR_LONG_SUBTRACTION_CHECK_COUNT] =
        plan->success_string;
    all_strings[MIR_LONG_SUBTRACTION_CHECK_COUNT + 1] =
        plan->failure_string;
    for (item = 0;
         item < MIR_LONG_SUBTRACTION_CHECK_COUNT + 2;
         ++item)
        for (check = 0; check < item; ++check)
            if (all_strings[item] == all_strings[check])
                return 0;

    plan->failures = find_global(mir.insns[744].name);
    if (plan->failures == NULL ||
        plan->failures->storage == SC_FUNC ||
        !plan->failures->is_defined ||
        plan->failures->is_array ||
        plan->failures->is_volatile ||
        !mir_long_subtraction_word_type(plan->failures->type) ||
        !mir_machine_same_location(
            &mir.insns[744], &mir.insns[756]) ||
        !mir_machine_same_location(
            &mir.insns[744], &mir.insns[760]) ||
        arguments[1] != mir.insns[756].dst ||
        mir.insns[761].src1 != mir.insns[760].dst)
        return 0;
    for (item = 0; item < mir.count; ++item)
        if (mir.insns[item].opcode == MIR_CALL)
            ++call_count;
    return call_count == 31;
}

static int mir_match_long_subtraction_runner(
    struct MirLongSubtractionRunner *plan)
{
    int instruction;
    int object;

    memset(plan, 0, sizeof(*plan));
    if (mir.count !=
            (int)(sizeof(mir_long_subtraction_opcodes) /
                  sizeof(mir_long_subtraction_opcodes[0])) ||
        mir_cfg_block_count() != 32 || mir.has_vla ||
        mir.local_bytes != 26 || mir.aggregate_temp_bytes != 0 ||
        mir.object_count != 3 ||
        !mir_long_subtraction_word_type(mir.return_type))
        return mir_machine_reject(
            "long-subtraction-runner", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
                mir_long_subtraction_opcodes[instruction])
            return mir_machine_reject(
                "long-subtraction-runner", "opcode");
    for (object = 0; object < mir.object_count; ++object)
        if (mir.objects[object].storage != SC_LOCAL ||
            !mir_long_subtraction_word_type(
                mir.objects[object].type) ||
            mir.objects[object].is_register)
            return mir_machine_reject(
                "long-subtraction-runner", "objects");
    if (!mir_long_subtraction_match_roots(plan))
        return mir_machine_reject(
            "long-subtraction-runner", "roots");
    if (!mir_long_subtraction_match_graph())
        return mir_machine_reject(
            "long-subtraction-runner", "graph");
    if (!mir_long_subtraction_match_constants(plan))
        return mir_machine_reject(
            "long-subtraction-runner", "constants");
    if (!mir_long_subtraction_match_calls(plan))
        return mir_machine_reject(
            "long-subtraction-runner", "calls");
    return 1;
}

static int mir_match_promotion_call_runner(
    struct MirPromotionCallRunner *plan)
{
    static const unsigned long expected[MIR_PROMOTION_CHECK_COUNT] = {
        65348UL, 3125UL, 7716UL, 4294904796UL, 15625000UL,
        400UL, 3392UL, 251UL, 4000000000UL, 4000000015UL,
        57856UL, 65526UL, 15526UL, 55UL, 246UL, 65516UL,
        190UL, 63536UL, 38528UL, 300UL, 2UL, 4000050000UL,
        4000123456UL, 500UL, 60536UL, 200UL, 56UL, 10UL,
        9UL, 0UL, 294967296UL, 294967295UL, 1000000UL,
        4294843839UL, 1UL, 1UL, 1UL, 1UL, 50000UL,
        4000000000UL, 4000000000UL, 4293967296UL, 246UL,
        200UL, 57920UL, 10240UL, 4UL, 4464UL, 16UL
    };
    static const int branch_targets[][2] = {
        {459, 463}, {478, 483}, {497, 501}, {516, 520}, {645, 654}
    };
    static const int phi_contracts[][5] = {
        {468, 461, 466, 11, 465},
        {487, 481, 485, 480, 15},
        {506, 499, 504, 15, 13},
        {524, 518, 522, 13, 18}
    };
    int all_strings[52];
    int call_count = 0;
    int check = 0;
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (!mir_promotion_opcode_sequence())
        return mir_machine_reject(
            "promotion-call-runner", "opcode");
    if (!mir_promotion_operations())
        return mir_machine_reject(
            "promotion-call-runner", "operations");
    if (!mir_promotion_initial_state())
        return mir_machine_reject(
            "promotion-call-runner", "initial-state");
    if (mir_cfg_block_count() != 18 || mir.has_vla ||
        mir.is_variadic_function ||
        !mir_promotion_scalar_type(
            mir.return_type, TYPE_INT, 0, 2))
        return mir_machine_reject(
            "promotion-call-runner", "shape");
    for (item = 0;
         item < (int)(sizeof(branch_targets) /
                      sizeof(branch_targets[0]));
         ++item)
        if (mir.insns[branch_targets[item][0]].label !=
            mir.insns[branch_targets[item][1]].label)
            return mir_machine_reject(
                "promotion-call-runner", "branches");
    for (item = 0;
         item < (int)(sizeof(phi_contracts) /
                      sizeof(phi_contracts[0]));
         ++item) {
        const struct MirInsn *phi =
            &mir.insns[phi_contracts[item][0]];

        if (phi->phi_pred1 !=
                mir.insns[phi_contracts[item][1]].label ||
            phi->phi_pred2 !=
                mir.insns[phi_contracts[item][2]].label ||
            phi->src1 !=
                mir.insns[phi_contracts[item][3]].dst ||
            phi->src2 !=
                mir.insns[phi_contracts[item][4]].dst)
            return mir_machine_reject(
                "promotion-call-runner", "phis");
    }

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *call = &mir.insns[instruction];
        struct Sym *function;
        int arguments[3];
        int argument_count;
        int string_id;

        if (call->opcode != MIR_CALL)
            continue;
        if (call_count == 0 || call_count >= 50) {
            int print_slot = call_count == 0 ? 0
                           : call_count == 50 ? 1 : 2;

            argument_count = call_count == 50 ? 2 : 1;
            if (!mir_promotion_direct_call(
                    call, &function, 1, 1) ||
                !mir_promotion_scalar_type(
                    function->type, TYPE_INT, 0, 2) ||
                !mir_promotion_char_pointer_type(
                    function->proto_types[0]) ||
                !mir_machine_call_arguments(
                    call, argument_count, arguments) ||
                !mir_promotion_string_argument(
                    arguments[0], &string_id))
                return mir_machine_reject(
                    "promotion-call-runner", "print-call");
            if (plan->print_function == NULL)
                plan->print_function = function;
            else if (plan->print_function != function)
                return mir_machine_reject(
                    "promotion-call-runner", "print-family");
            if (call_count == 0)
                plan->intro_string = string_id;
            else if (call_count == 50) {
                const struct MirInsn *failure =
                    mir_definition(arguments[1]);

                plan->failure_string = string_id;
                if (failure != &mir.insns[648])
                    return mir_machine_reject(
                        "promotion-call-runner",
                        "failure-argument");
            } else
                plan->success_string = string_id;
            mir_promotion_capture_call_name(
                plan->print_names[print_slot],
                call, function);
        } else {
            const struct MirInsn *got;
            const struct MirInsn *want;
            unsigned long got_bits;
            unsigned long want_bits;

            if (!mir_promotion_direct_call(
                    call, &function, 0, 3) ||
                (function->type & 15) != TYPE_VOID ||
                type_ptr_depth(function->type) != 0 ||
                !mir_promotion_char_pointer_type(
                    function->proto_types[0]) ||
                !mir_promotion_scalar_type(
                    function->proto_types[1],
                    TYPE_LONG, 1, 4) ||
                !mir_promotion_scalar_type(
                    function->proto_types[2],
                    TYPE_LONG, 1, 4) ||
                !mir_machine_call_arguments(
                    call, 3, arguments) ||
                !mir_promotion_string_argument(
                    arguments[0], &string_id) ||
                (got = mir_definition(arguments[1])) == NULL ||
                (want = mir_definition(arguments[2])) == NULL ||
                !mir_promotion_integer_type(got->type) ||
                !mir_promotion_scalar_type(
                    want->type, TYPE_LONG, 1, 4) ||
                !mir_promotion_evaluate(
                    arguments[1], &got_bits, 0) ||
                !mir_promotion_evaluate(
                    arguments[2], &want_bits, 0) ||
                got_bits != expected[check] ||
                want_bits != expected[check] ||
                call->secondary_offset != check + 1) {
                return mir_machine_reject(
                    "promotion-call-runner", "checks");
            }
            if (plan->check_function == NULL)
                plan->check_function = function;
            else if (plan->check_function != function)
                return mir_machine_reject(
                    "promotion-call-runner", "check-family");
            plan->check_strings[check] = string_id;
            plan->values[check] = expected[check];
            ++check;
        }
        all_strings[call_count] = string_id;
        for (item = 0; item < call_count; ++item)
            if (all_strings[item] == string_id)
                return mir_machine_reject(
                    "promotion-call-runner", "strings");
        ++call_count;
    }
    if (call_count != 52 || check != MIR_PROMOTION_CHECK_COUNT)
        return mir_machine_reject(
            "promotion-call-runner", "call-count");
    if (!mir_machine_named_nonvolatile(&mir.insns[25]) ||
        (plan->failures = find_global(
             mir.insns[25].name)) == NULL ||
        !mir_machine_same_location(
            &mir.insns[25], &mir.insns[644]) ||
        !mir_machine_same_location(
            &mir.insns[25], &mir.insns[648]) ||
        !mir_promotion_scalar_type(
            plan->failures->type, TYPE_INT, 0, 2) ||
        mir.insns[25].src1 != mir.insns[23].dst ||
        mir.insns[645].src1 != mir.insns[644].dst ||
        mir.insns[649].src1 != mir.insns[648].dst ||
        mir.insns[651].immediate != 1 ||
        mir.insns[652].src1 != mir.insns[651].dst ||
        mir.insns[658].immediate != 0 ||
        mir.insns[659].src1 != mir.insns[658].dst)
        return mir_machine_reject(
            "promotion-call-runner", "failure-path");
    return 1;
}

static void mir_promotion_print(
    MirStream *out, const struct MirPromotionCallRunner *plan,
    int string_id, int print_slot, int words)
{
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_emit_runtime_call(out, plan->print_names[print_slot]);
    mir_emit_final_call_cleanup(out, words);
}

static void mir_long_subtraction_emit_wide_constant(
    MirStream *out, unsigned long value)
{
    mir_stream_printf(out, "\tld hl,%lu\n\tld de,%lu\n",
            value & 0xffffUL, (value >> 16) & 0xffffUL);
}

static void mir_long_subtraction_emit_local_load(
    MirStream *out, int index)
{
    mir_machine_emit_ix_wide_load(out, -16 + index * 4);
}

static void mir_long_subtraction_emit_local_store(
    MirStream *out, int index, unsigned long value)
{
    mir_long_subtraction_emit_wide_constant(out, value);
    mir_machine_emit_ix_wide_store(out, -16 + index * 4);
}

static const char *mir_long_subtraction_symbol_name(
    struct Sym *symbol)
{
    return asm_name_for(sym_asm_name(symbol));
}

static void mir_long_subtraction_emit_global_load(
    MirStream *out, const struct MirLongSubtractionRunner *plan,
    int index)
{
    const char *name =
        mir_long_subtraction_symbol_name(plan->global_array);
    int offset = index * 4;

    if (offset == 0)
        mir_stream_printf(out, "\tld hl,(%s)\n\tld de,(%s+2)\n",
                name, name);
    else
        mir_stream_printf(out,
                "\tld hl,(%s+%d)\n\tld de,(%s+%d)\n",
                name, offset, name, offset + 2);
}

static void mir_long_subtraction_emit_global_store(
    MirStream *out, const struct MirLongSubtractionRunner *plan,
    int index, unsigned long value)
{
    const char *name =
        mir_long_subtraction_symbol_name(plan->global_array);
    int offset = index * 4;

    mir_stream_printf(out, "\tld hl,%lu\n",
            value & 0xffffUL);
    if (offset == 0)
        mir_stream_printf(out, "\tld (%s),hl\n", name);
    else
        mir_stream_printf(out, "\tld (%s+%d),hl\n", name, offset);
    mir_stream_printf(out, "\tld hl,%lu\n",
            (value >> 16) & 0xffffUL);
    if (offset == 0)
        mir_stream_printf(out, "\tld (%s+2),hl\n", name);
    else
        mir_stream_printf(out, "\tld (%s+%d),hl\n",
                name, offset + 2);
}

static void mir_long_subtraction_emit_add(
    MirStream *out, unsigned long value)
{
    mir_stream_printf(out,
            "\tld bc,%lu\n\tadd hl,bc\n"
            "\tex de,hl\n\tld bc,%lu\n"
            "\tadc hl,bc\n\tex de,hl\n",
            value & 0xffffUL, (value >> 16) & 0xffffUL);
}

static void mir_long_subtraction_cleanup_wide_call(
    MirStream *out)
{
    mir_stream_puts("\tex de,hl\n\tld hl,8\n\tadd hl,sp\n"
          "\tld sp,hl\n\tex de,hl\n", out);
}

static const char *mir_long_subtraction_compare_name(
    int operation)
{
    switch (operation) {
    case '<': return "__lts";
    case '>': return "__lgs";
    case TOK_LE: return "__les";
    case TOK_GE: return "__lks";
    default: fatal("invalid long subtraction comparison");
    }
    return "__lts";
}

static void mir_long_subtraction_emit_load(
    MirStream *out, const struct MirLongSubtractionRunner *plan,
    int global, int index)
{
    if (global)
        mir_long_subtraction_emit_global_load(
            out, plan, index);
    else
        mir_long_subtraction_emit_local_load(out, index);
}

static void mir_long_subtraction_finish_check(
    MirStream *out, const struct MirLongSubtractionRunner *plan,
    int check)
{
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->check_strings[check]);
    mir_machine_emit_symbol_call(out, plan->check_function);
    mir_emit_final_call_cleanup(out, 3);
}

static void mir_long_subtraction_emit_direct_check(
    MirStream *out, const struct MirLongSubtractionRunner *plan,
    int check, int global, int left, int right,
    int operation, int addend)
{
    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n",
            plan->check_wants[check]);
    mir_long_subtraction_emit_load(
        out, plan, global, left);
    if (addend >= 0)
        mir_long_subtraction_emit_add(
            out, plan->direct_addends[addend]);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_long_subtraction_emit_load(
        out, plan, global, right);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_emit_runtime_call(
        out, mir_long_subtraction_compare_name(operation));
    mir_long_subtraction_cleanup_wide_call(out);
    mir_long_subtraction_finish_check(out, plan, check);
}

static void mir_long_subtraction_emit_counter_check(
    MirStream *out, const struct MirLongSubtractionRunner *plan,
    int check, int boolean)
{
    int done = new_label();

    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n",
            plan->check_wants[check]);
    mir_stream_puts("\tld l,(ix-18)\n\tld h,(ix-17)\n", out);
    if (boolean) {
        mir_stream_puts("\tld a,h\n\tor l\n\tld hl,0\n", out);
        mir_stream_printf(out, "\tjp z,L%d\n\tinc hl\nL%d:\n",
                done, done);
    }
    mir_long_subtraction_finish_check(out, plan, check);
}

static void mir_long_subtraction_emit_helper_check(
    MirStream *out, const struct MirLongSubtractionRunner *plan,
    int check, int left, int right, int add)
{
    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n",
            plan->check_wants[check]);
    mir_long_subtraction_emit_local_load(out, right);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_long_subtraction_emit_local_load(out, left);
    if (add)
        mir_long_subtraction_emit_add(
            out, plan->helper_addend);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->helper_function);
    mir_long_subtraction_cleanup_wide_call(out);
    mir_long_subtraction_finish_check(out, plan, check);
}

static void mir_long_subtraction_emit_condition(
    MirStream *out, int left, int right,
    unsigned long addend, int has_addend)
{
    mir_long_subtraction_emit_local_load(out, left);
    if (has_addend)
        mir_long_subtraction_emit_add(out, addend);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_long_subtraction_emit_local_load(out, right);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_emit_runtime_call(out, "__lts");
    mir_long_subtraction_cleanup_wide_call(out);
}

static void mir_emit_long_subtraction_runner(
    MirStream *out, const struct MirLongSubtractionRunner *plan)
{
    static const struct {
        unsigned char global;
        unsigned char left;
        unsigned char right;
        int operation;
        signed char addend;
    } direct[MIR_LONG_SUBTRACTION_DIRECT_COUNT] = {
        {0, 0, 1, '<', -1}, {0, 1, 0, '<', -1},
        {0, 1, 0, '>', -1}, {0, 0, 1, '>', -1},
        {0, 0, 1, TOK_LE, -1}, {0, 0, 0, TOK_LE, -1},
        {0, 1, 0, TOK_GE, -1}, {0, 0, 1, TOK_GE, -1},
        {0, 2, 0, '<', -1}, {0, 1, 3, '<', -1},
        {0, 0, 1, '<', 0}, {0, 0, 1, '>', 1},
        {0, 1, 0, '<', 2}, {0, 0, 1, TOK_LE, 3},
        {0, 0, 1, TOK_GE, 4}, {0, 0, 1, '<', -1},
        {0, 1, 0, '<', -1}, {0, 0, 1, '<', 5},
        {1, 0, 1, '<', -1}, {1, 0, 1, '<', 6}
    };
    int while_loop = new_label();
    int while_done = new_label();
    int for_loop = new_label();
    int for_done = new_label();
    int success = new_label();
    int finish = new_label();
    int check;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-18\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    for (check = 0; check < 4; ++check)
        mir_long_subtraction_emit_local_store(
            out, check, plan->initial_values[check]);
    for (check = 0; check < 10; ++check)
        mir_long_subtraction_emit_direct_check(
            out, plan, check, direct[check].global,
            direct[check].left, direct[check].right,
            direct[check].operation, direct[check].addend);

    for (check = 0; check < 2; ++check)
        mir_long_subtraction_emit_local_store(
            out, check, plan->addition_values[check]);
    for (check = 10; check < 15; ++check)
        mir_long_subtraction_emit_direct_check(
            out, plan, check, direct[check].global,
            direct[check].left, direct[check].right,
            direct[check].operation, direct[check].addend);

    for (check = 0; check < 2; ++check)
        mir_long_subtraction_emit_local_store(
            out, check, plan->pointer_values[check]);
    for (check = 15; check < 18; ++check)
        mir_long_subtraction_emit_direct_check(
            out, plan, check, direct[check].global,
            direct[check].left, direct[check].right,
            direct[check].operation, direct[check].addend);

    for (check = 0; check < 2; ++check)
        mir_long_subtraction_emit_global_store(
            out, plan, check, plan->global_values[check]);
    for (check = 18; check < 20; ++check)
        mir_long_subtraction_emit_direct_check(
            out, plan, check, direct[check].global,
            direct[check].left, direct[check].right,
            direct[check].operation, direct[check].addend);

    for (check = 0; check < 2; ++check)
        mir_long_subtraction_emit_local_store(
            out, check, plan->while_values[check]);
    mir_stream_puts("\tld hl,0\n\tld (ix-18),l\n"
          "\tld (ix-17),h\n", out);
    mir_stream_printf(out, "L%d:\n", while_loop);
    mir_long_subtraction_emit_condition(
        out, 0, 1,
        plan->while_condition_addend, 1);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", while_done);
    mir_stream_puts("\tld l,(ix-18)\n\tld h,(ix-17)\n\tinc hl\n"
          "\tld (ix-18),l\n\tld (ix-17),h\n", out);
    mir_long_subtraction_emit_local_load(out, 0);
    mir_long_subtraction_emit_add(out, plan->while_step);
    mir_machine_emit_ix_wide_store(out, -16);
    mir_stream_printf(out,
            "\tld l,(ix-18)\n\tld h,(ix-17)\n"
            "\tld de,%d\n\tor a\n\tsbc hl,de\n"
            "\tjp nc,L%d\n\tjp L%d\nL%d:\n",
            plan->while_limit + 1, while_done,
            while_loop, while_done);
    mir_long_subtraction_emit_counter_check(
        out, plan, 20, 1);
    mir_long_subtraction_emit_counter_check(
        out, plan, 21, 0);

    for (check = 0; check < 2; ++check)
        mir_long_subtraction_emit_local_store(
            out, check, plan->for_values[check]);
    mir_stream_puts("\tld hl,0\n\tld (ix-18),l\n"
          "\tld (ix-17),h\n", out);
    mir_stream_printf(out, "L%d:\n", for_loop);
    mir_long_subtraction_emit_condition(
        out, 1, 0, 0, 0);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", for_done);
    mir_long_subtraction_emit_local_load(out, 1);
    mir_long_subtraction_emit_add(out, plan->for_step);
    mir_machine_emit_ix_wide_store(out, -12);
    mir_stream_printf(out,
            "\tld l,(ix-18)\n\tld h,(ix-17)\n"
            "\tld de,%d\n\tor a\n\tsbc hl,de\n"
            "\tjp nc,L%d\n"
            "\tld l,(ix-18)\n\tld h,(ix-17)\n"
            "\tinc hl\n\tld (ix-18),l\n"
            "\tld (ix-17),h\n\tjp L%d\nL%d:\n",
            plan->for_limit + 1, for_done,
            for_loop, for_done);
    mir_long_subtraction_emit_counter_check(
        out, plan, 22, 1);

    for (check = 0; check < 2; ++check)
        mir_long_subtraction_emit_local_store(
            out, check, plan->helper_values[check]);
    mir_long_subtraction_emit_helper_check(
        out, plan, 23, 0, 1, 0);
    mir_long_subtraction_emit_helper_check(
        out, plan, 24, 1, 0, 0);
    mir_long_subtraction_emit_helper_check(
        out, plan, 25, 0, 1, 1);

    mir_machine_emit_global_word(
        out, plan->failures, 0);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n\tpush hl\n"
                 "\tld hl,S%d\n\tpush hl\n",
            success, plan->failure_string);
    mir_emit_runtime_call(out, plan->print_names[1]);
    mir_emit_final_call_cleanup(out, 2);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n"
                 "\tld hl,S%d\n\tpush hl\n",
            finish, success, plan->success_string);
    mir_emit_runtime_call(out, plan->print_names[0]);
    mir_emit_final_call_cleanup(out, 1);
    mir_stream_printf(out, "L%d:\n", finish);
    mir_machine_emit_global_word(
        out, plan->failures, 0);
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static void mir_emit_promotion_call_runner(
    MirStream *out, const struct MirPromotionCallRunner *plan)
{
    int success = new_label();
    int check;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_promotion_print(
        out, plan, plan->intro_string, 0, 1);
    mir_stream_puts("\tld hl,0\n", out);
    mir_machine_emit_global_word_store(
        out, plan->failures, 0);
    for (check = 0; check < MIR_PROMOTION_CHECK_COUNT; ++check) {
        mir_emit_final_call_constant(
            out, plan->values[check], 4);
        mir_emit_final_call_constant(
            out, plan->values[check], 4);
        mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
                plan->check_strings[check]);
        mir_machine_emit_symbol_call(
            out, plan->check_function);
        mir_emit_final_call_cleanup(out, 5);
    }
    mir_machine_emit_global_word(
        out, plan->failures, 0);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n\tpush hl\n", success);
    mir_promotion_print(
        out, plan, plan->failure_string, 1, 2);
    mir_stream_puts("\tld hl,1\n\tret\n", out);
    mir_stream_printf(out, "L%d:\n", success);
    mir_promotion_print(
        out, plan, plan->success_string, 2, 1);
    mir_stream_puts("\tld hl,0\n\tret\n", out);
}

static int mir_inline_call_sum_signed_word_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           (type & 15) == TYPE_INT &&
           type_size(type) == 2 &&
           (type & TYPE_UNSIGNED) == 0;
}

static int mir_inline_call_sum_word_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
           (type & 15) == TYPE_INT &&
           type_size(type) == 2;
}

static int mir_legal_filter_control_edges_match(
    const int (*edges)[2], int edge_count)
{
    int edge;

    for (edge = 0; edge < edge_count; ++edge)
        if (mir.insns[edges[edge][0]].label !=
            mir.insns[edges[edge][1]].label)
            return 0;
    return 1;
}

static struct Sym *mir_legal_filter_function(
    int call_instruction, int argument_count, int return_size)
{
    const struct MirInsn *call = &mir.insns[call_instruction];
    struct Sym *function = find_global(call->name);
    int arguments[2];

    if (function == NULL || function->storage != SC_FUNC ||
        !function->is_defined || function->is_funcptr ||
        !function->has_proto ||
        function->proto_nargs != argument_count ||
        function->proto_variadic ||
        type_size(function->type) != return_size ||
        !mir_machine_call_arguments(
            call, argument_count, arguments))
        return NULL;
    return function;
}

static int mir_match_legal_move_filter_schedule(
    struct MirLegalMoveFilterSchedule *plan)
{
    static const int expected_opcodes[117] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_ARG, MIR_CALL, MIR_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_STORE, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_PHI,
        MIR_NOP, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_LOAD,
        MIR_NOP, MIR_STORE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_UNARY, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_INDEX_ADDRESS, MIR_ARG, MIR_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT, MIR_NOP, MIR_LABEL,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL
    };
    static const int control_edges[][2] = {
        {21, 41}, {40, 14}, {61, 116}, {73, 101}, {115, 53}
    };
    const struct MirInsn *ply = &mir.insns[1];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 117 || mir_cfg_block_count() != 8 ||
        mir.has_vla || mir.local_bytes != 6 ||
        mir.aggregate_temp_bytes != 0 ||
        !mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "legal-move-filter-schedule", "opcodes");
    if (!mir_legal_filter_control_edges_match(
            control_edges,
            (int)(sizeof(control_edges) / sizeof(control_edges[0]))))
        return mir_machine_reject(
            "legal-move-filter-schedule", "control-flow");
    if (type_ptr_depth(ply->type) != 0 ||
        (ply->type & 15) != TYPE_INT ||
        (ply->type & TYPE_UNSIGNED) != 0 ||
        type_size(ply->type) != 2 ||
        !mir_machine_parameter_value_offset(
            ply->dst, &plan->ply_stack_offset) ||
        plan->ply_stack_offset != 2)
        return mir_machine_reject(
            "legal-move-filter-schedule", "parameter");

    plan->generate_function =
        mir_legal_filter_function(4, 1, 0);
    plan->copy_function =
        mir_legal_filter_function(34, 2, 0);
    plan->apply_function =
        mir_legal_filter_function(68, 1, 0);
    plan->check_function =
        mir_legal_filter_function(71, 1, 2);
    plan->undo_function =
        mir_legal_filter_function(108, 1, 0);
    if (plan->generate_function == NULL ||
        plan->copy_function == NULL ||
        plan->apply_function == NULL ||
        plan->check_function == NULL ||
        plan->undo_function == NULL ||
        find_global(mir.insns[89].name) != plan->copy_function)
        return mir_machine_reject(
            "legal-move-filter-schedule", "calls");
    plan->move_counts = find_global(mir.insns[5].name);
    plan->temporary_moves = find_global(mir.insns[22].name);
    plan->moves = find_global(mir.insns[28].name);
    plan->side = find_global(mir.insns[47].name);
    if (plan->move_counts == NULL ||
        plan->temporary_moves == NULL ||
        plan->moves == NULL || plan->side == NULL ||
        plan->move_counts->storage != SC_GLOBAL ||
        plan->temporary_moves->storage != SC_GLOBAL ||
        plan->moves->storage != SC_GLOBAL ||
        plan->side->storage != SC_GLOBAL ||
        !plan->move_counts->is_array ||
        !plan->temporary_moves->is_array ||
        !plan->moves->is_array ||
        plan->move_counts->is_volatile ||
        plan->temporary_moves->is_volatile ||
        plan->moves->is_volatile ||
        plan->move_counts->pointee_is_volatile ||
        plan->temporary_moves->pointee_is_volatile ||
        plan->moves->pointee_is_volatile ||
        plan->move_counts->elem_size != 2 ||
        plan->temporary_moves->elem_size != 1024 ||
        plan->moves->elem_size != 1024 ||
        plan->side->is_array || plan->side->is_volatile ||
        strcmp(mir.insns[5].name, mir.insns[42].name) != 0 ||
        strcmp(mir.insns[5].name, mir.insns[77].name) != 0 ||
        strcmp(mir.insns[5].name, mir.insns[90].name) != 0 ||
        strcmp(mir.insns[22].name, mir.insns[62].name) != 0 ||
        strcmp(mir.insns[22].name, mir.insns[83].name) != 0 ||
        strcmp(mir.insns[22].name, mir.insns[102].name) != 0 ||
        strcmp(mir.insns[28].name, mir.insns[74].name) != 0)
        return mir_machine_reject(
            "legal-move-filter-schedule", "globals");
    plan->ply_stride = (int)mir.insns[24].immediate;
    plan->move_stride = (int)mir.insns[26].immediate;
    if (plan->ply_stride != 1024 ||
        plan->move_stride != 8 ||
        mir.insns[30].immediate != plan->ply_stride ||
        mir.insns[32].immediate != plan->move_stride ||
        mir.insns[64].immediate != plan->ply_stride ||
        mir.insns[66].immediate != plan->move_stride ||
        mir.insns[76].immediate != plan->ply_stride ||
        mir.insns[81].immediate != plan->move_stride ||
        mir.insns[85].immediate != plan->ply_stride ||
        mir.insns[87].immediate != plan->move_stride ||
        mir.insns[104].immediate != plan->ply_stride ||
        mir.insns[106].immediate != plan->move_stride)
        return mir_machine_reject(
            "legal-move-filter-schedule", "layout");
    return 1;
}

static void mir_emit_legal_filter_array_address(
    MirStream *out, struct Sym *array, int index_offset)
{
    const char *array_name =
        asm_name_for(sym_asm_name(array));

    mir_stream_printf(out,
            "\tpush iy\n\tpop hl\n\tld h,l\n\tld l,0\n"
            "\tadd hl,hl\n\tadd hl,hl\n"
            "\tld de,%s\n\tadd hl,de\n\tpush hl\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tadd hl,hl\n\tadd hl,hl\n\tadd hl,hl\n"
            "\tpop de\n\tadd hl,de\n",
            array_name, index_offset, index_offset + 1);
}

static void mir_emit_legal_filter_count_address(
    MirStream *out, const struct MirLegalMoveFilterSchedule *plan)
{
    const char *count_name =
        asm_name_for(sym_asm_name(plan->move_counts));

    mir_stream_printf(out,
            "\tpush iy\n\tpop hl\n\tadd hl,hl\n"
            "\tld de,%s\n\tadd hl,de\n",
            count_name);
}

static void mir_emit_legal_move_filter_schedule(
    MirStream *out, const struct MirLegalMoveFilterSchedule *plan)
{
    const char *side_name =
        asm_name_for(sym_asm_name(plan->side));
    const char *moves_name =
        asm_name_for(sym_asm_name(plan->moves));
    int copy_loop = new_label();
    int copy_done = new_label();
    int filter_loop = new_label();
    int reject_move = new_label();
    int filter_done = new_label();

    mir_stream_puts(";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-6\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tpush hl\n\tpop iy\n\tpush iy\n",
            plan->ply_stack_offset + 4,
            plan->ply_stack_offset + 5);
    mir_machine_emit_symbol_call(out, plan->generate_function);
    mir_stream_puts("\tpop bc\n", out);
    mir_emit_legal_filter_count_address(out, plan);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld (ix-4),e\n\tld (ix-3),d\n"
          "\txor a\n\tld (ix-2),a\n\tld (ix-1),a\n", out);

    mir_stream_printf(out, "L%d:\n", copy_loop);
    mir_stream_puts("\tld l,(ix-2)\n\tld h,(ix-1)\n"
          "\tld e,(ix-4)\n\tld d,(ix-3)\n"
          "\tld a,h\n\txor 80h\n\tld h,a\n"
          "\tld a,d\n\txor 80h\n\tld d,a\n"
          "\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp nc,L%d\n", copy_done);
    mir_emit_legal_filter_array_address(
        out, plan->moves, -2);
    mir_stream_puts("\tpush hl\n", out);
    mir_emit_legal_filter_array_address(
        out, plan->temporary_moves, -2);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->copy_function);
    mir_stream_printf(out,
            "\tpop bc\n\tpop bc\n\tinc (ix-2)\n\tjp nz,L%d\n"
            "\tinc (ix-1)\n\tjp L%d\nL%d:\n",
            copy_loop, copy_loop, copy_done);

    mir_emit_legal_filter_count_address(out, plan);
    mir_stream_puts("\txor a\n\tld (hl),a\n\tinc hl\n\tld (hl),a\n", out);
    mir_stream_printf(out,
            "\tld hl,(%s)\n\tld (ix-6),l\n\tld (ix-5),h\n"
            "\txor a\n\tld (ix-2),a\n\tld (ix-1),a\n"
            "L%d:\n",
            side_name, filter_loop);
    mir_stream_puts("\tld l,(ix-2)\n\tld h,(ix-1)\n"
          "\tld e,(ix-4)\n\tld d,(ix-3)\n"
          "\tld a,h\n\txor 80h\n\tld h,a\n"
          "\tld a,d\n\txor 80h\n\tld d,a\n"
          "\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp nc,L%d\n", filter_done);
    mir_emit_legal_filter_array_address(
        out, plan->temporary_moves, -2);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->apply_function);
    mir_stream_puts("\tpop bc\n\tld l,(ix-6)\n\tld h,(ix-5)\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->check_function);
    mir_stream_printf(out,
            "\tpop bc\n\tld a,h\n\tor l\n\tjp nz,L%d\n",
            reject_move);

    mir_emit_legal_filter_array_address(
        out, plan->temporary_moves, -2);
    mir_stream_puts("\tpush hl\n", out);
    mir_emit_legal_filter_count_address(out, plan);
    mir_stream_printf(out,
          "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tpush iy\n\tpop hl\n\tld h,l\n\tld l,0\n"
          "\tadd hl,hl\n\tadd hl,hl\n"
          "\tld de,%s\n\tadd hl,de\n\tpush hl\n"
          "\tld l,c\n\tld h,b\n"
          "\tadd hl,hl\n\tadd hl,hl\n\tadd hl,hl\n"
          "\tpop de\n\tadd hl,de\n\tpush hl\n",
          moves_name);
    mir_machine_emit_symbol_call(out, plan->copy_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_emit_legal_filter_count_address(out, plan);
    mir_stream_puts("\tinc (hl)\n\tjp nz,", out);
    mir_stream_printf(out, "L%d\n\tinc hl\n\tinc (hl)\n", reject_move);

    mir_stream_printf(out, "L%d:\n", reject_move);
    mir_emit_legal_filter_array_address(
        out, plan->temporary_moves, -2);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->undo_function);
    mir_stream_printf(out,
            "\tpop bc\n\tinc (ix-2)\n\tjp nz,L%d\n"
            "\tinc (ix-1)\n\tjp L%d\n"
            "L%d:\n\tld sp,ix\n\tpop ix\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            filter_loop, filter_loop, filter_done);
}

static int mir_match_statement_vm_schedule(
    struct MirStatementVmSchedule *plan)
{
    static const unsigned char expected_opcodes[643] = {
        MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_STORE, MIR_LOAD, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_LOAD, MIR_UNARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_LOAD, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP, MIR_STORE, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_JUMP, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_NOP, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_NOP, MIR_JUMP,
        MIR_LABEL, MIR_CALL, MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_LOAD,
        MIR_ARG, MIR_CALL, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_LABEL, MIR_LOAD, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_INDEX_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD, MIR_CONST, MIR_CONST, MIR_BINARY, MIR_BINARY,
        MIR_STORE_INDIRECT, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_NOP, MIR_STORE, MIR_NOP, MIR_JUMP,
        MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL,
        MIR_STORE, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_STORE, MIR_LOAD, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_STORE,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_LOAD, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_BINARY,
        MIR_INDEX_ADDRESS, MIR_LOAD, MIR_UNARY, MIR_STORE_INDIRECT,
        MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_LABEL, MIR_PHI, MIR_CONST, MIR_BINARY, MIR_BINARY, MIR_STORE,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_LOAD, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL,
        MIR_JUMP, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_LOAD,
        MIR_LOAD, MIR_INDEX_ADDRESS, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_UNARY, MIR_STORE_INDIRECT, MIR_LOAD, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_INDEX_ADDRESS, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_CONST, MIR_BINARY, MIR_UNARY, MIR_STORE_INDIRECT, MIR_NOP,
        MIR_LABEL, MIR_NOP, MIR_LOAD, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_INDEX_ADDRESS, MIR_NOP, MIR_STORE, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD, MIR_CONST, MIR_CONST, MIR_BINARY,
        MIR_BINARY, MIR_STORE_INDIRECT, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_STORE_INDIRECT, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL,
        MIR_STORE_INDIRECT, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL,
        MIR_STORE_INDIRECT, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_INDEX_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_LOAD, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_STORE, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_STORE, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_PHI, MIR_BRANCH_FALSE, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_NOP, MIR_STORE, MIR_NOP, MIR_JUMP,
        MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_JUMP, MIR_NOP, MIR_LABEL, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_CALL, MIR_NOP, MIR_JUMP, MIR_NOP,
        MIR_LABEL, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_LABEL, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_LABEL, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_NOP, MIR_STORE, MIR_NOP, MIR_JUMP,
        MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_NOP, MIR_JUMP, MIR_NOP, MIR_LABEL,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_NOP, MIR_LABEL,
        MIR_JUMP, MIR_LABEL
    };
    static const int die_calls[] = {
        137, 145, 174, 271, 485, 545, 592
    };
    static const int evaluate_calls[] = {180, 330, 338, 469, 559};
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 643 || mir_cfg_block_count() != 84 ||
        mir.has_vla || mir.local_bytes != 42 ||
        mir.aggregate_temp_bytes != 0 ||
        !mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "statement-vm-schedule", "opcodes");

    plan->statements = find_global(mir.insns[1].name);
    plan->statement_count = find_global(mir.insns[5].name);
    plan->program_counter = find_global(mir.insns[3].name);
    plan->halted = find_global(mir.insns[12].name);
    plan->calls = find_global(mir.insns[147].name);
    plan->call_count = find_global(mir.insns[139].name);
    plan->loops = find_global(mir.insns[296].name);
    plan->loop_count = find_global(mir.insns[168].name);
    plan->symbols = find_global(mir.insns[186].name);
    plan->memory = find_global(mir.insns[197].name);
    plan->memory_capacity = find_global(mir.insns[254].name);
    if (plan->statements == NULL ||
        plan->statement_count == NULL ||
        plan->program_counter == NULL || plan->halted == NULL ||
        plan->calls == NULL || plan->call_count == NULL ||
        plan->loops == NULL || plan->loop_count == NULL ||
        plan->symbols == NULL || plan->memory == NULL ||
        plan->memory_capacity == NULL ||
        plan->statements->is_volatile ||
        plan->statement_count->is_volatile ||
        plan->program_counter->is_volatile ||
        plan->halted->is_volatile ||
        plan->calls->is_volatile ||
        plan->call_count->is_volatile ||
        plan->loops->is_volatile ||
        plan->loop_count->is_volatile ||
        plan->symbols->is_volatile ||
        plan->memory->is_volatile ||
        plan->memory_capacity->is_volatile ||
        plan->statements->pointee_is_volatile ||
        plan->calls->pointee_is_volatile ||
        plan->loops->pointee_is_volatile ||
        plan->symbols->pointee_is_volatile ||
        plan->memory->pointee_is_volatile)
        return mir_machine_reject(
            "statement-vm-schedule", "globals");
    if (find_global(mir.insns[4].name) != plan->statements ||
        find_global(mir.insns[16].name) != plan->statements ||
        find_global(mir.insns[148].name) != plan->call_count ||
        find_global(mir.insns[151].name) != plan->call_count ||
        find_global(mir.insns[273].name) != plan->memory ||
        find_global(mir.insns[281].name) != plan->memory ||
        find_global(mir.insns[297].name) != plan->loop_count ||
        find_global(mir.insns[300].name) != plan->loop_count ||
        find_global(mir.insns[351].name) != plan->loops ||
        find_global(mir.insns[352].name) != plan->loop_count ||
        find_global(mir.insns[369].name) != plan->loops ||
        find_global(mir.insns[370].name) != plan->loop_count)
        return mir_machine_reject(
            "statement-vm-schedule", "global-uses");

    plan->return_function = find_global(mir.insns[115].name);
    plan->write_function = find_global(mir.insns[121].name);
    plan->die_function = find_global(mir.insns[137].name);
    plan->evaluate_function = find_global(mir.insns[180].name);
    plan->bump_function = find_global(mir.insns[384].name);
    plan->assign_function = find_global(mir.insns[525].name);
    if (plan->return_function == NULL ||
        plan->write_function == NULL ||
        plan->die_function == NULL ||
        plan->evaluate_function == NULL ||
        plan->bump_function == NULL ||
        plan->assign_function == NULL ||
        plan->return_function->proto_nargs != 0 ||
        plan->write_function->proto_nargs != 1 ||
        plan->die_function->proto_nargs != 1 ||
        plan->evaluate_function->proto_nargs != 1 ||
        plan->bump_function->proto_nargs != 2 ||
        plan->assign_function->proto_nargs != 3 ||
        find_global(mir.insns[502].name) !=
            plan->return_function ||
        find_global(mir.insns[626].name) !=
            plan->assign_function)
        return mir_machine_reject(
            "statement-vm-schedule", "calls");
    for (item = 0;
         item < (int)(sizeof(die_calls) /
                      sizeof(die_calls[0]));
         ++item)
        if (find_global(mir.insns[die_calls[item]].name) !=
            plan->die_function)
            return mir_machine_reject(
                "statement-vm-schedule", "die-calls");
    for (item = 0;
         item < (int)(sizeof(evaluate_calls) /
                      sizeof(evaluate_calls[0]));
         ++item)
        if (find_global(mir.insns[evaluate_calls[item]].name) !=
            plan->evaluate_function)
            return mir_machine_reject(
                "statement-vm-schedule", "evaluate-calls");

    plan->statement_stride = (int)mir.insns[6].immediate;
    plan->statement_op_offset = (int)mir.insns[43].immediate;
    plan->statement_target_offset = (int)mir.insns[130].immediate;
    plan->statement_ae_offset = (int)mir.insns[177].immediate;
    plan->statement_symbol_offset = (int)mir.insns[183].immediate;
    plan->statement_be_offset = (int)mir.insns[327].immediate;
    plan->statement_ce_offset = (int)mir.insns[335].immediate;
    plan->statement_action_offset = (int)mir.insns[472].immediate;
    plan->statement_action_target_offset =
        (int)mir.insns[478].immediate;
    plan->statement_action_symbol_offset =
        (int)mir.insns[514].immediate;
    plan->statement_action_index_offset =
        (int)mir.insns[518].immediate;
    plan->statement_action_rhs_offset =
        (int)mir.insns[522].immediate;
    plan->statement_target_count_offset =
        (int)mir.insns[568].immediate;
    plan->statement_targets_offset =
        (int)mir.insns[581].immediate;
    plan->loop_stride = (int)mir.insns[301].immediate;
    plan->loop_label_offset = (int)mir.insns[305].immediate;
    plan->loop_pc_offset = (int)mir.insns[311].immediate;
    plan->loop_symbol_offset = (int)mir.insns[319].immediate;
    plan->loop_end_offset = (int)mir.insns[325].immediate;
    plan->loop_step_offset = (int)mir.insns[333].immediate;
    plan->symbol_stride = (int)mir.insns[188].immediate;
    plan->symbol_type_offset = (int)mir.insns[191].immediate;
    plan->symbol_base_offset = (int)mir.insns[199].immediate;
    plan->byte_type = (int)mir.insns[193].immediate;
    plan->call_limit = (int)mir.insns[140].immediate;
    plan->loop_limit = (int)mir.insns[169].immediate;
    plan->opcode_count = (int)mir.insns[95].immediate + 1;
    plan->action_goto = (int)mir.insns[474].immediate;
    plan->action_return = (int)mir.insns[499].immediate;
    plan->action_assign = (int)mir.insns[510].immediate;
    plan->bad_call_string_id = (int)mir.insns[135].immediate;
    plan->call_stack_string_id = (int)mir.insns[143].immediate;
    plan->loop_stack_string_id = (int)mir.insns[172].immediate;
    plan->bounds_string_id = (int)mir.insns[269].immediate;
    plan->bad_label_string_id = (int)mir.insns[483].immediate;
    if (plan->statement_stride != 66 ||
        plan->statement_op_offset != 6 ||
        plan->statement_target_offset != 8 ||
        plan->statement_symbol_offset != 14 ||
        plan->statement_ae_offset != 22 ||
        plan->statement_be_offset != 24 ||
        plan->statement_ce_offset != 26 ||
        plan->statement_action_offset != 28 ||
        plan->statement_action_target_offset != 30 ||
        plan->statement_action_symbol_offset != 34 ||
        plan->statement_action_index_offset != 40 ||
        plan->statement_action_rhs_offset != 42 ||
        plan->statement_target_count_offset != 44 ||
        plan->statement_targets_offset != 46 ||
        plan->loop_stride != 10 ||
        plan->loop_label_offset != 0 ||
        plan->loop_pc_offset != 2 ||
        plan->loop_symbol_offset != 4 ||
        plan->loop_end_offset != 6 ||
        plan->loop_step_offset != 8 ||
        plan->symbol_stride != 32 ||
        plan->symbol_type_offset != 26 ||
        plan->symbol_base_offset != 28 ||
        plan->byte_type != 1 ||
        plan->call_limit <= 0 || plan->call_limit > 255 ||
        plan->loop_limit <= 0 || plan->loop_limit > 255 ||
        plan->opcode_count != 11 ||
        plan->action_goto != 1 ||
        plan->action_return != 2 ||
        plan->action_assign != 3 ||
        plan->bad_call_string_id < 0 ||
        plan->call_stack_string_id < 0 ||
        plan->loop_stack_string_id < 0 ||
        plan->bounds_string_id < 0 ||
        plan->bad_label_string_id < 0)
        return mir_machine_reject(
            "statement-vm-schedule", "layout");
    return 1;
}

static void mir_statement_vm_sync_pc(
    MirStream *out, const struct MirStatementVmSchedule *plan)
{
    mir_stream_puts("\tpush iy\n\tpop hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->program_counter, 0);
}

static void mir_statement_vm_load_iy_word(
    MirStream *out, int offset)
{
    mir_stream_printf(out,
            "\tld l,(iy%+d)\n\tld h,(iy%+d)\n",
            offset, offset + 1);
}

static void mir_statement_vm_load_pointer_word(
    MirStream *out, int pointer_frame, int offset)
{
    mir_stream_printf(out,
            "\tld l,(ix%d)\n\tld h,(ix%d)\n",
            pointer_frame, pointer_frame + 1);
    if (offset != 0)
        mir_stream_printf(out, "\tld de,%d\n\tadd hl,de\n", offset);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n", out);
}

static void mir_statement_vm_store_pointer_word(
    MirStream *out, int pointer_frame, int offset)
{
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out,
            "\tld l,(ix%d)\n\tld h,(ix%d)\n",
            pointer_frame, pointer_frame + 1);
    if (offset != 0)
        mir_stream_printf(out, "\tld de,%d\n\tadd hl,de\n", offset);
    mir_stream_puts("\tpop de\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n",
          out);
}

static void mir_statement_vm_die(
    MirStream *out, const struct MirStatementVmSchedule *plan,
    int string_id)
{
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_machine_emit_symbol_call(out, plan->die_function);
    mir_stream_puts("\tpop bc\n", out);
}

static void mir_statement_vm_indexed_global(
    MirStream *out, struct Sym *root, int index_frame, int stride)
{
    mir_stream_printf(out,
            "\tld l,(ix%d)\n\tld h,(ix%d)\n",
            index_frame, index_frame + 1);
    mir_emit_mul_hl_const(out, (unsigned long)stride);
    mir_stream_puts("\tex de,hl\n", out);
    mir_machine_emit_global_word(out, root, 0);
    mir_stream_puts("\tadd hl,de\n", out);
}

static void mir_statement_vm_symbol_bounds(
    MirStream *out, const struct MirStatementVmSchedule *plan,
    int ok_label)
{
    int nonnegative = new_label();

    mir_stream_puts("\tld l,(ix-10)\n\tld h,(ix-9)\n"
          "\tbit 7,h\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", nonnegative);
    mir_statement_vm_die(
        out, plan, plan->bounds_string_id);
    mir_stream_printf(out, "L%d:\n", nonnegative);
    mir_machine_emit_global_word(
        out, plan->memory_capacity, 0);
    mir_stream_puts("\tex de,hl\n\tld l,(ix-10)\n\tld h,(ix-9)\n"
          "\tinc hl\n\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp c,L%d\n", ok_label);
    mir_statement_vm_die(
        out, plan, plan->bounds_string_id);
}

static void mir_statement_vm_store_symbol_value(
    MirStream *out, const struct MirStatementVmSchedule *plan)
{
    int word_value = new_label();
    int bounds_ok = new_label();
    int done = new_label();

    mir_statement_vm_indexed_global(
        out, plan->symbols, MIR_STMT_INDEX,
        plan->symbol_stride);
    mir_stream_puts("\tld (ix-4),l\n\tld (ix-3),h\n", out);
    mir_stream_printf(out, "\tld de,%d\n\tadd hl,de\n"
                 "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                 "\tld hl,%d\n\tor a\n\tsbc hl,de\n",
            plan->symbol_type_offset, plan->byte_type);
    mir_stream_printf(out, "\tjp nz,L%d\n", word_value);
    mir_statement_vm_load_pointer_word(
        out, MIR_STMT_POINTER, plan->symbol_base_offset);
    mir_stream_puts("\tld (ix-10),l\n\tld (ix-9),h\n", out);
    mir_machine_emit_global_word(out, plan->memory, 0);
    mir_stream_puts("\tld e,(ix-10)\n\tld d,(ix-9)\n\tadd hl,de\n"
          "\tld a,(ix-6)\n\tld (hl),a\n", out);
    mir_stream_printf(out, "\tjp L%d\n", done);

    mir_stream_printf(out, "L%d:\n", word_value);
    mir_statement_vm_load_pointer_word(
        out, MIR_STMT_POINTER, plan->symbol_base_offset);
    mir_stream_puts("\tld (ix-10),l\n\tld (ix-9),h\n", out);
    mir_statement_vm_symbol_bounds(out, plan, bounds_ok);
    mir_stream_printf(out, "L%d:\n", bounds_ok);
    mir_machine_emit_global_word(out, plan->memory, 0);
    mir_stream_puts("\tld e,(ix-10)\n\tld d,(ix-9)\n\tadd hl,de\n"
          "\tld e,(ix-6)\n\tld d,(ix-5)\n"
          "\tld (hl),e\n\tinc hl\n\tld (hl),d\n", out);
    mir_stream_printf(out, "L%d:\n", done);
}

static void mir_statement_vm_evaluate_field(
    MirStream *out, const struct MirStatementVmSchedule *plan,
    int field_offset)
{
    mir_statement_vm_load_iy_word(out, field_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->evaluate_function);
    mir_stream_puts("\tpop bc\n", out);
}

static void mir_statement_vm_advance(
    MirStream *out, const struct MirStatementVmSchedule *plan,
    int dispatch)
{
    mir_stream_printf(out, "\tld de,%d\n\tadd iy,de\n\tjp L%d\n",
            plan->statement_stride, dispatch);
}

static void mir_emit_statement_vm_schedule(
    MirStream *out, const struct MirStatementVmSchedule *plan)
{
    int cases[11];
    int dispatch = new_label();
    int table = new_label();
    int advance = new_label();
    int done = new_label();
    int bad_opcode = new_label();
    int item;

    for (item = 0; item < plan->opcode_count; ++item)
        cases[item] = new_label();
    mir_stream_puts(";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-10\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_word(out, plan->statements, 0);
    mir_stream_puts("\tpush hl\n\tpop iy\n", out);
    mir_machine_emit_global_word(out, plan->statement_count, 0);
    mir_emit_mul_hl_const(
        out, (unsigned long)plan->statement_stride);
    mir_stream_puts("\tpush iy\n\tpop de\n\tadd hl,de\n"
          "\tld (ix-2),l\n\tld (ix-1),h\n", out);

    mir_stream_printf(out, "L%d:\n", dispatch);
    mir_statement_vm_sync_pc(out, plan);
    mir_machine_emit_global_word(out, plan->halted, 0);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", done);
    mir_stream_puts("\tpush iy\n\tpop hl\n", out);
    mir_machine_emit_global_word(out, plan->statements, 0);
    mir_stream_puts("\tex de,hl\n\tpush iy\n\tpop hl\n"
          "\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp c,L%d\n", done);
    mir_stream_puts("\tpush iy\n\tpop hl\n"
          "\tld e,(ix-2)\n\tld d,(ix-1)\n"
          "\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp nc,L%d\n", done);
    mir_stream_printf(out,
            "\tld a,(iy%+d)\n\tor a\n\tjp nz,L%d\n"
            "\tld a,(iy%+d)\n\tcp %d\n\tjp nc,L%d\n"
            "\tld l,a\n\tld h,0\n\tadd hl,hl\n"
            "\tld de,L%d\n\tadd hl,de\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tex de,hl\n\tjp (hl)\nL%d:\n",
            plan->statement_op_offset + 1, bad_opcode,
            plan->statement_op_offset, plan->opcode_count,
            bad_opcode, table, table);
    for (item = 0; item < plan->opcode_count; ++item)
        mir_stream_printf(out, "\tdw L%d\n", cases[item]);
    mir_stream_printf(out, "L%d:\n\tjp L%d\n", bad_opcode, advance);

    mir_stream_printf(out, "L%d:\n\tjp L%d\n", cases[0], advance);
    mir_stream_printf(out, "L%d:\n\tld hl,1\n", cases[1]);
    mir_machine_emit_global_word_store(out, plan->halted, 0);
    mir_stream_printf(out, "\tjp L%d\n", dispatch);

    mir_stream_printf(out, "L%d:\n", cases[2]);
    mir_machine_emit_symbol_call(out, plan->return_function);
    mir_machine_emit_global_word(out, plan->program_counter, 0);
    mir_stream_puts("\tpush hl\n\tpop iy\n", out);
    mir_stream_printf(out, "\tjp L%d\n", dispatch);

    mir_stream_printf(out, "L%d:\n\tpush iy\n", cases[3]);
    mir_machine_emit_symbol_call(out, plan->write_function);
    mir_stream_puts("\tpop bc\n", out);
    mir_stream_printf(out, "\tjp L%d\n", advance);

    {
        int target_ok = new_label();
        int stack_ok = new_label();

        mir_stream_printf(out, "L%d:\n", cases[4]);
        mir_statement_vm_load_iy_word(
            out, plan->statement_target_offset);
        mir_stream_puts("\tld a,h\n\tor l\n", out);
        mir_stream_printf(out, "\tjp nz,L%d\n", target_ok);
        mir_statement_vm_die(
            out, plan, plan->bad_call_string_id);
        mir_stream_printf(out, "L%d:\n"
                     "\tld (ix-4),l\n\tld (ix-3),h\n",
                target_ok);
        mir_machine_emit_global_word(out, plan->call_count, 0);
        mir_stream_printf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                     "\tjp c,L%d\n",
                plan->call_limit, stack_ok);
        mir_statement_vm_die(
            out, plan, plan->call_stack_string_id);
        mir_stream_printf(out, "L%d:\n", stack_ok);
        mir_machine_emit_global_word(out, plan->call_count, 0);
        mir_stream_puts("\tld (ix-8),l\n\tld (ix-7),h\n\tinc hl\n",
              out);
        mir_machine_emit_global_word_store(
            out, plan->call_count, 0);
        mir_statement_vm_indexed_global(
            out, plan->calls, MIR_STMT_INDEX, 2);
        mir_stream_puts("\tpush iy\n\tpop de\n", out);
        mir_stream_printf(out, "\tld bc,%d\n\tex de,hl\n\tadd hl,bc\n"
                     "\tex de,hl\n\tld (hl),e\n\tinc hl\n"
                     "\tld (hl),d\n",
                plan->statement_stride);
        mir_stream_puts("\tld l,(ix-4)\n\tld h,(ix-3)\n"
              "\tpush hl\n\tpop iy\n", out);
        mir_stream_printf(out, "\tjp L%d\n", dispatch);
    }

    {
        int stack_ok = new_label();

        mir_stream_printf(out, "L%d:\n", cases[5]);
        mir_machine_emit_global_word(out, plan->loop_count, 0);
        mir_stream_printf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                     "\tjp c,L%d\n",
                plan->loop_limit, stack_ok);
        mir_statement_vm_die(
            out, plan, plan->loop_stack_string_id);
        mir_stream_printf(out, "L%d:\n", stack_ok);
        mir_statement_vm_evaluate_field(
            out, plan, plan->statement_ae_offset);
        mir_stream_puts("\tld (ix-6),l\n\tld (ix-5),h\n", out);
        mir_statement_vm_load_iy_word(
            out, plan->statement_symbol_offset);
        mir_stream_puts("\tld (ix-8),l\n\tld (ix-7),h\n", out);
        mir_statement_vm_store_symbol_value(out, plan);

        mir_machine_emit_global_word(out, plan->loop_count, 0);
        mir_stream_puts("\tld (ix-8),l\n\tld (ix-7),h\n\tinc hl\n",
              out);
        mir_machine_emit_global_word_store(
            out, plan->loop_count, 0);
        mir_statement_vm_indexed_global(
            out, plan->loops, MIR_STMT_INDEX,
            plan->loop_stride);
        mir_stream_puts("\tld (ix-4),l\n\tld (ix-3),h\n", out);
        mir_statement_vm_load_iy_word(
            out, plan->statement_target_offset);
        mir_statement_vm_store_pointer_word(
            out, MIR_STMT_POINTER, plan->loop_label_offset);
        mir_stream_puts("\tpush iy\n\tpop hl\n", out);
        mir_stream_printf(out, "\tld de,%d\n\tadd hl,de\n",
                plan->statement_stride);
        mir_statement_vm_store_pointer_word(
            out, MIR_STMT_POINTER, plan->loop_pc_offset);
        mir_statement_vm_load_iy_word(
            out, plan->statement_symbol_offset);
        mir_statement_vm_store_pointer_word(
            out, MIR_STMT_POINTER, plan->loop_symbol_offset);
        mir_statement_vm_evaluate_field(
            out, plan, plan->statement_be_offset);
        mir_statement_vm_store_pointer_word(
            out, MIR_STMT_POINTER, plan->loop_end_offset);
        mir_statement_vm_evaluate_field(
            out, plan, plan->statement_ce_offset);
        mir_statement_vm_store_pointer_word(
            out, MIR_STMT_POINTER, plan->loop_step_offset);
        mir_stream_printf(out, "\tjp L%d\n", advance);
    }

    {
        int no_loop = new_label();
        int label_match = new_label();
        int negative_step = new_label();
        int continue_loop = new_label();
        int finish_loop = new_label();

        mir_stream_printf(out, "L%d:\n", cases[6]);
        mir_machine_emit_global_word(out, plan->loop_count, 0);
        mir_stream_puts("\tld a,h\n\tor l\n", out);
        mir_stream_printf(out, "\tjp z,L%d\n", no_loop);
        mir_stream_puts("\tdec hl\n\tld (ix-8),l\n\tld (ix-7),h\n",
              out);
        mir_statement_vm_indexed_global(
            out, plan->loops, MIR_STMT_INDEX,
            plan->loop_stride);
        mir_stream_puts("\tld (ix-4),l\n\tld (ix-3),h\n", out);
        mir_statement_vm_load_pointer_word(
            out, MIR_STMT_POINTER, plan->loop_label_offset);
        mir_stream_puts("\tex de,hl\n\tpush iy\n\tpop hl\n"
              "\tor a\n\tsbc hl,de\n", out);
        mir_stream_printf(out, "\tjp z,L%d\n\tjp L%d\n",
                label_match, no_loop);
        mir_stream_printf(out, "L%d:\n", label_match);
        mir_statement_vm_load_pointer_word(
            out, MIR_STMT_POINTER, plan->loop_step_offset);
        mir_stream_puts("\tpush hl\n", out);
        mir_statement_vm_load_pointer_word(
            out, MIR_STMT_POINTER, plan->loop_symbol_offset);
        mir_stream_puts("\tpush hl\n", out);
        mir_machine_emit_symbol_call(out, plan->bump_function);
        mir_stream_puts("\tpop bc\n\tpop bc\n"
              "\tld (ix-6),l\n\tld (ix-5),h\n", out);
        mir_statement_vm_load_pointer_word(
            out, MIR_STMT_POINTER, plan->loop_step_offset);
        mir_stream_puts("\tbit 7,h\n", out);
        mir_stream_printf(out, "\tjp nz,L%d\n", negative_step);
        mir_stream_puts("\tld l,(ix-6)\n\tld h,(ix-5)\n"
              "\tld (ix-10),l\n\tld (ix-9),h\n", out);
        mir_statement_vm_load_pointer_word(
            out, MIR_STMT_POINTER, plan->loop_end_offset);
        mir_stream_puts("\tex de,hl\t\n\tld l,(ix-10)\n\tld h,(ix-9)\n"
              "\tld a,h\n\txor 128\n\tld h,a\n"
              "\tld a,d\n\txor 128\n\tld d,a\n"
              "\tor a\n\tsbc hl,de\n", out);
        mir_stream_printf(out, "\tjp c,L%d\n\tjp z,L%d\n\tjp L%d\n",
                continue_loop, continue_loop, finish_loop);
        mir_stream_printf(out, "L%d:\n", negative_step);
        mir_stream_puts("\tld l,(ix-6)\n\tld h,(ix-5)\n"
              "\tld (ix-10),l\n\tld (ix-9),h\n", out);
        mir_statement_vm_load_pointer_word(
            out, MIR_STMT_POINTER, plan->loop_end_offset);
        mir_stream_puts("\tex de,hl\n\tld l,(ix-10)\n\tld h,(ix-9)\n"
              "\tld a,h\n\txor 128\n\tld h,a\n"
              "\tld a,d\n\txor 128\n\tld d,a\n"
              "\tor a\n\tsbc hl,de\n", out);
        mir_stream_printf(out, "\tjp nc,L%d\n\tjp L%d\n",
                continue_loop, finish_loop);
        mir_stream_printf(out, "L%d:\n", continue_loop);
        mir_statement_vm_load_pointer_word(
            out, MIR_STMT_POINTER, plan->loop_pc_offset);
        mir_stream_puts("\tpush hl\n\tpop iy\n", out);
        mir_stream_printf(out, "\tjp L%d\n", dispatch);
        mir_stream_printf(out, "L%d:\n", finish_loop);
        mir_machine_emit_global_word(out, plan->loop_count, 0);
        mir_stream_puts("\tdec hl\n", out);
        mir_machine_emit_global_word_store(
            out, plan->loop_count, 0);
        mir_stream_printf(out, "L%d:\n\tjp L%d\n", no_loop, advance);
    }

    {
        int false_path = new_label();
        int action_goto = new_label();
        int action_return = new_label();
        int action_assign = new_label();
        int target_ok = new_label();

        mir_stream_printf(out, "L%d:\n", cases[7]);
        mir_statement_vm_evaluate_field(
            out, plan, plan->statement_ae_offset);
        mir_stream_puts("\tld a,h\n\tor l\n", out);
        mir_stream_printf(out, "\tjp z,L%d\n", false_path);
        mir_statement_vm_load_iy_word(
            out, plan->statement_action_offset);
        mir_stream_printf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                     "\tjp z,L%d\n",
                plan->action_goto, action_goto);
        mir_statement_vm_load_iy_word(
            out, plan->statement_action_offset);
        mir_stream_printf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                     "\tjp z,L%d\n",
                plan->action_return, action_return);
        mir_statement_vm_load_iy_word(
            out, plan->statement_action_offset);
        mir_stream_printf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                     "\tjp z,L%d\n\tjp L%d\n",
                plan->action_assign, action_assign, false_path);
        mir_stream_printf(out, "L%d:\n", action_goto);
        mir_statement_vm_load_iy_word(
            out, plan->statement_action_target_offset);
        mir_stream_puts("\tld a,h\n\tor l\n", out);
        mir_stream_printf(out, "\tjp nz,L%d\n", target_ok);
        mir_statement_vm_die(
            out, plan, plan->bad_label_string_id);
        mir_stream_printf(out, "L%d:\n\tpush hl\n\tpop iy\n\tjp L%d\n",
                target_ok, dispatch);
        mir_stream_printf(out, "L%d:\n", action_return);
        mir_machine_emit_symbol_call(out, plan->return_function);
        mir_machine_emit_global_word(out, plan->program_counter, 0);
        mir_stream_puts("\tpush hl\n\tpop iy\n", out);
        mir_stream_printf(out, "\tjp L%d\n", dispatch);
        mir_stream_printf(out, "L%d:\n", action_assign);
        mir_statement_vm_load_iy_word(
            out, plan->statement_action_rhs_offset);
        mir_stream_puts("\tpush hl\n", out);
        mir_statement_vm_load_iy_word(
            out, plan->statement_action_index_offset);
        mir_stream_puts("\tpush hl\n", out);
        mir_statement_vm_load_iy_word(
            out, plan->statement_action_symbol_offset);
        mir_stream_puts("\tpush hl\n", out);
        mir_machine_emit_symbol_call(out, plan->assign_function);
        mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n", out);
        mir_stream_printf(out, "L%d:\n\tjp L%d\n", false_path, advance);
    }

    {
        int target_ok = new_label();

        mir_stream_printf(out, "L%d:\n", cases[8]);
        mir_statement_vm_load_iy_word(
            out, plan->statement_target_offset);
        mir_stream_puts("\tld a,h\n\tor l\n", out);
        mir_stream_printf(out, "\tjp nz,L%d\n", target_ok);
        mir_statement_vm_die(
            out, plan, plan->bad_label_string_id);
        mir_stream_printf(out, "L%d:\n\tpush hl\n\tpop iy\n\tjp L%d\n",
                target_ok, dispatch);
    }

    {
        int out_of_range = new_label();
        int target_ok = new_label();

        mir_stream_printf(out, "L%d:\n", cases[9]);
        mir_statement_vm_evaluate_field(
            out, plan, plan->statement_ae_offset);
        mir_stream_puts("\tld (ix-8),l\n\tld (ix-7),h\n"
              "\tbit 7,h\n", out);
        mir_stream_printf(out, "\tjp nz,L%d\n", out_of_range);
        mir_stream_puts("\tld a,h\n\tor l\n", out);
        mir_stream_printf(out, "\tjp z,L%d\n", out_of_range);
        mir_statement_vm_load_iy_word(
            out, plan->statement_target_count_offset);
        mir_stream_puts("\tex de,hl\n\tld l,(ix-8)\n\tld h,(ix-7)\n"
              "\tld a,h\n\txor 128\n\tld h,a\n"
              "\tld a,d\n\txor 128\n\tld d,a\n"
              "\tor a\n\tsbc hl,de\n", out);
        mir_stream_printf(out, "\tjp c,L%d\n\tjp z,L%d\n",
                target_ok, target_ok);
        mir_stream_printf(out, "\tjp L%d\nL%d:\n",
                out_of_range, target_ok);
        mir_stream_puts("\tld l,(ix-8)\n\tld h,(ix-7)\n"
              "\tdec hl\n\tadd hl,hl\n", out);
        mir_stream_printf(out, "\tld de,%d\n\tadd hl,de\n"
                     "\tpush iy\n\tpop de\n\tadd hl,de\n"
                     "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                     "\tld a,d\n\tor e\n",
                plan->statement_targets_offset);
        {
            int nonnull = new_label();

            mir_stream_printf(out, "\tjp nz,L%d\n", nonnull);
            mir_statement_vm_die(
                out, plan, plan->bad_label_string_id);
            mir_stream_printf(out, "L%d:\n\tpush de\n\tpop iy\n\tjp L%d\n",
                    nonnull, dispatch);
        }
        mir_stream_printf(out, "L%d:\n\tjp L%d\n", out_of_range, advance);
    }

    mir_stream_printf(out, "L%d:\n", cases[10]);
    mir_statement_vm_load_iy_word(
        out, plan->statement_be_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_statement_vm_load_iy_word(
        out, plan->statement_ae_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_statement_vm_load_iy_word(
        out, plan->statement_symbol_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->assign_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n", out);

    mir_stream_printf(out, "L%d:\n", advance);
    mir_statement_vm_advance(out, plan, dispatch);
    mir_stream_printf(out,
            "L%d:\n\tld sp,ix\n\tpop ix\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            done);
}

static int mir_match_format_walk_schedule(
    struct MirFormatWalkSchedule *plan)
{
    static const unsigned char expected_opcodes[188] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_CONST, MIR_NOP,
        MIR_STORE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY,
        MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_PHI, MIR_NOP, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_NOP, MIR_LABEL,
        MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_NOP,
        MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL
    };
    const struct MirInsn *format = &mir.insns[1];
    const struct MirInsn *argument_count = &mir.insns[2];
    const struct MirInsn *values = &mir.insns[3];
    int print_arguments[2];
    int putchar_argument;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 188 || mir_cfg_block_count() != 36 ||
        mir.has_vla || mir.local_bytes != 6 ||
        mir.aggregate_temp_bytes != 0 ||
        !mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "format-walk-schedule", "opcodes");

    if (!mir_machine_parameter_value_offset(
            format->dst, &plan->format_stack_offset) ||
        !mir_machine_parameter_value_offset(
            argument_count->dst,
            &plan->argument_count_stack_offset) ||
        !mir_machine_parameter_value_offset(
            values->dst, &plan->values_stack_offset) ||
        plan->format_stack_offset < 2 ||
        plan->argument_count_stack_offset !=
            plan->format_stack_offset + 2 ||
        plan->values_stack_offset !=
            plan->argument_count_stack_offset + 2 ||
        plan->values_stack_offset > 121 ||
        type_ptr_depth(format->type) != 1 ||
        type_size(format->type) != 2 ||
        type_ptr_depth(argument_count->type) != 0 ||
        type_size(argument_count->type) != 2 ||
        type_ptr_depth(values->type) != 1 ||
        type_size(values->type) != 2 ||
        mir_machine_pointee_is_volatile(format) ||
        mir_machine_pointee_is_volatile(values))
        return mir_machine_reject(
            "format-walk-schedule", "parameters");

    plan->print_function = find_global(mir.insns[171].name);
    plan->putchar_function = find_global(mir.insns[178].name);
    if (plan->print_function == NULL ||
        plan->putchar_function == NULL ||
        plan->print_function->proto_nargs < 1 ||
        plan->putchar_function->proto_nargs != 1 ||
        plan->putchar_function->proto_variadic ||
        !mir_machine_two_call_arguments(
            &mir.insns[171], print_arguments) ||
        print_arguments[0] != mir.insns[161].dst ||
        print_arguments[1] != mir.insns[169].dst ||
        !mir_machine_single_call_argument(
            &mir.insns[178], &putchar_argument) ||
        putchar_argument != mir.insns[25].dst)
        return mir_machine_reject(
            "format-walk-schedule", "calls");
    plan->integer_format_string_id =
        (int)mir.insns[161].immediate;
    if (plan->integer_format_string_id < 0 ||
        !mir_machine_constant_equals(mir.insns[28].dst, '%') ||
        !mir_machine_constant_equals(mir.insns[62].dst, 'l') ||
        !mir_machine_constant_equals(mir.insns[74].dst, 'u') ||
        !mir_machine_constant_equals(mir.insns[98].dst, 'd') ||
        !mir_machine_constant_equals(mir.insns[118].dst, 'u') ||
        !mir_machine_constant_equals(mir.insns[130].dst, 'd') ||
        !mir_machine_constant_equals(mir.insns[165].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[183].dst, 1))
        return mir_machine_reject(
            "format-walk-schedule", "constants");
    return 1;
}

static void mir_emit_format_walk_schedule(
    MirStream *out, const struct MirFormatWalkSchedule *plan)
{
    int loop = new_label();
    int ordinary = new_label();
    int specifier = new_label();
    int print_done = new_label();
    int next_character = new_label();
    int done = new_label();
    int count_offset =
        plan->argument_count_stack_offset + 4;
    int values_offset = plan->values_stack_offset + 4;

    mir_stream_puts(";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-2\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tpush hl\n\tpop iy\n"
            "\txor a\n\tld (ix-2),a\n\tld (ix-1),a\n"
            "L%d:\n\tld a,(iy)\n\tor a\n\tjp z,L%d\n"
            "\tcp %d\n\tjp nz,L%d\n"
            "\tld a,(iy+1)\n\tor a\n\tjp z,L%d\n"
            "\tinc iy\n"
            "L%d:\n\tld a,(iy)\n\tcp %d\n"
            "\tjp nz,L%d\n\tinc iy\n\tjp L%d\n"
            "L%d:\n",
            plan->format_stack_offset + 4,
            plan->format_stack_offset + 5,
            loop, done, '%', ordinary, ordinary,
            specifier, 'l', print_done, specifier, print_done);
    mir_stream_puts("\tld l,(ix-2)\n\tld h,(ix-1)\n", out);
    mir_stream_printf(out,
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
            count_offset, count_offset + 1);
    mir_stream_puts("\tld a,h\n\txor 128\n\tld h,a\n"
          "\tld a,d\n\txor 128\n\tld d,a\n"
          "\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp nc,L%d\n", next_character);
    mir_stream_puts("\tld l,(ix-2)\n\tld h,(ix-1)\n\tadd hl,hl\n",
          out);
    mir_stream_printf(out,
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
            values_offset, values_offset + 1);
    mir_stream_puts("\tadd hl,de\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tpush de\n\tinc (ix-2)\n", out);
    {
        int no_carry = new_label();

        mir_stream_printf(out,
                "\tjp nz,L%d\n\tinc (ix-1)\nL%d:\n",
                no_carry, no_carry);
    }
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->integer_format_string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_stream_printf(out, "\tjp L%d\n", next_character);

    mir_stream_printf(out, "L%d:\n\tld l,a\n", ordinary);
    mir_stream_puts("\trla\n\tsbc a,a\n\tld h,a\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->putchar_function);
    mir_stream_puts("\tpop bc\n", out);

    mir_stream_printf(out,
            "L%d:\n\tinc iy\n\tjp L%d\n"
            "L%d:\n\tld sp,ix\n\tpop ix\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            next_character, loop, done);
}

static int mir_match_inline_call_sum_schedule(
    struct MirInlineCallSumSchedule *plan)
{
    static const int expected_opcodes[37] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_CONST, MIR_NOP,
        MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP,
        MIR_PHI, MIR_PHI, MIR_NOP, MIR_NOP,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_ARG, MIR_CALL, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_RETURN
    };
    const struct MirInsn *values = &mir.insns[1];
    const struct MirInsn *count = &mir.insns[2];
    const struct MirInsn *total_store = &mir.insns[5];
    const struct MirInsn *index_store = &mir.insns[8];
    const struct MirInsn *total_phi = &mir.insns[12];
    const struct MirInsn *index_phi = &mir.insns[13];
    const struct MirInsn *call = &mir.insns[22];
    const struct MirInsn *load = &mir.insns[24];
    const char *assembly_name;
    int argument;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 37 || mir_cfg_block_count() != 4 ||
        mir.has_vla ||
        !mir_inline_call_sum_signed_word_type(mir.return_type))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
        return mir_machine_reject(
            "inline-call-sum-schedule", "opcodes");

    if (!mir_machine_parameter_value_offset(
        values->dst, &plan->values_stack_offset) ||
        !mir_machine_parameter_value_offset(
        count->dst, &plan->count_stack_offset) ||
        plan->count_stack_offset !=
        plan->values_stack_offset + 2 ||
        !mir_inline_call_sum_word_pointer_type(values->type) ||
        mir_machine_pointee_is_volatile(values) ||
        !mir_machine_named_nonvolatile(values) ||
        !mir_inline_call_sum_signed_word_type(count->type) ||
        !mir_machine_named_nonvolatile(count) ||
        values->object < 0 || count->object < 0 ||
        values->object == count->object ||
        mir.insns[10].object != values->object ||
        mir.insns[11].object != count->object ||
        mir.insns[15].object != count->object)
        return mir_machine_reject(
        "inline-call-sum-schedule", "parameters");

    if (!mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[6].dst, 0) ||
        !mir_machine_unobservable_local_store(total_store) ||
        !mir_machine_unobservable_local_store(index_store) ||
        total_store->src1 != mir.insns[3].dst ||
        index_store->src1 != mir.insns[6].dst ||
        total_store->object < 0 || index_store->object < 0 ||
        total_store->object == index_store->object ||
        total_phi->object != total_store->object ||
        index_phi->object != index_store->object ||
        !mir_machine_same_location(total_phi, total_store) ||
        !mir_machine_same_location(index_phi, index_store) ||
        total_phi->src1 != mir.insns[3].dst ||
        total_phi->src2 != mir.insns[25].dst ||
        index_phi->src1 != mir.insns[6].dst ||
        index_phi->src2 != mir.insns[31].dst ||
        total_phi->phi_pred1 != mir.insns[0].label ||
        total_phi->phi_pred2 != mir.insns[28].label ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[28].label ||
        !mir_inline_call_sum_signed_word_type(total_phi->type) ||
        !mir_inline_call_sum_signed_word_type(index_phi->type))
        return mir_machine_reject(
        "inline-call-sum-schedule", "loop-state");

    if (mir.insns[16].src1 != index_phi->dst ||
        mir.insns[16].src2 != count->dst ||
        mir.insns[16].immediate != '<' ||
        mir.insns[16].secondary_offset != TYPE_INT ||
        !mir_inline_call_sum_signed_word_type(mir.insns[16].type) ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        mir.insns[17].label != mir.insns[34].label ||
        mir.insns[18].object != total_store->object ||
        mir.insns[19].object != values->object ||
        mir.insns[20].object != index_store->object ||
        !mir_machine_single_call_argument(call, &argument) ||
        argument != index_phi->dst)
        return mir_machine_reject(
        "inline-call-sum-schedule", "loop");

    plan->index_function = find_global(call->name);
    if (call->src1 >= 0 ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0 ||
        plan->index_function == NULL ||
        plan->index_function->storage != SC_FUNC ||
        plan->index_function->is_funcptr ||
        plan->index_function->is_noreturn ||
        !plan->index_function->has_proto ||
        plan->index_function->proto_variadic ||
        plan->index_function->proto_nargs != 1 ||
        !mir_inline_call_sum_signed_word_type(
        plan->index_function->type) ||
        !mir_inline_call_sum_signed_word_type(
        plan->index_function->proto_types[0]) ||
        !mir_inline_call_sum_signed_word_type(call->type))
        return mir_machine_reject(
        "inline-call-sum-schedule", "call");
    assembly_name =
        asm_name_for(sym_asm_name(plan->index_function));
    if (call->base_name[0] != 0 &&
        strcmp(call->base_name, assembly_name))
        return mir_machine_reject(
        "inline-call-sum-schedule", "call-name");

    if (mir.insns[23].src1 != values->dst ||
        mir.insns[23].src2 != call->dst ||
        !mir_inline_call_sum_word_pointer_type(
        mir.insns[23].type) ||
        mir.insns[23].memory_size != 2 ||
        mir.insns[23].immediate != 2 ||
        (mir.insns[23].memory_flags & (1 | 8)) != 0 ||
        load->src1 != mir.insns[23].dst ||
        load->memory_size != 2 ||
        load->bit_width != 0 ||
        (load->memory_flags & (1 | 8)) != 0 ||
        !mir_inline_call_sum_signed_word_type(load->type) ||
        mir.insns[25].src1 != total_phi->dst ||
        mir.insns[25].src2 != load->dst ||
        mir.insns[25].immediate != '+' ||
        mir.insns[25].secondary_offset != TYPE_INT ||
        !mir_inline_call_sum_signed_word_type(
        mir.insns[25].type) ||
        !mir_machine_same_location(
        &mir.insns[27], total_store) ||
        mir.insns[27].src1 != mir.insns[25].dst)
        return mir_machine_reject(
        "inline-call-sum-schedule", "accumulate");

    if (!mir_machine_constant_equals(mir.insns[30].dst, 1) ||
        mir.insns[31].src1 != index_phi->dst ||
        mir.insns[31].src2 != mir.insns[30].dst ||
        mir.insns[31].immediate != '+' ||
        mir.insns[31].secondary_offset != TYPE_INT ||
        !mir_inline_call_sum_signed_word_type(
        mir.insns[31].type) ||
        !mir_machine_same_location(
        &mir.insns[32], index_store) ||
        mir.insns[32].src1 != mir.insns[31].dst ||
        mir.insns[33].label != mir.insns[9].label ||
        mir.insns[35].object != total_store->object ||
        mir.insns[36].src1 != total_phi->dst)
        return mir_machine_reject(
        "inline-call-sum-schedule", "tail");
    return 1;
}

static void mir_emit_inline_call_sum_schedule(
    MirStream *out, const struct MirInlineCallSumSchedule *plan)
{
    int loop = new_label();
    int done = new_label();
    int increment_high = new_label();

    mir_stream_puts(";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-4\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
        "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
        "\tpush hl\n\tpop iy\n"
        "\tld hl,0\n"
        "\tld (ix-2),l\n\tld (ix-1),h\n"
        "\tld (ix-4),l\n\tld (ix-3),h\n"
        "L%d:\n"
        "\tld l,(ix-4)\n\tld h,(ix-3)\n"
        "\tld e,(ix%+d)\n\tld d,(ix%+d)\n"
        "\tld a,h\n\txor 128\n\tld h,a\n"
        "\tld a,d\n\txor 128\n\tld d,a\n"
        "\tor a\n\tsbc hl,de\n\tjp nc,L%d\n"
        "\tld l,(ix-4)\n\tld h,(ix-3)\n\tpush hl\n",
        plan->values_stack_offset + 4,
        plan->values_stack_offset + 5,
        loop,
        plan->count_stack_offset + 4,
        plan->count_stack_offset + 5,
        done);
    mir_machine_emit_symbol_call(out, plan->index_function);
    mir_stream_puts("\tpop bc\n\tadd hl,hl\n\tpush iy\n\tpop de\n"
          "\tadd hl,de\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld l,(ix-2)\n\tld h,(ix-1)\n\tadd hl,de\n"
          "\tld (ix-2),l\n\tld (ix-1),h\n"
          "\tinc (ix-4)\n", out);
    mir_stream_printf(out,
        "\tjp nz,L%d\n\tinc (ix-3)\n"
        "L%d:\n\tjp L%d\n"
        "L%d:\n\tld l,(ix-2)\n\tld h,(ix-1)\n"
        "\tld sp,ix\n\tpop ix\n\tpop iy\n"
        ";@dcc.reg free=iy\n\tret\n",
        increment_high, increment_high, loop, done);
}

static int mir_call_safe_signed_word_type(int type)
{
    return type_ptr_depth(type) == 0 &&
        !type_is_float(type) &&
        (type & 15) == TYPE_INT &&
        (type & TYPE_UNSIGNED) == 0 &&
        type_size(type) == 2;
}

static int mir_call_safe_word_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
        !type_is_float(type) &&
        type_size(type) == 2;
}

static int mir_call_safe_bool_type(int type)
{
    return type_ptr_depth(type) == 0 &&
        (type & 15) == TYPE_BOOL &&
        type_size(type) == 1;
}

static int mir_call_safe_direct_function(
    const struct MirInsn *arg, const struct MirInsn *call,
    int argument_value, int parameter_type, int result_type,
    struct Sym **function)
{
    struct Sym *candidate;
    int collected_argument;
    const char *assembly_name;

    if (call->opcode != MIR_CALL ||
        call->src1 >= 0 ||
        call->secondary_offset < 0 ||
        call->memory_flags != 0 ||
        arg->opcode != MIR_ARG ||
        arg->secondary_offset != call->secondary_offset ||
        arg->immediate != 0 ||
        arg->src1 != argument_value ||
        arg->type != parameter_type ||
        !mir_machine_single_call_argument(
            call, &collected_argument) ||
        collected_argument != argument_value)
        return 0;
    candidate = find_global(call->name);
    if (candidate == NULL ||
        candidate->storage != SC_FUNC ||
        candidate->is_funcptr ||
        candidate->is_noreturn ||
        !candidate->has_proto ||
        candidate->proto_variadic ||
        candidate->proto_nargs != 1 ||
        candidate->proto_types[0] != parameter_type ||
        candidate->type != result_type ||
        call->type != candidate->type)
        return 0;
    assembly_name = asm_name_for(sym_asm_name(candidate));
    if (call->base_name[0] != 0 &&
        strcmp(call->base_name, assembly_name))
        return 0;
    if (*function == NULL)
        *function = candidate;
    return *function == candidate;
}

static int mir_match_call_safe_countdown_sum_schedule(
    struct MirCallSafeWordLoopSchedule *plan)
{
    static const int expected_opcodes[54] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_CONST, MIR_STORE, MIR_LABEL,
        MIR_PHI, MIR_NOP, MIR_PHI, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_BINARY, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_ARG, MIR_CALL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_ARG, MIR_CALL, MIR_BINARY, MIR_NOP, MIR_BINARY,
        MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_RETURN
    };
    const struct MirInsn *counter = &mir.insns[1];
    const struct MirInsn *values = &mir.insns[2];
    const struct MirInsn *total_store = &mir.insns[4];
    const struct MirInsn *counter_phi = &mir.insns[6];
    const struct MirInsn *total_phi = &mir.insns[8];
    struct Sym *call_function = NULL;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 54 || mir_cfg_block_count() != 4 ||
        mir.has_vla ||
        !mir_call_safe_signed_word_type(mir.return_type))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
                expected_opcodes[instruction])
            return mir_machine_reject(
                "call-safe-countdown-sum-schedule", "opcodes");
    if (!mir_machine_parameter_value_offset(
            counter->dst, &plan->first_stack_offset) ||
        !mir_machine_parameter_value_offset(
            values->dst, &plan->second_stack_offset) ||
        plan->second_stack_offset != plan->first_stack_offset + 2 ||
        !mir_call_safe_signed_word_type(counter->type) ||
        !mir_call_safe_word_pointer_type(values->type) ||
        !mir_machine_named_nonvolatile(counter) ||
        !mir_machine_named_nonvolatile(values) ||
        mir_machine_pointee_is_volatile(values) ||
        counter->object < 0 || values->object < 0 ||
        counter->object == values->object ||
        mir.insns[7].object != values->object)
        return mir_machine_reject(
            "call-safe-countdown-sum-schedule", "parameters");
    if (!mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_machine_unobservable_local_store(total_store) ||
        total_store->src1 != mir.insns[3].dst ||
        total_store->object < 0 ||
        counter_phi->object != counter->object ||
        total_phi->object != total_store->object ||
        !mir_machine_same_location(counter_phi, counter) ||
        !mir_machine_same_location(total_phi, total_store) ||
        counter_phi->src1 != counter->dst ||
        counter_phi->src2 != mir.insns[15].dst ||
        total_phi->src1 != mir.insns[3].dst ||
        total_phi->src2 != mir.insns[45].dst ||
        counter_phi->phi_pred1 != mir.insns[0].label ||
        counter_phi->phi_pred2 != mir.insns[49].label ||
        total_phi->phi_pred1 != mir.insns[0].label ||
        total_phi->phi_pred2 != mir.insns[49].label)
        return mir_machine_reject(
            "call-safe-countdown-sum-schedule", "loop-state");
    if (!mir_call_safe_signed_word_type(counter_phi->type) ||
        !mir_call_safe_signed_word_type(total_phi->type))
        return mir_machine_reject(
            "call-safe-countdown-sum-schedule", "loop-types");
    if (!mir_machine_constant_equals(mir.insns[10].dst, 0) ||
        mir.insns[11].src1 != counter_phi->dst ||
        mir.insns[11].src2 != mir.insns[10].dst ||
        mir.insns[11].immediate != '>' ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[12].label != mir.insns[51].label ||
        !mir_machine_constant_equals(mir.insns[14].dst, 1) ||
        mir.insns[15].src1 != counter_phi->dst ||
        mir.insns[15].src2 != mir.insns[14].dst ||
        mir.insns[15].immediate != '-' ||
        !mir_machine_same_location(&mir.insns[16], counter) ||
        mir.insns[16].src1 != mir.insns[15].dst)
        return mir_machine_reject(
            "call-safe-countdown-sum-schedule", "countdown");
    if (!mir_machine_constant_equals(mir.insns[20].dst, 3) ||
        mir.insns[21].src1 != mir.insns[15].dst ||
        mir.insns[21].src2 != mir.insns[20].dst ||
        mir.insns[21].immediate != '&' ||
        mir.insns[22].src1 != values->dst ||
        mir.insns[22].src2 != mir.insns[21].dst ||
        mir.insns[22].immediate != 2 ||
        mir.insns[22].memory_size != 2 ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[23].memory_size != 2 ||
        !mir_call_safe_signed_word_type(mir.insns[23].type) ||
        (mir.insns[23].memory_flags & (1 | 8)) != 0 ||
        !mir_call_safe_signed_word_type(mir.insns[25].type) ||
        !mir_call_safe_direct_function(
            &mir.insns[24], &mir.insns[25],
            mir.insns[23].dst, mir.insns[23].type,
            mir.insns[25].type, &call_function))
        return mir_machine_reject(
            "call-safe-countdown-sum-schedule", "indexed-call");
    if (mir.insns[27].immediate != '+' ||
        !((mir.insns[27].src1 == mir.insns[25].dst &&
           mir.insns[27].src2 == mir.insns[15].dst) ||
          (mir.insns[27].src2 == mir.insns[25].dst &&
           mir.insns[27].src1 == mir.insns[15].dst)) ||
        mir.insns[28].immediate != '+' ||
        !((mir.insns[28].src1 == total_phi->dst &&
           mir.insns[28].src2 == mir.insns[27].dst) ||
          (mir.insns[28].src2 == total_phi->dst &&
           mir.insns[28].src1 == mir.insns[27].dst)) ||
        !mir_machine_same_location(&mir.insns[30], total_store) ||
        mir.insns[30].src1 != mir.insns[28].dst)
        return mir_machine_reject(
            "call-safe-countdown-sum-schedule", "first-sum");
    if (!mir_machine_constant_equals(mir.insns[33].dst, 1) ||
        mir.insns[34].src1 != mir.insns[15].dst ||
        mir.insns[34].src2 != mir.insns[33].dst ||
        mir.insns[34].immediate != '+' ||
        !mir_call_safe_signed_word_type(mir.insns[36].type) ||
        !mir_call_safe_direct_function(
            &mir.insns[35], &mir.insns[36],
            mir.insns[34].dst, mir.insns[34].type,
            mir.insns[36].type, &call_function) ||
        !mir_machine_constant_equals(mir.insns[38].dst, 1) ||
        mir.insns[39].src1 != mir.insns[15].dst ||
        mir.insns[39].src2 != mir.insns[38].dst ||
        mir.insns[39].immediate != '-' ||
        !mir_call_safe_signed_word_type(mir.insns[41].type) ||
        !mir_call_safe_direct_function(
            &mir.insns[40], &mir.insns[41],
            mir.insns[39].dst, mir.insns[39].type,
            mir.insns[41].type, &call_function) ||
        mir.insns[42].immediate != '+' ||
        !((mir.insns[42].src1 == mir.insns[36].dst &&
           mir.insns[42].src2 == mir.insns[41].dst) ||
          (mir.insns[42].src2 == mir.insns[36].dst &&
           mir.insns[42].src1 == mir.insns[41].dst)) ||
        mir.insns[44].immediate != '+' ||
        !((mir.insns[44].src1 == mir.insns[42].dst &&
           mir.insns[44].src2 == mir.insns[15].dst) ||
          (mir.insns[44].src2 == mir.insns[42].dst &&
           mir.insns[44].src1 == mir.insns[15].dst)) ||
        mir.insns[45].immediate != '+' ||
        !((mir.insns[45].src1 == mir.insns[28].dst &&
           mir.insns[45].src2 == mir.insns[44].dst) ||
          (mir.insns[45].src2 == mir.insns[28].dst &&
           mir.insns[45].src1 == mir.insns[44].dst)) ||
        !mir_machine_same_location(&mir.insns[47], total_store) ||
        mir.insns[47].src1 != mir.insns[45].dst ||
        mir.insns[50].label != mir.insns[5].label ||
        mir.insns[53].src1 != total_phi->dst)
        return mir_machine_reject(
            "call-safe-countdown-sum-schedule", "second-sum");
    plan->kind = MIR_CALL_SAFE_COUNTDOWN_SUM;
    plan->call_function = call_function;
    return 1;
}

static int mir_call_safe_member_load(
    int member_instruction, int load_instruction,
    int base, int offset)
{
    const struct MirInsn *member = &mir.insns[member_instruction];
    const struct MirInsn *load = &mir.insns[load_instruction];

    return member->opcode == MIR_MEMBER_ADDRESS &&
        member->src1 == base &&
        member->immediate == offset &&
        member->memory_size == 2 &&
        (member->memory_flags & (1 | 8)) == 0 &&
        load->opcode == MIR_LOAD_INDIRECT &&
        load->src1 == member->dst &&
        load->memory_size == 2 &&
        (load->memory_flags & (1 | 8)) == 0 &&
        mir_call_safe_signed_word_type(load->type);
}

static int mir_match_call_safe_member_sum_schedule(
    struct MirCallSafeWordLoopSchedule *plan)
{
    static const int expected_opcodes[69] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_CONST, MIR_STORE, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_PHI,
        MIR_PHI, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL,
        MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL,
        MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL,
        MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_RETURN
    };
    static const int member_instructions[6] = {
        19, 28, 37, 46, 49, 53
    };
    static const int load_instructions[6] = {
        20, 29, 38, 47, 50, 54
    };
    const struct MirInsn *pointer = &mir.insns[1];
    const struct MirInsn *count = &mir.insns[2];
    const struct MirInsn *total_store = &mir.insns[4];
    const struct MirInsn *index_store = &mir.insns[7];
    const struct MirInsn *total_phi = &mir.insns[11];
    const struct MirInsn *index_phi = &mir.insns[12];
    struct Sym *call_function = NULL;
    int member;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 69 || mir_cfg_block_count() != 4 ||
        mir.has_vla ||
        !mir_call_safe_signed_word_type(mir.return_type))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
                expected_opcodes[instruction])
            return mir_machine_reject(
                "call-safe-member-sum-schedule", "opcodes");
    if (!mir_machine_parameter_value_offset(
            pointer->dst, &plan->first_stack_offset) ||
        !mir_machine_parameter_value_offset(
            count->dst, &plan->second_stack_offset) ||
        plan->second_stack_offset != plan->first_stack_offset + 2 ||
        !mir_call_safe_word_pointer_type(pointer->type) ||
        !mir_call_safe_signed_word_type(count->type) ||
        !mir_machine_named_nonvolatile(pointer) ||
        !mir_machine_named_nonvolatile(count) ||
        mir_machine_pointee_is_volatile(pointer) ||
        pointer->object < 0 || count->object < 0 ||
        pointer->object == count->object ||
        mir.insns[9].object != pointer->object ||
        mir.insns[10].object != count->object ||
        mir.insns[14].object != count->object)
        return mir_machine_reject(
            "call-safe-member-sum-schedule", "parameters");
    if (!mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[5].dst, 0) ||
        !mir_machine_unobservable_local_store(total_store) ||
        !mir_machine_unobservable_local_store(index_store) ||
        total_store->src1 != mir.insns[3].dst ||
        index_store->src1 != mir.insns[5].dst ||
        total_store->object < 0 || index_store->object < 0 ||
        total_store->object == index_store->object ||
        total_phi->object != total_store->object ||
        index_phi->object != index_store->object ||
        !mir_machine_same_location(total_phi, total_store) ||
        !mir_machine_same_location(index_phi, index_store) ||
        total_phi->src1 != mir.insns[3].dst ||
        total_phi->src2 != mir.insns[56].dst ||
        index_phi->src1 != mir.insns[5].dst ||
        index_phi->src2 != mir.insns[63].dst ||
        total_phi->phi_pred1 != mir.insns[0].label ||
        total_phi->phi_pred2 != mir.insns[60].label ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[60].label)
        return mir_machine_reject(
            "call-safe-member-sum-schedule", "loop-state");
    if (!mir_call_safe_signed_word_type(total_phi->type) ||
        !mir_call_safe_signed_word_type(index_phi->type))
        return mir_machine_reject(
            "call-safe-member-sum-schedule", "loop-types");
    if (mir.insns[15].src1 != index_phi->dst ||
        mir.insns[15].src2 != count->dst ||
        mir.insns[15].immediate != '<' ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[16].label != mir.insns[66].label)
        return mir_machine_reject(
            "call-safe-member-sum-schedule", "condition");
    plan->member_offsets[0] = (int)mir.insns[19].immediate;
    plan->member_offsets[1] = (int)mir.insns[28].immediate;
    plan->member_offsets[2] = (int)mir.insns[37].immediate;
    if (plan->member_offsets[0] < 0 ||
        plan->member_offsets[1] != plan->member_offsets[0] + 2 ||
        plan->member_offsets[2] != plan->member_offsets[1] + 2)
        return mir_machine_reject(
            "call-safe-member-sum-schedule", "layout");
    for (member = 0; member < 6; ++member)
        if (!mir_call_safe_member_load(
                member_instructions[member],
                load_instructions[member],
                pointer->dst,
                plan->member_offsets[member % 3]))
            return mir_machine_reject(
                "call-safe-member-sum-schedule", "member-load");
    for (member = 0; member < 3; ++member) {
        const struct MirInsn *call = &mir.insns[22 + member * 9];

        if (!mir_call_safe_signed_word_type(call->type) ||
            !mir_call_safe_direct_function(
                &mir.insns[21 + member * 9], call,
                mir.insns[20 + member * 9].dst,
                mir.insns[20 + member * 9].type,
                call->type, &call_function))
            return mir_machine_reject(
                "call-safe-member-sum-schedule", "calls");
    }
    if (mir.insns[23].immediate != '+' ||
        !((mir.insns[23].src1 == total_phi->dst &&
           mir.insns[23].src2 == mir.insns[22].dst) ||
          (mir.insns[23].src2 == total_phi->dst &&
           mir.insns[23].src1 == mir.insns[22].dst)) ||
        mir.insns[32].immediate != '+' ||
        !((mir.insns[32].src1 == mir.insns[23].dst &&
           mir.insns[32].src2 == mir.insns[31].dst) ||
          (mir.insns[32].src2 == mir.insns[23].dst &&
           mir.insns[32].src1 == mir.insns[31].dst)) ||
        mir.insns[41].immediate != '+' ||
        !((mir.insns[41].src1 == mir.insns[32].dst &&
           mir.insns[41].src2 == mir.insns[40].dst) ||
          (mir.insns[41].src2 == mir.insns[32].dst &&
           mir.insns[41].src1 == mir.insns[40].dst)) ||
        !mir_machine_same_location(&mir.insns[25], total_store) ||
        mir.insns[25].src1 != mir.insns[23].dst ||
        !mir_machine_same_location(&mir.insns[34], total_store) ||
        mir.insns[34].src1 != mir.insns[32].dst ||
        !mir_machine_same_location(&mir.insns[43], total_store) ||
        mir.insns[43].src1 != mir.insns[41].dst)
        return mir_machine_reject(
            "call-safe-member-sum-schedule", "call-sum");
    if (mir.insns[51].immediate != '+' ||
        !((mir.insns[51].src1 == mir.insns[47].dst &&
           mir.insns[51].src2 == mir.insns[50].dst) ||
          (mir.insns[51].src2 == mir.insns[47].dst &&
           mir.insns[51].src1 == mir.insns[50].dst)) ||
        mir.insns[55].immediate != '+' ||
        !((mir.insns[55].src1 == mir.insns[51].dst &&
           mir.insns[55].src2 == mir.insns[54].dst) ||
          (mir.insns[55].src2 == mir.insns[51].dst &&
           mir.insns[55].src1 == mir.insns[54].dst)) ||
        mir.insns[56].immediate != '+' ||
        !((mir.insns[56].src1 == mir.insns[41].dst &&
           mir.insns[56].src2 == mir.insns[55].dst) ||
          (mir.insns[56].src2 == mir.insns[41].dst &&
           mir.insns[56].src1 == mir.insns[55].dst)) ||
        !mir_machine_same_location(&mir.insns[58], total_store) ||
        mir.insns[58].src1 != mir.insns[56].dst ||
        !mir_machine_constant_equals(mir.insns[62].dst, 1) ||
        mir.insns[63].src1 != index_phi->dst ||
        mir.insns[63].src2 != mir.insns[62].dst ||
        mir.insns[63].immediate != '+' ||
        !mir_machine_same_location(&mir.insns[64], index_store) ||
        mir.insns[64].src1 != mir.insns[63].dst ||
        mir.insns[65].label != mir.insns[8].label ||
        mir.insns[68].src1 != total_phi->dst)
        return mir_machine_reject(
            "call-safe-member-sum-schedule", "tail");
    plan->kind = MIR_CALL_SAFE_MEMBER_SUM;
    plan->call_function = call_function;
    return 1;
}

static void mir_emit_call_safe_countdown_sum_schedule(
    MirStream *out, const struct MirCallSafeWordLoopSchedule *plan)
{
    int loop = new_label();
    int done = new_label();

    mir_stream_puts(";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tdec sp\n\tdec sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tpush hl\n\tpop iy\n"
            "\tld hl,0\n\tld (ix-2),l\n\tld (ix-1),h\n"
            "L%d:\n\tpush iy\n\tpop hl\n"
            "\tld a,h\n\tor l\n\tjp z,L%d\n"
            "\tbit 7,h\n\tjp nz,L%d\n"
            "\tdec iy\n"
            "\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n"
            "\tpush iy\n\tpop hl\n\tld h,0\n\tld a,l\n"
            "\tand 3\n\tld l,a\n\tadd hl,hl\n"
            "\tex de,hl\n\tpop hl\n\tadd hl,de\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n",
            plan->first_stack_offset + 4,
            plan->first_stack_offset + 5,
            loop, done, done,
            plan->second_stack_offset + 4,
            plan->second_stack_offset + 5);
    mir_machine_emit_symbol_call(out, plan->call_function);
    mir_stream_puts("\tpop bc\n\tpush hl\n\tpush iy\n\tpop hl\n"
          "\tex de,hl\n\tpop hl\n\tadd hl,de\n\tex de,hl\n"
          "\tpop hl\n\tadd hl,de\n\tpush hl\n"
          "\tpush iy\n\tpop hl\n\tinc hl\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->call_function);
    mir_stream_puts("\tpop bc\n\tpush hl\n\tpush iy\n\tpop hl\n"
          "\tdec hl\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->call_function);
    mir_stream_puts("\tpop bc\n\tex de,hl\n\tpop hl\n\tadd hl,de\n"
          "\tpush hl\n\tpush iy\n\tpop hl\n\tex de,hl\n"
          "\tpop hl\n\tadd hl,de\n\tex de,hl\n"
          "\tpop hl\n\tadd hl,de\n"
          "\tld (ix-2),l\n\tld (ix-1),h\n", out);
    mir_stream_printf(out,
            "\tjp L%d\n"
            "L%d:\n\tld l,(ix-2)\n\tld h,(ix-1)\n"
            "\tld sp,ix\n\tpop ix\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            loop, done);
}

static void mir_emit_call_safe_member_sum_schedule(
    MirStream *out, const struct MirCallSafeWordLoopSchedule *plan)
{
    int loop = new_label();
    int done = new_label();
    int member;

    mir_stream_puts(";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tdec sp\n\tdec sp\n\tdec sp\n\tdec sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tpush hl\n\tpop iy\n"
            "\tld hl,0\n\tld (ix-2),l\n\tld (ix-1),h\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld (ix-4),l\n\tld (ix-3),h\n"
            "\tld a,h\n\tor a\n\tjp m,L%d\n"
            "\tor l\n\tjp z,L%d\n"
            "L%d:\n\tld l,(ix-2)\n\tld h,(ix-1)\n",
            plan->first_stack_offset + 4,
            plan->first_stack_offset + 5,
            plan->second_stack_offset + 4,
            plan->second_stack_offset + 5,
            done, done, loop);
    for (member = 0; member < 3; ++member) {
        mir_stream_puts("\tpush hl\n", out);
        mir_stream_printf(out,
                "\tld l,(iy%+d)\n\tld h,(iy%+d)\n\tpush hl\n",
                plan->member_offsets[member],
                plan->member_offsets[member] + 1);
        mir_machine_emit_symbol_call(out, plan->call_function);
        mir_stream_puts("\tpop bc\n\tex de,hl\n\tpop hl\n\tadd hl,de\n",
              out);
    }
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out,
            "\tld l,(iy%+d)\n\tld h,(iy%+d)\n\tpush hl\n"
            "\tld l,(iy%+d)\n\tld h,(iy%+d)\n"
            "\tex de,hl\n\tpop hl\n\tadd hl,de\n\tpush hl\n"
            "\tld l,(iy%+d)\n\tld h,(iy%+d)\n"
            "\tex de,hl\n\tpop hl\n\tadd hl,de\n"
            "\tex de,hl\n\tpop hl\n\tadd hl,de\n"
            "\tld (ix-2),l\n\tld (ix-1),h\n"
            "\tld l,(ix-4)\n\tld h,(ix-3)\n\tdec hl\n"
            "\tld (ix-4),l\n\tld (ix-3),h\n"
            "\tld a,h\n\tor l\n\tjp nz,L%d\n"
            "L%d:\n\tld l,(ix-2)\n\tld h,(ix-1)\n"
            "\tld sp,ix\n\tpop ix\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            plan->member_offsets[0],
            plan->member_offsets[0] + 1,
            plan->member_offsets[1],
            plan->member_offsets[1] + 1,
            plan->member_offsets[2],
            plan->member_offsets[2] + 1,
            loop, done);
}

static void mir_emit_call_safe_word_loop_schedule(
    MirStream *out, const struct MirCallSafeWordLoopSchedule *plan)
{
    if (plan->kind == MIR_CALL_SAFE_COUNTDOWN_SUM)
        mir_emit_call_safe_countdown_sum_schedule(out, plan);
    else
        mir_emit_call_safe_member_sum_schedule(out, plan);
}

static int mir_match_constant_do_while_schedule(
    struct MirConstantDoWhileSchedule *plan)
{
    static const int expected_opcodes[171] = {
        MIR_LABEL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_CONST, MIR_NOP,
        MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_UNARY, MIR_ARG, MIR_CALL, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_NOP, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_NOP, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_UNARY, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_UNARY, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_CONST, MIR_NOP,
        MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_JUMP, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_JUMP, MIR_LABEL,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_UNARY, MIR_ARG, MIR_CALL,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_UNARY, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_JUMP,
        MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_UNARY, MIR_ARG, MIR_CALL, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_UNARY, MIR_ARG, MIR_CALL, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_UNARY, MIR_ARG, MIR_CALL
    };
    static const int call_instructions[8] = {
        27, 58, 64, 103, 109, 158, 164, 170
    };
    static const int argument_values[8] = {
        25, 56, 62, 101, 107, 156, 162, 168
    };
    static const int comparison_values[8] = {
        24, 55, 61, 100, 106, 155, 161, 167
    };
    const struct MirInsn *execution_store = &mir.insns[3];
    const struct MirInsn *condition_store = &mir.insns[6];
    const struct MirInsn *hit_store = &mir.insns[118];
    struct Sym *check_function = NULL;
    long second_count;
    long third_initial;
    long break_count;
    long fourth_count;
    int call;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 171 || mir_cfg_block_count() != 15 ||
        mir.has_vla || type_size(mir.return_type) != 0)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
                expected_opcodes[instruction])
            return mir_machine_reject(
                "constant-do-while-schedule", "opcodes");
    if (!mir_machine_constant_equals(mir.insns[1].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[4].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[11].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[17].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[23].dst, 1) ||
        !mir_machine_unobservable_local_store(execution_store) ||
        !mir_machine_unobservable_local_store(condition_store) ||
        !mir_call_safe_signed_word_type(execution_store->type) ||
        !mir_call_safe_signed_word_type(condition_store->type) ||
        execution_store->src1 != mir.insns[1].dst ||
        condition_store->src1 != mir.insns[4].dst ||
        execution_store->object < 0 || condition_store->object < 0 ||
        execution_store->object == condition_store->object ||
        !mir_machine_same_location(&mir.insns[10], execution_store) ||
        !mir_machine_same_location(&mir.insns[13], execution_store) ||
        mir.insns[12].src1 != mir.insns[10].dst ||
        mir.insns[12].src2 != mir.insns[11].dst ||
        mir.insns[12].immediate != '+' ||
        mir.insns[18].src1 != mir.insns[4].dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        mir.insns[18].immediate != TOK_NE ||
        mir.insns[19].label != mir.insns[21].label ||
        mir.insns[20].label != mir.insns[7].label ||
        mir.insns[24].src1 != mir.insns[12].dst ||
        mir.insns[24].src2 != mir.insns[23].dst ||
        mir.insns[24].immediate != TOK_EQ)
        return mir_machine_reject(
            "constant-do-while-schedule", "first-loop");
    if (!mir_machine_evaluate_constant(
            mir.insns[31].dst, &second_count, 0) ||
        second_count <= 0 || second_count > 255 ||
        !mir_machine_constant_equals(mir.insns[28].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[38].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[42].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[48].dst, 0) ||
        !mir_machine_constant_equals(
            mir.insns[54].dst, second_count) ||
        !mir_machine_constant_equals(mir.insns[60].dst, 0) ||
        !mir_machine_same_location(&mir.insns[30], execution_store) ||
        !mir_machine_same_location(&mir.insns[33], condition_store) ||
        !mir_machine_same_location(&mir.insns[37], execution_store) ||
        !mir_machine_same_location(&mir.insns[40], execution_store) ||
        !mir_machine_same_location(&mir.insns[41], condition_store) ||
        !mir_machine_same_location(&mir.insns[44], condition_store) ||
        mir.insns[39].src1 != mir.insns[37].dst ||
        mir.insns[39].src2 != mir.insns[38].dst ||
        mir.insns[39].immediate != '+' ||
        mir.insns[40].src1 != mir.insns[39].dst ||
        mir.insns[43].src1 != mir.insns[41].dst ||
        mir.insns[43].src2 != mir.insns[42].dst ||
        mir.insns[43].immediate != '-' ||
        mir.insns[44].src1 != mir.insns[43].dst ||
        mir.insns[49].src1 != mir.insns[43].dst ||
        mir.insns[49].src2 != mir.insns[48].dst ||
        mir.insns[49].immediate != '>' ||
        mir.insns[50].label != mir.insns[52].label ||
        mir.insns[51].label != mir.insns[34].label ||
        mir.insns[55].src1 != mir.insns[39].dst ||
        mir.insns[55].src2 != mir.insns[54].dst ||
        mir.insns[55].immediate != TOK_EQ ||
        mir.insns[61].src1 != mir.insns[43].dst ||
        mir.insns[61].src2 != mir.insns[60].dst ||
        mir.insns[61].immediate != TOK_EQ)
        return mir_machine_reject(
            "constant-do-while-schedule", "second-loop");
    if (!mir_machine_evaluate_constant(
            mir.insns[68].dst, &third_initial, 0) ||
        !mir_machine_evaluate_constant(
            mir.insns[83].dst, &break_count, 0) ||
        third_initial <= 0 || third_initial > 255 ||
        break_count <= 0 || break_count > third_initial ||
        !mir_machine_constant_equals(mir.insns[65].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[75].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[79].dst, 1) ||
        !mir_machine_constant_equals(
            mir.insns[93].dst, 0) ||
        !mir_machine_constant_equals(
            mir.insns[99].dst, break_count) ||
        !mir_machine_constant_equals(
            mir.insns[105].dst, third_initial - break_count) ||
        !mir_machine_same_location(&mir.insns[67], execution_store) ||
        !mir_machine_same_location(&mir.insns[70], condition_store) ||
        !mir_machine_same_location(&mir.insns[74], execution_store) ||
        !mir_machine_same_location(&mir.insns[77], execution_store) ||
        !mir_machine_same_location(&mir.insns[78], condition_store) ||
        !mir_machine_same_location(&mir.insns[81], condition_store) ||
        mir.insns[76].src1 != mir.insns[74].dst ||
        mir.insns[76].src2 != mir.insns[75].dst ||
        mir.insns[76].immediate != '+' ||
        mir.insns[77].src1 != mir.insns[76].dst ||
        mir.insns[80].src1 != mir.insns[78].dst ||
        mir.insns[80].src2 != mir.insns[79].dst ||
        mir.insns[80].immediate != '-' ||
        mir.insns[81].src1 != mir.insns[80].dst ||
        mir.insns[84].src1 != mir.insns[76].dst ||
        mir.insns[84].src2 != mir.insns[83].dst ||
        mir.insns[84].immediate != TOK_EQ ||
        mir.insns[85].label != mir.insns[89].label ||
        mir.insns[87].label != mir.insns[97].label ||
        mir.insns[94].src1 != mir.insns[92].dst ||
        mir.insns[94].src2 != mir.insns[93].dst ||
        mir.insns[94].immediate != '>' ||
        mir.insns[95].label != mir.insns[97].label ||
        mir.insns[96].label != mir.insns[71].label ||
        !mir_machine_same_location(&mir.insns[98], execution_store) ||
        mir.insns[100].src1 != mir.insns[98].dst ||
        mir.insns[100].src2 != mir.insns[99].dst ||
        mir.insns[100].immediate != TOK_EQ ||
        !mir_machine_same_location(&mir.insns[104], condition_store) ||
        mir.insns[106].src1 != mir.insns[104].dst ||
        mir.insns[106].src2 != mir.insns[105].dst ||
        mir.insns[106].immediate != TOK_EQ)
        return mir_machine_reject(
            "constant-do-while-schedule", "third-loop");
    if (!mir_machine_evaluate_constant(
            mir.insns[113].dst, &fourth_count, 0) ||
        fourth_count <= 0 || fourth_count > 255 ||
        !mir_machine_constant_equals(mir.insns[110].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[116].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[124].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[128].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[132].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[134].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[142].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[148].dst, 0) ||
        !mir_machine_constant_equals(
            mir.insns[154].dst, fourth_count) ||
        !mir_machine_constant_equals(mir.insns[160].dst, 0) ||
        !mir_machine_constant_equals(
            mir.insns[166].dst, (fourth_count + 1) / 2) ||
        !mir_machine_same_location(&mir.insns[112], execution_store) ||
        !mir_machine_same_location(&mir.insns[115], condition_store) ||
        !mir_machine_unobservable_local_store(hit_store) ||
        !mir_call_safe_signed_word_type(hit_store->type) ||
        hit_store->src1 != mir.insns[116].dst ||
        hit_store->object < 0 ||
        hit_store->object == execution_store->object ||
        hit_store->object == condition_store->object ||
        !mir_machine_same_location(&mir.insns[123], execution_store) ||
        !mir_machine_same_location(&mir.insns[126], execution_store) ||
        !mir_machine_same_location(&mir.insns[127], condition_store) ||
        !mir_machine_same_location(&mir.insns[130], condition_store) ||
        !mir_machine_same_location(&mir.insns[141], hit_store) ||
        !mir_machine_same_location(&mir.insns[144], hit_store) ||
        mir.insns[125].src1 != mir.insns[123].dst ||
        mir.insns[125].src2 != mir.insns[124].dst ||
        mir.insns[125].immediate != '+' ||
        mir.insns[126].src1 != mir.insns[125].dst ||
        mir.insns[129].src1 != mir.insns[127].dst ||
        mir.insns[129].src2 != mir.insns[128].dst ||
        mir.insns[129].immediate != '-' ||
        mir.insns[130].src1 != mir.insns[129].dst ||
        mir.insns[133].src1 != mir.insns[125].dst ||
        mir.insns[133].src2 != mir.insns[132].dst ||
        mir.insns[133].immediate != '%' ||
        mir.insns[135].src1 != mir.insns[133].dst ||
        mir.insns[135].src2 != mir.insns[134].dst ||
        mir.insns[135].immediate != TOK_EQ ||
        mir.insns[136].label != mir.insns[140].label ||
        mir.insns[138].label != mir.insns[146].label ||
        mir.insns[143].src1 != mir.insns[141].dst ||
        mir.insns[143].src2 != mir.insns[142].dst ||
        mir.insns[143].immediate != '+' ||
        mir.insns[144].src1 != mir.insns[143].dst ||
        mir.insns[149].src1 != mir.insns[147].dst ||
        mir.insns[149].src2 != mir.insns[148].dst ||
        mir.insns[149].immediate != '>' ||
        mir.insns[150].label != mir.insns[152].label ||
        mir.insns[151].label != mir.insns[119].label ||
        !mir_machine_same_location(&mir.insns[153], execution_store) ||
        mir.insns[155].src1 != mir.insns[153].dst ||
        mir.insns[155].src2 != mir.insns[154].dst ||
        mir.insns[155].immediate != TOK_EQ ||
        !mir_machine_same_location(&mir.insns[159], condition_store) ||
        mir.insns[161].src1 != mir.insns[159].dst ||
        mir.insns[161].src2 != mir.insns[160].dst ||
        mir.insns[161].immediate != TOK_EQ ||
        !mir_machine_same_location(&mir.insns[165], hit_store) ||
        mir.insns[167].src1 != mir.insns[165].dst ||
        mir.insns[167].src2 != mir.insns[166].dst ||
        mir.insns[167].immediate != TOK_EQ)
        return mir_machine_reject(
            "constant-do-while-schedule", "fourth-loop");
    for (call = 0; call < 8; ++call)
        if (mir.insns[argument_values[call]].src1 !=
                mir.insns[comparison_values[call]].dst ||
            mir.insns[argument_values[call]].immediate != 0 ||
            !mir_call_safe_bool_type(
                mir.insns[argument_values[call]].type) ||
            !mir_call_safe_direct_function(
                &mir.insns[call_instructions[call] - 1],
                &mir.insns[call_instructions[call]],
                mir.insns[argument_values[call]].dst,
                mir.insns[argument_values[call]].type,
                TYPE_VOID, &check_function))
            return mir_machine_reject(
                "constant-do-while-schedule", "calls");
    plan->check_function = check_function;
    plan->second_count = (int)second_count;
    plan->third_initial = (int)third_initial;
    plan->break_count = (int)break_count;
    plan->fourth_count = (int)fourth_count;
    return 1;
}

static void mir_emit_call_safe_check(
    MirStream *out, struct Sym *function)
{
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, function);
    mir_stream_puts("\tpop bc\n", out);
}

static void mir_emit_bc_equal_constant(MirStream *out, int value)
{
    int done = new_label();

    mir_stream_printf(out,
            "\tld h,b\n\tld l,c\n\tld de,%d\n"
            "\tor a\n\tsbc hl,de\n\tld hl,0\n"
            "\tjp nz,L%d\n\tinc l\nL%d:\n",
            value, done, done);
}

static void mir_emit_de_equal_constant(MirStream *out, int value)
{
    int done = new_label();

    mir_stream_printf(out,
            "\tld h,d\n\tld l,e\n\tld bc,%d\n"
            "\tor a\n\tsbc hl,bc\n\tld hl,0\n"
            "\tjp nz,L%d\n\tinc l\nL%d:\n",
            value, done, done);
}

static void mir_emit_byte_equal_constant(
    MirStream *out, char reg, int value)
{
    int done = new_label();

    mir_stream_printf(out,
            "\tld hl,0\n\tld a,%c\n\tcp %d\n"
            "\tjp nz,L%d\n\tinc l\nL%d:\n",
            reg, value, done, done);
}

static void mir_emit_constant_do_while_schedule(
    MirStream *out, const struct MirConstantDoWhileSchedule *plan)
{
    int first = new_label();
    int second = new_label();
    int second_done = new_label();
    int third = new_label();
    int third_condition = new_label();
    int third_done = new_label();
    int fourth = new_label();
    int fourth_condition = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    mir_stream_printf(out,
            "\tld bc,0\n\tld de,0\n"
            "L%d:\n\tinc bc\n\tld a,d\n\tor e\n\tjp nz,L%d\n",
            first, first);
    mir_emit_bc_equal_constant(out, 1);
    mir_emit_call_safe_check(out, plan->check_function);

    mir_stream_printf(out,
            "\tld bc,0\n\tld de,%d\n"
            "L%d:\n\tinc bc\n\tdec de\n"
            "\tld a,d\n\tor e\n\tjp nz,L%d\n"
            "L%d:\n\tpush de\n",
            plan->second_count, second, second, second_done);
    mir_emit_bc_equal_constant(out, plan->second_count);
    mir_emit_call_safe_check(out, plan->check_function);
    mir_stream_puts("\tpop de\n", out);
    mir_emit_de_equal_constant(out, 0);
    mir_emit_call_safe_check(out, plan->check_function);

    mir_stream_printf(out,
            "\tld bc,0\n\tld de,%d\n"
            "L%d:\n\tinc bc\n\tdec de\n"
            "\tld a,b\n\tor a\n\tjp nz,L%d\n"
            "\tld a,c\n\tcp %d\n\tjp z,L%d\n"
            "L%d:\n\tld a,d\n\tor e\n\tjp nz,L%d\n"
            "L%d:\n\tpush de\n",
            plan->third_initial, third,
            third_condition, plan->break_count, third_done,
            third_condition, third, third_done);
    mir_emit_bc_equal_constant(out, plan->break_count);
    mir_emit_call_safe_check(out, plan->check_function);
    mir_stream_puts("\tpop de\n", out);
    mir_emit_de_equal_constant(
        out, plan->third_initial - plan->break_count);
    mir_emit_call_safe_check(out, plan->check_function);

    mir_stream_printf(out,
            "\tld b,0\n\tld c,%d\n\tld de,0\n"
            "L%d:\n\tinc b\n\tdec c\n\tbit 0,b\n"
            "\tjp z,L%d\n\tinc e\n"
            "L%d:\n\tld a,c\n\tor a\n\tjp nz,L%d\n"
            "\tpush bc\n\tpush de\n",
            plan->fourth_count, fourth,
            fourth_condition, fourth_condition, fourth);
    mir_emit_byte_equal_constant(
        out, 'b', plan->fourth_count);
    mir_emit_call_safe_check(out, plan->check_function);
    mir_stream_puts("\tpop de\n\tpop bc\n\tpush de\n", out);
    mir_emit_byte_equal_constant(out, 'c', 0);
    mir_emit_call_safe_check(out, plan->check_function);
    mir_stream_puts("\tpop de\n", out);
    mir_emit_byte_equal_constant(
        out, 'e', (plan->fourth_count + 1) / 2);
    mir_emit_call_safe_check(out, plan->check_function);
    mir_stream_puts("\tret\n", out);
}

static int mir_call_recovery_opcode_sequence(
    const unsigned char *expected, size_t count)
{
    size_t instruction;

    if ((size_t)mir.count != count)
        return 0;
    for (instruction = 0; instruction < count; ++instruction)
        if (mir.insns[instruction].opcode != expected[instruction])
            return 0;
    return 1;
}

static int mir_call_recovery_edges(
    const struct MirCallRecoveryEdge *edges, size_t count)
{
    size_t item;

    for (item = 0; item < count; ++item)
        if (mir.insns[edges[item].instruction].label !=
                mir.insns[edges[item].target].label)
            return 0;
    return 1;
}

static int mir_call_recovery_phis(
    const struct MirCallRecoveryPhi *phis, size_t count)
{
    size_t item;

    for (item = 0; item < count; ++item) {
        const struct MirCallRecoveryPhi *expected = &phis[item];
        const struct MirInsn *phi =
            &mir.insns[expected->instruction];

        if (phi->src1 != expected->first ||
            phi->src2 != expected->second ||
            phi->phi_pred1 !=
                mir.insns[expected->first_predecessor].label ||
            phi->phi_pred2 !=
                mir.insns[expected->second_predecessor].label)
            return 0;
    }
    return 1;
}

static int mir_call_char_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
           (type & 15) == TYPE_CHAR &&
           type_size(type) == 2;
}

static struct Sym *mir_call_recovery_function(
    int instruction, int variadic, int argument_count,
    int allow_noreturn, char call_name[64])
{
    const struct MirInsn *call = &mir.insns[instruction];
    struct Sym *function;

    if (call->opcode != MIR_CALL || call->src1 >= 0 ||
        ((call->memory_flags & MIR_CALL_FLAG_VARIADIC) != 0) !=
            variadic ||
        (call->memory_flags & MIR_CALL_FLAG_FORMAT_RUNTIME) != 0 ||
        (function = find_global(call->name)) == NULL ||
        function->storage != SC_FUNC || function->is_funcptr ||
        (!allow_noreturn && function->is_noreturn) ||
        !function->has_proto ||
        function->proto_variadic != variadic ||
        function->proto_nargs != argument_count)
        return NULL;
    snprintf(call_name, 64, "%s",
             call->base_name[0] != 0
                 ? call->base_name
                 : asm_name_for(sym_asm_name(function)));
    return function;
}

static void mir_call_recovery_emit_named_call(
    MirStream *out, struct Sym *function, const char *call_name)
{
    const char *assembly_name =
        asm_name_for(sym_asm_name(function));

    if (call_name != NULL && call_name[0] != 0 &&
        strcmp(call_name, assembly_name))
        mir_emit_runtime_call(out, call_name);
    else
        mir_machine_emit_symbol_call(out, function);
}

static int mir_match_zero_allocation_schedule(
    struct MirZeroAllocationSchedule *plan)
{
    static const unsigned char expected_opcodes[29] = {
        MIR_LABEL, MIR_CONST, MIR_NOP, MIR_ARG, MIR_CALL, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_UNARY, MIR_STORE, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_LABEL, MIR_LOAD, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT,
        MIR_LOAD, MIR_NOP, MIR_ARG, MIR_CALL
    };
    struct Sym *first_free;
    struct Sym *second_free;
    struct Sym *allocate_function;
    struct Sym *failure_function;
    int argument;
    long stored_byte;

    memset(plan, 0, sizeof(*plan));
    if (!mir_call_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        mir_cfg_block_count() != 2 || mir.local_bytes != 2 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return 0;
    first_free = mir_memory_runner_call_function(4, 0, 1);
    allocate_function =
        mir_memory_runner_call_function(7, 0, 1);
    failure_function =
        mir_memory_runner_call_function(17, 0, 1);
    second_free = mir_memory_runner_call_function(28, 0, 1);
    if (first_free == NULL || second_free != first_free ||
        allocate_function == NULL || failure_function == NULL ||
        first_free == allocate_function ||
        first_free == failure_function ||
        allocate_function == failure_function ||
        (first_free->type & 15) != TYPE_VOID ||
        type_ptr_depth(first_free->type) != 0 ||
        type_ptr_depth(allocate_function->type) != 1 ||
        type_size(allocate_function->type) != 2 ||
        (failure_function->type & 15) != TYPE_VOID ||
        type_ptr_depth(failure_function->type) != 0)
        return mir_machine_reject(
            "zero-allocation-schedule", "functions");
    if (!mir_machine_constant_equals(mir.insns[1].dst, 0) ||
        !mir_machine_single_call_argument(
            &mir.insns[4], &argument) ||
        argument != mir.insns[1].dst ||
        !mir_machine_constant_equals(mir.insns[5].dst, 0) ||
        !mir_machine_single_call_argument(
            &mir.insns[7], &argument) ||
        argument != mir.insns[5].dst ||
        mir.insns[9].src1 != mir.insns[7].dst ||
        mir.insns[9].immediate != 0 ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        !mir_machine_same_location(
            &mir.insns[10], &mir.insns[11]) ||
        !mir_machine_same_location(
            &mir.insns[10], &mir.insns[19]) ||
        !mir_machine_same_location(
            &mir.insns[10], &mir.insns[25]) ||
        !mir_machine_named_nonvolatile(&mir.insns[10]) ||
        !mir_machine_constant_equals(mir.insns[12].dst, 0) ||
        mir.insns[13].src1 != mir.insns[11].dst ||
        mir.insns[13].src2 != mir.insns[12].dst ||
        mir.insns[13].immediate != TOK_EQ ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        mir.insns[14].label != mir.insns[18].label ||
        !mir_machine_single_call_argument(
            &mir.insns[17], &argument) ||
        argument != mir.insns[15].dst)
        return mir_machine_reject(
            "zero-allocation-schedule", "control");
    if (!mir_machine_constant_equals(mir.insns[20].dst, 0) ||
        mir.insns[21].src1 != mir.insns[19].dst ||
        mir.insns[21].src2 != mir.insns[20].dst ||
        mir.insns[21].immediate != 1 ||
        mir.insns[21].memory_size != 1 ||
        !mir_machine_evaluate_constant(
            mir.insns[23].dst, &stored_byte, 0) ||
        stored_byte < -128 || stored_byte > 255 ||
        mir.insns[24].src1 != mir.insns[21].dst ||
        mir.insns[24].src2 != mir.insns[23].dst ||
        mir.insns[24].memory_size != 1 ||
        (mir.insns[24].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_single_call_argument(
            &mir.insns[28], &argument) ||
        argument != mir.insns[25].dst)
        return mir_machine_reject(
            "zero-allocation-schedule", "store-free");
    plan->allocate_function = allocate_function;
    plan->free_function = first_free;
    plan->failure_function = failure_function;
    plan->failure_string = (int)mir.insns[15].immediate;
    plan->stored_byte = (int)stored_byte & 0xff;
    return 1;
}

static void mir_emit_zero_allocation_schedule(
    MirStream *out, const struct MirZeroAllocationSchedule *plan)
{
    int allocated = new_label();

    mir_stream_printf(out,
            "%s\n"
            ";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
            "\tpush iy\n",
            MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\tld hl,0\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->free_function);
    mir_emit_final_call_cleanup(out, 1);
    mir_stream_puts("\tld hl,0\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->allocate_function);
    mir_emit_final_call_cleanup(out, 1);
    mir_stream_puts("\tpush hl\n\tpop iy\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n\tld hl,S%d\n\tpush hl\n",
            allocated, plan->failure_string);
    mir_machine_emit_symbol_call(out, plan->failure_function);
    mir_emit_final_call_cleanup(out, 1);
    mir_stream_printf(out,
            "L%d:\n\tpush iy\n\tpop hl\n"
            "\tld (hl),%d\n\tpush hl\n",
            allocated, plan->stored_byte);
    mir_machine_emit_symbol_call(out, plan->free_function);
    mir_emit_final_call_cleanup(out, 1);
    mir_stream_puts("\tpop iy\n;@dcc.reg free=iy\n\tret\n", out);
}

static int mir_match_allocator_reuse_schedule(
    struct MirAllocatorReuseSchedule *plan)
{
    static const unsigned char expected_opcodes[85] = {
        MIR_LABEL, MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP, MIR_UNARY,
        MIR_STORE, MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP, MIR_UNARY,
        MIR_STORE, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP, MIR_UNARY, MIR_STORE,
        MIR_LOAD, MIR_LOAD, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_LOAD,
        MIR_NOP, MIR_ARG, MIR_CALL, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_UNARY, MIR_STORE, MIR_LOAD, MIR_LOAD, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_ARG, MIR_CALL, MIR_LOAD,
        MIR_NOP, MIR_ARG, MIR_CALL
    };
    static const int allocate_calls[4] = {3, 9, 47, 65};
    static const int allocate_constants[4] = {1, 7, 45, 63};
    static const int free_calls[4] = {44, 62, 80, 84};
    static const int free_values[4] = {41, 59, 77, 81};
    struct Sym *allocate_function = NULL;
    struct Sym *free_function = NULL;
    struct Sym *failure_function = NULL;
    int argument;
    long size;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (!mir_call_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        mir_cfg_block_count() != 11 || mir.local_bytes != 8 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (item = 0; item < 4; ++item) {
        struct Sym *function = mir_memory_runner_call_function(
            allocate_calls[item], 0, 1);

        if (function == NULL ||
            !mir_machine_single_call_argument(
                &mir.insns[allocate_calls[item]], &argument) ||
            argument != mir.insns[allocate_constants[item]].dst ||
            !mir_machine_evaluate_constant(
                argument, &size, 0) ||
            size < 0 || size > 65535)
            return mir_machine_reject(
                "allocator-reuse-schedule", "allocation-call");
        if (allocate_function == NULL)
            allocate_function = function;
        else if (allocate_function != function)
            return mir_machine_reject(
                "allocator-reuse-schedule", "mixed-allocator");
        if (item == 0)
            plan->sizes[0] = (int)size;
        else if (item == 1)
            plan->sizes[1] = (int)size;
        else if (item == 2)
            plan->sizes[2] = (int)size;
        else if ((int)size != plan->sizes[0])
            return mir_machine_reject(
                "allocator-reuse-schedule", "final-size");
    }
    for (item = 0; item < 4; ++item) {
        struct Sym *function = mir_memory_runner_call_function(
            free_calls[item], 0, 1);

        if (function == NULL ||
            !mir_machine_single_call_argument(
                &mir.insns[free_calls[item]], &argument) ||
            argument != mir.insns[free_values[item]].dst)
            return mir_machine_reject(
                "allocator-reuse-schedule", "free-call");
        if (free_function == NULL)
            free_function = function;
        else if (free_function != function)
            return mir_machine_reject(
                "allocator-reuse-schedule", "mixed-free");
    }
    failure_function =
        mir_memory_runner_call_function(39, 0, 1);
    if (failure_function == NULL ||
        mir_memory_runner_call_function(
            57, 0, 1) != failure_function ||
        mir_memory_runner_call_function(
            75, 0, 1) != failure_function ||
        allocate_function == free_function ||
        allocate_function == failure_function ||
        free_function == failure_function)
        return mir_machine_reject(
            "allocator-reuse-schedule", "functions");
    if (mir.insns[5].src1 != mir.insns[3].dst ||
        mir.insns[5].immediate != 0 ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        mir.insns[11].src1 != mir.insns[9].dst ||
        mir.insns[11].immediate != 0 ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        !mir_machine_same_location(
            &mir.insns[6], &mir.insns[13]) ||
        !mir_machine_same_location(
            &mir.insns[6], &mir.insns[41]) ||
        !mir_machine_same_location(
            &mir.insns[6], &mir.insns[52]) ||
        !mir_machine_same_location(
            &mir.insns[6], &mir.insns[70]) ||
        !mir_machine_same_location(
            &mir.insns[12], &mir.insns[21]) ||
        !mir_machine_same_location(
            &mir.insns[12], &mir.insns[81]))
        return mir_machine_reject(
            "allocator-reuse-schedule", "pointer-locals");
    if (!mir_machine_constant_equals(mir.insns[14].dst, 0) ||
        mir.insns[15].src1 != mir.insns[13].dst ||
        mir.insns[15].src2 != mir.insns[14].dst ||
        mir.insns[15].immediate != TOK_EQ ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[16].label != mir.insns[20].label ||
        !mir_machine_constant_equals(mir.insns[18].dst, 1) ||
        mir.insns[19].label != mir.insns[34].label ||
        !mir_machine_constant_equals(mir.insns[22].dst, 0) ||
        mir.insns[23].src1 != mir.insns[21].dst ||
        mir.insns[23].src2 != mir.insns[22].dst ||
        mir.insns[23].immediate != TOK_EQ ||
        mir.insns[24].src1 != mir.insns[23].dst ||
        mir.insns[24].label != mir.insns[28].label ||
        !mir_machine_constant_equals(mir.insns[26].dst, 1) ||
        mir.insns[27].label != mir.insns[30].label ||
        !mir_machine_constant_equals(mir.insns[29].dst, 0) ||
        mir.insns[31].src1 != mir.insns[26].dst ||
        mir.insns[31].src2 != mir.insns[29].dst ||
        mir.insns[31].phi_pred1 != mir.insns[25].label ||
        mir.insns[31].phi_pred2 != mir.insns[28].label ||
        mir.insns[33].label != mir.insns[34].label ||
        mir.insns[35].src1 != mir.insns[18].dst ||
        mir.insns[35].src2 != mir.insns[31].dst ||
        mir.insns[35].phi_pred1 != mir.insns[17].label ||
        mir.insns[35].phi_pred2 != mir.insns[32].label ||
        mir.insns[36].src1 != mir.insns[35].dst ||
        mir.insns[36].label != mir.insns[40].label)
        return mir_machine_reject(
            "allocator-reuse-schedule", "setup-condition");
    if (mir.insns[49].src1 != mir.insns[47].dst ||
        mir.insns[49].immediate != 0 ||
        mir.insns[50].src1 != mir.insns[49].dst ||
        !mir_machine_same_location(
            &mir.insns[50], &mir.insns[51]) ||
        !mir_machine_same_location(
            &mir.insns[50], &mir.insns[59]) ||
        mir.insns[53].src1 != mir.insns[51].dst ||
        mir.insns[53].src2 != mir.insns[52].dst ||
        mir.insns[53].immediate != TOK_NE ||
        mir.insns[54].src1 != mir.insns[53].dst ||
        mir.insns[54].label != mir.insns[58].label ||
        mir.insns[67].src1 != mir.insns[65].dst ||
        mir.insns[67].immediate != 0 ||
        mir.insns[68].src1 != mir.insns[67].dst ||
        !mir_machine_same_location(
            &mir.insns[68], &mir.insns[69]) ||
        !mir_machine_same_location(
            &mir.insns[68], &mir.insns[77]) ||
        mir.insns[71].src1 != mir.insns[69].dst ||
        mir.insns[71].src2 != mir.insns[70].dst ||
        mir.insns[71].immediate != TOK_NE ||
        mir.insns[72].src1 != mir.insns[71].dst ||
        mir.insns[72].label != mir.insns[76].label)
        return mir_machine_reject(
            "allocator-reuse-schedule", "reuse-checks");
    if (!mir_machine_single_call_argument(
            &mir.insns[39], &argument) ||
        argument != mir.insns[37].dst ||
        !mir_machine_single_call_argument(
            &mir.insns[57], &argument) ||
        argument != mir.insns[55].dst ||
        !mir_machine_single_call_argument(
            &mir.insns[75], &argument) ||
        argument != mir.insns[73].dst)
        return mir_machine_reject(
            "allocator-reuse-schedule", "failure-arguments");
    plan->allocate_function = allocate_function;
    plan->free_function = free_function;
    plan->failure_function = failure_function;
    plan->failure_strings[0] = (int)mir.insns[37].immediate;
    plan->failure_strings[1] = (int)mir.insns[55].immediate;
    plan->failure_strings[2] = (int)mir.insns[73].immediate;
    return 1;
}

static void mir_allocator_call_one(
    MirStream *out, struct Sym *function, int value)
{
    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n", value);
    mir_machine_emit_symbol_call(out, function);
    mir_emit_final_call_cleanup(out, 1);
}

static void mir_allocator_fail(
    MirStream *out, struct Sym *failure_function, int string_id)
{
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_machine_emit_symbol_call(out, failure_function);
    mir_emit_final_call_cleanup(out, 1);
}

static void mir_emit_allocator_reuse_schedule(
    MirStream *out, const struct MirAllocatorReuseSchedule *plan)
{
    int setup_failed = new_label();
    int setup_ok = new_label();
    int first_reuse_ok = new_label();
    int second_reuse_ok = new_label();

    mir_stream_printf(out,
            "%s\n"
            ";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
            "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tdec sp\n\tdec sp\n\tdec sp\n\tdec sp\n",
            MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_allocator_call_one(
        out, plan->allocate_function, plan->sizes[0]);
    mir_stream_puts("\tpush hl\n\tpop iy\n", out);
    mir_allocator_call_one(
        out, plan->allocate_function, plan->sizes[1]);
    mir_temp_store_hl(out, -2);
    mir_stream_puts("\tpush iy\n\tpop hl\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", setup_failed);
    mir_temp_load_hl(out, -2);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out,
            "\tjp nz,L%d\n"
            "L%d:\n",
            setup_ok, setup_failed);
    mir_allocator_fail(
        out, plan->failure_function, plan->failure_strings[0]);
    mir_stream_printf(out, "L%d:\n", setup_ok);
    mir_stream_puts("\tpush iy\n\tpop hl\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->free_function);
    mir_emit_final_call_cleanup(out, 1);

    mir_allocator_call_one(
        out, plan->allocate_function, plan->sizes[2]);
    mir_temp_store_hl(out, -4);
    mir_stream_puts("\tpush iy\n\tpop de\n\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", first_reuse_ok);
    mir_allocator_fail(
        out, plan->failure_function, plan->failure_strings[1]);
    mir_stream_printf(out, "L%d:\n", first_reuse_ok);
    mir_temp_load_hl(out, -4);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->free_function);
    mir_emit_final_call_cleanup(out, 1);

    mir_allocator_call_one(
        out, plan->allocate_function, plan->sizes[0]);
    mir_temp_store_hl(out, -4);
    mir_stream_puts("\tpush iy\n\tpop de\n\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", second_reuse_ok);
    mir_allocator_fail(
        out, plan->failure_function, plan->failure_strings[2]);
    mir_stream_printf(out, "L%d:\n", second_reuse_ok);
    mir_temp_load_hl(out, -4);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->free_function);
    mir_emit_final_call_cleanup(out, 1);
    mir_temp_load_hl(out, -2);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->free_function);
    mir_emit_final_call_cleanup(out, 1);
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tpop iy\n"
          ";@dcc.reg free=iy\n\tret\n", out);
}

static int mir_match_allocation_guard_schedule(
    struct MirAllocationGuardSchedule *plan)
{
    static const unsigned char expected_opcodes[33] = {
        MIR_LABEL, MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP, MIR_UNARY,
        MIR_STORE, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_UNARY, MIR_STORE, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_ARG,
        MIR_CALL
    };
    struct Sym *allocate_function;
    struct Sym *free_function;
    struct Sym *failure_function;
    int argument;
    long value;

    memset(plan, 0, sizeof(*plan));
    if (!mir_call_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        mir_cfg_block_count() != 3 || mir.local_bytes != 4 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return 0;
    allocate_function =
        mir_memory_runner_call_function(3, 0, 1);
    free_function =
        mir_memory_runner_call_function(32, 0, 1);
    failure_function =
        mir_memory_runner_call_function(13, 0, 1);
    if (allocate_function == NULL || free_function == NULL ||
        failure_function == NULL ||
        mir_memory_runner_call_function(
            17, 0, 1) != allocate_function ||
        mir_memory_runner_call_function(
            27, 0, 1) != failure_function ||
        allocate_function == free_function ||
        allocate_function == failure_function ||
        free_function == failure_function)
        return mir_machine_reject(
            "allocation-guard-schedule", "functions");
    if (!mir_machine_single_call_argument(
            &mir.insns[3], &argument) ||
        !mir_machine_evaluate_constant(argument, &value, 0) ||
        value < 0 || value > 65535)
        return mir_machine_reject(
            "allocation-guard-schedule", "guard-size");
    plan->guard_size = (int)value;
    if (!mir_machine_single_call_argument(
            &mir.insns[17], &argument) ||
        !mir_machine_evaluate_constant(argument, &value, 0) ||
        value < 0 || value > 65535)
        return mir_machine_reject(
            "allocation-guard-schedule", "rejected-size");
    plan->rejected_size = (int)value;
    if (mir.insns[5].src1 != mir.insns[3].dst ||
        mir.insns[5].immediate != 0 ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        !mir_machine_same_location(
            &mir.insns[6], &mir.insns[7]) ||
        !mir_machine_same_location(
            &mir.insns[6], &mir.insns[29]) ||
        !mir_machine_constant_equals(mir.insns[8].dst, 0) ||
        mir.insns[9].src1 != mir.insns[7].dst ||
        mir.insns[9].src2 != mir.insns[8].dst ||
        mir.insns[9].immediate != TOK_EQ ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].label != mir.insns[14].label ||
        mir.insns[19].src1 != mir.insns[17].dst ||
        mir.insns[19].immediate != 0 ||
        mir.insns[20].src1 != mir.insns[19].dst ||
        !mir_machine_same_location(
            &mir.insns[20], &mir.insns[21]) ||
        !mir_machine_constant_equals(mir.insns[22].dst, 0) ||
        mir.insns[23].src1 != mir.insns[21].dst ||
        mir.insns[23].src2 != mir.insns[22].dst ||
        mir.insns[23].immediate != TOK_NE ||
        mir.insns[24].src1 != mir.insns[23].dst ||
        mir.insns[24].label != mir.insns[28].label)
        return mir_machine_reject(
            "allocation-guard-schedule", "conditions");
    if (!mir_machine_single_call_argument(
            &mir.insns[13], &argument) ||
        argument != mir.insns[11].dst ||
        !mir_machine_single_call_argument(
            &mir.insns[27], &argument) ||
        argument != mir.insns[25].dst ||
        !mir_machine_single_call_argument(
            &mir.insns[32], &argument) ||
        argument != mir.insns[29].dst)
        return mir_machine_reject(
            "allocation-guard-schedule", "arguments");
    plan->allocate_function = allocate_function;
    plan->free_function = free_function;
    plan->failure_function = failure_function;
    plan->failure_strings[0] = (int)mir.insns[11].immediate;
    plan->failure_strings[1] = (int)mir.insns[25].immediate;
    return 1;
}

static void mir_emit_allocation_guard_schedule(
    MirStream *out, const struct MirAllocationGuardSchedule *plan)
{
    int guard_ok = new_label();
    int rejected_ok = new_label();

    mir_stream_printf(out,
            "%s\n"
            ";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
            "\tpush iy\n",
            MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_allocator_call_one(
        out, plan->allocate_function, plan->guard_size);
    mir_stream_puts("\tpush hl\n\tpop iy\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", guard_ok);
    mir_allocator_fail(
        out, plan->failure_function,
        plan->failure_strings[0]);
    mir_stream_printf(out, "L%d:\n", guard_ok);
    mir_allocator_call_one(
        out, plan->allocate_function, plan->rejected_size);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", rejected_ok);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->failure_strings[1]);
    mir_machine_emit_symbol_call(out, plan->failure_function);
    mir_emit_final_call_cleanup(out, 1);
    mir_stream_printf(out, "L%d:\n\tpush iy\n\tpop hl\n\tpush hl\n",
            rejected_ok);
    mir_machine_emit_symbol_call(out, plan->free_function);
    mir_emit_final_call_cleanup(out, 1);
    mir_stream_puts("\tpop iy\n;@dcc.reg free=iy\n\tret\n", out);
}

static int mir_match_allocator_trim_schedule(
    struct MirAllocatorTrimSchedule *plan)
{
    static const unsigned char expected_opcodes[55] = {
        MIR_LABEL, MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP, MIR_UNARY,
        MIR_STORE, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_LOAD,
        MIR_NOP, MIR_ARG, MIR_CALL, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_UNARY, MIR_STORE, MIR_LOAD, MIR_LOAD, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_ARG, MIR_CALL, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_UNARY, MIR_STORE, MIR_LOAD,
        MIR_LOAD, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_ARG,
        MIR_CALL
    };
    static const int allocation_calls[3] = {3, 21, 39};
    static const int allocation_constants[3] = {1, 19, 37};
    static const int free_calls[3] = {18, 36, 54};
    static const int free_values[3] = {15, 33, 51};
    static const int failure_calls[3] = {13, 31, 49};
    static const int failure_values[3] = {11, 29, 47};
    struct Sym *allocate_function = NULL;
    struct Sym *free_function = NULL;
    struct Sym *failure_function = NULL;
    int argument;
    long value;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (!mir_call_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        mir_cfg_block_count() != 4 || mir.local_bytes != 6 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (item = 0; item < 3; ++item) {
        struct Sym *function = mir_memory_runner_call_function(
            allocation_calls[item], 0, 1);

        if (function == NULL ||
            !mir_machine_single_call_argument(
                &mir.insns[allocation_calls[item]], &argument) ||
            argument != mir.insns[allocation_constants[item]].dst ||
            !mir_machine_evaluate_constant(argument, &value, 0) ||
            value < 0 || value > 65535)
            return mir_machine_reject(
                "allocator-trim-schedule", "allocation");
        if (allocate_function == NULL)
            allocate_function = function;
        else if (allocate_function != function)
            return mir_machine_reject(
                "allocator-trim-schedule", "mixed-allocation");
        if (item == 0)
            plan->first_size = (int)value;
        else if (item == 1 && (int)value != plan->first_size)
            return mir_machine_reject(
                "allocator-trim-schedule", "reuse-size");
        else if (item == 2)
            plan->final_size = (int)value;
    }
    for (item = 0; item < 3; ++item) {
        struct Sym *function = mir_memory_runner_call_function(
            free_calls[item], 0, 1);

        if (function == NULL ||
            !mir_machine_single_call_argument(
                &mir.insns[free_calls[item]], &argument) ||
            argument != mir.insns[free_values[item]].dst)
            return mir_machine_reject(
                "allocator-trim-schedule", "free");
        if (free_function == NULL)
            free_function = function;
        else if (free_function != function)
            return mir_machine_reject(
                "allocator-trim-schedule", "mixed-free");
    }
    for (item = 0; item < 3; ++item) {
        struct Sym *function = mir_memory_runner_call_function(
            failure_calls[item], 0, 1);

        if (function == NULL ||
            !mir_machine_single_call_argument(
                &mir.insns[failure_calls[item]], &argument) ||
            argument != mir.insns[failure_values[item]].dst)
            return mir_machine_reject(
                "allocator-trim-schedule", "failure");
        if (failure_function == NULL)
            failure_function = function;
        else if (failure_function != function)
            return mir_machine_reject(
                "allocator-trim-schedule", "mixed-failure");
        plan->failure_strings[item] =
            (int)mir.insns[failure_values[item]].immediate;
    }
    if (allocate_function == free_function ||
        allocate_function == failure_function ||
        free_function == failure_function)
        return mir_machine_reject(
            "allocator-trim-schedule", "functions");
    if (mir.insns[5].src1 != mir.insns[3].dst ||
        mir.insns[5].immediate != 0 ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        !mir_machine_same_location(
            &mir.insns[6], &mir.insns[7]) ||
        !mir_machine_same_location(
            &mir.insns[6], &mir.insns[15]) ||
        !mir_machine_same_location(
            &mir.insns[6], &mir.insns[26]) ||
        !mir_machine_same_location(
            &mir.insns[6], &mir.insns[44]) ||
        !mir_machine_constant_equals(mir.insns[8].dst, 0) ||
        mir.insns[9].src1 != mir.insns[7].dst ||
        mir.insns[9].src2 != mir.insns[8].dst ||
        mir.insns[9].immediate != TOK_EQ ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].label != mir.insns[14].label)
        return mir_machine_reject(
            "allocator-trim-schedule", "initial");
    if (mir.insns[23].src1 != mir.insns[21].dst ||
        mir.insns[23].immediate != 0 ||
        mir.insns[24].src1 != mir.insns[23].dst ||
        !mir_machine_same_location(
            &mir.insns[24], &mir.insns[25]) ||
        !mir_machine_same_location(
            &mir.insns[24], &mir.insns[33]) ||
        mir.insns[27].src1 != mir.insns[25].dst ||
        mir.insns[27].src2 != mir.insns[26].dst ||
        mir.insns[27].immediate != TOK_NE ||
        mir.insns[28].src1 != mir.insns[27].dst ||
        mir.insns[28].label != mir.insns[32].label ||
        mir.insns[41].src1 != mir.insns[39].dst ||
        mir.insns[41].immediate != 0 ||
        mir.insns[42].src1 != mir.insns[41].dst ||
        !mir_machine_same_location(
            &mir.insns[42], &mir.insns[43]) ||
        !mir_machine_same_location(
            &mir.insns[42], &mir.insns[51]) ||
        mir.insns[45].src1 != mir.insns[43].dst ||
        mir.insns[45].src2 != mir.insns[44].dst ||
        mir.insns[45].immediate != TOK_NE ||
        mir.insns[46].src1 != mir.insns[45].dst ||
        mir.insns[46].label != mir.insns[50].label)
        return mir_machine_reject(
            "allocator-trim-schedule", "reuse");
    plan->allocate_function = allocate_function;
    plan->free_function = free_function;
    plan->failure_function = failure_function;
    return 1;
}

static void mir_emit_allocator_trim_schedule(
    MirStream *out, const struct MirAllocatorTrimSchedule *plan)
{
    int initial_ok = new_label();
    int first_reuse_ok = new_label();
    int final_reuse_ok = new_label();

    mir_stream_printf(out,
            "%s\n"
            ";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
            "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tdec sp\n\tdec sp\n",
            MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_allocator_call_one(
        out, plan->allocate_function, plan->first_size);
    mir_stream_puts("\tpush hl\n\tpop iy\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", initial_ok);
    mir_allocator_fail(
        out, plan->failure_function, plan->failure_strings[0]);
    mir_stream_printf(out, "L%d:\n\tpush iy\n\tpop hl\n\tpush hl\n",
            initial_ok);
    mir_machine_emit_symbol_call(out, plan->free_function);
    mir_emit_final_call_cleanup(out, 1);

    mir_allocator_call_one(
        out, plan->allocate_function, plan->first_size);
    mir_temp_store_hl(out, -2);
    mir_stream_puts("\tpush iy\n\tpop de\n\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", first_reuse_ok);
    mir_allocator_fail(
        out, plan->failure_function, plan->failure_strings[1]);
    mir_stream_printf(out, "L%d:\n", first_reuse_ok);
    mir_temp_load_hl(out, -2);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->free_function);
    mir_emit_final_call_cleanup(out, 1);

    mir_allocator_call_one(
        out, plan->allocate_function, plan->final_size);
    mir_temp_store_hl(out, -2);
    mir_stream_puts("\tpush iy\n\tpop de\n\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", final_reuse_ok);
    mir_allocator_fail(
        out, plan->failure_function, plan->failure_strings[2]);
    mir_stream_printf(out, "L%d:\n", final_reuse_ok);
    mir_temp_load_hl(out, -2);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->free_function);
    mir_emit_final_call_cleanup(out, 1);
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tpop iy\n"
          ";@dcc.reg free=iy\n\tret\n", out);
}

static int mir_match_allocator_coalesce_common(
    struct MirAllocatorCoalesceSchedule *plan,
    enum MirAllocatorCoalesceKind kind)
{
    static const unsigned char bridge_opcodes[104] = {
        MIR_LABEL, MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP, MIR_UNARY,
        MIR_STORE, MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP, MIR_UNARY,
        MIR_STORE, MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP, MIR_UNARY,
        MIR_STORE, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_PHI, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_ARG, MIR_CALL, MIR_LOAD,
        MIR_NOP, MIR_ARG, MIR_CALL, MIR_LOAD, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP, MIR_UNARY,
        MIR_STORE, MIR_LOAD, MIR_LOAD, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_LOAD,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_LOAD, MIR_NOP, MIR_ARG, MIR_CALL
    };
    static const unsigned char reverse_opcodes[104] = {
        MIR_LABEL, MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP, MIR_UNARY,
        MIR_STORE, MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP, MIR_UNARY,
        MIR_STORE, MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP, MIR_UNARY,
        MIR_STORE, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_PHI, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_ARG, MIR_CALL, MIR_LOAD,
        MIR_NOP, MIR_ARG, MIR_CALL, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_UNARY, MIR_STORE, MIR_LOAD, MIR_LOAD, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_LABEL, MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_LOAD, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_LOAD, MIR_NOP, MIR_ARG, MIR_CALL
    };
    const unsigned char *expected =
        kind == MIR_ALLOCATOR_BRIDGE
            ? bridge_opcodes : reverse_opcodes;
    static const int allocation_calls[3] = {3, 9, 15};
    static const int allocation_constants[3] = {1, 7, 13};
    struct Sym *allocate_function = NULL;
    struct Sym *failure_function;
    int argument;
    long value;
    int item;

    memset(plan, 0, sizeof(*plan));
    plan->kind = kind;
    if (!mir_call_recovery_opcode_sequence(expected, 104) ||
        mir_cfg_block_count() != 17 || mir.local_bytes != 8 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (item = 0; item < 3; ++item) {
        struct Sym *function = mir_memory_runner_call_function(
            allocation_calls[item], 0, 1);

        if (function == NULL ||
            !mir_machine_single_call_argument(
                &mir.insns[allocation_calls[item]], &argument) ||
            argument != mir.insns[allocation_constants[item]].dst ||
            !mir_machine_evaluate_constant(argument, &value, 0) ||
            value <= 0 || value > 65535)
            return mir_machine_reject(
                "allocator-coalesce-schedule", "allocation");
        if (allocate_function == NULL) {
            allocate_function = function;
            plan->allocation_size = (int)value;
        } else if (allocate_function != function ||
                   plan->allocation_size != (int)value) {
            return mir_machine_reject(
                "allocator-coalesce-schedule", "mixed-allocation");
        }
    }
    failure_function =
        mir_memory_runner_call_function(65, 0, 1);
    if (failure_function == NULL ||
        !mir_machine_single_call_argument(
            &mir.insns[65], &argument) ||
        argument != mir.insns[63].dst)
        return mir_machine_reject(
            "allocator-coalesce-schedule", "setup-failure");
    if (mir.insns[5].src1 != mir.insns[3].dst ||
        mir.insns[5].immediate != 0 ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        mir.insns[11].src1 != mir.insns[9].dst ||
        mir.insns[11].immediate != 0 ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[17].src1 != mir.insns[15].dst ||
        mir.insns[17].immediate != 0 ||
        mir.insns[18].src1 != mir.insns[17].dst ||
        !mir_machine_same_location(
            &mir.insns[6], &mir.insns[19]) ||
        !mir_machine_same_location(
            &mir.insns[12], &mir.insns[27]) ||
        !mir_machine_same_location(
            &mir.insns[18], &mir.insns[47]) ||
        !mir_machine_constant_equals(mir.insns[20].dst, 0) ||
        mir.insns[21].src1 != mir.insns[19].dst ||
        mir.insns[21].src2 != mir.insns[20].dst ||
        mir.insns[21].immediate != TOK_EQ ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        mir.insns[22].label != mir.insns[26].label ||
        !mir_machine_constant_equals(mir.insns[24].dst, 1) ||
        mir.insns[25].label != mir.insns[40].label ||
        !mir_machine_constant_equals(mir.insns[28].dst, 0) ||
        mir.insns[29].src1 != mir.insns[27].dst ||
        mir.insns[29].src2 != mir.insns[28].dst ||
        mir.insns[29].immediate != TOK_EQ ||
        mir.insns[30].src1 != mir.insns[29].dst ||
        mir.insns[30].label != mir.insns[34].label ||
        !mir_machine_constant_equals(mir.insns[32].dst, 1) ||
        mir.insns[33].label != mir.insns[36].label ||
        !mir_machine_constant_equals(mir.insns[35].dst, 0) ||
        mir.insns[37].src1 != mir.insns[32].dst ||
        mir.insns[37].src2 != mir.insns[35].dst ||
        mir.insns[37].phi_pred1 != mir.insns[31].label ||
        mir.insns[37].phi_pred2 != mir.insns[34].label ||
        mir.insns[39].label != mir.insns[40].label ||
        mir.insns[41].src1 != mir.insns[24].dst ||
        mir.insns[41].src2 != mir.insns[37].dst ||
        mir.insns[41].phi_pred1 != mir.insns[23].label ||
        mir.insns[41].phi_pred2 != mir.insns[38].label ||
        mir.insns[42].src1 != mir.insns[41].dst ||
        mir.insns[42].label != mir.insns[46].label)
        return mir_machine_reject(
            "allocator-coalesce-schedule", "first-condition");
    if (!mir_machine_constant_equals(mir.insns[48].dst, 0) ||
        mir.insns[49].src1 != mir.insns[47].dst ||
        mir.insns[49].src2 != mir.insns[48].dst ||
        mir.insns[49].immediate != TOK_EQ ||
        mir.insns[50].src1 != mir.insns[49].dst ||
        mir.insns[50].label != mir.insns[54].label ||
        !mir_machine_constant_equals(mir.insns[52].dst, 1) ||
        mir.insns[53].label != mir.insns[56].label ||
        !mir_machine_constant_equals(mir.insns[55].dst, 0) ||
        mir.insns[57].src1 != mir.insns[52].dst ||
        mir.insns[57].src2 != mir.insns[55].dst ||
        mir.insns[57].phi_pred1 != mir.insns[51].label ||
        mir.insns[57].phi_pred2 != mir.insns[54].label ||
        mir.insns[59].label != mir.insns[60].label ||
        mir.insns[61].src1 != mir.insns[44].dst ||
        mir.insns[61].src2 != mir.insns[57].dst ||
        mir.insns[61].phi_pred1 != mir.insns[43].label ||
        mir.insns[61].phi_pred2 != mir.insns[58].label ||
        mir.insns[62].src1 != mir.insns[61].dst ||
        mir.insns[62].label != mir.insns[66].label)
        return mir_machine_reject(
            "allocator-coalesce-schedule", "second-condition");
    plan->allocate_function = allocate_function;
    plan->failure_function = failure_function;
    plan->failure_strings[0] = (int)mir.insns[63].immediate;
    return 1;
}

static int mir_match_allocator_bridge_schedule(
    struct MirAllocatorCoalesceSchedule *plan)
{
    static const int free_calls[4] = {70, 74, 78, 103};
    static const int free_values[4] = {67, 71, 75, 100};
    int arguments[3];
    int argument;
    long value;
    long merged_value;
    long fill_value;
    int item;

    if (!mir_match_allocator_coalesce_common(
            plan, MIR_ALLOCATOR_BRIDGE))
        return 0;
    for (item = 0; item < 4; ++item) {
        struct Sym *function = mir_memory_runner_call_function(
            free_calls[item], 0, 1);

        if (function == NULL ||
            !mir_machine_single_call_argument(
                &mir.insns[free_calls[item]], &argument) ||
            argument != mir.insns[free_values[item]].dst)
            return mir_machine_reject(
                "allocator-coalesce-schedule", "bridge-free");
        if (plan->free_function == NULL)
            plan->free_function = function;
        else if (plan->free_function != function)
            return mir_machine_reject(
                "allocator-coalesce-schedule", "bridge-mixed-free");
    }
    if (!mir_machine_same_location(
            &mir.insns[6], &mir.insns[67]) ||
        !mir_machine_same_location(
            &mir.insns[18], &mir.insns[71]) ||
        !mir_machine_same_location(
            &mir.insns[12], &mir.insns[75]) ||
        !mir_machine_evaluate_constant(
            mir.insns[79].dst, &value, 0) ||
        value <= 0 || value > 65535 ||
        !mir_machine_single_call_argument(
            &mir.insns[81], &argument) ||
        argument != mir.insns[79].dst ||
        mir_memory_runner_call_function(
            81, 0, 1) != plan->allocate_function ||
        mir.insns[83].src1 != mir.insns[81].dst ||
        mir.insns[83].immediate != 0 ||
        mir.insns[84].src1 != mir.insns[83].dst ||
        !mir_machine_same_location(
            &mir.insns[84], &mir.insns[85]) ||
        !mir_machine_same_location(
            &mir.insns[84], &mir.insns[93]) ||
        !mir_machine_same_location(
            &mir.insns[84], &mir.insns[100]) ||
        mir.insns[87].src1 != mir.insns[85].dst ||
        mir.insns[87].src2 != mir.insns[86].dst ||
        mir.insns[87].immediate != TOK_NE ||
        !mir_machine_same_location(
            &mir.insns[6], &mir.insns[86]) ||
        mir.insns[88].src1 != mir.insns[87].dst ||
        mir.insns[88].label != mir.insns[92].label ||
        mir_memory_runner_call_function(
            91, 0, 1) != plan->failure_function ||
        !mir_machine_single_call_argument(
            &mir.insns[91], &argument) ||
        argument != mir.insns[89].dst)
        return mir_machine_reject(
            "allocator-coalesce-schedule", "bridge-result");
    plan->fill_function =
        mir_memory_runner_call_function(99, 0, 3);
    if (plan->fill_function == NULL ||
        !mir_machine_three_call_arguments(
            &mir.insns[99], arguments) ||
        arguments[0] != mir.insns[93].dst ||
        arguments[1] != mir.insns[95].dst ||
        arguments[2] != mir.insns[97].dst ||
        !mir_machine_evaluate_constant(
            arguments[1], &merged_value, 0) ||
        merged_value <= 0 || merged_value > 65535 ||
        !mir_machine_evaluate_constant(
            arguments[2], &fill_value, 0) ||
        fill_value < -32768 || fill_value > 65535)
        return mir_machine_reject(
            "allocator-coalesce-schedule", "bridge-fill");
    plan->merged_size = (int)merged_value;
    plan->fill_value = (int)fill_value;
    if (!mir_machine_evaluate_constant(
            mir.insns[79].dst, &value, 0) ||
        (int)value != plan->merged_size)
        return mir_machine_reject(
            "allocator-coalesce-schedule", "bridge-size");
    plan->failure_strings[1] = (int)mir.insns[89].immediate;
    return 1;
}

static int mir_match_allocator_reverse_schedule(
    struct MirAllocatorCoalesceSchedule *plan)
{
    static const int free_calls[4] = {70, 74, 99, 103};
    static const int free_values[4] = {67, 71, 96, 100};
    int arguments[3];
    int argument;
    long value;
    long merged_value;
    long fill_value;
    int item;

    if (!mir_match_allocator_coalesce_common(
            plan, MIR_ALLOCATOR_REVERSE))
        return 0;
    for (item = 0; item < 4; ++item) {
        struct Sym *function = mir_memory_runner_call_function(
            free_calls[item], 0, 1);

        if (function == NULL ||
            !mir_machine_single_call_argument(
                &mir.insns[free_calls[item]], &argument) ||
            argument != mir.insns[free_values[item]].dst)
            return mir_machine_reject(
                "allocator-coalesce-schedule", "reverse-free");
        if (plan->free_function == NULL)
            plan->free_function = function;
        else if (plan->free_function != function)
            return mir_machine_reject(
                "allocator-coalesce-schedule", "reverse-mixed-free");
    }
    if (!mir_machine_same_location(
            &mir.insns[6], &mir.insns[67]) ||
        !mir_machine_same_location(
            &mir.insns[12], &mir.insns[71]) ||
        !mir_machine_same_location(
            &mir.insns[18], &mir.insns[100]) ||
        !mir_machine_evaluate_constant(
            mir.insns[75].dst, &value, 0) ||
        value <= 0 || value > 65535 ||
        !mir_machine_single_call_argument(
            &mir.insns[77], &argument) ||
        argument != mir.insns[75].dst ||
        mir_memory_runner_call_function(
            77, 0, 1) != plan->allocate_function ||
        mir.insns[79].src1 != mir.insns[77].dst ||
        mir.insns[79].immediate != 0 ||
        mir.insns[80].src1 != mir.insns[79].dst ||
        !mir_machine_same_location(
            &mir.insns[80], &mir.insns[81]) ||
        !mir_machine_same_location(
            &mir.insns[80], &mir.insns[89]) ||
        !mir_machine_same_location(
            &mir.insns[80], &mir.insns[96]) ||
        mir.insns[83].src1 != mir.insns[81].dst ||
        mir.insns[83].src2 != mir.insns[82].dst ||
        mir.insns[83].immediate != TOK_NE ||
        !mir_machine_same_location(
            &mir.insns[6], &mir.insns[82]) ||
        mir.insns[84].src1 != mir.insns[83].dst ||
        mir.insns[84].label != mir.insns[88].label ||
        mir_memory_runner_call_function(
            87, 0, 1) != plan->failure_function ||
        !mir_machine_single_call_argument(
            &mir.insns[87], &argument) ||
        argument != mir.insns[85].dst)
        return mir_machine_reject(
            "allocator-coalesce-schedule", "reverse-result");
    plan->fill_function =
        mir_memory_runner_call_function(95, 0, 3);
    if (plan->fill_function == NULL ||
        !mir_machine_three_call_arguments(
            &mir.insns[95], arguments) ||
        arguments[0] != mir.insns[89].dst ||
        arguments[1] != mir.insns[91].dst ||
        arguments[2] != mir.insns[93].dst ||
        !mir_machine_evaluate_constant(
            arguments[1], &merged_value, 0) ||
        merged_value <= 0 || merged_value > 65535 ||
        !mir_machine_evaluate_constant(
            arguments[2], &fill_value, 0) ||
        fill_value < -32768 || fill_value > 65535)
        return mir_machine_reject(
            "allocator-coalesce-schedule", "reverse-fill");
    plan->merged_size = (int)merged_value;
    plan->fill_value = (int)fill_value;
    if (!mir_machine_evaluate_constant(
            mir.insns[75].dst, &value, 0) ||
        (int)value != plan->merged_size)
        return mir_machine_reject(
            "allocator-coalesce-schedule", "reverse-size");
    plan->failure_strings[1] = (int)mir.insns[85].immediate;
    return 1;
}

static void mir_allocator_free_ix(
    MirStream *out, struct Sym *free_function, int offset)
{
    mir_temp_load_hl(out, offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, free_function);
    mir_emit_final_call_cleanup(out, 1);
}

static void mir_emit_allocator_coalesce_schedule(
    MirStream *out, const struct MirAllocatorCoalesceSchedule *plan)
{
    int setup_failed = new_label();
    int setup_ok = new_label();
    int result_ok = new_label();

    mir_stream_printf(out,
            "%s\n"
            ";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
            "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tld hl,-6\n\tadd hl,sp\n\tld sp,hl\n",
            MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_allocator_call_one(
        out, plan->allocate_function, plan->allocation_size);
    mir_stream_puts("\tpush hl\n\tpop iy\n", out);
    mir_allocator_call_one(
        out, plan->allocate_function, plan->allocation_size);
    mir_temp_store_hl(out, -2);
    mir_allocator_call_one(
        out, plan->allocate_function, plan->allocation_size);
    mir_temp_store_hl(out, -4);
    mir_stream_puts("\tpush iy\n\tpop hl\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", setup_failed);
    mir_temp_load_hl(out, -2);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", setup_failed);
    mir_temp_load_hl(out, -4);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\nL%d:\n",
            setup_ok, setup_failed);
    mir_allocator_fail(
        out, plan->failure_function, plan->failure_strings[0]);
    mir_stream_printf(out, "L%d:\n", setup_ok);

    mir_stream_puts("\tpush iy\n\tpop hl\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->free_function);
    mir_emit_final_call_cleanup(out, 1);
    if (plan->kind == MIR_ALLOCATOR_BRIDGE) {
        mir_allocator_free_ix(out, plan->free_function, -4);
        mir_allocator_free_ix(out, plan->free_function, -2);
    } else {
        mir_allocator_free_ix(out, plan->free_function, -2);
    }
    mir_allocator_call_one(
        out, plan->allocate_function, plan->merged_size);
    mir_temp_store_hl(out, -6);
    mir_stream_puts("\tpush iy\n\tpop de\n\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", result_ok);
    mir_allocator_fail(
        out, plan->failure_function, plan->failure_strings[1]);
    mir_stream_printf(out, "L%d:\n", result_ok);

    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n",
            plan->fill_value);
    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n",
            plan->merged_size);
    mir_temp_load_hl(out, -6);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->fill_function);
    mir_emit_final_call_cleanup(out, 3);
    mir_allocator_free_ix(out, plan->free_function, -6);
    if (plan->kind == MIR_ALLOCATOR_REVERSE)
        mir_allocator_free_ix(out, plan->free_function, -4);
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tpop iy\n"
          ";@dcc.reg free=iy\n\tret\n", out);
}

static struct Sym *mir_bios_direct_function(
    int instruction, int argument_count, char call_name[64])
{
    const struct MirInsn *call = &mir.insns[instruction];
    struct Sym *function;
    const char *rtl_name;
    int first_argument;
    int second_argument;

    if (call->opcode != MIR_CALL || call->src1 >= 0 ||
        call->memory_flags != 0 ||
        (function = find_global(call->name)) == NULL ||
        function->storage != SC_FUNC || function->is_funcptr ||
        function->is_noreturn || !function->has_proto ||
        function->proto_variadic ||
        function->proto_nargs != argument_count ||
        !mir_memory_runner_word_type(function->type, 0) ||
        argument_count != 2 ||
        !mir_call_is_bdos_family_fastcall(
            instruction, &rtl_name,
            &first_argument, &second_argument) ||
        !mir_memory_runner_word_type(function->proto_types[0], 0) ||
        !mir_memory_runner_word_type(function->proto_types[1], 0))
        return NULL;
    snprintf(call_name, 64, "%s", rtl_name);
    return function;
}

static int mir_bios_call_constants(
    int instruction, long first, long second)
{
    int arguments[2];

    return mir_machine_two_call_arguments(
               &mir.insns[instruction], arguments) &&
           mir_machine_constant_equals(arguments[0], first) &&
           mir_machine_constant_equals(arguments[1], second);
}

static void mir_call_recovery_emit_function_address(
    MirStream *out, struct Sym *function)
{
    const char *name = asm_name_for(sym_asm_name(function));

    if ((function->storage == SC_EXTERN || function->needs_extrn) &&
        mir_extrn_should_emit(function))
        mir_stream_printf(out, "\textrn %s\n", name);
    mir_stream_printf(out, "\tld hl,%s\n", name);
}

static int mir_bios_print_call(
    struct MirBiosCallSchedule *plan, int instruction,
    int argument_count, const int *definitions)
{
    const struct MirInsn *call = &mir.insns[instruction];
    struct Sym *function;
    int arguments[2];
    int argument;

    if (argument_count < 1 || argument_count > 2 ||
        call->opcode != MIR_CALL || call->src1 >= 0 ||
        call->base_name[0] == 0 ||
        call->memory_flags != MIR_CALL_FLAG_VARIADIC ||
        (function = find_global(call->name)) == NULL ||
        function->storage != SC_FUNC || function->is_funcptr ||
        function->is_noreturn || !function->has_proto ||
        !function->proto_variadic || function->proto_nargs != 1 ||
        !mir_memory_runner_word_type(function->type, 0) ||
        !mir_machine_call_arguments(
            call, argument_count, arguments))
        return 0;
    for (argument = 0; argument < argument_count; ++argument)
        if (arguments[argument] !=
                mir.insns[definitions[argument]].dst)
            return 0;
    if (plan->print_function == NULL) {
        plan->print_function = function;
        snprintf(plan->print_name, sizeof(plan->print_name), "%s",
                 call->base_name);
    }
    return plan->print_function == function &&
           !strcmp(plan->print_name, call->base_name);
}

static int mir_match_bios_call_schedule(
    struct MirBiosCallSchedule *plan)
{
    static const unsigned char expected_opcodes[111] = {
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_CONST,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_ADDRESS, MIR_STORE, MIR_CONST,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_LOAD, MIR_CALL, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_LABEL, MIR_LABEL, MIR_PHI,
        MIR_ARG, MIR_CALL, MIR_CONST, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_CONST,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_CONST, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_CONST, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_ADDRESS, MIR_STORE, MIR_CONST, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_LOAD, MIR_CALL, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_LABEL, MIR_LABEL, MIR_PHI,
        MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_RETURN
    };
    static const int one_argument_prints[3][2] = {
        {3, 1}, {27, 25}, {108, 106}
    };
    static const int two_argument_prints[2][3] = {
        {43, 28, 41}, {105, 90, 103}
    };
    static const int direct_bios_calls[6] = {8, 48, 53, 58, 63, 68};
    static const int direct_bios_arguments[6][2] = {
        {2, 0}, {4, 72}, {4, 105}, {4, 33}, {4, 13}, {4, 10}
    };
    struct Sym *bios_function;
    struct Sym *bioshl_function;
    int definitions[2];
    int arguments[2];
    char call_name[64];
    int item;

    memset(plan, 0, sizeof(*plan));
    if (!mir_call_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        mir_cfg_block_count() != 9 || mir.local_bytes != 12 ||
        mir.has_vla ||
        !mir_memory_runner_word_type(mir.return_type, 0))
        return 0;
    bios_function =
        mir_bios_direct_function(8, 2, plan->bios_name);
    bioshl_function =
        mir_bios_direct_function(73, 2, plan->bioshl_name);
    if (bios_function == NULL || bioshl_function == NULL ||
        bios_function == bioshl_function ||
        !mir_bios_call_constants(73, 2, 0))
        return mir_machine_reject(
            "bios-call-schedule", "direct-functions");
    for (item = 0; item < 6; ++item) {
        struct Sym *function = mir_bios_direct_function(
            direct_bios_calls[item], 2, call_name);

        if (function != bios_function ||
            strcmp(call_name, plan->bios_name) ||
            !mir_bios_call_constants(
                direct_bios_calls[item],
                direct_bios_arguments[item][0],
                direct_bios_arguments[item][1]))
            return mir_machine_reject(
                "bios-call-schedule", "direct-calls");
    }
    if (find_global(mir.insns[14].name) != bios_function ||
        find_global(mir.insns[79].name) != bioshl_function ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        mir.insns[80].src1 != mir.insns[79].dst ||
        !mir_machine_same_location(
            &mir.insns[15], &mir.insns[20]) ||
        !mir_machine_same_location(
            &mir.insns[80], &mir.insns[85]) ||
        mir.insns[21].src1 != mir.insns[20].dst ||
        mir.insns[86].src1 != mir.insns[85].dst ||
        !mir_machine_two_call_arguments(
            &mir.insns[21], arguments) ||
        !mir_machine_constant_equals(arguments[0], 2) ||
        !mir_machine_constant_equals(arguments[1], 0) ||
        !mir_machine_two_call_arguments(
            &mir.insns[86], arguments) ||
        !mir_machine_constant_equals(arguments[0], 2) ||
        !mir_machine_constant_equals(arguments[1], 0))
        return mir_machine_reject(
            "bios-call-schedule", "indirect-calls");
    if (mir.insns[32].src1 != mir.insns[8].dst ||
        mir.insns[32].src2 != mir.insns[21].dst ||
        mir.insns[32].immediate != TOK_EQ ||
        mir.insns[33].src1 != mir.insns[32].dst ||
        mir.insns[33].label != mir.insns[37].label ||
        mir.insns[36].label != mir.insns[40].label ||
        mir.insns[41].src1 != mir.insns[34].dst ||
        mir.insns[41].src2 != mir.insns[38].dst ||
        mir.insns[41].phi_pred1 != mir.insns[35].label ||
        mir.insns[41].phi_pred2 != mir.insns[39].label ||
        mir.insns[94].src1 != mir.insns[73].dst ||
        mir.insns[94].src2 != mir.insns[86].dst ||
        mir.insns[94].immediate != TOK_EQ ||
        mir.insns[95].src1 != mir.insns[94].dst ||
        mir.insns[95].label != mir.insns[99].label ||
        mir.insns[98].label != mir.insns[102].label ||
        mir.insns[103].src1 != mir.insns[96].dst ||
        mir.insns[103].src2 != mir.insns[100].dst ||
        mir.insns[103].phi_pred1 != mir.insns[97].label ||
        mir.insns[103].phi_pred2 != mir.insns[101].label)
        return mir_machine_reject(
            "bios-call-schedule", "comparisons");
    for (item = 0; item < 3; ++item) {
        definitions[0] = one_argument_prints[item][1];
        if (!mir_bios_print_call(
                plan, one_argument_prints[item][0], 1, definitions))
            return mir_machine_reject(
                "bios-call-schedule", "one-argument-print");
    }
    for (item = 0; item < 2; ++item) {
        definitions[0] = two_argument_prints[item][1];
        definitions[1] = two_argument_prints[item][2];
        if (!mir_bios_print_call(
                plan, two_argument_prints[item][0], 2, definitions))
            return mir_machine_reject(
                "bios-call-schedule", "two-argument-print");
    }
    if (!mir_machine_constant_equals(mir.insns[109].dst, 0) ||
        mir.insns[110].src1 != mir.insns[109].dst)
        return mir_machine_reject(
            "bios-call-schedule", "return");
    plan->strings[0] = (int)mir.insns[1].immediate;
    plan->strings[1] = (int)mir.insns[25].immediate;
    plan->strings[2] = (int)mir.insns[28].immediate;
    plan->strings[3] = (int)mir.insns[34].immediate;
    plan->strings[4] = (int)mir.insns[38].immediate;
    plan->strings[5] = (int)mir.insns[90].immediate;
    plan->strings[6] = (int)mir.insns[106].immediate;
    if (plan->strings[3] != (int)mir.insns[96].immediate ||
        plan->strings[4] != (int)mir.insns[100].immediate)
        return mir_machine_reject(
            "bios-call-schedule", "conditional-strings");
    plan->bios_function = bios_function;
    plan->bioshl_function = bioshl_function;
    plan->status_function = 2;
    plan->output_function = 4;
    for (item = 0; item < 5; ++item)
        plan->output_characters[item] =
            direct_bios_arguments[item + 1][1];
    return 1;
}

static void mir_emit_bios_direct_call(
    MirStream *out, const char *call_name,
    int function_number, int argument)
{
    mir_stream_printf(out, "\tld c,%d\n\tld de,%d\n",
            function_number & 0xff, argument);
    mir_emit_runtime_call(out, call_name);
}

static void mir_emit_bios_indirect_call(
    MirStream *out, struct Sym *function, int function_number)
{
    mir_stream_puts("\tld hl,0\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n", function_number);
    mir_call_recovery_emit_function_address(out, function);
    mir_emit_runtime_call(out, "__call_hl");
    mir_emit_final_call_cleanup(out, 2);
}

static void mir_emit_bios_print(
    MirStream *out, const struct MirBiosCallSchedule *plan,
    int format_string, int value_string)
{
    if (value_string >= 0)
        mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", value_string);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", format_string);
    mir_emit_runtime_call(out, plan->print_name);
    mir_emit_final_call_cleanup(
        out, value_string >= 0 ? 2 : 1);
}

static void mir_emit_bios_choose_agreement(
    MirStream *out, const struct MirBiosCallSchedule *plan)
{
    int disagree = new_label();
    int ready = new_label();

    mir_stream_puts("\tpush iy\n\tpop de\n\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out,
            "\tjp nz,L%d\n\tld hl,S%d\n\tjp L%d\n"
            "L%d:\n\tld hl,S%d\nL%d:\n",
            disagree, plan->strings[3], ready,
            disagree, plan->strings[4], ready);
}

static void mir_emit_bios_agreement(
    MirStream *out, const struct MirBiosCallSchedule *plan,
    int format_string)
{
    mir_emit_bios_choose_agreement(out, plan);
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", format_string);
    mir_emit_runtime_call(out, plan->print_name);
    mir_emit_final_call_cleanup(out, 2);
}

static void mir_emit_bios_call_schedule(
    MirStream *out, const struct MirBiosCallSchedule *plan)
{
    int item;

    mir_stream_printf(out,
            "%s\n"
            ";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
            "\tpush iy\n",
            MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_emit_bios_print(out, plan, plan->strings[0], -1);
    mir_emit_bios_direct_call(
        out, plan->bios_name, plan->status_function, 0);
    mir_stream_puts("\tpush hl\n\tpop iy\n", out);
    mir_emit_bios_indirect_call(
        out, plan->bios_function, plan->status_function);
    mir_emit_bios_choose_agreement(out, plan);
    mir_stream_puts("\tpush hl\n", out);
    mir_emit_bios_print(out, plan, plan->strings[1], -1);
    mir_stream_puts("\tpop hl\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[2]);
    mir_emit_runtime_call(out, plan->print_name);
    mir_emit_final_call_cleanup(out, 2);
    for (item = 0; item < 5; ++item)
        mir_emit_bios_direct_call(
            out, plan->bios_name, plan->output_function,
            plan->output_characters[item]);
    mir_emit_bios_direct_call(
        out, plan->bioshl_name, plan->status_function, 0);
    mir_stream_puts("\tpush hl\n\tpop iy\n", out);
    mir_emit_bios_indirect_call(
        out, plan->bioshl_function, plan->status_function);
    mir_emit_bios_agreement(out, plan, plan->strings[5]);
    mir_emit_bios_print(out, plan, plan->strings[6], -1);
    mir_stream_puts("\tld hl,0\n\tpop iy\n"
          ";@dcc.reg free=iy\n\tret\n", out);
}

static int mir_exec_call_arguments(
    int instruction, int first_definition, int second_definition)
{
    int arguments[2];

    return mir_machine_two_call_arguments(
               &mir.insns[instruction], arguments) &&
           arguments[0] == mir.insns[first_definition].dst &&
           arguments[1] == mir.insns[second_definition].dst;
}

static int mir_exec_print_call(
    struct MirExecArgumentSchedule *plan, int instruction,
    int argument_count, int first_definition, int second_definition)
{
    char call_name[64];
    struct Sym *function;
    int arguments[2];

    function = mir_call_recovery_function(
        instruction, 1, 1, 0, call_name);
    if (function == NULL || argument_count < 1 || argument_count > 2 ||
        !mir_machine_call_arguments(
            &mir.insns[instruction], argument_count, arguments) ||
        arguments[0] != mir.insns[first_definition].dst ||
        (argument_count == 2 &&
         arguments[1] != mir.insns[second_definition].dst))
        return 0;
    if (plan->print_function == NULL) {
        plan->print_function = function;
        snprintf(plan->print_name, sizeof(plan->print_name), "%s",
                 call_name);
    }
    return plan->print_function == function &&
           !strcmp(plan->print_name, call_name);
}

static int mir_match_exec_argument_schedule(
    struct MirExecArgumentSchedule *plan)
{
    static const unsigned char expected_opcodes[105] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_RETURN, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_RETURN, MIR_NOP, MIR_LABEL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_STRING_ADDRESS, MIR_STORE_INDIRECT,
        MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_NOP,
        MIR_STORE_INDIRECT, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE, MIR_NOP, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN,
        MIR_NOP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN
    };
    struct Sym *compare_function;
    struct Sym *child_function;
    struct Sym *exec_function;
    struct Sym *execv_function;
    char call_name[64];
    int argc_offset;
    int argv_offset;

    memset(plan, 0, sizeof(*plan));
    if (!mir_call_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        mir_cfg_block_count() != 7 || mir.local_bytes != 8 ||
        mir.has_vla ||
        !mir_memory_runner_word_type(mir.return_type, 0) ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &argc_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &argv_offset) ||
        argv_offset != argc_offset + 2 ||
        !mir_memory_runner_word_type(mir.insns[1].type, 0) ||
        !mir_memory_runner_pointer_type(
            mir.insns[2].type, 2, TYPE_CHAR) ||
        !mir_machine_named_nonvolatile(&mir.insns[1]) ||
        !mir_machine_named_nonvolatile(&mir.insns[2]))
        return 0;
    if (!mir_machine_constant_equals(mir.insns[4].dst, 2) ||
        mir.insns[5].src1 != mir.insns[1].dst ||
        mir.insns[5].src2 != mir.insns[4].dst ||
        mir.insns[5].immediate != TOK_GE ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        mir.insns[6].label != mir.insns[21].label ||
        strcmp(mir.insns[7].name, mir.insns[2].name) ||
        !mir_machine_constant_equals(mir.insns[8].dst, 1) ||
        mir.insns[9].src1 != mir.insns[7].dst ||
        mir.insns[9].src2 != mir.insns[8].dst ||
        mir.insns[9].immediate != 2 ||
        mir.insns[9].memory_size != 2 ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].memory_size != 2 ||
        !mir_call_char_pointer_type(mir.insns[10].type))
        return mir_machine_reject(
            "exec-argument-schedule", "child-condition");
    compare_function = mir_call_recovery_function(
        14, 0, 2, 0, plan->compare_name);
    child_function = mir_call_recovery_function(
        30, 0, 2, 0, call_name);
    if (compare_function == NULL || child_function == NULL ||
        !mir_exec_call_arguments(14, 10, 12) ||
        !mir_memory_runner_word_type(compare_function->type, 0) ||
        !mir_call_char_pointer_type(
            compare_function->proto_types[0]) ||
        !mir_call_char_pointer_type(
            compare_function->proto_types[1]) ||
        !mir_machine_constant_equals(mir.insns[15].dst, 0) ||
        mir.insns[16].src1 != mir.insns[14].dst ||
        mir.insns[16].src2 != mir.insns[15].dst ||
        mir.insns[16].immediate != TOK_EQ ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        mir.insns[17].label != mir.insns[21].label ||
        !mir_machine_constant_equals(mir.insns[19].dst, 1) ||
        mir.insns[20].label != mir.insns[23].label ||
        !mir_machine_constant_equals(mir.insns[22].dst, 0) ||
        mir.insns[24].src1 != mir.insns[19].dst ||
        mir.insns[24].src2 != mir.insns[22].dst ||
        mir.insns[24].phi_pred1 != mir.insns[18].label ||
        mir.insns[24].phi_pred2 != mir.insns[21].label ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        mir.insns[25].label != mir.insns[32].label ||
        !mir_exec_call_arguments(30, 1, 28) ||
        strcmp(mir.insns[26].name, mir.insns[1].name) ||
        strcmp(mir.insns[28].name, mir.insns[2].name) ||
        mir.insns[31].src1 != mir.insns[30].dst)
        return mir_machine_reject(
            "exec-argument-schedule", "child-call");
    exec_function = mir_call_recovery_function(
        40, 0, 2, 0, plan->exec_name);
    execv_function = mir_call_recovery_function(
        75, 0, 2, 0, plan->execv_name);
    if (exec_function == NULL || execv_function == NULL ||
        exec_function == execv_function ||
        !mir_exec_call_arguments(40, 36, 38) ||
        !mir_exec_call_arguments(75, 71, 73) ||
        mir_call_recovery_function(
            99, 0, 2, 0, call_name) != exec_function ||
        strcmp(call_name, plan->exec_name) ||
        !mir_exec_call_arguments(99, 95, 97) ||
        !mir_memory_runner_word_type(exec_function->type, 0) ||
        !mir_memory_runner_word_type(execv_function->type, 0) ||
        !mir_call_char_pointer_type(exec_function->proto_types[0]) ||
        !mir_call_char_pointer_type(exec_function->proto_types[1]) ||
        !mir_call_char_pointer_type(execv_function->proto_types[0]) ||
        !mir_memory_runner_pointer_type(
            execv_function->proto_types[1], 2, TYPE_CHAR))
        return mir_machine_reject(
            "exec-argument-schedule", "exec-calls");
    if (!mir_machine_constant_equals(mir.insns[45].dst, 65535) ||
        mir.insns[46].src1 != mir.insns[40].dst ||
        mir.insns[46].src2 != mir.insns[45].dst ||
        mir.insns[46].immediate != TOK_NE ||
        mir.insns[47].src1 != mir.insns[46].dst ||
        mir.insns[47].label != mir.insns[56].label ||
        !mir_machine_constant_equals(mir.insns[53].dst, 1) ||
        mir.insns[54].src1 != mir.insns[53].dst ||
        !mir_machine_constant_equals(mir.insns[80].dst, 65535) ||
        mir.insns[81].src1 != mir.insns[75].dst ||
        mir.insns[81].src2 != mir.insns[80].dst ||
        mir.insns[81].immediate != TOK_NE ||
        mir.insns[82].src1 != mir.insns[81].dst ||
        mir.insns[82].label != mir.insns[91].label ||
        !mir_machine_constant_equals(mir.insns[88].dst, 1) ||
        mir.insns[89].src1 != mir.insns[88].dst ||
        !mir_machine_constant_equals(mir.insns[103].dst, 1) ||
        mir.insns[104].src1 != mir.insns[103].dst)
        return mir_machine_reject(
            "exec-argument-schedule", "failure-flow");
    if (!mir_machine_same_location(
            &mir.insns[60], &mir.insns[65]) ||
        !mir_machine_same_location(
            &mir.insns[60], &mir.insns[73]) ||
        !mir_machine_constant_equals(mir.insns[61].dst, 0) ||
        mir.insns[62].src1 != mir.insns[60].dst ||
        mir.insns[62].src2 != mir.insns[61].dst ||
        mir.insns[62].immediate != 2 ||
        mir.insns[62].memory_size != 2 ||
        mir.insns[64].src1 != mir.insns[62].dst ||
        mir.insns[64].src2 != mir.insns[63].dst ||
        mir.insns[64].memory_size != 2 ||
        !mir_machine_constant_equals(mir.insns[66].dst, 1) ||
        mir.insns[67].src1 != mir.insns[65].dst ||
        mir.insns[67].src2 != mir.insns[66].dst ||
        mir.insns[67].immediate != 2 ||
        mir.insns[67].memory_size != 2 ||
        !mir_machine_constant_equals(mir.insns[68].dst, 0) ||
        mir.insns[70].src1 != mir.insns[67].dst ||
        mir.insns[70].src2 != mir.insns[68].dst ||
        mir.insns[70].memory_size != 2)
        return mir_machine_reject(
            "exec-argument-schedule", "argv-local");
    if (!mir_exec_print_call(plan, 35, 1, 33, 0) ||
        !mir_exec_print_call(plan, 52, 2, 48, 40) ||
        !mir_exec_print_call(plan, 59, 1, 57, 0) ||
        !mir_exec_print_call(plan, 87, 2, 83, 75) ||
        !mir_exec_print_call(plan, 94, 1, 92, 0) ||
        !mir_exec_print_call(plan, 102, 1, 100, 0))
        return mir_machine_reject(
            "exec-argument-schedule", "prints");
    if (mir.insns[63].immediate != mir.insns[36].immediate ||
        mir.insns[71].immediate != mir.insns[36].immediate)
        return mir_machine_reject(
            "exec-argument-schedule", "filename-reuse");
    plan->compare_function = compare_function;
    plan->child_function = child_function;
    plan->exec_function = exec_function;
    plan->execv_function = execv_function;
    plan->argc_stack_offset = argc_offset;
    plan->argv_stack_offset = argv_offset;
    plan->strings[0] = (int)mir.insns[12].immediate;
    plan->strings[1] = (int)mir.insns[33].immediate;
    plan->strings[2] = (int)mir.insns[36].immediate;
    plan->strings[3] = (int)mir.insns[38].immediate;
    plan->strings[4] = (int)mir.insns[48].immediate;
    plan->strings[5] = (int)mir.insns[57].immediate;
    plan->strings[6] = (int)mir.insns[83].immediate;
    plan->strings[7] = (int)mir.insns[92].immediate;
    plan->strings[8] = (int)mir.insns[95].immediate;
    plan->strings[9] = (int)mir.insns[97].immediate;
    plan->strings[10] = (int)mir.insns[100].immediate;
    return plan->strings[10] >= 0;
}

static void mir_exec_load_parameter(
    MirStream *out, int stack_offset)
{
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            stack_offset + 2, stack_offset + 3);
}

static void mir_exec_print_string(
    MirStream *out, const struct MirExecArgumentSchedule *plan,
    int string_id)
{
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_call_recovery_emit_named_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 1);
}

static void mir_exec_failure(
    MirStream *out, const struct MirExecArgumentSchedule *plan,
    int string_id)
{
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_call_recovery_emit_named_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 2);
}

static void mir_emit_exec_argument_schedule(
    MirStream *out, const struct MirExecArgumentSchedule *plan)
{
    int parent = new_label();
    int first_ok = new_label();
    int second_ok = new_label();
    int return_one = new_label();
    int done = new_label();

    mir_stream_printf(out,
            "%s\n"
            "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tdec sp\n\tdec sp\n\tdec sp\n\tdec sp\n",
            MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_exec_load_parameter(out, plan->argc_stack_offset);
    mir_stream_printf(out,
            "\tbit 7,h\n\tjp nz,L%d\n"
            "\tld de,2\n\tor a\n\tsbc hl,de\n\tjp c,L%d\n",
            parent, parent);
    mir_exec_load_parameter(out, plan->argv_stack_offset);
    mir_stream_puts("\tinc hl\n\tinc hl\n"
          "\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n"
          "\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[0]);
    mir_call_recovery_emit_named_call(
        out, plan->compare_function, plan->compare_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_stream_printf(out, "\tld a,h\n\tor l\n\tjp nz,L%d\n", parent);
    mir_exec_load_parameter(out, plan->argv_stack_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_exec_load_parameter(out, plan->argc_stack_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->child_function);
    mir_emit_final_call_cleanup(out, 2);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", done, parent);

    mir_exec_print_string(out, plan, plan->strings[1]);
    mir_stream_printf(out,
            "\tld hl,S%d\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->strings[3], plan->strings[2]);
    mir_call_recovery_emit_named_call(
        out, plan->exec_function, plan->exec_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_stream_puts("\tld de,-1\n\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n\tld de,-1\n\tadd hl,de\n",
            first_ok);
    mir_exec_failure(out, plan, plan->strings[4]);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", return_one, first_ok);

    mir_exec_print_string(out, plan, plan->strings[5]);
    mir_stream_puts("\tpush ix\n\tpop hl\n\tld de,-4\n\tadd hl,de\n", out);
    mir_stream_printf(out,
            "\tld de,S%d\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n"
            "\tinc hl\n\txor a\n\tld (hl),a\n\tinc hl\n\tld (hl),a\n",
            plan->strings[2]);
    mir_stream_puts("\tpush ix\n\tpop hl\n\tld de,-4\n\tadd hl,de\n"
          "\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[2]);
    mir_call_recovery_emit_named_call(
        out, plan->execv_function, plan->execv_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_stream_puts("\tld de,-1\n\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n\tld de,-1\n\tadd hl,de\n",
            second_ok);
    mir_exec_failure(out, plan, plan->strings[6]);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", return_one, second_ok);

    mir_exec_print_string(out, plan, plan->strings[7]);
    mir_stream_printf(out,
            "\tld hl,S%d\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->strings[9], plan->strings[8]);
    mir_call_recovery_emit_named_call(
        out, plan->exec_function, plan->exec_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_exec_print_string(out, plan, plan->strings[10]);
    mir_stream_printf(out,
            "L%d:\n\tld hl,1\n"
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            return_one, done);
}

static int mir_temp_no_call_arguments(const struct MirInsn *call)
{
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_ARG &&
            mir.insns[instruction].secondary_offset ==
                call->secondary_offset)
            return 0;
    return 1;
}

static int mir_temp_local_address(
    int instruction, int *offset_out)
{
    int type;
    int storage;
    int offset;

    if (mir.insns[instruction].opcode != MIR_ADDRESS ||
        !mir_machine_named_nonvolatile(&mir.insns[instruction]) ||
        !mir_scalar_memory_location(
            &mir.insns[instruction], &type, &storage, &offset) ||
        storage != SC_LOCAL)
        return 0;
    *offset_out = offset;
    return 1;
}

static int mir_temp_print_call(
    struct MirTemporaryFileSchedule *plan, int instruction,
    int argument_count, const int *definitions)
{
    char call_name[64];
    struct Sym *function;
    int arguments[2];
    int argument;

    function = mir_call_recovery_function(
        instruction, 1, 1, 0, call_name);
    if (function == NULL || argument_count < 1 || argument_count > 2 ||
        !mir_machine_call_arguments(
            &mir.insns[instruction], argument_count, arguments))
        return 0;
    for (argument = 0; argument < argument_count; ++argument)
        if (arguments[argument] !=
                mir.insns[definitions[argument]].dst)
            return 0;
    if (plan->print_function == NULL) {
        plan->print_function = function;
        snprintf(plan->print_name, sizeof(plan->print_name), "%s",
                 call_name);
    }
    return plan->print_function == function &&
           !strcmp(plan->print_name, call_name);
}

static int mir_temp_failure_increment(
    int load_instruction, int constant_instruction,
    int binary_instruction, int store_instruction,
    struct Sym **failure_count)
{
    struct Sym *symbol = find_global(mir.insns[load_instruction].name);

    if (symbol == NULL || symbol->storage == SC_FUNC ||
        symbol->is_array || symbol->is_volatile ||
        !mir_memory_runner_word_type(symbol->type, 0) ||
        !mir_machine_named_nonvolatile(
            &mir.insns[load_instruction]) ||
        !mir_machine_named_nonvolatile(
            &mir.insns[store_instruction]) ||
        !mir_machine_same_location(
            &mir.insns[load_instruction],
            &mir.insns[store_instruction]) ||
        !mir_machine_constant_equals(
            mir.insns[constant_instruction].dst, 1) ||
        mir.insns[binary_instruction].src1 !=
            mir.insns[load_instruction].dst ||
        mir.insns[binary_instruction].src2 !=
            mir.insns[constant_instruction].dst ||
        mir.insns[binary_instruction].immediate != '+' ||
        mir.insns[store_instruction].src1 !=
            mir.insns[binary_instruction].dst)
        return 0;
    if (*failure_count == NULL)
        *failure_count = symbol;
    return *failure_count == symbol;
}

static int mir_match_temporary_file_schedule(
    struct MirTemporaryFileSchedule *plan)
{
    static const unsigned char expected_opcodes[168] = {
        MIR_LABEL, MIR_CONST, MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_STORE, MIR_LOAD, MIR_UNARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_NOP, MIR_JUMP, MIR_LABEL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_LABEL, MIR_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ADDRESS,
        MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_LOAD,
        MIR_ADDRESS, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_LOAD,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_ARG, MIR_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_NOP, MIR_LABEL, MIR_CALL,
        MIR_NOP, MIR_STORE, MIR_LOAD, MIR_UNARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_NOP, MIR_JUMP, MIR_LABEL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_LOAD, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_UNARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_NOP, MIR_JUMP,
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_ARG,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_LABEL, MIR_CONST, MIR_RETURN
    };
    static const int single_prints[5][2] = {
        {13, 11}, {82, 80}, {98, 96}, {126, 124}, {164, 162}
    };
    static const int double_prints[3][3] = {
        {25, 21, 23}, {50, 46, 48}, {159, 155, 157}
    };
    static const int increments[4][4] = {
        {14, 15, 16, 17}, {83, 84, 85, 86},
        {99, 100, 101, 102}, {127, 128, 129, 130}
    };
    static const int first_name_uses[4] = {6, 7, 23, 52};
    static const int second_name_uses[3] = {32, 35, 42};
    static const int stream_uses[6] = {91, 92, 108, 111, 118, 147};
    static const int buffer_addresses[5] = {28, 36, 43, 48, 66};
    static const int read_buffer_addresses[2] = {114, 136};
    struct Sym *function;
    char call_name[64];
    int definitions[2];
    int arguments[3];
    int offset;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (!mir_call_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        mir_cfg_block_count() != 21 || mir.local_bytes != 55 ||
        mir.has_vla ||
        !mir_memory_runner_word_type(mir.return_type, 0))
        return 0;
    plan->frame_bytes = mir.local_bytes;
    for (item = 1; item < 4; ++item)
        if (!mir_machine_same_location(
                &mir.insns[first_name_uses[0]],
                &mir.insns[first_name_uses[item]]))
            return mir_machine_reject(
                "temporary-file-schedule", "first-name-local");
    for (item = 1; item < 3; ++item)
        if (!mir_machine_same_location(
                &mir.insns[second_name_uses[0]],
                &mir.insns[second_name_uses[item]]))
            return mir_machine_reject(
                "temporary-file-schedule", "second-name-local");
    for (item = 1; item < 6; ++item)
        if (!mir_machine_same_location(
                &mir.insns[stream_uses[0]],
                &mir.insns[stream_uses[item]]))
            return mir_machine_reject(
                "temporary-file-schedule", "stream-local");
    if (!mir_scalar_memory_location(
            &mir.insns[6], &arguments[0], &arguments[1],
            &plan->first_name_offset) ||
        arguments[1] != SC_LOCAL ||
        !mir_scalar_memory_location(
            &mir.insns[32], &arguments[0], &arguments[1],
            &plan->second_name_offset) ||
        arguments[1] != SC_LOCAL ||
        !mir_scalar_memory_location(
            &mir.insns[91], &arguments[0], &arguments[1],
            &plan->stream_offset) ||
        arguments[1] != SC_LOCAL)
        return mir_machine_reject(
            "temporary-file-schedule", "scalar-locals");
    if (!mir_temp_local_address(
            buffer_addresses[0], &plan->buffer_offset) ||
        !mir_temp_local_address(
            read_buffer_addresses[0],
            &plan->read_buffer_offset))
        return mir_machine_reject(
            "temporary-file-schedule", "buffers");
    for (item = 1; item < 5; ++item) {
        if (!mir_temp_local_address(
                buffer_addresses[item], &offset) ||
            offset != plan->buffer_offset)
            return mir_machine_reject(
                "temporary-file-schedule", "buffer-alias");
    }
    if (!mir_temp_local_address(
            read_buffer_addresses[1], &offset) ||
        offset != plan->read_buffer_offset ||
        plan->buffer_offset == plan->read_buffer_offset)
        return mir_machine_reject(
            "temporary-file-schedule", "read-buffer-alias");
    plan->tmpnam_function = mir_call_recovery_function(
        4, 0, 1, 0, plan->tmpnam_name);
    function = mir_call_recovery_function(
        30, 0, 1, 0, call_name);
    if (plan->tmpnam_function == NULL ||
        function != plan->tmpnam_function ||
        strcmp(call_name, plan->tmpnam_name) ||
        !mir_machine_single_call_argument(
            &mir.insns[4], &arguments[0]) ||
        !mir_machine_constant_equals(arguments[0], 0) ||
        !mir_machine_single_call_argument(
            &mir.insns[30], &arguments[0]) ||
        arguments[0] != mir.insns[28].dst ||
        !mir_call_char_pointer_type(
            plan->tmpnam_function->type) ||
        !mir_call_char_pointer_type(
            plan->tmpnam_function->proto_types[0]))
        return mir_machine_reject(
            "temporary-file-schedule", "tmpnam-calls");
    plan->check_function = mir_call_recovery_function(
        41, 0, 3, 0, plan->check_name);
    function = mir_call_recovery_function(
        144, 0, 3, 0, call_name);
    if (plan->check_function == NULL ||
        function != plan->check_function ||
        strcmp(call_name, plan->check_name) ||
        !mir_machine_three_call_arguments(
            &mir.insns[41], arguments) ||
        arguments[0] != mir.insns[33].dst ||
        arguments[1] != mir.insns[37].dst ||
        arguments[2] != mir.insns[39].dst ||
        !mir_machine_constant_equals(arguments[2], 1) ||
        !mir_machine_three_call_arguments(
            &mir.insns[144], arguments) ||
        arguments[0] != mir.insns[134].dst ||
        arguments[1] != mir.insns[140].dst ||
        arguments[2] != mir.insns[142].dst ||
        !mir_machine_constant_equals(arguments[2], 0))
        return mir_machine_reject(
            "temporary-file-schedule", "check-calls");
    plan->compare_function = mir_call_recovery_function(
        68, 0, 2, 0, plan->compare_name);
    function = mir_call_recovery_function(
        140, 0, 2, 0, call_name);
    if (plan->compare_function == NULL ||
        function != plan->compare_function ||
        strcmp(call_name, plan->compare_name) ||
        !mir_exec_call_arguments(68, 64, 66) ||
        !mir_exec_call_arguments(140, 136, 138))
        return mir_machine_reject(
            "temporary-file-schedule", "compare-calls");
    plan->tmpfile_function = mir_call_recovery_function(
        89, 0, 0, 0, plan->tmpfile_name);
    if (plan->tmpfile_function == NULL ||
        !mir_temp_no_call_arguments(&mir.insns[89]) ||
        type_ptr_depth(plan->tmpfile_function->type) != 1 ||
        type_size(plan->tmpfile_function->type) != 2)
        return mir_machine_reject(
            "temporary-file-schedule", "tmpfile-call");
    plan->puts_function = mir_call_recovery_function(
        110, 0, 2, 0, plan->puts_name);
    plan->rewind_function = mir_call_recovery_function(
        113, 0, 1, 0, plan->rewind_name);
    plan->gets_function = mir_call_recovery_function(
        120, 0, 3, 0, plan->gets_name);
    plan->close_function = mir_call_recovery_function(
        149, 0, 1, 0, plan->close_name);
    if (plan->puts_function == NULL ||
        plan->rewind_function == NULL ||
        plan->gets_function == NULL ||
        plan->close_function == NULL ||
        !mir_exec_call_arguments(110, 106, 108) ||
        !mir_machine_single_call_argument(
            &mir.insns[113], &arguments[0]) ||
        arguments[0] != mir.insns[111].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[120], arguments) ||
        arguments[0] != mir.insns[114].dst ||
        arguments[1] != mir.insns[116].dst ||
        arguments[2] != mir.insns[118].dst ||
        !mir_machine_constant_equals(arguments[1], 32) ||
        !mir_machine_single_call_argument(
            &mir.insns[149], &arguments[0]) ||
        arguments[0] != mir.insns[147].dst)
        return mir_machine_reject(
            "temporary-file-schedule", "file-calls");
    for (item = 0; item < 5; ++item) {
        definitions[0] = single_prints[item][1];
        if (!mir_temp_print_call(
                plan, single_prints[item][0], 1, definitions))
            return mir_machine_reject(
                "temporary-file-schedule", "single-print");
    }
    for (item = 0; item < 3; ++item) {
        definitions[0] = double_prints[item][1];
        definitions[1] = double_prints[item][2];
        if (!mir_temp_print_call(
                plan, double_prints[item][0], 2, definitions))
            return mir_machine_reject(
                "temporary-file-schedule", "double-print");
    }
    for (item = 0; item < 4; ++item)
        if (!mir_temp_failure_increment(
                increments[item][0], increments[item][1],
                increments[item][2], increments[item][3],
                &plan->failure_count))
            return mir_machine_reject(
                "temporary-file-schedule", "failure-increment");
    if (find_global(mir.insns[152].name) != plan->failure_count ||
        find_global(mir.insns[157].name) != plan->failure_count ||
        !mir_machine_named_nonvolatile(&mir.insns[152]) ||
        !mir_machine_named_nonvolatile(&mir.insns[157]) ||
        mir.insns[153].src1 != mir.insns[152].dst ||
        mir.insns[153].label != mir.insns[161].label ||
        !mir_machine_constant_equals(mir.insns[166].dst, 0) ||
        mir.insns[167].src1 != mir.insns[166].dst)
        return mir_machine_reject(
            "temporary-file-schedule", "summary");
    if (mir.insns[8].src1 != mir.insns[7].dst ||
        mir.insns[8].immediate != '!' ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].label != mir.insns[20].label ||
        mir.insns[19].label != mir.insns[27].label ||
        mir.insns[37].src1 != mir.insns[35].dst ||
        mir.insns[37].src2 != mir.insns[36].dst ||
        mir.insns[37].immediate != TOK_EQ ||
        mir.insns[44].src1 != mir.insns[42].dst ||
        mir.insns[44].src2 != mir.insns[43].dst ||
        mir.insns[44].immediate != TOK_EQ ||
        mir.insns[45].src1 != mir.insns[44].dst ||
        mir.insns[45].label != mir.insns[51].label ||
        mir.insns[53].src1 != mir.insns[52].dst ||
        mir.insns[53].label != mir.insns[59].label ||
        mir.insns[55].src1 != mir.insns[54].dst ||
        mir.insns[55].label != mir.insns[59].label ||
        mir.insns[62].src1 != mir.insns[57].dst ||
        mir.insns[62].src2 != mir.insns[60].dst ||
        mir.insns[62].phi_pred1 != mir.insns[56].label ||
        mir.insns[62].phi_pred2 != mir.insns[59].label ||
        mir.insns[63].src1 != mir.insns[62].dst ||
        mir.insns[63].label != mir.insns[75].label ||
        !mir_machine_constant_equals(mir.insns[69].dst, 0) ||
        mir.insns[70].src1 != mir.insns[68].dst ||
        mir.insns[70].src2 != mir.insns[69].dst ||
        mir.insns[70].immediate != TOK_EQ ||
        mir.insns[71].src1 != mir.insns[70].dst ||
        mir.insns[71].label != mir.insns[75].label ||
        mir.insns[74].label != mir.insns[77].label ||
        mir.insns[78].src1 != mir.insns[73].dst ||
        mir.insns[78].src2 != mir.insns[76].dst ||
        mir.insns[78].phi_pred1 != mir.insns[72].label ||
        mir.insns[78].phi_pred2 != mir.insns[75].label ||
        mir.insns[79].src1 != mir.insns[78].dst ||
        mir.insns[79].label != mir.insns[88].label ||
        mir.insns[93].src1 != mir.insns[92].dst ||
        mir.insns[93].immediate != '!' ||
        mir.insns[94].src1 != mir.insns[93].dst ||
        mir.insns[94].label != mir.insns[105].label ||
        mir.insns[104].label != mir.insns[151].label ||
        mir.insns[121].src1 != mir.insns[120].dst ||
        mir.insns[121].immediate != '!' ||
        mir.insns[122].src1 != mir.insns[121].dst ||
        mir.insns[122].label != mir.insns[133].label ||
        mir.insns[132].label != mir.insns[146].label ||
        mir.insns[160].label != mir.insns[165].label)
        return mir_machine_reject(
            "temporary-file-schedule", "control-flow");
    plan->strings[0] = (int)mir.insns[11].immediate;
    plan->strings[1] = (int)mir.insns[21].immediate;
    plan->strings[2] = (int)mir.insns[33].immediate;
    plan->strings[3] = (int)mir.insns[46].immediate;
    plan->strings[4] = (int)mir.insns[80].immediate;
    plan->strings[5] = (int)mir.insns[96].immediate;
    plan->strings[6] = (int)mir.insns[106].immediate;
    plan->strings[7] = (int)mir.insns[124].immediate;
    plan->strings[8] = (int)mir.insns[134].immediate;
    plan->strings[9] = (int)mir.insns[155].immediate;
    plan->strings[10] = (int)mir.insns[162].immediate;
    if (plan->strings[6] != (int)mir.insns[138].immediate)
        return mir_machine_reject(
            "temporary-file-schedule", "content-string");
    return 1;
}

static void mir_temp_emit_ix_address(MirStream *out, int offset)
{
    mir_stream_puts("\tpush ix\n\tpop hl\n", out);
    if (offset != 0)
        mir_stream_printf(out, "\tld de,%d\n\tadd hl,de\n", offset);
}

static void mir_temp_store_hl(MirStream *out, int offset)
{
    mir_stream_printf(out,
            "\tld (ix%+d),l\n\tld (ix%+d),h\n",
            offset, offset + 1);
}

static void mir_temp_load_hl(MirStream *out, int offset)
{
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            offset, offset + 1);
}

static void mir_temp_increment_failures(
    MirStream *out, const struct MirTemporaryFileSchedule *plan)
{
    mir_machine_emit_global_word(out, plan->failure_count, 0);
    mir_stream_puts("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->failure_count, 0);
}

static void mir_temp_print_one(
    MirStream *out, const struct MirTemporaryFileSchedule *plan,
    int string_id)
{
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_call_recovery_emit_named_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 1);
}

static void mir_temp_print_pointer(
    MirStream *out, const struct MirTemporaryFileSchedule *plan,
    int string_id, int pointer_offset)
{
    mir_temp_load_hl(out, pointer_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_call_recovery_emit_named_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 2);
}

static void mir_temp_call_check(
    MirStream *out, const struct MirTemporaryFileSchedule *plan,
    int name_string, int expected)
{
    mir_stream_printf(out,
            "\tld de,%d\n\tpush de\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            expected, name_string);
    mir_call_recovery_emit_named_call(
        out, plan->check_function, plan->check_name);
    mir_emit_final_call_cleanup(out, 3);
}

static void mir_emit_temporary_file_schedule(
    MirStream *out, const struct MirTemporaryFileSchedule *plan)
{
    int first_ok = new_label();
    int first_done = new_label();
    int pointer_equal = new_label();
    int pointer_checked = new_label();
    int duplicate_done = new_label();
    int names_checked = new_label();
    int file_ok = new_label();
    int content_ok = new_label();
    int close_stream = new_label();
    int file_done = new_label();
    int summary_success = new_label();
    int done = new_label();

    mir_stream_printf(out,
            "%s\n"
            "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n",
            MIR_EXACT_KERNEL_MARKER, plan->frame_bytes);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\tld hl,0\n\tpush hl\n", out);
    mir_call_recovery_emit_named_call(
        out, plan->tmpnam_function, plan->tmpnam_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_temp_store_hl(out, plan->first_name_offset);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", first_ok);
    mir_temp_print_one(out, plan, plan->strings[0]);
    mir_temp_increment_failures(out, plan);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", first_done, first_ok);
    mir_temp_print_pointer(
        out, plan, plan->strings[1],
        plan->first_name_offset);
    mir_stream_printf(out, "L%d:\n", first_done);

    mir_temp_emit_ix_address(out, plan->buffer_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_call_recovery_emit_named_call(
        out, plan->tmpnam_function, plan->tmpnam_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_temp_store_hl(out, plan->second_name_offset);
    mir_temp_emit_ix_address(out, plan->buffer_offset);
    mir_stream_puts("\tex de,hl\n", out);
    mir_temp_load_hl(out, plan->second_name_offset);
    mir_stream_puts("\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out,
            "\tjp z,L%d\n\tld hl,0\n\tjp L%d\n"
            "L%d:\n\tld hl,1\nL%d:\n",
            pointer_equal, pointer_checked,
            pointer_equal, pointer_checked);
    mir_temp_call_check(out, plan, plan->strings[2], 1);
    mir_temp_emit_ix_address(out, plan->buffer_offset);
    mir_stream_puts("\tex de,hl\n", out);
    mir_temp_load_hl(out, plan->second_name_offset);
    mir_stream_puts("\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", duplicate_done);
    mir_temp_print_pointer(
        out, plan, plan->strings[3],
        plan->second_name_offset);
    mir_stream_printf(out, "L%d:\n", duplicate_done);

    mir_temp_load_hl(out, plan->first_name_offset);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", names_checked);
    mir_temp_load_hl(out, plan->second_name_offset);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", names_checked);
    mir_temp_emit_ix_address(out, plan->buffer_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_temp_load_hl(out, plan->first_name_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_call_recovery_emit_named_call(
        out, plan->compare_function, plan->compare_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", names_checked);
    mir_temp_print_one(out, plan, plan->strings[4]);
    mir_temp_increment_failures(out, plan);
    mir_stream_printf(out, "L%d:\n", names_checked);

    mir_call_recovery_emit_named_call(
        out, plan->tmpfile_function, plan->tmpfile_name);
    mir_temp_store_hl(out, plan->stream_offset);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", file_ok);
    mir_temp_print_one(out, plan, plan->strings[5]);
    mir_temp_increment_failures(out, plan);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", file_done, file_ok);
    mir_temp_load_hl(out, plan->stream_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[6]);
    mir_call_recovery_emit_named_call(
        out, plan->puts_function, plan->puts_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_temp_load_hl(out, plan->stream_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_call_recovery_emit_named_call(
        out, plan->rewind_function, plan->rewind_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_temp_load_hl(out, plan->stream_offset);
    mir_stream_puts("\tpush hl\n\tld hl,32\n\tpush hl\n", out);
    mir_temp_emit_ix_address(out, plan->read_buffer_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_call_recovery_emit_named_call(
        out, plan->gets_function, plan->gets_name);
    mir_emit_final_call_cleanup(out, 3);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", content_ok);
    mir_temp_print_one(out, plan, plan->strings[7]);
    mir_temp_increment_failures(out, plan);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", close_stream, content_ok);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[6]);
    mir_temp_emit_ix_address(out, plan->read_buffer_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_call_recovery_emit_named_call(
        out, plan->compare_function, plan->compare_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_temp_call_check(out, plan, plan->strings[8], 0);
    mir_stream_printf(out, "L%d:\n", close_stream);
    mir_temp_load_hl(out, plan->stream_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_call_recovery_emit_named_call(
        out, plan->close_function, plan->close_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_stream_printf(out, "L%d:\n", file_done);

    mir_machine_emit_global_word(out, plan->failure_count, 0);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n\tpush hl\n", summary_success);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[9]);
    mir_call_recovery_emit_named_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", done, summary_success);
    mir_temp_print_one(out, plan, plan->strings[10]);
    mir_stream_printf(out,
            "L%d:\n\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n",
            done);
}

static int mir_match_nullable_string_check_schedule(
    struct MirNullableStringCheckSchedule *plan)
{
    static const unsigned char expected_opcodes[85] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL,
        MIR_JUMP, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_LOAD, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL,
        MIR_JUMP, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_LOAD, MIR_BRANCH_FALSE, MIR_LOAD, MIR_LABEL,
        MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_LABEL,
        MIR_LABEL, MIR_PHI, MIR_ARG, MIR_LOAD,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_LABEL, MIR_LABEL,
        MIR_PHI, MIR_ARG, MIR_CALL, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_NOP, MIR_LABEL
    };
    static const struct MirCallRecoveryEdge edges[15] = {
        {7, 11}, {10, 25}, {15, 19}, {18, 21}, {24, 25},
        {27, 31}, {30, 49}, {39, 43}, {42, 45}, {48, 49},
        {51, 84}, {57, 61}, {60, 64}, {68, 72}, {71, 75}
    };
    static const struct MirCallRecoveryPhi phis[6] = {
        {22, 10, 11, 16, 19}, {26, 6, 12, 8, 23},
        {46, 20, 21, 40, 43}, {50, 14, 22, 28, 47},
        {65, 27, 28, 59, 63}, {76, 31, 32, 70, 74}
    };
    struct Sym *compare_function;
    struct Sym *print_function;
    struct Sym *failure_count;
    int compare_arguments[2];
    int print_arguments[4];
    int name_offset;
    int got_offset;
    int want_offset;

    memset(plan, 0, sizeof(*plan));
    if (!mir_call_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        !mir_call_recovery_edges(
            edges, sizeof(edges) / sizeof(edges[0])) ||
        !mir_call_recovery_phis(
            phis, sizeof(phis) / sizeof(phis[0])) ||
        mir_cfg_block_count() != 24 || mir.local_bytes != 0 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &name_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &got_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[3].dst, &want_offset) ||
        got_offset != name_offset + 2 ||
        want_offset != got_offset + 2 ||
        !mir_call_char_pointer_type(mir.insns[1].type) ||
        !mir_call_char_pointer_type(mir.insns[2].type) ||
        !mir_call_char_pointer_type(mir.insns[3].type) ||
        !mir_machine_named_nonvolatile(&mir.insns[1]) ||
        !mir_machine_named_nonvolatile(&mir.insns[2]) ||
        !mir_machine_named_nonvolatile(&mir.insns[3]))
        return 0;
    if (strcmp(mir.insns[4].name, mir.insns[2].name) ||
        strcmp(mir.insns[12].name, mir.insns[3].name) ||
        strcmp(mir.insns[32].name, mir.insns[2].name) ||
        strcmp(mir.insns[34].name, mir.insns[3].name) ||
        !mir_machine_constant_equals(mir.insns[5].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[13].dst, 0) ||
        mir.insns[6].src1 != mir.insns[4].dst ||
        mir.insns[6].src2 != mir.insns[5].dst ||
        mir.insns[6].immediate != TOK_EQ ||
        mir.insns[14].src1 != mir.insns[12].dst ||
        mir.insns[14].src2 != mir.insns[13].dst ||
        mir.insns[14].immediate != TOK_EQ ||
        !mir_machine_constant_equals(mir.insns[9].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[17].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[20].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[29].dst, 1))
        return mir_machine_reject(
            "nullable-string-check-schedule", "condition");
    compare_function = mir_memory_runner_call_function(36, 0, 2);
    if (compare_function == NULL ||
        !mir_machine_two_call_arguments(
            &mir.insns[36], compare_arguments) ||
        compare_arguments[0] != mir.insns[32].dst ||
        compare_arguments[1] != mir.insns[34].dst ||
        !mir_call_char_pointer_type(
            compare_function->proto_types[0]) ||
        !mir_call_char_pointer_type(
            compare_function->proto_types[1]) ||
        !mir_memory_runner_word_type(compare_function->type, 0) ||
        !mir_machine_constant_equals(mir.insns[37].dst, 0) ||
        mir.insns[38].src1 != mir.insns[36].dst ||
        mir.insns[38].src2 != mir.insns[37].dst ||
        mir.insns[38].immediate != TOK_NE ||
        !mir_machine_constant_equals(mir.insns[41].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[44].dst, 0))
        return mir_machine_reject(
            "nullable-string-check-schedule", "compare");
    print_function = mir_memory_runner_call_function(78, 1, 1);
    if (print_function == NULL ||
        !mir_machine_call_arguments(
            &mir.insns[78], 4, print_arguments) ||
        print_arguments[0] != mir.insns[52].dst ||
        print_arguments[1] != mir.insns[54].dst ||
        print_arguments[2] != mir.insns[65].dst ||
        print_arguments[3] != mir.insns[76].dst ||
        strcmp(mir.insns[54].name, mir.insns[1].name) ||
        strcmp(mir.insns[56].name, mir.insns[2].name) ||
        strcmp(mir.insns[58].name, mir.insns[2].name) ||
        strcmp(mir.insns[67].name, mir.insns[3].name) ||
        strcmp(mir.insns[69].name, mir.insns[3].name) ||
        mir.insns[62].immediate != mir.insns[73].immediate ||
        mir.insns[52].immediate == mir.insns[62].immediate ||
        mir.insns[65].src1 != mir.insns[58].dst ||
        mir.insns[65].src2 != mir.insns[62].dst ||
        mir.insns[76].src1 != mir.insns[69].dst ||
        mir.insns[76].src2 != mir.insns[73].dst)
        return mir_machine_reject(
            "nullable-string-check-schedule", "report");
    failure_count = find_global(mir.insns[79].name);
    if (failure_count == NULL || failure_count->is_volatile ||
        !mir_machine_named_nonvolatile(&mir.insns[79]) ||
        !mir_machine_named_nonvolatile(&mir.insns[82]) ||
        !mir_machine_same_location(&mir.insns[79], &mir.insns[82]) ||
        !mir_machine_constant_equals(mir.insns[80].dst, 1) ||
        mir.insns[81].src1 != mir.insns[79].dst ||
        mir.insns[81].src2 != mir.insns[80].dst ||
        mir.insns[81].immediate != '+' ||
        mir.insns[82].src1 != mir.insns[81].dst)
        return mir_machine_reject(
            "nullable-string-check-schedule", "failure-count");
    plan->compare_function = compare_function;
    plan->print_function = print_function;
    plan->failure_count = failure_count;
    plan->name_stack_offset = name_offset;
    plan->got_stack_offset = got_offset;
    plan->want_stack_offset = want_offset;
    plan->format_string = (int)mir.insns[52].immediate;
    plan->null_string = (int)mir.insns[62].immediate;
    snprintf(plan->compare_name, sizeof(plan->compare_name), "%s",
             mir.insns[36].base_name[0] != 0
                 ? mir.insns[36].base_name
                 : asm_name_for(sym_asm_name(compare_function)));
    snprintf(plan->print_name, sizeof(plan->print_name), "%s",
             mir.insns[78].base_name);
    return plan->print_name[0] != 0;
}

static void mir_emit_nullable_string_check_schedule(
    MirStream *out, const struct MirNullableStringCheckSchedule *plan)
{
    int failed = new_label();
    int got_ready = new_label();
    int want_ready = new_label();
    int done = new_label();

    mir_stream_printf(out,
            "%s\n"
            "\tpush ix\n\tld ix,0\n\tadd ix,sp\n",
            MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld a,h\n\tor l\n\tjp z,L%d\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld a,h\n\tor l\n\tjp z,L%d\n"
            "\tpush hl\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n",
            plan->got_stack_offset + 2,
            plan->got_stack_offset + 3, failed,
            plan->want_stack_offset + 2,
            plan->want_stack_offset + 3, failed,
            plan->got_stack_offset + 2,
            plan->got_stack_offset + 3);
    mir_emit_runtime_call(out, plan->compare_name);
    mir_stream_printf(out,
            "\tpop bc\n\tpop bc\n"
            "\tld a,h\n\tor l\n\tjp z,L%d\n"
            "L%d:\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld a,h\n\tor l\n\tjp nz,L%d\n"
            "\tld hl,S%d\n"
            "L%d:\n\tpush hl\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld a,h\n\tor l\n\tjp nz,L%d\n"
            "\tld hl,S%d\n"
            "L%d:\n\tpush hl\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            done, failed,
            plan->want_stack_offset + 2,
            plan->want_stack_offset + 3, want_ready,
            plan->null_string, want_ready,
            plan->got_stack_offset + 2,
            plan->got_stack_offset + 3, got_ready,
            plan->null_string, got_ready,
            plan->name_stack_offset + 2,
            plan->name_stack_offset + 3,
            plan->format_string);
    mir_emit_runtime_call(out, plan->print_name);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_global_word(out, plan->failure_count, 0);
    mir_stream_puts("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->failure_count, 0);
    mir_stream_printf(out,
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            done);
}

static int mir_match_nullable_string_failure_schedule(
    struct MirNullableStringFailureSchedule *plan)
{
    static const unsigned char expected_opcodes[56] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_LOAD, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL,
        MIR_JUMP, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_LOAD, MIR_BRANCH_FALSE, MIR_LOAD, MIR_LABEL,
        MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_LABEL,
        MIR_LABEL, MIR_PHI, MIR_ARG, MIR_LOAD,
        MIR_ARG, MIR_CALL, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_NOP, MIR_LABEL
    };
    struct Sym *compare_function;
    struct Sym *print_function;
    struct Sym *failure_count;
    int compare_arguments[2];
    int print_arguments[4];
    int label_offset;
    int got_offset;
    int want_offset;

    memset(plan, 0, sizeof(*plan));
    if (!mir_call_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        mir_cfg_block_count() != 13 || mir.local_bytes != 0 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        (mir.return_type & 15) != TYPE_VOID ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &label_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &got_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[3].dst, &want_offset) ||
        got_offset != label_offset + 2 ||
        want_offset != got_offset + 2 ||
        !mir_call_char_pointer_type(mir.insns[1].type) ||
        !mir_call_char_pointer_type(mir.insns[2].type) ||
        !mir_call_char_pointer_type(mir.insns[3].type))
        return 0;
    if (strcmp(mir.insns[4].name, mir.insns[2].name) ||
        strcmp(mir.insns[12].name, mir.insns[2].name) ||
        strcmp(mir.insns[14].name, mir.insns[3].name) ||
        strcmp(mir.insns[34].name, mir.insns[1].name) ||
        strcmp(mir.insns[36].name, mir.insns[2].name) ||
        strcmp(mir.insns[38].name, mir.insns[2].name) ||
        strcmp(mir.insns[47].name, mir.insns[3].name) ||
        !mir_machine_constant_equals(mir.insns[5].dst, 0) ||
        mir.insns[6].src1 != mir.insns[4].dst ||
        mir.insns[6].src2 != mir.insns[5].dst ||
        mir.insns[6].immediate != TOK_EQ ||
        mir.insns[7].src1 != mir.insns[6].dst ||
        mir.insns[7].label != mir.insns[11].label ||
        !mir_machine_constant_equals(mir.insns[9].dst, 1) ||
        mir.insns[10].label != mir.insns[29].label ||
        !mir_machine_constant_equals(mir.insns[17].dst, 0) ||
        mir.insns[18].src1 != mir.insns[16].dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        mir.insns[18].immediate != TOK_NE ||
        mir.insns[19].src1 != mir.insns[18].dst ||
        mir.insns[19].label != mir.insns[23].label ||
        !mir_machine_constant_equals(mir.insns[21].dst, 1) ||
        mir.insns[22].label != mir.insns[25].label ||
        !mir_machine_constant_equals(mir.insns[24].dst, 0) ||
        mir.insns[26].src1 != mir.insns[21].dst ||
        mir.insns[26].src2 != mir.insns[24].dst ||
        mir.insns[26].phi_pred1 != mir.insns[20].label ||
        mir.insns[26].phi_pred2 != mir.insns[23].label ||
        mir.insns[28].label != mir.insns[29].label ||
        mir.insns[30].src1 != mir.insns[9].dst ||
        mir.insns[30].src2 != mir.insns[26].dst ||
        mir.insns[30].phi_pred1 != mir.insns[8].label ||
        mir.insns[30].phi_pred2 != mir.insns[27].label ||
        mir.insns[31].src1 != mir.insns[30].dst ||
        mir.insns[31].label != mir.insns[55].label)
        return mir_machine_reject(
            "nullable-string-failure-schedule", "condition");
    compare_function = mir_memory_runner_call_function(16, 0, 2);
    if (compare_function == NULL ||
        !mir_machine_two_call_arguments(
            &mir.insns[16], compare_arguments) ||
        compare_arguments[0] != mir.insns[12].dst ||
        compare_arguments[1] != mir.insns[14].dst ||
        !mir_call_char_pointer_type(
            compare_function->proto_types[0]) ||
        !mir_call_char_pointer_type(
            compare_function->proto_types[1]) ||
        !mir_memory_runner_word_type(compare_function->type, 0))
        return mir_machine_reject(
            "nullable-string-failure-schedule", "compare");
    if (mir.insns[37].src1 != mir.insns[36].dst ||
        mir.insns[37].label != mir.insns[41].label ||
        mir.insns[40].label != mir.insns[44].label ||
        mir.insns[45].src1 != mir.insns[38].dst ||
        mir.insns[45].src2 != mir.insns[42].dst ||
        mir.insns[45].phi_pred1 != mir.insns[39].label ||
        mir.insns[45].phi_pred2 != mir.insns[43].label)
        return mir_machine_reject(
            "nullable-string-failure-schedule", "null-report");
    print_function = mir_memory_runner_call_function(49, 1, 1);
    if (print_function == NULL ||
        !mir_machine_call_arguments(
            &mir.insns[49], 4, print_arguments) ||
        print_arguments[0] != mir.insns[32].dst ||
        print_arguments[1] != mir.insns[34].dst ||
        print_arguments[2] != mir.insns[45].dst ||
        print_arguments[3] != mir.insns[47].dst ||
        mir.insns[32].immediate == mir.insns[42].immediate)
        return mir_machine_reject(
            "nullable-string-failure-schedule", "report");
    failure_count = find_global(mir.insns[50].name);
    if (failure_count == NULL || failure_count->is_volatile ||
        !mir_machine_named_nonvolatile(&mir.insns[50]) ||
        !mir_machine_named_nonvolatile(&mir.insns[53]) ||
        !mir_machine_same_location(
            &mir.insns[50], &mir.insns[53]) ||
        !mir_machine_constant_equals(mir.insns[51].dst, 1) ||
        mir.insns[52].src1 != mir.insns[50].dst ||
        mir.insns[52].src2 != mir.insns[51].dst ||
        mir.insns[52].immediate != '+' ||
        mir.insns[53].src1 != mir.insns[52].dst)
        return mir_machine_reject(
            "nullable-string-failure-schedule", "failure-count");
    plan->compare_function = compare_function;
    plan->print_function = print_function;
    plan->failure_count = failure_count;
    plan->label_stack_offset = label_offset;
    plan->got_stack_offset = got_offset;
    plan->want_stack_offset = want_offset;
    plan->format_string = (int)mir.insns[32].immediate;
    plan->null_string = (int)mir.insns[42].immediate;
    snprintf(plan->compare_name, sizeof(plan->compare_name), "%s",
             mir.insns[16].base_name[0] != 0
                 ? mir.insns[16].base_name
                 : asm_name_for(sym_asm_name(compare_function)));
    snprintf(plan->print_name, sizeof(plan->print_name), "%s",
             mir.insns[49].base_name);
    return plan->print_name[0] != 0;
}

static void mir_emit_nullable_string_failure_schedule(
    MirStream *out, const struct MirNullableStringFailureSchedule *plan)
{
    int failed = new_label();
    int got_ready = new_label();
    int done = new_label();

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld a,h\n\tor l\n\tjp z,L%d\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n",
            plan->got_stack_offset + 2,
            plan->got_stack_offset + 3, failed,
            plan->want_stack_offset + 2,
            plan->want_stack_offset + 3,
            plan->got_stack_offset + 2,
            plan->got_stack_offset + 3);
    mir_call_recovery_emit_named_call(
        out, plan->compare_function, plan->compare_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\nL%d:\n", done, failed);
    mir_stream_printf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld a,h\n\tor l\n\tjp nz,L%d\n"
            "\tld hl,S%d\nL%d:\n\tpush hl\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->want_stack_offset + 2,
            plan->want_stack_offset + 3,
            plan->got_stack_offset + 2,
            plan->got_stack_offset + 3, got_ready,
            plan->null_string, got_ready,
            plan->label_stack_offset + 2,
            plan->label_stack_offset + 3,
            plan->format_string);
    mir_call_recovery_emit_named_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 4);
    mir_machine_emit_global_word(out, plan->failure_count, 0);
    mir_stream_puts("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->failure_count, 0);
    mir_stream_printf(out,
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            done);
}

static int mir_match_conditional_parameter_schedule(
    struct MirConditionalParameterSchedule *plan)
{
    static const unsigned char expected_opcodes[13] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_RETURN
    };
    long assigned;
    long addend;
    int condition_offset;
    int value_offset;

    memset(plan, 0, sizeof(*plan));
    if (!mir_call_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        mir_cfg_block_count() != 2 || mir.local_bytes != 0 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        !mir_memory_runner_word_type(mir.return_type, 0) ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &condition_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &value_offset) ||
        value_offset != condition_offset + 2 ||
        !mir_memory_runner_word_type(mir.insns[1].type, 0) ||
        !mir_memory_runner_word_type(mir.insns[2].type, 0))
        return 0;
    if (mir.insns[4].src1 != mir.insns[1].dst ||
        mir.insns[4].label != mir.insns[8].label ||
        !mir_machine_evaluate_constant(
            mir.insns[5].dst, &assigned, 0) ||
        mir.insns[7].src1 != mir.insns[5].dst ||
        mir.insns[7].object < 0 ||
        mir.insns[7].object != mir.insns[2].object ||
        !mir_machine_same_location(
            &mir.insns[7], &mir.insns[9]) ||
        !mir_machine_evaluate_constant(
            mir.insns[10].dst, &addend, 0) ||
        mir.insns[11].src1 != mir.insns[9].dst ||
        mir.insns[11].src2 != mir.insns[10].dst ||
        mir.insns[11].immediate != '+' ||
        type_size(mir.insns[11].type) != 2 ||
        mir.insns[12].src1 != mir.insns[11].dst)
        return mir_machine_reject(
            "conditional-parameter-schedule", "semantics");
    plan->condition_stack_offset = condition_offset;
    plan->value_stack_offset = value_offset;
    plan->assigned_value = (int)((unsigned long)assigned & 0xffffUL);
    plan->addend = (int)((unsigned long)addend & 0xffffUL);
    return 1;
}

static void mir_emit_conditional_parameter_schedule(
    MirStream *out, const struct MirConditionalParameterSchedule *plan)
{
    int retain = new_label();
    int finish = new_label();

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n"
            "\tjp z,L%d\n\tld hl,%d\n\tjp L%d\n"
            "L%d:\n\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tex de,hl\nL%d:\n",
            plan->condition_stack_offset, retain,
            plan->assigned_value, finish,
            retain, plan->value_stack_offset, finish);
    if (plan->addend == 1)
        mir_stream_puts("\tinc hl\n", out);
    else if (plan->addend != 0)
        mir_stream_printf(out, "\tld de,%d\n\tadd hl,de\n",
                plan->addend);
    mir_stream_puts("\tret\n", out);
}

static int mir_match_argv_print_schedule(
    struct MirArgvPrintSchedule *plan)
{
    static const unsigned char expected_opcodes[43] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST,
        MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP,
        MIR_PHI, MIR_NOP, MIR_NOP, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP,
        MIR_ARG, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_RETURN
    };
    int count_arguments[2];
    int item_arguments[3];
    int done_arguments[1];
    struct Sym *count_function;
    struct Sym *item_function;
    struct Sym *done_function;

    memset(plan, 0, sizeof(*plan));
    if (!mir_call_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        mir_cfg_block_count() != 4 || mir.local_bytes != 2 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        mir_call_runner_has_volatile_memory() ||
        type_ptr_depth(mir.return_type) != 0 ||
        type_size(mir.return_type) != 2 ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->argc_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &plan->argv_stack_offset) ||
        plan->argv_stack_offset != plan->argc_stack_offset + 2 ||
        type_ptr_depth(mir.insns[1].type) != 0 ||
        type_size(mir.insns[1].type) != 2 ||
        type_ptr_depth(mir.insns[2].type) != 2 ||
        type_size(mir.insns[2].type) != 2)
        return 0;
    count_function = mir_memory_runner_call_function(7, 1, 1);
    item_function = mir_memory_runner_call_function(30, 1, 1);
    done_function = mir_memory_runner_call_function(40, 1, 1);
    if (count_function == NULL ||
        item_function != count_function ||
        done_function != count_function ||
        !mir_machine_call_arguments(
            &mir.insns[7], 2, count_arguments) ||
        !mir_machine_call_arguments(
            &mir.insns[30], 3, item_arguments) ||
        !mir_machine_call_arguments(
            &mir.insns[40], 1, done_arguments) ||
        count_arguments[0] != mir.insns[3].dst ||
        count_arguments[1] != mir.insns[1].dst ||
        item_arguments[0] != mir.insns[21].dst ||
        item_arguments[1] != mir.insns[16].dst ||
        item_arguments[2] != mir.insns[28].dst ||
        done_arguments[0] != mir.insns[38].dst)
        return mir_machine_reject(
            "argv-print-schedule", "calls");
    if (!mir_machine_constant_equals(mir.insns[11].dst, 0) ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[16].src1 != mir.insns[11].dst ||
        mir.insns[16].src2 != mir.insns[34].dst ||
        mir.insns[16].phi_pred1 != mir.insns[0].label ||
        mir.insns[16].phi_pred2 != mir.insns[31].label ||
        mir.insns[19].src1 != mir.insns[16].dst ||
        mir.insns[19].src2 != mir.insns[1].dst ||
        mir.insns[19].immediate != '<' ||
        mir.insns[20].src1 != mir.insns[19].dst ||
        mir.insns[20].label != mir.insns[37].label ||
        mir.insns[27].src1 != mir.insns[2].dst ||
        mir.insns[27].src2 != mir.insns[16].dst ||
        mir.insns[27].immediate != 2 ||
        mir.insns[27].memory_size != 2 ||
        mir.insns[28].src1 != mir.insns[27].dst ||
        mir.insns[28].memory_size != 2 ||
        !mir_machine_constant_equals(mir.insns[33].dst, 1) ||
        mir.insns[34].src1 != mir.insns[16].dst ||
        mir.insns[34].src2 != mir.insns[33].dst ||
        mir.insns[34].immediate != '+' ||
        mir.insns[35].src1 != mir.insns[34].dst ||
        mir.insns[36].label != mir.insns[13].label ||
        !mir_machine_constant_equals(mir.insns[41].dst, 0) ||
        mir.insns[42].src1 != mir.insns[41].dst)
        return mir_machine_reject(
            "argv-print-schedule", "loop");
    plan->print_function = count_function;
    plan->count_string = (int)mir.insns[3].immediate;
    plan->item_string = (int)mir.insns[21].immediate;
    plan->done_string = (int)mir.insns[38].immediate;
    snprintf(plan->count_name, sizeof(plan->count_name), "%s",
             mir.insns[7].base_name);
    snprintf(plan->item_name, sizeof(plan->item_name), "%s",
             mir.insns[30].base_name);
    snprintf(plan->done_name, sizeof(plan->done_name), "%s",
             mir.insns[40].base_name);
    return plan->count_name[0] != 0 &&
           plan->item_name[0] != 0 &&
           plan->done_name[0] != 0;
}

static void mir_emit_argv_print_schedule(
    MirStream *out, const struct MirArgvPrintSchedule *plan)
{
    int loop = new_label();
    int done = new_label();

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n"
          ";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tpush de\n\tld hl,S%d\n\tpush hl\n",
            plan->argc_stack_offset + 2,
            plan->count_string);
    mir_call_recovery_emit_named_call(
        out, plan->print_function, plan->count_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_stream_puts("\tld iy,0\n", out);
    mir_stream_printf(out,
            "L%d:\n\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tex de,hl\n\tbit 7,h\n\tjp nz,L%d\n"
            "\tpush iy\n\tpop de\n\tor a\n\tsbc hl,de\n"
            "\tjp z,L%d\n\tjp m,L%d\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tpush iy\n\tpop hl\n\tadd hl,hl\n\tadd hl,de\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tpush de\n\tpush iy\n\tpop hl\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            loop, plan->argc_stack_offset + 2,
            done, done, done,
            plan->argv_stack_offset + 2,
            plan->item_string);
    mir_call_recovery_emit_named_call(
        out, plan->print_function, plan->item_name);
    mir_emit_final_call_cleanup(out, 3);
    mir_stream_printf(out,
            "\tinc iy\n\tjp L%d\nL%d:\n"
            "\tld hl,S%d\n\tpush hl\n",
            loop, done, plan->done_string);
    mir_call_recovery_emit_named_call(
        out, plan->print_function, plan->done_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_stream_puts("\tld hl,0\n\tpop iy\n\tret\n", out);
}

static int mir_match_list_prepend_schedule(
    struct MirListPrependSchedule *plan)
{
    static const unsigned char expected_opcodes[27] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_CONST,
        MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_UNARY, MIR_STORE, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_RETURN,
        MIR_LABEL, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_NOP,
        MIR_STORE_INDIRECT, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD,
        MIR_STORE_INDIRECT, MIR_LOAD, MIR_RETURN
    };
    int arguments[1];
    struct Sym *function;
    long allocation_size;

    memset(plan, 0, sizeof(*plan));
    if (!mir_call_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        mir_cfg_block_count() != 2 || mir.local_bytes != 2 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        mir_call_runner_has_volatile_memory() ||
        type_ptr_depth(mir.return_type) != 1 ||
        type_size(mir.return_type) != 2 ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->head_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &plan->value_stack_offset) ||
        plan->value_stack_offset != plan->head_stack_offset + 2 ||
        mir.insns[1].type != mir.return_type ||
        !mir_memory_runner_word_type(mir.insns[2].type, 0))
        return 0;
    function = mir_memory_runner_call_function(6, 0, 1);
    if (function == NULL ||
        !mir_machine_call_arguments(
            &mir.insns[6], 1, arguments) ||
        !mir_machine_evaluate_constant(
            arguments[0], &allocation_size, 0) ||
        allocation_size <= 0 || allocation_size > 255 ||
        type_ptr_depth(function->type) != 1 ||
        type_size(function->type) != 2)
        return mir_machine_reject(
            "list-prepend-schedule", "allocation");
    if (!mir_machine_same_location(
            &mir.insns[9], &mir.insns[10]) ||
        !mir_machine_same_location(
            &mir.insns[9], &mir.insns[17]) ||
        !mir_machine_same_location(
            &mir.insns[9], &mir.insns[21]) ||
        !mir_machine_same_location(
            &mir.insns[9], &mir.insns[25]) ||
        !mir_machine_constant_equals(mir.insns[11].dst, 0) ||
        mir.insns[12].src1 != mir.insns[10].dst ||
        mir.insns[12].src2 != mir.insns[11].dst ||
        mir.insns[12].immediate != TOK_EQ ||
        mir.insns[13].src1 != mir.insns[12].dst ||
        mir.insns[13].label != mir.insns[16].label ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        mir.insns[18].src1 != mir.insns[17].dst ||
        mir.insns[20].src1 != mir.insns[18].dst ||
        mir.insns[20].src2 != mir.insns[2].dst ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        mir.insns[24].src1 != mir.insns[22].dst ||
        mir.insns[24].src2 != mir.insns[23].dst ||
        mir.insns[26].src1 != mir.insns[25].dst)
        return mir_machine_reject(
            "list-prepend-schedule", "semantics");
    plan->allocate_function = function;
    plan->allocation_size = (int)allocation_size;
    plan->value_offset = (int)mir.insns[18].immediate;
    plan->next_offset = (int)mir.insns[22].immediate;
    return plan->value_offset >= 0 &&
           plan->next_offset >= 0 &&
           plan->value_offset + 1 < plan->allocation_size &&
           plan->next_offset + 1 < plan->allocation_size &&
           plan->value_offset != plan->next_offset;
}

static void mir_emit_list_prepend_schedule(
    MirStream *out, const struct MirListPrependSchedule *plan)
{
    int allocated = new_label();

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tpush hl\n",
            plan->allocation_size);
    mir_machine_emit_symbol_call(out, plan->allocate_function);
    mir_emit_final_call_cleanup(out, 1);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out,
            "\tjp nz,L%d\n\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tex de,hl\n\tret\nL%d:\n"
            "\tld c,l\n\tld b,h\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld l,c\n\tld h,b\n",
            allocated, plan->head_stack_offset,
            allocated, plan->value_stack_offset);
    mir_machine_emit_hl_offset(out, plan->value_offset, 0);
    mir_stream_puts("\tld (hl),e\n\tinc hl\n\tld (hl),d\n", out);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld l,c\n\tld h,b\n",
            plan->head_stack_offset);
    mir_machine_emit_hl_offset(out, plan->next_offset, 0);
    mir_stream_puts("\tld (hl),e\n\tinc hl\n\tld (hl),d\n"
          "\tld l,c\n\tld h,b\n\tret\n", out);
}

static int mir_match_list_reverse_schedule(
    struct MirListReverseSchedule *plan)
{
    static const unsigned char expected_opcodes[39] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST,
        MIR_STORE, MIR_LOAD, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_LABEL, MIR_LOAD, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_STORE, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD, MIR_STORE_INDIRECT, MIR_LOAD,
        MIR_NOP, MIR_STORE, MIR_LOAD, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_LOAD, MIR_RETURN
    };

    memset(plan, 0, sizeof(*plan));
    if (!mir_call_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        mir_cfg_block_count() != 4 || mir.local_bytes != 6 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        mir_call_runner_has_volatile_memory() ||
        mir_has_cfg_backedge() == 0 ||
        type_ptr_depth(mir.return_type) != 1 ||
        type_size(mir.return_type) != 2 ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->head_stack_offset) ||
        !mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        mir.insns[16].immediate != TOK_NE ||
        !mir_machine_constant_equals(mir.insns[15].dst, 0) ||
        mir.insns[17].label != mir.insns[36].label ||
        mir.insns[20].src1 != mir.insns[19].dst ||
        mir.insns[21].src1 != mir.insns[20].dst ||
        mir.insns[24].src1 != mir.insns[23].dst ||
        mir.insns[26].src1 != mir.insns[24].dst ||
        mir.insns[26].src2 != mir.insns[25].dst ||
        mir.insns[35].label != mir.insns[12].label ||
        mir.insns[38].src1 != mir.insns[37].dst)
        return 0;
    plan->next_offset = (int)mir.insns[20].immediate;
    return plan->next_offset >= 0 &&
           plan->next_offset <= 126 &&
           mir.insns[20].memory_size == 2 &&
           mir.insns[24].immediate == plan->next_offset &&
           mir.insns[24].memory_size == 2;
}

static void mir_emit_list_reverse_schedule(
    MirStream *out, const struct MirListReverseSchedule *plan)
{
    int loop = new_label();
    int done = new_label();

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tex de,hl\n\tld bc,0\nL%d:\n"
            "\tld a,h\n\tor l\n\tjp z,L%d\n"
            "\tpush hl\n",
            plan->head_stack_offset, loop, done);
    mir_machine_emit_hl_offset(out, plan->next_offset, 0);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tpop hl\n\tpush hl\n", out);
    mir_machine_emit_hl_offset(out, plan->next_offset, 0);
    mir_stream_puts("\tld (hl),c\n\tinc hl\n\tld (hl),b\n"
          "\tpop bc\n\tex de,hl\n", out);
    mir_stream_printf(out,
            "\tjp L%d\nL%d:\n\tld l,c\n\tld h,b\n\tret\n",
            loop, done);
}

static int mir_match_struct_return_member_schedule(
    struct MirStructReturnMemberSchedule *plan)
{
    static const int aggregate_calls[6] = {
        5, 49, 67, 73, 91, 100
    };
    static const int high_members[4] = {6, 50, 68, 92};
    static const int low_members[2] = {74, 101};
    static const int constants[][2] = {
        {1, 3}, {3, 4}, {12, 11}, {17, 22}, {22, 33},
        {24, 0}, {28, 0}, {35, 3}, {44, 1},
        {63, 1}, {65, 2}, {71, 7},
        {87, 6}, {89, 8}, {96, 1}, {98, 2},
        {119, 5}, {121, 9}, {151, 0}
    };
    int print_arguments[8];
    int member_arguments[2];
    struct Sym *pair_function = NULL;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 153 || mir.next_value != 107 ||
        mir_cfg_block_count() != 4 || mir.local_bytes != 105 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        mir_call_runner_has_volatile_memory() ||
        mir_has_cfg_backedge() == 0 ||
        !mir_memory_runner_word_type(mir.return_type, 0))
        return 0;
    for (item = 0;
         item < (int)(sizeof(constants) / sizeof(constants[0]));
         ++item)
        if (!mir_machine_constant_equals(
                mir.insns[constants[item][0]].dst,
                constants[item][1]))
            return mir_machine_reject(
                "struct-return-member-schedule", "constants");
    for (item = 0; item < 6; ++item) {
        const struct MirInsn *call =
            &mir.insns[aggregate_calls[item]];
        struct Sym *function;

        if (call->opcode != MIR_CALL_AGGREGATE ||
            call->memory_size != 4 ||
            (function = find_global(call->name)) == NULL ||
            function->storage != SC_FUNC ||
            function->is_funcptr || !function->has_proto ||
            function->proto_variadic ||
            function->proto_nargs != 2)
            return mir_machine_reject(
                "struct-return-member-schedule", "pair-calls");
        if (pair_function == NULL)
            pair_function = function;
        else if (pair_function != function)
            return mir_machine_reject(
                "struct-return-member-schedule", "mixed-pair-calls");
    }
    for (item = 0; item < 4; ++item) {
        const struct MirInsn *member =
            &mir.insns[high_members[item]];

        if (member->opcode != MIR_MEMBER_ADDRESS ||
            member->immediate != 2 ||
            member->memory_size != 2 ||
            mir.insns[high_members[item] + 1].opcode !=
                MIR_LOAD_INDIRECT ||
            mir.insns[high_members[item] + 1].src1 != member->dst ||
            mir.insns[high_members[item] + 1].memory_size != 2)
            return mir_machine_reject(
                "struct-return-member-schedule", "high-members");
    }
    for (item = 0; item < 2; ++item) {
        const struct MirInsn *member =
            &mir.insns[low_members[item]];

        if (member->opcode != MIR_MEMBER_ADDRESS ||
            member->immediate != 0 ||
            member->memory_size != 2 ||
            mir.insns[low_members[item] + 1].opcode !=
                MIR_LOAD_INDIRECT ||
            mir.insns[low_members[item] + 1].src1 != member->dst ||
            mir.insns[low_members[item] + 1].memory_size != 2)
            return mir_machine_reject(
                "struct-return-member-schedule", "low-members");
    }
    plan->member_function =
        mir_memory_runner_call_function(123, 0, 2);
    if (plan->member_function == NULL ||
        !mir_machine_call_arguments(
            &mir.insns[123], 2, member_arguments) ||
        member_arguments[0] != mir.insns[119].dst ||
        member_arguments[1] != mir.insns[121].dst)
        return mir_machine_reject(
            "struct-return-member-schedule", "member-call");
    plan->print_function =
        mir_memory_runner_call_function(150, 1, 1);
    if (plan->print_function == NULL ||
        !mir_machine_call_arguments(
            &mir.insns[150], 8, print_arguments) ||
        print_arguments[0] != mir.insns[114].dst ||
        mir.insns[114].opcode != MIR_STRING_ADDRESS)
        return mir_machine_reject(
            "struct-return-member-schedule", "print");
    plan->pair_function = pair_function;
    plan->pair_size = 4;
    plan->low_offset = 0;
    plan->high_offset = 2;
    plan->format_string = (int)mir.insns[114].immediate;
    snprintf(plan->print_name, sizeof(plan->print_name), "%s",
             mir.insns[150].base_name);
    return plan->print_name[0] != 0;
}

static void mir_struct_return_ix_address(MirStream *out, int offset)
{
    mir_stream_puts("\tpush ix\n\tpop hl\n", out);
    mir_machine_emit_hl_offset(out, offset, 0);
}

static void mir_struct_return_pair_call(
    MirStream *out, const struct MirStructReturnMemberSchedule *plan,
    int first, int second, int temp_offset)
{
    mir_stream_printf(out,
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,%d\n\tpush hl\n",
            second, first);
    mir_struct_return_ix_address(out, temp_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->pair_function);
    mir_emit_final_call_cleanup(out, 3);
}

static void mir_struct_return_load_member(
    MirStream *out, int temp_offset, int member_offset)
{
    mir_struct_return_ix_address(
        out, temp_offset + member_offset);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n", out);
}

static void mir_struct_return_store_hl(MirStream *out, int offset)
{
    mir_stream_printf(out,
            "\tld (ix%+d),l\n\tld (ix%+d),h\n",
            offset, offset + 1);
}

static void mir_struct_return_load_hl(MirStream *out, int offset)
{
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            offset, offset + 1);
}

static void mir_emit_struct_return_member_schedule(
    MirStream *out, const struct MirStructReturnMemberSchedule *plan)
{
    int item;

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-20\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    mir_struct_return_pair_call(out, plan, 3, 4, -4);
    mir_struct_return_load_member(out, -4, plan->high_offset);
    mir_struct_return_store_hl(out, -12);
    mir_stream_puts("\tld hl,11\n", out);
    mir_struct_return_store_hl(out, -10);
    mir_stream_puts("\tld hl,22\n", out);
    mir_struct_return_store_hl(out, -8);
    mir_stream_puts("\tld hl,33\n", out);
    mir_struct_return_store_hl(out, -6);
    mir_stream_puts("\tld hl,0\n", out);
    mir_struct_return_store_hl(out, -14);

    for (item = 0; item < 3; ++item) {
        mir_struct_return_pair_call(
            out, plan, item, item + 1, -4);
        mir_struct_return_load_member(
            out, -4, plan->high_offset);
        mir_stream_puts("\tex de,hl\n", out);
        mir_struct_return_load_hl(out, -14);
        mir_stream_puts("\tadd hl,de\n", out);
        mir_struct_return_store_hl(out, -14);
    }

    mir_struct_return_pair_call(out, plan, 1, 2, -4);
    mir_struct_return_load_member(out, -4, plan->high_offset);
    mir_struct_return_store_hl(out, -20);
    mir_stream_puts("\tld hl,7\n\tpush hl\n", out);
    mir_struct_return_load_hl(out, -20);
    mir_stream_puts("\tpush hl\n", out);
    mir_struct_return_ix_address(out, -4);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->pair_function);
    mir_emit_final_call_cleanup(out, 3);
    mir_struct_return_load_member(out, -4, plan->low_offset);
    mir_struct_return_store_hl(out, -16);

    mir_struct_return_pair_call(out, plan, 6, 8, -4);
    mir_struct_return_load_member(out, -4, plan->high_offset);
    mir_struct_return_store_hl(out, -18);
    mir_struct_return_pair_call(out, plan, 1, 2, -4);
    mir_struct_return_load_member(out, -4, plan->low_offset);
    mir_stream_puts("\tex de,hl\n", out);
    mir_struct_return_load_hl(out, -18);
    mir_stream_puts("\tadd hl,de\n", out);
    mir_struct_return_store_hl(out, -18);
    mir_stream_puts("\tex de,hl\n", out);
    mir_struct_return_load_hl(out, -16);
    mir_stream_puts("\tadd hl,de\n", out);
    mir_struct_return_store_hl(out, -16);

    mir_stream_puts("\tld hl,9\n\tpush hl\n\tld hl,5\n\tpush hl\n",
          out);
    mir_machine_emit_symbol_call(out, plan->member_function);
    mir_emit_final_call_cleanup(out, 2);
    mir_struct_return_store_hl(out, -20);

    mir_struct_return_load_hl(out, -6);
    mir_stream_puts("\tpush hl\n", out);
    mir_struct_return_load_hl(out, -8);
    mir_stream_puts("\tpush hl\n", out);
    mir_struct_return_load_hl(out, -10);
    mir_stream_puts("\tpush hl\n", out);
    mir_struct_return_load_hl(out, -16);
    mir_stream_puts("\tpush hl\n", out);
    mir_struct_return_load_hl(out, -14);
    mir_stream_puts("\tpush hl\n", out);
    mir_struct_return_load_hl(out, -20);
    mir_stream_puts("\tpush hl\n", out);
    mir_struct_return_load_hl(out, -12);
    mir_stream_printf(out, "\tpush hl\n\tld hl,S%d\n\tpush hl\n",
            plan->format_string);
    mir_call_recovery_emit_named_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 8);
    mir_stream_puts("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_pointer_table_runner_schedule(
    struct MirPointerTableRunnerSchedule *plan)
{
    static const int consume_calls[8] = {
        28, 43, 55, 77, 94, 118, 140, 176
    };
    static const int table_addresses[12] = {
        17, 34, 46, 65, 78, 85,
        102, 109, 131, 145, 160, 167
    };
    static const int constants[][2] = {
        {1, 1}, {4, 0}, {7, 0}, {14, 3},
        {25, 1}, {30, 1}, {35, 0}, {37, 0},
        {41, 2}, {47, 0}, {53, 1}, {60, 1},
        {68, 2}, {75, 1}, {79, 0}, {81, 0},
        {86, 0}, {88, 0}, {92, 1}, {96, 0},
        {103, 0}, {110, 0}, {116, 1}, {125, 0},
        {132, 0}, {138, 1}, {146, 0}, {154, 0},
        {161, 0}, {168, 0}, {174, 1}, {182, 1},
        {194, 2}, {199, 0}
    };
    int print_arguments[3];
    struct Sym *consume_function = NULL;
    struct Sym *table = NULL;
    long table_offset = 0;
    int memory_type;
    int memory_storage;
    int total_offset;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 201 || mir.next_value != 134 ||
        mir_cfg_block_count() != 17 || mir.local_bytes != 6 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        mir_call_runner_has_volatile_memory() ||
        mir_has_cfg_backedge() == 0 ||
        !mir_memory_runner_word_type(mir.return_type, 0))
        return 0;
    for (item = 0;
         item < (int)(sizeof(constants) / sizeof(constants[0]));
         ++item)
        if (!mir_machine_constant_equals(
                mir.insns[constants[item][0]].dst,
                constants[item][1]))
            return mir_machine_reject(
                "pointer-table-runner-schedule", "constants");
    for (item = 0; item < 8; ++item) {
        struct Sym *function =
            mir_memory_runner_call_function(
                consume_calls[item], 0, 2);

        if (function == NULL)
            return mir_machine_reject(
                "pointer-table-runner-schedule", "consume-call");
        if (consume_function == NULL)
            consume_function = function;
        else if (consume_function != function)
            return mir_machine_reject(
                "pointer-table-runner-schedule", "mixed-consume");
    }
    for (item = 0; item < 12; ++item) {
        struct Sym *root = NULL;
        long offset = 0;

        if (!mir_machine_global_address_offset(
                mir.insns[table_addresses[item]].dst,
                &root, &offset, 0) ||
            root == NULL || root->is_volatile ||
            root->pointee_is_volatile)
            return mir_machine_reject(
                "pointer-table-runner-schedule", "table-address");
        if (table == NULL) {
            table = root;
            table_offset = offset;
        } else if (table != root || table_offset != offset) {
            return mir_machine_reject(
                "pointer-table-runner-schedule", "mixed-table");
        }
    }
    if (!table->is_array || table->elem_size != 6 ||
        table->dim_count != 2 ||
        table->dims[0] != 2 || table->dims[1] != 3 ||
        !mir_scalar_memory_location(
            &mir.insns[6], &memory_type,
            &memory_storage, &total_offset) ||
        (memory_storage != SC_GLOBAL &&
         memory_storage != SC_EXTERN) ||
        !mir_machine_same_location(
            &mir.insns[6], &mir.insns[189]) ||
        (plan->total = find_global(mir.insns[6].name)) == NULL ||
        plan->total->is_volatile)
        return mir_machine_reject(
            "pointer-table-runner-schedule", "globals");
    plan->print_function =
        mir_memory_runner_call_function(198, 1, 1);
    if (plan->print_function == NULL ||
        !mir_machine_call_arguments(
            &mir.insns[198], 3, print_arguments) ||
        print_arguments[0] != mir.insns[187].dst ||
        print_arguments[1] != mir.insns[189].dst ||
        print_arguments[2] != mir.insns[196].dst ||
        mir.insns[187].opcode != MIR_STRING_ADDRESS)
        return mir_machine_reject(
            "pointer-table-runner-schedule", "print");
    plan->table = table;
    plan->consume_function = consume_function;
    plan->table_offset = (int)table_offset;
    plan->total_offset = total_offset;
    plan->format_string = (int)mir.insns[187].immediate;
    snprintf(plan->print_name, sizeof(plan->print_name), "%s",
             mir.insns[198].base_name);
    return plan->print_name[0] != 0;
}

static void mir_pointer_table_call(
    MirStream *out, const struct MirPointerTableRunnerSchedule *plan,
    int entry, int weight)
{
    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n", weight);
    mir_machine_emit_global_word(
        out, plan->table, plan->table_offset + entry * 2);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->consume_function);
    mir_emit_final_call_cleanup(out, 2);
}

static void mir_emit_pointer_table_runner_schedule(
    MirStream *out, const struct MirPointerTableRunnerSchedule *plan)
{
    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\tld hl,0\n", out);
    mir_machine_emit_global_word_store(
        out, plan->total, plan->total_offset);

    mir_pointer_table_call(out, plan, 3, 1);
    mir_pointer_table_call(out, plan, 0, 1);
    mir_pointer_table_call(out, plan, 4, 2);
    mir_pointer_table_call(out, plan, 0, 2);
    mir_pointer_table_call(out, plan, 5, 3);
    mir_pointer_table_call(out, plan, 2, 1);
    mir_pointer_table_call(out, plan, 5, 1);
    mir_pointer_table_call(out, plan, 0, 1);
    mir_pointer_table_call(out, plan, 0, 1);
    mir_pointer_table_call(out, plan, 0, 1);
    mir_pointer_table_call(out, plan, 0, 1);

    mir_machine_emit_global_word(
        out, plan->table, plan->table_offset + 10);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_global_word(
        out, plan->total, plan->total_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->format_string);
    mir_call_recovery_emit_named_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 3);
    mir_stream_puts("\tld hl,0\n\tret\n", out);
}

static int mir_match_address_check_runner_schedule(
    struct MirAddressCheckRunnerSchedule *plan)
{
    static const int check_calls[12] = {
        17, 25, 34, 44, 55, 93,
        110, 127, 137, 160, 172, 193
    };
    static const int expected_values[12] = {
        42, 42, 1, 7, 7, 30,
        99, 123, 456, 5, 77, 3
    };
    static const int constants[][2] = {
        {1, 42}, {15, 42}, {23, 42}, {32, 1}, {36, 7}, {42, 7},
        {53, 7}, {57, 0}, {59, 10}, {62, 1}, {64, 20}, {67, 2},
        {69, 30}, {72, 3}, {74, 40}, {80, 2}, {81, 2}, {91, 30},
        {95, 1}, {96, 2}, {99, 99}, {104, 1}, {108, 99}, {111, 123},
        {125, 123}, {129, 456}, {135, 456}, {140, 5}, {144, 6},
        {158, 5}, {162, 77}, {170, 77}, {174, 0}, {181, 3},
        {182, 2}, {187, 2}, {191, 3}, {196, 1}, {202, 0}
    };
    struct Sym *check_function = NULL;
    int print_arguments[1];
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 204 || mir.next_value != 140 ||
        mir_cfg_block_count() != 2 || mir.local_bytes != 20 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        mir_call_runner_has_volatile_memory() ||
        mir_has_cfg_backedge() ||
        !mir_memory_runner_word_type(mir.return_type, 0))
        return 0;
    for (item = 0;
         item < (int)(sizeof(constants) / sizeof(constants[0]));
         ++item)
        if (!mir_machine_constant_equals(
                mir.insns[constants[item][0]].dst,
                constants[item][1]))
            return mir_machine_reject(
                "address-check-runner-schedule", "constants");
    for (item = 0; item < 12; ++item) {
        const struct MirInsn *call =
            &mir.insns[check_calls[item]];
        int arguments[3];
        struct Sym *function =
            mir_memory_runner_call_function(
                check_calls[item], 0, 3);
        const struct MirInsn *string;
        long expected;

        if (function == NULL ||
            !mir_machine_call_arguments(call, 3, arguments) ||
            (string = mir_definition(arguments[0])) == NULL ||
            string->opcode != MIR_STRING_ADDRESS ||
            !mir_machine_evaluate_constant(
                arguments[2], &expected, 0) ||
            (expected & 0xffffL) != expected_values[item] ||
            !mir_memory_runner_word_type(
                function->proto_types[1], 0) ||
            !mir_memory_runner_word_type(
                function->proto_types[2], 0))
            return mir_machine_reject(
                "address-check-runner-schedule", "checks");
        if (check_function == NULL)
            check_function = function;
        else if (check_function != function)
            return mir_machine_reject(
                "address-check-runner-schedule", "mixed-checks");
        plan->strings[item] = (int)string->immediate;
        plan->values[item] = expected_values[item];
    }
    if (mir.insns[194].opcode != MIR_LOAD ||
        (plan->failure_count =
             find_global(mir.insns[194].name)) == NULL ||
        plan->failure_count->is_volatile ||
        mir.insns[195].opcode != MIR_BRANCH_FALSE ||
        mir.insns[195].src1 != mir.insns[194].dst ||
        mir.insns[195].label != mir.insns[198].label ||
        !mir_machine_constant_equals(mir.insns[196].dst, 1) ||
        mir.insns[197].src1 != mir.insns[196].dst ||
        mir.insns[199].opcode != MIR_STRING_ADDRESS ||
        !mir_machine_constant_equals(mir.insns[202].dst, 0) ||
        mir.insns[203].src1 != mir.insns[202].dst)
        return mir_machine_reject(
            "address-check-runner-schedule", "tail");
    plan->print_function =
        mir_memory_runner_call_function(201, 1, 1);
    if (plan->print_function == NULL ||
        !mir_machine_call_arguments(
            &mir.insns[201], 1, print_arguments) ||
        print_arguments[0] != mir.insns[199].dst)
        return mir_machine_reject(
            "address-check-runner-schedule", "print");
    plan->check_function = check_function;
    plan->success_string = (int)mir.insns[199].immediate;
    snprintf(plan->print_name, sizeof(plan->print_name), "%s",
             mir.insns[201].base_name);
    return plan->print_name[0] != 0;
}

static void mir_emit_address_check_runner_schedule(
    MirStream *out, const struct MirAddressCheckRunnerSchedule *plan)
{
    int success = new_label();
    int item;

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (item = 0; item < 12; ++item) {
        mir_stream_printf(out,
                "\tld hl,%d\n\tpush hl\n\tpush hl\n"
                "\tld hl,S%d\n\tpush hl\n",
                plan->values[item], plan->strings[item]);
        mir_machine_emit_symbol_call(out, plan->check_function);
        mir_emit_final_call_cleanup(out, 3);
    }
    mir_machine_emit_global_word(out, plan->failure_count, 0);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out,
            "\tjp z,L%d\n\tld hl,1\n\tret\n"
            "L%d:\n\tld hl,S%d\n\tpush hl\n",
            success, success, plan->success_string);
    mir_call_recovery_emit_named_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_stream_puts("\tld hl,0\n\tret\n", out);
}

static int mir_match_qualifier_runner_schedule(
    struct MirQualifierRunnerSchedule *plan)
{
    static const int check_calls[7] = {
        36, 43, 54, 65, 76, 84, 97
    };
    static const int expected_values[7] = {
        7, 5, 98, 120, 120, 5, 12
    };
    struct Sym *check_function = NULL;
    int failure_arguments[2];
    int success_arguments[1];
    int add_arguments[2];
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 114 || mir.next_value != 73 ||
        mir_cfg_block_count() != 2 || mir.local_bytes != 16 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        mir_has_cfg_backedge() ||
        !mir_memory_runner_word_type(mir.return_type, 0))
        return 0;
    for (item = 0; item < 7; ++item) {
        const struct MirInsn *call =
            &mir.insns[check_calls[item]];
        int arguments[3];
        const struct MirInsn *string;
        struct Sym *function =
            mir_memory_runner_call_function(
                check_calls[item], 0, 3);
        long expected;

        if (function == NULL ||
            !mir_machine_call_arguments(call, 3, arguments) ||
            (string = mir_definition(arguments[0])) == NULL ||
            string->opcode != MIR_STRING_ADDRESS ||
            !mir_machine_evaluate_constant(
                arguments[2], &expected, 0) ||
            (expected & 0xffffL) != expected_values[item])
            return mir_machine_reject(
                "qualifier-runner-schedule", "checks");
        if (check_function == NULL)
            check_function = function;
        else if (check_function != function)
            return mir_machine_reject(
                "qualifier-runner-schedule", "mixed-checks");
        plan->check_strings[item] = (int)string->immediate;
    }
    plan->add_function =
        mir_memory_runner_call_function(93, 0, 2);
    if (plan->add_function == NULL ||
        !mir_machine_call_arguments(
            &mir.insns[93], 2, add_arguments) ||
        type_ptr_depth(plan->add_function->proto_types[0]) != 1 ||
        type_ptr_depth(plan->add_function->proto_types[1]) != 1 ||
        !mir_memory_runner_word_type(plan->add_function->type, 0))
        return mir_machine_reject(
            "qualifier-runner-schedule", "add-call");
    if (!mir_machine_constant_equals(mir.insns[1].dst, 7) ||
        !mir_machine_constant_equals(mir.insns[3].dst, 5) ||
        !mir_machine_constant_equals(mir.insns[16].dst, 120) ||
        !mir_machine_constant_equals(mir.insns[13].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[22].dst, 0) ||
        mir.insns[39].opcode != MIR_LOAD ||
        mir.insns[80].opcode != MIR_LOAD_INDIRECT)
        return mir_machine_reject(
            "qualifier-runner-schedule", "qualifiers");
    if (mir.insns[98].opcode != MIR_LOAD ||
        (plan->failure_count =
             find_global(mir.insns[98].name)) == NULL ||
        plan->failure_count->is_volatile ||
        mir.insns[99].opcode != MIR_BRANCH_FALSE ||
        mir.insns[99].src1 != mir.insns[98].dst ||
        mir.insns[99].label != mir.insns[108].label ||
        !mir_machine_constant_equals(mir.insns[105].dst, 1) ||
        mir.insns[106].src1 != mir.insns[105].dst ||
        !mir_machine_constant_equals(mir.insns[112].dst, 0) ||
        mir.insns[113].src1 != mir.insns[112].dst)
        return mir_machine_reject(
            "qualifier-runner-schedule", "tail");
    plan->print_function =
        mir_memory_runner_call_function(104, 1, 1);
    if (plan->print_function == NULL ||
        mir_memory_runner_call_function(111, 1, 1) !=
            plan->print_function ||
        !mir_machine_call_arguments(
            &mir.insns[104], 2, failure_arguments) ||
        !mir_machine_call_arguments(
            &mir.insns[111], 1, success_arguments))
        return mir_machine_reject(
            "qualifier-runner-schedule", "prints");
    plan->check_function = check_function;
    plan->failure_string =
        (int)mir_definition(failure_arguments[0])->immediate;
    plan->success_string =
        (int)mir_definition(success_arguments[0])->immediate;
    snprintf(plan->failure_name, sizeof(plan->failure_name), "%s",
             mir.insns[104].base_name);
    snprintf(plan->success_name, sizeof(plan->success_name), "%s",
             mir.insns[111].base_name);
    return plan->failure_name[0] != 0 &&
           plan->success_name[0] != 0;
}

static void mir_qualifier_store_word(
    MirStream *out, int offset, int value)
{
    mir_stream_printf(out,
            "\tld hl,%d\n\tld (ix%+d),l\n\tld (ix%+d),h\n",
            value, offset, offset + 1);
}

static void mir_qualifier_push_word(MirStream *out, int offset)
{
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n",
            offset, offset + 1);
}

static void mir_qualifier_check_constant(
    MirStream *out, const struct MirQualifierRunnerSchedule *plan,
    int check, int actual)
{
    mir_stream_printf(out,
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            actual, actual, plan->check_strings[check]);
    mir_machine_emit_symbol_call(out, plan->check_function);
    mir_emit_final_call_cleanup(out, 3);
}

static void mir_qualifier_check_local(
    MirStream *out, const struct MirQualifierRunnerSchedule *plan,
    int check, int offset, int expected)
{
    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n", expected);
    mir_qualifier_push_word(out, offset);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->check_strings[check]);
    mir_machine_emit_symbol_call(out, plan->check_function);
    mir_emit_final_call_cleanup(out, 3);
}

static void mir_emit_qualifier_runner_schedule(
    MirStream *out, const struct MirQualifierRunnerSchedule *plan)
{
    int success = new_label();

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-8\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\tld hl,0\n", out);
    mir_machine_emit_global_word_store(
        out, plan->failure_count, 0);
    mir_qualifier_store_word(out, -2, 7);
    mir_qualifier_store_word(out, -4, 5);
    mir_stream_puts("\tld (ix-8),120\n\tld (ix-7),0\n", out);

    mir_qualifier_check_local(out, plan, 0, -2, 7);
    mir_qualifier_check_local(out, plan, 1, -4, 5);
    mir_qualifier_check_constant(out, plan, 2, 98);
    mir_stream_printf(out, "\tld l,(ix-8)\n\tld h,0\n\tpush hl\n"
            "\tld hl,120\n\tpush hl\n\tld hl,S%d\n\tpush hl\n",
            plan->check_strings[3]);
    mir_machine_emit_symbol_call(out, plan->check_function);
    mir_emit_final_call_cleanup(out, 3);
    mir_stream_printf(out, "\tld l,(ix-8)\n\tld h,0\n\tpush hl\n"
            "\tld hl,120\n\tpush hl\n\tld hl,S%d\n\tpush hl\n",
            plan->check_strings[4]);
    mir_machine_emit_symbol_call(out, plan->check_function);
    mir_emit_final_call_cleanup(out, 3);
    mir_qualifier_check_local(out, plan, 5, -4, 5);

    mir_stream_puts("\tld hl,12\n\tpush hl\n", out);
    mir_struct_return_ix_address(out, -2);
    mir_stream_puts("\tpush hl\n", out);
    mir_struct_return_ix_address(out, -4);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->add_function);
    mir_emit_final_call_cleanup(out, 2);
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->check_strings[6]);
    mir_machine_emit_symbol_call(out, plan->check_function);
    mir_emit_final_call_cleanup(out, 3);

    mir_machine_emit_global_word(out, plan->failure_count, 0);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out,
            "\tjp z,L%d\n\tpush hl\n\tld hl,S%d\n\tpush hl\n",
            success, plan->failure_string);
    mir_call_recovery_emit_named_call(
        out, plan->print_function, plan->failure_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_stream_puts("\tld hl,1\n\tld sp,ix\n\tpop ix\n\tret\n", out);
    mir_stream_printf(out, "L%d:\n\tld hl,S%d\n\tpush hl\n",
            success, plan->success_string);
    mir_call_recovery_emit_named_call(
        out, plan->print_function, plan->success_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_stream_puts("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_union_value_global(
    int instruction, struct Sym **root_out)
{
    long offset;

    return mir_machine_global_address_offset(
               mir.insns[instruction].dst,
               root_out, &offset, 0) &&
           *root_out != NULL && offset == 0 &&
           !(*root_out)->is_volatile &&
           !(*root_out)->pointee_is_volatile;
}

static int mir_match_union_value_runner_schedule(
    struct MirUnionValueRunnerSchedule *plan)
{
    static const int print_calls[8] = {
        46, 71, 86, 108, 133, 170, 200, 203
    };
    static const int string_instructions[8] = {
        25, 47, 72, 87, 109, 149, 179, 201
    };
    static const int sum_calls[6] = {
        44, 78, 84, 106, 168, 198
    };
    static const int constants[][2] = {
        {3, 6}, {5, 4000}, {7, 10},
        {17, 120}, {19, 121}, {21, 122},
        {51, 0}, {57, 1}, {63, 2},
        {75, 0}, {81, 1},
        {113, 0}, {119, 1}, {125, 2},
        {135, 7}, {137, 5000}, {141, 11},
        {175, 1}, {204, 0}
    };
    struct Sym *root;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 206 || mir.next_value != 146 ||
        mir_cfg_block_count() != 1 || mir.local_bytes != 28 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        mir_call_runner_has_volatile_memory() ||
        mir_has_cfg_backedge() ||
        !mir_memory_runner_word_type(mir.return_type, 0))
        return 0;
    for (item = 0;
         item < (int)(sizeof(constants) / sizeof(constants[0]));
         ++item)
        if (!mir_machine_constant_equals(
                mir.insns[constants[item][0]].dst,
                constants[item][1]))
            return mir_machine_reject(
                "union-value-runner-schedule", "constants");
    if (!mir_union_value_global(27, &plan->global_union) ||
        !mir_union_value_global(49, &plan->global_name) ||
        !mir_union_value_global(74, &plan->global_array) ||
        plan->global_union == plan->global_name ||
        plan->global_union == plan->global_array ||
        plan->global_name == plan->global_array ||
        type_size(plan->global_union->type) != 8 ||
        type_size(plan->global_name->type) != 4 ||
        !plan->global_array->is_array ||
        plan->global_array->elem_size != 8)
        return mir_machine_reject(
            "union-value-runner-schedule", "globals");
    for (item = 0; item < 6; ++item) {
        struct Sym *function =
            mir_memory_runner_call_function(
                sum_calls[item], 0, 1);

        if (function == NULL ||
            type_size(function->proto_types[0]) != 8 ||
            type_size(function->type) != 4)
            return mir_machine_reject(
                "union-value-runner-schedule", "sum-calls");
        if (item == 0)
            plan->sum_function = function;
        else if (plan->sum_function != function)
            return mir_machine_reject(
                "union-value-runner-schedule", "mixed-sum");
    }
    if (mir.insns[143].opcode != MIR_CALL_AGGREGATE ||
        mir.insns[143].memory_size != 8 ||
        (plan->make_function =
             find_global(mir.insns[143].name)) == NULL ||
        plan->make_function->storage != SC_FUNC ||
        plan->make_function->is_funcptr ||
        !plan->make_function->has_proto ||
        plan->make_function->proto_nargs != 3 ||
        mir_memory_runner_call_function(178, 0, 2) == NULL ||
        (plan->copy_function =
             find_global(mir.insns[178].name)) == NULL)
        return mir_machine_reject(
            "union-value-runner-schedule", "aggregate-calls");
    for (item = 0; item < 8; ++item) {
        const struct MirInsn *call =
            &mir.insns[print_calls[item]];
        struct Sym *function = find_global(call->name);

        if (call->opcode != MIR_CALL || call->src1 >= 0 ||
            (call->memory_flags & MIR_CALL_FLAG_VARIADIC) == 0 ||
            function == NULL || function->storage != SC_FUNC ||
            function->is_funcptr || !function->has_proto ||
            !function->proto_variadic ||
            function->proto_nargs != 1 ||
            mir.insns[string_instructions[item]].opcode !=
                MIR_STRING_ADDRESS)
            return mir_machine_reject(
                "union-value-runner-schedule", "prints");
        if (item == 0)
            plan->print_function = function;
        else if (plan->print_function != function)
            return mir_machine_reject(
                "union-value-runner-schedule", "mixed-prints");
        plan->strings[item] =
            (int)mir.insns[string_instructions[item]].immediate;
        snprintf(plan->print_names[item],
                 sizeof(plan->print_names[item]), "%s",
                 mir.insns[print_calls[item]].base_name);
        if (plan->print_names[item][0] == 0)
            return 0;
    }
    if (mir.insns[29].immediate != 0 ||
        mir.insns[34].immediate != 1 ||
        mir.insns[39].immediate != 3 ||
        mir.insns[153].immediate != 0 ||
        mir.insns[158].immediate != 1 ||
        mir.insns[163].immediate != 3 ||
        mir.insns[183].immediate != 0 ||
        mir.insns[188].immediate != 1 ||
        mir.insns[193].immediate != 3)
        return mir_machine_reject(
            "union-value-runner-schedule", "member-layout");
    root = NULL;
    return mir_union_value_global(32, &root) &&
           root == plan->global_union &&
           mir_union_value_global(37, &root) &&
           root == plan->global_union &&
           mir_union_value_global(42, &root) &&
           root == plan->global_union &&
           mir_union_value_global(174, &root) &&
           root == plan->global_array;
}

static void mir_union_value_local_address(MirStream *out, int offset)
{
    mir_struct_return_ix_address(out, offset);
}

static void mir_union_value_push_aggregate_hl(MirStream *out, int size)
{
    mir_stream_puts("\tex de,hl\n", out);
    mir_stream_printf(out,
            "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n"
            "\tex de,hl\n\tld bc,%d\n\tldir\n",
            size, size);
}

static void mir_union_value_call_sum_global(
    MirStream *out, const struct MirUnionValueRunnerSchedule *plan,
    struct Sym *root, int offset, int result_offset)
{
    mir_machine_emit_global_address_de(out, root, offset);
    mir_stream_puts("\tex de,hl\n", out);
    mir_union_value_push_aggregate_hl(out, 8);
    mir_machine_emit_symbol_call(out, plan->sum_function);
    mir_emit_final_call_cleanup(out, 4);
    mir_machine_emit_ix_wide_store(out, result_offset);
}

static void mir_union_value_call_sum_local(
    MirStream *out, const struct MirUnionValueRunnerSchedule *plan,
    int source_offset, int result_offset)
{
    mir_union_value_local_address(out, source_offset);
    mir_union_value_push_aggregate_hl(out, 8);
    mir_machine_emit_symbol_call(out, plan->sum_function);
    mir_emit_final_call_cleanup(out, 4);
    mir_machine_emit_ix_wide_store(out, result_offset);
}

static void mir_union_value_push_global_byte(
    MirStream *out, struct Sym *root, int offset)
{
    mir_machine_emit_global_address_de(out, root, offset);
    mir_stream_puts("\tex de,hl\n\tld l,(hl)\n\tld h,0\n\tpush hl\n",
          out);
}

static void mir_union_value_push_global_word(
    MirStream *out, struct Sym *root, int offset)
{
    mir_machine_emit_global_word(out, root, offset);
    mir_stream_puts("\tpush hl\n", out);
}

static void mir_union_value_push_local_byte(
    MirStream *out, int offset)
{
    mir_stream_printf(out, "\tld l,(ix%+d)\n\tld h,0\n\tpush hl\n",
            offset);
}

static void mir_union_value_push_local_word(
    MirStream *out, int offset)
{
    mir_struct_return_load_hl(out, offset);
    mir_stream_puts("\tpush hl\n", out);
}

static void mir_union_value_print(
    MirStream *out, const struct MirUnionValueRunnerSchedule *plan,
    int print_index, int argument_words)
{
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[print_index]);
    mir_call_recovery_emit_named_call(
        out, plan->print_function,
        plan->print_names[print_index]);
    mir_emit_final_call_cleanup(
        out, argument_words + 1);
}

static void mir_emit_union_value_runner_schedule(
    MirStream *out, const struct MirUnionValueRunnerSchedule *plan)
{
    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-36\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    mir_stream_puts("\tld (ix-8),6\n\tld hl,4000\n"
          "\tld (ix-7),l\n\tld (ix-6),h\n"
          "\tld (ix-5),10\n\txor a\n"
          "\tld (ix-4),a\n\tld (ix-3),a\n"
          "\tld (ix-2),a\n\tld (ix-1),a\n"
          "\tld (ix-12),120\n\tld (ix-11),121\n"
          "\tld (ix-10),122\n\tld (ix-9),0\n", out);

    mir_union_value_call_sum_global(
        out, plan, plan->global_union, 0, -32);
    mir_machine_emit_ix_wide_load(out, -32);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_union_value_push_global_byte(
        out, plan->global_union, 3);
    mir_union_value_push_global_word(
        out, plan->global_union, 1);
    mir_union_value_push_global_byte(
        out, plan->global_union, 0);
    mir_union_value_print(out, plan, 0, 5);

    mir_machine_emit_global_word(
        out, plan->global_name, 2);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_global_word(
        out, plan->global_name, 0);
    mir_stream_puts("\tpush hl\n", out);
    mir_union_value_push_global_byte(
        out, plan->global_name, 2);
    mir_union_value_push_global_byte(
        out, plan->global_name, 1);
    mir_union_value_push_global_byte(
        out, plan->global_name, 0);
    mir_union_value_print(out, plan, 1, 5);

    mir_union_value_call_sum_global(
        out, plan, plan->global_array, 0, -32);
    mir_union_value_call_sum_global(
        out, plan, plan->global_array, 8, -36);
    mir_machine_emit_ix_wide_load(out, -36);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, -32);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_union_value_print(out, plan, 2, 4);

    mir_union_value_call_sum_local(out, plan, -8, -32);
    mir_machine_emit_ix_wide_load(out, -32);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_union_value_push_local_byte(out, -5);
    mir_union_value_push_local_word(out, -7);
    mir_union_value_push_local_byte(out, -8);
    mir_union_value_print(out, plan, 3, 5);

    mir_union_value_push_local_word(out, -10);
    mir_union_value_push_local_word(out, -12);
    mir_union_value_push_local_byte(out, -10);
    mir_union_value_push_local_byte(out, -11);
    mir_union_value_push_local_byte(out, -12);
    mir_union_value_print(out, plan, 4, 5);

    mir_stream_puts("\tld hl,11\n\tpush hl\n\tld hl,5000\n\tpush hl\n"
          "\tld hl,7\n\tpush hl\n", out);
    mir_union_value_local_address(out, -20);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->make_function);
    mir_emit_final_call_cleanup(out, 4);
    mir_union_value_local_address(out, -20);
    mir_stream_puts("\tpush hl\n", out);
    mir_union_value_local_address(out, -28);
    mir_stream_puts("\tex de,hl\n\tpop hl\n\tld bc,8\n\tldir\n", out);
    mir_union_value_call_sum_local(out, plan, -28, -32);
    mir_machine_emit_ix_wide_load(out, -32);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_union_value_push_local_byte(out, -25);
    mir_union_value_push_local_word(out, -27);
    mir_union_value_push_local_byte(out, -28);
    mir_union_value_print(out, plan, 5, 5);

    mir_machine_emit_global_address_de(
        out, plan->global_array, 8);
    mir_stream_puts("\tex de,hl\n\tpush hl\n", out);
    mir_union_value_local_address(out, -28);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->copy_function);
    mir_emit_final_call_cleanup(out, 2);
    mir_union_value_call_sum_local(out, plan, -28, -32);
    mir_machine_emit_ix_wide_load(out, -32);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_union_value_push_local_byte(out, -25);
    mir_union_value_push_local_word(out, -27);
    mir_union_value_push_local_byte(out, -28);
    mir_union_value_print(out, plan, 6, 5);

    mir_union_value_print(out, plan, 7, 0);
    mir_stream_puts("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_struct_value_print_string(
    const struct MirInsn *call, int *string_id)
{
    int found = 0;
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *argument = &mir.insns[instruction];
        const struct MirInsn *definition;

        if (argument->opcode != MIR_ARG ||
            argument->secondary_offset != call->secondary_offset)
            continue;
        definition = mir_definition(argument->src1);
        if (definition != NULL &&
            definition->opcode == MIR_STRING_ADDRESS) {
            *string_id = (int)definition->immediate;
            ++found;
        }
    }
    return found == 1;
}

static int mir_struct_value_global(
    int instruction, struct Sym **root_out)
{
    long offset;

    return mir_machine_global_address_offset(
               mir.insns[instruction].dst,
               root_out, &offset, 0) &&
           *root_out != NULL && offset == 0 &&
           !(*root_out)->is_volatile &&
           !(*root_out)->pointee_is_volatile;
}

static int mir_match_struct_value_runner_schedule(
    struct MirStructValueRunnerSchedule *plan)
{
    static const int print_calls[17] = {
        53, 86, 105, 158, 206, 228, 271, 328, 372,
        415, 432, 447, 479, 534, 573, 617, 620
    };
    static const int aggregate_calls[][3] = {
        {11, 4, 3}, {62, 4, 3}, {117, 4, 3},
        {168, 7, 3}, {217, 4, 3}, {240, 4, 3},
        {277, 42, 2}, {337, 42, 2}, {381, 4, 3},
        {392, 4, 3}, {423, 7, 3}, {438, 42, 2},
        {458, 4, 1}, {491, 4, 3}, {512, 4, 2},
        {545, 7, 1}, {586, 42, 1}, {595, 42, 1}
    };
    static const int scalar_calls[][2] = {
        {34, 1}, {67, 1}, {103, 1}, {133, 2},
        {176, 1}, {221, 3}, {246, 1}, {285, 1},
        {343, 1}, {399, 1}, {405, 2}, {413, 3},
        {430, 1}, {445, 1}, {454, 2}, {477, 1},
        {501, 2}, {532, 1}, {541, 2},
        {582, 3}, {615, 1}
    };
    static const int constants[][2] = {
        {4, 8}, {6, 600}, {9, 14}, {15, 3}, {19, 1000}, {24, 7},
        {55, 4}, {57, 2000}, {60, 8}, {107, 0}, {110, 5},
        {112, 3000}, {115, 9}, {120, 1}, {126, 0}, {130, 1},
        {139, 0}, {145, 0}, {151, 0}, {160, 0}, {163, 11},
        {166, 12}, {207, 100}, {210, 6}, {212, 4000}, {215, 10},
        {219, 20}, {230, 1}, {233, 7}, {235, 5000}, {238, 13},
        {243, 1}, {252, 1}, {258, 1}, {264, 1}, {273, 1},
        {275, 6000}, {292, 0}, {298, 1}, {304, 2}, {310, 10},
        {316, 20}, {322, 39}, {330, 1}, {333, 2}, {335, 7000},
        {340, 1}, {349, 1}, {352, 0}, {357, 1}, {360, 39},
        {365, 1}, {374, 3}, {376, 1000}, {379, 7}, {385, 4},
        {387, 2000}, {390, 8}, {407, 10}, {411, 20}, {418, 11},
        {421, 12}, {434, 1}, {436, 6000}, {481, 0}, {484, 5},
        {486, 3000}, {489, 9}, {494, 1}, {498, 0}, {503, 2},
        {509, 1}, {529, 2}, {578, 2}, {580, 7000}, {590, 1},
        {601, 0}, {607, 39}, {612, 1}, {621, 0}
    };
    struct Sym *print_function = NULL;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 623 || mir.next_value != 439 ||
        mir_cfg_block_count() != 1 || mir.local_bytes != 256 ||
        mir.aggregate_temp_bytes != 4 || mir.has_vla ||
        mir_call_runner_has_volatile_memory() ||
        mir_has_cfg_backedge() ||
        !mir_memory_runner_word_type(mir.return_type, 0))
        return 0;
    for (item = 0;
         item < (int)(sizeof(constants) / sizeof(constants[0]));
         ++item)
        if (!mir_machine_constant_equals(
                mir.insns[constants[item][0]].dst,
                constants[item][1]))
            return mir_machine_reject(
                "struct-value-runner-schedule", "constants");
    for (item = 0; item < 18; ++item) {
        const struct MirInsn *call =
            &mir.insns[aggregate_calls[item][0]];
        struct Sym *function = find_global(call->name);

        if (call->opcode != MIR_CALL_AGGREGATE ||
            call->memory_size != aggregate_calls[item][1] ||
            function == NULL || function->storage != SC_FUNC ||
            function->is_funcptr || !function->has_proto ||
            function->proto_variadic ||
            function->proto_nargs != aggregate_calls[item][2])
            return mir_machine_reject(
                "struct-value-runner-schedule", "aggregate-calls");
    }
    for (item = 0; item < 21; ++item) {
        const struct MirInsn *call =
            &mir.insns[scalar_calls[item][0]];
        struct Sym *function = find_global(call->name);

        if (call->opcode != MIR_CALL || call->src1 >= 0 ||
            function == NULL || function->storage != SC_FUNC ||
            function->is_funcptr || !function->has_proto ||
            function->proto_variadic ||
            function->proto_nargs != scalar_calls[item][1])
            return mir_machine_reject(
                "struct-value-runner-schedule", "scalar-calls");
    }
    for (item = 0; item < 17; ++item) {
        const struct MirInsn *call =
            &mir.insns[print_calls[item]];
        struct Sym *function = find_global(call->name);

        if (call->opcode != MIR_CALL || call->src1 >= 0 ||
            (call->memory_flags & MIR_CALL_FLAG_VARIADIC) == 0 ||
            function == NULL || !function->has_proto ||
            !function->proto_variadic ||
            !mir_struct_value_print_string(
                call, &plan->strings[item]))
            return mir_machine_reject(
                "struct-value-runner-schedule", "prints");
        if (print_function == NULL)
            print_function = function;
        else if (print_function != function)
            return mir_machine_reject(
                "struct-value-runner-schedule", "mixed-prints");
        snprintf(plan->print_names[item],
                 sizeof(plan->print_names[item]), "%s",
                 call->base_name[0] != 0
                     ? call->base_name
                     : asm_name_for(sym_asm_name(function)));
    }
    plan->make_pair = find_global(mir.insns[11].name);
    plan->make_wrap = find_global(mir.insns[168].name);
    plan->make_big = find_global(mir.insns[277].name);
    plan->sum_pair = find_global(mir.insns[34].name);
    plan->sum_two = find_global(mir.insns[133].name);
    plan->sum_mix = find_global(mir.insns[221].name);
    plan->sum_wrap = find_global(mir.insns[176].name);
    plan->sum_big = find_global(mir.insns[285].name);
    plan->copy_pair = find_global(mir.insns[454].name);
    plan->return_pair = find_global(mir.insns[458].name);
    plan->assign_return_pair = find_global(mir.insns[512].name);
    plan->copy_wrap = find_global(mir.insns[541].name);
    plan->return_wrap = find_global(mir.insns[545].name);
    plan->fill_big = find_global(mir.insns[582].name);
    plan->return_big = find_global(mir.insns[586].name);
    plan->print_function = print_function;
    if (plan->make_pair == NULL || plan->make_wrap == NULL ||
        plan->make_big == NULL || plan->sum_pair == NULL ||
        plan->sum_two == NULL || plan->sum_mix == NULL ||
        plan->sum_wrap == NULL || plan->sum_big == NULL ||
        plan->copy_pair == NULL || plan->return_pair == NULL ||
        plan->assign_return_pair == NULL ||
        plan->copy_wrap == NULL || plan->return_wrap == NULL ||
        plan->fill_big == NULL || plan->return_big == NULL)
        return 0;
    if (!mir_struct_value_global(32, &plan->global_pair) ||
        !mir_struct_value_global(
            480, &plan->global_pair_array) ||
        !mir_struct_value_global(174, &plan->global_wrap) ||
        !mir_struct_value_global(283, &plan->global_big) ||
        !mir_struct_value_global(
            589, &plan->global_big_array))
        return mir_machine_reject(
            "struct-value-runner-schedule", "globals");
    return plan->global_pair != plan->global_pair_array &&
           plan->global_pair != plan->global_wrap &&
           plan->global_pair != plan->global_big &&
           plan->global_pair_array != plan->global_big_array &&
           plan->global_big != plan->global_big_array;
}

static void mir_struct_value_cleanup(MirStream *out, int bytes)
{
    if (bytes <= 0)
        return;
    mir_stream_printf(out,
            "\tex de,hl\n\tld hl,%d\n\tadd hl,sp\n"
            "\tld sp,hl\n\tex de,hl\n",
            bytes);
}

static void mir_struct_value_local_address(MirStream *out, int offset)
{
    mir_struct_return_ix_address(out, offset);
}

static void mir_struct_value_global_address(
    MirStream *out, struct Sym *root, int offset)
{
    mir_machine_emit_global_address_de(out, root, offset);
    mir_stream_puts("\tex de,hl\n", out);
}

static void mir_struct_value_copy(
    MirStream *out, int source_local, struct Sym *source_global,
    int source_offset, int destination_local,
    struct Sym *destination_global, int destination_offset,
    int size)
{
    if (source_global != NULL)
        mir_struct_value_global_address(
            out, source_global, source_offset);
    else
        mir_struct_value_local_address(out, source_local);
    mir_stream_puts("\tpush hl\n", out);
    if (destination_global != NULL)
        mir_struct_value_global_address(
            out, destination_global, destination_offset);
    else
        mir_struct_value_local_address(out, destination_local);
    mir_stream_puts("\tex de,hl\n\tpop hl\n", out);
    mir_stream_printf(out, "\tld bc,%d\n\tldir\n", size);
}

static void mir_struct_value_push_aggregate(
    MirStream *out, int source_local, struct Sym *source_global,
    int source_offset, int size)
{
    if (source_global != NULL)
        mir_struct_value_global_address(
            out, source_global, source_offset);
    else
        mir_struct_value_local_address(out, source_local);
    mir_stream_puts("\tex de,hl\n", out);
    mir_stream_printf(out,
            "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n"
            "\tex de,hl\n\tld bc,%d\n\tldir\n",
            size, size);
}

static void mir_struct_value_call_make_pair(
    MirStream *out, const struct MirStructValueRunnerSchedule *plan,
    int destination_local, struct Sym *destination_global,
    int destination_offset, int a, int b, int c)
{
    mir_stream_printf(out,
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,%d\n\tpush hl\n",
            c, b, a);
    if (destination_global != NULL)
        mir_struct_value_global_address(
            out, destination_global, destination_offset);
    else
        mir_struct_value_local_address(out, destination_local);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->make_pair);
    mir_struct_value_cleanup(out, 8);
}

static void mir_struct_value_call_make_wrap(
    MirStream *out, const struct MirStructValueRunnerSchedule *plan,
    int destination_local, int pair_local, int z, int tail)
{
    mir_stream_printf(out,
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,%d\n\tpush hl\n",
            tail, z);
    mir_struct_value_push_aggregate(
        out, pair_local, NULL, 0, 4);
    mir_struct_value_local_address(out, destination_local);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->make_wrap);
    mir_struct_value_cleanup(out, 10);
}

static void mir_struct_value_call_make_big(
    MirStream *out, const struct MirStructValueRunnerSchedule *plan,
    int destination_local, int base, int tag)
{
    mir_stream_printf(out,
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,%d\n\tpush hl\n",
            tag, base);
    mir_struct_value_local_address(out, destination_local);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->make_big);
    mir_struct_value_cleanup(out, 6);
}

static void mir_struct_value_call_sum(
    MirStream *out, struct Sym *function,
    int source_local, struct Sym *source_global,
    int source_offset, int size, int result_offset)
{
    mir_struct_value_push_aggregate(
        out, source_local, source_global, source_offset, size);
    mir_machine_emit_symbol_call(out, function);
    mir_struct_value_cleanup(out, size);
    mir_struct_return_store_hl(out, result_offset);
}

static void mir_struct_value_call_sum_two(
    MirStream *out, const struct MirStructValueRunnerSchedule *plan,
    int first_local, int second_local, int result_offset)
{
    mir_struct_value_push_aggregate(
        out, second_local, NULL, 0, 4);
    mir_struct_value_push_aggregate(
        out, first_local, NULL, 0, 4);
    mir_machine_emit_symbol_call(out, plan->sum_two);
    mir_struct_value_cleanup(out, 8);
    mir_struct_return_store_hl(out, result_offset);
}

static void mir_struct_value_call_sum_mix(
    MirStream *out, const struct MirStructValueRunnerSchedule *plan,
    int left, int pair_local, int right, int result_offset)
{
    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n", right);
    mir_struct_value_push_aggregate(
        out, pair_local, NULL, 0, 4);
    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n", left);
    mir_machine_emit_symbol_call(out, plan->sum_mix);
    mir_struct_value_cleanup(out, 8);
    mir_struct_return_store_hl(out, result_offset);
}

static void mir_struct_value_push_byte(
    MirStream *out, int local, struct Sym *global, int offset,
    int is_unsigned)
{
    if (global != NULL) {
        mir_struct_value_global_address(out, global, offset);
        mir_stream_puts("\tld l,(hl)\n", out);
    } else {
        mir_stream_printf(out, "\tld l,(ix%+d)\n", local + offset);
    }
    if (is_unsigned)
        mir_stream_puts("\tld h,0\n", out);
    else
        mir_stream_puts("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n",
              out);
    mir_stream_puts("\tpush hl\n", out);
}

static void mir_struct_value_push_word(
    MirStream *out, int local, struct Sym *global, int offset)
{
    if (global != NULL)
        mir_machine_emit_global_word(out, global, offset);
    else
        mir_struct_return_load_hl(out, local + offset);
    mir_stream_puts("\tpush hl\n", out);
}

static void mir_struct_value_print(
    MirStream *out, const struct MirStructValueRunnerSchedule *plan,
    int print, int words)
{
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[print]);
    mir_call_recovery_emit_named_call(
        out, plan->print_function,
        plan->print_names[print]);
    mir_emit_final_call_cleanup(out, words + 1);
}

static void mir_struct_value_push_address(
    MirStream *out, int local, struct Sym *global, int offset)
{
    if (global != NULL)
        mir_struct_value_global_address(out, global, offset);
    else
        mir_struct_value_local_address(out, local);
    mir_stream_puts("\tpush hl\n", out);
}

static void mir_struct_value_call_pointer_copy(
    MirStream *out, struct Sym *function,
    int destination_local, struct Sym *destination_global,
    int destination_offset, int source_local,
    struct Sym *source_global, int source_offset)
{
    mir_struct_value_push_address(
        out, source_local, source_global, source_offset);
    mir_struct_value_push_address(
        out, destination_local,
        destination_global, destination_offset);
    mir_machine_emit_symbol_call(out, function);
    mir_emit_final_call_cleanup(out, 2);
}

static void mir_struct_value_call_pointer_return(
    MirStream *out, struct Sym *function,
    int destination_local, struct Sym *destination_global,
    int destination_offset, int source_local,
    struct Sym *source_global, int source_offset)
{
    mir_struct_value_push_address(
        out, source_local, source_global, source_offset);
    mir_struct_value_push_address(
        out, destination_local,
        destination_global, destination_offset);
    mir_machine_emit_symbol_call(out, function);
    mir_emit_final_call_cleanup(out, 2);
}

static void mir_struct_value_call_assign_return(
    MirStream *out, const struct MirStructValueRunnerSchedule *plan,
    int destination_local, struct Sym *destination_global,
    int destination_offset, int assigned_local,
    int source_local, struct Sym *source_global, int source_offset)
{
    mir_struct_value_push_address(
        out, source_local, source_global, source_offset);
    mir_struct_value_push_address(
        out, assigned_local, NULL, 0);
    mir_struct_value_push_address(
        out, destination_local,
        destination_global, destination_offset);
    mir_machine_emit_symbol_call(out, plan->assign_return_pair);
    mir_emit_final_call_cleanup(out, 3);
}

static void mir_struct_value_print_pair(
    MirStream *out, const struct MirStructValueRunnerSchedule *plan,
    int print, int local, struct Sym *global,
    int global_offset, int result_offset)
{
    mir_struct_value_push_word(out, result_offset, NULL, 0);
    mir_struct_value_push_byte(
        out, local, global, global_offset + 3, 1);
    mir_struct_value_push_word(
        out, local, global, global_offset + 1);
    mir_struct_value_push_byte(
        out, local, global, global_offset, 0);
    mir_struct_value_print(out, plan, print, 4);
}

static void mir_struct_value_print_wrap(
    MirStream *out, const struct MirStructValueRunnerSchedule *plan,
    int print, int local, struct Sym *global, int result_offset)
{
    mir_struct_value_push_word(out, result_offset, NULL, 0);
    mir_struct_value_push_byte(out, local, global, 6, 0);
    mir_struct_value_push_word(out, local, global, 4);
    mir_struct_value_push_byte(out, local, global, 3, 1);
    mir_struct_value_push_word(out, local, global, 1);
    mir_struct_value_push_byte(out, local, global, 0, 0);
    mir_struct_value_print(out, plan, print, 6);
}

static void mir_emit_struct_value_runner_schedule(
    MirStream *out, const struct MirStructValueRunnerSchedule *plan)
{
    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-116\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    mir_struct_value_call_make_pair(
        out, plan, -12, NULL, 0, 8, 600, 14);

    mir_stream_puts("\tld (ix-4),3\n\tld hl,1000\n"
          "\tld (ix-3),l\n\tld (ix-2),h\n"
          "\tld (ix-1),7\n", out);
    mir_struct_value_copy(
        out, -4, NULL, 0, -8, NULL, 0, 4);
    mir_struct_value_copy(
        out, -8, NULL, 0, 0, plan->global_pair, 0, 4);
    mir_struct_value_call_sum(
        out, plan->sum_pair, 0, plan->global_pair, 0, 4, -112);
    mir_struct_value_print_pair(
        out, plan, 0, -8, NULL, 0, -112);

    mir_struct_value_call_make_pair(
        out, plan, -8, NULL, 0, 4, 2000, 8);
    mir_struct_value_call_sum(
        out, plan->sum_pair, -8, NULL, 0, 4, -112);
    mir_struct_value_print_pair(
        out, plan, 1, -8, NULL, 0, -112);

    mir_struct_value_call_sum(
        out, plan->sum_pair, -12, NULL, 0, 4, -112);
    mir_struct_value_print_pair(
        out, plan, 2, -12, NULL, 0, -112);

    mir_struct_value_call_make_pair(
        out, plan, -4, NULL, 0, 5, 3000, 9);
    mir_struct_value_call_sum_two(
        out, plan, -4, -8, -112);
    mir_struct_value_print_pair(
        out, plan, 3, -4, NULL, 0, -112);

    mir_struct_value_call_make_wrap(
        out, plan, -19, -4, 11, 12);
    mir_struct_value_copy(
        out, -19, NULL, 0, 0, plan->global_wrap, 0, 7);
    mir_struct_value_call_sum(
        out, plan->sum_wrap, 0, plan->global_wrap, 0, 7, -112);
    mir_struct_value_print_wrap(
        out, plan, 4, -19, NULL, -112);

    mir_struct_value_call_make_pair(
        out, plan, -4, NULL, 0, 6, 4000, 10);
    mir_struct_value_call_sum_mix(
        out, plan, 100, -4, 20, -112);
    mir_struct_value_push_word(out, -112, NULL, 0);
    mir_struct_value_print(out, plan, 5, 1);

    mir_struct_value_call_make_pair(
        out, plan, -8, NULL, 0, 7, 5000, 13);
    mir_struct_value_call_sum(
        out, plan->sum_pair, -8, NULL, 0, 4, -112);
    mir_struct_value_print_pair(
        out, plan, 6, -8, NULL, 0, -112);

    mir_struct_value_call_make_big(
        out, plan, -68, 1, 6000);
    mir_struct_value_copy(
        out, -68, NULL, 0, 0, plan->global_big, 0, 42);
    mir_struct_value_call_sum(
        out, plan->sum_big, 0, plan->global_big, 0, 42, -112);
    mir_struct_value_push_word(out, -112, NULL, 0);
    mir_struct_value_push_byte(out, -68, NULL, 39, 0);
    mir_struct_value_push_byte(out, -68, NULL, 20, 0);
    mir_struct_value_push_byte(out, -68, NULL, 10, 0);
    mir_struct_value_push_byte(out, -68, NULL, 2, 0);
    mir_struct_value_push_byte(out, -68, NULL, 1, 0);
    mir_struct_value_push_byte(out, -68, NULL, 0, 0);
    mir_struct_value_print(out, plan, 7, 7);

    mir_struct_value_call_make_big(
        out, plan, -110, 2, 7000);
    mir_struct_value_call_sum(
        out, plan->sum_big, -110, NULL, 0, 42, -112);
    mir_struct_value_push_word(out, -112, NULL, 0);
    mir_struct_value_push_word(out, -110, NULL, 40);
    mir_struct_value_push_byte(out, -110, NULL, 39, 0);
    mir_struct_value_push_byte(out, -110, NULL, 0, 0);
    mir_struct_value_print(out, plan, 8, 4);

    mir_struct_value_call_make_pair(
        out, plan, -4, NULL, 0, 3, 1000, 7);
    mir_struct_value_call_make_pair(
        out, plan, -8, NULL, 0, 4, 2000, 8);
    mir_struct_value_call_sum(
        out, plan->sum_pair, -4, NULL, 0, 4, -112);
    mir_struct_value_call_sum_two(
        out, plan, -4, -8, -114);
    mir_struct_value_call_sum_mix(
        out, plan, 10, -4, 20, -116);
    mir_struct_value_push_word(out, -116, NULL, 0);
    mir_struct_value_push_word(out, -114, NULL, 0);
    mir_struct_value_push_word(out, -112, NULL, 0);
    mir_struct_value_print(out, plan, 9, 3);

    mir_struct_value_call_make_wrap(
        out, plan, -19, -4, 11, 12);
    mir_struct_value_call_sum(
        out, plan->sum_wrap, -19, NULL, 0, 7, -112);
    mir_struct_value_push_word(out, -112, NULL, 0);
    mir_struct_value_print(out, plan, 10, 1);

    mir_struct_value_call_make_big(
        out, plan, -68, 1, 6000);
    mir_struct_value_call_sum(
        out, plan->sum_big, -68, NULL, 0, 42, -112);
    mir_struct_value_push_word(out, -112, NULL, 0);
    mir_struct_value_print(out, plan, 11, 1);

    mir_struct_value_call_pointer_copy(
        out, plan->copy_pair,
        -8, NULL, 0, -4, NULL, 0);
    mir_struct_value_call_pointer_return(
        out, plan->return_pair,
        0, plan->global_pair, 0, -8, NULL, 0);
    mir_struct_value_call_sum(
        out, plan->sum_pair, 0, plan->global_pair, 0, 4, -112);
    mir_struct_value_print_pair(
        out, plan, 12, 0, plan->global_pair, 0, -112);

    mir_struct_value_call_make_pair(
        out, plan, 0, plan->global_pair_array, 0,
        5, 3000, 9);
    mir_struct_value_call_pointer_copy(
        out, plan->copy_pair,
        0, plan->global_pair_array, 4,
        0, plan->global_pair_array, 0);
    mir_struct_value_call_assign_return(
        out, plan, 0, plan->global_pair_array, 8,
        -8, 0, plan->global_pair_array, 4);
    mir_struct_value_call_sum(
        out, plan->sum_pair, 0,
        plan->global_pair_array, 8, 4, -112);
    mir_struct_value_print_pair(
        out, plan, 13, -8, NULL, 0, -112);

    mir_struct_value_call_pointer_copy(
        out, plan->copy_wrap,
        -26, NULL, 0, -19, NULL, 0);
    mir_struct_value_call_pointer_return(
        out, plan->return_wrap,
        0, plan->global_wrap, 0, -26, NULL, 0);
    mir_struct_value_push_byte(out, 0, plan->global_wrap, 6, 0);
    mir_struct_value_push_word(out, 0, plan->global_wrap, 4);
    mir_struct_value_push_byte(out, 0, plan->global_wrap, 3, 1);
    mir_struct_value_push_word(out, 0, plan->global_wrap, 1);
    mir_struct_value_push_byte(out, 0, plan->global_wrap, 0, 0);
    mir_struct_value_print(out, plan, 14, 5);

    mir_stream_puts("\tld hl,7000\n\tpush hl\n"
          "\tld hl,2\n\tpush hl\n", out);
    mir_struct_value_push_address(out, -110, NULL, 0);
    mir_machine_emit_symbol_call(out, plan->fill_big);
    mir_emit_final_call_cleanup(out, 3);
    mir_struct_value_call_pointer_return(
        out, plan->return_big,
        0, plan->global_big, 0, -110, NULL, 0);
    mir_struct_value_call_pointer_return(
        out, plan->return_big,
        0, plan->global_big_array, 42,
        0, plan->global_big, 0);
    mir_struct_value_call_sum(
        out, plan->sum_big, 0,
        plan->global_big_array, 42, 42, -112);
    mir_struct_value_push_word(out, -112, NULL, 0);
    mir_struct_value_push_byte(out, 0, plan->global_big, 39, 0);
    mir_struct_value_push_byte(out, 0, plan->global_big, 0, 0);
    mir_struct_value_print(out, plan, 15, 3);
    mir_struct_value_print(out, plan, 16, 0);
    mir_stream_puts("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
    mir_stream_puts("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_inline_nest_runner_schedule(
    struct MirInlineNestRunnerSchedule *plan)
{
    int first_arguments[1];
    int second_arguments[1];
    int third_arguments[1];
    struct Sym *buffer;
    struct Sym *count;
    long offset;
    int memory_type;
    int memory_storage;
    int count_offset;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 69 || mir.next_value != 41 ||
        mir_cfg_block_count() != 13 || mir.local_bytes != 32 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        mir_call_runner_has_volatile_memory() ||
        mir_has_cfg_backedge() ||
        !mir_memory_runner_word_type(mir.return_type, 0) ||
        !mir_machine_constant_equals(mir.insns[1].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[4].dst, 10) ||
        !mir_machine_constant_equals(mir.insns[7].dst, 4) ||
        !mir_machine_constant_equals(mir.insns[12].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[18].dst, 3) ||
        !mir_machine_constant_equals(mir.insns[23].dst, 100) ||
        !mir_machine_constant_equals(mir.insns[32].dst, 65436) ||
        !mir_machine_constant_equals(mir.insns[41].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[63].dst, 1))
        return 0;
    plan->give_function =
        mir_memory_runner_call_function(6, 0, 1);
    if (plan->give_function == NULL ||
        mir_memory_runner_call_function(9, 0, 1) !=
            plan->give_function ||
        mir_memory_runner_call_function(60, 0, 1) !=
            plan->give_function ||
        !mir_machine_call_arguments(
            &mir.insns[6], 1, first_arguments) ||
        !mir_machine_call_arguments(
            &mir.insns[9], 1, second_arguments) ||
        !mir_machine_call_arguments(
            &mir.insns[60], 1, third_arguments) ||
        first_arguments[0] != mir.insns[4].dst ||
        second_arguments[0] != mir.insns[7].dst ||
        third_arguments[0] != mir.insns[58].dst)
        return mir_machine_reject(
            "inline-nest-runner-schedule", "calls");
    if (!mir_machine_global_address_offset(
            mir.insns[10].dst, &buffer, &offset, 0) ||
        offset != 0 ||
        !mir_machine_match_nonvolatile_array(
            &mir.insns[10], 2, 0, &buffer) ||
        !mir_machine_global_address_offset(
            mir.insns[61].dst, &plan->buffer, &offset, 0) ||
        plan->buffer != buffer || offset != 0 ||
        !mir_scalar_memory_location(
            &mir.insns[11], &memory_type,
            &memory_storage, &count_offset) ||
        (memory_storage != SC_GLOBAL &&
         memory_storage != SC_EXTERN) ||
        (count = find_global(mir.insns[11].name)) == NULL ||
        count->is_volatile ||
        !mir_machine_same_location(
            &mir.insns[11], &mir.insns[14]) ||
        !mir_machine_same_location(
            &mir.insns[11], &mir.insns[62]) ||
        !mir_machine_same_location(
            &mir.insns[11], &mir.insns[65]))
        return mir_machine_reject(
            "inline-nest-runner-schedule", "globals");
    plan->buffer = buffer;
    plan->count = count;
    plan->first_value = 10;
    plan->second_value = 4;
    plan->scale = 3;
    return 1;
}

static void mir_inline_nest_give(
    MirStream *out, const struct MirInlineNestRunnerSchedule *plan,
    int value)
{
    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n", value);
    mir_machine_emit_symbol_call(out, plan->give_function);
    mir_emit_final_call_cleanup(out, 1);
}

static void mir_inline_nest_take(
    MirStream *out, const struct MirInlineNestRunnerSchedule *plan)
{
    mir_machine_emit_global_word(out, plan->count, 0);
    mir_stream_puts("\tdec hl\n\tpush hl\n", out);
    mir_machine_emit_global_word_store(out, plan->count, 0);
    mir_stream_puts("\tpop hl\n\tadd hl,hl\n", out);
    mir_machine_emit_global_address_de(out, plan->buffer, 0);
    mir_stream_puts("\tadd hl,de\n\tld e,(hl)\n\tinc hl\n"
          "\tld d,(hl)\n\tex de,hl\n", out);
}

static void mir_emit_inline_nest_runner_schedule(
    MirStream *out, const struct MirInlineNestRunnerSchedule *plan)
{
    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\tld hl,0\n", out);
    mir_machine_emit_global_word_store(out, plan->count, 0);
    mir_inline_nest_give(out, plan, plan->first_value);
    mir_inline_nest_give(out, plan, plan->second_value);
    mir_inline_nest_take(out, plan);
    if (plan->scale == 3)
        mir_stream_puts("\tld e,l\n\tld d,h\n\tadd hl,hl\n"
              "\tadd hl,de\n", out);
    else
        mir_emit_mul_hl_const(out, (unsigned long)plan->scale);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->give_function);
    mir_emit_final_call_cleanup(out, 1);
    mir_inline_nest_take(out, plan);
    mir_stream_puts("\tret\n", out);
}

static const struct AstNode *mir_call_inline_single_statement(
    const struct Sym *callee)
{
    const struct AstNode *statement;

    if (callee == NULL)
        return NULL;
    statement = callee->inline_stmt_body != NULL
        ? callee->inline_stmt_body : callee->inline_stmt_expr;
    if (statement != NULL && statement->kind == AST_COMPOUND) {
        if (statement->list_len != 1)
            return NULL;
        statement = statement->list[0];
    }
    if (statement != NULL && statement->kind == AST_EXPR_STMT)
        statement = statement->a;
    return statement;
}

static int mir_call_inline_byte_store(
    const struct MirInsn *call, struct Sym **array_out)
{
    const struct AstNode *assignment;
    const struct AstNode *index;
    const struct AstNode *addition;
    const struct AstNode *value;
    struct Sym *callee;
    struct Sym *array;

    if (call == NULL || call->opcode != MIR_CALL ||
        (call->memory_flags &
         MIR_CALL_FLAG_INLINE_SUBSTITUTABLE) == 0 ||
        (call->type & 15) != TYPE_VOID ||
        (callee = find_global(call->name)) == NULL ||
        !callee->is_static || !callee->is_inline ||
        !callee->has_proto || callee->proto_variadic ||
        callee->proto_nargs != 3 || callee->has_inline_local)
        return 0;
    assignment = mir_call_inline_single_statement(callee);
    if (assignment == NULL || assignment->kind != AST_ASSIGN ||
        assignment->op != '=' || assignment->a == NULL ||
        assignment->a->kind != AST_INDEX ||
        !mir_inline_is_parameter_low_bytes(
            assignment->b, callee, 2, 1))
        return 0;
    index = assignment->a;
    value = assignment->b;
    while (value != NULL && value->kind == AST_CAST) {
        if ((value->type & 15) == TYPE_BOOL)
            return 0;
        value = value->a;
    }
    array = mir_inline_ident_symbol(index->a);
    addition = mir_inline_unwrap_cast(index->b);
    if (array == NULL || !array->is_array || array->elem_size != 1 ||
        (array->storage != SC_GLOBAL &&
         array->storage != SC_EXTERN) ||
        array->is_volatile || array->pointee_is_volatile ||
        addition == NULL || addition->kind != AST_BINARY ||
        addition->op != '+' ||
        !((mir_inline_is_parameter(addition->a, callee, 0) &&
           mir_inline_is_parameter(addition->b, callee, 1)) ||
          (mir_inline_is_parameter(addition->a, callee, 1) &&
           mir_inline_is_parameter(addition->b, callee, 0))))
        return 0;
    *array_out = array;
    return 1;
}

static int mir_match_inline_parameter_call_schedule(
    struct MirInlineParameterCallSchedule *plan)
{
    static const unsigned char expected_opcodes[39] = {
        MIR_LABEL, MIR_CONST, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_STORE, MIR_CONST, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_STORE, MIR_CONST, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_STORE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG,
        MIR_NOP, MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN
    };
    struct Sym *first_function;
    struct Sym *second_function;
    struct Sym *print_function;
    struct Sym *array;
    int first_arguments[2];
    int second_arguments[2];
    int store_arguments[3];
    int print_arguments[4];
    long value;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (!mir_call_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        mir_cfg_block_count() != 1 || mir.local_bytes != 6 ||
        mir.has_vla ||
        !mir_memory_runner_word_type(mir.return_type, 0))
        return 0;
    first_function = mir_memory_runner_call_function(5, 0, 2);
    second_function = mir_memory_runner_call_function(12, 0, 2);
    if (first_function == NULL || first_function != second_function ||
        !mir_memory_runner_word_type(first_function->type, 0) ||
        !mir_memory_runner_word_type(
            first_function->proto_types[0], 0) ||
        !mir_memory_runner_word_type(
            first_function->proto_types[1], 0) ||
        !mir_machine_two_call_arguments(
            &mir.insns[5], first_arguments) ||
        !mir_machine_two_call_arguments(
            &mir.insns[12], second_arguments) ||
        first_arguments[0] != mir.insns[1].dst ||
        first_arguments[1] != mir.insns[3].dst ||
        second_arguments[0] != mir.insns[8].dst ||
        second_arguments[1] != mir.insns[10].dst)
        return mir_machine_reject(
            "inline-parameter-call-schedule", "value-calls");
    for (item = 0; item < 2; ++item) {
        if (!mir_machine_evaluate_constant(
                first_arguments[item], &value, 0))
            return 0;
        plan->first_arguments[item] = (int)value;
        if (!mir_machine_evaluate_constant(
                second_arguments[item], &value, 0))
            return 0;
        plan->second_arguments[item] = (int)value;
    }
    if (!mir_machine_unobservable_local_store(&mir.insns[7]) ||
        !mir_machine_unobservable_local_store(&mir.insns[14]) ||
        mir.insns[7].src1 != mir.insns[5].dst ||
        mir.insns[14].src1 != mir.insns[12].dst ||
        mir.insns[7].object < 0 || mir.insns[14].object < 0 ||
        mir.insns[7].object == mir.insns[14].object ||
        !mir_call_inline_byte_store(&mir.insns[21], &array) ||
        !mir_machine_three_call_arguments(
            &mir.insns[21], store_arguments))
        return mir_machine_reject(
            "inline-parameter-call-schedule", "inline-store");
    for (item = 0; item < 3; ++item) {
        if (!mir_machine_evaluate_constant(
                store_arguments[item], &value, 0))
            return 0;
        if (item < 2)
            plan->array_offset += (int)value;
        else
            plan->store_value = (int)value & 0xff;
    }
    if (plan->array_offset < 0 || plan->array_offset > 32767 ||
        find_global(mir.insns[22].name) != array ||
        mir.insns[24].src1 != mir.insns[22].dst ||
        mir.insns[24].src2 != mir.insns[23].dst ||
        !mir_machine_constant_equals(
            mir.insns[23].dst, plan->array_offset) ||
        mir.insns[24].immediate != 1 ||
        mir.insns[24].memory_size != 1 ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        mir.insns[25].memory_size != 1 ||
        (mir.insns[25].memory_flags & (1 | 8)) != 0 ||
        mir.insns[26].src1 != mir.insns[25].dst ||
        mir.insns[26].immediate != 0 ||
        !mir_machine_unobservable_local_store(&mir.insns[27]) ||
        mir.insns[27].src1 != mir.insns[26].dst)
        return mir_machine_reject(
            "inline-parameter-call-schedule", "array-load");
    print_function = mir_memory_runner_call_function(36, 1, 1);
    if (print_function == NULL ||
        !mir_machine_call_arguments(
            &mir.insns[36], 4, print_arguments) ||
        print_arguments[0] != mir.insns[28].dst ||
        print_arguments[1] != mir.insns[5].dst ||
        print_arguments[2] != mir.insns[12].dst ||
        print_arguments[3] != mir.insns[26].dst ||
        !mir_machine_constant_equals(mir.insns[37].dst, 0) ||
        mir.insns[38].src1 != mir.insns[37].dst)
        return mir_machine_reject(
            "inline-parameter-call-schedule", "print");
    plan->value_function = first_function;
    plan->array = array;
    plan->print_function = print_function;
    plan->format_string = (int)mir.insns[28].immediate;
    snprintf(plan->print_name, sizeof(plan->print_name), "%s",
             mir.insns[36].base_name);
    return plan->print_name[0] != 0;
}

static void mir_call_emit_global_address(
    MirStream *out, struct Sym *symbol, int offset)
{
    const char *name = asm_name_for(sym_asm_name(symbol));

    if ((symbol->storage == SC_EXTERN || symbol->needs_extrn) &&
        mir_extrn_should_emit(symbol))
        mir_stream_printf(out, "\textrn %s\n", name);
    if (offset == 0)
        mir_stream_printf(out, "\tld hl,%s\n", name);
    else
        mir_stream_printf(out, "\tld hl,%s%+d\n", name, offset);
}

static void mir_emit_inline_parameter_value_call(
    MirStream *out, struct Sym *function, const int arguments[2],
    int frame_offset)
{
    mir_stream_printf(out,
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,%d\n\tpush hl\n",
            arguments[1], arguments[0]);
    mir_machine_emit_symbol_call(out, function);
    mir_stream_printf(out,
            "\tpop bc\n\tpop bc\n"
            "\tld (ix%+d),l\n\tld (ix%+d),h\n",
            frame_offset, frame_offset + 1);
}

static void mir_emit_inline_parameter_call_schedule(
    MirStream *out, const struct MirInlineParameterCallSchedule *plan)
{
    mir_stream_printf(out,
            "%s\n"
            "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tld hl,-4\n\tadd hl,sp\n\tld sp,hl\n",
            MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_emit_inline_parameter_value_call(
        out, plan->value_function, plan->first_arguments, -2);
    mir_emit_inline_parameter_value_call(
        out, plan->value_function, plan->second_arguments, -4);
    mir_stream_puts(";@dcc.mir inline-simple-store\n", out);
    mir_call_emit_global_address(
        out, plan->array, plan->array_offset);
    mir_stream_printf(out,
            "\tld a,%d\n\tld (hl),a\n"
            "\tld a,(hl)\n\tld l,a\n\tld h,0\n\tpush hl\n"
            "\tld l,(ix-4)\n\tld h,(ix-3)\n\tpush hl\n"
            "\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->store_value, plan->format_string);
    mir_emit_runtime_call(out, plan->print_name);
    mir_stream_puts("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_arrow_direct_function(
    const struct MirInsn *call, int argument_count, int variadic,
    struct Sym **function_out)
{
    struct Sym *function;

    if (call->opcode != MIR_CALL || call->src1 >= 0)
        return 0;
    function = find_global(call->name);
    if (function == NULL || function->storage != SC_FUNC ||
        function->is_funcptr || !function->has_proto ||
        function->proto_nargs != argument_count ||
        function->proto_variadic != variadic ||
        (call->base_name[0] != 0 && !variadic &&
         strcmp(call->base_name,
                asm_name_for(sym_asm_name(function)))))
        return 0;
    *function_out = function;
    return 1;
}

static int mir_match_room_resolution_schedule(
    struct MirRoomResolutionSchedule *plan)
{
    static const unsigned char expected_opcodes[121] = {
        MIR_LABEL, MIR_PARAM, MIR_LABEL, MIR_LOAD, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_NOP, MIR_STORE, MIR_NOP, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_CALL, MIR_LOAD,
        MIR_ARG, MIR_CALL, MIR_RETURN, MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_LOAD, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_CALL, MIR_CONST, MIR_RETURN, MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_LOAD, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_CALL, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CALL, MIR_STORE_INDIRECT, MIR_NOP,
        MIR_JUMP, MIR_NOP, MIR_LABEL, MIR_CONST, MIR_RETURN, MIR_NOP, MIR_LABEL, MIR_JUMP,
        MIR_LABEL
    };
    static const int edges[][2] = {
        {18, 28}, {36, 40}, {39, 58}, {48, 52},
        {51, 54}, {57, 58}, {60, 68}, {76, 80},
        {79, 98}, {88, 92}, {91, 94}, {97, 98},
        {100, 114}, {112, 2}, {119, 2}
    };
    static const int game_loads[] = {
        3, 4, 12, 23, 30, 42, 70, 82, 105
    };
    static const int location_members[] = {
        5, 13, 31, 43, 71, 83, 106
    };
    static const int print_calls[] = {21, 63, 103};
    static const int print_strings[] = {19, 61, 101};
    static const int flush_calls[] = {22, 64, 104};
    const struct MirInsn *game = &mir.insns[1];
    struct Sym *print_function = NULL;
    struct Sym *flush_function = NULL;
    int edge;
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 121 || mir_cfg_block_count() != 21 ||
        mir.local_bytes != 2 || mir.aggregate_temp_bytes != 0 ||
        mir.has_vla || !mir_has_cfg_backedge() ||
        type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "room-resolution-schedule", "opcodes");
        if ((insn->opcode == MIR_LOAD_INDIRECT ||
             insn->opcode == MIR_STORE_INDIRECT) &&
            (insn->memory_flags & (1 | 8)) != 0)
            return mir_machine_reject(
                "room-resolution-schedule", "volatile-memory");
    }
    for (edge = 0;
         edge < (int)(sizeof(edges) / sizeof(edges[0])); ++edge)
        if (mir.insns[edges[edge][0]].label !=
            mir.insns[edges[edge][1]].label)
            return mir_machine_reject(
                "room-resolution-schedule", "control-flow");
    if (type_ptr_depth(game->type) != 1 ||
        type_size(game->type) != 2 ||
        mir_machine_pointee_is_volatile(game) ||
        !mir_machine_parameter_value_offset(
            game->dst, &plan->game_stack_offset))
        return mir_machine_reject(
            "room-resolution-schedule", "parameter");
    for (item = 0;
         item < (int)(sizeof(game_loads) /
                      sizeof(game_loads[0])); ++item)
        if (!mir_machine_same_location(
                game, &mir.insns[game_loads[item]]))
            return mir_machine_reject(
                "room-resolution-schedule", "game-loads");
    plan->location_offset = (int)mir.insns[5].immediate;
    if (plan->location_offset < -120 ||
        plan->location_offset > 116)
        return mir_machine_reject(
            "room-resolution-schedule", "location-offset");
    for (item = 0; item < 7; ++item)
        if (mir.insns[location_members[item]].immediate !=
                plan->location_offset ||
            mir.insns[location_members[item]].memory_size != 12)
            return mir_machine_reject(
                "room-resolution-schedule", "location-members");
    if (!mir_machine_constant_equals(mir.insns[6].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[14].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[32].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[44].dst, 3) ||
        !mir_machine_constant_equals(mir.insns[65].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[72].dst, 4) ||
        !mir_machine_constant_equals(mir.insns[84].dst, 5) ||
        !mir_machine_constant_equals(mir.insns[107].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[115].dst, 0))
        return mir_machine_reject(
            "room-resolution-schedule", "constants");
    if (!mir_arrow_direct_function(
            &mir.insns[25], 1, 0, &plan->wake_function) ||
        !mir_arrow_direct_function(
            &mir.insns[109], 0, 0,
            &plan->random_room_function))
        return mir_machine_reject(
            "room-resolution-schedule", "value-calls");
    for (item = 0; item < 3; ++item) {
        struct Sym *candidate;

        if (!mir_arrow_direct_function(
                &mir.insns[print_calls[item]], 1, 1,
                &candidate) ||
            mir.insns[print_strings[item]].immediate < 0 ||
            (item == 0
                 ? (print_function = candidate, 0)
                 : candidate != print_function))
            return mir_machine_reject(
                "room-resolution-schedule", "print-calls");
        plan->string_ids[item] =
            (int)mir.insns[print_strings[item]].immediate;
        if (!mir_arrow_direct_function(
                &mir.insns[flush_calls[item]], 0, 0,
                &candidate) ||
            (item == 0
                 ? (flush_function = candidate, 0)
                 : candidate != flush_function))
            return mir_machine_reject(
                "room-resolution-schedule", "flush-calls");
    }
    plan->flush_function = flush_function;
    snprintf(plan->print_name, sizeof(plan->print_name), "%s",
             mir.insns[print_calls[0]].base_name);
    return 1;
}

static int mir_match_global_memset_schedule(
    struct MirGlobalMemsetSchedule *plan)
{
    const struct MirInsn *size = &mir.insns[1];
    const struct MirInsn *address;
    const struct MirInsn *call;
    int arguments[3];
    long offset;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 17 ||
        mir_cfg_block_count() != 1 ||
        mir.local_bytes != 0 || mir.aggregate_temp_bytes != 0 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID ||
        size->opcode != MIR_PARAM ||
        type_ptr_depth(size->type) != 0 ||
        (size->type & 15) != TYPE_INT ||
        type_size(size->type) != 2 ||
        !mir_machine_parameter_value_offset(
            size->dst, &plan->size_stack_offset))
        return 0;
    {
        const struct MirInsn *value = &mir.insns[2];

        address = &mir.insns[8];
        call = &mir.insns[16];
        if (value->opcode != MIR_PARAM ||
            type_ptr_depth(value->type) != 0 ||
            (value->type & 15) != TYPE_INT ||
            type_size(value->type) != 2 ||
            !mir_machine_parameter_value_offset(
                value->dst, &plan->value_stack_offset) ||
            !mir_machine_constant_equals(mir.insns[4].dst, 255) ||
            mir.insns[5].immediate != '&' ||
            mir.insns[5].src1 != value->dst ||
            mir.insns[5].src2 != mir.insns[4].dst ||
            !mir_machine_call_arguments(
                call, 3, arguments) ||
            arguments[0] != address->dst ||
            arguments[1] != mir.insns[5].dst ||
            arguments[2] != size->dst)
            return mir_machine_reject(
                "global-memset-schedule", "value");
    }
    if (!mir_machine_global_address_offset(
            address->dst, &plan->buffer, &offset, 0) ||
        plan->buffer == NULL || offset < -32768 ||
        offset > 32767 ||
        !mir_arrow_direct_function(call, 3, 0, &plan->function) ||
        type_ptr_depth(call->type) != 1)
        return mir_machine_reject(
            "global-memset-schedule", "target");
    plan->buffer_offset = (int)offset;
    return 1;
}

static void mir_emit_global_memset_schedule(
    MirStream *out, const struct MirGlobalMemsetSchedule *plan)
{
    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tld d,0\n",
            plan->value_stack_offset);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n"
            "\tpush hl\n\tpush de\n",
            plan->size_stack_offset);
    mir_machine_emit_global_address_de(
        out, plan->buffer, plan->buffer_offset);
    mir_stream_puts("\tpush de\n", out);
    mir_machine_emit_symbol_call(out, plan->function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tret\n", out);
}

static int mir_match_word_table_runner_schedule(
    struct MirWordTableRunnerSchedule *plan)
{
    static const unsigned char expected_opcodes[34] = {
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_PHI, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_ARG, MIR_CALL, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_RETURN
    };
    static const int edges[][2] = {
        {15, 28}, {27, 7}
    };
    struct Sym *print_function;
    struct Sym *second_table;
    long table_offset;
    long second_table_offset;
    int arguments[1];
    int edge;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 34 || mir_cfg_block_count() != 4 ||
        mir.local_bytes != 2 || mir.aggregate_temp_bytes != 0 ||
        mir.has_vla || !mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "word-table-runner-schedule", "opcodes");
    for (edge = 0;
         edge < (int)(sizeof(edges) / sizeof(edges[0])); ++edge)
        if (mir.insns[edges[edge][0]].label !=
            mir.insns[edges[edge][1]].label)
            return mir_machine_reject(
                "word-table-runner-schedule", "control-flow");
    if (!mir_machine_global_address_offset(
            mir.insns[9].dst, &plan->table, &table_offset, 0) ||
        !mir_machine_global_address_offset(
            mir.insns[16].dst, &second_table,
            &second_table_offset, 0) ||
        plan->table == NULL || second_table != plan->table ||
        second_table_offset != table_offset ||
        table_offset < -32768 || table_offset > 32767 ||
        mir.insns[11].immediate != 2 ||
        mir.insns[18].immediate != 2 ||
        !mir_machine_constant_equals(mir.insns[4].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[13].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[24].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[32].dst, 0) ||
        mir.insns[1].immediate < 0 ||
        mir.insns[29].immediate < 0 ||
        !mir_arrow_direct_function(
            &mir.insns[3], 1, 1, &print_function) ||
        !mir_arrow_direct_function(
            &mir.insns[31], 1, 1, &plan->print_function) ||
        print_function != plan->print_function ||
        !mir_arrow_direct_function(
            &mir.insns[21], 1, 0, &plan->run_function) ||
        !mir_machine_call_arguments(
            &mir.insns[21], 1, arguments) ||
        arguments[0] != mir.insns[19].dst)
        return mir_machine_reject(
            "word-table-runner-schedule", "semantics");
    plan->table_offset = (int)table_offset;
    plan->start_string_id = (int)mir.insns[1].immediate;
    plan->done_string_id = (int)mir.insns[29].immediate;
    snprintf(plan->print_name, sizeof(plan->print_name), "%s",
             mir.insns[3].base_name);
    return 1;
}

static void mir_emit_word_table_runner_schedule(
    MirStream *out, const struct MirWordTableRunnerSchedule *plan)
{
    int loop = new_label();
    int done = new_label();

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n"
          ";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->start_string_id);
    mir_emit_runtime_call(out, plan->print_name);
    mir_stream_puts("\tpop bc\n", out);
    mir_machine_emit_global_address_de(
        out, plan->table, plan->table_offset);
    mir_stream_puts("\tpush de\n\tpop iy\n", out);
    mir_stream_printf(out,
            "L%d:\n\tld l,(iy+0)\n\tld h,(iy+1)\n"
            "\tld a,h\n\tor l\n\tjp z,L%d\n"
            "\tpush hl\n",
            loop, done);
    mir_machine_emit_symbol_call(out, plan->run_function);
    mir_stream_puts("\tpop bc\n\tinc iy\n\tinc iy\n", out);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n\tld hl,S%d\n\tpush hl\n",
            loop, done, plan->done_string_id);
    mir_emit_runtime_call(out, plan->print_name);
    mir_stream_puts("\tpop bc\n\tld hl,0\n\tpop iy\n"
          ";@dcc.reg free=iy\n\tret\n", out);
}

static int mir_match_seek_check_schedule(
    struct MirSeekCheckSchedule *plan)
{
    static const unsigned char expected_opcodes[28] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_ARG,
        MIR_NOP, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STORE,
        MIR_NOP, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_NOP, MIR_LABEL
    };
    const struct MirInsn *fd = &mir.insns[1];
    const struct MirInsn *offset = &mir.insns[2];
    const struct MirInsn *where = &mir.insns[3];
    int seek_arguments[3];
    int print_arguments[3];
    int fail_arguments[1];
    struct Sym *print_function;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 28 || mir_cfg_block_count() != 2 ||
        mir.local_bytes != 4 || mir.aggregate_temp_bytes != 0 ||
        mir.has_vla || mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "seek-check-schedule", "opcodes");
    if (type_ptr_depth(fd->type) != 0 ||
        (fd->type & 15) != TYPE_INT ||
        type_size(fd->type) != 2 ||
        type_ptr_depth(offset->type) != 0 ||
        (offset->type & 15) != TYPE_LONG ||
        type_size(offset->type) != 4 ||
        type_ptr_depth(where->type) != 1 ||
        (where->type & 15) != TYPE_CHAR ||
        !mir_machine_parameter_value_offset(
            fd->dst, &plan->fd_stack_offset) ||
        !mir_machine_parameter_value_offset(
            where->dst, &plan->where_stack_offset) ||
        plan->where_stack_offset !=
            plan->fd_stack_offset + 6 ||
        !mir_machine_constant_equals(mir.insns[8].dst, 0))
        return mir_machine_reject(
            "seek-check-schedule", "parameters");
    plan->offset_stack_offset = plan->fd_stack_offset + 2;
    if (
        !mir_machine_call_arguments(
            &mir.insns[10], 3, seek_arguments) ||
        seek_arguments[0] != fd->dst ||
        seek_arguments[1] != offset->dst ||
        seek_arguments[2] != mir.insns[8].dst ||
        !mir_arrow_direct_function(
            &mir.insns[10], 3, 0, &plan->seek_function) ||
        mir.insns[14].immediate != TOK_NE ||
        mir.insns[14].src1 != mir.insns[10].dst ||
        mir.insns[14].src2 != offset->dst ||
        mir.insns[15].label != mir.insns[27].label ||
        mir.insns[16].immediate < 0)
        return mir_machine_reject(
            "seek-check-schedule", "seek-call");
    if (
        !mir_machine_call_arguments(
            &mir.insns[22], 3, print_arguments) ||
        print_arguments[0] != mir.insns[16].dst ||
        print_arguments[1] != mir.insns[10].dst ||
        print_arguments[2] != offset->dst ||
        !mir_arrow_direct_function(
            &mir.insns[22], 1, 1, &print_function))
        return mir_machine_reject(
            "seek-check-schedule", "print-call");
    if (
        !mir_machine_call_arguments(
            &mir.insns[25], 1, fail_arguments) ||
        fail_arguments[0] != mir.insns[23].dst ||
        !mir_arrow_direct_function(
            &mir.insns[25], 1, 0, &plan->fail_function) ||
        !mir_machine_same_location(where, &mir.insns[23]))
        return mir_machine_reject(
            "seek-check-schedule", "fail-call");
    plan->format_string_id = (int)mir.insns[16].immediate;
    snprintf(plan->print_name, sizeof(plan->print_name), "%s",
             mir.insns[22].base_name);
    return 1;
}

static void mir_emit_seek_check_schedule(
    MirStream *out, const struct MirSeekCheckSchedule *plan)
{
    int done = new_label();

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-4\n\tadd hl,sp\n\tld sp,hl\n",
          out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\tld hl,0\n\tpush hl\n", out);
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n"
            "\tpush de\n\tpush hl\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n",
            plan->offset_stack_offset + 2,
            plan->offset_stack_offset + 3,
            plan->offset_stack_offset + 4,
            plan->offset_stack_offset + 5,
            plan->fd_stack_offset + 2,
            plan->fd_stack_offset + 3);
    mir_machine_emit_symbol_call(out, plan->seek_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tld (ix-2),l\n\tld (ix-1),h\n"
          "\tld (ix-4),e\n\tld (ix-3),d\n", out);
    mir_stream_printf(out,
            "\tld a,l\n\txor (ix%+d)\n\tld b,a\n"
            "\tld a,h\n\txor (ix%+d)\n\tor b\n\tld b,a\n"
            "\tld a,e\n\txor (ix%+d)\n\tor b\n\tld b,a\n"
            "\tld a,d\n\txor (ix%+d)\n\tor b\n\tjp z,L%d\n",
            plan->offset_stack_offset + 2,
            plan->offset_stack_offset + 3,
            plan->offset_stack_offset + 4,
            plan->offset_stack_offset + 5, done);
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n"
            "\tpush de\n\tpush hl\n"
            "\tld l,(ix-2)\n\tld h,(ix-1)\n"
            "\tld e,(ix-4)\n\tld d,(ix-3)\n"
            "\tpush de\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->offset_stack_offset + 2,
            plan->offset_stack_offset + 3,
            plan->offset_stack_offset + 4,
            plan->offset_stack_offset + 5,
            plan->format_string_id);
    mir_emit_runtime_call(out, plan->print_name);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n",
            plan->where_stack_offset + 2,
            plan->where_stack_offset + 3);
    mir_machine_emit_symbol_call(out, plan->fail_function);
    mir_stream_puts("\tpop bc\n", out);
    mir_stream_printf(out,
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            done);
}

static int mir_roundtrip_call(
    int instruction, int arguments, int variadic,
    struct Sym **function_out)
{
    const struct MirInsn *call = &mir.insns[instruction];
    struct Sym *function;
    int values[16];

    if (arguments < 0 || arguments > 16 ||
        call->opcode != MIR_CALL || call->src1 >= 0 ||
        call->memory_flags !=
            (variadic ? MIR_CALL_FLAG_VARIADIC : 0) ||
        !mir_machine_call_arguments(call, arguments, values) ||
        (function = find_global(call->name)) == NULL ||
        function->storage != SC_FUNC || function->is_funcptr)
        return 0;
    *function_out = function;
    return 1;
}

static int mir_match_file_roundtrip_schedule(
    struct MirFileRoundtripSchedule *plan)
{
    static const unsigned char expected_opcodes[416] = {
        MIR_LABEL, MIR_PARAM, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_NOP,
        MIR_NOP, MIR_PHI, MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_ARG, MIR_NOP, MIR_UNARY, MIR_ARG, MIR_CALL, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_ARG, MIR_NOP, MIR_UNARY, MIR_ARG, MIR_CALL, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_ARG, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP, MIR_ARG, MIR_NOP, MIR_NOP,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_NOP,
        MIR_NOP, MIR_PHI, MIR_PHI, MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP, MIR_ARG, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_NOP, MIR_ARG, MIR_NOP, MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG,
        MIR_NOP, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP, MIR_LABEL,
        MIR_NOP, MIR_ARG, MIR_NOP, MIR_UNARY, MIR_ARG, MIR_CALL, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL,
        MIR_NOP, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP, MIR_ARG,
        MIR_NOP, MIR_UNARY, MIR_ARG, MIR_CALL, MIR_ARG, MIR_CALL, MIR_NOP, MIR_ARG,
        MIR_NOP, MIR_UNARY, MIR_ARG, MIR_CALL, MIR_ARG, MIR_CALL, MIR_NOP, MIR_ARG,
        MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP, MIR_ARG, MIR_NOP, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_STORE, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST,
        MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP, MIR_PHI, MIR_NOP,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_UNARY, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP, MIR_ARG, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_ARG, MIR_NOP, MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_NOP, MIR_ARG, MIR_NOP, MIR_UNARY, MIR_ARG, MIR_CALL,
        MIR_ARG, MIR_CALL, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_ARG, MIR_NOP, MIR_UNARY,
        MIR_ARG, MIR_CALL, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL
    };
    static const int edges[][2] = {
        {22, 26}, {38, 91}, {71, 83}, {90, 30}, {110, 114},
        {127, 175}, {147, 159}, {174, 118}, {191, 195},
        {208, 284}, {216, 276}, {262, 274}, {283, 199},
        {303, 307}, {323, 404}, {356, 368}, {376, 387},
        {386, 396}, {403, 313}
    };
    static const int print_calls[6] = {6, 78, 154, 269, 363, 415};
    static const int print_argument_counts[6] = {2, 3, 3, 3, 3, 2};
    static const int string_instructions[20] = {
        7, 23, 72, 79, 98, 111, 148, 155,
        179, 192, 227, 263, 270, 291, 304, 334,
        357, 364, 408, 411
    };
    static const int buffer_addresses[4] = {57, 133, 248, 342};
    struct Sym *function;
    struct Sym *root;
    long offset;
    int edge;
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 416 || mir_cfg_block_count() != 25 ||
        mir.local_bytes != 12 || mir.aggregate_temp_bytes != 0 ||
        mir.has_vla || !mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "file-roundtrip-schedule", "opcodes");
    for (edge = 0;
         edge < (int)(sizeof(edges) / sizeof(edges[0])); ++edge)
        if (mir.insns[edges[edge][0]].label !=
            mir.insns[edges[edge][1]].label)
            return mir_machine_reject(
                "file-roundtrip-schedule", "control-flow");
    if (mir.insns[1].opcode != MIR_PARAM ||
        type_ptr_depth(mir.insns[1].type) != 0 ||
        (mir.insns[1].type & 15) != TYPE_INT ||
        type_size(mir.insns[1].type) != 2 ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->size_stack_offset))
        return mir_machine_reject(
            "file-roundtrip-schedule", "parameter");
    for (item = 0; item < 4; ++item) {
        if (!mir_machine_global_address_offset(
                mir.insns[buffer_addresses[item]].dst,
                &root, &offset, 0) ||
            root == NULL || offset < -32768 || offset > 32767 ||
            (item == 0
                 ? (plan->buffer = root,
                    plan->buffer_offset = (int)offset, 0)
                 : root != plan->buffer ||
                   offset != plan->buffer_offset))
            return mir_machine_reject(
                "file-roundtrip-schedule", "buffer");
    }
    for (item = 0; item < 20; ++item) {
        if (mir.insns[string_instructions[item]].immediate < 0)
            return mir_machine_reject(
                "file-roundtrip-schedule", "strings");
        plan->strings[item] =
            (int)mir.insns[string_instructions[item]].immediate;
    }
    if (plan->strings[0] != plan->strings[4] ||
        plan->strings[0] != plan->strings[8] ||
        plan->strings[0] != plan->strings[13] ||
    plan->strings[0] != plan->strings[18] ||
    plan->strings[6] != plan->strings[16] ||
        !mir_machine_constant_equals(mir.insns[13].dst, 578) ||
        !mir_machine_constant_equals(mir.insns[15].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[36].dst, 256) ||
        !mir_machine_constant_equals(mir.insns[87].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[100].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[102].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[125].dst, 256) ||
        !mir_machine_constant_equals(mir.insns[171].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[181].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[183].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[206].dst, 256) ||
        !mir_machine_constant_equals(mir.insns[211].dst, 7) ||
        !mir_machine_constant_equals(mir.insns[214].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[280].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[293].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[295].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[311].dst, 255) ||
        !mir_machine_constant_equals(mir.insns[321].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[371].dst, 7) ||
        !mir_machine_constant_equals(mir.insns[374].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[400].dst, 1))
        return mir_machine_reject(
            "file-roundtrip-schedule", "constants");
    if (mir.insns[2].immediate < 0)
        return mir_machine_reject(
            "file-roundtrip-schedule", "start-string");
    plan->start_string_id = (int)mir.insns[2].immediate;
    for (item = 0; item < 6; ++item) {
        if (!mir_roundtrip_call(
                print_calls[item], print_argument_counts[item],
                1, &function) ||
            (item == 0
                 ? (plan->print_function = function, 0)
                 : function != plan->print_function))
            return mir_machine_reject(
                "file-roundtrip-schedule", "print-functions");
        snprintf(plan->print_names[item],
                 sizeof(plan->print_names[item]), "%s",
                 mir.insns[print_calls[item]].base_name);
    }
#define MIR_ROUNDTRIP_FUNCTION(INDEX, ARGS, FIELD) \
    do { \
        if (!mir_roundtrip_call( \
                (INDEX), (ARGS), 0, &function) || \
            (plan->FIELD != NULL && plan->FIELD != function)) \
            return mir_machine_reject( \
                "file-roundtrip-schedule", #FIELD); \
        plan->FIELD = function; \
    } while (0)
    {
        static const int open_calls[] = {17, 104, 185, 297};
        int open_arguments[3];

        for (item = 0; item < 4; ++item) {
            const struct MirInsn *call =
                &mir.insns[open_calls[item]];
            function = find_global(call->name);
            if (function == NULL || function->storage != SC_FUNC ||
                function->is_funcptr || call->src1 >= 0 ||
                call->memory_flags != 0 ||
                !mir_machine_call_arguments(
                    call, 3, open_arguments) ||
                type_ptr_depth(function->type) != 0 ||
                (function->type & 15) != TYPE_INT ||
                type_size(function->type) != 2 ||
                (plan->open_function != NULL &&
                 plan->open_function != function))
                return mir_machine_reject(
                    "file-roundtrip-schedule", "open-function");
            plan->open_function = function;
        }
    }
    MIR_ROUNDTRIP_FUNCTION(25, 1, fail_function);
    MIR_ROUNDTRIP_FUNCTION(81, 1, fail_function);
    MIR_ROUNDTRIP_FUNCTION(113, 1, fail_function);
    MIR_ROUNDTRIP_FUNCTION(157, 1, fail_function);
    MIR_ROUNDTRIP_FUNCTION(194, 1, fail_function);
    MIR_ROUNDTRIP_FUNCTION(272, 1, fail_function);
    MIR_ROUNDTRIP_FUNCTION(306, 1, fail_function);
    MIR_ROUNDTRIP_FUNCTION(366, 1, fail_function);
    MIR_ROUNDTRIP_FUNCTION(44, 1, pattern_function);
    MIR_ROUNDTRIP_FUNCTION(52, 1, pattern_function);
    MIR_ROUNDTRIP_FUNCTION(165, 1, pattern_function);
    MIR_ROUNDTRIP_FUNCTION(393, 1, pattern_function);
    MIR_ROUNDTRIP_FUNCTION(235, 1, reverse_pattern_function);
    MIR_ROUNDTRIP_FUNCTION(243, 1, reverse_pattern_function);
    MIR_ROUNDTRIP_FUNCTION(383, 1, reverse_pattern_function);
    MIR_ROUNDTRIP_FUNCTION(46, 2, fill_function);
    MIR_ROUNDTRIP_FUNCTION(237, 2, fill_function);
    MIR_ROUNDTRIP_FUNCTION(130, 1, clear_function);
    MIR_ROUNDTRIP_FUNCTION(339, 1, clear_function);
    MIR_ROUNDTRIP_FUNCTION(54, 2, check_function);
    MIR_ROUNDTRIP_FUNCTION(167, 2, check_function);
    MIR_ROUNDTRIP_FUNCTION(245, 2, check_function);
    MIR_ROUNDTRIP_FUNCTION(385, 2, check_function);
    MIR_ROUNDTRIP_FUNCTION(395, 2, check_function);
    MIR_ROUNDTRIP_FUNCTION(65, 3, write_function);
    MIR_ROUNDTRIP_FUNCTION(256, 3, write_function);
    MIR_ROUNDTRIP_FUNCTION(141, 3, read_function);
    MIR_ROUNDTRIP_FUNCTION(350, 3, read_function);
    MIR_ROUNDTRIP_FUNCTION(94, 1, sync_function);
    MIR_ROUNDTRIP_FUNCTION(287, 1, sync_function);
    MIR_ROUNDTRIP_FUNCTION(97, 1, close_function);
    MIR_ROUNDTRIP_FUNCTION(178, 1, close_function);
    MIR_ROUNDTRIP_FUNCTION(290, 1, close_function);
    MIR_ROUNDTRIP_FUNCTION(407, 1, close_function);
    MIR_ROUNDTRIP_FUNCTION(229, 3, seek_function);
    MIR_ROUNDTRIP_FUNCTION(336, 3, seek_function);
    MIR_ROUNDTRIP_FUNCTION(410, 1, unlink_function);
#undef MIR_ROUNDTRIP_FUNCTION
    return 1;
}

static int mir_match_intel_hex_load_schedule(
    struct MirIntelHexLoadSchedule *plan)
{
    static const unsigned char expected_opcodes[162] = {
        MIR_LABEL, MIR_PARAM, MIR_LABEL, MIR_LOAD, MIR_ADDRESS, MIR_ARG, MIR_CONST, MIR_CONST,
        MIR_BINARY, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE, MIR_LOAD,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_LOAD, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_UNARY, MIR_NOP, MIR_STORE, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_UNARY,
        MIR_NOP, MIR_STORE, MIR_CONST, MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_LOAD,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP, MIR_NOP, MIR_UNARY, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_BINARY,
        MIR_CONST, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_UNARY, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_NOP, MIR_STORE_INDIRECT, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_JUMP, MIR_LABEL,
        MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_CONST, MIR_RETURN
    };
    static const int edges[][2] = {
        {16, 27}, {23, 27}, {26, 29}, {31, 155},
        {40, 44}, {78, 81}, {80, 155}, {86, 90},
        {105, 151}, {109, 113}, {144, 94}, {147, 151},
        {150, 155}, {154, 2}
    };
    struct Sym *function;
    int edge;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 162 || mir_cfg_block_count() != 17 ||
        mir.local_bytes != 128 || mir.aggregate_temp_bytes != 0 ||
        mir.has_vla || !mir_has_cfg_backedge() ||
        type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_BOOL)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "intel-hex-load-schedule", "opcodes");
    for (edge = 0;
         edge < (int)(sizeof(edges) / sizeof(edges[0])); ++edge)
        if (mir.insns[edges[edge][0]].label !=
            mir.insns[edges[edge][1]].label)
            return mir_machine_reject(
                "intel-hex-load-schedule", "control-flow");
    if (mir.insns[1].opcode != MIR_PARAM ||
        type_ptr_depth(mir.insns[1].type) != 1 ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->file_stack_offset) ||
        !mir_machine_constant_equals(mir.insns[6].dst, 120) ||
        !mir_machine_constant_equals(mir.insns[7].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[20].dst, 11) ||
        !mir_machine_constant_equals(mir.insns[33].dst, ':') ||
        !mir_machine_constant_equals(mir.insns[46].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[49].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[56].dst, 3) ||
        !mir_machine_constant_equals(mir.insns[59].dst, 4) ||
        !mir_machine_constant_equals(mir.insns[65].dst, 7) ||
        !mir_machine_constant_equals(mir.insns[68].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[74].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[82].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[92].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[115].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[120].dst, 9) ||
        !mir_machine_constant_equals(mir.insns[123].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[141].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[160].dst, 1))
        return mir_machine_reject(
            "intel-hex-load-schedule", "constants");
    if (mir.insns[41].immediate < 0 ||
        mir.insns[87].immediate < 0 ||
        mir.insns[110].immediate < 0)
        return mir_machine_reject(
            "intel-hex-load-schedule", "strings");
    plan->strings[0] = (int)mir.insns[41].immediate;
    plan->strings[1] = (int)mir.insns[87].immediate;
    plan->strings[2] = (int)mir.insns[110].immediate;
#define MIR_INTEL_CALL(INDEX, ARGS, FIELD) \
    do { \
        if (!mir_roundtrip_call( \
                (INDEX), (ARGS), 0, &function) || \
            (plan->FIELD != NULL && plan->FIELD != function)) \
            return mir_machine_reject( \
                "intel-hex-load-schedule", #FIELD); \
        plan->FIELD = function; \
    } while (0)
    MIR_INTEL_CALL(12, 3, line_function);
    MIR_INTEL_CALL(19, 1, length_function);
    MIR_INTEL_CALL(43, 1, error_function);
    MIR_INTEL_CALL(89, 1, error_function);
    MIR_INTEL_CALL(112, 1, error_function);
    MIR_INTEL_CALL(51, 2, hex_function);
    MIR_INTEL_CALL(61, 2, hex_function);
    MIR_INTEL_CALL(70, 2, hex_function);
    MIR_INTEL_CALL(125, 2, hex_function);
    MIR_INTEL_CALL(108, 1, eof_function);
    MIR_INTEL_CALL(134, 1, memory_function);
    MIR_INTEL_CALL(158, 1, close_function);
#undef MIR_INTEL_CALL
    return 1;
}

static void mir_roundtrip_cleanup(MirStream *out, int words)
{
    while (words-- > 0)
        mir_stream_puts("\tpop bc\n", out);
}

static void mir_intel_buffer_address(MirStream *out, int offset)
{
    mir_stream_puts("\tpush ix\n\tpop hl\n", out);
    mir_stream_printf(out, "\tld de,%d\n\tadd hl,de\n", -120 + offset);
}

static void mir_intel_error(
    MirStream *out, const struct MirIntelHexLoadSchedule *plan,
    int string_id)
{
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_machine_emit_symbol_call(out, plan->error_function);
    mir_stream_puts("\tpop bc\n", out);
}

static void mir_intel_read_hex(
    MirStream *out, const struct MirIntelHexLoadSchedule *plan,
    int offset, int length)
{
    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n", length);
    mir_intel_buffer_address(out, offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->hex_function);
    mir_roundtrip_cleanup(out, 2);
}

static void mir_emit_intel_hex_load_schedule(
    MirStream *out, const struct MirIntelHexLoadSchedule *plan)
{
    int loop = new_label();
    int done = new_label();
    int header_ok = new_label();
    int data_record = new_label();
    int data_loop = new_label();
    int data_done = new_label();
    int eof_ok = new_label();

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n"
          ";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-126\n\tadd hl,sp\n\tld sp,hl\n",
          out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tpush hl\n\tpop iy\nL%d:\n"
            "\tpush iy\n\tld hl,120\n\tpush hl\n",
            plan->file_stack_offset + 4,
            plan->file_stack_offset + 5, loop);
    mir_intel_buffer_address(out, 0);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->line_function);
    mir_roundtrip_cleanup(out, 3);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", done);
    mir_intel_buffer_address(out, 0);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->length_function);
    mir_stream_puts("\tpop bc\n\tld de,11\n\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp c,L%d\n", done);
    mir_stream_puts("\tld a,(ix-120)\n\tcp ':'\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", header_ok);
    mir_intel_error(out, plan, plan->strings[0]);
    mir_stream_printf(out, "L%d:\n", header_ok);

    mir_intel_read_hex(out, plan, 1, 2);
    mir_stream_puts("\tld (ix-122),l\n", out);
    mir_intel_read_hex(out, plan, 3, 4);
    mir_stream_puts("\tld (ix-124),l\n\tld (ix-123),h\n", out);
    mir_intel_read_hex(out, plan, 7, 2);
    mir_stream_puts("\tld a,l\n\tcp 1\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n\tor a\n\tjp z,L%d\n",
            done, data_record);
    mir_intel_error(out, plan, plan->strings[1]);
    mir_stream_printf(out,
            "L%d:\n\txor a\n\tld (ix-125),a\n"
            "L%d:\n\tld a,(ix-125)\n\tld b,a\n"
            "\tld a,(ix-122)\n\tcp b\n\tjp z,L%d\n"
            "\tpush iy\n",
            data_record, data_loop, data_done);
    mir_machine_emit_symbol_call(out, plan->eof_function);
    mir_stream_puts("\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", eof_ok);
    mir_intel_error(out, plan, plan->strings[2]);
    mir_stream_printf(out, "L%d:\n", eof_ok);
    mir_stream_puts("\tld a,(ix-125)\n\tadd a,a\n\tadd a,9\n"
          "\tld c,a\n\tld hl,2\n\tpush hl\n", out);
    mir_intel_buffer_address(out, 0);
    mir_stream_puts("\tld e,c\n\tld d,0\n\tadd hl,de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->hex_function);
    mir_roundtrip_cleanup(out, 2);
    mir_stream_puts("\tld (ix-126),l\n"
          "\tld l,(ix-124)\n\tld h,(ix-123)\n"
          "\tld e,(ix-125)\n\tld d,0\n\tadd hl,de\n"
          "\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->memory_function);
    mir_stream_puts("\tpop bc\n\tld a,(ix-126)\n\tld (hl),a\n"
          "\tld a,(ix-125)\n\tinc a\n\tld (ix-125),a\n", out);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n\tjp L%d\n",
            data_loop, data_done, loop);
    mir_stream_printf(out, "L%d:\n\tpush iy\n", done);
    mir_machine_emit_symbol_call(out, plan->close_function);
    mir_stream_puts("\tpop bc\n\tld hl,1\n"
          "\tld sp,ix\n\tpop ix\n\tpop iy\n"
          ";@dcc.reg free=iy\n\tret\n", out);
}

static void mir_roundtrip_load_ix_word(
    MirStream *out, int offset)
{
    mir_stream_printf(out, "\tld l,(ix%d)\n\tld h,(ix%d)\n",
            offset, offset + 1);
}

static void mir_roundtrip_store_ix_word(
    MirStream *out, int offset)
{
    mir_stream_printf(out, "\tld (ix%d),l\n\tld (ix%d),h\n",
            offset, offset + 1);
}

static void mir_roundtrip_push_size(MirStream *out)
{
    mir_stream_puts("\tpush iy\n", out);
}

static void mir_roundtrip_push_buffer(
    MirStream *out, const struct MirFileRoundtripSchedule *plan)
{
    mir_machine_emit_global_address_de(
        out, plan->buffer, plan->buffer_offset);
    mir_stream_puts("\tpush de\n", out);
}

static void mir_roundtrip_call_one_ix(
    MirStream *out, struct Sym *function, int offset)
{
    mir_roundtrip_load_ix_word(out, offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, function);
    mir_stream_puts("\tpop bc\n", out);
}

static void mir_roundtrip_call_pattern(
    MirStream *out, struct Sym *function)
{
    mir_roundtrip_call_one_ix(out, function, -4);
    mir_roundtrip_store_ix_word(out, -8);
}

static void mir_roundtrip_call_fill_check(
    MirStream *out, struct Sym *function)
{
    mir_roundtrip_load_ix_word(out, -8);
    mir_stream_puts("\tpush hl\n", out);
    mir_roundtrip_push_size(out);
    mir_machine_emit_symbol_call(out, function);
    mir_roundtrip_cleanup(out, 2);
}

static void mir_roundtrip_call_io(
    MirStream *out, const struct MirFileRoundtripSchedule *plan,
    struct Sym *function)
{
    mir_roundtrip_push_size(out);
    mir_roundtrip_push_buffer(out, plan);
    mir_roundtrip_load_ix_word(out, -2);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, function);
    mir_roundtrip_cleanup(out, 3);
    mir_roundtrip_store_ix_word(out, -6);
}

static void mir_roundtrip_call_fail(
    MirStream *out, const struct MirFileRoundtripSchedule *plan,
    int string_id)
{
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_machine_emit_symbol_call(out, plan->fail_function);
    mir_stream_puts("\tpop bc\n", out);
}

static void mir_roundtrip_open(
    MirStream *out, const struct MirFileRoundtripSchedule *plan,
    int flags, int failure_string)
{
    int opened = new_label();

    mir_stream_puts("\tld hl,0\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n", flags);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[0]);
    mir_machine_emit_symbol_call(out, plan->open_function);
    mir_roundtrip_cleanup(out, 3);
    mir_roundtrip_store_ix_word(out, -2);
    mir_stream_puts("\tbit 7,h\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", opened);
    mir_roundtrip_call_fail(out, plan, failure_string);
    mir_stream_printf(out, "L%d:\n", opened);
}

static void mir_roundtrip_print_failure(
    MirStream *out, const struct MirFileRoundtripSchedule *plan,
    int print_index, int format_string)
{
    mir_stream_puts("\tld hl,0\n\tpush hl\n", out);
    mir_roundtrip_load_ix_word(out, -4);
    mir_stream_puts("\tpush hl\n", out);
    mir_roundtrip_load_ix_word(out, -6);
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", format_string);
    mir_emit_runtime_call(out, plan->print_names[print_index]);
    mir_roundtrip_cleanup(out, 4);
}

static void mir_roundtrip_multiply_offset(MirStream *out)
{
    mir_roundtrip_load_ix_word(out, -4);
    mir_stream_puts("\tld de,0\n\tpush de\n\tpush hl\n"
          "\tpush iy\n\tpop hl\n\tld a,h\n\trlca\n\tsbc a,a\n"
          "\tld d,a\n\tld e,a\n", out);
    mir_emit_runtime_call(out, "__lmul");
    mir_roundtrip_cleanup(out, 2);
}

static void mir_roundtrip_seek(
    MirStream *out, const struct MirFileRoundtripSchedule *plan,
    int where_string)
{
    mir_roundtrip_multiply_offset(out);
    mir_stream_printf(out, "\tld bc,S%d\n\tpush bc\n", where_string);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_roundtrip_load_ix_word(out, -2);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->seek_function);
    mir_roundtrip_cleanup(out, 4);
}

static void mir_emit_file_roundtrip_schedule(
    MirStream *out, const struct MirFileRoundtripSchedule *plan)
{
    int first_loop = new_label();
    int first_done = new_label();
    int first_ok = new_label();
    int second_loop = new_label();
    int second_done = new_label();
    int second_ok = new_label();
    int third_loop = new_label();
    int third_done = new_label();
    int third_skip = new_label();
    int third_ok = new_label();
    int fourth_loop = new_label();
    int fourth_done = new_label();
    int fourth_ok = new_label();
    int use_pattern = new_label();
    int value_ready = new_label();

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n"
          ";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-8\n\tadd hl,sp\n\tld sp,hl\n",
          out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tpush hl\n\tpop iy\n\tpush iy\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->size_stack_offset + 4,
            plan->size_stack_offset + 5,
            plan->start_string_id);
    mir_emit_runtime_call(out, plan->print_names[0]);
    mir_roundtrip_cleanup(out, 2);
    mir_roundtrip_open(out, plan, 578, plan->strings[1]);

    mir_stream_puts("\tld hl,0\n", out);
    mir_roundtrip_store_ix_word(out, -4);
    mir_stream_printf(out, "L%d:\n", first_loop);
    mir_stream_puts("\tld a,(ix-3)\n\tor a\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", first_done);
    mir_roundtrip_call_pattern(out, plan->pattern_function);
    mir_roundtrip_call_fill_check(out, plan->fill_function);
    mir_roundtrip_call_fill_check(out, plan->check_function);
    mir_roundtrip_call_io(out, plan, plan->write_function);
    mir_roundtrip_load_ix_word(out, -6);
    mir_stream_puts("\tpush iy\n\tpop de\n\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", first_ok);
    mir_roundtrip_print_failure(out, plan, 1, plan->strings[2]);
    mir_roundtrip_call_fail(out, plan, plan->strings[3]);
    mir_stream_printf(out, "L%d:\n", first_ok);
    mir_roundtrip_load_ix_word(out, -4);
    mir_stream_puts("\tinc hl\n", out);
    mir_roundtrip_store_ix_word(out, -4);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", first_loop, first_done);
    mir_roundtrip_call_one_ix(out, plan->sync_function, -2);
    mir_roundtrip_call_one_ix(out, plan->close_function, -2);

    mir_roundtrip_open(out, plan, 0, plan->strings[5]);
    mir_stream_puts("\tld hl,0\n", out);
    mir_roundtrip_store_ix_word(out, -4);
    mir_stream_printf(out, "L%d:\n", second_loop);
    mir_stream_puts("\tld a,(ix-3)\n\tor a\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", second_done);
    mir_roundtrip_push_size(out);
    mir_machine_emit_symbol_call(out, plan->clear_function);
    mir_stream_puts("\tpop bc\n", out);
    mir_roundtrip_call_io(out, plan, plan->read_function);
    mir_roundtrip_load_ix_word(out, -6);
    mir_stream_puts("\tpush iy\n\tpop de\n\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", second_ok);
    mir_roundtrip_print_failure(out, plan, 2, plan->strings[6]);
    mir_roundtrip_call_fail(out, plan, plan->strings[7]);
    mir_stream_printf(out, "L%d:\n", second_ok);
    mir_roundtrip_call_pattern(out, plan->pattern_function);
    mir_roundtrip_call_fill_check(out, plan->check_function);
    mir_roundtrip_load_ix_word(out, -4);
    mir_stream_puts("\tinc hl\n", out);
    mir_roundtrip_store_ix_word(out, -4);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", second_loop, second_done);
    mir_roundtrip_call_one_ix(out, plan->close_function, -2);

    mir_roundtrip_open(out, plan, 2, plan->strings[9]);
    mir_stream_puts("\tld hl,0\n", out);
    mir_roundtrip_store_ix_word(out, -4);
    mir_stream_printf(out, "L%d:\n", third_loop);
    mir_stream_puts("\tld a,(ix-3)\n\tor a\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", third_done);
    mir_stream_puts("\tld a,(ix-4)\n\tand 7\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", third_skip);
    mir_roundtrip_seek(out, plan, plan->strings[10]);
    mir_roundtrip_call_pattern(
        out, plan->reverse_pattern_function);
    mir_roundtrip_call_fill_check(out, plan->fill_function);
    mir_roundtrip_call_fill_check(out, plan->check_function);
    mir_roundtrip_call_io(out, plan, plan->write_function);
    mir_roundtrip_load_ix_word(out, -6);
    mir_stream_puts("\tpush iy\n\tpop de\n\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", third_ok);
    mir_roundtrip_print_failure(out, plan, 3, plan->strings[11]);
    mir_roundtrip_call_fail(out, plan, plan->strings[12]);
    mir_stream_printf(out, "L%d:\n", third_ok);
    mir_stream_printf(out, "L%d:\n", third_skip);
    mir_roundtrip_load_ix_word(out, -4);
    mir_stream_puts("\tinc hl\n", out);
    mir_roundtrip_store_ix_word(out, -4);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", third_loop, third_done);
    mir_roundtrip_call_one_ix(out, plan->sync_function, -2);
    mir_roundtrip_call_one_ix(out, plan->close_function, -2);

    mir_roundtrip_open(out, plan, 0, plan->strings[14]);
    mir_stream_puts("\tld hl,255\n", out);
    mir_roundtrip_store_ix_word(out, -4);
    mir_stream_printf(out, "L%d:\n", fourth_loop);
    mir_stream_puts("\tbit 7,(ix-3)\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", fourth_done);
    mir_roundtrip_seek(out, plan, plan->strings[15]);
    mir_roundtrip_push_size(out);
    mir_machine_emit_symbol_call(out, plan->clear_function);
    mir_stream_puts("\tpop bc\n", out);
    mir_roundtrip_call_io(out, plan, plan->read_function);
    mir_roundtrip_load_ix_word(out, -6);
    mir_stream_puts("\tpush iy\n\tpop de\n\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", fourth_ok);
    mir_roundtrip_print_failure(out, plan, 4, plan->strings[16]);
    mir_roundtrip_call_fail(out, plan, plan->strings[17]);
    mir_stream_printf(out, "L%d:\n", fourth_ok);
    mir_stream_puts("\tld a,(ix-4)\n\tand 7\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", use_pattern);
    mir_roundtrip_call_pattern(
        out, plan->reverse_pattern_function);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", value_ready, use_pattern);
    mir_roundtrip_call_pattern(out, plan->pattern_function);
    mir_stream_printf(out, "L%d:\n", value_ready);
    mir_roundtrip_call_fill_check(out, plan->check_function);
    mir_roundtrip_load_ix_word(out, -4);
    mir_stream_puts("\tdec hl\n", out);
    mir_roundtrip_store_ix_word(out, -4);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", fourth_loop, fourth_done);
    mir_roundtrip_call_one_ix(out, plan->close_function, -2);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[18]);
    mir_machine_emit_symbol_call(out, plan->unlink_function);
    mir_stream_puts("\tpop bc\n\tpush iy\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[19]);
    mir_emit_runtime_call(out, plan->print_names[5]);
    mir_roundtrip_cleanup(out, 2);
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tpop iy\n"
          ";@dcc.reg free=iy\n\tret\n", out);
}

static void mir_emit_room_message(
    MirStream *out, const struct MirRoomResolutionSchedule *plan,
    int string_id)
{
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_emit_runtime_call(out, plan->print_name);
    mir_stream_puts("\tpop bc\n", out);
    mir_machine_emit_symbol_call(out, plan->flush_function);
}

static void mir_emit_room_compare(
    MirStream *out, const struct MirRoomResolutionSchedule *plan,
    int index, int target)
{
    mir_stream_printf(out,
            "\tld e,(iy%+d)\n\tld d,(iy%+d)\n"
            "\tld l,(iy%+d)\n\tld h,(iy%+d)\n"
            "\tor a\n\tsbc hl,de\n\tjp z,L%d\n",
            plan->location_offset,
            plan->location_offset + 1,
            plan->location_offset + index * 2,
            plan->location_offset + index * 2 + 1,
            target);
}

static void mir_emit_room_resolution_schedule(
    MirStream *out, const struct MirRoomResolutionSchedule *plan)
{
    int loop = new_label();
    int hazard = new_label();
    int pit = new_label();
    int bat = new_label();
    int done = new_label();

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n"
          ";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tpush de\n\tpop iy\nL%d:\n",
            plan->game_stack_offset + 2, loop);
    mir_emit_room_compare(out, plan, 1, hazard);
    mir_emit_room_compare(out, plan, 2, pit);
    mir_emit_room_compare(out, plan, 3, pit);
    mir_emit_room_compare(out, plan, 4, bat);
    mir_emit_room_compare(out, plan, 5, bat);
    mir_stream_printf(out, "\tld hl,0\n\tjp L%d\nL%d:\n",
            done, hazard);
    mir_emit_room_message(out, plan, plan->string_ids[0]);
    mir_stream_puts("\tpush iy\n", out);
    mir_machine_emit_symbol_call(out, plan->wake_function);
    mir_stream_puts("\tpop bc\n", out);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", done, pit);
    mir_emit_room_message(out, plan, plan->string_ids[1]);
    mir_stream_printf(out, "\tld hl,2\n\tjp L%d\nL%d:\n", done, bat);
    mir_emit_room_message(out, plan, plan->string_ids[2]);
    mir_machine_emit_symbol_call(out, plan->random_room_function);
    mir_stream_printf(out,
            "\tld (iy%+d),l\n\tld (iy%+d),h\n\tjp L%d\n"
            "L%d:\n\tpop iy\n;@dcc.reg free=iy\n\tret\n",
            plan->location_offset, plan->location_offset + 1,
            loop, done);
}

static int mir_match_arrow_path_schedule(
    struct MirArrowPathSchedule *plan)
{
    static const unsigned char expected_opcodes[131] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_NOP, MIR_STORE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_LOAD,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP, MIR_LOAD, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_ARG, MIR_LOAD, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_STORE,
        MIR_JUMP, MIR_LABEL, MIR_ADDRESS, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_PHI, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_CALL, MIR_CONST, MIR_RETURN, MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT,
        MIR_CALL, MIR_CONST, MIR_RETURN, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_CALL, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_CALL, MIR_CONST, MIR_RETURN, MIR_NOP, MIR_LABEL, MIR_LOAD,
        MIR_ARG, MIR_CALL, MIR_RETURN
    };
    static const int edges[][2] = {
        {23, 100}, {32, 41}, {40, 52}, {60, 68},
        {76, 92}, {99, 14}, {118, 126}
    };
    static const int game_loads[] = {
        4, 15, 54, 70, 80, 82, 101, 103, 113, 127
    };
    static const int location_members[] = {5, 55, 71};
    static const int arrow_members[] = {81, 83, 102, 104, 114};
    static const int print_calls[] = {63, 79, 111, 121};
    static const int print_strings[] = {61, 77, 109, 119};
    static const int flush_calls[] = {64, 88, 112, 122};
    const struct MirInsn *game = &mir.insns[1];
    const struct MirInsn *path = &mir.insns[2];
    const struct MirInsn *length = &mir.insns[3];
    struct Sym *print_function = NULL;
    struct Sym *flush_function = NULL;
    long cave_offset;
    int edge;
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 131 || mir_cfg_block_count() != 10 ||
        mir.local_bytes != 4 || mir.aggregate_temp_bytes != 0 ||
        mir.has_vla || !mir_has_cfg_backedge() ||
        type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "arrow-path-schedule", "opcodes");
        if ((insn->opcode == MIR_LOAD_INDIRECT ||
             insn->opcode == MIR_STORE_INDIRECT) &&
            (insn->memory_flags & (1 | 8)) != 0)
            return mir_machine_reject(
                "arrow-path-schedule", "volatile-memory");
    }
    for (edge = 0;
         edge < (int)(sizeof(edges) / sizeof(edges[0])); ++edge)
        if (mir.insns[edges[edge][0]].label !=
            mir.insns[edges[edge][1]].label)
            return mir_machine_reject(
                "arrow-path-schedule", "control-flow");
    if (type_ptr_depth(game->type) != 1 ||
        type_ptr_depth(path->type) != 1 ||
        type_ptr_depth(length->type) != 0 ||
        type_size(game->type) != 2 ||
        type_size(path->type) != 2 ||
        type_size(length->type) != 2 ||
        mir_machine_pointee_is_volatile(game) ||
        mir_machine_pointee_is_volatile(path) ||
        !mir_machine_parameter_value_offset(
            game->dst, &plan->game_stack_offset) ||
        !mir_machine_parameter_value_offset(
            path->dst, &plan->path_stack_offset) ||
        !mir_machine_parameter_value_offset(
            length->dst, &plan->length_stack_offset))
        return mir_machine_reject(
            "arrow-path-schedule", "parameters");
    for (item = 0;
         item < (int)(sizeof(game_loads) /
                      sizeof(game_loads[0])); ++item)
        if (!mir_machine_same_location(
                game, &mir.insns[game_loads[item]]))
            return mir_machine_reject(
                "arrow-path-schedule", "game-loads");
    plan->location_offset = (int)mir.insns[5].immediate;
    plan->arrows_offset = (int)mir.insns[81].immediate;
    if (plan->location_offset < -120 ||
        plan->location_offset > 116 ||
        plan->arrows_offset < -128 ||
        plan->arrows_offset > 126)
        return mir_machine_reject(
            "arrow-path-schedule", "member-offsets");
    for (item = 0; item < 3; ++item)
        if (mir.insns[location_members[item]].immediate !=
                plan->location_offset ||
            mir.insns[location_members[item]].memory_size != 12)
            return mir_machine_reject(
                "arrow-path-schedule", "location-members");
    for (item = 0; item < 5; ++item)
        if (mir.insns[arrow_members[item]].immediate !=
                plan->arrows_offset ||
            mir.insns[arrow_members[item]].memory_size != 2)
            return mir_machine_reject(
                "arrow-path-schedule", "arrow-members");
    if (!mir_machine_global_address_offset(
            mir.insns[42].dst, &plan->cave, &cave_offset, 0) ||
        plan->cave == NULL || cave_offset < -32768 ||
        cave_offset > 32767 ||
        mir.insns[44].immediate != 6 ||
        mir.insns[48].immediate != 2 ||
        !mir_machine_constant_equals(mir.insns[6].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[11].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[45].dst, 3) ||
        !mir_machine_constant_equals(mir.insns[65].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[85].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[89].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[106].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[116].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[123].dst, 2))
        return mir_machine_reject(
            "arrow-path-schedule", "constants");
    plan->cave_offset = (int)cave_offset;
    if (!mir_arrow_direct_function(
            &mir.insns[31], 2, 0,
            &plan->adjacent_function) ||
        !mir_arrow_direct_function(
            &mir.insns[47], 1, 0,
            &plan->random_function) ||
        !mir_arrow_direct_function(
            &mir.insns[129], 1, 0,
            &plan->wake_function))
        return mir_machine_reject(
            "arrow-path-schedule", "value-calls");
    for (item = 0; item < 4; ++item) {
        struct Sym *candidate;

        if (!mir_arrow_direct_function(
                &mir.insns[print_calls[item]], 1, 1,
                &candidate) ||
            mir.insns[print_strings[item]].immediate < 0 ||
            mir.insns[print_strings[item] + 1].src1 !=
                mir.insns[print_strings[item]].dst ||
            (item == 0
                 ? (print_function = candidate, 0)
                 : candidate != print_function))
            return mir_machine_reject(
                "arrow-path-schedule", "print-calls");
        plan->string_ids[item] =
            (int)mir.insns[print_strings[item]].immediate;
    }
    for (item = 0; item < 4; ++item) {
        struct Sym *candidate;

        if (!mir_arrow_direct_function(
                &mir.insns[flush_calls[item]], 0, 0,
                &candidate) ||
            (item == 0
                 ? (flush_function = candidate, 0)
                 : candidate != flush_function))
            return mir_machine_reject(
                "arrow-path-schedule", "flush-calls");
    }
    plan->flush_function = flush_function;
    snprintf(plan->print_name, sizeof(plan->print_name), "%s",
             mir.insns[print_calls[0]].base_name);
    return 1;
}

static void mir_emit_arrow_print(
    MirStream *out, const struct MirArrowPathSchedule *plan,
    int string_id)
{
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_emit_runtime_call(out, plan->print_name);
    mir_stream_puts("\tpop bc\n", out);
    mir_machine_emit_symbol_call(out, plan->flush_function);
}

static void mir_emit_arrow_decrement(
    MirStream *out, const struct MirArrowPathSchedule *plan)
{
    mir_stream_printf(out,
            "\tld l,(iy%+d)\n\tld h,(iy%+d)\n\tdec hl\n"
            "\tld (iy%+d),l\n\tld (iy%+d),h\n",
            plan->arrows_offset, plan->arrows_offset + 1,
            plan->arrows_offset, plan->arrows_offset + 1);
}

static void mir_emit_arrow_path_schedule(
    MirStream *out, const struct MirArrowPathSchedule *plan)
{
    int loop = new_label();
    int random_room = new_label();
    int room_ready = new_label();
    int not_wumpus = new_label();
    int not_player = new_label();
    int missed = new_label();
    int no_arrows = new_label();
    int have_arrows = new_label();
    int return_win = new_label();
    int return_loss = new_label();
    int epilogue = new_label();

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n"
          ";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-6\n\tadd hl,sp\n\tld sp,hl\n",
          out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tpush hl\n\tpop iy\n"
            "\tld l,(iy%+d)\n\tld h,(iy%+d)\n"
            "\tld (ix-2),l\n\tld (ix-1),h\n"
            "\txor a\n\tld (ix-4),a\n\tld (ix-3),a\n"
            "L%d:\n"
            "\tld l,(ix-4)\n\tld h,(ix-3)\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n"
            "\tld a,h\n\txor 128\n\tld h,a\n"
            "\tld a,d\n\txor 128\n\tld d,a\n"
            "\tor a\n\tsbc hl,de\n\tjp nc,L%d\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld e,(ix-4)\n\tld d,(ix-3)\n"
            "\tadd hl,de\n\tadd hl,de\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld (ix-6),e\n\tld (ix-5),d\n"
            "\tpush de\n\tld l,(ix-2)\n\tld h,(ix-1)\n"
            "\tpush hl\n",
            plan->game_stack_offset + 4,
            plan->game_stack_offset + 5,
            plan->location_offset,
            plan->location_offset + 1,
            loop, missed,
            plan->length_stack_offset + 4,
            plan->length_stack_offset + 5,
            plan->path_stack_offset + 4,
            plan->path_stack_offset + 5);
    mir_machine_emit_symbol_call(out, plan->adjacent_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out,
            "\tjp z,L%d\n\tld l,(ix-6)\n\tld h,(ix-5)\n"
            "\tjp L%d\nL%d:\n\tld hl,3\n\tpush hl\n",
            random_room, room_ready, random_room);
    mir_machine_emit_symbol_call(out, plan->random_function);
    mir_stream_puts("\tpop bc\n\tadd hl,hl\n\tpush hl\n"
          "\tld l,(ix-2)\n\tld h,(ix-1)\n"
          "\tadd hl,hl\n\tld d,h\n\tld e,l\n"
          "\tadd hl,hl\n\tadd hl,de\n\tpush hl\n",
          out);
    mir_machine_emit_global_address_de(
        out, plan->cave, plan->cave_offset);
    mir_stream_puts("\tpop hl\n\tadd hl,de\n\tpop de\n\tadd hl,de\n"
          "\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n",
          out);
    mir_stream_printf(out,
            "L%d:\n\tld (ix-2),l\n\tld (ix-1),h\n"
            "\tld e,(iy%+d)\n\tld d,(iy%+d)\n"
            "\tor a\n\tsbc hl,de\n\tjp nz,L%d\n",
            room_ready,
            plan->location_offset + 2,
            plan->location_offset + 3,
            not_wumpus);
    mir_emit_arrow_print(out, plan, plan->string_ids[0]);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", return_win, not_wumpus);
    mir_stream_printf(out,
            "\tld l,(ix-2)\n\tld h,(ix-1)\n"
            "\tld e,(iy%+d)\n\tld d,(iy%+d)\n"
            "\tor a\n\tsbc hl,de\n\tjp nz,L%d\n",
            plan->location_offset,
            plan->location_offset + 1, not_player);
    mir_emit_arrow_print(out, plan, plan->string_ids[1]);
    mir_emit_arrow_decrement(out, plan);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", return_loss, not_player);
    mir_stream_puts("\tld l,(ix-4)\n\tld h,(ix-3)\n"
          "\tinc hl\n\tld (ix-4),l\n\tld (ix-3),h\n", out);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", loop, missed);
    mir_emit_arrow_decrement(out, plan);
    mir_emit_arrow_print(out, plan, plan->string_ids[2]);
    mir_stream_printf(out,
            "\tld l,(iy%+d)\n\tld h,(iy%+d)\n"
            "\tld a,h\n\tor a\n\tjp m,L%d\n"
            "\tor l\n\tjp nz,L%d\n",
            plan->arrows_offset, plan->arrows_offset + 1,
            no_arrows, have_arrows);
    mir_stream_printf(out, "L%d:\n", no_arrows);
    mir_emit_arrow_print(out, plan, plan->string_ids[3]);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", return_loss, have_arrows);
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n",
            plan->game_stack_offset + 4,
            plan->game_stack_offset + 5);
    mir_machine_emit_symbol_call(out, plan->wake_function);
    mir_stream_puts("\tpop bc\n", out);
    mir_stream_printf(out,
            "\tjp L%d\nL%d:\n\tld hl,1\n\tjp L%d\n"
            "L%d:\n\tld hl,2\nL%d:\n"
            "\tld sp,ix\n\tpop ix\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            epilogue, return_win, epilogue,
            return_loss, epilogue);
}

int mir_try_emit_call_runners(MirStream *out)
{
        struct MirInlineParameterCallSchedule inline_parameter;
        struct MirNullableStringCheckSchedule nullable_string;
        struct MirNullableStringFailureSchedule nullable_failure;
        struct MirConditionalParameterSchedule conditional_parameter;
        struct MirArgvPrintSchedule argv_print;
        struct MirListPrependSchedule list_prepend;
        struct MirListReverseSchedule list_reverse;
        struct MirStructReturnMemberSchedule struct_return_member;
        struct MirPointerTableRunnerSchedule pointer_table;
        struct MirAddressCheckRunnerSchedule address_checks;
        struct MirQualifierRunnerSchedule qualifier_runner;
        struct MirUnionValueRunnerSchedule union_value;
        struct MirStructValueRunnerSchedule struct_value;
        struct MirInlineNestRunnerSchedule inline_nest;
        struct MirZeroAllocationSchedule zero_allocation;
        struct MirAllocatorReuseSchedule allocator_reuse;
        struct MirAllocationGuardSchedule allocation_guard;
        struct MirAllocatorTrimSchedule allocator_trim;
        struct MirAllocatorCoalesceSchedule allocator_coalesce;
        struct MirBiosCallSchedule bios_calls;
        struct MirExecArgumentSchedule exec_arguments;
        struct MirTemporaryFileSchedule temporary_file;
        struct MirCallSafeWordLoopSchedule call_safe_loop;
        struct MirConstantDoWhileSchedule do_while_plan;
        struct MirArrowPathSchedule arrow_path_plan;
        struct MirRoomResolutionSchedule room_resolution_plan;
        struct MirGlobalMemsetSchedule memset_plan;
        struct MirWordTableRunnerSchedule table_runner_plan;
        struct MirSeekCheckSchedule seek_check_plan;
        struct MirFileRoundtripSchedule file_roundtrip_plan;
        struct MirIntelHexLoadSchedule intel_hex_plan;
        struct MirLegalMoveFilterSchedule legal_filter_plan;
        struct MirStatementVmSchedule statement_vm_plan;
        struct MirFormatWalkSchedule format_walk_plan;
        struct MirInlineCallSumSchedule inline_call_sum_plan;
        struct MirLongSubtractionRunner long_subtraction_plan;
        struct MirPromotionCallRunner promotion_plan;
        struct MirReadValidateRunner read_validate_plan;
        if (mir_match_temporary_file_schedule(&temporary_file)) {
            mir_emit_temporary_file_schedule(out, &temporary_file);
            return 1;
        }
        if (mir_match_exec_argument_schedule(&exec_arguments)) {
            mir_emit_exec_argument_schedule(out, &exec_arguments);
            return 1;
        }
        if (mir_match_bios_call_schedule(&bios_calls)) {
            mir_emit_bios_call_schedule(out, &bios_calls);
            return 1;
        }
        if (mir_match_allocator_bridge_schedule(
                &allocator_coalesce) ||
            mir_match_allocator_reverse_schedule(
                &allocator_coalesce)) {
            mir_emit_allocator_coalesce_schedule(
                out, &allocator_coalesce);
            return 1;
        }
        if (mir_match_allocator_trim_schedule(&allocator_trim)) {
            mir_emit_allocator_trim_schedule(out, &allocator_trim);
            return 1;
        }
        if (mir_match_allocation_guard_schedule(&allocation_guard)) {
            mir_emit_allocation_guard_schedule(out, &allocation_guard);
            return 1;
        }
        if (mir_match_allocator_reuse_schedule(&allocator_reuse)) {
            mir_emit_allocator_reuse_schedule(out, &allocator_reuse);
            return 1;
        }
        if (mir_match_zero_allocation_schedule(&zero_allocation)) {
            mir_emit_zero_allocation_schedule(out, &zero_allocation);
            return 1;
        }
        if (mir_match_nullable_string_check_schedule(
                &nullable_string)) {
            mir_emit_nullable_string_check_schedule(
                out, &nullable_string);
            return 1;
        }
        if (mir_match_nullable_string_failure_schedule(
                &nullable_failure)) {
            mir_emit_nullable_string_failure_schedule(
                out, &nullable_failure);
            return 1;
        }
        if (mir_match_conditional_parameter_schedule(
                &conditional_parameter)) {
            mir_emit_conditional_parameter_schedule(
                out, &conditional_parameter);
            return 1;
        }
        if (mir_match_argv_print_schedule(&argv_print)) {
            mir_emit_argv_print_schedule(out, &argv_print);
            return 1;
        }
        if (mir_match_list_prepend_schedule(&list_prepend)) {
            mir_emit_list_prepend_schedule(out, &list_prepend);
            return 1;
        }
        if (mir_match_list_reverse_schedule(&list_reverse)) {
            mir_emit_list_reverse_schedule(out, &list_reverse);
            return 1;
        }
        if (mir_match_struct_return_member_schedule(
                &struct_return_member)) {
            mir_emit_struct_return_member_schedule(
                out, &struct_return_member);
            return 1;
        }
        if (mir_match_pointer_table_runner_schedule(
                &pointer_table)) {
            mir_emit_pointer_table_runner_schedule(
                out, &pointer_table);
            return 1;
        }
        if (mir_match_address_check_runner_schedule(
                &address_checks)) {
            mir_emit_address_check_runner_schedule(
                out, &address_checks);
            return 1;
        }
        if (mir_match_qualifier_runner_schedule(
                &qualifier_runner)) {
            mir_emit_qualifier_runner_schedule(
                out, &qualifier_runner);
            return 1;
        }
        if (mir_match_union_value_runner_schedule(&union_value)) {
            mir_emit_union_value_runner_schedule(out, &union_value);
            return 1;
        }
        if (mir_match_struct_value_runner_schedule(&struct_value)) {
            mir_emit_struct_value_runner_schedule(out, &struct_value);
            return 1;
        }
        if (mir_match_inline_nest_runner_schedule(&inline_nest)) {
            mir_emit_inline_nest_runner_schedule(out, &inline_nest);
            return 1;
        }
        if (mir_match_inline_parameter_call_schedule(
                &inline_parameter)) {
            mir_emit_inline_parameter_call_schedule(
                out, &inline_parameter);
            return 1;
        }
        if (mir_match_call_safe_countdown_sum_schedule(
                &call_safe_loop) ||
            mir_match_call_safe_member_sum_schedule(
                &call_safe_loop)) {
            mir_emit_call_safe_word_loop_schedule(
                out, &call_safe_loop);
            return 1;
        }
        if (mir_match_constant_do_while_schedule(&do_while_plan)) {
            mir_emit_constant_do_while_schedule(
                out, &do_while_plan);
            return 1;
        }
        if (mir_match_arrow_path_schedule(&arrow_path_plan)) {
            mir_emit_arrow_path_schedule(out, &arrow_path_plan);
            return 1;
        }
        if (mir_match_room_resolution_schedule(
                &room_resolution_plan)) {
            mir_emit_room_resolution_schedule(
                out, &room_resolution_plan);
            return 1;
        }
        if (mir_match_global_memset_schedule(&memset_plan)) {
            mir_emit_global_memset_schedule(out, &memset_plan);
            return 1;
        }
        if (mir_match_word_table_runner_schedule(
                &table_runner_plan)) {
            mir_emit_word_table_runner_schedule(
                out, &table_runner_plan);
            return 1;
        }
        if (mir_match_seek_check_schedule(&seek_check_plan)) {
            mir_emit_seek_check_schedule(out, &seek_check_plan);
            return 1;
        }
        if (mir_match_file_roundtrip_schedule(
                &file_roundtrip_plan)) {
            mir_emit_file_roundtrip_schedule(
                out, &file_roundtrip_plan);
            return 1;
        }
        if (mir_match_intel_hex_load_schedule(
                &intel_hex_plan)) {
            mir_emit_intel_hex_load_schedule(
                out, &intel_hex_plan);
            return 1;
        }
        if (mir_match_legal_move_filter_schedule(
                &legal_filter_plan)) {
            mir_emit_legal_move_filter_schedule(
                out, &legal_filter_plan);
            return 1;
        }
        if (mir_match_statement_vm_schedule(&statement_vm_plan)) {
            mir_emit_statement_vm_schedule(out, &statement_vm_plan);
            return 1;
        }
        if (mir_match_format_walk_schedule(&format_walk_plan)) {
            mir_emit_format_walk_schedule(out, &format_walk_plan);
            return 1;
        }

        if (mir_match_inline_call_sum_schedule(
                &inline_call_sum_plan)) {
            mir_emit_inline_call_sum_schedule(
                out, &inline_call_sum_plan);
            return 1;
        }
        if (mir_match_long_subtraction_runner(
                &long_subtraction_plan)) {
            mir_emit_long_subtraction_runner(
                out, &long_subtraction_plan);
            return 1;
        }
        if (mir_match_promotion_call_runner(
                &promotion_plan)) {
            mir_emit_promotion_call_runner(
                out, &promotion_plan);
            return 1;
        }
        if (mir_match_read_validate_runner(
                &read_validate_plan)) {
            mir_emit_read_validate_runner(
                out, &read_validate_plan);
            return 1;
        }
    return -1;
}
