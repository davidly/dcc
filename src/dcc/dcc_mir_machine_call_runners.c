/* dcc_mir_machine_call_runners.c - strict call/check orchestration schedules. */

#include "dcc_mir_machine_internal.h"

struct MirFixedCallCheckRunner {
    struct Sym *functions[5];
    struct Sym *print_function;
    int failure_string_ids[5];
    int summary_string_id;
    int success_string_id;
    int expected_words[4];
    unsigned long expected_long;
    int array_values[4];
    int binary_arguments[2][2];
    int pointer_count;
    unsigned long long_arguments[2];
    int long_middle_argument;
    int byte_arguments[3];
};

struct MirFixedIndexCallRunner {
    struct Sym *functions[5];
    struct Sym *print_function;
    int failure_string_id;
    int success_string_id;
    int int_values[4];
    unsigned long long_values[4];
    unsigned long pair_values[6];
    int scalar_arguments[2];
    int pointer_indices[3];
    int expected_words[3];
    unsigned long expected_longs[2];
};

struct MirLongIndexCallRunner {
    struct Sym *copy_string_function;
    struct Sym *count_function;
    struct Sym *length_function;
    struct Sym *long_check_function;
    struct Sym *copy_function;
    struct Sym *string_check_function;
    struct Sym *safe_sum_function;
    struct Sym *unsafe_sum_function;
    struct Sym *print_function;
    struct Sym *source_buffer;
    struct Sym *output_buffer;
    struct Sym *inline_values;
    struct Sym *unsafe_call_count;
    struct Sym *checks;
    struct Sym *failures;
    int first_source_string_id;
    int first_count_string_id;
    int first_copy_string_id;
    int second_source_string_id;
    int second_count_string_id;
    int second_copy_string_id;
    int safe_sum_string_id;
    int unsafe_sum_string_id;
    int unsafe_count_string_id;
    int summary_string_id;
    int result_string_id;
    int zero_result_string_id;
    int nonzero_result_string_id;
    int second_count_expected;
    int inline_bound;
    int safe_count;
    int safe_expected;
    int unsafe_count;
    int unsafe_expected;
    int unsafe_calls_expected;
    int copy_fastcall;
    int length_fastcall;
    char copy_runtime_name[16];
};

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

struct MirAllocationLifetimeRunner {
    struct Sym *allocate_function;
    struct Sym *free_function;
    struct Sym *print_function;
    int strings[7];
};

struct MirCallbackRegistrationRunner {
    struct Sym *register_function;
    struct Sym *callbacks[3];
    struct Sym *print_function;
    int failure_string_id;
    int success_string_id;
};

struct MirForIncrementRunner {
    struct Sym *helpers[7];
    struct Sym *print_function;
    int input_string_id;
    int format_string_id;
    int expected[7];
};

struct MirCommaLoopRunner {
    struct Sym *print_function;
    int strings[7];
    int values[4];
};

struct MirScopeBlockRunner {
    struct Sym *check_function;
    struct Sym *parameter_function;
    struct Sym *helper_functions[3];
    struct Sym *print_function;
    struct Sym *failures;
    int check_strings[26];
    int success_string;
    int failure_string;
};

struct MirMemoryExerciseRunner {
    struct Sym *print_function;
    struct Sym *calloc_function;
    struct Sym *check_function;
    struct Sym *memset_function;
    struct Sym *malloc_function;
    struct Sym *free_function;
    struct Sym *logging_root;
    char pointer_array_name[64];
    int strings[5];
    int argc_stack_offset;
    int memset_fastcall;
};

struct MirByteEqualityRunner {
    struct Sym *check_function;
    struct Sym *print_function;
    struct Sym *signed_array;
    struct Sym *unsigned_array;
    struct Sym *failures;
    int check_strings[15];
    int format_string;
    int pass_string;
    int fail_string;
    int argc_stack_offset;
};

struct MirGnarlyRunner {
    struct Sym *hello_function;
    struct Sym *duff_function;
    struct Sym *structure_function;
    struct Sym *implicit_function;
    struct Sym *indirect_function;
    struct Sym *sum_function;
    struct Sym *print_function;
    int strings[35];
    char print_names[32][64];
};

struct MirNestedForRunner {
    struct Sym *build_function;
    struct Sym *check_functions[5];
    struct Sym *count_functions[2];
    struct Sym *stride_functions[2];
    struct Sym *print_function;
    struct Sym *pointer_array;
    struct Sym *grid_array;
    int strings[5];
    char print_names[3][64];
};

struct MirWideStringRunner {
    struct Sym *random_function;
    struct Sym *exit_function;
    struct Sym *length_function;
    struct Sym *find_first_function;
    struct Sym *find_last_function;
    struct Sym *copy_function;
    struct Sym *find_string_function;
    struct Sym *compare_function;
    struct Sym *print_function;
    struct Sym *buffer;
    int strings[14];
    char print_names[13][64];
};

struct MirCastLogicalRunner {
    struct Sym *print_function;
    int format_string_id;
};

enum MirAbortFileString {
    MIR_ABORT_NEW_MISSING,
    MIR_ABORT_CONTENT,
    MIR_ABORT_CONTENT_CHECK,
    MIR_ABORT_OLD_PRESENT,
    MIR_ABORT_FAILURE_SUMMARY,
    MIR_ABORT_OLD_NAME,
    MIR_ABORT_WRITE_MODE,
    MIR_ABORT_RENAME_CHECK,
    MIR_ABORT_NEW_NAME,
    MIR_ABORT_READ_MODE,
    MIR_ABORT_GRAPH_FIRST,
    MIR_ABORT_GRAPH_LAST = MIR_ABORT_GRAPH_FIRST + 6,
    MIR_ABORT_SUCCESS,
    MIR_ABORT_RETURNED,
    MIR_ABORT_STRING_COUNT
};

enum {
    MIR_STRADDR = MIR_STRING_ADDRESS,
    MIR_BRFALSE = MIR_BRANCH_FALSE
};

struct MirAbortFileRunner {
    struct Sym *open_function;
    struct Sym *puts_function;
    struct Sym *close_function;
    struct Sym *rename_function;
    struct Sym *check_function;
    struct Sym *print_function;
    struct Sym *gets_function;
    struct Sym *compare_function;
    struct Sym *remove_function;
    struct Sym *is_print_function;
    struct Sym *is_space_function;
    struct Sym *abort_function;
    struct Sym *failures;
    int strings[MIR_ABORT_STRING_COUNT];
    int graph_characters[7];
    int graph_expected[7];
};

enum MirFileIoString {
    MIR_FILE_IO_CREATE_FIRST_FAILURE,
    MIR_FILE_IO_CREATE_SECOND_FAILURE,
    MIR_FILE_IO_OPEN_FIRST_FAILURE,
    MIR_FILE_IO_READ_FIRST_FAILURE,
    MIR_FILE_IO_FIRST_CONTENT,
    MIR_FILE_IO_FIRST_CONTENT_FAILURE,
    MIR_FILE_IO_REOPEN_FAILURE,
    MIR_FILE_IO_READ_SECOND_FAILURE,
    MIR_FILE_IO_SECOND_CONTENT,
    MIR_FILE_IO_SECOND_CONTENT_FAILURE,
    MIR_FILE_IO_READ_MODE,
    MIR_FILE_IO_MISSING_NAME,
    MIR_FILE_IO_MISSING_FAILURE,
    MIR_FILE_IO_FIRST_NAME,
    MIR_FILE_IO_WRITE_MODE,
    MIR_FILE_IO_FIRST_LINE,
    MIR_FILE_IO_SECOND_NAME,
    MIR_FILE_IO_SECOND_LINE,
    MIR_FILE_IO_SUCCESS,
    MIR_FILE_IO_STRING_COUNT
};

struct MirFileIoRunner {
    struct Sym *open_function;
    struct Sym *reopen_function;
    struct Sym *close_function;
    struct Sym *remove_function;
    struct Sym *read_function;
    struct Sym *write_function;
    struct Sym *compare_function;
    struct Sym *print_function;
    int strings[MIR_FILE_IO_STRING_COUNT];
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

enum MirBufferedConsoleFunction {
    MIR_BUFFER_PRINT,
    MIR_BUFFER_ALLOCATE,
    MIR_BUFFER_CONFIGURE,
    MIR_BUFFER_CHECK,
    MIR_BUFFER_FLUSH,
    MIR_BUFFER_FREE,
    MIR_BUFFER_PUTS_STREAM,
    MIR_BUFFER_PUTS,
    MIR_BUFFER_PUTCHAR,
    MIR_BUFFER_SETBUF,
    MIR_BUFFER_FPRINT,
    MIR_BUFFER_CLEAR,
    MIR_BUFFER_OPEN,
    MIR_BUFFER_CLOSE,
    MIR_BUFFER_REMOVE,
    MIR_BUFFER_FUNCTION_COUNT
};

struct MirBufferedConsoleRunner {
    struct Sym *functions[MIR_BUFFER_FUNCTION_COUNT];
    struct Sym *failures;
    char call_names[MIR_BUFFER_FUNCTION_COUNT][64];
    int strings[40];
};

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

static struct Sym *mir_long_index_global_address(int instruction)
{
    const struct MirInsn *insn = &mir.insns[instruction];
    struct Sym *symbol;

    if (insn->opcode != MIR_ADDRESS ||
        !mir_machine_named_nonvolatile(insn) ||
        (symbol = find_global(insn->name)) == NULL)
        return NULL;
    return symbol;
}

static struct Sym *mir_long_index_call_function(int instruction)
{
    const struct MirInsn *call = &mir.insns[instruction];

    if (call->opcode != MIR_CALL || call->src1 >= 0 ||
        (call->memory_flags & MIR_CALL_FLAG_VARIADIC) != 0)
        return NULL;
    return find_global(call->name);
}

static int mir_long_index_constant(int instruction, int *value_out)
{
    long value;

    if (!mir_machine_constant_value(
            mir.insns[instruction].dst, &value, 0) ||
        value < -32768 || value > 65535)
        return 0;
    *value_out = (int)value;
    return 1;
}

static struct Sym *mir_allocation_runner_call_function(
    int instruction, int variadic)
{
    const struct MirInsn *call = &mir.insns[instruction];

    if (call->opcode != MIR_CALL || call->src1 >= 0 ||
        ((call->memory_flags & MIR_CALL_FLAG_VARIADIC) != 0) != variadic ||
        (call->memory_flags & MIR_CALL_FLAG_FORMAT_RUNTIME) != 0)
        return NULL;
    return find_global(call->name);
}

static int mir_allocation_runner_single_argument(
    int call_instruction, int argument_instruction)
{
    int argument;

    return mir_machine_single_call_argument(
               &mir.insns[call_instruction], &argument) &&
           argument == mir.insns[argument_instruction].dst;
}

static int mir_memory_runner_word_type(int type, int is_unsigned)
{
    return type_ptr_depth(type) == 0 &&
           (type & 15) == TYPE_INT &&
           ((type & TYPE_UNSIGNED) != 0) == is_unsigned &&
           type_size(type) == 2;
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

static int mir_memory_runner_call_matches(
    int instruction, struct Sym *function, int ordinal,
    int argument_count, const int *definitions)
{
    const struct MirInsn *call = &mir.insns[instruction];
    int arguments[3];
    int argument;

    if (find_global(call->name) != function ||
        call->secondary_offset != ordinal ||
        call->type != function->type ||
        !mir_machine_call_arguments(
            call, argument_count, arguments))
        return 0;
    for (argument = 0; argument < argument_count; ++argument)
        if (arguments[argument] !=
            mir.insns[definitions[argument]].dst)
            return 0;
    return 1;
}

static int mir_memory_runner_loop(
    int initial_constant, int initial_store, int entry_label,
    int header_label,
    int phi, int bound_constant, int comparison, int branch,
    int exit_label, int back_label, int step_constant,
    int increment, int increment_store, int jump,
    long initial, long bound, long step)
{
    const struct MirInsn *merge = &mir.insns[phi];

    return mir_machine_constant_equals(
               mir.insns[initial_constant].dst, initial) &&
           mir.insns[initial_store].src1 ==
               mir.insns[initial_constant].dst &&
           mir_machine_unobservable_local_store(
               &mir.insns[initial_store]) &&
           merge->src1 == mir.insns[initial_constant].dst &&
           merge->src2 == mir.insns[increment].dst &&
           merge->phi_pred1 == mir.insns[entry_label].label &&
           merge->phi_pred2 == mir.insns[back_label].label &&
           mir_machine_same_location(
               &mir.insns[initial_store], merge) &&
           mir_machine_constant_equals(
               mir.insns[bound_constant].dst, bound) &&
           mir.insns[comparison].immediate == '<' &&
           mir.insns[comparison].src1 == merge->dst &&
           mir.insns[comparison].src2 ==
               mir.insns[bound_constant].dst &&
           mir.insns[branch].src1 ==
               mir.insns[comparison].dst &&
           mir.insns[branch].label ==
               mir.insns[exit_label].label &&
           mir_machine_constant_equals(
               mir.insns[step_constant].dst, step) &&
           mir.insns[increment].immediate == '+' &&
           mir.insns[increment].src1 == merge->dst &&
           mir.insns[increment].src2 ==
               mir.insns[step_constant].dst &&
           mir.insns[increment_store].src1 ==
               mir.insns[increment].dst &&
           mir_machine_same_location(
               &mir.insns[initial_store],
               &mir.insns[increment_store]) &&
           mir.insns[jump].label ==
               mir.insns[header_label].label;
}

static int mir_memory_runner_sizes(
    int index_value, int base_constant, int scale_constant,
    int product, int sum, int size_store, int extra_constant,
    int extended_sum, int extended_store, long extra)
{
    return mir_machine_constant_equals(
               mir.insns[base_constant].dst, 8) &&
           mir_machine_constant_equals(
               mir.insns[scale_constant].dst, 10) &&
           mir.insns[product].immediate == '*' &&
           mir.insns[product].src1 == mir.insns[index_value].dst &&
           mir.insns[product].src2 ==
               mir.insns[scale_constant].dst &&
           mir.insns[sum].immediate == '+' &&
           mir.insns[sum].src1 ==
               mir.insns[base_constant].dst &&
           mir.insns[sum].src2 == mir.insns[product].dst &&
           mir.insns[size_store].src1 == mir.insns[sum].dst &&
           mir_machine_unobservable_local_store(
               &mir.insns[size_store]) &&
           mir_machine_constant_equals(
               mir.insns[extra_constant].dst, extra) &&
           mir.insns[extended_sum].immediate == '+' &&
           mir.insns[extended_sum].src1 == mir.insns[sum].dst &&
           mir.insns[extended_sum].src2 ==
               mir.insns[extra_constant].dst &&
           mir.insns[extended_store].src1 ==
               mir.insns[extended_sum].dst &&
           mir_machine_unobservable_local_store(
               &mir.insns[extended_store]);
}

static int mir_memory_runner_array_root(
    int instruction, char name[64])
{
    const struct MirInsn *address = &mir.insns[instruction];
    const char *separator;

    if (address->opcode != MIR_ADDRESS ||
        !mir_machine_named_nonvolatile(address) ||
        address->memory_flags != 0 || address->object >= 0 ||
        !mir_memory_runner_pointer_type(
            address->type, 2, TYPE_CHAR) ||
        (separator = strchr(address->name, '#')) == NULL ||
        separator == address->name || separator[1] == 0 ||
        strchr(separator + 1, '#') != NULL)
        return 0;
    strncpy(name, separator + 1, 63);
    name[63] = 0;
    return 1;
}

static int mir_memory_runner_array_access(
    const char *root_name, int address, int index_address,
    int load, int index_value)
{
    const struct MirInsn *base = &mir.insns[address];
    const struct MirInsn *index = &mir.insns[index_address];
    const char *separator = strchr(base->name, '#');

    if (base->opcode != MIR_ADDRESS ||
        separator == NULL ||
        strcmp(separator + 1, root_name) ||
        base->type != mir.insns[97].type ||
        base->memory_flags != 0 || base->object >= 0 ||
        index->src1 != base->dst ||
        index->src2 != mir.insns[index_value].dst ||
        index->immediate != 2 || index->memory_size != 2)
        return 0;
    if (load < 0)
        return 1;
    return mir.insns[load].src1 == index->dst &&
           mir.insns[load].memory_size == 2 &&
           mir_memory_runner_pointer_type(
               mir.insns[load].type, 1, TYPE_CHAR);
}

static int mir_match_memory_exercise_runner(
    struct MirMemoryExerciseRunner *plan)
{
    static const int expected_opcodes[386] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_NOP, MIR_STORE, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP,
        MIR_NOP, MIR_PHI, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP,
        MIR_CONST, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST,
        MIR_NOP, MIR_CONST, MIR_NOP, MIR_BINARY, MIR_NOP, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST, MIR_NOP, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_LOAD, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL, MIR_LABEL,
        MIR_NOP, MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_UNARY, MIR_STORE, MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_NOP, MIR_ARG, MIR_CALL, MIR_LOAD, MIR_NOP, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL, MIR_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_NOP, MIR_ARG, MIR_CONST, MIR_ARG, MIR_NOP,
        MIR_ARG, MIR_CALL, MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG, MIR_NOP,
        MIR_ARG, MIR_CALL, MIR_LOAD, MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_LOAD, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_CONST, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST,
        MIR_NOP, MIR_CONST, MIR_NOP, MIR_BINARY, MIR_NOP, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST, MIR_NOP, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_LOAD, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL, MIR_LABEL,
        MIR_NOP, MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_UNARY, MIR_STORE, MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_NOP, MIR_ARG, MIR_CALL, MIR_LOAD, MIR_NOP, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL, MIR_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_LOAD, MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_NOP, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_LOAD, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_CONST, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST,
        MIR_NOP, MIR_CONST, MIR_NOP, MIR_BINARY, MIR_NOP, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST, MIR_NOP, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_LOAD, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL, MIR_LABEL,
        MIR_NOP, MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_UNARY, MIR_STORE, MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_NOP, MIR_ARG, MIR_CALL, MIR_LOAD, MIR_NOP, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL, MIR_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_LOAD, MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_NOP, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_RETURN
    };
    static const int constant_instructions[] = {
        4, 9, 18, 26, 36, 44, 48, 50, 58, 75, 84, 92, 111,
        118, 130, 142, 152, 156, 158, 166, 183, 192, 200, 210,
        221, 235, 247, 261, 271, 275, 277, 285, 302, 311, 319,
        329, 340, 354, 366, 376, 384
    };
    static const long constant_values[] = {
        1, 0, 0, 10, 0, 66, 8, 10, 5, 1, 0, 204, 170, 204, 1,
        0, 66, 8, 10, 3, 1, 0, 204, 170, 255, 204, 2, 1, 66, 8,
        10, 7, 1, 0, 204, 170, 255, 204, 2, 1, 0
    };
    enum {
        MIR_MEMORY_PRINT,
        MIR_MEMORY_CALLOC,
        MIR_MEMORY_CHECK,
        MIR_MEMORY_MEMSET,
        MIR_MEMORY_MALLOC,
        MIR_MEMORY_FREE
    };
    static const struct {
        int instruction;
        int function;
        int argument_count;
        int definitions[3];
    } calls[] = {
        {33, MIR_MEMORY_PRINT, 1, {31, 0, 0}},
        {71, MIR_MEMORY_PRINT, 3, {65, 42, 54}},
        {78, MIR_MEMORY_CALLOC, 2, {60, 75, 0}},
        {88, MIR_MEMORY_CHECK, 3, {82, 84, 60}},
        {96, MIR_MEMORY_MEMSET, 3, {89, 92, 60}},
        {102, MIR_MEMORY_MALLOC, 1, {54, 0, 0}},
        {115, MIR_MEMORY_MEMSET, 3, {108, 111, 54}},
        {122, MIR_MEMORY_CHECK, 3, {116, 118, 60}},
        {126, MIR_MEMORY_FREE, 1, {123, 0, 0}},
        {139, MIR_MEMORY_PRINT, 1, {137, 0, 0}},
        {179, MIR_MEMORY_PRINT, 3, {173, 148, 162}},
        {186, MIR_MEMORY_CALLOC, 2, {168, 183, 0}},
        {196, MIR_MEMORY_CHECK, 3, {190, 192, 168}},
        {204, MIR_MEMORY_MEMSET, 3, {197, 200, 168}},
        {214, MIR_MEMORY_CHECK, 3, {208, 210, 162}},
        {225, MIR_MEMORY_MEMSET, 3, {218, 221, 162}},
        {232, MIR_MEMORY_FREE, 1, {229, 0, 0}},
        {239, MIR_MEMORY_CHECK, 3, {233, 235, 168}},
        {243, MIR_MEMORY_FREE, 1, {240, 0, 0}},
        {258, MIR_MEMORY_PRINT, 1, {256, 0, 0}},
        {298, MIR_MEMORY_PRINT, 3, {292, 267, 281}},
        {305, MIR_MEMORY_CALLOC, 2, {287, 302, 0}},
        {315, MIR_MEMORY_CHECK, 3, {309, 311, 287}},
        {323, MIR_MEMORY_MEMSET, 3, {316, 319, 287}},
        {333, MIR_MEMORY_CHECK, 3, {327, 329, 281}},
        {344, MIR_MEMORY_MEMSET, 3, {337, 340, 281}},
        {351, MIR_MEMORY_FREE, 1, {348, 0, 0}},
        {358, MIR_MEMORY_CHECK, 3, {352, 354, 287}},
        {362, MIR_MEMORY_FREE, 1, {359, 0, 0}},
        {383, MIR_MEMORY_PRINT, 1, {381, 0, 0}}
    };
    struct Sym *functions[6];
    int argv_stack_offset;
    int call_count = 0;
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 386 || mir_cfg_block_count() != 19 ||
        mir.has_vla || mir.local_bytes != 10 ||
        mir.aggregate_temp_bytes != 0 ||
        !mir_memory_runner_word_type(mir.return_type, 0))
        return mir_machine_reject(
            "memory-exercise-runner", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        if (mir.insns[instruction].opcode !=
                expected_opcodes[instruction])
            return mir_machine_reject(
                "memory-exercise-runner", "opcode");
        if (mir.insns[instruction].opcode == MIR_CALL)
            ++call_count;
    }
    if (call_count != 30)
        return mir_machine_reject(
            "memory-exercise-runner", "call-count");
    for (item = 0;
         item < (int)(sizeof(constant_instructions) /
                      sizeof(constant_instructions[0]));
         ++item)
        if (!mir_machine_constant_equals(
                mir.insns[constant_instructions[item]].dst,
                constant_values[item]))
            return mir_machine_reject(
                "memory-exercise-runner", "constant");

    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->argc_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &argv_stack_offset) ||
        plan->argc_stack_offset != 2 || argv_stack_offset != 4 ||
        !mir_memory_runner_word_type(mir.insns[1].type, 0) ||
        !mir_memory_runner_pointer_type(
            mir.insns[2].type, 2, TYPE_CHAR) ||
        !mir_machine_same_location(
            &mir.insns[1], &mir.insns[3]) ||
        mir.insns[5].immediate != '>' ||
        mir.insns[5].src1 != mir.insns[1].dst ||
        mir.insns[5].src2 != mir.insns[4].dst ||
        !mir_memory_runner_word_type(mir.insns[5].type, 0))
        return mir_machine_reject(
            "memory-exercise-runner", "parameters");

    plan->logging_root = find_global(mir.insns[7].name);
    if (plan->logging_root == NULL ||
        !plan->logging_root->is_defined ||
        plan->logging_root->is_array ||
        plan->logging_root->is_volatile ||
        !mir_memory_runner_word_type(
            plan->logging_root->type, 0) ||
        mir.insns[7].src1 != mir.insns[5].dst ||
        mir.insns[7].memory_size != 2 ||
        !mir_machine_named_nonvolatile(&mir.insns[7]))
        return mir_machine_reject(
            "memory-exercise-runner", "logging");
    {
        static const int logging_loads[] = {
            29, 63, 135, 171, 254, 290
        };

        for (item = 0; item < 6; ++item)
            if (!mir_machine_same_location(
                    &mir.insns[7],
                    &mir.insns[logging_loads[item]]))
                return mir_machine_reject(
                    "memory-exercise-runner", "logging-load");
    }

    if (mir.insns[10].src1 != mir.insns[2].dst ||
        mir.insns[10].src2 != mir.insns[9].dst ||
        mir.insns[10].immediate != 2 ||
        mir.insns[10].memory_size != 2 ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[11].memory_size != 2 ||
        mir.insns[13].src1 != mir.insns[11].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[13]))
        return mir_machine_reject(
            "memory-exercise-runner", "dead-argv-load");

    if (!mir_memory_runner_loop(
            18, 20, 0, 21, 24, 26, 27, 28, 380, 374,
            376, 377, 378, 379, 0, 10, 1) ||
        !mir_memory_runner_loop(
            36, 37, 34, 38, 42, 44, 46, 47, 134, 128,
            130, 131, 132, 133, 0, 66, 1) ||
        !mir_memory_runner_loop(
            142, 143, 140, 144, 148, 152, 154, 155, 253, 245,
            247, 249, 251, 252, 0, 66, 2) ||
        !mir_memory_runner_loop(
            261, 262, 259, 263, 267, 271, 273, 274, 372, 364,
            366, 368, 370, 371, 1, 66, 2))
        return mir_machine_reject(
            "memory-exercise-runner", "loop-cfg");
    if (!mir_memory_runner_sizes(
            42, 48, 50, 52, 54, 56, 58, 60, 62, 5) ||
        !mir_memory_runner_sizes(
            148, 156, 158, 160, 162, 164, 166, 168, 170, 3) ||
        !mir_memory_runner_sizes(
            267, 275, 277, 279, 281, 283, 285, 287, 289, 7) ||
        !mir_machine_same_location(
            &mir.insns[56], &mir.insns[164]) ||
        !mir_machine_same_location(
            &mir.insns[56], &mir.insns[283]) ||
        !mir_machine_same_location(
            &mir.insns[62], &mir.insns[170]) ||
        !mir_machine_same_location(
            &mir.insns[62], &mir.insns[289]))
        return mir_machine_reject(
            "memory-exercise-runner", "sizes");
    if (mir_machine_same_location(
            &mir.insns[20], &mir.insns[37]) ||
        mir_machine_same_location(
            &mir.insns[20], &mir.insns[56]) ||
        mir_machine_same_location(
            &mir.insns[20], &mir.insns[62]) ||
        mir_machine_same_location(
            &mir.insns[13], &mir.insns[20]) ||
        mir_machine_same_location(
            &mir.insns[13], &mir.insns[37]) ||
        mir_machine_same_location(
            &mir.insns[13], &mir.insns[56]) ||
        mir_machine_same_location(
            &mir.insns[13], &mir.insns[62]) ||
        mir_machine_same_location(
            &mir.insns[37], &mir.insns[56]) ||
        mir_machine_same_location(
            &mir.insns[37], &mir.insns[62]) ||
        mir_machine_same_location(
            &mir.insns[56], &mir.insns[62]))
        return mir_machine_reject(
            "memory-exercise-runner", "local-alias");

    functions[MIR_MEMORY_PRINT] =
        mir_memory_runner_call_function(33, 1, 1);
    functions[MIR_MEMORY_CALLOC] =
        mir_memory_runner_call_function(78, 0, 2);
    functions[MIR_MEMORY_CHECK] =
        mir_memory_runner_call_function(88, 0, 3);
    functions[MIR_MEMORY_MEMSET] =
        mir_memory_runner_call_function(96, 0, 3);
    functions[MIR_MEMORY_MALLOC] =
        mir_memory_runner_call_function(102, 0, 1);
    functions[MIR_MEMORY_FREE] =
        mir_memory_runner_call_function(126, 0, 1);
    for (item = 0; item < 6; ++item) {
        int previous;

        if (functions[item] == NULL)
            return mir_machine_reject(
                "memory-exercise-runner", "function");
        for (previous = 0; previous < item; ++previous)
            if (functions[item] == functions[previous])
                return mir_machine_reject(
                    "memory-exercise-runner", "function-alias");
    }
    plan->print_function = functions[MIR_MEMORY_PRINT];
    plan->calloc_function = functions[MIR_MEMORY_CALLOC];
    plan->check_function = functions[MIR_MEMORY_CHECK];
    plan->memset_function = functions[MIR_MEMORY_MEMSET];
    plan->malloc_function = functions[MIR_MEMORY_MALLOC];
    plan->free_function = functions[MIR_MEMORY_FREE];

    if (!mir_memory_runner_word_type(
            plan->print_function->type, 0) ||
        !mir_memory_runner_pointer_type(
            plan->print_function->proto_types[0], 1, TYPE_CHAR) ||
        !mir_memory_runner_pointer_type(
            plan->calloc_function->type, 1, TYPE_VOID) ||
        !mir_memory_runner_word_type(
            plan->calloc_function->proto_types[0], 1) ||
        !mir_memory_runner_word_type(
            plan->calloc_function->proto_types[1], 1) ||
        (plan->check_function->type & 15) != TYPE_VOID ||
        type_ptr_depth(plan->check_function->type) != 0 ||
        !plan->check_function->is_defined ||
        !mir_memory_runner_pointer_type(
            plan->check_function->proto_types[0], 1, TYPE_CHAR) ||
        !mir_memory_runner_word_type(
            plan->check_function->proto_types[1], 0) ||
        !mir_memory_runner_word_type(
            plan->check_function->proto_types[2], 1) ||
        !mir_memory_runner_pointer_type(
            plan->memset_function->type, 1, TYPE_VOID) ||
        !mir_memory_runner_pointer_type(
            plan->memset_function->proto_types[0], 1, TYPE_VOID) ||
        !mir_memory_runner_word_type(
            plan->memset_function->proto_types[1], 0) ||
        !mir_memory_runner_word_type(
            plan->memset_function->proto_types[2], 1) ||
        !mir_memory_runner_pointer_type(
            plan->malloc_function->type, 1, TYPE_VOID) ||
        !mir_memory_runner_word_type(
            plan->malloc_function->proto_types[0], 1) ||
        (plan->free_function->type & 15) != TYPE_VOID ||
        type_ptr_depth(plan->free_function->type) != 0 ||
        !mir_memory_runner_pointer_type(
            plan->free_function->proto_types[0], 1, TYPE_VOID))
        return mir_machine_reject(
            "memory-exercise-runner", "prototype");
    for (item = 0;
         item < (int)(sizeof(calls) / sizeof(calls[0]));
         ++item)
        if (!mir_memory_runner_call_matches(
                calls[item].instruction,
                functions[calls[item].function],
                item, calls[item].argument_count,
                calls[item].definitions))
            return mir_machine_reject(
                "memory-exercise-runner", "call");
    {
        static const int memset_calls[] = {
            96, 115, 204, 225, 323, 344
        };
        static const int memset_destinations[] = {
            89, 108, 197, 218, 316, 337
        };
        static const int memset_fills[] = {
            92, 111, 200, 221, 319, 340
        };
        static const int memset_counts[] = {
            60, 54, 168, 162, 287, 281
        };

        int fastcall_count = 0;

        for (item = 0; item < 6; ++item) {
            int destination;
            int fill;
            int count;

            if (!mir_call_is_memset_fastcall(
                    memset_calls[item], &destination,
                    &fill, &count))
                continue;
            ++fastcall_count;
            if (destination !=
                    mir.insns[memset_destinations[item]].dst ||
                fill != mir.insns[memset_fills[item]].dst ||
                count != mir.insns[memset_counts[item]].dst)
                return mir_machine_reject(
                    "memory-exercise-runner", "memset-abi");
        }
        if (fastcall_count != 0 && fastcall_count != 6)
            return mir_machine_reject(
                "memory-exercise-runner", "memset-abi-mix");
        plan->memset_fastcall = fastcall_count == 6;
    }

    plan->strings[0] = (int)mir.insns[31].immediate;
    plan->strings[1] = (int)mir.insns[65].immediate;
    plan->strings[2] = (int)mir.insns[137].immediate;
    plan->strings[3] = (int)mir.insns[256].immediate;
    plan->strings[4] = (int)mir.insns[381].immediate;
    if (mir.insns[173].immediate != plan->strings[1] ||
        mir.insns[292].immediate != plan->strings[1])
        return mir_machine_reject(
            "memory-exercise-runner", "report-string");
    for (item = 0; item < 5; ++item) {
        int previous;

        for (previous = 0; previous < item; ++previous)
            if (plan->strings[item] == plan->strings[previous])
                return mir_machine_reject(
                    "memory-exercise-runner", "string-alias");
    }

    if (mir.insns[30].src1 != mir.insns[29].dst ||
        mir.insns[30].label != mir.insns[34].label ||
        mir.insns[64].src1 != mir.insns[63].dst ||
        mir.insns[64].label != mir.insns[72].label ||
        mir.insns[136].src1 != mir.insns[135].dst ||
        mir.insns[136].label != mir.insns[140].label ||
        mir.insns[172].src1 != mir.insns[171].dst ||
        mir.insns[172].label != mir.insns[180].label ||
        mir.insns[255].src1 != mir.insns[254].dst ||
        mir.insns[255].label != mir.insns[259].label ||
        mir.insns[291].src1 != mir.insns[290].dst ||
        mir.insns[291].label != mir.insns[299].label)
        return mir_machine_reject(
            "memory-exercise-runner", "print-cfg");

    if (!mir_memory_runner_array_root(
            97, plan->pointer_array_name) ||
        !mir_memory_runner_array_access(
            plan->pointer_array_name, 97, 99, -1, 42) ||
        mir.insns[104].src1 != mir.insns[99].dst ||
        mir.insns[104].src2 != mir.insns[102].dst ||
        mir.insns[104].memory_size != 2 ||
        !mir_memory_runner_array_access(
            plan->pointer_array_name, 105, 107, 108, 42) ||
        !mir_memory_runner_array_access(
            plan->pointer_array_name, 205, 207, 208, 148) ||
        !mir_memory_runner_array_access(
            plan->pointer_array_name, 215, 217, 218, 148) ||
        !mir_memory_runner_array_access(
            plan->pointer_array_name, 226, 228, 229, 148) ||
        !mir_memory_runner_array_access(
            plan->pointer_array_name, 324, 326, 327, 267) ||
        !mir_memory_runner_array_access(
            plan->pointer_array_name, 334, 336, 337, 267) ||
        !mir_memory_runner_array_access(
            plan->pointer_array_name, 345, 347, 348, 267))
        return mir_machine_reject(
            "memory-exercise-runner", "pointer-array");

    {
        static const int pointer_locations[] = {
            81, 82, 89, 116, 123, 189, 190, 197, 233, 240,
            308, 309, 316, 352, 359
        };

        for (item = 0;
             item < (int)(sizeof(pointer_locations) /
                          sizeof(pointer_locations[0]));
             ++item)
            if (!mir_machine_same_location(
                    &mir.insns[13],
                    &mir.insns[pointer_locations[item]]))
                return mir_machine_reject(
                    "memory-exercise-runner", "pointer-local");
    }
    if (mir.insns[80].immediate != 0 ||
        mir.insns[80].src1 != mir.insns[78].dst ||
        mir.insns[81].src1 != mir.insns[80].dst ||
        mir.insns[188].immediate != 0 ||
        mir.insns[188].src1 != mir.insns[186].dst ||
        mir.insns[189].src1 != mir.insns[188].dst ||
        mir.insns[307].immediate != 0 ||
        mir.insns[307].src1 != mir.insns[305].dst ||
        mir.insns[308].src1 != mir.insns[307].dst ||
        mir.insns[385].src1 != mir.insns[384].dst)
        return mir_machine_reject(
            "memory-exercise-runner", "result-flow");
    return 1;
}

static int mir_abort_runner_word_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           (type & 15) == TYPE_INT &&
           (type & TYPE_UNSIGNED) == 0 &&
           type_size(type) == 2;
}

static int mir_abort_runner_pointer_type(
    int type, int base_type)
{
    return type_ptr_depth(type) == 1 &&
           (type & 15) == base_type &&
           type_size(type) == 2;
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

static int mir_abort_runner_call(
    int instruction, struct Sym *function, int ordinal,
    int argument_count, const int *definitions)
{
    const struct MirInsn *call = &mir.insns[instruction];
    const char *assembly_name;
    int arguments[3];
    int argument;

    if (function == NULL ||
        call->src1 >= 0 ||
        find_global(call->name) != function ||
        call->memory_flags !=
            (function->proto_variadic
                 ? MIR_CALL_FLAG_VARIADIC : 0) ||
        call->type != function->type ||
        call->secondary_offset != ordinal ||
        !mir_machine_call_arguments(
            call, argument_count, arguments))
        return 0;
    assembly_name = asm_name_for(sym_asm_name(function));
    if (call->base_name[0] != 0 &&
        strcmp(call->base_name, assembly_name))
        return 0;
    for (argument = 0; argument < argument_count; ++argument)
        if (arguments[argument] !=
            mir.insns[definitions[argument]].dst)
            return 0;
    return 1;
}

static int mir_abort_runner_no_argument_call(
    int instruction, struct Sym *function, int ordinal)
{
    const struct MirInsn *call = &mir.insns[instruction];
    const char *assembly_name;
    int scan;

    if (function == NULL ||
        call->src1 >= 0 ||
        find_global(call->name) != function ||
        call->memory_flags != 0 ||
        call->type != function->type ||
        call->secondary_offset != ordinal)
        return 0;
    assembly_name = asm_name_for(sym_asm_name(function));
    if (call->base_name[0] != 0 &&
        strcmp(call->base_name, assembly_name))
        return 0;
    for (scan = 0; scan < mir.count; ++scan)
        if (mir.insns[scan].opcode == MIR_ARG &&
            mir.insns[scan].secondary_offset ==
                call->secondary_offset)
            return 0;
    return 1;
}

static int mir_abort_runner_function_types(
    const struct MirAbortFileRunner *plan)
{
    return
        mir_abort_runner_pointer_type(
            plan->open_function->type, TYPE_INT) &&
        mir_abort_runner_pointer_type(
            plan->open_function->proto_types[0], TYPE_CHAR) &&
        mir_abort_runner_pointer_type(
            plan->open_function->proto_types[1], TYPE_CHAR) &&
        mir_abort_runner_word_type(plan->puts_function->type) &&
        mir_abort_runner_pointer_type(
            plan->puts_function->proto_types[0], TYPE_CHAR) &&
        mir_abort_runner_pointer_type(
            plan->puts_function->proto_types[1], TYPE_INT) &&
        mir_abort_runner_word_type(plan->close_function->type) &&
        mir_abort_runner_pointer_type(
            plan->close_function->proto_types[0], TYPE_INT) &&
        mir_abort_runner_word_type(plan->rename_function->type) &&
        mir_abort_runner_pointer_type(
            plan->rename_function->proto_types[0], TYPE_CHAR) &&
        mir_abort_runner_pointer_type(
            plan->rename_function->proto_types[1], TYPE_CHAR) &&
        type_ptr_depth(plan->check_function->type) == 0 &&
        (plan->check_function->type & 15) == TYPE_VOID &&
        mir_abort_runner_pointer_type(
            plan->check_function->proto_types[0], TYPE_CHAR) &&
        mir_abort_runner_word_type(
            plan->check_function->proto_types[1]) &&
        mir_abort_runner_word_type(
            plan->check_function->proto_types[2]) &&
        mir_abort_runner_word_type(plan->print_function->type) &&
        mir_abort_runner_pointer_type(
            plan->print_function->proto_types[0], TYPE_CHAR) &&
        mir_abort_runner_pointer_type(
            plan->gets_function->type, TYPE_CHAR) &&
        mir_abort_runner_pointer_type(
            plan->gets_function->proto_types[0], TYPE_CHAR) &&
        mir_abort_runner_word_type(
            plan->gets_function->proto_types[1]) &&
        mir_abort_runner_pointer_type(
            plan->gets_function->proto_types[2], TYPE_INT) &&
        mir_abort_runner_word_type(plan->compare_function->type) &&
        mir_abort_runner_pointer_type(
            plan->compare_function->proto_types[0], TYPE_CHAR) &&
        mir_abort_runner_pointer_type(
            plan->compare_function->proto_types[1], TYPE_CHAR) &&
        mir_abort_runner_word_type(plan->remove_function->type) &&
        mir_abort_runner_pointer_type(
            plan->remove_function->proto_types[0], TYPE_CHAR) &&
        mir_abort_runner_word_type(
            plan->is_print_function->type) &&
        mir_abort_runner_word_type(
            plan->is_print_function->proto_types[0]) &&
        mir_abort_runner_word_type(
            plan->is_space_function->type) &&
        mir_abort_runner_word_type(
            plan->is_space_function->proto_types[0]) &&
        type_ptr_depth(plan->abort_function->type) == 0 &&
        (plan->abort_function->type & 15) == TYPE_VOID;
}

static int mir_match_abort_file_runner(
    struct MirAbortFileRunner *plan)
{
    static const int expected_opcodes[269] = {
        MIR_LABEL, MIR_STRADDR, MIR_ARG, MIR_STRADDR, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_STORE, MIR_STRADDR, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG,
        MIR_STRADDR, MIR_ARG, MIR_STRADDR, MIR_ARG, MIR_CALL, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_STRADDR,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE, MIR_LOAD, MIR_UNARY,
        MIR_BRFALSE, MIR_LABEL, MIR_STRADDR, MIR_ARG, MIR_CALL, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_NOP, MIR_JUMP, MIR_LABEL,
        MIR_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG,
        MIR_ADDRESS, MIR_ARG, MIR_STRADDR, MIR_ARG, MIR_CALL, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP, MIR_LABEL, MIR_STRADDR,
        MIR_ARG, MIR_STRADDR, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE,
        MIR_LOAD, MIR_BRFALSE, MIR_STRADDR, MIR_ARG, MIR_CALL, MIR_LOAD,
        MIR_ARG, MIR_CALL, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_NOP, MIR_LABEL, MIR_STRADDR, MIR_ARG, MIR_CALL, MIR_STRADDR,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_BRFALSE, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_UNARY, MIR_BRFALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_BRFALSE, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_UNARY, MIR_BRFALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_BRFALSE, MIR_CONST, MIR_ARG, MIR_CALL, MIR_UNARY, MIR_BRFALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_PHI, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_BRFALSE, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_UNARY, MIR_BRFALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_BRFALSE, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_UNARY, MIR_BRFALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_BRFALSE, MIR_CONST, MIR_ARG, MIR_CALL, MIR_UNARY, MIR_BRFALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_PHI, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRADDR,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_BRFALSE, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_UNARY, MIR_BRFALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_LOAD, MIR_BRFALSE, MIR_STRADDR,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN,
        MIR_NOP, MIR_LABEL, MIR_STRADDR, MIR_ARG, MIR_CALL, MIR_CALL,
        MIR_STRADDR, MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN
    };
    static const int string_instructions[MIR_ABORT_STRING_COUNT] = {
        38, 8, 58, 80, 251, 1, 3, 16, 20, 29,
        95, 117, 139, 161, 183, 205, 227, 260, 264
    };
    static const int graph_starts[7] = {
        95, 117, 139, 161, 183, 205, 227
    };
    static const int graph_characters[7] = {
        'A', 'z', '0', '!', ' ', '\t', '\0'
    };
    static const int graph_expected[7] = {
        1, 1, 1, 1, 0, 0, 0
    };
    static const int open_definitions[3][2] = {
        {1, 3}, {27, 29}, {71, 73}
    };
    static const int open_calls[3] = {5, 31, 75};
    static const int open_ordinals[3] = {0, 5, 11};
    static const int close_definitions[3] = {13, 55, 83};
    static const int close_calls[3] = {15, 57, 85};
    static const int close_ordinals[3] = {2, 8, 13};
    static const int file_locations[] = {
        7, 10, 13, 33, 34, 52, 55, 77, 78, 83
    };
    static const int failure_locations[] = {
        41, 44, 86, 89, 249, 253
    };
    struct Sym *functions[12];
    int arguments[3];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;
    int first;
    int second;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 269 || mir_cfg_block_count() != 27 ||
        mir.has_vla || mir.local_bytes != 12 ||
        mir.aggregate_temp_bytes != 0 ||
        !mir_abort_runner_word_type(mir.return_type))
        return mir_machine_reject(
            "abort-file-runner", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
                expected_opcodes[instruction])
            return mir_machine_reject(
                "abort-file-runner", "opcode");

    plan->open_function =
        mir_abort_runner_function(5, 0, 2, 0);
    plan->puts_function =
        mir_abort_runner_function(12, 0, 2, 0);
    plan->close_function =
        mir_abort_runner_function(15, 0, 1, 0);
    plan->rename_function =
        mir_abort_runner_function(22, 0, 2, 0);
    plan->check_function =
        mir_abort_runner_function(26, 0, 3, 0);
    plan->print_function =
        mir_abort_runner_function(40, 1, 1, 0);
    plan->gets_function =
        mir_abort_runner_function(54, 0, 3, 0);
    plan->compare_function =
        mir_abort_runner_function(64, 0, 2, 0);
    plan->remove_function =
        mir_abort_runner_function(94, 0, 1, 0);
    plan->is_print_function =
        mir_abort_runner_function(99, 0, 1, 0);
    plan->is_space_function =
        mir_abort_runner_function(103, 0, 1, 0);
    plan->abort_function =
        mir_abort_runner_function(263, 0, 0, 1);
    functions[0] = plan->open_function;
    functions[1] = plan->puts_function;
    functions[2] = plan->close_function;
    functions[3] = plan->rename_function;
    functions[4] = plan->check_function;
    functions[5] = plan->print_function;
    functions[6] = plan->gets_function;
    functions[7] = plan->compare_function;
    functions[8] = plan->remove_function;
    functions[9] = plan->is_print_function;
    functions[10] = plan->is_space_function;
    functions[11] = plan->abort_function;
    for (first = 0; first < 12; ++first) {
        if (functions[first] == NULL)
            return mir_machine_reject(
                "abort-file-runner", "function");
        for (second = first + 1; second < 12; ++second)
            if (functions[first] == functions[second])
                return mir_machine_reject(
                    "abort-file-runner", "function-alias");
    }
    if (!mir_abort_runner_function_types(plan))
        return mir_machine_reject(
            "abort-file-runner", "function-type");
    if (!plan->check_function->is_defined ||
        !plan->check_function->is_static)
        return mir_machine_reject(
            "abort-file-runner", "checker-linkage");

    for (item = 0; item < 3; ++item)
        if (!mir_abort_runner_call(
                open_calls[item], plan->open_function,
                open_ordinals[item], 2,
                open_definitions[item]) ||
            !mir_abort_runner_call(
                close_calls[item], plan->close_function,
                close_ordinals[item], 1,
                &close_definitions[item]))
            return mir_machine_reject(
                "abort-file-runner", "file-call");
    arguments[0] = 8;
    arguments[1] = 10;
    if (!mir_abort_runner_call(
            12, plan->puts_function, 1, 2, arguments))
        return mir_machine_reject(
            "abort-file-runner", "puts-call");
    arguments[0] = 18;
    arguments[1] = 20;
    if (!mir_abort_runner_call(
            22, plan->rename_function, 4, 2, arguments))
        return mir_machine_reject(
            "abort-file-runner", "rename-call");
    arguments[0] = 16;
    arguments[1] = 22;
    arguments[2] = 24;
    if (!mir_abort_runner_call(
            26, plan->check_function, 3, 3, arguments))
        return mir_machine_reject(
            "abort-file-runner", "rename-check");
    arguments[0] = 48;
    arguments[1] = 50;
    arguments[2] = 52;
    if (!mir_abort_runner_call(
            54, plan->gets_function, 7, 3, arguments))
        return mir_machine_reject(
            "abort-file-runner", "gets-call");
    arguments[0] = 60;
    arguments[1] = 62;
    if (!mir_abort_runner_call(
            64, plan->compare_function, 10, 2, arguments))
        return mir_machine_reject(
            "abort-file-runner", "compare-call");
    arguments[0] = 58;
    arguments[1] = 64;
    arguments[2] = 66;
    if (!mir_abort_runner_call(
            68, plan->check_function, 9, 3, arguments))
        return mir_machine_reject(
            "abort-file-runner", "content-check");
    arguments[0] = 92;
    if (!mir_abort_runner_call(
            94, plan->remove_function, 14, 1, arguments))
        return mir_machine_reject(
            "abort-file-runner", "remove-call");
    arguments[0] = 38;
    if (!mir_abort_runner_call(
            40, plan->print_function, 6, 1, arguments))
        return mir_machine_reject(
            "abort-file-runner", "missing-print");
    arguments[0] = 80;
    if (!mir_abort_runner_call(
            82, plan->print_function, 12, 1, arguments))
        return mir_machine_reject(
            "abort-file-runner", "old-print");
    arguments[0] = 251;
    arguments[1] = 253;
    if (!mir_abort_runner_call(
            255, plan->print_function, 36, 2, arguments))
        return mir_machine_reject(
            "abort-file-runner", "failure-print");
    arguments[0] = 260;
    if (!mir_abort_runner_call(
            262, plan->print_function, 37, 1, arguments))
        return mir_machine_reject(
            "abort-file-runner", "success-print");
    if (!mir_abort_runner_no_argument_call(
            263, plan->abort_function, 38))
        return mir_machine_reject(
            "abort-file-runner", "abort-call");
    arguments[0] = 264;
    if (!mir_abort_runner_call(
            266, plan->print_function, 39, 1, arguments))
        return mir_machine_reject(
            "abort-file-runner", "post-abort-print");

    for (item = 0; item < MIR_ABORT_STRING_COUNT; ++item) {
        const struct MirInsn *string =
            &mir.insns[string_instructions[item]];

        if (!mir_abort_runner_pointer_type(
                string->type, TYPE_CHAR))
            return mir_machine_reject(
                "abort-file-runner", "string-type");
        plan->strings[item] = (int)string->immediate;
        for (second = 0; second < item; ++second)
            if (plan->strings[item] == plan->strings[second])
                return mir_machine_reject(
                    "abort-file-runner", "string-alias");
    }
    if (mir.insns[18].immediate !=
            plan->strings[MIR_ABORT_OLD_NAME] ||
        mir.insns[71].immediate !=
            plan->strings[MIR_ABORT_OLD_NAME] ||
        mir.insns[27].immediate !=
            plan->strings[MIR_ABORT_NEW_NAME] ||
        mir.insns[73].immediate !=
            plan->strings[MIR_ABORT_READ_MODE] ||
        mir.insns[92].immediate !=
            plan->strings[MIR_ABORT_NEW_NAME] ||
        mir.insns[62].immediate !=
            plan->strings[MIR_ABORT_CONTENT])
        return mir_machine_reject(
            "abort-file-runner", "string-reuse");

    if (!mir_scalar_memory_location(
            &mir.insns[7], &memory_type,
            &memory_storage, &memory_offset) ||
        memory_storage != SC_LOCAL || memory_offset != -2 ||
        !mir_abort_runner_pointer_type(memory_type, TYPE_INT) ||
        !mir_machine_unobservable_local_store(&mir.insns[7]))
        return mir_machine_reject(
            "abort-file-runner", "file-local");
    for (item = 1;
         item < (int)(sizeof(file_locations) /
                      sizeof(file_locations[0]));
         ++item)
        if (!mir_machine_same_location(
                &mir.insns[file_locations[0]],
                &mir.insns[file_locations[item]]))
            return mir_machine_reject(
                "abort-file-runner", "file-location");
    if (!mir_machine_same_location(
            &mir.insns[7], &mir.insns[6]) ||
        !mir_machine_same_location(
            &mir.insns[33], &mir.insns[32]) ||
        !mir_machine_same_location(
            &mir.insns[77], &mir.insns[76]) ||
        mir.insns[7].src1 != mir.insns[5].dst ||
        mir.insns[33].src1 != mir.insns[31].dst ||
        mir.insns[77].src1 != mir.insns[75].dst)
        return mir_machine_reject(
            "abort-file-runner", "file-declaration");

    if (!mir_scalar_memory_location(
            &mir.insns[48], &memory_type,
            &memory_storage, &memory_offset) ||
        memory_storage != SC_LOCAL || memory_offset != -10 ||
        (memory_type & 15) != TYPE_CHAR ||
        type_size(memory_type) != 1 ||
        strcmp(mir.insns[48].name, mir.insns[60].name) ||
        strcmp(mir.insns[48].name, mir.insns[50].name) ||
        !mir_machine_constant_equals(mir.insns[50].dst, 8))
        return mir_machine_reject(
            "abort-file-runner", "buffer");

    plan->failures = find_global(mir.insns[41].name);
    if (plan->failures == NULL ||
        plan->failures->storage != SC_GLOBAL ||
        !plan->failures->is_defined ||
        !plan->failures->is_static ||
        plan->failures->is_array ||
        plan->failures->is_volatile ||
        !mir_abort_runner_word_type(plan->failures->type) ||
        !mir_scalar_memory_location(
            &mir.insns[41], &memory_type,
            &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL || memory_offset != 0)
        return mir_machine_reject(
            "abort-file-runner", "failure-global");
    for (item = 1;
         item < (int)(sizeof(failure_locations) /
                      sizeof(failure_locations[0]));
         ++item)
        if (!mir_machine_same_location(
                &mir.insns[failure_locations[0]],
                &mir.insns[failure_locations[item]]))
            return mir_machine_reject(
                "abort-file-runner", "failure-location");
    if (!mir_machine_constant_equals(mir.insns[42].dst, 1) ||
        mir.insns[43].immediate != '+' ||
        mir.insns[43].src1 != mir.insns[41].dst ||
        mir.insns[43].src2 != mir.insns[42].dst ||
        mir.insns[44].src1 != mir.insns[43].dst ||
        !mir_machine_constant_equals(mir.insns[87].dst, 1) ||
        mir.insns[88].immediate != '+' ||
        mir.insns[88].src1 != mir.insns[86].dst ||
        mir.insns[88].src2 != mir.insns[87].dst ||
        mir.insns[89].src1 != mir.insns[88].dst)
        return mir_machine_reject(
            "abort-file-runner", "failure-update");

    if (mir.insns[35].immediate != '!' ||
        mir.insns[35].src1 != mir.insns[34].dst ||
        mir.insns[36].src1 != mir.insns[35].dst ||
        mir.insns[36].label != mir.insns[47].label ||
        mir.insns[46].label != mir.insns[70].label ||
        mir.insns[79].src1 != mir.insns[78].dst ||
        mir.insns[79].label != mir.insns[91].label ||
        mir.insns[250].src1 != mir.insns[249].dst ||
        mir.insns[250].label != mir.insns[259].label)
        return mir_machine_reject(
            "abort-file-runner", "branch");

    for (item = 0; item < 7; ++item) {
        int start = graph_starts[item];

        plan->graph_characters[item] = graph_characters[item];
        plan->graph_expected[item] = graph_expected[item];
        arguments[0] = start + 2;
        if (!mir_abort_runner_call(
                start + 4, plan->is_print_function,
                16 + item * 3, 1, arguments))
            return mir_machine_reject(
                "abort-file-runner", "printable-call");
        arguments[0] = start + 6;
        if (!mir_abort_runner_call(
                start + 8, plan->is_space_function,
                17 + item * 3, 1, arguments))
            return mir_machine_reject(
                "abort-file-runner", "space-call");
        arguments[0] = start;
        arguments[1] = start + 17;
        arguments[2] = start + 19;
        if (!mir_abort_runner_call(
                start + 21, plan->check_function,
                15 + item * 3, 3, arguments) ||
            !mir_machine_constant_equals(
                mir.insns[start + 2].dst,
                graph_characters[item]) ||
            !mir_machine_constant_equals(
                mir.insns[start + 6].dst,
                graph_characters[item]) ||
            mir.insns[start + 5].src1 !=
                mir.insns[start + 4].dst ||
            mir.insns[start + 5].label !=
                mir.insns[start + 14].label ||
            mir.insns[start + 9].immediate != '!' ||
            mir.insns[start + 9].src1 !=
                mir.insns[start + 8].dst ||
            mir.insns[start + 10].src1 !=
                mir.insns[start + 9].dst ||
            mir.insns[start + 10].label !=
                mir.insns[start + 14].label ||
            !mir_machine_constant_equals(
                mir.insns[start + 12].dst, 1) ||
            mir.insns[start + 13].label !=
                mir.insns[start + 16].label ||
            !mir_machine_constant_equals(
                mir.insns[start + 15].dst, 0) ||
            mir.insns[start + 17].src1 !=
                mir.insns[start + 12].dst ||
            mir.insns[start + 17].src2 !=
                mir.insns[start + 15].dst ||
            mir.insns[start + 17].phi_pred1 !=
                mir.insns[start + 11].label ||
            mir.insns[start + 17].phi_pred2 !=
                mir.insns[start + 14].label ||
            !mir_machine_constant_equals(
                mir.insns[start + 19].dst,
                graph_expected[item]))
            return mir_machine_reject(
                "abort-file-runner", "graph-check");
    }

    if (!mir_machine_constant_equals(mir.insns[24].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[66].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[256].dst, 1) ||
        mir.insns[257].src1 != mir.insns[256].dst ||
        !mir_machine_constant_equals(mir.insns[267].dst, 1) ||
        mir.insns[268].src1 != mir.insns[267].dst)
        return mir_machine_reject(
            "abort-file-runner", "return");
    return 1;
}

static int mir_file_io_function_types(
    const struct MirFileIoRunner *plan)
{
    return
        mir_abort_runner_pointer_type(
            plan->open_function->type, TYPE_INT) &&
        mir_abort_runner_pointer_type(
            plan->open_function->proto_types[0], TYPE_CHAR) &&
        mir_abort_runner_pointer_type(
            plan->open_function->proto_types[1], TYPE_CHAR) &&
        mir_abort_runner_pointer_type(
            plan->reopen_function->type, TYPE_INT) &&
        mir_abort_runner_pointer_type(
            plan->reopen_function->proto_types[0], TYPE_CHAR) &&
        mir_abort_runner_pointer_type(
            plan->reopen_function->proto_types[1], TYPE_CHAR) &&
        mir_abort_runner_pointer_type(
            plan->reopen_function->proto_types[2], TYPE_INT) &&
        mir_abort_runner_word_type(plan->close_function->type) &&
        mir_abort_runner_pointer_type(
            plan->close_function->proto_types[0], TYPE_INT) &&
        mir_abort_runner_word_type(plan->remove_function->type) &&
        mir_abort_runner_pointer_type(
            plan->remove_function->proto_types[0], TYPE_CHAR) &&
        mir_abort_runner_pointer_type(
            plan->read_function->type, TYPE_CHAR) &&
        mir_abort_runner_pointer_type(
            plan->read_function->proto_types[0], TYPE_CHAR) &&
        mir_abort_runner_word_type(
            plan->read_function->proto_types[1]) &&
        mir_abort_runner_pointer_type(
            plan->read_function->proto_types[2], TYPE_INT) &&
        mir_abort_runner_word_type(plan->write_function->type) &&
        mir_abort_runner_pointer_type(
            plan->write_function->proto_types[0], TYPE_CHAR) &&
        mir_abort_runner_pointer_type(
            plan->write_function->proto_types[1], TYPE_INT) &&
        mir_abort_runner_word_type(plan->compare_function->type) &&
        mir_abort_runner_pointer_type(
            plan->compare_function->proto_types[0], TYPE_CHAR) &&
        mir_abort_runner_pointer_type(
            plan->compare_function->proto_types[1], TYPE_CHAR) &&
        mir_abort_runner_word_type(plan->print_function->type) &&
        mir_abort_runner_pointer_type(
            plan->print_function->proto_types[0], TYPE_CHAR);
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

static int mir_file_io_trim_loop(int base)
{
    const struct MirInsn *index_phi = &mir.insns[base + 4];
    const struct MirInsn *newline_phi = &mir.insns[base + 24];
    const struct MirInsn *return_phi = &mir.insns[base + 40];
    int buffer_addresses[] = {
        base + 5, base + 10, base + 26, base + 49
    };
    int item;

    if (!mir_machine_constant_equals(
            mir.insns[base].dst, 0) ||
        mir.insns[base + 2].src1 != mir.insns[base].dst ||
        !mir_machine_unobservable_local_store(
            &mir.insns[base + 2]) ||
        index_phi->src1 != mir.insns[base].dst ||
        index_phi->src2 != mir.insns[base + 45].dst ||
        index_phi->phi_pred1 != mir.insns[base - 1].label ||
        index_phi->phi_pred2 != mir.insns[base + 42].label ||
        !mir_machine_same_location(
            &mir.insns[base + 2], index_phi) ||
        mir.insns[base + 7].src1 != mir.insns[base + 5].dst ||
        mir.insns[base + 7].src2 != index_phi->dst ||
        mir.insns[base + 7].immediate != 1 ||
        mir.insns[base + 7].memory_size != 1 ||
        mir.insns[base + 8].src1 != mir.insns[base + 7].dst ||
        mir.insns[base + 8].memory_size != 1 ||
        mir.insns[base + 9].src1 != mir.insns[base + 8].dst ||
        mir.insns[base + 9].label != mir.insns[base + 21].label ||
        mir.insns[base + 12].src1 != mir.insns[base + 10].dst ||
        mir.insns[base + 12].src2 != index_phi->dst ||
        mir.insns[base + 12].immediate != 1 ||
        mir.insns[base + 12].memory_size != 1 ||
        mir.insns[base + 13].src1 != mir.insns[base + 12].dst ||
        mir.insns[base + 13].memory_size != 1 ||
        !mir_machine_constant_equals(
            mir.insns[base + 14].dst, 10) ||
        mir.insns[base + 15].immediate != 0 ||
        mir.insns[base + 15].src1 != mir.insns[base + 13].dst ||
        mir.insns[base + 16].immediate != TOK_NE ||
        mir.insns[base + 16].src1 != mir.insns[base + 15].dst ||
        mir.insns[base + 16].src2 != mir.insns[base + 14].dst ||
        mir.insns[base + 17].src1 != mir.insns[base + 16].dst ||
        mir.insns[base + 17].label != mir.insns[base + 21].label ||
        !mir_machine_constant_equals(
            mir.insns[base + 19].dst, 1) ||
        mir.insns[base + 20].label != mir.insns[base + 23].label ||
        !mir_machine_constant_equals(
            mir.insns[base + 22].dst, 0) ||
        newline_phi->src1 != mir.insns[base + 19].dst ||
        newline_phi->src2 != mir.insns[base + 22].dst ||
        newline_phi->phi_pred1 != mir.insns[base + 18].label ||
        newline_phi->phi_pred2 != mir.insns[base + 21].label ||
        mir.insns[base + 25].src1 != newline_phi->dst ||
        mir.insns[base + 25].label != mir.insns[base + 37].label ||
        mir.insns[base + 28].src1 != mir.insns[base + 26].dst ||
        mir.insns[base + 28].src2 != index_phi->dst ||
        mir.insns[base + 28].immediate != 1 ||
        mir.insns[base + 28].memory_size != 1 ||
        mir.insns[base + 29].src1 != mir.insns[base + 28].dst ||
        mir.insns[base + 29].memory_size != 1 ||
        !mir_machine_constant_equals(
            mir.insns[base + 30].dst, 13) ||
        mir.insns[base + 31].immediate != 0 ||
        mir.insns[base + 31].src1 != mir.insns[base + 29].dst ||
        mir.insns[base + 32].immediate != TOK_NE ||
        mir.insns[base + 32].src1 != mir.insns[base + 31].dst ||
        mir.insns[base + 32].src2 != mir.insns[base + 30].dst ||
        mir.insns[base + 33].src1 != mir.insns[base + 32].dst ||
        mir.insns[base + 33].label != mir.insns[base + 37].label ||
        !mir_machine_constant_equals(
            mir.insns[base + 35].dst, 1) ||
        mir.insns[base + 36].label != mir.insns[base + 39].label ||
        !mir_machine_constant_equals(
            mir.insns[base + 38].dst, 0) ||
        return_phi->src1 != mir.insns[base + 35].dst ||
        return_phi->src2 != mir.insns[base + 38].dst ||
        return_phi->phi_pred1 != mir.insns[base + 34].label ||
        return_phi->phi_pred2 != mir.insns[base + 37].label ||
        mir.insns[base + 41].src1 != return_phi->dst ||
        mir.insns[base + 41].label != mir.insns[base + 48].label ||
        !mir_machine_constant_equals(
            mir.insns[base + 44].dst, 1) ||
        mir.insns[base + 45].immediate != '+' ||
        mir.insns[base + 45].src1 != index_phi->dst ||
        mir.insns[base + 45].src2 != mir.insns[base + 44].dst ||
        mir.insns[base + 46].src1 != mir.insns[base + 45].dst ||
        !mir_machine_same_location(
            &mir.insns[base + 2], &mir.insns[base + 46]) ||
        mir.insns[base + 47].label != mir.insns[base + 3].label ||
        mir.insns[base + 51].src1 != mir.insns[base + 49].dst ||
        mir.insns[base + 51].src2 != index_phi->dst ||
        mir.insns[base + 51].immediate != 1 ||
        mir.insns[base + 51].memory_size != 1 ||
        !mir_machine_constant_equals(
            mir.insns[base + 53].dst, 0) ||
        type_size(mir.insns[base + 53].type) != 1 ||
        mir.insns[base + 54].src1 != mir.insns[base + 51].dst ||
        mir.insns[base + 54].src2 != mir.insns[base + 53].dst ||
        mir.insns[base + 54].memory_size != 1)
        return 0;
    for (item = 1; item < 4; ++item)
        if (!mir_machine_same_location(
                &mir.insns[buffer_addresses[0]],
                &mir.insns[buffer_addresses[item]]))
            return 0;
    return 1;
}

static int mir_match_file_io_runner(struct MirFileIoRunner *plan)
{
    static const int expected_opcodes[316] = {
        MIR_LABEL, MIR_STRADDR, MIR_ARG, MIR_STRADDR, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_STORE, MIR_LOAD, MIR_UNARY, MIR_BRFALSE, MIR_STRADDR,
        MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN, MIR_NOP, MIR_LABEL,
        MIR_STRADDR, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_LOAD,
        MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_STRADDR, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_STORE, MIR_LOAD, MIR_UNARY, MIR_BRFALSE,
        MIR_STRADDR, MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN, MIR_NOP,
        MIR_LABEL, MIR_STRADDR, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_LOAD, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_STRADDR,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE, MIR_LOAD, MIR_UNARY,
        MIR_BRFALSE, MIR_STRADDR, MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN,
        MIR_NOP, MIR_LABEL, MIR_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_LOAD, MIR_ARG, MIR_CALL, MIR_UNARY, MIR_BRFALSE, MIR_STRADDR,
        MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN, MIR_NOP, MIR_LABEL,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_PHI, MIR_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BRFALSE,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRFALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRFALSE,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRFALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRFALSE,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_ARG, MIR_STRADDR,
        MIR_ARG, MIR_CALL, MIR_CONST, MIR_BINARY, MIR_BRFALSE, MIR_STRADDR,
        MIR_ARG, MIR_ADDRESS, MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN,
        MIR_NOP, MIR_LABEL, MIR_STRADDR, MIR_ARG, MIR_STRADDR, MIR_ARG,
        MIR_LOAD, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE, MIR_LOAD,
        MIR_UNARY, MIR_BRFALSE, MIR_STRADDR, MIR_ARG, MIR_CALL, MIR_CONST,
        MIR_RETURN, MIR_NOP, MIR_LABEL, MIR_ADDRESS, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_UNARY, MIR_BRFALSE,
        MIR_STRADDR, MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN, MIR_NOP,
        MIR_LABEL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_PHI,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_BRFALSE, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRFALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_PHI, MIR_BRFALSE, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRFALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_PHI, MIR_BRFALSE, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT,
        MIR_ADDRESS, MIR_ARG, MIR_STRADDR, MIR_ARG, MIR_CALL, MIR_CONST,
        MIR_BINARY, MIR_BRFALSE, MIR_STRADDR, MIR_ARG, MIR_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN, MIR_NOP, MIR_LABEL,
        MIR_LOAD, MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_STRADDR,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_LOAD,
        MIR_BRFALSE, MIR_NOP, MIR_STRADDR, MIR_ARG, MIR_STRADDR, MIR_ARG,
        MIR_LOAD, MIR_ARG, MIR_CALL, MIR_STORE, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_BRFALSE, MIR_STRADDR, MIR_ARG, MIR_CALL, MIR_CONST,
        MIR_RETURN, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_STRADDR,
        MIR_ARG, MIR_CALL, MIR_STRADDR, MIR_ARG, MIR_CALL, MIR_STRADDR,
        MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN
    };
    static const int string_instructions[MIR_FILE_IO_STRING_COUNT] = {
        11, 36, 61, 77, 141, 147, 168, 184, 248, 254,
        53, 284, 296, 1, 3, 18, 26, 43, 311
    };
    static const int string_uses[][2] = {
        {28, MIR_FILE_IO_WRITE_MODE},
        {51, MIR_FILE_IO_FIRST_NAME},
        {156, MIR_FILE_IO_SECOND_NAME},
        {158, MIR_FILE_IO_READ_MODE},
        {266, MIR_FILE_IO_FIRST_NAME},
        {268, MIR_FILE_IO_READ_MODE},
        {286, MIR_FILE_IO_READ_MODE},
        {305, MIR_FILE_IO_FIRST_NAME},
        {308, MIR_FILE_IO_SECOND_NAME}
    };
    static const int file_locations[] = {
        6, 7, 8, 20, 23, 31, 32, 33, 45, 48, 56, 57, 58,
        72, 160, 163, 164, 165, 179, 263, 271, 272, 277, 281, 288
    };
    static const int buffer_addresses[] = {
        68, 89, 94, 110, 133, 139, 149,
        175, 196, 201, 217, 240, 246, 256
    };
    static const int open_calls[] = {5, 30, 55, 270};
    static const int open_ordinals[] = {0, 4, 8, 21};
    static const int open_arguments[][2] = {
        {1, 3}, {26, 28}, {51, 53}, {266, 268}
    };
    static const int print_calls[] = {
        13, 38, 63, 79, 151, 170, 186, 258, 298, 313
    };
    static const int print_ordinals[] = {
        1, 5, 9, 11, 13, 15, 17, 19, 23, 27
    };
    static const int print_arguments[][2] = {
        {11, -1}, {36, -1}, {61, -1}, {77, -1}, {147, 149},
        {168, -1}, {184, -1}, {254, 256}, {296, -1}, {311, -1}
    };
    static const int branch_pairs[][2] = {
        {10, 17}, {35, 42}, {60, 67}, {76, 83}, {146, 155},
        {167, 174}, {183, 190}, {253, 262}, {282, 304}, {295, 302}
    };
    static const int return_pairs[][2] = {
        {14, 15}, {39, 40}, {64, 65}, {80, 81}, {152, 153},
        {171, 172}, {187, 188}, {259, 260}, {299, 300}, {314, 315}
    };
    struct Sym *functions[8];
    int arguments[3];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;
    int first;
    int second;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 316 || mir_cfg_block_count() != 29 ||
        mir.has_vla || mir.local_bytes != 40 ||
        mir.aggregate_temp_bytes != 0 ||
        !mir_abort_runner_word_type(mir.return_type))
        return mir_machine_reject("file-io-runner", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
                expected_opcodes[instruction])
            return mir_machine_reject("file-io-runner", "opcode");

    plan->open_function =
        mir_abort_runner_function(5, 0, 2, 0);
    plan->reopen_function =
        mir_abort_runner_function(162, 0, 3, 0);
    plan->close_function =
        mir_abort_runner_function(25, 0, 1, 0);
    plan->remove_function =
        mir_abort_runner_function(307, 0, 1, 0);
    plan->read_function =
        mir_abort_runner_function(74, 0, 3, 0);
    plan->write_function =
        mir_abort_runner_function(22, 0, 2, 0);
    plan->compare_function =
        mir_abort_runner_function(143, 0, 2, 0);
    plan->print_function =
        mir_abort_runner_function(13, 1, 1, 0);
    functions[0] = plan->open_function;
    functions[1] = plan->reopen_function;
    functions[2] = plan->close_function;
    functions[3] = plan->remove_function;
    functions[4] = plan->read_function;
    functions[5] = plan->write_function;
    functions[6] = plan->compare_function;
    functions[7] = plan->print_function;
    for (first = 0; first < 8; ++first) {
        if (functions[first] == NULL)
            return mir_machine_reject("file-io-runner", "function");
        for (second = first + 1; second < 8; ++second)
            if (functions[first] == functions[second])
                return mir_machine_reject(
                    "file-io-runner", "function-alias");
    }
    if (!mir_file_io_function_types(plan))
        return mir_machine_reject(
            "file-io-runner", "function-type");

    for (item = 0; item < 4; ++item)
        if (!mir_abort_runner_call(
                open_calls[item], plan->open_function,
                open_ordinals[item], 2, open_arguments[item]))
            return mir_machine_reject(
                "file-io-runner", "open-call");
    arguments[0] = 156;
    arguments[1] = 158;
    arguments[2] = 160;
    if (!mir_abort_runner_call(
            162, plan->reopen_function, 14, 3, arguments))
        return mir_machine_reject(
            "file-io-runner", "reopen-call");
    arguments[0] = 284;
    arguments[1] = 286;
    arguments[2] = 288;
    if (!mir_abort_runner_call(
            290, plan->reopen_function, 24, 3, arguments))
        return mir_machine_reject(
            "file-io-runner", "missing-reopen-call");
    {
        static const int calls[] = {25, 50, 265};
        static const int ordinals[] = {3, 7, 20};
        static const int definitions[] = {23, 48, 263};

        for (item = 0; item < 3; ++item) {
            arguments[0] = definitions[item];
            if (!mir_abort_runner_call(
                    calls[item], plan->close_function,
                    ordinals[item], 1, arguments))
                return mir_machine_reject(
                    "file-io-runner", "close-call");
        }
    }
    {
        static const int calls[] = {307, 310};
        static const int ordinals[] = {25, 26};
        static const int definitions[] = {305, 308};

        for (item = 0; item < 2; ++item) {
            arguments[0] = definitions[item];
            if (!mir_abort_runner_call(
                    calls[item], plan->remove_function,
                    ordinals[item], 1, arguments))
                return mir_machine_reject(
                    "file-io-runner", "remove-call");
        }
    }
    {
        static const int calls[] = {74, 181};
        static const int ordinals[] = {10, 16};
        static const int definitions[][3] = {
            {68, 70, 72}, {175, 177, 179}
        };

        for (item = 0; item < 2; ++item)
            if (!mir_abort_runner_call(
                    calls[item], plan->read_function,
                    ordinals[item], 3, definitions[item]))
                return mir_machine_reject(
                    "file-io-runner", "read-call");
    }
    {
        static const int calls[] = {22, 47};
        static const int ordinals[] = {2, 6};
        static const int definitions[][2] = {
            {18, 20}, {43, 45}
        };

        for (item = 0; item < 2; ++item)
            if (!mir_abort_runner_call(
                    calls[item], plan->write_function,
                    ordinals[item], 2, definitions[item]))
                return mir_machine_reject(
                    "file-io-runner", "write-call");
    }
    {
        static const int calls[] = {143, 250};
        static const int ordinals[] = {12, 18};
        static const int definitions[][2] = {
            {139, 141}, {246, 248}
        };

        for (item = 0; item < 2; ++item)
            if (!mir_abort_runner_call(
                    calls[item], plan->compare_function,
                    ordinals[item], 2, definitions[item]))
                return mir_machine_reject(
                    "file-io-runner", "compare-call");
    }
    for (item = 0; item < 10; ++item) {
        arguments[0] = print_arguments[item][0];
        if (print_arguments[item][1] < 0) {
            if (!mir_abort_runner_call(
                    print_calls[item], plan->print_function,
                    print_ordinals[item], 1, arguments))
                return mir_machine_reject(
                    "file-io-runner", "print-call");
        } else {
            arguments[1] = print_arguments[item][1];
            if (!mir_abort_runner_call(
                    print_calls[item], plan->print_function,
                    print_ordinals[item], 2, arguments))
                return mir_machine_reject(
                    "file-io-runner", "print-call");
        }
    }

    for (item = 0; item < MIR_FILE_IO_STRING_COUNT; ++item) {
        const struct MirInsn *string =
            &mir.insns[string_instructions[item]];

        if (!mir_abort_runner_pointer_type(
                string->type, TYPE_CHAR))
            return mir_machine_reject(
                "file-io-runner", "string-type");
        plan->strings[item] = (int)string->immediate;
        for (second = 0; second < item; ++second)
            if (plan->strings[item] == plan->strings[second])
                return mir_machine_reject(
                    "file-io-runner", "string-alias");
    }
    for (item = 0;
         item < (int)(sizeof(string_uses) /
                      sizeof(string_uses[0]));
         ++item)
        if (mir.insns[string_uses[item][0]].immediate !=
                plan->strings[string_uses[item][1]])
            return mir_machine_reject(
                "file-io-runner", "string-reuse");

    if (!mir_scalar_memory_location(
            &mir.insns[7], &memory_type,
            &memory_storage, &memory_offset) ||
        memory_storage != SC_LOCAL || memory_offset != -2 ||
        !mir_abort_runner_pointer_type(memory_type, TYPE_INT) ||
        !mir_machine_unobservable_local_store(&mir.insns[7]))
        return mir_machine_reject(
            "file-io-runner", "stream-local");
    for (item = 1;
         item < (int)(sizeof(file_locations) /
                      sizeof(file_locations[0]));
         ++item)
        if (!mir_machine_same_location(
                &mir.insns[file_locations[0]],
                &mir.insns[file_locations[item]]))
            return mir_machine_reject(
                "file-io-runner", "stream-location");
    if (mir.insns[7].src1 != mir.insns[5].dst ||
        mir.insns[32].src1 != mir.insns[30].dst ||
        mir.insns[57].src1 != mir.insns[55].dst ||
        mir.insns[164].src1 != mir.insns[162].dst ||
        mir.insns[272].src1 != mir.insns[270].dst)
        return mir_machine_reject(
            "file-io-runner", "stream-update");

    if (!mir_scalar_memory_location(
            &mir.insns[68], &memory_type,
            &memory_storage, &memory_offset) ||
        memory_storage != SC_LOCAL || memory_offset != -34 ||
        !mir_abort_runner_pointer_type(
            mir.insns[68].type, TYPE_CHAR))
        return mir_machine_reject(
            "file-io-runner", "buffer");
    for (item = 1;
         item < (int)(sizeof(buffer_addresses) /
                      sizeof(buffer_addresses[0]));
         ++item)
        if (!mir_machine_same_location(
                &mir.insns[buffer_addresses[0]],
                &mir.insns[buffer_addresses[item]]))
            return mir_machine_reject(
                "file-io-runner", "buffer-location");
    if (!mir_machine_constant_equals(mir.insns[70].dst, 32) ||
        !mir_machine_constant_equals(mir.insns[177].dst, 32) ||
        !mir_file_io_trim_loop(84) ||
        !mir_file_io_trim_loop(191))
        return mir_machine_reject(
            "file-io-runner", "buffer-flow");
    if (!mir_scalar_memory_location(
            &mir.insns[86], &memory_type,
            &memory_storage, &memory_offset) ||
        memory_storage != SC_LOCAL || memory_offset != -38 ||
        !mir_abort_runner_word_type(memory_type) ||
        !mir_machine_same_location(
            &mir.insns[86], &mir.insns[193]))
        return mir_machine_reject(
            "file-io-runner", "index-local");

    if (!mir_scalar_memory_location(
            &mir.insns[291], &memory_type,
            &memory_storage, &memory_offset) ||
        memory_storage != SC_LOCAL || memory_offset != -40 ||
        !mir_abort_runner_pointer_type(memory_type, TYPE_INT) ||
        !mir_machine_same_location(
            &mir.insns[291], &mir.insns[292]) ||
        mir_machine_same_location(
            &mir.insns[291], &mir.insns[7]) ||
        mir.insns[291].src1 != mir.insns[290].dst)
        return mir_machine_reject(
            "file-io-runner", "secondary-stream");

    for (item = 0;
         item < (int)(sizeof(branch_pairs) /
                      sizeof(branch_pairs[0]));
         ++item)
        if (mir.insns[branch_pairs[item][0]].label !=
                mir.insns[branch_pairs[item][1]].label)
            return mir_machine_reject(
                "file-io-runner", "branch");
    if (mir.insns[9].immediate != '!' ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[34].immediate != '!' ||
        mir.insns[34].src1 != mir.insns[33].dst ||
        mir.insns[35].src1 != mir.insns[34].dst ||
        mir.insns[59].immediate != '!' ||
        mir.insns[59].src1 != mir.insns[58].dst ||
        mir.insns[60].src1 != mir.insns[59].dst ||
        mir.insns[75].immediate != '!' ||
        mir.insns[75].src1 != mir.insns[74].dst ||
        mir.insns[76].src1 != mir.insns[75].dst ||
        mir.insns[166].immediate != '!' ||
        mir.insns[166].src1 != mir.insns[165].dst ||
        mir.insns[167].src1 != mir.insns[166].dst ||
        mir.insns[182].immediate != '!' ||
        mir.insns[182].src1 != mir.insns[181].dst ||
        mir.insns[183].src1 != mir.insns[182].dst ||
        !mir_machine_constant_equals(mir.insns[144].dst, 0) ||
        mir.insns[145].immediate != TOK_NE ||
        mir.insns[145].src1 != mir.insns[143].dst ||
        mir.insns[145].src2 != mir.insns[144].dst ||
        mir.insns[146].src1 != mir.insns[145].dst ||
        !mir_machine_constant_equals(mir.insns[251].dst, 0) ||
        mir.insns[252].immediate != TOK_NE ||
        mir.insns[252].src1 != mir.insns[250].dst ||
        mir.insns[252].src2 != mir.insns[251].dst ||
        mir.insns[253].src1 != mir.insns[252].dst ||
        mir.insns[282].src1 != mir.insns[281].dst ||
        !mir_machine_constant_equals(mir.insns[293].dst, 0) ||
        mir.insns[294].immediate != TOK_NE ||
        mir.insns[294].src1 != mir.insns[292].dst ||
        mir.insns[294].src2 != mir.insns[293].dst ||
        mir.insns[295].src1 != mir.insns[294].dst)
        return mir_machine_reject(
            "file-io-runner", "condition");
    for (item = 0;
         item < (int)(sizeof(return_pairs) /
                      sizeof(return_pairs[0]));
         ++item)
        if (!mir_machine_constant_equals(
                mir.insns[return_pairs[item][0]].dst,
                item == 9 ? 0 : 1) ||
            mir.insns[return_pairs[item][1]].src1 !=
                mir.insns[return_pairs[item][0]].dst)
            return mir_machine_reject(
                "file-io-runner", "return");
    return 1;
}

static int mir_match_allocation_lifetime_runner(
    struct MirAllocationLifetimeRunner *plan)
{
    static const int expected_opcodes[200] = {
        MIR_LABEL, MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP, MIR_UNARY,
        MIR_STORE, MIR_LOAD, MIR_UNARY, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN,
        MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT, MIR_LOAD, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT,
        MIR_LOAD, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_UNARY, MIR_LOAD, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN, MIR_NOP, MIR_LABEL,
        MIR_LOAD, MIR_NOP, MIR_ARG, MIR_CALL, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_UNARY, MIR_STORE, MIR_LOAD, MIR_UNARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_RETURN, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_STORE, MIR_LABEL, MIR_NOP, MIR_PHI, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_NOP, MIR_STORE_INDIRECT,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_LOAD, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN,
        MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_UNARY, MIR_STORE, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_LABEL, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_UNARY, MIR_STORE, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_RETURN, MIR_NOP, MIR_LABEL, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_STORE, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_RETURN, MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_NOP,
        MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_RETURN
    };
    static const int allocation_calls[5] = {3, 63, 146, 161, 177};
    static const int allocation_arguments[5] = {1, 61, 144, 159, 175};
    static const int free_calls[4] = {60, 143, 156, 194};
    static const int free_arguments[4] = {57, 140, 153, 191};
    static const int print_calls[7] = {12, 52, 72, 135, 170, 186, 197};
    static const int string_instructions[7] = {10, 46, 70, 133, 168, 184, 195};
    struct Sym *function;
    int arguments[3];
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 200 || mir_cfg_block_count() != 18 ||
        mir.has_vla || mir.local_bytes != 11 ||
        mir.aggregate_temp_bytes != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return mir_machine_reject(
            "allocation-lifetime-runner", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
                expected_opcodes[instruction])
            return mir_machine_reject(
                "allocation-lifetime-runner", "opcode");

    plan->allocate_function =
        mir_allocation_runner_call_function(3, 0);
    plan->free_function =
        mir_allocation_runner_call_function(60, 0);
    plan->print_function =
        mir_allocation_runner_call_function(12, 1);
    if (plan->allocate_function == NULL ||
        plan->free_function == NULL ||
        plan->print_function == NULL ||
        plan->allocate_function->storage != SC_FUNC ||
        plan->free_function->storage != SC_FUNC ||
        plan->print_function->storage != SC_FUNC ||
        plan->allocate_function->is_funcptr ||
        plan->free_function->is_funcptr ||
        plan->print_function->is_funcptr ||
        plan->allocate_function->is_noreturn ||
        plan->free_function->is_noreturn ||
        plan->print_function->is_noreturn ||
        plan->allocate_function == plan->free_function ||
        plan->allocate_function == plan->print_function ||
        plan->free_function == plan->print_function)
        return mir_machine_reject(
            "allocation-lifetime-runner", "functions");
    for (item = 0; item < 5; ++item) {
        function = mir_allocation_runner_call_function(
            allocation_calls[item], 0);
        if (function != plan->allocate_function ||
            type_ptr_depth(mir.insns[allocation_calls[item]].type) == 0 ||
            !mir_allocation_runner_single_argument(
                allocation_calls[item],
                allocation_arguments[item]))
            return mir_machine_reject(
                "allocation-lifetime-runner", "allocation-call");
    }
    for (item = 0; item < 4; ++item) {
        function = mir_allocation_runner_call_function(
            free_calls[item], 0);
        if (function != plan->free_function ||
            (mir.insns[free_calls[item]].type & 15) != TYPE_VOID ||
            !mir_allocation_runner_single_argument(
                free_calls[item], free_arguments[item]))
            return mir_machine_reject(
                "allocation-lifetime-runner", "free-call");
    }
    for (item = 0; item < 7; ++item) {
        function = mir_allocation_runner_call_function(
            print_calls[item], 1);
        if (function != plan->print_function ||
            type_size(mir.insns[print_calls[item]].type) != 2)
            return mir_machine_reject(
                "allocation-lifetime-runner", "print-call");
        plan->strings[item] =
            (int)mir.insns[string_instructions[item]].immediate;
    }
    if (!mir_allocation_runner_single_argument(12, 10) ||
        !mir_machine_three_call_arguments(&mir.insns[52], arguments) ||
        arguments[0] != mir.insns[46].dst ||
        arguments[1] != mir.insns[39].dst ||
        arguments[2] != mir.insns[50].dst ||
        !mir_allocation_runner_single_argument(72, 70) ||
        !mir_allocation_runner_single_argument(135, 133) ||
        !mir_allocation_runner_single_argument(170, 168) ||
        !mir_allocation_runner_single_argument(186, 184) ||
        !mir_allocation_runner_single_argument(197, 195))
        return mir_machine_reject(
            "allocation-lifetime-runner", "print-arguments");

    if (!mir_machine_constant_equals(mir.insns[1].dst, 32768) ||
        mir.insns[5].immediate != 0 ||
        mir.insns[5].src1 != mir.insns[3].dst ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[6]) ||
        !mir_machine_same_location(&mir.insns[6], &mir.insns[7]) ||
        mir.insns[8].immediate != '!' ||
        mir.insns[8].src1 != mir.insns[7].dst ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].label != mir.insns[16].label ||
        !mir_machine_constant_equals(mir.insns[13].dst, 1) ||
        mir.insns[14].src1 != mir.insns[13].dst)
        return mir_machine_reject(
            "allocation-lifetime-runner", "first-allocation");

    if (!mir_machine_same_location(&mir.insns[6], &mir.insns[17]) ||
        !mir_machine_constant_equals(mir.insns[18].dst, 0) ||
        mir.insns[19].src1 != mir.insns[17].dst ||
        mir.insns[19].src2 != mir.insns[18].dst ||
        mir.insns[19].immediate != 1 ||
        mir.insns[22].src1 != mir.insns[19].dst ||
        mir.insns[22].src2 != mir.insns[21].dst ||
        mir.insns[22].memory_size != 1 ||
        !mir_machine_constant_equals(mir.insns[21].dst, 0x12) ||
        !mir_machine_same_location(&mir.insns[6], &mir.insns[23]) ||
        !mir_machine_constant_equals(mir.insns[24].dst, 32767) ||
        mir.insns[25].src1 != mir.insns[23].dst ||
        mir.insns[25].src2 != mir.insns[24].dst ||
        mir.insns[25].immediate != 1 ||
        mir.insns[28].src1 != mir.insns[25].dst ||
        mir.insns[28].src2 != mir.insns[27].dst ||
        mir.insns[28].memory_size != 1 ||
        !mir_machine_constant_equals(mir.insns[27].dst, 0x34))
        return mir_machine_reject(
            "allocation-lifetime-runner", "large-writes");

    if (!mir_machine_same_location(&mir.insns[6], &mir.insns[29]) ||
        !mir_machine_constant_equals(mir.insns[30].dst, 0) ||
        mir.insns[31].src1 != mir.insns[29].dst ||
        mir.insns[31].src2 != mir.insns[30].dst ||
        mir.insns[31].immediate != 1 ||
        mir.insns[32].src1 != mir.insns[31].dst ||
        mir.insns[32].memory_size != 1 ||
        mir.insns[33].src1 != mir.insns[32].dst ||
        mir.insns[33].immediate != 0 ||
        !mir_machine_same_location(&mir.insns[6], &mir.insns[34]) ||
        !mir_machine_constant_equals(mir.insns[35].dst, 32767) ||
        mir.insns[36].src1 != mir.insns[34].dst ||
        mir.insns[36].src2 != mir.insns[35].dst ||
        mir.insns[36].immediate != 1 ||
        mir.insns[37].src1 != mir.insns[36].dst ||
        mir.insns[37].memory_size != 1 ||
        mir.insns[38].src1 != mir.insns[37].dst ||
        mir.insns[38].immediate != 0 ||
        mir.insns[39].immediate != '+' ||
        mir.insns[39].src1 != mir.insns[33].dst ||
        mir.insns[39].src2 != mir.insns[38].dst ||
        mir.insns[41].src1 != mir.insns[39].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[41]) ||
        !mir_machine_constant_equals(mir.insns[43].dst, 0x46) ||
        mir.insns[44].immediate != TOK_NE ||
        mir.insns[44].src1 != mir.insns[39].dst ||
        mir.insns[44].src2 != mir.insns[43].dst ||
        mir.insns[45].src1 != mir.insns[44].dst ||
        mir.insns[45].label != mir.insns[56].label ||
        !mir_machine_constant_equals(mir.insns[50].dst, 0x46) ||
        !mir_machine_constant_equals(mir.insns[53].dst, 1) ||
        mir.insns[54].src1 != mir.insns[53].dst)
        return mir_machine_reject(
            "allocation-lifetime-runner", "large-check");

    if (!mir_machine_same_location(&mir.insns[6], &mir.insns[57]) ||
        !mir_machine_constant_equals(mir.insns[61].dst, 32) ||
        mir.insns[65].immediate != 0 ||
        mir.insns[65].src1 != mir.insns[63].dst ||
        mir.insns[66].src1 != mir.insns[65].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[66]) ||
        mir_machine_same_location(&mir.insns[6], &mir.insns[66]) ||
        !mir_machine_same_location(&mir.insns[66], &mir.insns[67]) ||
        mir.insns[68].immediate != '!' ||
        mir.insns[68].src1 != mir.insns[67].dst ||
        mir.insns[69].src1 != mir.insns[68].dst ||
        mir.insns[69].label != mir.insns[76].label ||
        !mir_machine_constant_equals(mir.insns[73].dst, 1) ||
        mir.insns[74].src1 != mir.insns[73].dst)
        return mir_machine_reject(
            "allocation-lifetime-runner", "small-allocation");

    if (!mir_machine_constant_equals(mir.insns[78].dst, 0) ||
        mir.insns[79].src1 != mir.insns[78].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[79]) ||
        mir.insns[82].src1 != mir.insns[78].dst ||
        mir.insns[82].src2 != mir.insns[97].dst ||
        mir.insns[82].phi_pred1 != mir.insns[76].label ||
        mir.insns[82].phi_pred2 != mir.insns[94].label ||
        !mir_machine_constant_equals(mir.insns[84].dst, 32) ||
        mir.insns[85].immediate != 0 ||
        mir.insns[85].src1 != mir.insns[82].dst ||
        mir.insns[86].immediate != '<' ||
        mir.insns[86].src1 != mir.insns[85].dst ||
        mir.insns[86].src2 != mir.insns[84].dst ||
        mir.insns[87].src1 != mir.insns[86].dst ||
        mir.insns[87].label != mir.insns[100].label ||
        !mir_machine_same_location(&mir.insns[66], &mir.insns[88]) ||
        mir.insns[90].src1 != mir.insns[88].dst ||
        mir.insns[90].src2 != mir.insns[82].dst ||
        mir.insns[90].immediate != 1 ||
        mir.insns[93].src1 != mir.insns[90].dst ||
        mir.insns[93].src2 != mir.insns[82].dst ||
        mir.insns[93].memory_size != 1 ||
        !mir_machine_constant_equals(mir.insns[96].dst, 1) ||
        mir.insns[97].immediate != '+' ||
        mir.insns[97].src1 != mir.insns[82].dst ||
        mir.insns[97].src2 != mir.insns[96].dst ||
        mir.insns[98].src1 != mir.insns[97].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[98]) ||
        mir.insns[99].label != mir.insns[80].label)
        return mir_machine_reject(
            "allocation-lifetime-runner", "fill-loop");

    if (!mir_machine_same_location(&mir.insns[66], &mir.insns[101]) ||
        !mir_machine_constant_equals(mir.insns[102].dst, 0) ||
        mir.insns[103].src1 != mir.insns[101].dst ||
        mir.insns[103].src2 != mir.insns[102].dst ||
        mir.insns[103].immediate != 1 ||
        mir.insns[104].src1 != mir.insns[103].dst ||
        mir.insns[104].memory_size != 1 ||
        !mir_machine_constant_equals(mir.insns[105].dst, 0) ||
        mir.insns[106].src1 != mir.insns[104].dst ||
        mir.insns[106].immediate != 0 ||
        mir.insns[107].immediate != TOK_NE ||
        mir.insns[107].src1 != mir.insns[106].dst ||
        mir.insns[107].src2 != mir.insns[105].dst ||
        mir.insns[108].src1 != mir.insns[107].dst ||
        mir.insns[108].label != mir.insns[112].label ||
        !mir_machine_same_location(&mir.insns[66], &mir.insns[113]) ||
        !mir_machine_constant_equals(mir.insns[114].dst, 31) ||
        mir.insns[115].src1 != mir.insns[113].dst ||
        mir.insns[115].src2 != mir.insns[114].dst ||
        mir.insns[115].immediate != 1 ||
        mir.insns[116].src1 != mir.insns[115].dst ||
        mir.insns[116].memory_size != 1 ||
        !mir_machine_constant_equals(mir.insns[117].dst, 31) ||
        mir.insns[118].src1 != mir.insns[116].dst ||
        mir.insns[118].immediate != 0 ||
        mir.insns[119].immediate != TOK_NE ||
        mir.insns[119].src1 != mir.insns[118].dst ||
        mir.insns[119].src2 != mir.insns[117].dst ||
        mir.insns[120].src1 != mir.insns[119].dst ||
        mir.insns[120].label != mir.insns[124].label ||
        !mir_machine_constant_equals(mir.insns[110].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[122].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[125].dst, 0) ||
        mir.insns[127].src1 != mir.insns[122].dst ||
        mir.insns[127].src2 != mir.insns[125].dst ||
        mir.insns[127].phi_pred1 != mir.insns[121].label ||
        mir.insns[127].phi_pred2 != mir.insns[124].label ||
        mir.insns[131].src1 != mir.insns[110].dst ||
        mir.insns[131].src2 != mir.insns[127].dst ||
        mir.insns[131].phi_pred1 != mir.insns[109].label ||
        mir.insns[131].phi_pred2 != mir.insns[128].label ||
        mir.insns[132].src1 != mir.insns[131].dst ||
        mir.insns[132].label != mir.insns[139].label ||
        !mir_machine_constant_equals(mir.insns[136].dst, 1) ||
        mir.insns[137].src1 != mir.insns[136].dst)
        return mir_machine_reject(
            "allocation-lifetime-runner", "small-check");

    if (!mir_machine_same_location(&mir.insns[66], &mir.insns[140]) ||
        !mir_machine_constant_equals(mir.insns[144].dst, 32768) ||
        mir.insns[147].immediate != 0 ||
        mir.insns[147].src1 != mir.insns[146].dst ||
        mir.insns[148].src1 != mir.insns[147].dst ||
        !mir_machine_same_location(&mir.insns[6], &mir.insns[148]) ||
        !mir_machine_same_location(&mir.insns[6], &mir.insns[149]) ||
        !mir_machine_constant_equals(mir.insns[150].dst, 0) ||
        mir.insns[151].immediate != TOK_NE ||
        mir.insns[151].src1 != mir.insns[149].dst ||
        mir.insns[151].src2 != mir.insns[150].dst ||
        mir.insns[152].src1 != mir.insns[151].dst ||
        mir.insns[152].label != mir.insns[158].label ||
        !mir_machine_same_location(&mir.insns[6], &mir.insns[153]) ||
        !mir_machine_constant_equals(mir.insns[159].dst, 1) ||
        mir.insns[162].immediate != 0 ||
        mir.insns[162].src1 != mir.insns[161].dst ||
        mir.insns[163].src1 != mir.insns[162].dst ||
        !mir_machine_same_location(&mir.insns[66], &mir.insns[163]) ||
        !mir_machine_same_location(&mir.insns[66], &mir.insns[164]) ||
        !mir_machine_constant_equals(mir.insns[165].dst, 0) ||
        mir.insns[166].immediate != TOK_EQ ||
        mir.insns[166].src1 != mir.insns[164].dst ||
        mir.insns[166].src2 != mir.insns[165].dst ||
        mir.insns[167].src1 != mir.insns[166].dst ||
        mir.insns[167].label != mir.insns[174].label ||
        !mir_machine_constant_equals(mir.insns[171].dst, 1) ||
        mir.insns[172].src1 != mir.insns[171].dst)
        return mir_machine_reject(
            "allocation-lifetime-runner", "reuse");

    if (!mir_machine_constant_equals(mir.insns[175].dst, 65000) ||
        mir.insns[179].src1 != mir.insns[177].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[179]) ||
        mir_machine_same_location(&mir.insns[6], &mir.insns[179]) ||
        mir_machine_same_location(&mir.insns[66], &mir.insns[179]) ||
        !mir_machine_same_location(&mir.insns[179], &mir.insns[180]) ||
        !mir_machine_constant_equals(mir.insns[181].dst, 0) ||
        mir.insns[182].immediate != TOK_NE ||
        mir.insns[182].src1 != mir.insns[180].dst ||
        mir.insns[182].src2 != mir.insns[181].dst ||
        mir.insns[183].src1 != mir.insns[182].dst ||
        mir.insns[183].label != mir.insns[190].label ||
        !mir_machine_constant_equals(mir.insns[187].dst, 1) ||
        mir.insns[188].src1 != mir.insns[187].dst ||
        !mir_machine_same_location(&mir.insns[66], &mir.insns[191]) ||
        !mir_machine_constant_equals(mir.insns[198].dst, 0) ||
        mir.insns[199].src1 != mir.insns[198].dst)
        return mir_machine_reject(
            "allocation-lifetime-runner", "final");
    return 1;
}

static int mir_match_callback_registration_runner(
    struct MirCallbackRegistrationRunner *plan)
{
    static const int expected_opcodes[66] = {
        MIR_LABEL, MIR_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE,
        MIR_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE,
        MIR_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL,
        MIR_JUMP, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST,
        MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_LABEL, MIR_CONST, MIR_RETURN
    };
    static const int callback_addresses[3] = {1, 6, 11};
    static const int arguments[3] = {2, 7, 12};
    static const int calls[3] = {3, 8, 13};
    static const int stores[3] = {5, 10, 15};
    static const int declaration_nops[3] = {4, 9, 14};
    static const int condition_nops[3] = {16, 22, 40};
    struct Sym *function;
    const char *assembly_name;
    int call_count = 0;
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 66 || mir_cfg_block_count() != 18 ||
        mir.has_vla || mir.local_bytes != 6 ||
        mir.aggregate_temp_bytes != 0 ||
        type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        (mir.return_type & TYPE_UNSIGNED) != 0 ||
        type_size(mir.return_type) != 2)
        return mir_machine_reject(
            "callback-registration-runner", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        if (mir.insns[instruction].opcode !=
                expected_opcodes[instruction])
            return mir_machine_reject(
                "callback-registration-runner", "opcode");
        if (mir.insns[instruction].opcode == MIR_CALL)
            ++call_count;
    }
    if (call_count != 5)
        return mir_machine_reject(
            "callback-registration-runner", "call-count");

    for (item = 0; item < 3; ++item) {
        const struct MirInsn *address =
            &mir.insns[callback_addresses[item]];
        const struct MirInsn *argument =
            &mir.insns[arguments[item]];
        const struct MirInsn *call = &mir.insns[calls[item]];
        const struct MirInsn *store = &mir.insns[stores[item]];
        int call_argument;

        plan->callbacks[item] = find_global(address->name);
        if (address->memory_flags != 0 ||
            address->object >= 0 ||
            type_ptr_depth(address->type) != 1 ||
            (address->type & 15) != TYPE_VOID ||
            plan->callbacks[item] == NULL ||
            plan->callbacks[item]->storage != SC_FUNC ||
            !plan->callbacks[item]->is_defined ||
            plan->callbacks[item]->is_funcptr ||
            plan->callbacks[item]->is_noreturn ||
            !plan->callbacks[item]->has_proto ||
            plan->callbacks[item]->proto_variadic ||
            plan->callbacks[item]->proto_nargs != 0 ||
            type_ptr_depth(plan->callbacks[item]->type) != 0 ||
            (plan->callbacks[item]->type & 15) != TYPE_VOID ||
            (item > 0 &&
             (plan->callbacks[item] == plan->callbacks[0] ||
              (item == 2 &&
               plan->callbacks[item] == plan->callbacks[1]))))
            return mir_machine_reject(
                "callback-registration-runner", "callback");
        if (argument->src1 != address->dst ||
            argument->immediate != 0 ||
            argument->secondary_offset != call->secondary_offset ||
            type_ptr_depth(argument->type) != 1 ||
            (argument->type & 15) != TYPE_VOID ||
            !mir_machine_single_call_argument(
                call, &call_argument) ||
            call_argument != address->dst)
            return mir_machine_reject(
                "callback-registration-runner", "argument");
        function = find_global(call->name);
        if (call->src1 >= 0 || call->memory_flags != 0 ||
            type_ptr_depth(call->type) != 0 ||
            (call->type & 15) != TYPE_INT ||
            (call->type & TYPE_UNSIGNED) != 0 ||
            type_size(call->type) != 2 ||
            function == NULL || function->storage != SC_FUNC ||
            function->is_funcptr || function->is_noreturn ||
            !function->has_proto || function->proto_variadic ||
            function->proto_nargs != 1 ||
            type_ptr_depth(function->type) != 0 ||
            (function->type & 15) != TYPE_INT ||
            (function->type & TYPE_UNSIGNED) != 0 ||
            type_size(function->type) != 2 ||
            type_ptr_depth(function->proto_types[0]) != 1 ||
            (function->proto_types[0] & 15) != TYPE_VOID ||
            function->proto_types[0] != address->type ||
            (item > 0 && function != plan->register_function))
            return mir_machine_reject(
                "callback-registration-runner", "registration-call");
        if (item == 0)
            plan->register_function = function;
        if (store->src1 != call->dst ||
            store->memory_size != 2 ||
            type_ptr_depth(store->type) != 0 ||
            (store->type & 15) != TYPE_INT ||
            !mir_machine_unobservable_local_store(store) ||
            !mir_machine_same_location(
                store, &mir.insns[declaration_nops[item]]) ||
            !mir_machine_same_location(
                store, &mir.insns[condition_nops[item]]) ||
            (item > 0 &&
             mir_machine_same_location(
                 store, &mir.insns[stores[0]])) ||
            (item == 2 &&
             mir_machine_same_location(
                 store, &mir.insns[stores[1]])))
            return mir_machine_reject(
                "callback-registration-runner", "result-local");
    }

    if (mir.insns[17].src1 != mir.insns[3].dst ||
        mir.insns[17].label != mir.insns[21].label ||
        !mir_machine_constant_equals(mir.insns[19].dst, 1) ||
        mir.insns[20].label != mir.insns[33].label ||
        mir.insns[23].src1 != mir.insns[8].dst ||
        mir.insns[23].label != mir.insns[27].label ||
        !mir_machine_constant_equals(mir.insns[25].dst, 1) ||
        mir.insns[26].label != mir.insns[29].label ||
        !mir_machine_constant_equals(mir.insns[28].dst, 0) ||
        mir.insns[30].src1 != mir.insns[25].dst ||
        mir.insns[30].src2 != mir.insns[28].dst ||
        mir.insns[30].phi_pred1 != mir.insns[24].label ||
        mir.insns[30].phi_pred2 != mir.insns[27].label ||
        mir.insns[32].label != mir.insns[33].label ||
        mir.insns[34].src1 != mir.insns[19].dst ||
        mir.insns[34].src2 != mir.insns[30].dst ||
        mir.insns[34].phi_pred1 != mir.insns[18].label ||
        mir.insns[34].phi_pred2 != mir.insns[31].label ||
        mir.insns[35].src1 != mir.insns[34].dst ||
        mir.insns[35].label != mir.insns[39].label ||
        !mir_machine_constant_equals(mir.insns[37].dst, 1) ||
        mir.insns[38].label != mir.insns[51].label ||
        mir.insns[41].src1 != mir.insns[13].dst ||
        mir.insns[41].label != mir.insns[45].label ||
        !mir_machine_constant_equals(mir.insns[43].dst, 1) ||
        mir.insns[44].label != mir.insns[47].label ||
        !mir_machine_constant_equals(mir.insns[46].dst, 0) ||
        mir.insns[48].src1 != mir.insns[43].dst ||
        mir.insns[48].src2 != mir.insns[46].dst ||
        mir.insns[48].phi_pred1 != mir.insns[42].label ||
        mir.insns[48].phi_pred2 != mir.insns[45].label ||
        mir.insns[50].label != mir.insns[51].label ||
        mir.insns[52].src1 != mir.insns[37].dst ||
        mir.insns[52].src2 != mir.insns[48].dst ||
        mir.insns[52].phi_pred1 != mir.insns[36].label ||
        mir.insns[52].phi_pred2 != mir.insns[49].label ||
        mir.insns[53].src1 != mir.insns[52].dst ||
        mir.insns[53].label != mir.insns[59].label ||
        mir.insns[58].label != mir.insns[63].label)
        return mir_machine_reject(
            "callback-registration-runner", "failure-branches");

    plan->print_function = find_global(mir.insns[57].name);
    if (plan->print_function == NULL ||
        plan->print_function->storage != SC_FUNC ||
        plan->print_function->is_defined ||
        plan->print_function->is_funcptr ||
        plan->print_function->is_noreturn ||
        !plan->print_function->has_proto ||
        !plan->print_function->proto_variadic ||
        plan->print_function->proto_nargs != 1 ||
        type_ptr_depth(plan->print_function->type) != 0 ||
        (plan->print_function->type & 15) != TYPE_INT ||
        (plan->print_function->type & TYPE_UNSIGNED) != 0 ||
        type_size(plan->print_function->type) != 2 ||
        type_ptr_depth(plan->print_function->proto_types[0]) != 1 ||
        (plan->print_function->proto_types[0] & 15) != TYPE_CHAR)
        return mir_machine_reject(
            "callback-registration-runner", "print-function");
    assembly_name =
        asm_name_for(sym_asm_name(plan->print_function));
    for (item = 0; item < 2; ++item) {
        const int string_instruction = item == 0 ? 55 : 60;
        const int argument_instruction = item == 0 ? 56 : 61;
        const int call_instruction = item == 0 ? 57 : 62;
        const struct MirInsn *string =
            &mir.insns[string_instruction];
        const struct MirInsn *argument =
            &mir.insns[argument_instruction];
        const struct MirInsn *call = &mir.insns[call_instruction];
        int call_argument;

        if (find_global(call->name) != plan->print_function ||
            call->src1 >= 0 ||
            call->memory_flags != MIR_CALL_FLAG_VARIADIC ||
            type_ptr_depth(call->type) != 0 ||
            (call->type & 15) != TYPE_INT ||
            (call->type & TYPE_UNSIGNED) != 0 ||
            type_size(call->type) != 2 ||
            (call->base_name[0] != 0 &&
             strcmp(call->base_name, assembly_name)) ||
            type_ptr_depth(string->type) != 1 ||
            (string->type & 15) != TYPE_CHAR ||
            argument->src1 != string->dst ||
            argument->immediate != 0 ||
            argument->secondary_offset != call->secondary_offset ||
            argument->type != string->type ||
            !mir_machine_single_call_argument(
                call, &call_argument) ||
            call_argument != string->dst)
            return mir_machine_reject(
                "callback-registration-runner", "print-call");
    }
    plan->failure_string_id = (int)mir.insns[55].immediate;
    plan->success_string_id = (int)mir.insns[60].immediate;
    if (plan->failure_string_id == plan->success_string_id ||
        !mir_machine_constant_equals(mir.insns[64].dst, 0) ||
        mir.insns[65].src1 != mir.insns[64].dst)
        return mir_machine_reject(
            "callback-registration-runner", "return");
    return 1;
}

static int mir_for_increment_word_function(
    const struct MirInsn *call, struct Sym **function_out,
    int argument_count)
{
    struct Sym *function;
    const char *assembly_name;

    if (call->opcode != MIR_CALL || call->src1 >= 0 ||
        call->memory_flags != 0 ||
        type_ptr_depth(call->type) != 0 ||
        (call->type & 15) != TYPE_INT ||
        (call->type & TYPE_UNSIGNED) != 0 ||
        type_size(call->type) != 2 ||
        (function = find_global(call->name)) == NULL ||
        function->storage != SC_FUNC ||
        !function->is_defined ||
        function->is_funcptr ||
        function->is_noreturn ||
        !function->has_proto ||
        function->proto_variadic ||
        function->proto_nargs != argument_count ||
        type_ptr_depth(function->type) != 0 ||
        (function->type & 15) != TYPE_INT ||
        (function->type & TYPE_UNSIGNED) != 0 ||
        type_size(function->type) != 2)
        return 0;
    assembly_name = asm_name_for(sym_asm_name(function));
    if (call->base_name[0] != 0 &&
        strcmp(call->base_name, assembly_name))
        return 0;
    *function_out = function;
    return 1;
}

static int mir_match_for_increment_runner(
    struct MirForIncrementRunner *plan)
{
    static const int expected_opcodes[123] = {
        MIR_LABEL, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STORE,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_STORE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_STORE, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STORE,
        MIR_CALL, MIR_STORE, MIR_CALL, MIR_STORE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_PHI, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_UNARY, MIR_RETURN
    };
    static const int calls[7] = {3, 7, 13, 19, 23, 25, 27};
    static const int stores[7] = {4, 8, 14, 20, 24, 26, 28};
    static const int condition_nops[7] = {46, 50, 62, 74, 86, 98, 110};
    static const int comparisons[7] = {48, 52, 64, 76, 88, 100, 112};
    static const int constants[7] = {47, 51, 63, 75, 87, 99, 111};
    static const int expected_values[7] = {10, 10, 4, 4, 14, 22, 14};
    static const int stage_phis[5] = {72, 84, 96, 108, 120};
    int call_arguments[8];
    const char *assembly_name;
    int call_count = 0;
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 123 || mir_cfg_block_count() != 19 ||
        mir.has_vla || mir.local_bytes != 14 ||
        mir.aggregate_temp_bytes != 0 ||
        type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        (mir.return_type & TYPE_UNSIGNED) != 0 ||
        type_size(mir.return_type) != 2)
        return mir_machine_reject(
            "for-increment-runner", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        if (mir.insns[instruction].opcode !=
                expected_opcodes[instruction])
            return mir_machine_reject(
                "for-increment-runner", "opcode");
        if (mir.insns[instruction].opcode == MIR_CALL)
            ++call_count;
    }
    if (call_count != 8)
        return mir_machine_reject(
            "for-increment-runner", "call-count");

    for (item = 0; item < 7; ++item) {
        const struct MirInsn *call = &mir.insns[calls[item]];
        const struct MirInsn *store = &mir.insns[stores[item]];
        int expected_argument_count =
            item < 2 || item == 4 ? 1 : (item < 4 ? 2 : 0);
        int previous;

        if (!mir_for_increment_word_function(
                call, &plan->helpers[item],
                expected_argument_count) ||
            !mir_machine_call_arguments(
                call, expected_argument_count, call_arguments) ||
            store->src1 != call->dst ||
            store->memory_size != 2 ||
            type_ptr_depth(store->type) != 0 ||
            (store->type & 15) != TYPE_INT ||
            (store->type & TYPE_UNSIGNED) != 0 ||
            type_size(store->type) != 2 ||
            !mir_machine_unobservable_local_store(store) ||
            !mir_machine_same_location(
                store, &mir.insns[condition_nops[item]]))
            return mir_machine_reject(
                "for-increment-runner", "helper-call");
        for (previous = 0; previous < item; ++previous) {
            if (plan->helpers[item] == plan->helpers[previous] ||
                mir_machine_same_location(
                    store, &mir.insns[stores[previous]]))
                return mir_machine_reject(
                    "for-increment-runner", "distinct-helper");
        }
        if (item < 2) {
            const int constant_instruction = item == 0 ? 1 : 5;
            const int argument_instruction = item == 0 ? 2 : 6;

            if (!mir_machine_constant_equals(
                    mir.insns[constant_instruction].dst, 4) ||
                call_arguments[0] !=
                    mir.insns[constant_instruction].dst ||
                mir.insns[argument_instruction].src1 !=
                    mir.insns[constant_instruction].dst ||
                mir.insns[argument_instruction].immediate != 0 ||
                mir.insns[argument_instruction].secondary_offset !=
                    call->secondary_offset ||
                mir.insns[argument_instruction].type !=
                    mir.insns[constant_instruction].type ||
                type_ptr_depth(
                    mir.insns[constant_instruction].type) != 0 ||
                (mir.insns[constant_instruction].type & 15) != TYPE_INT ||
                (mir.insns[constant_instruction].type &
                 TYPE_UNSIGNED) != 0 ||
                type_size(mir.insns[constant_instruction].type) != 2 ||
                type_ptr_depth(
                    plan->helpers[item]->proto_types[0]) != 0 ||
                (plan->helpers[item]->proto_types[0] & 15) != TYPE_INT ||
                (plan->helpers[item]->proto_types[0] &
                 TYPE_UNSIGNED) != 0 ||
                type_size(
                    plan->helpers[item]->proto_types[0]) != 2)
                return mir_machine_reject(
                    "for-increment-runner", "integer-helper");
        } else if (item < 4) {
            const int string_instruction = item == 2 ? 9 : 15;
            const int string_argument = item == 2 ? 10 : 16;
            const int constant_instruction = item == 2 ? 11 : 17;
            const int constant_argument = item == 2 ? 12 : 18;
            const struct MirInsn *string =
                &mir.insns[string_instruction];

            if (type_ptr_depth(string->type) != 1 ||
                (string->type & 15) != TYPE_CHAR ||
                string->immediate != mir.insns[9].immediate ||
                call_arguments[0] != string->dst ||
                call_arguments[1] !=
                    mir.insns[constant_instruction].dst ||
                mir.insns[string_argument].src1 != string->dst ||
                mir.insns[string_argument].immediate != 0 ||
                mir.insns[string_argument].secondary_offset !=
                    call->secondary_offset ||
                mir.insns[string_argument].type != string->type ||
                !mir_machine_constant_equals(
                    mir.insns[constant_instruction].dst, 5) ||
                mir.insns[constant_argument].src1 !=
                    mir.insns[constant_instruction].dst ||
                mir.insns[constant_argument].immediate != 1 ||
                mir.insns[constant_argument].secondary_offset !=
                    call->secondary_offset ||
                mir.insns[constant_argument].type !=
                    mir.insns[constant_instruction].type ||
                type_ptr_depth(
                    mir.insns[constant_instruction].type) != 0 ||
                (mir.insns[constant_instruction].type & 15) != TYPE_INT ||
                (mir.insns[constant_instruction].type &
                 TYPE_UNSIGNED) != 0 ||
                type_size(mir.insns[constant_instruction].type) != 2 ||
                plan->helpers[item]->proto_types[0] != string->type ||
                type_ptr_depth(
                    plan->helpers[item]->proto_types[0]) != 1 ||
                (plan->helpers[item]->proto_types[0] & 15) != TYPE_CHAR ||
                type_ptr_depth(
                    plan->helpers[item]->proto_types[1]) != 0 ||
                (plan->helpers[item]->proto_types[1] & 15) != TYPE_INT ||
                (plan->helpers[item]->proto_types[1] &
                 TYPE_UNSIGNED) != 0 ||
                type_size(
                    plan->helpers[item]->proto_types[1]) != 2)
                return mir_machine_reject(
                    "for-increment-runner", "pointer-helper");
        } else if (item == 4) {
            if (!mir_machine_constant_equals(mir.insns[21].dst, 5) ||
                call_arguments[0] != mir.insns[21].dst ||
                mir.insns[22].src1 != mir.insns[21].dst ||
                mir.insns[22].immediate != 0 ||
                mir.insns[22].secondary_offset !=
                    call->secondary_offset ||
                mir.insns[22].type != mir.insns[21].type ||
                type_ptr_depth(mir.insns[21].type) != 0 ||
                (mir.insns[21].type & 15) != TYPE_INT ||
                (mir.insns[21].type & TYPE_UNSIGNED) != 0 ||
                type_size(mir.insns[21].type) != 2 ||
                type_ptr_depth(
                    plan->helpers[item]->proto_types[0]) != 0 ||
                (plan->helpers[item]->proto_types[0] & 15) != TYPE_INT ||
                (plan->helpers[item]->proto_types[0] &
                 TYPE_UNSIGNED) != 0 ||
                type_size(
                    plan->helpers[item]->proto_types[0]) != 2)
                return mir_machine_reject(
                    "for-increment-runner", "bounded-helper");
        }
    }
    plan->input_string_id = (int)mir.insns[9].immediate;
    plan->format_string_id = (int)mir.insns[29].immediate;
    if (plan->input_string_id == plan->format_string_id)
        return mir_machine_reject(
            "for-increment-runner", "strings");

    plan->print_function = find_global(mir.insns[45].name);
    if (plan->print_function == NULL ||
        plan->print_function->storage != SC_FUNC ||
        plan->print_function->is_defined ||
        plan->print_function->is_funcptr ||
        plan->print_function->is_noreturn ||
        !plan->print_function->has_proto ||
        !plan->print_function->proto_variadic ||
        plan->print_function->proto_nargs != 1 ||
        type_ptr_depth(plan->print_function->type) != 0 ||
        (plan->print_function->type & 15) != TYPE_INT ||
        (plan->print_function->type & TYPE_UNSIGNED) != 0 ||
        type_size(plan->print_function->type) != 2 ||
        type_ptr_depth(plan->print_function->proto_types[0]) != 1 ||
        (plan->print_function->proto_types[0] & 15) != TYPE_CHAR ||
        mir.insns[45].src1 >= 0 ||
        mir.insns[45].memory_flags != MIR_CALL_FLAG_VARIADIC ||
        type_ptr_depth(mir.insns[45].type) != 0 ||
        (mir.insns[45].type & 15) != TYPE_INT ||
        (mir.insns[45].type & TYPE_UNSIGNED) != 0 ||
        type_size(mir.insns[45].type) != 2 ||
        !mir_machine_call_arguments(
            &mir.insns[45], 8, call_arguments))
        return mir_machine_reject(
            "for-increment-runner", "print-function");
    assembly_name =
        asm_name_for(sym_asm_name(plan->print_function));
    if ((mir.insns[45].base_name[0] != 0 &&
         strcmp(mir.insns[45].base_name, assembly_name)) ||
        type_ptr_depth(mir.insns[29].type) != 1 ||
        (mir.insns[29].type & 15) != TYPE_CHAR ||
        call_arguments[0] != mir.insns[29].dst ||
        mir.insns[30].src1 != mir.insns[29].dst ||
        mir.insns[30].immediate != 0 ||
        mir.insns[30].secondary_offset !=
            mir.insns[45].secondary_offset ||
        mir.insns[30].type != mir.insns[29].type)
        return mir_machine_reject(
            "for-increment-runner", "print-format");
    for (item = 0; item < 7; ++item) {
        const int nop_instruction = 31 + item * 2;
        const int argument_instruction = nop_instruction + 1;

        if (!mir_machine_same_location(
                &mir.insns[stores[item]],
                &mir.insns[nop_instruction]) ||
            call_arguments[item + 1] !=
                mir.insns[calls[item]].dst ||
            mir.insns[argument_instruction].src1 !=
                mir.insns[calls[item]].dst ||
            mir.insns[argument_instruction].immediate != item + 1 ||
            mir.insns[argument_instruction].secondary_offset !=
                mir.insns[45].secondary_offset ||
            mir.insns[argument_instruction].type !=
                mir.insns[calls[item]].type)
            return mir_machine_reject(
                "for-increment-runner", "print-arguments");
    }

    for (item = 0; item < 7; ++item) {
        const struct MirInsn *comparison =
            &mir.insns[comparisons[item]];

        plan->expected[item] = expected_values[item];
        if (!mir_machine_same_location(
                &mir.insns[stores[item]],
                &mir.insns[condition_nops[item]]) ||
            !mir_machine_constant_equals(
                mir.insns[constants[item]].dst,
                expected_values[item]) ||
            comparison->immediate != TOK_EQ ||
            comparison->src1 != mir.insns[calls[item]].dst ||
            comparison->src2 != mir.insns[constants[item]].dst ||
            type_ptr_depth(comparison->type) != 0 ||
            (comparison->type & 15) != TYPE_INT ||
            (comparison->type & TYPE_UNSIGNED) != 0 ||
            type_size(comparison->type) != 2)
            return mir_machine_reject(
                "for-increment-runner", "checks");
    }
    if (mir.insns[49].src1 != mir.insns[48].dst ||
        mir.insns[49].label != mir.insns[57].label ||
        mir.insns[53].src1 != mir.insns[52].dst ||
        mir.insns[53].label != mir.insns[57].label ||
        !mir_machine_constant_equals(mir.insns[55].dst, 1) ||
        mir.insns[56].label != mir.insns[59].label ||
        !mir_machine_constant_equals(mir.insns[58].dst, 0) ||
        mir.insns[60].src1 != mir.insns[55].dst ||
        mir.insns[60].src2 != mir.insns[58].dst ||
        mir.insns[60].phi_pred1 != mir.insns[54].label ||
        mir.insns[60].phi_pred2 != mir.insns[57].label ||
        mir.insns[61].src1 != mir.insns[60].dst ||
        mir.insns[61].label != mir.insns[69].label)
        return mir_machine_reject(
            "for-increment-runner", "first-checks");
    for (item = 0; item < 5; ++item) {
        const int comparison = 64 + item * 12;
        const int success_label = comparison + 2;
        const int one = comparison + 3;
        const int jump = comparison + 4;
        const int failure_label = comparison + 5;
        const int zero = comparison + 6;
        const int join_label = comparison + 7;
        const int phi = comparison + 8;
        const int prior_branch = comparison - 3;

        if (mir.insns[prior_branch].label !=
                mir.insns[failure_label].label ||
            mir.insns[comparison + 1].src1 !=
                mir.insns[comparison].dst ||
            mir.insns[comparison + 1].label !=
                mir.insns[failure_label].label ||
            !mir_machine_constant_equals(
                mir.insns[one].dst, 1) ||
            mir.insns[jump].label !=
                mir.insns[join_label].label ||
            !mir_machine_constant_equals(
                mir.insns[zero].dst, 0) ||
            mir.insns[phi].src1 != mir.insns[one].dst ||
            mir.insns[phi].src2 != mir.insns[zero].dst ||
            mir.insns[phi].phi_pred1 !=
                mir.insns[success_label].label ||
            mir.insns[phi].phi_pred2 !=
                mir.insns[failure_label].label ||
            phi != stage_phis[item])
            return mir_machine_reject(
                "for-increment-runner", "check-cfg");
        if (item < 4 &&
            (mir.insns[phi + 1].src1 != mir.insns[phi].dst ||
             mir.insns[phi + 1].label !=
                 mir.insns[failure_label + 12].label))
            return mir_machine_reject(
                "for-increment-runner", "check-chain");
    }
    if (mir.insns[121].immediate != '!' ||
        mir.insns[121].src1 != mir.insns[120].dst ||
        type_ptr_depth(mir.insns[121].type) != 0 ||
        (mir.insns[121].type & 15) != TYPE_INT ||
        (mir.insns[121].type & TYPE_UNSIGNED) != 0 ||
        type_size(mir.insns[121].type) != 2 ||
        mir.insns[122].src1 != mir.insns[121].dst)
        return mir_machine_reject(
            "for-increment-runner", "return");
    return 1;
}

static int mir_match_long_index_call_runner(
    struct MirLongIndexCallRunner *plan)
{
    static const int expected_opcodes[148] = {
        MIR_LABEL, MIR_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_ARG, MIR_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_UNARY, MIR_ARG, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_ADDRESS, MIR_ARG, MIR_ADDRESS,
        MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_ARG,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_CONST, MIR_STORE,
        MIR_LABEL, MIR_PHI, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP, MIR_UNARY,
        MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL,
        MIR_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_UNARY, MIR_ARG, MIR_NOP,
        MIR_CONST, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_UNARY, MIR_ARG, MIR_NOP,
        MIR_CONST, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_LOAD, MIR_UNARY, MIR_ARG,
        MIR_NOP, MIR_CONST, MIR_ARG, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_LOAD, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS,
        MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_ARG,
        MIR_CALL, MIR_LOAD, MIR_BRANCH_FALSE, MIR_CONST,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_CONST,
        MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_RETURN
    };
    int arguments[3];
    int call_argument;
    int instruction;
    int fast_arg0;
    int fast_arg1;
    const char *runtime_name = NULL;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 148 || mir_cfg_block_count() != 12 ||
        mir.has_vla || mir.local_bytes != 1 ||
        (mir.return_type & 15) != TYPE_INT)
        return mir_machine_reject(
            "long-index-call-runner", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
                expected_opcodes[instruction])
            return mir_machine_reject(
                "long-index-call-runner", "opcode");

    plan->source_buffer = mir_long_index_global_address(1);
    plan->output_buffer = mir_long_index_global_address(21);
    plan->inline_values = mir_long_index_global_address(63);
    if (plan->source_buffer == NULL ||
        plan->output_buffer == NULL ||
        plan->inline_values == NULL ||
        plan->source_buffer == plan->output_buffer ||
        plan->source_buffer == plan->inline_values ||
        plan->output_buffer == plan->inline_values)
        return mir_machine_reject(
            "long-index-call-runner", "global-address");
    {
        static const int source_addresses[] = {
            6, 10, 18, 23, 28, 33, 43
        };
        static const int output_addresses[] = {46};
        static const int inline_addresses[] = {76, 92};
        size_t index;

        for (index = 0;
             index < sizeof(source_addresses) /
                         sizeof(source_addresses[0]);
             ++index)
            if (mir_long_index_global_address(
                    source_addresses[index]) !=
                    plan->source_buffer)
                return mir_machine_reject(
                    "long-index-call-runner", "source-root");
        for (index = 0;
             index < sizeof(output_addresses) /
                         sizeof(output_addresses[0]);
             ++index)
            if (mir_long_index_global_address(
                    output_addresses[index]) !=
                    plan->output_buffer)
                return mir_machine_reject(
                    "long-index-call-runner", "output-root");
        for (index = 0;
             index < sizeof(inline_addresses) /
                         sizeof(inline_addresses[0]);
             ++index)
            if (mir_long_index_global_address(
                    inline_addresses[index]) !=
                    plan->inline_values)
                return mir_machine_reject(
                    "long-index-call-runner", "inline-root");
    }

    plan->copy_string_function =
        mir_long_index_call_function(5);
    plan->count_function =
        mir_long_index_call_function(8);
    plan->length_function =
        mir_long_index_call_function(12);
    plan->long_check_function =
        mir_long_index_call_function(17);
    plan->copy_function =
        mir_long_index_call_function(20);
    plan->string_check_function =
        mir_long_index_call_function(27);
    plan->safe_sum_function =
        mir_long_index_call_function(80);
    plan->unsafe_sum_function =
        mir_long_index_call_function(96);
    plan->print_function = find_global(mir.insns[120].name);
    if (plan->copy_string_function == NULL ||
        plan->count_function == NULL ||
        plan->length_function == NULL ||
        plan->long_check_function == NULL ||
        plan->copy_function == NULL ||
        plan->string_check_function == NULL ||
        plan->safe_sum_function == NULL ||
        plan->unsafe_sum_function == NULL ||
        plan->print_function == NULL ||
        mir_long_index_call_function(32) !=
            plan->copy_string_function ||
        mir_long_index_call_function(35) !=
            plan->count_function ||
        mir_long_index_call_function(42) !=
            plan->long_check_function ||
        mir_long_index_call_function(45) !=
            plan->copy_function ||
        mir_long_index_call_function(52) !=
            plan->string_check_function ||
        mir_long_index_call_function(88) !=
            plan->long_check_function ||
        mir_long_index_call_function(104) !=
            plan->long_check_function ||
        mir_long_index_call_function(113) !=
            plan->long_check_function ||
        mir.insns[136].opcode != MIR_CALL ||
        find_global(mir.insns[136].name) !=
            plan->print_function ||
        plan->safe_sum_function == plan->unsafe_sum_function)
        return mir_machine_reject(
            "long-index-call-runner", "call-family");

    if (!mir_machine_two_call_arguments(
            &mir.insns[5], arguments) ||
        arguments[0] != mir.insns[1].dst ||
        arguments[1] != mir.insns[3].dst ||
        !mir_machine_single_call_argument(
            &mir.insns[8], &call_argument) ||
        call_argument != mir.insns[6].dst ||
        !mir_machine_single_call_argument(
            &mir.insns[12], &call_argument) ||
        call_argument != mir.insns[10].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[17], arguments) ||
        arguments[0] != mir.insns[8].dst ||
        arguments[1] != mir.insns[13].dst ||
        arguments[2] != mir.insns[15].dst ||
        mir.insns[13].src1 != mir.insns[12].dst ||
        type_size(mir.insns[8].type) != 4 ||
        type_size(mir.insns[12].type) != 2 ||
        type_size(mir.insns[13].type) != 4)
        return mir_machine_reject(
            "long-index-call-runner", "first-count");
    if (!mir_machine_single_call_argument(
            &mir.insns[20], &call_argument) ||
        call_argument != mir.insns[18].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[27], arguments) ||
        arguments[0] != mir.insns[21].dst ||
        arguments[1] != mir.insns[23].dst ||
        arguments[2] != mir.insns[25].dst)
        return mir_machine_reject(
            "long-index-call-runner", "first-copy");
    if (!mir_machine_two_call_arguments(
            &mir.insns[32], arguments) ||
        arguments[0] != mir.insns[28].dst ||
        arguments[1] != mir.insns[30].dst ||
        !mir_machine_single_call_argument(
            &mir.insns[35], &call_argument) ||
        call_argument != mir.insns[33].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[42], arguments) ||
        arguments[0] != mir.insns[35].dst ||
        arguments[1] != mir.insns[38].dst ||
        arguments[2] != mir.insns[40].dst)
        return mir_machine_reject(
            "long-index-call-runner", "second-count");
    if (!mir_machine_single_call_argument(
            &mir.insns[45], &call_argument) ||
        call_argument != mir.insns[43].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[52], arguments) ||
        arguments[0] != mir.insns[46].dst ||
        arguments[1] != mir.insns[48].dst ||
        arguments[2] != mir.insns[50].dst ||
        mir.insns[48].immediate != mir.insns[30].immediate)
        return mir_machine_reject(
            "long-index-call-runner", "second-copy");

    if (!mir_machine_constant_equals(mir.insns[54].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[55]) ||
        mir.insns[57].src1 != mir.insns[54].dst ||
        mir.insns[57].src2 != mir.insns[72].dst ||
        mir.insns[57].phi_pred1 != mir.insns[0].label ||
        mir.insns[57].phi_pred2 != mir.insns[69].label ||
        mir.insns[61].opcode != MIR_BINARY ||
        mir.insns[61].immediate != '<' ||
        mir.insns[61].src1 != mir.insns[60].dst ||
        mir.insns[61].src2 != mir.insns[59].dst ||
        mir.insns[62].label != mir.insns[75].label ||
        mir.insns[65].src1 != mir.insns[63].dst ||
        mir.insns[65].src2 != mir.insns[57].dst ||
        mir.insns[65].immediate != 2 ||
        mir.insns[68].src1 != mir.insns[65].dst ||
        mir.insns[68].src2 != mir.insns[67].dst ||
        mir.insns[68].memory_size != 2 ||
        mir.insns[72].immediate != '+' ||
        !mir_machine_constant_equals(mir.insns[71].dst, 1) ||
        !mir_machine_unobservable_local_store(&mir.insns[73]) ||
        mir.insns[74].label != mir.insns[56].label ||
        !mir_long_index_constant(59, &plan->inline_bound) ||
        plan->inline_bound <= 0 ||
        plan->inline_bound > 127)
        return mir_machine_reject(
            "long-index-call-runner", "inline-loop");

    if (!mir_machine_two_call_arguments(
            &mir.insns[80], arguments) ||
        arguments[0] != mir.insns[76].dst ||
        arguments[1] != mir.insns[78].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[88], arguments) ||
        arguments[0] != mir.insns[81].dst ||
        arguments[1] != mir.insns[84].dst ||
        arguments[2] != mir.insns[86].dst ||
        mir.insns[81].src1 != mir.insns[80].dst ||
        !mir_machine_two_call_arguments(
            &mir.insns[96], arguments) ||
        arguments[0] != mir.insns[92].dst ||
        arguments[1] != mir.insns[94].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[104], arguments) ||
        arguments[0] != mir.insns[97].dst ||
        arguments[1] != mir.insns[100].dst ||
        arguments[2] != mir.insns[102].dst ||
        mir.insns[97].src1 != mir.insns[96].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[113], arguments) ||
        arguments[0] != mir.insns[106].dst ||
        arguments[1] != mir.insns[109].dst ||
        arguments[2] != mir.insns[111].dst ||
        mir.insns[106].src1 != mir.insns[105].dst)
        return mir_machine_reject(
            "long-index-call-runner", "inline-calls");

    plan->unsafe_call_count =
        find_global(mir.insns[91].name);
    if (plan->unsafe_call_count == NULL ||
        plan->unsafe_call_count->is_volatile ||
        mir.insns[91].src1 != mir.insns[89].dst ||
        mir.insns[91].memory_size != 2 ||
        !mir_machine_constant_equals(mir.insns[89].dst, 0) ||
        find_global(mir.insns[105].name) !=
            plan->unsafe_call_count ||
        type_size(mir.insns[105].type) != 2)
        return mir_machine_reject(
            "long-index-call-runner", "unsafe-count");

    plan->checks = find_global(mir.insns[116].name);
    plan->failures = find_global(mir.insns[118].name);
    if (plan->checks == NULL || plan->failures == NULL ||
        plan->checks == plan->failures ||
        plan->checks->is_volatile || plan->failures->is_volatile ||
        !mir_machine_three_call_arguments(
            &mir.insns[120], arguments) ||
        arguments[0] != mir.insns[114].dst ||
        arguments[1] != mir.insns[116].dst ||
        arguments[2] != mir.insns[118].dst ||
        find_global(mir.insns[123].name) != plan->failures ||
        mir.insns[125].opcode != MIR_BINARY ||
        mir.insns[125].immediate != TOK_EQ ||
        mir.insns[125].src1 != mir.insns[123].dst ||
        !mir_machine_constant_equals(mir.insns[124].dst, 0) ||
        mir.insns[126].label != mir.insns[130].label ||
        mir.insns[129].label != mir.insns[133].label ||
        mir.insns[134].src1 != mir.insns[127].dst ||
        mir.insns[134].src2 != mir.insns[131].dst ||
        mir.insns[134].phi_pred1 != mir.insns[128].label ||
        mir.insns[134].phi_pred2 != mir.insns[132].label ||
        !mir_machine_two_call_arguments(
            &mir.insns[136], arguments) ||
        arguments[0] != mir.insns[121].dst ||
        arguments[1] != mir.insns[134].dst ||
        find_global(mir.insns[137].name) != plan->failures ||
        mir.insns[138].label != mir.insns[142].label ||
        !mir_machine_constant_equals(mir.insns[139].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[143].dst, 0) ||
        mir.insns[146].src1 != mir.insns[139].dst ||
        mir.insns[146].src2 != mir.insns[143].dst ||
        mir.insns[147].src1 != mir.insns[146].dst)
        return mir_machine_reject(
            "long-index-call-runner", "final");

    if (!mir_long_index_constant(
            38, &plan->second_count_expected) ||
        !mir_long_index_constant(78, &plan->safe_count) ||
        !mir_long_index_constant(84, &plan->safe_expected) ||
        !mir_long_index_constant(94, &plan->unsafe_count) ||
        !mir_long_index_constant(100, &plan->unsafe_expected) ||
        !mir_long_index_constant(
            109, &plan->unsafe_calls_expected))
        return mir_machine_reject(
            "long-index-call-runner", "constants");

    plan->first_source_string_id =
        (int)mir.insns[3].immediate;
    plan->first_count_string_id =
        (int)mir.insns[15].immediate;
    plan->first_copy_string_id =
        (int)mir.insns[25].immediate;
    plan->second_source_string_id =
        (int)mir.insns[30].immediate;
    plan->second_count_string_id =
        (int)mir.insns[40].immediate;
    plan->second_copy_string_id =
        (int)mir.insns[50].immediate;
    plan->safe_sum_string_id =
        (int)mir.insns[86].immediate;
    plan->unsafe_sum_string_id =
        (int)mir.insns[102].immediate;
    plan->unsafe_count_string_id =
        (int)mir.insns[111].immediate;
    plan->summary_string_id =
        (int)mir.insns[114].immediate;
    plan->result_string_id =
        (int)mir.insns[121].immediate;
    plan->zero_result_string_id =
        (int)mir.insns[127].immediate;
    plan->nonzero_result_string_id =
        (int)mir.insns[131].immediate;

    plan->copy_fastcall = mir_call_is_de_hl_fastcall(
        5, &runtime_name, &fast_arg0, &fast_arg1);
    if (!plan->copy_fastcall ||
        fast_arg0 != mir.insns[1].dst ||
        fast_arg1 != mir.insns[3].dst ||
        runtime_name == NULL ||
        strlen(runtime_name) >=
            sizeof(plan->copy_runtime_name))
        return mir_machine_reject(
            "long-index-call-runner", "copy-fastcall");
    strcpy(plan->copy_runtime_name, runtime_name);
    plan->length_fastcall = mir_call_is_strlen_fastcall(
        12, &call_argument);
    if (!plan->length_fastcall ||
        call_argument != mir.insns[10].dst)
        return mir_machine_reject(
            "long-index-call-runner", "length-fastcall");
    return 1;
}

static int mir_match_fixed_call_check_runner(
    struct MirFixedCallCheckRunner *plan)
{
    static const int function_calls[5] = {
        32, 49, 66, 85, 108
    };
    static const int expected_constants[5] = {
        33, 50, 67, 89, 109
    };
    static const int failure_strings[5] = {
        36, 53, 70, 92, 112
    };
    static const int failure_prints[5] = {
        38, 55, 72, 94, 114
    };
    static const int comparisons[5] = {
        34, 51, 68, 90, 110
    };
    static const int branches[5] = {
        35, 52, 69, 91, 111
    };
    static const int array_addresses[5] = {
        4, 10, 16, 22, 62
    };
    static const int array_indices[4] = {
        5, 11, 17, 23
    };
    static const int array_values[4] = {
        8, 14, 20, 26
    };
    static const int array_stores[4] = {
        9, 15, 21, 27
    };
    int arguments[4];
    long value;
    int call;
    int index;
    int call_count = 0;
    const char *array_name;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 137 || mir_cfg_block_count() != 7 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_INT ||
        !mir_machine_constant_equals(mir.insns[1].dst, 0) ||
        mir.insns[3].opcode != MIR_STORE ||
        mir.insns[4].opcode != MIR_ADDRESS)
        return mir_machine_reject(
            "fixed-call-check-runner", "shape");
    array_name = mir.insns[4].name;
    for (index = 0; index < 4; ++index) {
        if (mir.insns[array_addresses[index]].opcode !=
                MIR_ADDRESS ||
            strcmp(mir.insns[array_addresses[index]].name,
                   array_name) != 0 ||
            !mir_machine_constant_equals(
                mir.insns[array_indices[index]].dst, index) ||
            mir.insns[array_stores[index]].opcode !=
                MIR_STORE_INDIRECT ||
            mir.insns[array_stores[index]].src1 !=
                mir.insns[array_stores[index] - 3].dst ||
            mir.insns[array_stores[index]].memory_size != 1 ||
            !mir_machine_constant_value(
                mir.insns[array_values[index]].dst,
                &value, 0))
            return mir_machine_reject(
                "fixed-call-check-runner", "array");
        plan->array_values[index] = (int)value;
    }
    if (mir.insns[array_addresses[4]].opcode != MIR_ADDRESS ||
        strcmp(mir.insns[array_addresses[4]].name,
               array_name) != 0)
        return mir_machine_reject(
            "fixed-call-check-runner", "array-argument");
    for (call = 0; call < 5; ++call) {
        struct Sym *function;
        struct Sym *print_function;

        if (mir.insns[function_calls[call]].opcode != MIR_CALL ||
            (function =
                 find_global(
                     mir.insns[function_calls[call]].name)) ==
                NULL ||
            mir.insns[comparisons[call]].opcode != MIR_BINARY ||
            mir.insns[comparisons[call]].immediate != TOK_NE ||
            mir.insns[comparisons[call]].src1 !=
                mir.insns[function_calls[call]].dst ||
            mir.insns[comparisons[call]].src2 !=
                mir.insns[expected_constants[call]].dst ||
            mir.insns[branches[call]].opcode !=
                MIR_BRANCH_FALSE ||
            mir.insns[failure_strings[call]].opcode !=
                MIR_STRING_ADDRESS ||
            mir.insns[failure_prints[call]].opcode != MIR_CALL ||
            (print_function =
                 find_global(
                     mir.insns[failure_prints[call]].name)) ==
                NULL)
            return mir_machine_reject(
                "fixed-call-check-runner", "check");
        if (call == 0)
            plan->print_function = print_function;
        else if (print_function != plan->print_function)
            return mir_machine_reject(
                "fixed-call-check-runner", "print");
        plan->functions[call] = function;
        plan->failure_string_ids[call] =
            (int)mir.insns[failure_strings[call]].immediate;
        if (call == 3) {
            if (!mir_machine_constant_value(
                    mir.insns[expected_constants[call]].dst,
                    &value, 0))
                return mir_machine_reject(
                    "fixed-call-check-runner", "wide-result");
            plan->expected_long =
                (unsigned long)value;
        } else {
            if (!mir_machine_constant_value(
                    mir.insns[expected_constants[call]].dst,
                    &value, 0))
                return mir_machine_reject(
                    "fixed-call-check-runner", "result");
            plan->expected_words[
                call < 3 ? call : 3] = (int)value;
        }
    }
    if (!mir_machine_two_call_arguments(
            &mir.insns[32], arguments) ||
        !mir_machine_constant_value(arguments[0], &value, 0))
        return mir_machine_reject(
            "fixed-call-check-runner", "first-args");
    plan->binary_arguments[0][0] = (int)value;
    if (!mir_machine_constant_value(arguments[1], &value, 0))
        return mir_machine_reject(
            "fixed-call-check-runner", "first-arg2");
    plan->binary_arguments[0][1] = (int)value;
    if (!mir_machine_two_call_arguments(
            &mir.insns[49], arguments) ||
        !mir_machine_constant_value(arguments[0], &value, 0))
        return mir_machine_reject(
            "fixed-call-check-runner", "second-args");
    plan->binary_arguments[1][0] = (int)value;
    if (!mir_machine_constant_value(arguments[1], &value, 0))
        return mir_machine_reject(
            "fixed-call-check-runner", "second-arg2");
    plan->binary_arguments[1][1] = (int)value;
    if (!mir_machine_two_call_arguments(
            &mir.insns[66], arguments) ||
        arguments[0] != mir.insns[62].dst ||
        !mir_machine_constant_value(arguments[1], &value, 0))
        return mir_machine_reject(
            "fixed-call-check-runner", "pointer-args");
    plan->pointer_count = (int)value;
    if (!mir_machine_three_call_arguments(
            &mir.insns[85], arguments) ||
        !mir_machine_constant_value(arguments[0], &value, 0))
        return mir_machine_reject(
            "fixed-call-check-runner", "wide-args");
    plan->long_arguments[0] = (unsigned long)value;
    if (!mir_machine_constant_value(arguments[1], &value, 0))
        return mir_machine_reject(
            "fixed-call-check-runner", "wide-middle");
    plan->long_middle_argument = (int)value;
    if (!mir_machine_constant_value(arguments[2], &value, 0))
        return mir_machine_reject(
            "fixed-call-check-runner", "wide-last");
    plan->long_arguments[1] = (unsigned long)value;
    if (!mir_machine_three_call_arguments(
            &mir.insns[108], arguments))
        return mir_machine_reject(
            "fixed-call-check-runner", "byte-args");
    for (index = 0; index < 3; ++index) {
        if (!mir_machine_constant_value(
                arguments[index], &value, 0))
            return mir_machine_reject(
                "fixed-call-check-runner", "byte-arg");
        plan->byte_arguments[index] = (int)value;
    }
    for (index = 0; index < mir.count; ++index)
        if (mir.insns[index].opcode == MIR_CALL)
            ++call_count;
    if (call_count != 12 ||
        mir.insns[127].opcode != MIR_CALL ||
        find_global(mir.insns[127].name) !=
            plan->print_function ||
        mir.insns[134].opcode != MIR_CALL ||
        find_global(mir.insns[134].name) !=
            plan->print_function ||
        mir.insns[123].opcode != MIR_STRING_ADDRESS ||
        mir.insns[132].opcode != MIR_STRING_ADDRESS ||
        mir.insns[129].opcode != MIR_RETURN ||
        !mir_machine_constant_equals(mir.insns[128].dst, 1) ||
        mir.insns[136].opcode != MIR_RETURN ||
        !mir_machine_constant_equals(mir.insns[135].dst, 0))
        return mir_machine_reject(
            "fixed-call-check-runner", "final");
    plan->summary_string_id =
        (int)mir.insns[123].immediate;
    plan->success_string_id =
        (int)mir.insns[132].immediate;
    return 1;
}

static int mir_match_fixed_index_call_runner(
    struct MirFixedIndexCallRunner *plan)
{
    static const int int_addresses[4] = {4, 9, 14, 19};
    static const int int_indices[4] = {5, 10, 15, 20};
    static const int int_values[4] = {7, 12, 17, 22};
    static const int int_stores[4] = {8, 13, 18, 23};
    static const int long_addresses[4] = {24, 29, 34, 39};
    static const int long_indices[4] = {25, 30, 35, 40};
    static const int long_values[4] = {27, 32, 37, 42};
    static const int long_stores[4] = {28, 33, 38, 43};
    static const int pair_addresses[6] = {44, 50, 56, 62, 68, 74};
    static const int pair_indices[6] = {45, 51, 57, 63, 69, 75};
    static const int pair_members[6] = {47, 53, 59, 65, 71, 77};
    static const int pair_values[6] = {48, 54, 60, 66, 72, 78};
    static const int pair_stores[6] = {49, 55, 61, 67, 73, 79};
    static const int calls[5] = {82, 93, 106, 119, 132};
    static const int expecteds[5] = {83, 94, 107, 120, 133};
    static const int comparisons[5] = {84, 95, 108, 121, 134};
    static const int branches[5] = {85, 96, 109, 122, 135};
    static const int call_addresses[3] = {102, 115, 128};
    int arguments[2];
    int argument;
    int call_count = 0;
    long value;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 157 || mir_cfg_block_count() != 7 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_INT ||
        !mir_machine_constant_equals(mir.insns[1].dst, 0) ||
        mir.insns[3].opcode != MIR_STORE)
        return mir_machine_reject(
            "fixed-index-call-runner", "shape");
    for (argument = 0; argument < 4; ++argument) {
        if (mir.insns[int_addresses[argument]].opcode != MIR_ADDRESS ||
            (argument != 0 &&
                strcmp(mir.insns[int_addresses[argument]].name,
                       mir.insns[int_addresses[0]].name) != 0) ||
            !mir_machine_constant_equals(
                mir.insns[int_indices[argument]].dst, argument) ||
            mir.insns[int_stores[argument]].opcode !=
                MIR_STORE_INDIRECT ||
            mir.insns[int_stores[argument]].src1 !=
                mir.insns[int_stores[argument] - 2].dst ||
            mir.insns[int_stores[argument]].src2 !=
                mir.insns[int_values[argument]].dst ||
            mir.insns[int_stores[argument]].memory_size != 2 ||
            !mir_machine_constant_value(
                mir.insns[int_values[argument]].dst, &value, 0))
            return mir_machine_reject(
                "fixed-index-call-runner", "int-array");
        plan->int_values[argument] = (int)value;
        if (mir.insns[long_addresses[argument]].opcode != MIR_ADDRESS ||
            (argument != 0 &&
                strcmp(mir.insns[long_addresses[argument]].name,
                       mir.insns[long_addresses[0]].name) != 0) ||
            !mir_machine_constant_equals(
                mir.insns[long_indices[argument]].dst, argument) ||
            mir.insns[long_stores[argument]].opcode !=
                MIR_STORE_INDIRECT ||
            mir.insns[long_stores[argument]].src1 !=
                mir.insns[long_stores[argument] - 2].dst ||
            mir.insns[long_stores[argument]].src2 !=
                mir.insns[long_values[argument]].dst ||
            mir.insns[long_stores[argument]].memory_size != 4 ||
            !mir_machine_constant_value(
                mir.insns[long_values[argument]].dst, &value, 0))
            return mir_machine_reject(
                "fixed-index-call-runner", "long-array");
        plan->long_values[argument] = (unsigned long)value;
    }
    for (argument = 0; argument < 6; ++argument) {
        int expected_member_offset = (argument & 1) ? 4 : 0;

        if (mir.insns[pair_addresses[argument]].opcode != MIR_ADDRESS ||
            (argument != 0 &&
                strcmp(mir.insns[pair_addresses[argument]].name,
                       mir.insns[pair_addresses[0]].name) != 0) ||
            !mir_machine_constant_equals(
                mir.insns[pair_indices[argument]].dst,
                argument / 2) ||
            mir.insns[pair_members[argument]].opcode !=
                MIR_MEMBER_ADDRESS ||
            mir.insns[pair_members[argument]].immediate !=
                expected_member_offset ||
            mir.insns[pair_stores[argument]].opcode !=
                MIR_STORE_INDIRECT ||
            mir.insns[pair_stores[argument]].src1 !=
                mir.insns[pair_members[argument]].dst ||
            mir.insns[pair_stores[argument]].src2 !=
                mir.insns[pair_values[argument]].dst ||
            mir.insns[pair_stores[argument]].memory_size != 4 ||
            !mir_machine_constant_value(
                mir.insns[pair_values[argument]].dst, &value, 0))
            return mir_machine_reject(
                "fixed-index-call-runner", "pair-array");
        plan->pair_values[argument] = (unsigned long)value;
    }
    for (argument = 0; argument < 5; ++argument) {
        struct Sym *function;

        if (mir.insns[calls[argument]].opcode != MIR_CALL ||
            (function =
                 find_global(mir.insns[calls[argument]].name)) == NULL ||
            mir.insns[comparisons[argument]].opcode != MIR_BINARY ||
            mir.insns[comparisons[argument]].immediate != TOK_NE ||
            mir.insns[comparisons[argument]].src1 !=
                mir.insns[calls[argument]].dst ||
            mir.insns[comparisons[argument]].src2 !=
                mir.insns[expecteds[argument]].dst ||
            mir.insns[branches[argument]].opcode != MIR_BRANCH_FALSE)
            return mir_machine_reject(
                "fixed-index-call-runner", "call-check");
        plan->functions[argument] = function;
        if (!mir_machine_constant_value(
                mir.insns[expecteds[argument]].dst, &value, 0))
            return mir_machine_reject(
                "fixed-index-call-runner", "expected");
        if (argument < 3)
            plan->expected_words[argument] = (int)value;
        else
            plan->expected_longs[argument - 3] =
                (unsigned long)value;
    }
    for (argument = 0; argument < 2; ++argument) {
        int call_argument;

        if (!mir_machine_single_call_argument(
                &mir.insns[calls[argument]], &call_argument) ||
            !mir_machine_constant_value(
                call_argument, &value, 0))
            return mir_machine_reject(
                "fixed-index-call-runner", "scalar-argument");
        plan->scalar_arguments[argument] = (int)value;
    }
    for (argument = 0; argument < 3; ++argument) {
        if (!mir_machine_two_call_arguments(
                &mir.insns[calls[argument + 2]], arguments) ||
            arguments[0] != mir.insns[call_addresses[argument]].dst ||
            !mir_machine_constant_value(arguments[1], &value, 0))
            return mir_machine_reject(
                "fixed-index-call-runner", "pointer-argument");
        plan->pointer_indices[argument] = (int)value;
    }
    plan->print_function =
        find_global(mir.insns[147].name);
    if (plan->print_function == NULL ||
        mir.insns[147].opcode != MIR_CALL ||
        mir.insns[154].opcode != MIR_CALL ||
        find_global(mir.insns[154].name) != plan->print_function ||
        mir.insns[143].opcode != MIR_STRING_ADDRESS ||
        mir.insns[152].opcode != MIR_STRING_ADDRESS ||
        mir.insns[149].opcode != MIR_RETURN ||
        !mir_machine_constant_equals(mir.insns[148].dst, 1) ||
        mir.insns[156].opcode != MIR_RETURN ||
        !mir_machine_constant_equals(mir.insns[155].dst, 0))
        return mir_machine_reject(
            "fixed-index-call-runner", "final");
    for (argument = 0; argument < mir.count; ++argument)
        if (mir.insns[argument].opcode == MIR_CALL)
            ++call_count;
    if (call_count != 7)
        return mir_machine_reject(
            "fixed-index-call-runner", "call-count");
    plan->failure_string_id =
        (int)mir.insns[143].immediate;
    plan->success_string_id =
        (int)mir.insns[152].immediate;
    return 1;
}

static int mir_byte_equality_byte_type(int type, int is_unsigned)
{
    return type_ptr_depth(type) == 0 &&
           (type & 15) == TYPE_CHAR &&
           ((type & TYPE_UNSIGNED) != 0) == is_unsigned &&
           type_size(type) == 1;
}

static int mir_byte_equality_word_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           (type & 15) == TYPE_INT &&
           (type & TYPE_UNSIGNED) == 0 &&
           type_size(type) == 2;
}

static int mir_byte_equality_byte_pointer_type(
    int type, int is_unsigned)
{
    return type_ptr_depth(type) == 1 &&
           (type & 15) == TYPE_CHAR &&
           ((type & TYPE_UNSIGNED) != 0) == is_unsigned &&
           type_size(type) == 2;
}

static int mir_byte_equality_global(
    int instruction, int is_unsigned, struct Sym **symbol_out)
{
    const struct MirInsn *address = &mir.insns[instruction];
    struct Sym *symbol;

    if (address->opcode != MIR_ADDRESS ||
        !mir_machine_named_nonvolatile(address) ||
        !mir_byte_equality_byte_pointer_type(
            address->type, is_unsigned) ||
        (symbol = find_global(address->name)) == NULL ||
        symbol->storage == SC_FUNC)
        return 0;
    *symbol_out = symbol;
    return 1;
}

static int mir_byte_equality_same_global(
    int instruction, struct Sym *symbol, int is_unsigned)
{
    const struct MirInsn *address = &mir.insns[instruction];

    return address->opcode == MIR_ADDRESS &&
           mir_machine_named_nonvolatile(address) &&
           mir_byte_equality_byte_pointer_type(
               address->type, is_unsigned) &&
           find_global(address->name) == symbol;
}

static int mir_byte_equality_branch(
    int comparison, int branch, int label, int operation)
{
    return mir.insns[comparison].opcode == MIR_BINARY &&
           mir.insns[comparison].immediate == operation &&
           mir_byte_equality_word_type(mir.insns[comparison].type) &&
           mir.insns[branch].opcode == MIR_BRANCH_FALSE &&
           mir.insns[branch].src1 == mir.insns[comparison].dst &&
           mir.insns[branch].label == mir.insns[label].label;
}

static int mir_byte_equality_call(
    int call_instruction, int string_instruction,
    int actual_instruction, int expected_instruction,
    struct Sym *function, int expected, int *string_out)
{
    int arguments[3];

    if (mir.insns[string_instruction].opcode !=
            MIR_STRING_ADDRESS ||
        !mir_machine_call_arguments(
            &mir.insns[call_instruction], 3, arguments) ||
        arguments[0] != mir.insns[string_instruction].dst ||
        arguments[1] != mir.insns[actual_instruction].dst ||
        arguments[2] != mir.insns[expected_instruction].dst ||
        find_global(mir.insns[call_instruction].name) != function ||
        !mir_machine_constant_equals(
            mir.insns[expected_instruction].dst, expected))
        return 0;
    *string_out =
        (int)mir.insns[string_instruction].immediate;
    return 1;
}

static int mir_match_byte_equality_runner(
    struct MirByteEqualityRunner *plan)
{
    static const unsigned char expected_opcodes[380] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP,
        MIR_CONST, MIR_NOP, MIR_BINARY, MIR_UNARY,
        MIR_STORE, MIR_NOP, MIR_CONST, MIR_NOP,
        MIR_BINARY, MIR_UNARY, MIR_STORE, MIR_CONST,
        MIR_NOP, MIR_BINARY, MIR_UNARY, MIR_STORE,
        MIR_CONST, MIR_NOP, MIR_BINARY, MIR_UNARY,
        MIR_STORE, MIR_CONST, MIR_NOP, MIR_BINARY,
        MIR_UNARY, MIR_STORE, MIR_NOP, MIR_STORE,
        MIR_LOAD, MIR_UNARY, MIR_ADDRESS, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_INDEX_ADDRESS, MIR_NOP, MIR_STORE_INDIRECT,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_LABEL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_NOP, MIR_UNARY,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_LABEL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL,
        MIR_JUMP, MIR_LABEL, MIR_PHI, MIR_UNARY,
        MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_CONST,
        MIR_ADDRESS, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_NOP, MIR_UNARY, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP,
        MIR_STORE, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP,
        MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_BINARY,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP,
        MIR_UNARY, MIR_UNARY, MIR_BINARY, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_NOP, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_NOP, MIR_UNARY, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP,
        MIR_STORE, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_LABEL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP,
        MIR_STORE, MIR_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP,
        MIR_UNARY, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_LABEL,
        MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_LABEL,
        MIR_LABEL, MIR_PHI, MIR_ARG, MIR_CALL,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_RETURN
    };
    static const int initial_constants[5] = {
        65480, 65480, 65, 200, 200
    };
    static const int constant_instructions[5] = {
        4, 10, 15, 20, 25
    };
    static const int multiply_instructions[5] = {
        6, 12, 17, 22, 27
    };
    static const int cast_instructions[5] = {
        7, 13, 18, 23, 28
    };
    static const int store_instructions[5] = {
        8, 14, 19, 24, 29
    };
    static const int address_instructions[10] = {
        34, 41, 143, 168, 191, 247, 263, 289, 313, 339
    };
    static const int index_constants[10] = {
        36, 43, 145, 170, 193, 249, 265, 291, 315, 341
    };
    static const int index_adds[10] = {
        37, 44, 146, 171, 194, 250, 266, 292, 316, 342
    };
    static const int index_addresses[10] = {
        38, 45, 147, 172, 195, 251, 267, 293, 317, 343
    };
    static const int check_calls[15] = {
        66, 85, 105, 139, 163, 187, 212, 223,
        234, 244, 259, 284, 309, 334, 359
    };
    static const int check_strings[15] = {
        60, 79, 99, 133, 157, 181, 206, 213,
        224, 235, 245, 278, 303, 328, 353
    };
    static const int check_actuals[15] = {
        62, 81, 101, 131, 159, 183, 208, 219,
        230, 240, 255, 280, 305, 330, 355
    };
    static const int check_expecteds[15] = {
        64, 83, 103, 137, 161, 185, 210, 221,
        232, 242, 257, 282, 307, 332, 357
    };
    static const int expected_results[15] = {
        0, 1, 0, 0, 0, 0, 0, 1,
        1, 1, 1, 1, 1, 1, 1
    };
    static const int comparison_instructions[18] = {
        54, 73, 93, 109, 118, 151, 175, 200, 219,
        230, 240, 255, 272, 297, 322, 347, 364, 378
    };
    static const int comparison_lefts[18] = {
        53, 72, 91, 108, 117, 150, 167, 198, 217,
        228, 239, 254, 270, 295, 320, 345, 362, 376
    };
    static const int comparison_rights[18] = {
        52, 71, 92, 107, 116, 149, 174, 199, 218,
        229, 238, 253, 271, 296, 321, 346, 363, 377
    };
    static const int branch_comparisons[13] = {
        54, 73, 93, 109, 118, 151, 175,
        200, 272, 297, 322, 347, 364
    };
    static const int branches[13] = {
        55, 74, 94, 110, 119, 152, 176,
        201, 273, 298, 323, 348, 365
    };
    static const int branch_labels[13] = {
        59, 78, 98, 114, 123, 156, 180,
        205, 277, 302, 327, 352, 369
    };
    static const int signed_loads[5] = {
        148, 173, 196, 268, 294
    };
    static const int unsigned_loads[3] = {
        252, 318, 344
    };
    static const int signed_promotions[14] = {
        53, 72, 91, 108, 150, 174, 198,
        217, 218, 239, 270, 271, 295, 296
    };
    static const int signed_promotion_sources[14] = {
        7, 7, 7, 7, 148, 173, 196,
        7, 13, 18, 268, 7, 7, 294
    };
    static const int unsigned_promotions[10] = {
        92, 117, 199, 228, 229,
        254, 320, 321, 345, 346
    };
    static const int unsigned_promotion_sources[10] = {
        23, 23, 23, 23, 28,
        252, 318, 23, 23, 344
    };
    static const int result_zero_constants[10] = {
        48, 67, 86, 140, 164, 188, 260, 285, 310, 335
    };
    static const int result_zero_stores[10] = {
        50, 69, 88, 142, 166, 190, 262, 287, 312, 337
    };
    static const int result_one_constants[10] = {
        56, 75, 95, 153, 177, 202, 274, 299, 324, 349
    };
    static const int result_one_stores[10] = {
        58, 77, 97, 155, 179, 204, 276, 301, 326, 351
    };
    static const int result_loads[10] = {
        62, 81, 101, 159, 183, 208, 280, 305, 330, 355
    };
    int call_count = 0;
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 380 || mir_cfg_block_count() != 22 ||
        mir.has_vla || !mir_byte_equality_word_type(mir.return_type))
        return mir_machine_reject(
            "byte-equality-runner", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        if (mir.insns[instruction].opcode !=
                expected_opcodes[instruction])
            return mir_machine_reject(
                "byte-equality-runner", "opcode");
        if (mir.insns[instruction].opcode == MIR_CALL)
            ++call_count;
    }
    if (call_count != 16 ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->argc_stack_offset) ||
        plan->argc_stack_offset != 2 ||
        !mir_byte_equality_word_type(mir.insns[1].type) ||
        type_ptr_depth(mir.insns[2].type) != 2 ||
        (mir.insns[2].type & 15) != TYPE_CHAR)
        return mir_machine_reject(
            "byte-equality-runner", "parameters");
    if (strcmp(mir.insns[2].name, mir.insns[32].name) ||
        mir.insns[33].src1 != mir.insns[32].dst ||
        (mir.insns[33].type & 15) != TYPE_VOID)
        return mir_machine_reject(
            "byte-equality-runner", "unused-argv");

    for (item = 0; item < 5; ++item) {
        const struct MirInsn *binary =
            &mir.insns[multiply_instructions[item]];
        const struct MirInsn *cast =
            &mir.insns[cast_instructions[item]];
        const struct MirInsn *store =
            &mir.insns[store_instructions[item]];

        if (!mir_machine_constant_equals(
                mir.insns[constant_instructions[item]].dst,
                initial_constants[item]) ||
            binary->immediate != '*' ||
            binary->src1 !=
                mir.insns[constant_instructions[item]].dst ||
            binary->src2 != mir.insns[1].dst ||
            !mir_byte_equality_word_type(binary->type) ||
            cast->immediate != 0 ||
            cast->src1 != binary->dst ||
            !mir_byte_equality_byte_type(
                cast->type, item >= 3) ||
            store->src1 != cast->dst ||
            store->memory_size != 1 ||
            !mir_machine_unobservable_local_store(store))
            return mir_machine_reject(
                "byte-equality-runner", "initializers");
        if (item != 0 &&
            mir_machine_same_location(
                store, &mir.insns[store_instructions[0]]))
            return mir_machine_reject(
                "byte-equality-runner", "locals");
    }

    if (!mir_byte_equality_global(
            address_instructions[0], 0, &plan->signed_array) ||
        !mir_byte_equality_global(
            address_instructions[1], 1, &plan->unsigned_array) ||
        plan->signed_array == plan->unsigned_array ||
        !plan->signed_array->is_array ||
        !plan->unsigned_array->is_array ||
        plan->signed_array->array_len < 4 ||
        plan->unsigned_array->array_len < 4 ||
        plan->signed_array->elem_size != 1 ||
        plan->unsigned_array->elem_size != 1 ||
        !plan->signed_array->is_defined ||
        !plan->unsigned_array->is_defined)
        return mir_machine_reject(
            "byte-equality-runner", "arrays");
    for (item = 0; item < 10; ++item) {
        int is_unsigned =
            item == 1 || item == 5 || item >= 8;
        struct Sym *expected =
            is_unsigned ? plan->unsigned_array :
                          plan->signed_array;
        const struct MirInsn *add =
            &mir.insns[index_adds[item]];
        const struct MirInsn *address =
            &mir.insns[index_addresses[item]];

        if (!mir_byte_equality_same_global(
                address_instructions[item],
                expected, is_unsigned) ||
            !mir_machine_constant_equals(
                mir.insns[index_constants[item]].dst, 1) ||
            add->immediate != '+' ||
            add->src1 != mir.insns[1].dst ||
            add->src2 !=
                mir.insns[index_constants[item]].dst ||
            address->src1 !=
                mir.insns[address_instructions[item]].dst ||
            address->src2 != add->dst ||
            address->memory_size != 1 ||
            address->immediate != 1)
            return mir_machine_reject(
                "byte-equality-runner", "index");
    }
    if (mir.insns[40].src1 != mir.insns[38].dst ||
        mir.insns[40].src2 != mir.insns[7].dst ||
        mir.insns[40].memory_size != 1 ||
        mir.insns[47].src1 != mir.insns[45].dst ||
        mir.insns[47].src2 != mir.insns[23].dst ||
        mir.insns[47].memory_size != 1)
        return mir_machine_reject(
            "byte-equality-runner", "array-stores");
    for (item = 0; item < 5; ++item)
        if (!mir_byte_equality_byte_type(
                mir.insns[signed_loads[item]].type, 0) ||
            mir.insns[signed_loads[item]].memory_size != 1)
            return mir_machine_reject(
                "byte-equality-runner", "signed-load");
    for (item = 0; item < 3; ++item)
        if (!mir_byte_equality_byte_type(
                mir.insns[unsigned_loads[item]].type, 1) ||
            mir.insns[unsigned_loads[item]].memory_size != 1)
            return mir_machine_reject(
                "byte-equality-runner", "unsigned-load");
    for (item = 0; item < 14; ++item)
        if (mir.insns[signed_promotions[item]].src1 !=
                mir.insns[signed_promotion_sources[item]].dst ||
            mir.insns[signed_promotions[item]].immediate != 0 ||
            !mir_byte_equality_word_type(
                mir.insns[signed_promotions[item]].type))
            return mir_machine_reject(
                "byte-equality-runner", "signed-promotion");
    for (item = 0; item < 10; ++item)
        if (mir.insns[unsigned_promotions[item]].src1 !=
                mir.insns[unsigned_promotion_sources[item]].dst ||
            mir.insns[unsigned_promotions[item]].immediate != 0 ||
            !mir_byte_equality_word_type(
                mir.insns[unsigned_promotions[item]].type))
            return mir_machine_reject(
                "byte-equality-runner", "unsigned-promotion");

    for (item = 0; item < 18; ++item) {
        int operation =
            item == 1 || item == 17 ? TOK_NE : TOK_EQ;
        const struct MirInsn *comparison =
            &mir.insns[comparison_instructions[item]];

        if (comparison->immediate != operation ||
            comparison->src1 !=
                mir.insns[comparison_lefts[item]].dst ||
            comparison->src2 !=
                mir.insns[comparison_rights[item]].dst ||
            !mir_byte_equality_word_type(comparison->type))
            return mir_machine_reject(
                "byte-equality-runner", "comparison");
    }
    for (item = 0; item < 13; ++item) {
        int operation = item == 1 ? TOK_NE : TOK_EQ;

        if (!mir_byte_equality_branch(
                branch_comparisons[item], branches[item],
                branch_labels[item], operation))
            return mir_machine_reject(
                "byte-equality-runner", "branch");
    }
    for (item = 0; item < 10; ++item)
        if (!mir_machine_constant_equals(
                mir.insns[result_zero_constants[item]].dst, 0) ||
            mir.insns[result_zero_stores[item]].src1 !=
                mir.insns[result_zero_constants[item]].dst ||
            !mir_machine_unobservable_local_store(
                &mir.insns[result_zero_stores[item]]) ||
            !mir_machine_constant_equals(
                mir.insns[result_one_constants[item]].dst, 1) ||
            mir.insns[result_one_stores[item]].src1 !=
                mir.insns[result_one_constants[item]].dst ||
            !mir_machine_unobservable_local_store(
                &mir.insns[result_one_stores[item]]) ||
            !mir_machine_same_location(
                &mir.insns[result_zero_stores[item]],
                &mir.insns[result_one_stores[item]]) ||
            !mir_machine_same_location(
                &mir.insns[result_zero_stores[item]],
                &mir.insns[result_loads[item]]))
            return mir_machine_reject(
                "byte-equality-runner", "branch-result");

    if (mir.insns[113].label != mir.insns[129].label ||
        mir.insns[122].label != mir.insns[125].label ||
        mir.insns[128].label != mir.insns[129].label ||
        mir.insns[126].src1 != mir.insns[121].dst ||
        mir.insns[126].src2 != mir.insns[124].dst ||
        mir.insns[126].phi_pred1 != mir.insns[120].label ||
        mir.insns[126].phi_pred2 != mir.insns[123].label ||
        mir.insns[130].src1 != mir.insns[112].dst ||
        mir.insns[130].src2 != mir.insns[126].dst ||
        mir.insns[130].phi_pred1 != mir.insns[111].label ||
        mir.insns[130].phi_pred2 != mir.insns[127].label)
        return mir_machine_reject(
            "byte-equality-runner", "logical-or");

    plan->check_function =
        mir_memory_runner_call_function(66, 0, 3);
    if (plan->check_function == NULL ||
        (plan->check_function->type & 15) != TYPE_VOID ||
        type_ptr_depth(plan->check_function->proto_types[0]) != 1 ||
        (plan->check_function->proto_types[0] & 15) != TYPE_CHAR ||
        !mir_byte_equality_word_type(
            plan->check_function->proto_types[1]) ||
        !mir_byte_equality_word_type(
            plan->check_function->proto_types[2]))
        return mir_machine_reject(
            "byte-equality-runner", "check-function");
    for (item = 0; item < 15; ++item)
        if (!mir_byte_equality_call(
                check_calls[item], check_strings[item],
                check_actuals[item], check_expecteds[item],
                plan->check_function, expected_results[item],
                &plan->check_strings[item]))
            return mir_machine_reject(
                "byte-equality-runner", "check-call");

    plan->failures = find_global(mir.insns[362].name);
    plan->print_function =
        mir_allocation_runner_call_function(375, 1);
    if (plan->failures == NULL ||
        plan->failures->storage == SC_FUNC ||
    plan->failures->is_array ||
    !mir_byte_equality_word_type(plan->failures->type) ||
    !mir_machine_named_nonvolatile(&mir.insns[362]) ||
        !mir_machine_named_nonvolatile(&mir.insns[376]) ||
        find_global(mir.insns[376].name) != plan->failures ||
        plan->print_function == NULL ||
        plan->print_function->storage != SC_FUNC ||
        !plan->print_function->has_proto ||
        !plan->print_function->proto_variadic ||
        !mir_byte_equality_word_type(plan->print_function->type) ||
        plan->print_function->proto_nargs != 1 ||
        type_ptr_depth(plan->print_function->proto_types[0]) != 1 ||
        (plan->print_function->proto_types[0] & 15) != TYPE_CHAR ||
        !mir_machine_unobservable_local_store(&mir.insns[31]) ||
        !mir_machine_unobservable_local_store(&mir.insns[132]) ||
        !mir_byte_equality_word_type(mir.insns[362].type) ||
        !mir_byte_equality_word_type(mir.insns[376].type) ||
        mir.insns[360].opcode != MIR_STRING_ADDRESS ||
        mir.insns[366].opcode != MIR_STRING_ADDRESS ||
        mir.insns[370].opcode != MIR_STRING_ADDRESS ||
        mir.insns[368].label != mir.insns[372].label ||
        mir.insns[373].src1 != mir.insns[366].dst ||
        mir.insns[373].src2 != mir.insns[370].dst ||
        mir.insns[373].phi_pred1 != mir.insns[367].label ||
        mir.insns[373].phi_pred2 != mir.insns[371].label ||
        mir.insns[379].src1 != mir.insns[378].dst)
        return mir_machine_reject(
            "byte-equality-runner", "final");
    {
        int arguments[2];

        if (!mir_machine_two_call_arguments(
                &mir.insns[375], arguments) ||
            arguments[0] != mir.insns[360].dst ||
            arguments[1] != mir.insns[373].dst)
            return mir_machine_reject(
                "byte-equality-runner", "print-call");
    }
    plan->format_string = (int)mir.insns[360].immediate;
    plan->pass_string = (int)mir.insns[366].immediate;
    plan->fail_string = (int)mir.insns[370].immediate;
    return 1;
}

enum MirByteEqualityValue {
    MIR_BYTE_EQ_SIGNED_HIGH,
    MIR_BYTE_EQ_SIGNED_SAME,
    MIR_BYTE_EQ_SIGNED_LOW,
    MIR_BYTE_EQ_UNSIGNED_HIGH,
    MIR_BYTE_EQ_UNSIGNED_SAME,
    MIR_BYTE_EQ_SIGNED_ARRAY,
    MIR_BYTE_EQ_UNSIGNED_ARRAY,
    MIR_BYTE_EQ_CONSTANT
};

static void mir_byte_equality_load_parameter(
    FILE *out, const struct MirByteEqualityRunner *plan)
{
    fprintf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n",
            plan->argc_stack_offset + 2,
            plan->argc_stack_offset + 3);
}

static void mir_byte_equality_emit_multiply(
    FILE *out, const struct MirByteEqualityRunner *plan,
    int multiplier, int offset)
{
    fprintf(out, "\tld hl,%d\n\tpush hl\n", multiplier);
    mir_byte_equality_load_parameter(out, plan);
    fputs("\tex de,hl\n\tpop hl\n", out);
    mir_emit_runtime_call(out, "__mulu");
    fprintf(out, "\tld (ix%d),l\n", offset);
}

static void mir_byte_equality_array_address(
    FILE *out, const struct MirByteEqualityRunner *plan,
    struct Sym *array)
{
    mir_byte_equality_load_parameter(out, plan);
    fprintf(out,
            "\tinc hl\n\tld de,%s\n\tadd hl,de\n",
            asm_name_for(sym_asm_name(array)));
}

static void mir_byte_equality_load_value(
    FILE *out, const struct MirByteEqualityRunner *plan,
    enum MirByteEqualityValue value, int constant)
{
    int offset = 0;
    int is_unsigned = 0;

    switch (value) {
    case MIR_BYTE_EQ_SIGNED_HIGH:
        offset = -1;
        break;
    case MIR_BYTE_EQ_SIGNED_SAME:
        offset = -2;
        break;
    case MIR_BYTE_EQ_SIGNED_LOW:
        offset = -3;
        break;
    case MIR_BYTE_EQ_UNSIGNED_HIGH:
        offset = -4;
        is_unsigned = 1;
        break;
    case MIR_BYTE_EQ_UNSIGNED_SAME:
        offset = -5;
        is_unsigned = 1;
        break;
    case MIR_BYTE_EQ_SIGNED_ARRAY:
        mir_byte_equality_array_address(
            out, plan, plan->signed_array);
        fputs("\tld l,(hl)\n", out);
        break;
    case MIR_BYTE_EQ_UNSIGNED_ARRAY:
        mir_byte_equality_array_address(
            out, plan, plan->unsigned_array);
        fputs("\tld l,(hl)\n", out);
        is_unsigned = 1;
        break;
    case MIR_BYTE_EQ_CONSTANT:
        fprintf(out, "\tld hl,%d\n", constant);
        return;
    }
    if (value <= MIR_BYTE_EQ_UNSIGNED_SAME)
        fprintf(out, "\tld l,(ix%d)\n", offset);
    if (is_unsigned) {
        fputs("\tld h,0\n", out);
    } else {
        fputs("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n",
              out);
    }
}

static void mir_byte_equality_compare(
    FILE *out, const struct MirByteEqualityRunner *plan,
    enum MirByteEqualityValue left, int left_constant,
    enum MirByteEqualityValue right, int right_constant,
    int operation)
{
    int done = new_label();

    mir_byte_equality_load_value(
        out, plan, left, left_constant);
    fputs("\tpush hl\n", out);
    mir_byte_equality_load_value(
        out, plan, right, right_constant);
    fputs("\tex de,hl\n\tpop hl\n\tor a\n\tsbc hl,de\n"
          "\tld hl,0\n", out);
    fprintf(out, "\tjp %s,L%d\n",
            operation == TOK_EQ ? "nz" : "z", done);
    fputs("\tinc hl\n", out);
    fprintf(out, "L%d:\n", done);
}

static void mir_byte_equality_check(
    FILE *out, const struct MirByteEqualityRunner *plan,
    int check, int expected)
{
    fprintf(out,
            "\tld de,%d\n\tpush de\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            expected, plan->check_strings[check]);
    mir_machine_emit_symbol_call(
        out, plan->check_function);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n", out);
}

static void mir_emit_byte_equality_runner(
    FILE *out, const struct MirByteEqualityRunner *plan)
{
    int logical_true = new_label();
    int logical_done = new_label();
    int result_fail = new_label();
    int result_done = new_label();
    int return_nonzero = new_label();
    int return_done = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-5\n\tadd hl,sp\n\tld sp,hl\n", out);

    mir_byte_equality_emit_multiply(out, plan, 65480, -1);
    mir_byte_equality_emit_multiply(out, plan, 65480, -2);
    mir_byte_equality_emit_multiply(out, plan, 65, -3);
    mir_byte_equality_emit_multiply(out, plan, 200, -4);
    mir_byte_equality_emit_multiply(out, plan, 200, -5);

    mir_byte_equality_array_address(
        out, plan, plan->signed_array);
    fputs("\tld a,(ix-1)\n\tld (hl),a\n", out);
    mir_byte_equality_array_address(
        out, plan, plan->unsigned_array);
    fputs("\tld a,(ix-4)\n\tld (hl),a\n", out);

    mir_byte_equality_compare(
        out, plan, MIR_BYTE_EQ_SIGNED_HIGH, 0,
        MIR_BYTE_EQ_CONSTANT, 200, TOK_EQ);
    mir_byte_equality_check(out, plan, 0, 0);

    mir_byte_equality_compare(
        out, plan, MIR_BYTE_EQ_SIGNED_HIGH, 0,
        MIR_BYTE_EQ_CONSTANT, 200, TOK_NE);
    mir_byte_equality_check(out, plan, 1, 1);

    mir_byte_equality_compare(
        out, plan, MIR_BYTE_EQ_SIGNED_HIGH, 0,
        MIR_BYTE_EQ_UNSIGNED_HIGH, 0, TOK_EQ);
    mir_byte_equality_check(out, plan, 2, 0);

    mir_byte_equality_compare(
        out, plan, MIR_BYTE_EQ_SIGNED_HIGH, 0,
        MIR_BYTE_EQ_CONSTANT, 200, TOK_EQ);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", logical_true);
    mir_byte_equality_compare(
        out, plan, MIR_BYTE_EQ_UNSIGNED_HIGH, 0,
        MIR_BYTE_EQ_CONSTANT, 201, TOK_EQ);
    fprintf(out, "\tjp L%d\nL%d:\n\tld hl,1\nL%d:\n",
            logical_done, logical_true, logical_done);
    mir_byte_equality_check(out, plan, 3, 0);

    mir_byte_equality_compare(
        out, plan, MIR_BYTE_EQ_SIGNED_ARRAY, 0,
        MIR_BYTE_EQ_CONSTANT, 200, TOK_EQ);
    mir_byte_equality_check(out, plan, 4, 0);

    mir_byte_equality_compare(
        out, plan, MIR_BYTE_EQ_CONSTANT, 200,
        MIR_BYTE_EQ_SIGNED_ARRAY, 0, TOK_EQ);
    mir_byte_equality_check(out, plan, 5, 0);

    mir_byte_equality_compare(
        out, plan, MIR_BYTE_EQ_SIGNED_ARRAY, 0,
        MIR_BYTE_EQ_UNSIGNED_HIGH, 0, TOK_EQ);
    mir_byte_equality_check(out, plan, 6, 0);

    mir_byte_equality_compare(
        out, plan, MIR_BYTE_EQ_SIGNED_HIGH, 0,
        MIR_BYTE_EQ_SIGNED_SAME, 0, TOK_EQ);
    mir_byte_equality_check(out, plan, 7, 1);

    mir_byte_equality_compare(
        out, plan, MIR_BYTE_EQ_UNSIGNED_HIGH, 0,
        MIR_BYTE_EQ_UNSIGNED_SAME, 0, TOK_EQ);
    mir_byte_equality_check(out, plan, 8, 1);

    mir_byte_equality_compare(
        out, plan, MIR_BYTE_EQ_SIGNED_LOW, 0,
        MIR_BYTE_EQ_CONSTANT, 65, TOK_EQ);
    mir_byte_equality_check(out, plan, 9, 1);

    mir_byte_equality_compare(
        out, plan, MIR_BYTE_EQ_UNSIGNED_ARRAY, 0,
        MIR_BYTE_EQ_CONSTANT, 200, TOK_EQ);
    mir_byte_equality_check(out, plan, 10, 1);

    mir_byte_equality_compare(
        out, plan, MIR_BYTE_EQ_SIGNED_ARRAY, 0,
        MIR_BYTE_EQ_SIGNED_HIGH, 0, TOK_EQ);
    mir_byte_equality_check(out, plan, 11, 1);

    mir_byte_equality_compare(
        out, plan, MIR_BYTE_EQ_SIGNED_HIGH, 0,
        MIR_BYTE_EQ_SIGNED_ARRAY, 0, TOK_EQ);
    mir_byte_equality_check(out, plan, 12, 1);

    mir_byte_equality_compare(
        out, plan, MIR_BYTE_EQ_UNSIGNED_ARRAY, 0,
        MIR_BYTE_EQ_UNSIGNED_HIGH, 0, TOK_EQ);
    mir_byte_equality_check(out, plan, 13, 1);

    mir_byte_equality_compare(
        out, plan, MIR_BYTE_EQ_UNSIGNED_HIGH, 0,
        MIR_BYTE_EQ_UNSIGNED_ARRAY, 0, TOK_EQ);
    mir_byte_equality_check(out, plan, 14, 1);

    mir_machine_emit_global_word(out, plan->failures, 0);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out,
            "\tjp nz,L%d\n\tld hl,S%d\n\tjp L%d\n"
            "L%d:\n\tld hl,S%d\n"
            "L%d:\n\tpush hl\n\tld hl,S%d\n\tpush hl\n",
            result_fail, plan->pass_string, result_done,
            result_fail, plan->fail_string,
            result_done, plan->format_string);
    mir_machine_emit_symbol_call(
        out, plan->print_function);
    fputs("\tpop bc\n\tpop bc\n", out);

    mir_machine_emit_global_word(out, plan->failures, 0);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out,
            "\tjp nz,L%d\n\tld hl,0\n\tjp L%d\n"
            "L%d:\n\tld hl,1\n"
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            return_nonzero, return_done,
            return_nonzero, return_done);
}

static int mir_gnarly_opcode_code(int opcode)
{
    switch (opcode) {
    case MIR_LABEL: return 'L';
    case MIR_PARAM: return 'Q';
    case MIR_NOP: return 'N';
    case MIR_CONST: return 'C';
    case MIR_STORE: return 'S';
    case MIR_PHI: return 'P';
    case MIR_BINARY: return 'B';
    case MIR_BRANCH_FALSE: return 'F';
    case MIR_ADDRESS: return 'A';
    case MIR_INDEX_ADDRESS: return 'I';
    case MIR_MEMBER_ADDRESS: return 'M';
    case MIR_LOAD_INDIRECT: return 'R';
    case MIR_STORE_INDIRECT: return 'W';
    case MIR_LOAD: return 'D';
    case MIR_STRING_ADDRESS: return 'T';
    case MIR_ARG: return 'G';
    case MIR_CALL: return 'K';
    case MIR_UNARY: return 'U';
    case MIR_JUMP: return 'J';
    case MIR_RETURN: return 'E';
    default: return 0;
    }
}

static int mir_gnarly_word_type(int type, int is_unsigned)
{
    return type_ptr_depth(type) == 0 &&
           (type & 15) == TYPE_INT &&
           ((type & TYPE_UNSIGNED) != 0) == is_unsigned &&
           type_size(type) == 2;
}

static int mir_gnarly_char_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           (type & 15) == TYPE_CHAR &&
           (type & TYPE_UNSIGNED) == 0 &&
           type_size(type) == 1;
}

static int mir_gnarly_string_type(int type)
{
    return type_ptr_depth(type) == 1 &&
           (type & 15) == TYPE_CHAR &&
           type_size(type) == 2;
}

static int mir_gnarly_value_from(int value, int instruction)
{
    return value >= 0 &&
           mir.insns[instruction].dst == value;
}

static int mir_gnarly_binary(
    int instruction, int left, int right, int operation)
{
    const struct MirInsn *binary = &mir.insns[instruction];

    return binary->opcode == MIR_BINARY &&
           mir_gnarly_value_from(binary->src1, left) &&
           mir_gnarly_value_from(binary->src2, right) &&
           binary->immediate == operation;
}

static int mir_gnarly_branch(
    int instruction, int value, int target)
{
    return mir.insns[instruction].opcode == MIR_BRANCH_FALSE &&
           mir_gnarly_value_from(mir.insns[instruction].src1, value) &&
           mir.insns[instruction].label == mir.insns[target].label;
}

static int mir_gnarly_phi(
    int instruction, int left, int right,
    int left_label, int right_label)
{
    const struct MirInsn *phi = &mir.insns[instruction];

    return phi->opcode == MIR_PHI &&
           mir_gnarly_value_from(phi->src1, left) &&
           mir_gnarly_value_from(phi->src2, right) &&
           phi->phi_pred1 == mir.insns[left_label].label &&
           phi->phi_pred2 == mir.insns[right_label].label;
}

static struct Sym *mir_gnarly_direct_function(
    int instruction, int argument_count, int is_void)
{
    const struct MirInsn *call = &mir.insns[instruction];
    struct Sym *function;
    const char *assembly_name;

    if (call->opcode != MIR_CALL || call->src1 >= 0 ||
        call->memory_flags != 0 ||
        (function = find_global(call->name)) == NULL ||
        function->storage != SC_FUNC || !function->is_defined ||
        function->is_funcptr || function->is_noreturn ||
        ((call->type & 15) == TYPE_VOID) != is_void ||
        (is_void ? type_ptr_depth(call->type) != 0 :
         !mir_gnarly_word_type(call->type, 0)))
        return NULL;
    assembly_name = asm_name_for(sym_asm_name(function));
    if (call->base_name[0] != 0 &&
        strcmp(call->base_name, assembly_name))
        return NULL;
    if (argument_count == 0) {
        int instruction_index;

        for (instruction_index = 0;
             instruction_index < mir.count; ++instruction_index)
            if (mir.insns[instruction_index].opcode == MIR_ARG &&
                mir.insns[instruction_index].secondary_offset ==
                    call->secondary_offset)
                return NULL;
    }
    return function;
}

static int mir_gnarly_print_call(
    struct MirGnarlyRunner *plan, int slot, int instruction,
    int call_id, int argument_count, const int *definitions)
{
    const struct MirInsn *call = &mir.insns[instruction];
    struct Sym *function;
    int arguments[5];
    int item;

    if (slot < 0 || slot >= 32 || argument_count < 1 ||
        argument_count > 5 ||
        call->opcode != MIR_CALL || call->src1 >= 0 ||
        call->secondary_offset != call_id ||
        call->memory_flags != MIR_CALL_FLAG_VARIADIC ||
        !mir_gnarly_word_type(call->type, 0) ||
        call->base_name[0] == 0 ||
        !mir_machine_call_arguments(
            call, argument_count, arguments) ||
        (function = find_global(call->name)) == NULL ||
        function->storage != SC_FUNC || function->is_defined ||
        function->is_funcptr || function->is_noreturn ||
        !function->has_proto || function->proto_nargs != 1 ||
        !function->proto_variadic ||
        !mir_gnarly_string_type(function->proto_types[0]) ||
        !mir_gnarly_word_type(function->type, 0) ||
        (plan->print_function != NULL &&
         plan->print_function != function))
        return 0;
    for (item = 0; item < argument_count; ++item)
        if (!mir_gnarly_value_from(
                arguments[item], definitions[item]))
            return 0;
    plan->print_function = function;
    snprintf(plan->print_names[slot],
             sizeof(plan->print_names[slot]), "%s",
             call->base_name);
    return 1;
}

static int mir_match_gnarly_runner(struct MirGnarlyRunner *plan)
{
    static const char expected_opcodes[] =
        "LCSCNSKCNSLNNPNCBFANINCBWLNCBSJLCNSLNNPNCBFANICWLNCBSJLAGAGCGKTGACIRGACIRGKKTGNU"
        "GNGKTGKGKCNSNCNSTGNGKCNSCNSNCBSNBNSTGNGKTGNGKTCIRNSTGNGKACICWACICWACICWACICWACIC"
        "WACIRNSTGNGKAKTNSTGDGKCNSCNSNCBSNCBSBNSTGNGNGNGKCNSCNSTGNNBGNNBGNNBGKTGACCBBRGAC"
        "IRGKCNSTGACNSNCBIRGKCNSANCBNSICWTGNGACIRGKANSDKTGTGKCNSTGNCGNGKTGCNGNCGKTGCGCGKG"
        "KTGNCGNCGKNAMCWAMCWANSTGAMRGAMRGKNNNNNCSNCSTGCFDLJLDLLPGKNNANSTGDNCIRGKNCNSTNIRN"
        "STGNGKTRNSTGNGKTCIRUSTGNGKCNSNSNSTGNGNGNGKCNSCNSCNSLNNNPPPNNNNNBFNNNBBNSLNCBSNCB"
        "SJLTGNGKTGNCGKNCNSNCBFCLJLNCBFNCLJLCLLPLLPNSTGNGKTGCGCGCGCGKNCSNUNSTGNGKNNCNSTGN"
        "GKCE";
    static const int constant_instructions[82] = {
        1, 3, 7, 15, 22, 27, 32, 40, 46, 50, 59, 65, 70, 89,
        93, 101, 104, 108, 126, 137, 139, 142, 144, 147, 149,
        152, 154, 157, 159, 162, 182, 185, 189, 193, 208, 211,
        232, 233, 239, 244, 250, 254, 260, 265, 270, 277, 292,
        298, 305, 309, 314, 316, 324, 327, 333, 337, 358, 361,
        365, 386, 392, 416, 426, 442, 445, 448, 474, 478, 491,
        495, 499, 502, 507, 511, 515, 531, 533, 535, 537, 541,
        554, 562
    };
    static const int constant_values[82] = {
        10, 2, 0, 5, 1, 1, 0, 5, 0, 1, 5, 0, 4, 10, 5, 20,
        30, 1, 0, 0, 10, 1, 20, 2, 30, 3, 40, 4, 50, 2, 1, 2,
        1, 1, 6, 3, 3, 2, 3, 0, 1, 2, 1, 2, 99, 3, 5, 2, 65,
        2, 7, 8, 10, 2, 42, 120, -1, 1, 0, 4, 2, 0, 5, 0, 0,
        9, 1, 1, 6, 65533, 0, 1, 0, 65535, 0, 255, 127, 65, 65,
        65, 65520, 0
    };
    static const int binary_instructions[24] = {
        16, 23, 28, 41, 51, 109, 112, 190, 194, 196, 218, 222,
        226, 234, 235, 255, 266, 463, 468, 469, 475, 479, 500, 508
    };
    static const int binary_lefts[24] = {
        13, 13, 13, 38, 38, 101, 101, 182, 185, 182, 208, 208,
        208, 232, 231, 250, 260, 455, 455, 457, 455, 456, 495, 495
    };
    static const int binary_rights[24] = {
        15, 22, 27, 40, 50, 108, 104, 189, 193, 194, 211, 211,
        211, 233, 234, 254, 265, 456, 456, 468, 474, 478, 499, 507
    };
    static const int binary_operations[24] = {
        '<', '+', '+', '<', '+', '+', '+', '-', '-', '-', '&', '|',
        '^', '*', '+', '+', '+', '<', '+', '+', '+', '-', '>', '<'
    };
    static const int string_instructions[35] = {
        342, 363, 382, 62, 76, 84, 96, 115, 120, 125, 131, 174,
        177, 199, 214, 229, 247, 272, 287, 289, 295, 303, 312,
        321, 395, 401, 410, 421, 433, 483, 488, 524, 529, 547, 557
    };
    static const int print_calls[32] = {
        74, 83, 88, 100, 119, 124, 135, 171, 181, 207, 228, 243,
        259, 281, 291, 302, 311, 320, 329, 352, 376, 390, 405,
        414, 425, 441, 487, 493, 528, 539, 551, 561
    };
    static const int print_call_ids[32] = {
        2, 4, 5, 7, 8, 9, 10, 11, 13, 14, 15, 16, 17, 18, 20,
        21, 22, 23, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,
        36, 37, 38
    };
    static const unsigned char print_argument_counts[32] = {
        3, 3, 2, 2, 2, 2, 2, 2, 2, 4, 4, 3, 2, 3, 2, 3,
        3, 2, 3, 3, 2, 2, 2, 2, 2, 4, 2, 2, 2, 5, 2, 2
    };
    static const int print_arguments[32][5] = {
        {62, 67, 72, -1, -1}, {76, 79, 1, -1, -1},
        {84, 86, -1, -1, -1}, {96, 93, -1, -1, -1},
        {115, 112, -1, -1, -1}, {120, 109, -1, -1, -1},
        {131, 128, -1, -1, -1}, {167, 164, -1, -1, -1},
        {177, 179, -1, -1, -1}, {199, 196, 190, 194, -1},
        {214, 218, 222, 226, -1}, {229, 236, 241, -1, -1},
        {247, 257, -1, -1, -1}, {272, 266, 279, -1, -1},
        {287, 289, -1, -1, -1}, {295, 298, 292, -1, -1},
        {303, 305, 309, -1, -1}, {312, 318, -1, -1, -1},
        {321, 324, 327, -1, -1}, {342, 346, 350, -1, -1},
        {363, 374, -1, -1, -1}, {382, 388, -1, -1, -1},
        {401, 398, -1, -1, -1}, {410, 407, -1, -1, -1},
        {421, 419, -1, -1, -1}, {433, 426, 426, 426, -1},
        {483, 457, -1, -1, -1}, {488, 491, -1, -1, -1},
        {524, 521, -1, -1, -1}, {529, 531, 533, 535, 537},
        {547, 544, -1, -1, -1}, {557, 554, -1, -1, -1}
    };
    static const int arr_addresses[12] = {
        136, 141, 146, 151, 156, 161, 231,
        238, 249, 263, 276, 379
    };
    static const int arr_indices[11] = {
        138, 143, 148, 153, 158, 163,
        240, 256, 269, 278, 387
    };
    static const int arr_index_values[11] = {
        137, 142, 147, 152, 157, 162,
        239, 255, 266, 277, 386
    };
    int call_count = 0;
    int instruction;
    int item;
    int previous;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 564 || mir_cfg_block_count() != 22 ||
        mir.has_vla || mir.local_bytes != 68 ||
        mir.object_count != 9 ||
        !mir_gnarly_word_type(mir.return_type, 0) ||
        strlen(expected_opcodes) != (size_t)mir.count)
        return mir_machine_reject(
            "gnarly-call-runner", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        if (mir_gnarly_opcode_code(
                mir.insns[instruction].opcode) !=
                expected_opcodes[instruction])
            return mir_machine_reject(
                "gnarly-call-runner", "opcode");
        if (mir.insns[instruction].opcode == MIR_CALL)
            ++call_count;
    }
    if (call_count != 39)
        return mir_machine_reject(
            "gnarly-call-runner", "call-count");
    for (item = 0; item < 82; ++item)
        if (!mir_machine_constant_equals(
                mir.insns[constant_instructions[item]].dst,
                constant_values[item]))
            return mir_machine_reject(
                "gnarly-call-runner", "constants");
    for (item = 0; item < 24; ++item)
        if (!mir_gnarly_binary(
                binary_instructions[item],
                binary_lefts[item], binary_rights[item],
                binary_operations[item]))
            return mir_machine_reject(
                "gnarly-call-runner", "operations");

    if (!mir_gnarly_phi(13, 7, 28, 0, 25) ||
        !mir_gnarly_phi(38, 32, 51, 31, 48) ||
        !mir_gnarly_phi(374, 367, 371, 368, 372) ||
        !mir_gnarly_phi(455, 445, 475, 373, 472) ||
        !mir_gnarly_phi(456, 448, 479, 373, 472) ||
        !mir_gnarly_phi(457, 442, 469, 373, 472) ||
        !mir_gnarly_phi(518, 511, 515, 512, 516) ||
        !mir_gnarly_phi(521, 502, 518, 503, 519) ||
        !mir_gnarly_branch(17, 16, 31) ||
        !mir_gnarly_branch(42, 41, 54) ||
        !mir_gnarly_branch(366, 365, 370) ||
        !mir_gnarly_branch(464, 463, 482) ||
        !mir_gnarly_branch(501, 500, 505) ||
        !mir_gnarly_branch(509, 508, 514) ||
        mir.insns[30].label != mir.insns[10].label ||
        mir.insns[53].label != mir.insns[35].label ||
        mir.insns[369].label != mir.insns[373].label ||
        mir.insns[481].label != mir.insns[451].label ||
        mir.insns[504].label != mir.insns[520].label ||
        mir.insns[513].label != mir.insns[517].label)
        return mir_machine_reject(
            "gnarly-call-runner", "control-flow");

    if (mir.insns[79].src1 != mir.insns[3].dst ||
        mir.insns[79].immediate != 0 ||
        type_ptr_depth(mir.insns[79].type) != 0 ||
        (mir.insns[79].type & 15) != TYPE_LONG ||
        (mir.insns[79].type & TYPE_UNSIGNED) == 0 ||
        type_size(mir.insns[79].type) != 4 ||
        mir.insns[419].src1 != mir.insns[418].dst ||
        mir.insns[419].immediate != 0 ||
        !mir_gnarly_char_type(mir.insns[419].type) ||
        mir.insns[544].src1 != mir.insns[541].dst ||
        mir.insns[544].immediate != '+' ||
        !mir_gnarly_word_type(mir.insns[544].type, 0))
        return mir_machine_reject(
            "gnarly-call-runner", "conversions");

    for (item = 0; item < 35; ++item) {
        const struct MirInsn *string =
            &mir.insns[string_instructions[item]];

        if (!mir_gnarly_string_type(string->type) ||
            string->immediate < 0)
            return mir_machine_reject(
                "gnarly-call-runner", "strings");
        plan->strings[item] = (int)string->immediate;
        for (previous = 0; previous < item; ++previous)
            if (plan->strings[item] == plan->strings[previous])
                return mir_machine_reject(
                    "gnarly-call-runner", "distinct-strings");
    }
    if (mir.insns[167].immediate != plan->strings[6] ||
        mir.insns[406].immediate != plan->strings[24] ||
        mir.insns[415].immediate != plan->strings[24])
        return mir_machine_reject(
            "gnarly-call-runner", "reused-strings");
    for (item = 0; item < 32; ++item)
        if (!mir_gnarly_print_call(
                plan, item, print_calls[item],
                print_call_ids[item],
                print_argument_counts[item],
                print_arguments[item]))
            return mir_machine_reject(
                "gnarly-call-runner", "print-calls");

    plan->hello_function =
        mir_gnarly_direct_function(6, 0, 0);
    plan->duff_function =
        mir_gnarly_direct_function(61, 3, 1);
    plan->structure_function =
        mir_gnarly_direct_function(75, 0, 0);
    plan->implicit_function =
        mir_gnarly_direct_function(86, 0, 0);
    plan->sum_function =
        mir_gnarly_direct_function(318, 2, 0);
    if (plan->hello_function == NULL ||
        plan->duff_function == NULL ||
        plan->structure_function == NULL ||
        plan->implicit_function == NULL ||
        plan->sum_function == NULL ||
        plan->hello_function == plan->duff_function ||
        plan->hello_function == plan->structure_function ||
        plan->hello_function == plan->implicit_function ||
        plan->hello_function == plan->sum_function ||
        plan->duff_function == plan->structure_function ||
        plan->duff_function == plan->implicit_function ||
        plan->duff_function == plan->sum_function ||
        plan->structure_function == plan->implicit_function ||
        plan->structure_function == plan->sum_function ||
        plan->implicit_function == plan->sum_function)
        return mir_machine_reject(
            "gnarly-call-runner", "direct-functions");
    {
        int arguments[3];

        if (!mir_machine_call_arguments(
                &mir.insns[61], 3, arguments) ||
            !mir_gnarly_value_from(arguments[0], 55) ||
            !mir_gnarly_value_from(arguments[1], 57) ||
            !mir_gnarly_value_from(arguments[2], 59) ||
            !mir_machine_call_arguments(
                &mir.insns[318], 2, arguments) ||
            !mir_gnarly_value_from(arguments[0], 314) ||
            !mir_gnarly_value_from(arguments[1], 316))
            return mir_machine_reject(
                "gnarly-call-runner", "direct-arguments");
    }

    plan->indirect_function =
        find_global(mir.insns[172].name);
    if (plan->indirect_function == NULL ||
        plan->indirect_function->storage != SC_FUNC ||
        !plan->indirect_function->is_defined ||
        plan->indirect_function->is_funcptr ||
        plan->indirect_function->is_noreturn ||
        plan->indirect_function == plan->hello_function ||
        plan->indirect_function == plan->duff_function ||
        plan->indirect_function == plan->structure_function ||
        plan->indirect_function == plan->implicit_function ||
        plan->indirect_function == plan->sum_function ||
        !mir_gnarly_value_from(mir.insns[173].src1, 172) ||
        (mir.insns[173].type & 15) != TYPE_VOID ||
        find_global(mir.insns[282].name) !=
            plan->indirect_function ||
        !mir_gnarly_value_from(mir.insns[284].src1, 282) ||
        !mir_machine_same_location(
            &mir.insns[284], &mir.insns[285]) ||
        !mir_gnarly_value_from(mir.insns[286].src1, 285) ||
        (mir.insns[286].type & 15) != TYPE_VOID)
        return mir_machine_reject(
            "gnarly-call-runner", "indirect-calls");

    if (!mir_machine_same_location(
            &mir.insns[18], &mir.insns[57]) ||
        !mir_machine_same_location(
            &mir.insns[43], &mir.insns[55]) ||
        !mir_machine_same_location(
            &mir.insns[43], &mir.insns[64]) ||
        !mir_machine_same_location(
            &mir.insns[43], &mir.insns[69]) ||
        mir_machine_same_location(
            &mir.insns[18], &mir.insns[43]))
        return mir_machine_reject(
            "gnarly-call-runner", "duff-arrays");
    for (item = 1; item < 12; ++item)
        if (!mir_machine_same_location(
                &mir.insns[arr_addresses[0]],
                &mir.insns[arr_addresses[item]]))
            return mir_machine_reject(
                "gnarly-call-runner", "main-array");
    if (mir_machine_same_location(
            &mir.insns[arr_addresses[0]], &mir.insns[18]))
        return mir_machine_reject(
            "gnarly-call-runner", "distinct-arrays");
    for (item = 0; item < 11; ++item) {
        const struct MirInsn *index =
            &mir.insns[arr_indices[item]];

        if (index->memory_size != 2 ||
            index->immediate != 2 ||
            !mir_gnarly_value_from(
                index->src2, arr_index_values[item]))
            return mir_machine_reject(
                "gnarly-call-runner", "array-indices");
    }
    if (!mir_machine_same_location(
            &mir.insns[331], &mir.insns[339]) ||
        !mir_machine_same_location(
            &mir.insns[340], &mir.insns[344]) ||
        mir_machine_same_location(
            &mir.insns[331], &mir.insns[340]) ||
        mir.insns[332].src1 != mir.insns[331].dst ||
        mir.insns[332].immediate != 0 ||
        mir.insns[332].memory_size != 2 ||
        mir.insns[336].src1 != mir.insns[335].dst ||
        mir.insns[336].immediate != 2 ||
        mir.insns[336].memory_size != 1 ||
        mir.insns[341].src1 != mir.insns[339].dst ||
        mir.insns[345].src1 != mir.insns[344].dst ||
        mir.insns[345].immediate != 0 ||
        mir.insns[349].src1 != mir.insns[348].dst ||
        mir.insns[349].immediate != 2)
        return mir_machine_reject(
            "gnarly-call-runner", "structure-copy");

    if (mir.insns[2].object != 0 ||
        mir.insns[5].object != 1 ||
        mir.insns[9].object != 2 ||
        mir.insns[91].object != 3 ||
        mir.insns[106].object != 4 ||
        mir.insns[114].object != 5 ||
        mir.insns[130].object != 6 ||
        mir.insns[210].object != 7 ||
        mir.insns[213].object != 8 ||
        mir.insns[563].src1 != mir.insns[562].dst)
        return mir_machine_reject(
            "gnarly-call-runner", "objects-return");
    return 1;
}

static void mir_gnarly_cleanup(FILE *out, int words)
{
    while (words-- > 0)
        fputs("\tpop bc\n", out);
}

static void mir_gnarly_ix_address(FILE *out, int offset)
{
    fprintf(out,
            "\tpush ix\n\tpop hl\n\tld de,%d\n\tadd hl,de\n",
            offset);
}

static void mir_gnarly_store_word(
    FILE *out, int offset, int value)
{
    fprintf(out,
            "\tld hl,%d\n\tld (ix%d),l\n\tld (ix%d),h\n",
            value, offset, offset + 1);
}

static void mir_gnarly_load_word(FILE *out, int offset)
{
    fprintf(out,
            "\tld l,(ix%d)\n\tld h,(ix%d)\n",
            offset, offset + 1);
}

static void mir_gnarly_push_word(
    FILE *out, int value)
{
    fprintf(out, "\tld hl,%d\n\tpush hl\n", value);
}

static void mir_gnarly_push_string(
    FILE *out, const struct MirGnarlyRunner *plan, int string)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[string]);
}

static void mir_gnarly_print(
    FILE *out, const struct MirGnarlyRunner *plan,
    int print_slot, int argument_words)
{
    mir_emit_runtime_call(out, plan->print_names[print_slot]);
    mir_gnarly_cleanup(out, argument_words);
}

static void mir_gnarly_load_array_word(
    FILE *out, int base_offset, int index)
{
    mir_gnarly_ix_address(out, base_offset + index * 2);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
          out);
}

static void mir_gnarly_store_array_word(
    FILE *out, int base_offset, int index, int value)
{
    mir_gnarly_ix_address(out, base_offset + index * 2);
    fprintf(out,
            "\tld de,%d\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n",
            value);
}

static void mir_gnarly_indirect_call(
    FILE *out, struct Sym *function)
{
    fprintf(out, "\tld hl,%s\n",
            asm_name_for(sym_asm_name(function)));
    mir_emit_runtime_call(out, "__call_hl");
}

static void mir_emit_gnarly_runner(
    FILE *out, const struct MirGnarlyRunner *plan)
{
    int source_loop = new_label();
    int zero_loop = new_label();
    int copy_loop = new_label();
    int conditional_false = new_label();
    int conditional_done = new_label();
    int comma_loop = new_label();
    int comma_done = new_label();
    int ternary_negative = new_label();
    int ternary_zero = new_label();
    int ternary_done = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-20\n\tadd hl,sp\n\tld sp,hl\n", out);

    mir_gnarly_store_word(out, -20, 10);
    mir_gnarly_store_word(out, -18, 2);
    mir_machine_emit_symbol_call(out, plan->hello_function);

    mir_gnarly_ix_address(out, -10);
    fputs("\tld de,1\n\tld b,5\n", out);
    fprintf(out,
            "L%d:\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n"
            "\tinc hl\n\tinc de\n\tdjnz L%d\n",
            source_loop, source_loop);
    mir_gnarly_ix_address(out, -20);
    fputs("\txor a\n\tld b,10\n", out);
    fprintf(out,
            "L%d:\n\tld (hl),a\n\tinc hl\n\tdjnz L%d\n",
            zero_loop, zero_loop);

    mir_gnarly_push_word(out, 5);
    mir_gnarly_ix_address(out, -10);
    fputs("\tpush hl\n", out);
    mir_gnarly_ix_address(out, -20);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->duff_function);
    mir_gnarly_cleanup(out, 3);

    mir_gnarly_load_array_word(out, -20, 4);
    fputs("\tpush hl\n", out);
    mir_gnarly_load_array_word(out, -20, 0);
    fputs("\tpush hl\n", out);
    mir_gnarly_push_string(out, plan, 3);
    mir_gnarly_print(out, plan, 0, 3);

    mir_machine_emit_symbol_call(out, plan->structure_function);
    mir_gnarly_push_word(out, 10);
    mir_gnarly_push_word(out, 0);
    mir_gnarly_push_word(out, 2);
    mir_gnarly_push_string(out, plan, 4);
    mir_gnarly_print(out, plan, 1, 4);

    mir_machine_emit_symbol_call(out, plan->implicit_function);
    fputs("\tpush hl\n", out);
    mir_gnarly_push_string(out, plan, 5);
    mir_gnarly_print(out, plan, 2, 2);

    mir_gnarly_store_word(out, -20, 10);
    mir_gnarly_store_word(out, -20, 5);
    mir_gnarly_load_word(out, -20);
    fputs("\tpush hl\n", out);
    mir_gnarly_push_string(out, plan, 6);
    mir_gnarly_print(out, plan, 3, 2);

    mir_gnarly_store_word(out, -20, 20);
    mir_gnarly_store_word(out, -18, 30);
    mir_gnarly_store_word(out, -20, 21);
    mir_gnarly_store_word(out, -16, 50);
    mir_gnarly_load_word(out, -16);
    fputs("\tpush hl\n", out);
    mir_gnarly_push_string(out, plan, 7);
    mir_gnarly_print(out, plan, 4, 2);
    mir_gnarly_load_word(out, -20);
    fputs("\tpush hl\n", out);
    mir_gnarly_push_string(out, plan, 8);
    mir_gnarly_print(out, plan, 5, 2);

    fprintf(out,
            "\tld hl,S%d\n\tld a,(hl)\n\tld (ix-14),a\n"
            "\tld l,a\n\trlca\n\tsbc a,a\n\tld h,a\n\tpush hl\n",
            plan->strings[9]);
    mir_gnarly_push_string(out, plan, 10);
    mir_gnarly_print(out, plan, 6, 2);

    mir_gnarly_store_array_word(out, -10, 0, 10);
    mir_gnarly_store_array_word(out, -10, 1, 20);
    mir_gnarly_store_array_word(out, -10, 2, 30);
    mir_gnarly_store_array_word(out, -10, 3, 40);
    mir_gnarly_store_array_word(out, -10, 4, 50);
    mir_gnarly_load_array_word(out, -10, 2);
    fputs("\tld (ix-20),l\n\tld (ix-19),h\n\tpush hl\n",
          out);
    mir_gnarly_push_string(out, plan, 6);
    mir_gnarly_print(out, plan, 7, 2);

    mir_gnarly_indirect_call(out, plan->indirect_function);
    fprintf(out,
            "\tld hl,S%d\n\tld (ix-20),l\n\tld (ix-19),h\n"
            "\tpush hl\n",
            plan->strings[11]);
    mir_gnarly_push_string(out, plan, 12);
    mir_gnarly_print(out, plan, 8, 2);

    mir_gnarly_store_word(out, -20, 1);
    mir_gnarly_store_word(out, -18, 2);
    mir_gnarly_store_word(out, -20, 0);
    mir_gnarly_store_word(out, -18, 1);
    mir_gnarly_store_word(out, -16, 0);
    mir_gnarly_load_word(out, -18);
    fputs("\tpush hl\n", out);
    mir_gnarly_load_word(out, -20);
    fputs("\tpush hl\n", out);
    mir_gnarly_load_word(out, -16);
    fputs("\tpush hl\n", out);
    mir_gnarly_push_string(out, plan, 13);
    mir_gnarly_print(out, plan, 9, 4);

    mir_gnarly_store_word(out, -13, 6);
    mir_gnarly_store_word(out, -11, 3);
    mir_gnarly_push_word(out, 5);
    mir_gnarly_push_word(out, 7);
    mir_gnarly_push_word(out, 2);
    mir_gnarly_push_string(out, plan, 14);
    mir_gnarly_print(out, plan, 10, 4);

    mir_gnarly_load_array_word(out, -10, 3);
    fputs("\tpush hl\t\n\tpush hl\n", out);
    mir_gnarly_push_string(out, plan, 15);
    mir_gnarly_print(out, plan, 11, 3);

    mir_gnarly_store_word(out, -20, 0);
    mir_gnarly_store_word(out, -20, 1);
    mir_gnarly_load_array_word(out, -10, 3);
    fputs("\tpush hl\n", out);
    mir_gnarly_push_string(out, plan, 16);
    mir_gnarly_print(out, plan, 12, 2);

    mir_gnarly_store_word(out, -20, 1);
    mir_gnarly_store_word(out, -20, 3);
    mir_gnarly_store_array_word(out, -10, 3, 99);
    mir_gnarly_load_array_word(out, -10, 3);
    fputs("\tpush hl\n", out);
    mir_gnarly_load_word(out, -20);
    fputs("\tpush hl\n", out);
    mir_gnarly_push_string(out, plan, 17);
    mir_gnarly_print(out, plan, 13, 3);

    fprintf(out,
            "\tld hl,%s\n\tld (ix-12),l\n\tld (ix-11),h\n",
            asm_name_for(sym_asm_name(plan->indirect_function)));
    mir_gnarly_load_word(out, -12);
    mir_emit_runtime_call(out, "__call_hl");

    mir_gnarly_push_string(out, plan, 19);
    mir_gnarly_push_string(out, plan, 18);
    mir_gnarly_print(out, plan, 14, 2);

    mir_gnarly_store_word(out, -20, 5);
    mir_gnarly_load_word(out, -20);
    fputs("\tpush hl\n", out);
    mir_gnarly_push_word(out, 0);
    mir_gnarly_push_word(out, 2);
    mir_gnarly_push_string(out, plan, 20);
    mir_gnarly_print(out, plan, 15, 4);

    mir_gnarly_push_word(out, 0);
    mir_gnarly_push_word(out, 2);
    mir_gnarly_push_word(out, 65);
    mir_gnarly_push_string(out, plan, 21);
    mir_gnarly_print(out, plan, 16, 4);

    mir_gnarly_push_word(out, 8);
    mir_gnarly_push_word(out, 7);
    mir_machine_emit_symbol_call(out, plan->sum_function);
    mir_gnarly_cleanup(out, 2);
    fputs("\tpush hl\n", out);
    mir_gnarly_push_string(out, plan, 22);
    mir_gnarly_print(out, plan, 17, 2);

    mir_gnarly_push_word(out, 0);
    mir_gnarly_push_word(out, 2);
    mir_gnarly_push_word(out, 0);
    mir_gnarly_push_word(out, 10);
    mir_gnarly_push_string(out, plan, 23);
    mir_gnarly_print(out, plan, 18, 5);

    mir_gnarly_store_word(out, -20, 42);
    fputs("\tld (ix-18),120\n", out);
    mir_gnarly_ix_address(out, -17);
    fputs("\tpush hl\n", out);
    mir_gnarly_ix_address(out, -20);
    fputs("\tex de,hl\n\tpop hl\n\tld b,3\n", out);
    fprintf(out,
            "L%d:\n\tld a,(de)\n\tld (hl),a\n\tinc de\n"
            "\tinc hl\n\tdjnz L%d\n",
            copy_loop, copy_loop);
    fputs("\tld a,(ix-15)\n\tld l,a\n\trlca\n\tsbc a,a\n"
          "\tld h,a\n\tpush hl\n", out);
    mir_gnarly_load_word(out, -17);
    fputs("\tpush hl\n", out);
    mir_gnarly_push_string(out, plan, 0);
    mir_gnarly_print(out, plan, 19, 3);

    fputs("\tld (ix-20),255\n", out);
    mir_gnarly_store_word(out, -18, 1);
    fputs("\tld hl,0\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", conditional_false);
    fputs("\tld l,(ix-20)\n\tld a,l\n\trlca\n\tsbc a,a\n"
          "\tld h,a\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n",
            conditional_done, conditional_false);
    mir_gnarly_load_word(out, -18);
    fprintf(out, "L%d:\n\tpush hl\n", conditional_done);
    mir_gnarly_push_string(out, plan, 1);
    mir_gnarly_print(out, plan, 20, 2);

    mir_gnarly_ix_address(out, -10);
    fputs("\tld (ix-20),l\n\tld (ix-19),h\n", out);
    mir_gnarly_load_word(out, -20);
    fputs("\tld de,8\n\tadd hl,de\n\tld e,(hl)\n\tinc hl\n"
          "\tld d,(hl)\n\tex de,hl\n\tpush hl\n", out);
    mir_gnarly_push_string(out, plan, 2);
    mir_gnarly_print(out, plan, 21, 2);

    mir_gnarly_store_word(out, -18, 2);
    fprintf(out,
            "\tld hl,S%d\n\tld de,2\n\tadd hl,de\n"
            "\tld a,(hl)\n\tld (ix-14),a\n"
            "\tld l,a\n\trlca\n\tsbc a,a\n\tld h,a\n\tpush hl\n",
            plan->strings[24]);
    mir_gnarly_push_string(out, plan, 25);
    mir_gnarly_print(out, plan, 22, 2);

    fprintf(out,
            "\tld hl,S%d\n\tld a,(hl)\n\tld (ix-14),a\n"
            "\tld l,a\n\trlca\n\tsbc a,a\n\tld h,a\n\tpush hl\n",
            plan->strings[24]);
    mir_gnarly_push_string(out, plan, 26);
    mir_gnarly_print(out, plan, 23, 2);
    fprintf(out,
            "\tld hl,S%d\n\tld a,(hl)\n\tld (ix-14),a\n"
            "\tld l,a\n\trlca\n\tsbc a,a\n\tld h,a\n\tpush hl\n",
            plan->strings[24]);
    mir_gnarly_push_string(out, plan, 27);
    mir_gnarly_print(out, plan, 24, 2);

    mir_gnarly_store_word(out, -20, 5);
    mir_gnarly_store_word(out, -18, 5);
    mir_gnarly_store_word(out, -16, 5);
    mir_gnarly_load_word(out, -16);
    fputs("\tpush hl\n", out);
    mir_gnarly_load_word(out, -18);
    fputs("\tpush hl\n", out);
    mir_gnarly_load_word(out, -20);
    fputs("\tpush hl\n", out);
    mir_gnarly_push_string(out, plan, 28);
    mir_gnarly_print(out, plan, 25, 4);

    mir_gnarly_store_word(out, -16, 0);
    mir_gnarly_store_word(out, -20, 0);
    mir_gnarly_store_word(out, -18, 9);
    fprintf(out, "L%d:\n", comma_loop);
    mir_gnarly_load_word(out, -20);
    fputs("\tpush hl\n", out);
    mir_gnarly_load_word(out, -18);
    fputs("\tex de,hl\n\tpop hl\n\tld a,h\n\txor 80h\n"
          "\tld h,a\n\tld a,d\n\txor 80h\n\tld d,a\n"
          "\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp nc,L%d\n", comma_done);
    mir_gnarly_load_word(out, -20);
    fputs("\tpush hl\n", out);
    mir_gnarly_load_word(out, -18);
    fputs("\tex de,hl\n\tpop hl\n\tadd hl,de\n\tpush hl\n",
          out);
    mir_gnarly_load_word(out, -16);
    fputs("\tex de,hl\n\tpop hl\n\tadd hl,de\n"
          "\tld (ix-16),l\n\tld (ix-15),h\n", out);
    mir_gnarly_load_word(out, -20);
    fputs("\tinc hl\n\tld (ix-20),l\n\tld (ix-19),h\n", out);
    mir_gnarly_load_word(out, -18);
    fputs("\tdec hl\n\tld (ix-18),l\n\tld (ix-17),h\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n", comma_loop, comma_done);
    mir_gnarly_load_word(out, -16);
    fputs("\tpush hl\n", out);
    mir_gnarly_push_string(out, plan, 29);
    mir_gnarly_print(out, plan, 26, 2);

    mir_gnarly_push_word(out, 0);
    mir_gnarly_push_word(out, 6);
    mir_gnarly_push_string(out, plan, 30);
    mir_gnarly_print(out, plan, 27, 3);

    mir_gnarly_store_word(out, -20, 65533);
    mir_gnarly_load_word(out, -20);
    fputs("\tld a,h\n\tor a\n", out);
    fprintf(out, "\tjp m,L%d\n", ternary_negative);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n\tld hl,1\n\tjp L%d\n",
            ternary_zero, ternary_done);
    fprintf(out, "L%d:\n\tld hl,65535\n\tjp L%d\n",
            ternary_negative, ternary_done);
    fprintf(out, "L%d:\n\tld hl,0\nL%d:\n",
            ternary_zero, ternary_done);
    fputs("\tld (ix-16),l\n\tld (ix-15),h\n\tpush hl\n", out);
    mir_gnarly_push_string(out, plan, 31);
    mir_gnarly_print(out, plan, 28, 2);

    mir_gnarly_push_word(out, 65);
    mir_gnarly_push_word(out, 65);
    mir_gnarly_push_word(out, 127);
    mir_gnarly_push_word(out, 255);
    mir_gnarly_push_string(out, plan, 32);
    mir_gnarly_print(out, plan, 29, 5);

    fputs("\tld (ix-14),65\n\tld l,(ix-14)\n"
          "\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n"
          "\tld (ix-16),l\n\tld (ix-15),h\n\tpush hl\n", out);
    mir_gnarly_push_string(out, plan, 33);
    mir_gnarly_print(out, plan, 30, 2);

    mir_gnarly_store_word(out, -16, 65520);
    mir_gnarly_load_word(out, -16);
    fputs("\tpush hl\n", out);
    mir_gnarly_push_string(out, plan, 34);
    mir_gnarly_print(out, plan, 31, 2);

    fputs("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_nested_for_opcode_code(int opcode)
{
    if (opcode == MIR_FLOAT_CONST)
        return 'Q';
    return mir_gnarly_opcode_code(opcode);
}

static struct Sym *mir_nested_for_direct_function(
    int instruction, int argument_count, int is_void, int call_id)
{
    struct Sym *function =
        mir_memory_runner_call_function(
            instruction, 0, argument_count);
    const struct MirInsn *call = &mir.insns[instruction];

    if (function == NULL || !function->is_defined ||
        call->secondary_offset != call_id ||
        ((call->type & 15) == TYPE_VOID) != is_void ||
        (is_void ? type_ptr_depth(call->type) != 0 :
         !mir_gnarly_word_type(call->type, 0)))
        return NULL;
    return function;
}

static int mir_nested_for_call(
    int instruction, struct Sym *function,
    int argument_count, const int *definitions)
{
    int arguments[6];
    int argument;

    if (find_global(mir.insns[instruction].name) != function ||
        !mir_machine_call_arguments(
            &mir.insns[instruction], argument_count, arguments))
        return 0;
    for (argument = 0; argument < argument_count; ++argument)
        if (!mir_gnarly_value_from(
                arguments[argument], definitions[argument]))
            return 0;
    return 1;
}

static int mir_nested_for_print_call(
    struct MirNestedForRunner *plan, int slot,
    int instruction, int call_id, int argument_count,
    const int *definitions)
{
    const struct MirInsn *call = &mir.insns[instruction];
    struct Sym *function;
    int arguments[6];
    int argument;

    if (slot < 0 || slot >= 3 ||
        call->opcode != MIR_CALL || call->src1 >= 0 ||
        call->secondary_offset != call_id ||
        call->memory_flags != MIR_CALL_FLAG_VARIADIC ||
        call->base_name[0] == 0 ||
        !mir_gnarly_word_type(call->type, 0) ||
        !mir_machine_call_arguments(
            call, argument_count, arguments) ||
        (function = find_global(call->name)) == NULL ||
        function->storage != SC_FUNC || function->is_defined ||
        function->is_funcptr || function->is_noreturn ||
        !function->has_proto || function->proto_nargs != 1 ||
        !function->proto_variadic ||
        !mir_gnarly_string_type(function->proto_types[0]) ||
        !mir_gnarly_word_type(function->type, 0) ||
        (plan->print_function != NULL &&
         plan->print_function != function))
        return 0;
    for (argument = 0; argument < argument_count; ++argument)
        if (!mir_gnarly_value_from(
                arguments[argument], definitions[argument]))
            return 0;
    plan->print_function = function;
    snprintf(plan->print_names[slot],
             sizeof(plan->print_names[slot]), "%s",
             call->base_name);
    return 1;
}

static int mir_nested_for_local_address(
    int instruction, int *offset_out)
{
    const struct MirInsn *address = &mir.insns[instruction];
    int type;
    int storage;
    int offset;

    if (address->opcode != MIR_ADDRESS ||
        !mir_machine_named_nonvolatile(address) ||
        !mir_scalar_memory_location(
            address, &type, &storage, &offset) ||
        storage != SC_LOCAL)
        return 0;
    *offset_out = offset;
    return 1;
}

static struct Sym *mir_nested_for_global_array(
    int instruction, int length, int element_size)
{
    const struct MirInsn *address = &mir.insns[instruction];
    struct Sym *symbol;

    if (address->opcode != MIR_ADDRESS ||
        !mir_machine_named_nonvolatile(address) ||
        address->object >= 0 ||
        (symbol = find_global(address->name)) == NULL ||
        symbol->storage == SC_FUNC || !symbol->is_defined ||
        symbol->is_volatile || !symbol->is_array ||
        symbol->array_len != length ||
        symbol->elem_size != element_size)
        return NULL;
    return symbol;
}

static int mir_nested_for_member(
    int instruction, int base, int offset,
    int memory_size, int pointer_depth, int base_type)
{
    const struct MirInsn *member = &mir.insns[instruction];

    return member->opcode == MIR_MEMBER_ADDRESS &&
           mir_gnarly_value_from(member->src1, base) &&
           member->immediate == offset &&
           member->memory_size == memory_size &&
           (member->memory_flags & (1 | 8)) == 0 &&
           type_ptr_depth(member->type) == pointer_depth &&
           (member->type & 15) == base_type;
}

static int mir_nested_for_index(
    int instruction, int base, int index,
    int stride, int memory_size)
{
    const struct MirInsn *address = &mir.insns[instruction];

    return address->opcode == MIR_INDEX_ADDRESS &&
           mir_gnarly_value_from(address->src1, base) &&
           mir_gnarly_value_from(address->src2, index) &&
           address->immediate == stride &&
           address->memory_size == memory_size &&
           (address->memory_flags & (1 | 8)) == 0;
}

static int mir_nested_for_store_indirect(
    int instruction, int address, int value, int memory_size)
{
    const struct MirInsn *store = &mir.insns[instruction];

    return store->opcode == MIR_STORE_INDIRECT &&
           mir_gnarly_value_from(store->src1, address) &&
           mir_gnarly_value_from(store->src2, value) &&
           store->memory_size == memory_size &&
           (store->memory_flags & (1 | 8)) == 0;
}

static int mir_match_nested_for_runner(
    struct MirNestedForRunner *plan)
{
    static const char expected_opcodes[] =
        "LANGKCNSLPNCBFAMNINCBUWAMNINCBFQLJLQLLPWAMNINCBFTLJLCNLLPWNLNCBNSJLACITWCNSLPNCBFNCNSLNDCBFANIDI"
        "CWLDCBNSJLNLNCBNSJLACICICWCNSCNSCNSCNSCNSCNSLPPPPPNNCBFNCBANGNGKBNSNCBANGNGKBNSNCBANGNGKBNSNCBNG"
        "KBNSNLNCBNSJLCNSLPNNNNPNCBFNCBNGKBNSLNCBNSJLTGANGKGANGKGAMCIRUGKTGNGNGNGNGNGKTGCGCGCGKGCGCGKGKCE";
    static const int constant_instructions[34] = {
        5, 11, 19, 28, 45, 52, 61, 68, 72, 78, 82, 88,
        96, 100, 109, 116, 118, 120, 122, 125, 128, 131,
        134, 137, 148, 152, 164, 176, 188, 199, 205, 216,
        220, 230
    };
    static const long constant_values[34] = {
        0, 4, 2, 1, 3, 0, 1, 2, 0, 3, 0, 4,
        1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
        0, 0, 4, 10, 10, 10, 10, 1, 0, 3,
        10, 1
    };
    static const int tail_constant_instructions[6] = {
        250, 271, 273, 275, 279, 281
    };
    static const long tail_constant_values[6] = {
        79, 0, 10, 2, 20, 3
    };
    static const int binary_instructions[23] = {
        12, 20, 29, 46, 62, 79, 89, 101, 110, 149, 153,
        160, 165, 172, 177, 184, 189, 193, 200, 217, 221,
        225, 231
    };
    static const int binary_lefts[23] = {
        9, 9, 9, 9, 9, 76, 87, 99, 76, 141, 142,
        153, 143, 165, 144, 177, 145, 189, 141, 209, 214,
        221, 209
    };
    static const int binary_rights[23] = {
        11, 19, 28, 45, 61, 78, 88, 100, 109, 148, 152,
        159, 164, 171, 176, 183, 188, 192, 199, 216, 220,
        224, 230
    };
    static const int binary_operations[23] = {
        '<', TOK_EQ, TOK_EQ, TOK_EQ, '+', '<', '<', '+', '+',
        '<', '*', '+', '*', '+', '*', '+', '*', '+', '+',
        '<', '*', '+', '+'
    };
    static const int string_instructions[5] = {
        48, 70, 236, 256, 269
    };
    static const int first_print_arguments[4] = {
        236, 241, 246, 253
    };
    static const int second_print_arguments[6] = {
        256, 142, 143, 144, 145, 214
    };
    static const int third_print_arguments[3] = {
        269, 277, 283
    };
    static const int direct_call_instructions[10] = {
        4, 159, 171, 183, 192, 224, 241, 246, 277, 283
    };
    static const int direct_call_ids[10] = {
        0, 1, 2, 3, 4, 5, 7, 8, 11, 12
    };
    static const int direct_call_counts[10] = {
        1, 2, 2, 2, 1, 1, 1, 1, 3, 2
    };
    static const int direct_call_arguments[10][3] = {
        {1, -1, -1}, {154, 141, -1}, {166, 141, -1},
        {178, 141, -1}, {141, -1, -1}, {209, -1, -1},
        {238, -1, -1}, {243, -1, -1}, {271, 273, 275},
        {279, 281, -1}
    };
    struct Sym *direct_functions[10];
    int s_offset;
    int b_offset;
    int call_count = 0;
    int instruction;
    int item;
    int previous;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 288 || mir_cfg_block_count() != 24 ||
        mir.has_vla || mir.local_bytes != 135 ||
        mir.object_count != 6 ||
        !mir_gnarly_word_type(mir.return_type, 0) ||
        strlen(expected_opcodes) != (size_t)mir.count)
        return mir_machine_reject(
            "nested-for-call-runner", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        if (mir_nested_for_opcode_code(
                mir.insns[instruction].opcode) !=
                expected_opcodes[instruction])
            return mir_machine_reject(
                "nested-for-call-runner", "opcode");
        if (mir.insns[instruction].opcode == MIR_CALL)
            ++call_count;
    }
    if (call_count != 13)
        return mir_machine_reject(
            "nested-for-call-runner", "call-count");
    for (item = 0; item < 34; ++item)
        if (!mir_machine_constant_equals(
                mir.insns[constant_instructions[item]].dst,
                constant_values[item]))
            return mir_machine_reject(
                "nested-for-call-runner", "constants");
    for (item = 0; item < 6; ++item)
        if (!mir_machine_constant_equals(
                mir.insns[tail_constant_instructions[item]].dst,
                tail_constant_values[item]))
            return mir_machine_reject(
                "nested-for-call-runner", "tail-constants");
    if (!mir_machine_constant_equals(mir.insns[286].dst, 0) ||
        mir.insns[287].src1 != mir.insns[286].dst ||
        mir.insns[31].immediate != 1065353216L ||
        mir.insns[35].immediate != 0 ||
        !type_is_float(mir.insns[31].type) ||
        mir.insns[35].type != mir.insns[31].type ||
        type_size(mir.insns[31].type) != 4)
        return mir_machine_reject(
            "nested-for-call-runner", "typed-constants");
    for (item = 0; item < 23; ++item)
        if (!mir_gnarly_binary(
                binary_instructions[item],
                binary_lefts[item], binary_rights[item],
                binary_operations[item]) ||
            !mir_gnarly_word_type(
                mir.insns[binary_instructions[item]].type, 0))
            return mir_machine_reject(
                "nested-for-call-runner", "operations");

    if (!mir_gnarly_phi(9, 5, 62, 0, 59) ||
        !mir_gnarly_branch(13, 12, 66) ||
        mir.insns[65].label != mir.insns[8].label ||
        !mir_gnarly_branch(30, 29, 34) ||
        mir.insns[33].label != mir.insns[37].label ||
        !mir_gnarly_phi(38, 31, 35, 32, 36) ||
        !mir_gnarly_branch(47, 46, 51) ||
        mir.insns[50].label != mir.insns[55].label ||
        !mir_gnarly_phi(56, 48, 52, 49, 54) ||
        !mir_gnarly_phi(76, 72, 110, 66, 107) ||
        !mir_gnarly_branch(80, 79, 114) ||
        !mir_gnarly_branch(90, 89, 105) ||
        mir.insns[104].label != mir.insns[85].label ||
        mir.insns[113].label != mir.insns[75].label ||
        !mir_gnarly_phi(141, 137, 200, 114, 197) ||
        !mir_gnarly_phi(142, 122, 160, 114, 197) ||
        !mir_gnarly_phi(143, 125, 172, 114, 197) ||
        !mir_gnarly_phi(144, 128, 184, 114, 197) ||
        !mir_gnarly_phi(145, 131, 193, 114, 197) ||
        !mir_gnarly_branch(150, 149, 204) ||
        mir.insns[203].label != mir.insns[140].label ||
        !mir_gnarly_phi(209, 205, 231, 204, 228) ||
        !mir_gnarly_phi(214, 134, 225, 204, 228) ||
        !mir_gnarly_branch(218, 217, 235) ||
        mir.insns[234].label != mir.insns[208].label)
        return mir_machine_reject(
            "nested-for-call-runner", "control-flow");

    if (!mir_nested_for_local_address(1, &s_offset) ||
        !mir_nested_for_local_address(14, &b_offset))
        return mir_machine_reject(
            "nested-for-call-runner", "local-addresses");
    if (
        s_offset == b_offset ||
        s_offset < -mir.local_bytes || b_offset < -mir.local_bytes ||
        s_offset + 81 > 0 || b_offset + 40 > 0 ||
        !(s_offset + 81 <= b_offset ||
          b_offset + 40 <= s_offset))
        return mir_machine_reject(
            "nested-for-call-runner", "local-aggregates");
    {
        static const int s_addresses[4] = {1, 238, 243, 248};
        static const int b_addresses[4] = {14, 154, 166, 178};

        for (item = 1; item < 4; ++item) {
            int offset;

            if (!mir_nested_for_local_address(
                    s_addresses[item], &offset) ||
                offset != s_offset ||
                strcmp(mir.insns[s_addresses[item]].name,
                       mir.insns[s_addresses[0]].name) ||
                !mir_nested_for_local_address(
                    b_addresses[item], &offset) ||
                offset != b_offset ||
                strcmp(mir.insns[b_addresses[item]].name,
                       mir.insns[b_addresses[0]].name))
                return mir_machine_reject(
                    "nested-for-call-runner", "aggregate-aliases");
        }
        if (mir_machine_same_location(
                &mir.insns[s_addresses[0]],
                &mir.insns[b_addresses[0]]))
            return mir_machine_reject(
                "nested-for-call-runner", "aggregate-overlap");
    }
    if (!mir_nested_for_member(15, 14, 0, 16, 1, TYPE_LONG) ||
        !mir_nested_for_index(17, 15, 9, 4, 4) ||
        !mir_gnarly_binary(20, 9, 19, TOK_EQ) ||
        mir.insns[21].src1 != mir.insns[20].dst ||
        mir.insns[21].immediate != 0 ||
        (mir.insns[21].type & 15) != TYPE_LONG ||
        type_size(mir.insns[21].type) != 4 ||
        !mir_nested_for_store_indirect(22, 17, 21, 4) ||
        !mir_nested_for_member(24, 23, 16, 16, 1, TYPE_FLOAT) ||
        !mir_nested_for_index(26, 24, 9, 4, 4) ||
        !mir_nested_for_store_indirect(39, 26, 38, 4) ||
        !mir_nested_for_member(41, 40, 32, 8, 2, TYPE_CHAR) ||
        !mir_nested_for_index(43, 41, 9, 2, 2) ||
        !mir_nested_for_store_indirect(57, 43, 56, 2))
        return mir_machine_reject(
            "nested-for-call-runner", "bag-initialization");

    plan->pointer_array =
        mir_nested_for_global_array(67, 4, 2);
    plan->grid_array =
        mir_nested_for_global_array(91, 3, 8);
    if (plan->pointer_array == NULL || plan->grid_array == NULL ||
        plan->pointer_array == plan->grid_array ||
        find_global(mir.insns[115].name) != plan->grid_array ||
        !mir_nested_for_index(69, 67, 68, 2, 2) ||
        !mir_nested_for_store_indirect(71, 69, 70, 2) ||
        !mir_nested_for_index(93, 91, 76, 8, 8) ||
        !mir_nested_for_index(95, 93, 94, 2, 2) ||
        !mir_nested_for_store_indirect(97, 95, 96, 2) ||
        !mir_nested_for_index(117, 115, 116, 8, 8) ||
        !mir_nested_for_index(119, 117, 118, 2, 2) ||
        !mir_nested_for_store_indirect(121, 119, 120, 2))
        return mir_machine_reject(
            "nested-for-call-runner", "global-arrays");

    if (!mir_machine_unobservable_local_store(&mir.insns[7]) ||
        !mir_machine_same_location(
            &mir.insns[7], &mir.insns[9]) ||
        !mir_machine_same_location(
            &mir.insns[7], &mir.insns[64]) ||
        !mir_machine_same_location(
            &mir.insns[7], &mir.insns[74]) ||
        !mir_machine_same_location(
            &mir.insns[7], &mir.insns[76]) ||
        !mir_machine_same_location(
            &mir.insns[7], &mir.insns[112]) ||
        !mir_machine_same_location(
            &mir.insns[7], &mir.insns[139]) ||
        !mir_machine_same_location(
            &mir.insns[7], &mir.insns[141]) ||
        !mir_machine_same_location(
            &mir.insns[7], &mir.insns[202]) ||
        !mir_machine_same_location(
            &mir.insns[7], &mir.insns[207]) ||
        !mir_machine_same_location(
            &mir.insns[7], &mir.insns[209]) ||
        !mir_machine_same_location(
            &mir.insns[7], &mir.insns[233]))
        return mir_machine_reject(
            "nested-for-call-runner", "loop-index");
    if (!mir_machine_unobservable_local_store(&mir.insns[84]) ||
        !mir_machine_same_location(
            &mir.insns[84], &mir.insns[87]) ||
        !mir_machine_same_location(
            &mir.insns[84], &mir.insns[94]) ||
        !mir_machine_same_location(
            &mir.insns[84], &mir.insns[99]) ||
        !mir_machine_same_location(
            &mir.insns[84], &mir.insns[103]) ||
        mir_machine_same_location(
            &mir.insns[84], &mir.insns[7]))
        return mir_machine_reject(
            "nested-for-call-runner", "nested-index");
    {
        static const int initial_stores[5] = {
            124, 127, 130, 133, 136
        };
        static const int phis[5] = {
            142, 143, 144, 145, 214
        };
        static const int update_stores[5] = {
            162, 174, 186, 195, 227
        };

        for (item = 0; item < 5; ++item) {
            if (!mir_machine_unobservable_local_store(
                    &mir.insns[initial_stores[item]]) ||
                !mir_machine_same_location(
                    &mir.insns[initial_stores[item]],
                    &mir.insns[phis[item]]) ||
                !mir_machine_same_location(
                    &mir.insns[initial_stores[item]],
                    &mir.insns[update_stores[item]]))
                return mir_machine_reject(
                    "nested-for-call-runner", "mask-locals");
            for (previous = 0; previous < item; ++previous)
                if (mir_machine_same_location(
                        &mir.insns[initial_stores[item]],
                        &mir.insns[initial_stores[previous]]))
                    return mir_machine_reject(
                        "nested-for-call-runner", "mask-alias");
        }
    }

    if (!mir_nested_for_member(249, 248, 0, 81, 1, TYPE_CHAR) ||
        (mir.insns[249].type & TYPE_UNSIGNED) == 0 ||
        !mir_nested_for_index(251, 249, 250, 1, 1) ||
        mir.insns[252].src1 != mir.insns[251].dst ||
        mir.insns[252].memory_size != 1 ||
        (mir.insns[252].type & TYPE_UNSIGNED) == 0 ||
        mir.insns[253].src1 != mir.insns[252].dst ||
        mir.insns[253].immediate != '!' ||
        !mir_gnarly_word_type(mir.insns[253].type, 0))
        return mir_machine_reject(
            "nested-for-call-runner", "sieve-check");

    for (item = 0; item < 5; ++item) {
        const struct MirInsn *string =
            &mir.insns[string_instructions[item]];

        if (!mir_gnarly_string_type(string->type) ||
            string->immediate < 0)
            return mir_machine_reject(
                "nested-for-call-runner", "strings");
        plan->strings[item] = (int)string->immediate;
        for (previous = 0; previous < item; ++previous)
            if (plan->strings[item] == plan->strings[previous])
                return mir_machine_reject(
                    "nested-for-call-runner", "distinct-strings");
    }

    for (item = 0; item < 10; ++item) {
        int is_void = item == 0;

        direct_functions[item] =
            mir_nested_for_direct_function(
                direct_call_instructions[item],
                direct_call_counts[item], is_void,
                direct_call_ids[item]);
        if (direct_functions[item] == NULL ||
            !mir_nested_for_call(
                direct_call_instructions[item],
                direct_functions[item],
                direct_call_counts[item],
                direct_call_arguments[item]))
            return mir_machine_reject(
                "nested-for-call-runner", "direct-calls");
        for (previous = 0; previous < item; ++previous)
            if (direct_functions[item] == direct_functions[previous])
                return mir_machine_reject(
                    "nested-for-call-runner", "distinct-functions");
    }
    plan->build_function = direct_functions[0];
    for (item = 0; item < 5; ++item)
        plan->check_functions[item] = direct_functions[item + 1];
    plan->count_functions[0] = direct_functions[6];
    plan->count_functions[1] = direct_functions[7];
    plan->stride_functions[0] = direct_functions[8];
    plan->stride_functions[1] = direct_functions[9];

    if (!mir_nested_for_print_call(
            plan, 0, 255, 6, 4, first_print_arguments) ||
        !mir_nested_for_print_call(
            plan, 1, 268, 9, 6, second_print_arguments) ||
        !mir_nested_for_print_call(
            plan, 2, 285, 10, 3, third_print_arguments))
        return mir_machine_reject(
            "nested-for-call-runner", "print-calls");
    for (item = 0; item < 10; ++item)
        if (direct_functions[item] == plan->print_function)
            return mir_machine_reject(
                "nested-for-call-runner", "print-alias");
    return 1;
}

enum MirNestedForFrame {
    MIR_NESTED_SIEVE = -132,
    MIR_NESTED_BAG = -51,
    MIR_NESTED_LMASK = -11,
    MIR_NESTED_FMASK = -9,
    MIR_NESTED_PMASK = -7,
    MIR_NESTED_GMASK = -5,
    MIR_NESTED_RMASK = -3,
    MIR_NESTED_INDEX = -1
};

static void mir_nested_for_ix_index_address(
    FILE *out, int base_offset, int scale)
{
    fputs("\tld a,(ix-1)\n", out);
    while (scale > 1) {
        fputs("\tadd a,a\n", out);
        scale /= 2;
    }
    fprintf(out,
            "\tld l,a\n\tld h,0\n\tld de,%d\n\tadd hl,de\n"
            "\tpush ix\n\tpop de\n\tadd hl,de\n",
            base_offset);
}

static void mir_nested_for_cleanup(FILE *out, int words)
{
    while (words-- > 0)
        fputs("\tpop bc\n", out);
}

static void mir_nested_for_mask_call(
    FILE *out, struct Sym *function,
    int mask_offset, int bag_argument)
{
    mir_gnarly_load_word(out, mask_offset);
    fputs("\tadd hl,hl\n\tld d,h\n\tld e,l\n"
          "\tadd hl,hl\n\tadd hl,hl\n\tadd hl,de\n"
          "\tpush hl\n"
          "\tld l,(ix-1)\n\tld h,0\n\tpush hl\n", out);
    if (bag_argument) {
        mir_gnarly_ix_address(out, MIR_NESTED_BAG);
        fputs("\tpush hl\n", out);
    }
    mir_machine_emit_symbol_call(out, function);
    mir_nested_for_cleanup(out, bag_argument ? 2 : 1);
    fputs("\tpop de\n\tadd hl,de\n", out);
    fprintf(out,
            "\tld (ix%+d),l\n\tld (ix%+d),h\n",
            mask_offset, mask_offset + 1);
}

static void mir_nested_for_print(
    FILE *out, const struct MirNestedForRunner *plan,
    int slot, int argument_count)
{
    mir_emit_runtime_call(out, plan->print_names[slot]);
    mir_nested_for_cleanup(out, argument_count);
}

static void mir_emit_nested_for_runner(
    FILE *out, const struct MirNestedForRunner *plan)
{
    int bag_loop = new_label();
    int long_zero = new_label();
    int float_zero = new_label();
    int float_done = new_label();
    int pointer_zero = new_label();
    int pointer_done = new_label();
    int grid_outer = new_label();
    int grid_inner = new_label();
    int mask_loop = new_label();
    int row_loop = new_label();
    int sieve_nonzero = new_label();
    int sieve_done = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-132\n\tadd hl,sp\n\tld sp,hl\n", out);

    mir_gnarly_ix_address(out, MIR_NESTED_SIEVE);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->build_function);
    mir_nested_for_cleanup(out, 1);

    fputs("\tld (ix-1),0\n", out);
    fprintf(out, "L%d:\n", bag_loop);
    mir_nested_for_ix_index_address(out, MIR_NESTED_BAG, 4);
    fputs("\tld a,(ix-1)\n\tcp 2\n\tld a,0\n", out);
    fprintf(out, "\tjp nz,L%d\n\tinc a\nL%d:\n",
            long_zero, long_zero);
    fputs("\tld (hl),a\n\txor a\n\tinc hl\n\tld (hl),a\n"
          "\tinc hl\n\tld (hl),a\n\tinc hl\n\tld (hl),a\n",
          out);

    mir_nested_for_ix_index_address(out, MIR_NESTED_BAG + 16, 4);
    fputs("\tld a,(ix-1)\n\tcp 1\n", out);
    fprintf(out, "\tjp nz,L%d\n", float_zero);
    fputs("\txor a\n\tld (hl),a\n\tinc hl\n\tld (hl),a\n"
          "\tinc hl\n\tld (hl),80h\n\tinc hl\n\tld (hl),3fh\n",
          out);
    fprintf(out, "\tjp L%d\nL%d:\n", float_done, float_zero);
    fputs("\txor a\n\tld (hl),a\n\tinc hl\n\tld (hl),a\n"
          "\tinc hl\n\tld (hl),a\n\tinc hl\n\tld (hl),a\n",
          out);
    fprintf(out, "L%d:\n", float_done);

    mir_nested_for_ix_index_address(out, MIR_NESTED_BAG + 32, 2);
    fputs("\tld a,(ix-1)\n\tcp 3\n\tld de,0\n", out);
    fprintf(out, "\tjp nz,L%d\n\tld de,S%d\nL%d:\n",
            pointer_zero, plan->strings[0], pointer_zero);
    fputs("\tld (hl),e\n\tinc hl\n\tld (hl),d\n", out);
    fprintf(out, "L%d:\n", pointer_done);
    fputs("\tinc (ix-1)\n\tld a,(ix-1)\n\tcp 4\n", out);
    fprintf(out, "\tjp c,L%d\n", bag_loop);

    fprintf(out, "\tld hl,S%d\n\tld (%s+4),hl\n",
            plan->strings[1],
            asm_name_for(sym_asm_name(plan->pointer_array)));

    fprintf(out, "\tld hl,%s\n\tld b,3\nL%d:\n\tld c,4\nL%d:\n",
            asm_name_for(sym_asm_name(plan->grid_array)),
            grid_outer, grid_inner);
    fputs("\tld (hl),1\n\tinc hl\n\tld (hl),0\n\tinc hl\n"
          "\tdec c\n", out);
    fprintf(out, "\tjp nz,L%d\n\tdjnz L%d\n",
            grid_inner, grid_outer);
    fprintf(out, "\txor a\n\tld (%s+8),a\n\tld (%s+9),a\n",
            asm_name_for(sym_asm_name(plan->grid_array)),
            asm_name_for(sym_asm_name(plan->grid_array)));

    mir_gnarly_store_word(out, MIR_NESTED_LMASK, 0);
    mir_gnarly_store_word(out, MIR_NESTED_FMASK, 0);
    mir_gnarly_store_word(out, MIR_NESTED_PMASK, 0);
    mir_gnarly_store_word(out, MIR_NESTED_GMASK, 0);
    mir_gnarly_store_word(out, MIR_NESTED_RMASK, 0);
    fputs("\tld (ix-1),0\n", out);
    fprintf(out, "L%d:\n", mask_loop);
    mir_nested_for_mask_call(
        out, plan->check_functions[0], MIR_NESTED_LMASK, 1);
    mir_nested_for_mask_call(
        out, plan->check_functions[1], MIR_NESTED_FMASK, 1);
    mir_nested_for_mask_call(
        out, plan->check_functions[2], MIR_NESTED_PMASK, 1);
    mir_nested_for_mask_call(
        out, plan->check_functions[3], MIR_NESTED_GMASK, 0);
    fputs("\tinc (ix-1)\n\tld a,(ix-1)\n\tcp 4\n", out);
    fprintf(out, "\tjp c,L%d\n", mask_loop);

    fputs("\tld (ix-1),0\n", out);
    fprintf(out, "L%d:\n", row_loop);
    mir_nested_for_mask_call(
        out, plan->check_functions[4], MIR_NESTED_RMASK, 0);
    fputs("\tinc (ix-1)\n\tld a,(ix-1)\n\tcp 3\n", out);
    fprintf(out, "\tjp c,L%d\n", row_loop);

    mir_gnarly_ix_address(out, MIR_NESTED_SIEVE);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->count_functions[0]);
    mir_nested_for_cleanup(out, 1);
    fputs("\tld (ix-51),l\n\tld (ix-50),h\n", out);
    mir_gnarly_ix_address(out, MIR_NESTED_SIEVE);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->count_functions[1]);
    mir_nested_for_cleanup(out, 1);
    fputs("\tld (ix-49),l\n\tld (ix-48),h\n"
          "\tld a,(ix-53)\n\tor a\n\tld hl,0\n", out);
    fprintf(out, "\tjp nz,L%d\n\tinc hl\nL%d:\n",
            sieve_nonzero, sieve_nonzero);
    fputs("\tpush hl\n", out);
    mir_gnarly_load_word(out, -49);
    fputs("\tpush hl\n", out);
    mir_gnarly_load_word(out, -51);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[2]);
    mir_nested_for_print(out, plan, 0, 4);
    fprintf(out, "L%d:\n", sieve_done);

    mir_gnarly_load_word(out, MIR_NESTED_RMASK);
    fputs("\tpush hl\n", out);
    mir_gnarly_load_word(out, MIR_NESTED_GMASK);
    fputs("\tpush hl\n", out);
    mir_gnarly_load_word(out, MIR_NESTED_PMASK);
    fputs("\tpush hl\n", out);
    mir_gnarly_load_word(out, MIR_NESTED_FMASK);
    fputs("\tpush hl\n", out);
    mir_gnarly_load_word(out, MIR_NESTED_LMASK);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[3]);
    mir_nested_for_print(out, plan, 1, 6);

    fputs("\tld hl,2\n\tpush hl\n\tld hl,10\n\tpush hl\n"
          "\tld hl,0\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->stride_functions[0]);
    mir_nested_for_cleanup(out, 3);
    fputs("\tld (ix-51),l\n\tld (ix-50),h\n"
          "\tld hl,3\n\tpush hl\n\tld hl,20\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->stride_functions[1]);
    mir_nested_for_cleanup(out, 2);
    fputs("\tpush hl\n", out);
    mir_gnarly_load_word(out, -51);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[4]);
    mir_nested_for_print(out, plan, 2, 3);

    fputs("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_wide_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
           (type & 15) == TYPE_INT &&
           (type & TYPE_UNSIGNED) != 0 &&
           type_size(type) == 2;
}

static struct Sym *mir_wide_direct_function(
    int instruction, int argument_count,
    int return_type, int return_pointer_depth)
{
    const struct MirInsn *call = &mir.insns[instruction];
    struct Sym *function;
    const char *assembly_name;

    if (call->opcode != MIR_CALL || call->src1 >= 0 ||
        call->memory_flags != 0 ||
        (function = find_global(call->name)) == NULL ||
        function->storage != SC_FUNC || function->is_funcptr ||
        (call->type & 15) != return_type ||
        type_ptr_depth(call->type) != return_pointer_depth)
        return NULL;
    assembly_name = asm_name_for(sym_asm_name(function));
    if (call->base_name[0] != 0 &&
        strcmp(call->base_name, assembly_name))
        return NULL;
    if (argument_count == 0) {
        int item;

        for (item = 0; item < mir.count; ++item)
            if (mir.insns[item].opcode == MIR_ARG &&
                mir.insns[item].secondary_offset ==
                    call->secondary_offset)
                return NULL;
    }
    return function;
}

static int mir_wide_call_arguments(
    int instruction, int argument_count, const int *definitions)
{
    int arguments[6];
    int argument;

    if (!mir_machine_call_arguments(
            &mir.insns[instruction], argument_count, arguments))
        return 0;
    for (argument = 0; argument < argument_count; ++argument)
        if (!mir_gnarly_value_from(
                arguments[argument], definitions[argument]))
            return 0;
    return 1;
}

static int mir_wide_print_call(
    struct MirWideStringRunner *plan, int slot,
    int instruction, int argument_count, const int *definitions)
{
    const struct MirInsn *call = &mir.insns[instruction];
    struct Sym *function;

    if (slot < 0 || slot >= 13 ||
        call->opcode != MIR_CALL || call->src1 >= 0 ||
        call->memory_flags != MIR_CALL_FLAG_VARIADIC ||
        call->base_name[0] == 0 ||
        !mir_gnarly_word_type(call->type, 0) ||
        !mir_wide_call_arguments(
            instruction, argument_count, definitions) ||
        (function = find_global(call->name)) == NULL ||
        function->storage != SC_FUNC || function->is_defined ||
        function->is_funcptr || function->is_noreturn ||
        !function->has_proto || function->proto_nargs != 1 ||
        !function->proto_variadic ||
        !mir_gnarly_string_type(function->proto_types[0]) ||
        !mir_gnarly_word_type(function->type, 0) ||
        (plan->print_function != NULL &&
         plan->print_function != function))
        return 0;
    plan->print_function = function;
    snprintf(plan->print_names[slot],
             sizeof(plan->print_names[slot]), "%s",
             call->base_name);
    return 1;
}

static struct Sym *mir_wide_global_buffer(int instruction)
{
    const struct MirInsn *address = &mir.insns[instruction];
    struct Sym *symbol;

    if (address->opcode != MIR_ADDRESS ||
        !mir_machine_named_nonvolatile(address) ||
        address->object >= 0 ||
        !mir_wide_pointer_type(address->type) ||
        (symbol = find_global(address->name)) == NULL ||
        symbol->storage == SC_FUNC || !symbol->is_defined ||
        symbol->is_volatile || !symbol->is_array ||
        symbol->array_len != 4096 || symbol->elem_size != 2)
        return NULL;
    return symbol;
}

static int mir_wide_index(
    int instruction, int base, int index)
{
    const struct MirInsn *address = &mir.insns[instruction];

    return address->opcode == MIR_INDEX_ADDRESS &&
           mir_gnarly_value_from(address->src1, base) &&
           mir_gnarly_value_from(address->src2, index) &&
           address->immediate == 2 &&
           address->memory_size == 2 &&
           (address->memory_flags & (1 | 8)) == 0 &&
           mir_wide_pointer_type(address->type);
}

static int mir_wide_store_indirect(
    int instruction, int address, int value)
{
    const struct MirInsn *store = &mir.insns[instruction];

    return store->opcode == MIR_STORE_INDIRECT &&
           mir_gnarly_value_from(store->src1, address) &&
           mir_gnarly_value_from(store->src2, value) &&
           store->memory_size == 2 &&
           (store->memory_flags & (1 | 8)) == 0;
}

static int mir_match_wide_string_runner(
    struct MirWideStringRunner *plan)
{
    static const char expected_opcodes[] =
        "LNNNCSLPNCCBBFANICNCBBNWLNCBSJLTGKNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNCSLNPNNNNNNCBFNKNCNBNSNCNB"
        "KNCNBNBNSNNNBSNANIRSANICNWNANCBBGKNSNNBFTGNGNGNGNGNGKCGKNLANINWNLNCBSJLTGKNNNNNNNNNNNNNNNNNNNNNNNNNN"
        "NNNNNNNNNNNNNNNCSLNNPPPPNPNCBFNKNCNBNSNCNBKNCNBNBNSNNNBSNANIRSANICNWNANCBBGCGKSDUFTGNGNGNGNGKCGKNLDA"
        "NCBBBFTGNGNGNGNGKCGKNLANCBBGCGKNSDUFTGNGNGNGNGKCGKNLDANCBBBFTGNGNGNGNGKCGKNLANINWNLNCBSJLTGKAGTNGKNN"
        "NNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNCSLNNPNPNNNPNNCBFNKNCNBNSNKNCNBNSNCKNCNBNBNBNSNNBCBFTGNGN"
        "GKCGKNLNANCBBSNANCBBGDGKSDUFTGNGNGNGNGDGKCGKNLDGDGNCBNGKFTGNGNGNGNGDGKCGKNLNLNCBSJLTGKNNNNNNNNNNNNNN"
        "NNNNNNNNNNNNNNNNNNNNNNCSLNNPPPPNNNNPNNCBFNNCBCBSNCNCBCBBSNNNBSNANIRUSANICNWNANCBBGKNSTGNGNGANCBBGKAN"
        "INWNLNCBSJL";
    static const int constant_instructions[57] = {
        4, 9, 10, 17, 19, 26, 74, 85, 91, 97, 102, 123,
        129, 153, 166, 215, 227, 233, 239, 244, 265, 271, 275,
        293, 301, 317, 324, 328, 347, 355, 371, 384, 443, 457,
        463, 471, 477, 480, 492, 502, 510, 517, 541, 551, 570,
        578, 622, 638, 643, 645, 649, 651, 653, 672, 678, 693,
        706
    };
    static const long constant_values[57] = {
        0, 8192, 2, 97, 26, 1, 0, 1000, 300, 1, 3000, 0,
        2, 1, 1, 0, 1000, 300, 1, 70, 33, 2, 33, 1, 2, 1,
        2, 33, 1, 2, 1, 1, 0, 1000, 300, 26, 1, 26, 26, 1,
        2, 2, 1, 2, 1, 1, 0, 20, 37, 300, 1, 17, 70, 0, 2,
        2, 1
    };
    static const int binary_instructions[58] = {
        11, 12, 20, 21, 27, 86, 93, 99, 104, 106, 112, 130,
        131, 138, 167, 228, 235, 241, 246, 248, 254, 272, 273,
        302, 303, 304, 325, 326, 356, 357, 358, 385, 458, 465,
        473, 482, 484, 486, 491, 493, 511, 512, 518, 519, 552,
        579, 639, 644, 646, 652, 654, 655, 660, 679, 680, 694,
        695, 707
    };
    static const int binary_lefts[58] = {
        9, 7, 7, 17, 7, 78, 89, 97, 100, 99, 106, 93, 127,
        112, 78, 225, 231, 239, 242, 241, 248, 235, 269, 248,
        299, 298, 235, 322, 248, 353, 352, 225, 454, 461, 469,
        480, 478, 477, 473, 491, 473, 508, 465, 515, 486, 454,
        635, 635, 644, 635, 652, 649, 646, 646, 676, 646, 691,
        635
    };
    static const int binary_rights[58] = {
        10, 11, 19, 20, 26, 85, 91, 93, 102, 104, 93, 129,
        130, 133, 166, 227, 233, 235, 244, 246, 235, 271, 272,
        301, 302, 303, 324, 325, 355, 356, 357, 384, 457, 463,
        471, 473, 482, 484, 486, 492, 510, 511, 517, 518, 551,
        578, 638, 643, 645, 651, 653, 654, 655, 678, 679, 693,
        694, 706
    };
    static const int binary_operations[58] = {
        '/', '<', '%', '+', '+', '<', '%', '+', '%', '+', '-',
        '*', '+', TOK_NE, '+', '<', '%', '+', '%', '+', '-', '*',
        '+', '*', '+', TOK_NE, '*', '+', '*', '+', TOK_NE, '+',
        '<', '%', '%', '-', '%', '+', '+', '>', '*', '+', '*', '+',
        '*', '+', '<', '*', '%', '*', '%', '+', '+', '*', '+', '*',
        '+', '+'
    };
    static const int string_instructions[14] = {
        31, 140, 171, 282, 306, 336, 360,
        389, 394, 495, 528, 557, 583, 685
    };
    static const int print_calls[13] = {
        33, 152, 173, 292, 316, 346, 370,
        391, 501, 540, 569, 585, 697
    };
    static const unsigned char print_argument_counts[13] = {
        1, 6, 1, 5, 5, 5, 5, 1, 3, 6, 6, 1, 4
    };
    static const int print_arguments[13][6] = {
        {31, -1, -1, -1, -1, -1},
        {140, 78, 112, 133, 93, 106},
        {171, -1, -1, -1, -1, -1},
        {282, 225, 254, 235, 248, -1},
        {306, 225, 254, 235, 248, -1},
        {336, 225, 254, 235, 248, -1},
        {360, 225, 254, 235, 248, -1},
        {389, -1, -1, -1, -1, -1},
        {495, 473, 486, -1, -1, -1},
        {528, 454, 465, 473, 486, 538},
        {557, 454, 465, 473, 486, 567},
        {583, -1, -1, -1, -1, -1},
        {685, 655, 682, 695, -1, -1}
    };
    static const int buffer_addresses[18] = {
        14, 115, 120, 127, 158, 257, 262, 269,
        299, 322, 353, 376, 515, 663, 669, 676, 691, 698
    };
    static const int exit_calls[8] = {
        155, 295, 319, 349, 373, 504, 543, 572
    };
    static const int exit_arguments[8] = {
        153, 293, 317, 347, 371, 502, 541, 570
    };
    static const int random_calls[7] = {
        89, 100, 231, 242, 461, 469, 478
    };
    static const int local_groups[][5] = {
        {5, 28, 7, -1, -1},
        {75, 168, 78, -1, -1},
        {95, 237, 467, 647, 220},
        {108, 250, 661, 221, 628},
        {113, 255, 488, 656, 222},
        {119, 261, 668, 223, 630},
        {216, 386, 225, -1, -1},
        {444, 580, 454, -1, -1},
        {623, 708, 635, -1, -1}
    };
    struct Sym *buffer;
    struct Sym *function;
    int alpha_offset;
    int call_count = 0;
    int instruction;
    int group;
    int item;
    int previous;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 711 || mir_cfg_block_count() != 24 ||
        mir.has_vla || mir.local_bytes != 105 ||
        mir.object_count != 12 ||
        (mir.return_type & 15) != TYPE_VOID ||
        type_ptr_depth(mir.return_type) != 0 ||
        strlen(expected_opcodes) != (size_t)mir.count)
        return mir_machine_reject(
            "wide-string-call-runner", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        if (mir_gnarly_opcode_code(
                mir.insns[instruction].opcode) !=
                expected_opcodes[instruction])
            return mir_machine_reject(
                "wide-string-call-runner", "opcode");
        if (mir.insns[instruction].opcode == MIR_CALL)
            ++call_count;
    }
    if (call_count != 35)
        return mir_machine_reject(
            "wide-string-call-runner", "call-count");
    for (item = 0; item < 57; ++item)
        if (!mir_machine_constant_equals(
                mir.insns[constant_instructions[item]].dst,
                constant_values[item]))
            return mir_machine_reject(
                "wide-string-call-runner", "constants");
    for (item = 0; item < 58; ++item)
        if (!mir_gnarly_binary(
                binary_instructions[item],
                binary_lefts[item], binary_rights[item],
                binary_operations[item]))
            return mir_machine_reject(
                "wide-string-call-runner", "operations");

    if (!mir_gnarly_phi(7, 4, 27, 0, 24) ||
        !mir_gnarly_phi(78, 74, 167, 30, 164) ||
        !mir_gnarly_phi(220, 93, 235, 170, 382) ||
        !mir_gnarly_phi(221, 106, 248, 170, 382) ||
        !mir_gnarly_phi(222, 112, 254, 170, 382) ||
        !mir_gnarly_phi(223, 118, 260, 170, 382) ||
        !mir_gnarly_phi(225, 215, 385, 170, 382) ||
        !mir_gnarly_phi(448, 220, 465, 388, 576) ||
        !mir_gnarly_phi(450, 222, 486, 388, 576) ||
        !mir_gnarly_phi(454, 443, 579, 388, 576) ||
        !mir_gnarly_phi(627, 448, 646, 582, 704) ||
        !mir_gnarly_phi(628, 221, 660, 582, 704) ||
        !mir_gnarly_phi(629, 450, 655, 582, 704) ||
        !mir_gnarly_phi(630, 223, 667, 582, 704) ||
        !mir_gnarly_phi(635, 622, 707, 582, 704) ||
        !mir_gnarly_branch(13, 12, 30) ||
        !mir_gnarly_branch(87, 86, 170) ||
        !mir_gnarly_branch(139, 138, 157) ||
        !mir_gnarly_branch(229, 228, 388) ||
        !mir_gnarly_branch(281, 280, 297) ||
        !mir_gnarly_branch(305, 304, 321) ||
        !mir_gnarly_branch(335, 334, 351) ||
        !mir_gnarly_branch(359, 358, 375) ||
        !mir_gnarly_branch(459, 458, 582) ||
        !mir_gnarly_branch(494, 493, 506) ||
        !mir_gnarly_branch(527, 526, 545) ||
        !mir_gnarly_branch(556, 555, 574) ||
        !mir_gnarly_branch(640, 639, 710) ||
        mir.insns[29].label != mir.insns[6].label ||
        mir.insns[169].label != mir.insns[76].label ||
        mir.insns[387].label != mir.insns[217].label ||
        mir.insns[581].label != mir.insns[445].label ||
        mir.insns[709].label != mir.insns[624].label)
        return mir_machine_reject(
            "wide-string-call-runner", "control-flow");

    for (item = 0; item < 14; ++item) {
        const struct MirInsn *string =
            &mir.insns[string_instructions[item]];

        if (!mir_gnarly_string_type(string->type) ||
            string->immediate < 0)
            return mir_machine_reject(
                "wide-string-call-runner", "strings");
        plan->strings[item] = (int)string->immediate;
        for (previous = 0; previous < item; ++previous)
            if (plan->strings[item] == plan->strings[previous])
                return mir_machine_reject(
                    "wide-string-call-runner",
                    "distinct-strings");
    }
    for (item = 0; item < 13; ++item)
        if (!mir_wide_print_call(
                plan, item, print_calls[item],
                print_argument_counts[item],
                print_arguments[item]))
            return mir_machine_reject(
                "wide-string-call-runner", "print-calls");

    for (item = 0; item < 18; ++item) {
        buffer = mir_wide_global_buffer(
            buffer_addresses[item]);
        if (buffer == NULL ||
            (plan->buffer != NULL && plan->buffer != buffer))
            return mir_machine_reject(
                "wide-string-call-runner", "buffer");
        plan->buffer = buffer;
    }
    if (!mir_wide_index(16, 14, 7) ||
        !mir_wide_index(117, 115, 106) ||
        !mir_wide_index(122, 120, 106) ||
        !mir_wide_index(160, 158, 106) ||
        !mir_wide_index(259, 257, 248) ||
        !mir_wide_index(264, 262, 248) ||
        !mir_wide_index(378, 376, 248) ||
        !mir_wide_index(665, 663, 660) ||
        !mir_wide_index(671, 669, 660) ||
        !mir_wide_index(700, 698, 660) ||
        !mir_wide_store_indirect(23, 16, 21) ||
        !mir_wide_store_indirect(125, 122, 123) ||
        !mir_wide_store_indirect(162, 160, 118) ||
        !mir_wide_store_indirect(267, 264, 265) ||
        !mir_wide_store_indirect(380, 378, 260) ||
        !mir_wide_store_indirect(674, 671, 672) ||
        !mir_wide_store_indirect(702, 700, 667) ||
        mir.insns[118].opcode != MIR_LOAD_INDIRECT ||
        !mir_gnarly_value_from(mir.insns[118].src1, 117) ||
        mir.insns[118].memory_size != 2 ||
        !mir_gnarly_word_type(mir.insns[118].type, 1) ||
        mir.insns[260].opcode != MIR_LOAD_INDIRECT ||
        !mir_gnarly_value_from(mir.insns[260].src1, 259) ||
        mir.insns[260].memory_size != 2 ||
        !mir_gnarly_word_type(mir.insns[260].type, 1) ||
        mir.insns[666].opcode != MIR_LOAD_INDIRECT ||
        !mir_gnarly_value_from(mir.insns[666].src1, 665) ||
        mir.insns[666].memory_size != 2 ||
        !mir_gnarly_word_type(mir.insns[666].type, 1) ||
        mir.insns[667].opcode != MIR_UNARY ||
        !mir_gnarly_value_from(mir.insns[667].src1, 666) ||
        mir.insns[667].immediate != 0 ||
        !mir_gnarly_char_type(mir.insns[667].type))
        return mir_machine_reject(
            "wide-string-call-runner", "wide-memory");

    if (!mir_nested_for_local_address(392, &alpha_offset) ||
        alpha_offset < -mir.local_bytes ||
        alpha_offset + 54 > 0 ||
        !mir_machine_same_location(
            &mir.insns[392], &mir.insns[508]) ||
        strcmp(mir.insns[392].name, mir.insns[508].name) ||
        !mir_wide_pointer_type(mir.insns[392].type))
        return mir_machine_reject(
            "wide-string-call-runner", "alpha-buffer");
    for (group = 0;
         group < (int)(sizeof(local_groups) /
                       sizeof(local_groups[0]));
         ++group) {
        int first = local_groups[group][0];

        for (item = 1; item < 5; ++item)
            if (local_groups[group][item] >= 0 &&
                !mir_machine_same_location(
                    &mir.insns[first],
                    &mir.insns[local_groups[group][item]]))
                return mir_machine_reject(
                    "wide-string-call-runner", "local-alias");
        for (previous = 0; previous < group; ++previous)
            if (mir_machine_same_location(
                    &mir.insns[first],
                    &mir.insns[local_groups[previous][0]]))
                return mir_machine_reject(
                    "wide-string-call-runner",
                    "distinct-locals");
    }
    if (!mir_machine_same_location(
            &mir.insns[513], &mir.insns[521]) ||
        !mir_machine_same_location(
            &mir.insns[513], &mir.insns[538]) ||
        !mir_machine_same_location(
            &mir.insns[513], &mir.insns[548]) ||
        !mir_machine_same_location(
            &mir.insns[513], &mir.insns[567]) ||
        !mir_machine_same_location(
            &mir.insns[524], &mir.insns[525]) ||
        !mir_machine_same_location(
            &mir.insns[524], &mir.insns[546]) ||
        mir_machine_same_location(
            &mir.insns[513], &mir.insns[524]))
        return mir_machine_reject(
            "wide-string-call-runner", "pointer-locals");

    for (item = 0; item < 7; ++item) {
        function = mir_wide_direct_function(
            random_calls[item], 0, TYPE_INT, 0);
        if (function == NULL ||
            !mir_gnarly_word_type(
                mir.insns[random_calls[item]].type, 0) ||
            (plan->random_function != NULL &&
             plan->random_function != function))
            return mir_machine_reject(
                "wide-string-call-runner", "random-calls");
        plan->random_function = function;
    }
    for (item = 0; item < 8; ++item) {
        int arguments[1] = {exit_arguments[item]};

        function = mir_wide_direct_function(
            exit_calls[item], 1, TYPE_VOID, 0);
        if (function == NULL || !function->is_noreturn ||
            !mir_wide_call_arguments(
                exit_calls[item], 1, arguments) ||
            !mir_machine_constant_equals(
                mir.insns[exit_arguments[item]].dst, 1) ||
            (plan->exit_function != NULL &&
             plan->exit_function != function))
            return mir_machine_reject(
                "wide-string-call-runner", "exit-calls");
        plan->exit_function = function;
    }
    {
        static const int first_length_arguments[1] = {131};
        static const int second_length_arguments[1] = {680};
        static const int first_find_arguments[2] = {273, 275};
        static const int last_find_arguments[2] = {326, 328};
        static const int copy_arguments[2] = {392, 394};
        static const int find_string_arguments[2] = {519, 521};
        static const int compare_arguments[3] = {546, 548, 552};

        plan->length_function =
            mir_wide_direct_function(133, 1, TYPE_INT, 0);
        if (plan->length_function == NULL ||
            !mir_gnarly_word_type(mir.insns[133].type, 1) ||
            !mir_wide_call_arguments(
                133, 1, first_length_arguments) ||
            mir_wide_direct_function(682, 1, TYPE_INT, 0) !=
                plan->length_function ||
            !mir_gnarly_word_type(mir.insns[682].type, 1) ||
            !mir_wide_call_arguments(
                682, 1, second_length_arguments))
            return mir_machine_reject(
                "wide-string-call-runner", "length-calls");
        plan->find_first_function =
            mir_wide_direct_function(277, 2, TYPE_INT, 1);
        plan->find_last_function =
            mir_wide_direct_function(330, 2, TYPE_INT, 1);
        plan->copy_function =
            mir_wide_direct_function(397, 2, TYPE_INT, 1);
        plan->find_string_function =
            mir_wide_direct_function(523, 2, TYPE_INT, 1);
        if (plan->find_first_function == NULL ||
            plan->find_last_function == NULL ||
            plan->copy_function == NULL ||
            plan->find_string_function == NULL ||
            !mir_wide_pointer_type(mir.insns[277].type) ||
            !mir_wide_pointer_type(mir.insns[330].type) ||
            !mir_wide_pointer_type(mir.insns[397].type) ||
            !mir_wide_pointer_type(mir.insns[523].type) ||
            !mir_wide_call_arguments(
                277, 2, first_find_arguments) ||
            !mir_wide_call_arguments(
                330, 2, last_find_arguments) ||
            !mir_wide_call_arguments(
                397, 2, copy_arguments) ||
            !mir_wide_call_arguments(
                523, 2, find_string_arguments))
            return mir_machine_reject(
                "wide-string-call-runner", "wide-calls");
        plan->compare_function =
            find_global(mir.insns[555].name);
        if (plan->compare_function == NULL ||
            plan->compare_function->storage != SC_FUNC ||
            plan->compare_function->is_funcptr ||
            mir.insns[555].src1 >= 0 ||
            mir.insns[555].memory_flags != 0 ||
            !mir_gnarly_word_type(mir.insns[555].type, 0) ||
            !mir_wide_call_arguments(
                555, 3, compare_arguments))
            return mir_machine_reject(
                "wide-string-call-runner", "compare-call");
    }
    if (plan->random_function == plan->exit_function ||
        plan->random_function == plan->length_function ||
        plan->random_function == plan->find_first_function ||
        plan->random_function == plan->find_last_function ||
        plan->random_function == plan->copy_function ||
        plan->random_function == plan->find_string_function ||
        plan->exit_function == plan->length_function ||
        plan->length_function == plan->find_first_function ||
        plan->length_function == plan->find_last_function ||
        plan->length_function == plan->copy_function ||
        plan->length_function == plan->find_string_function ||
        plan->find_first_function == plan->find_last_function ||
        plan->find_first_function == plan->copy_function ||
        plan->find_first_function == plan->find_string_function ||
        plan->find_last_function == plan->copy_function ||
        plan->find_last_function == plan->find_string_function ||
        plan->copy_function == plan->find_string_function ||
        plan->print_function == plan->random_function ||
        plan->print_function == plan->exit_function ||
        plan->print_function == plan->length_function ||
        plan->print_function == plan->find_first_function ||
        plan->print_function == plan->find_last_function ||
        plan->print_function == plan->copy_function ||
        plan->print_function == plan->find_string_function)
        return mir_machine_reject(
            "wide-string-call-runner", "function-alias");
    return 1;
}

enum {
    MIR_WIDE_I = -2,
    MIR_WIDE_START = -4,
    MIR_WIDE_END = -6,
    MIR_WIDE_LEN = -8,
    MIR_WIDE_ORIG = -10,
    MIR_WIDE_POINTER = -12,
    MIR_WIDE_OFFSET = -14,
    MIR_WIDE_LENGTH = -16,
    MIR_WIDE_ALPHA = -70,
    MIR_WIDE_FRAME_BYTES = 70
};

static void mir_wide_push_frame(FILE *out, int offset)
{
    mir_gnarly_load_word(out, offset);
    fputs("\tpush hl\n", out);
}

static void mir_wide_buffer_address(
    FILE *out, const struct MirWideStringRunner *plan, int index_offset)
{
    mir_gnarly_load_word(out, index_offset);
    fprintf(out,
            "\tadd hl,hl\n\tld de,%s\n\tadd hl,de\n",
            asm_name_for(sym_asm_name(plan->buffer)));
}

static void mir_wide_print(
    FILE *out, const struct MirWideStringRunner *plan,
    int slot, int argument_count)
{
    mir_emit_runtime_call(out, plan->print_names[slot]);
    mir_gnarly_cleanup(out, argument_count);
}

static void mir_wide_exit(
    FILE *out, const struct MirWideStringRunner *plan)
{
    fputs("\tld hl,1\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->exit_function);
    fputs("\tpop bc\n", out);
}

static void mir_wide_increment_word(
    FILE *out, int offset)
{
    int done = new_label();

    fprintf(out,
            "\tinc (ix%d)\n\tjp nz,L%d\n"
            "\tinc (ix%d)\nL%d:\n",
            offset, done, offset + 1, done);
}

static void mir_wide_loop_test(
    FILE *out, int offset, int bound, int done)
{
    mir_gnarly_load_word(out, offset);
    fprintf(out,
            "\tld de,%d\n\tor a\n\tsbc hl,de\n\tjp nc,L%d\n",
            bound, done);
}

static void mir_wide_emit_heading(
    FILE *out, const struct MirWideStringRunner *plan,
    int string_slot, int print_slot)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[string_slot]);
    mir_wide_print(out, plan, print_slot, 1);
}

static void mir_emit_wide_string_runner(
    FILE *out, const struct MirWideStringRunner *plan)
{
    int fill_loop = new_label();
    int fill_letter_done = new_label();
    int length_loop = new_label();
    int length_done = new_label();
    int length_ok = new_label();
    int find_loop = new_label();
    int find_done = new_label();
    int first_present = new_label();
    int first_correct = new_label();
    int last_present = new_label();
    int last_correct = new_label();
    int string_loop = new_label();
    int string_done = new_label();
    int string_bounds_ok = new_label();
    int string_present = new_label();
    int string_equal = new_label();
    int print_loop = new_label();
    int print_done = new_label();
    int start_in_range = new_label();
    int length_in_range = new_label();

    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    fprintf(out,
            "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n",
            MIR_WIDE_FRAME_BYTES);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    fprintf(out,
            "\tld hl,%s\n\tld bc,4096\n\tld d,97\nL%d:\n"
            "\tld (hl),d\n\tinc hl\n\tld (hl),0\n\tinc hl\n"
            "\tinc d\n\tld a,d\n\tcp 123\n",
            asm_name_for(sym_asm_name(plan->buffer)), fill_loop);
    fprintf(out,
            "\tjp nz,L%d\n\tld d,97\nL%d:\n"
            "\tdec bc\n\tld a,b\n\tor c\n\tjp nz,L%d\n",
            fill_letter_done, fill_letter_done, fill_loop);

    mir_wide_emit_heading(out, plan, 0, 0);
    mir_gnarly_store_word(out, MIR_WIDE_I, 0);
    fprintf(out, "L%d:\n", length_loop);
    mir_wide_loop_test(
        out, MIR_WIDE_I, 1000, length_done);
    mir_machine_emit_symbol_call(out, plan->random_function);
    fputs("\tld de,300\n", out);
    mir_emit_runtime_call(out, "__modu");
    fprintf(out,
            "\tld (ix%d),l\n\tld (ix%d),h\n",
            MIR_WIDE_START, MIR_WIDE_START + 1);
    mir_machine_emit_symbol_call(out, plan->random_function);
    fputs("\tld de,3000\n", out);
    mir_emit_runtime_call(out, "__modu");
    fputs("\tinc hl\n", out);
    fprintf(out,
            "\tld e,(ix%d)\n\tld d,(ix%d)\n\tadd hl,de\n"
            "\tld (ix%d),l\n\tld (ix%d),h\n"
            "\tor a\n\tsbc hl,de\n"
            "\tld (ix%d),l\n\tld (ix%d),h\n",
            MIR_WIDE_START, MIR_WIDE_START + 1,
            MIR_WIDE_END, MIR_WIDE_END + 1,
            MIR_WIDE_LEN, MIR_WIDE_LEN + 1);
    mir_wide_buffer_address(out, plan, MIR_WIDE_END);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n", out);
    fprintf(out,
            "\tld (ix%d),e\n\tld (ix%d),d\n"
            "\tdec hl\n\txor a\n\tld (hl),a\n\tinc hl\n\tld (hl),a\n",
            MIR_WIDE_ORIG, MIR_WIDE_ORIG + 1);
    mir_wide_buffer_address(out, plan, MIR_WIDE_START);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->length_function);
    fputs("\tpop bc\n", out);
    fprintf(out,
            "\tld (ix%d),l\n\tld (ix%d),h\n"
            "\tld e,(ix%d)\n\tld d,(ix%d)\n"
            "\tor a\n\tsbc hl,de\n\tjp z,L%d\n",
            MIR_WIDE_LENGTH, MIR_WIDE_LENGTH + 1,
            MIR_WIDE_LEN, MIR_WIDE_LEN + 1, length_ok);
    mir_wide_push_frame(out, MIR_WIDE_END);
    mir_wide_push_frame(out, MIR_WIDE_START);
    mir_wide_push_frame(out, MIR_WIDE_LENGTH);
    mir_wide_push_frame(out, MIR_WIDE_LEN);
    mir_wide_push_frame(out, MIR_WIDE_I);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[1]);
    mir_wide_print(out, plan, 1, 6);
    mir_wide_exit(out, plan);
    fprintf(out, "L%d:\n", length_ok);
    mir_wide_buffer_address(out, plan, MIR_WIDE_END);
    fprintf(out,
            "\tld e,(ix%d)\n\tld d,(ix%d)\n"
            "\tld (hl),e\n\tinc hl\n\tld (hl),d\n",
            MIR_WIDE_ORIG, MIR_WIDE_ORIG + 1);
    mir_wide_increment_word(out, MIR_WIDE_I);
    fprintf(out, "\tjp L%d\nL%d:\n", length_loop, length_done);

    mir_wide_emit_heading(out, plan, 2, 2);
    mir_gnarly_store_word(out, MIR_WIDE_I, 0);
    fprintf(out, "L%d:\n", find_loop);
    mir_wide_loop_test(
        out, MIR_WIDE_I, 1000, find_done);
    mir_machine_emit_symbol_call(out, plan->random_function);
    fputs("\tld de,300\n", out);
    mir_emit_runtime_call(out, "__modu");
    fprintf(out,
            "\tld (ix%d),l\n\tld (ix%d),h\n",
            MIR_WIDE_START, MIR_WIDE_START + 1);
    mir_machine_emit_symbol_call(out, plan->random_function);
    fputs("\tld de,70\n", out);
    mir_emit_runtime_call(out, "__modu");
    fputs("\tinc hl\n", out);
    fprintf(out,
            "\tld e,(ix%d)\n\tld d,(ix%d)\n\tadd hl,de\n"
            "\tld (ix%d),l\n\tld (ix%d),h\n"
            "\tor a\n\tsbc hl,de\n"
            "\tld (ix%d),l\n\tld (ix%d),h\n",
            MIR_WIDE_START, MIR_WIDE_START + 1,
            MIR_WIDE_END, MIR_WIDE_END + 1,
            MIR_WIDE_LEN, MIR_WIDE_LEN + 1);
    mir_wide_buffer_address(out, plan, MIR_WIDE_END);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n", out);
    fprintf(out,
            "\tld (ix%d),e\n\tld (ix%d),d\n"
            "\tdec hl\n\tld (hl),33\n\tinc hl\n\tld (hl),0\n",
            MIR_WIDE_ORIG, MIR_WIDE_ORIG + 1);
    fputs("\tld hl,33\n\tpush hl\n", out);
    mir_wide_buffer_address(out, plan, MIR_WIDE_START);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->find_first_function);
    fputs("\tpop bc\n\tpop bc\n", out);
    fprintf(out,
            "\tld (ix%d),l\n\tld (ix%d),h\n"
            "\tld a,h\n\tor l\n\tjp nz,L%d\n",
            MIR_WIDE_POINTER, MIR_WIDE_POINTER + 1, first_present);
    mir_wide_push_frame(out, MIR_WIDE_END);
    mir_wide_push_frame(out, MIR_WIDE_START);
    mir_wide_push_frame(out, MIR_WIDE_LEN);
    mir_wide_push_frame(out, MIR_WIDE_I);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[3]);
    mir_wide_print(out, plan, 3, 5);
    mir_wide_exit(out, plan);
    fprintf(out, "L%d:\n", first_present);
    mir_wide_buffer_address(out, plan, MIR_WIDE_END);
    fputs("\tex de,hl\n", out);
    mir_gnarly_load_word(out, MIR_WIDE_POINTER);
    fputs("\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp z,L%d\n", first_correct);
    mir_wide_push_frame(out, MIR_WIDE_END);
    mir_wide_push_frame(out, MIR_WIDE_START);
    mir_wide_push_frame(out, MIR_WIDE_LEN);
    mir_wide_push_frame(out, MIR_WIDE_I);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[4]);
    mir_wide_print(out, plan, 4, 5);
    mir_wide_exit(out, plan);
    fprintf(out, "L%d:\n", first_correct);
    fputs("\tld hl,33\n\tpush hl\n", out);
    mir_wide_buffer_address(out, plan, MIR_WIDE_START);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->find_last_function);
    fputs("\tpop bc\n\tpop bc\n", out);
    fprintf(out,
            "\tld (ix%d),l\n\tld (ix%d),h\n"
            "\tld a,h\n\tor l\n\tjp nz,L%d\n",
            MIR_WIDE_POINTER, MIR_WIDE_POINTER + 1, last_present);
    mir_wide_push_frame(out, MIR_WIDE_END);
    mir_wide_push_frame(out, MIR_WIDE_START);
    mir_wide_push_frame(out, MIR_WIDE_LEN);
    mir_wide_push_frame(out, MIR_WIDE_I);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[5]);
    mir_wide_print(out, plan, 5, 5);
    mir_wide_exit(out, plan);
    fprintf(out, "L%d:\n", last_present);
    mir_wide_buffer_address(out, plan, MIR_WIDE_END);
    fputs("\tex de,hl\n", out);
    mir_gnarly_load_word(out, MIR_WIDE_POINTER);
    fputs("\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp z,L%d\n", last_correct);
    mir_wide_push_frame(out, MIR_WIDE_END);
    mir_wide_push_frame(out, MIR_WIDE_START);
    mir_wide_push_frame(out, MIR_WIDE_LEN);
    mir_wide_push_frame(out, MIR_WIDE_I);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[6]);
    mir_wide_print(out, plan, 6, 5);
    mir_wide_exit(out, plan);
    fprintf(out, "L%d:\n", last_correct);
    mir_wide_buffer_address(out, plan, MIR_WIDE_END);
    fprintf(out,
            "\tld e,(ix%d)\n\tld d,(ix%d)\n"
            "\tld (hl),e\n\tinc hl\n\tld (hl),d\n",
            MIR_WIDE_ORIG, MIR_WIDE_ORIG + 1);
    mir_wide_increment_word(out, MIR_WIDE_I);
    fprintf(out, "\tjp L%d\nL%d:\n", find_loop, find_done);

    mir_wide_emit_heading(out, plan, 7, 7);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[8]);
    mir_gnarly_ix_address(out, MIR_WIDE_ALPHA);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->copy_function);
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_gnarly_store_word(out, MIR_WIDE_I, 0);
    fprintf(out, "L%d:\n", string_loop);
    mir_wide_loop_test(
        out, MIR_WIDE_I, 1000, string_done);
    mir_machine_emit_symbol_call(out, plan->random_function);
    fputs("\tld de,300\n", out);
    mir_emit_runtime_call(out, "__modu");
    fprintf(out,
            "\tld (ix%d),l\n\tld (ix%d),h\n",
            MIR_WIDE_START, MIR_WIDE_START + 1);
    mir_machine_emit_symbol_call(out, plan->random_function);
    fputs("\tld de,26\n", out);
    mir_emit_runtime_call(out, "__modu");
    fprintf(out,
            "\tld (ix%d),l\n\tld (ix%d),h\n",
            MIR_WIDE_OFFSET, MIR_WIDE_OFFSET + 1);
    mir_machine_emit_symbol_call(out, plan->random_function);
    fputs("\tpush hl\n\tld hl,26\n", out);
    fprintf(out,
            "\tld e,(ix%d)\n\tld d,(ix%d)\n"
            "\tor a\n\tsbc hl,de\n\tex de,hl\n\tpop hl\n",
            MIR_WIDE_OFFSET, MIR_WIDE_OFFSET + 1);
    mir_emit_runtime_call(out, "__modu");
    fputs("\tinc hl\n", out);
    fprintf(out,
            "\tld (ix%d),l\n\tld (ix%d),h\n"
            "\tld e,(ix%d)\n\tld d,(ix%d)\n\tadd hl,de\n"
            "\tld de,26\n\tor a\n\tsbc hl,de\n"
            "\tjp c,L%d\n\tjp z,L%d\n",
            MIR_WIDE_LEN, MIR_WIDE_LEN + 1,
            MIR_WIDE_OFFSET, MIR_WIDE_OFFSET + 1,
            string_bounds_ok, string_bounds_ok);
    mir_wide_push_frame(out, MIR_WIDE_LEN);
    mir_wide_push_frame(out, MIR_WIDE_OFFSET);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[9]);
    mir_wide_print(out, plan, 8, 3);
    mir_wide_exit(out, plan);
    fprintf(out, "L%d:\n", string_bounds_ok);
    mir_gnarly_ix_address(out, MIR_WIDE_ALPHA);
    fputs("\tpush hl\n", out);
    mir_gnarly_load_word(out, MIR_WIDE_OFFSET);
    fputs("\tadd hl,hl\n\tex de,hl\n\tpop hl\n\tadd hl,de\n", out);
    fprintf(out,
            "\tld (ix%d),l\n\tld (ix%d),h\n\tpush hl\n",
            MIR_WIDE_POINTER, MIR_WIDE_POINTER + 1);
    mir_wide_buffer_address(out, plan, MIR_WIDE_START);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->find_string_function);
    fputs("\tpop bc\n\tpop bc\n", out);
    fprintf(out,
            "\tld (ix%d),l\n\tld (ix%d),h\n"
            "\tld a,h\n\tor l\n\tjp nz,L%d\n",
            MIR_WIDE_LENGTH, MIR_WIDE_LENGTH + 1,
            string_present);
    mir_wide_push_frame(out, MIR_WIDE_POINTER);
    mir_wide_push_frame(out, MIR_WIDE_LEN);
    mir_wide_push_frame(out, MIR_WIDE_OFFSET);
    mir_wide_push_frame(out, MIR_WIDE_START);
    mir_wide_push_frame(out, MIR_WIDE_I);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[10]);
    mir_wide_print(out, plan, 9, 6);
    mir_wide_exit(out, plan);
    fprintf(out, "L%d:\n", string_present);
    mir_gnarly_load_word(out, MIR_WIDE_LEN);
    fputs("\tadd hl,hl\n\tpush hl\n", out);
    mir_wide_push_frame(out, MIR_WIDE_POINTER);
    mir_wide_push_frame(out, MIR_WIDE_LENGTH);
    mir_machine_emit_symbol_call(out, plan->compare_function);
    mir_gnarly_cleanup(out, 3);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", string_equal);
    mir_wide_push_frame(out, MIR_WIDE_POINTER);
    mir_wide_push_frame(out, MIR_WIDE_LEN);
    mir_wide_push_frame(out, MIR_WIDE_OFFSET);
    mir_wide_push_frame(out, MIR_WIDE_START);
    mir_wide_push_frame(out, MIR_WIDE_I);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[11]);
    mir_wide_print(out, plan, 10, 6);
    mir_wide_exit(out, plan);
    fprintf(out, "L%d:\n", string_equal);
    mir_wide_increment_word(out, MIR_WIDE_I);
    fprintf(out, "\tjp L%d\nL%d:\n", string_loop, string_done);

    mir_wide_emit_heading(out, plan, 12, 11);
    mir_gnarly_store_word(out, MIR_WIDE_I, 0);
    mir_gnarly_store_word(out, MIR_WIDE_START, 0);
    mir_gnarly_store_word(out, MIR_WIDE_LEN, 1);
    fprintf(out, "L%d:\n", print_loop);
    mir_wide_loop_test(
        out, MIR_WIDE_I, 20, print_done);
    mir_gnarly_load_word(out, MIR_WIDE_START);
    fprintf(out,
            "\tld e,(ix%d)\n\tld d,(ix%d)\n\tadd hl,de\n"
            "\tld (ix%d),l\n\tld (ix%d),h\n",
            MIR_WIDE_LEN, MIR_WIDE_LEN + 1,
            MIR_WIDE_END, MIR_WIDE_END + 1);
    mir_wide_buffer_address(out, plan, MIR_WIDE_END);
    fputs("\tld l,(hl)\n\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n", out);
    fprintf(out,
            "\tld (ix%d),l\n\tld (ix%d),h\n",
            MIR_WIDE_ORIG, MIR_WIDE_ORIG + 1);
    mir_wide_buffer_address(out, plan, MIR_WIDE_END);
    fputs("\txor a\n\tld (hl),a\n\tinc hl\n\tld (hl),a\n", out);
    mir_wide_buffer_address(out, plan, MIR_WIDE_START);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->length_function);
    fputs("\tpop bc\n", out);
    fprintf(out,
            "\tld (ix%d),l\n\tld (ix%d),h\n",
            MIR_WIDE_LENGTH, MIR_WIDE_LENGTH + 1);
    mir_wide_buffer_address(out, plan, MIR_WIDE_START);
    fputs("\tpush hl\n", out);
    mir_wide_push_frame(out, MIR_WIDE_LENGTH);
    mir_wide_push_frame(out, MIR_WIDE_LEN);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[13]);
    mir_wide_print(out, plan, 12, 4);
    mir_wide_buffer_address(out, plan, MIR_WIDE_END);
    fprintf(out,
            "\tld e,(ix%d)\n\tld d,(ix%d)\n"
            "\tld (hl),e\n\tinc hl\n\tld (hl),d\n",
            MIR_WIDE_ORIG, MIR_WIDE_ORIG + 1);
    mir_gnarly_load_word(out, MIR_WIDE_START);
    fputs("\tld de,37\n\tadd hl,de\n\tld de,300\n"
          "\tor a\n\tsbc hl,de\n", out);
    fprintf(out,
            "\tjp nc,L%d\n\tadd hl,de\nL%d:\n"
            "\tld (ix%d),l\n\tld (ix%d),h\n",
            start_in_range, start_in_range,
            MIR_WIDE_START, MIR_WIDE_START + 1);
    mir_gnarly_load_word(out, MIR_WIDE_LEN);
    fputs("\tld de,17\n\tadd hl,de\n\tld a,l\n\tcp 71\n", out);
    fprintf(out,
            "\tjp c,L%d\n\tsub 70\n\tld l,a\n\tld h,0\nL%d:\n"
            "\tld (ix%d),l\n\tld (ix%d),h\n",
            length_in_range, length_in_range,
            MIR_WIDE_LEN, MIR_WIDE_LEN + 1);
    mir_wide_increment_word(out, MIR_WIDE_I);
    fprintf(out, "\tjp L%d\nL%d:\n", print_loop, print_done);
    fputs("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static void mir_fixed_call_runner_failure(
    FILE *out, const struct MirFixedCallCheckRunner *plan,
    int check, int next_label)
{
    int increment_done = new_label();

    fprintf(out, "\tjp z,L%d\n\tld hl,S%d\n\tpush hl\n",
            next_label, plan->failure_string_ids[check]);
    mir_machine_emit_symbol_call(
        out, plan->print_function);
    fprintf(out,
            "\tpop bc\n\tinc (ix-2)\n"
            "\tjp nz,L%d\n\tinc (ix-1)\n"
            "L%d:\nL%d:\n",
            increment_done, increment_done, next_label);
}

static void mir_fixed_call_runner_word_check(
    FILE *out, const struct MirFixedCallCheckRunner *plan,
    int check, int expected)
{
    int next_label = new_label();

    fprintf(out,
            "\tld de,%d\n\tor a\n\tsbc hl,de\n",
            expected);
    mir_fixed_call_runner_failure(
        out, plan, check, next_label);
}

static void mir_fixed_call_runner_push_long(
    FILE *out, unsigned long value)
{
    fprintf(out,
            "\tld hl,%lu\n\tpush hl\n"
            "\tld hl,%lu\n\tpush hl\n",
            (value >> 16) & 0xffffUL,
            value & 0xffffUL);
}

static void mir_emit_fixed_call_check_runner(
    FILE *out, const struct MirFixedCallCheckRunner *plan)
{
    int success = new_label();
    int done = new_label();
    int next_label;
    int argument;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-6\n\tadd hl,sp\n\tld sp,hl\n"
          "\tld (ix-2),0\n\tld (ix-1),0\n", out);
    for (argument = 0; argument < 4; ++argument)
        fprintf(out, "\tld (ix%+d),%d\n",
                -6 + argument,
                plan->array_values[argument] & 255);

    for (argument = 1; argument >= 0; --argument)
        fprintf(out, "\tld hl,%d\n\tpush hl\n",
                plan->binary_arguments[0][argument]);
    mir_machine_emit_symbol_call(
        out, plan->functions[0]);
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_fixed_call_runner_word_check(
        out, plan, 0, plan->expected_words[0]);

    for (argument = 1; argument >= 0; --argument)
        fprintf(out, "\tld hl,%d\n\tpush hl\n",
                plan->binary_arguments[1][argument]);
    mir_machine_emit_symbol_call(
        out, plan->functions[1]);
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_fixed_call_runner_word_check(
        out, plan, 1, plan->expected_words[1]);

    fprintf(out,
            "\tld hl,%d\n\tpush hl\n"
            "\tpush ix\n\tpop hl\n"
            "\tld de,-6\n\tadd hl,de\n\tpush hl\n",
            plan->pointer_count);
    mir_machine_emit_symbol_call(
        out, plan->functions[2]);
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_fixed_call_runner_word_check(
        out, plan, 2, plan->expected_words[2]);

    mir_fixed_call_runner_push_long(
        out, plan->long_arguments[1]);
    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->long_middle_argument);
    mir_fixed_call_runner_push_long(
        out, plan->long_arguments[0]);
    mir_machine_emit_symbol_call(
        out, plan->functions[3]);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tpop bc\n\tpop bc\n", out);
    {
        int failure_label = new_label();
        int increment_done = new_label();

        next_label = new_label();
        fprintf(out,
                "\tld bc,%lu\n\tor a\n\tsbc hl,bc\n"
                "\tjp nz,L%d\n\tex de,hl\n"
                "\tld bc,%lu\n\tor a\n\tsbc hl,bc\n"
                "\tjp z,L%d\n"
                "L%d:\n\tld hl,S%d\n\tpush hl\n",
                plan->expected_long & 0xffffUL,
                failure_label,
                (plan->expected_long >> 16) & 0xffffUL,
                next_label, failure_label,
                plan->failure_string_ids[3]);
        mir_machine_emit_symbol_call(
            out, plan->print_function);
        fprintf(out,
                "\tpop bc\n\tinc (ix-2)\n"
                "\tjp nz,L%d\n\tinc (ix-1)\n"
                "L%d:\nL%d:\n",
                increment_done, increment_done, next_label);
    }

    for (argument = 2; argument >= 0; --argument)
        fprintf(out, "\tld hl,%d\n\tpush hl\n",
                plan->byte_arguments[argument]);
    mir_machine_emit_symbol_call(
        out, plan->functions[4]);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_fixed_call_runner_word_check(
        out, plan, 4, plan->expected_words[3]);

    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n"
          "\tld a,h\n\tor l\n", out);
    fprintf(out,
            "\tjp z,L%d\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            success, plan->summary_string_id);
    mir_machine_emit_symbol_call(
        out, plan->print_function);
    fprintf(out,
            "\tpop bc\n\tpop bc\n\tld hl,1\n"
            "\tjp L%d\n"
            "L%d:\n\tld hl,S%d\n\tpush hl\n",
            done, success, plan->success_string_id);
    mir_machine_emit_symbol_call(
        out, plan->print_function);
    fprintf(out,
            "\tpop bc\n\tld hl,0\n"
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            done);
}

static void mir_fixed_index_runner_failure(
    FILE *out, int next_label)
{
    int increment_done = new_label();

    fprintf(out,
            "\tjp z,L%d\n\tinc (ix-2)\n"
            "\tjp nz,L%d\n\tinc (ix-1)\n"
            "L%d:\nL%d:\n",
            next_label, increment_done,
            increment_done, next_label);
}

static void mir_fixed_index_runner_word_check(
    FILE *out, int expected)
{
    int next_label = new_label();

    fprintf(out,
            "\tld de,%d\n\tor a\n\tsbc hl,de\n",
            expected);
    mir_fixed_index_runner_failure(out, next_label);
}

static void mir_fixed_index_runner_wide_check(
    FILE *out, unsigned long expected)
{
    int failure = new_label();
    int next_label = new_label();

    fprintf(out,
            "\tld bc,%lu\n\tor a\n\tsbc hl,bc\n"
            "\tjp nz,L%d\n\tex de,hl\n"
            "\tld bc,%lu\n\tor a\n\tsbc hl,bc\n"
            "\tjp z,L%d\n"
            "L%d:\n",
            expected & 0xffffUL, failure,
            (expected >> 16) & 0xffffUL,
            next_label, failure);
    mir_fixed_index_runner_failure(out, next_label);
}

static void mir_emit_fixed_index_call_runner(
    FILE *out, const struct MirFixedIndexCallRunner *plan)
{
    static const int int_offsets[4] = {-50, -48, -46, -44};
    static const int long_offsets[4] = {-42, -38, -34, -30};
    static const int pair_offsets[6] = {
        -26, -22, -18, -14, -10, -6
    };
    static const int pointer_offsets[3] = {-50, -42, -26};
    int success = new_label();
    int done = new_label();
    int item;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-50\n\tadd hl,sp\n\tld sp,hl\n"
          "\tld (ix-2),0\n\tld (ix-1),0\n", out);
    for (item = 0; item < 4; ++item) {
        fprintf(out,
                "\tld hl,%d\n"
                "\tld (ix%+d),l\n\tld (ix%+d),h\n",
                plan->int_values[item],
                int_offsets[item], int_offsets[item] + 1);
        mir_machine_emit_float_bits(
            out, plan->long_values[item]);
        mir_machine_emit_ix_wide_store(
            out, long_offsets[item]);
    }
    for (item = 0; item < 6; ++item) {
        mir_machine_emit_float_bits(
            out, plan->pair_values[item]);
        mir_machine_emit_ix_wide_store(
            out, pair_offsets[item]);
    }

    for (item = 0; item < 2; ++item) {
        fprintf(out, "\tld hl,%d\n\tpush hl\n",
                plan->scalar_arguments[item]);
        mir_machine_emit_symbol_call(
            out, plan->functions[item]);
        fputs("\tpop bc\n", out);
        mir_fixed_index_runner_word_check(
            out, plan->expected_words[item]);
    }
    for (item = 0; item < 3; ++item) {
        fprintf(out,
                "\tld hl,%d\n\tpush hl\n"
                "\tpush ix\n\tpop hl\n"
                "\tld de,%d\n\tadd hl,de\n\tpush hl\n",
                plan->pointer_indices[item],
                pointer_offsets[item]);
        mir_machine_emit_symbol_call(
            out, plan->functions[item + 2]);
        fputs("\tpop bc\n\tpop bc\n", out);
        if (item == 0)
            mir_fixed_index_runner_word_check(
                out, plan->expected_words[2]);
        else
            mir_fixed_index_runner_wide_check(
                out, plan->expected_longs[item - 1]);
    }

    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n"
          "\tld a,h\n\tor l\n", out);
    fprintf(out,
            "\tjp z,L%d\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            success, plan->failure_string_id);
    mir_machine_emit_symbol_call(
        out, plan->print_function);
    fprintf(out,
            "\tpop bc\n\tpop bc\n\tld hl,1\n"
            "\tjp L%d\n"
            "L%d:\n\tld hl,S%d\n\tpush hl\n",
            done, success, plan->success_string_id);
    mir_machine_emit_symbol_call(
        out, plan->print_function);
    fprintf(out,
            "\tpop bc\n\tld hl,0\n"
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            done);
}

static void mir_long_index_emit_global_address(
    FILE *out, struct Sym *symbol)
{
    mir_machine_emit_global_address_de(out, symbol, 0);
    fputs("\tex de,hl\n", out);
}

static void mir_long_index_emit_one_pointer_call(
    FILE *out, struct Sym *function, struct Sym *argument)
{
    mir_long_index_emit_global_address(out, argument);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, function);
    fputs("\tpop bc\n", out);
}

static void mir_long_index_emit_string_copy(
    FILE *out, const struct MirLongIndexCallRunner *plan,
    int string_id)
{
    if (plan->copy_fastcall) {
        mir_machine_emit_global_address_de(
            out, plan->source_buffer, 0);
        fprintf(out, "\tld hl,S%d\n", string_id);
        mir_emit_runtime_call(
            out, plan->copy_runtime_name);
        return;
    }
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_long_index_emit_global_address(
        out, plan->source_buffer);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(
        out, plan->copy_string_function);
    fputs("\tpop bc\n\tpop bc\n", out);
}

static void mir_long_index_emit_length(
    FILE *out, const struct MirLongIndexCallRunner *plan)
{
    mir_long_index_emit_global_address(
        out, plan->source_buffer);
    if (plan->length_fastcall) {
        mir_emit_runtime_call(out, "__slf");
        return;
    }
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(
        out, plan->length_function);
    fputs("\tpop bc\n", out);
}

static void mir_long_index_emit_long_check(
    FILE *out, const struct MirLongIndexCallRunner *plan,
    int string_id, int expected_high, int expected_low,
    int actual_high_offset, int actual_low_offset)
{
    fprintf(out,
            "\tld hl,S%d\n\tpush hl\n"
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,%d\n\tpush hl\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n",
            string_id,
            expected_high, expected_low,
            actual_high_offset, actual_high_offset + 1,
            actual_low_offset, actual_low_offset + 1);
    mir_machine_emit_symbol_call(
        out, plan->long_check_function);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n",
          out);
}

static void mir_long_index_emit_live_long_check(
    FILE *out, const struct MirLongIndexCallRunner *plan,
    int string_id, int expected)
{
    fputs("\tld (ix-4),l\n\tld (ix-3),h\n"
          "\tex de,hl\n"
          "\tld (ix-2),l\n\tld (ix-1),h\n", out);
    mir_long_index_emit_long_check(
        out, plan, string_id,
        expected < 0 ? -1 : 0, expected, -2, -4);
}

static void mir_long_index_emit_length_check(
    FILE *out, const struct MirLongIndexCallRunner *plan)
{
    fputs("\tex de,hl\n", out);
    fprintf(out,
            "\tld hl,S%d\n\tpush hl\n"
            "\tld hl,0\n\tpush hl\n"
            "\tpush de\n"
            "\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n"
            "\tld l,(ix-4)\n\tld h,(ix-3)\n\tpush hl\n",
            plan->first_count_string_id);
    mir_machine_emit_symbol_call(
        out, plan->long_check_function);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n",
          out);
}

static void mir_long_index_emit_string_check(
    FILE *out, const struct MirLongIndexCallRunner *plan,
    struct Sym *expected, int expected_string_id,
    int label_string_id)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            label_string_id);
    if (expected != NULL)
        mir_long_index_emit_global_address(out, expected);
    else
        fprintf(out, "\tld hl,S%d\n", expected_string_id);
    fputs("\tpush hl\n", out);
    mir_long_index_emit_global_address(
        out, plan->output_buffer);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(
        out, plan->string_check_function);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n", out);
}

static void mir_long_index_emit_sum_check(
    FILE *out, const struct MirLongIndexCallRunner *plan,
    struct Sym *function, int count, int expected,
    int string_id)
{
    fprintf(out,
            "\tld hl,S%d\n\tpush hl\n"
            "\tld hl,%d\n"
            "\tld a,h\n\trlca\n\tsbc a,a\n"
            "\tld d,a\n\tld e,a\n"
            "\tpush de\n\tpush hl\n"
            "\tld hl,%d\n\tpush hl\n",
            string_id, expected, count);
    mir_long_index_emit_global_address(
        out, plan->inline_values);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, function);
    fputs("\tpop bc\n\tpop bc\n"
          "\tld a,h\n\trlca\n\tsbc a,a\n"
          "\tld d,a\n\tld e,a\n"
          "\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(
        out, plan->long_check_function);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n",
          out);
}

static void mir_emit_long_index_call_runner(
    FILE *out, const struct MirLongIndexCallRunner *plan)
{
    int fill_loop = new_label();
    int result_nonzero = new_label();
    int result_done = new_label();
    int return_nonzero = new_label();
    int return_done = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-4\n\tadd hl,sp\n\tld sp,hl\n", out);

    mir_long_index_emit_string_copy(
        out, plan, plan->first_source_string_id);
    mir_long_index_emit_one_pointer_call(
        out, plan->count_function, plan->source_buffer);
    fputs("\tld (ix-4),l\n\tld (ix-3),h\n"
          "\tex de,hl\n"
          "\tld (ix-2),l\n\tld (ix-1),h\n", out);
    mir_long_index_emit_length(out, plan);
    mir_long_index_emit_length_check(out, plan);

    mir_long_index_emit_one_pointer_call(
        out, plan->copy_function, plan->source_buffer);
    mir_long_index_emit_string_check(
        out, plan, plan->source_buffer, 0,
        plan->first_copy_string_id);

    mir_long_index_emit_string_copy(
        out, plan, plan->second_source_string_id);
    mir_long_index_emit_one_pointer_call(
        out, plan->count_function, plan->source_buffer);
    mir_long_index_emit_live_long_check(
        out, plan, plan->second_count_string_id,
        plan->second_count_expected);
    mir_long_index_emit_one_pointer_call(
        out, plan->copy_function, plan->source_buffer);
    mir_long_index_emit_string_check(
        out, plan, NULL, plan->second_source_string_id,
        plan->second_copy_string_id);

    mir_long_index_emit_global_address(
        out, plan->inline_values);
    fputs("\tld bc,0\n", out);
    fprintf(out,
            "L%d:\n"
            "\tld (hl),c\n\tinc hl\n"
            "\tld (hl),b\n\tinc hl\n"
            "\tinc bc\n\tld a,c\n\tcp %d\n"
            "\tjp c,L%d\n",
            fill_loop, plan->inline_bound, fill_loop);

    mir_long_index_emit_sum_check(
        out, plan, plan->safe_sum_function,
        plan->safe_count, plan->safe_expected,
        plan->safe_sum_string_id);
    fputs("\tld hl,0\n", out);
    mir_machine_emit_global_word_store(
        out, plan->unsafe_call_count, 0);
    mir_long_index_emit_sum_check(
        out, plan, plan->unsafe_sum_function,
        plan->unsafe_count, plan->unsafe_expected,
        plan->unsafe_sum_string_id);

    mir_machine_emit_global_word(
        out, plan->unsafe_call_count, 0);
    fputs("\tld a,h\n\trlca\n\tsbc a,a\n"
          "\tld d,a\n\tld e,a\n"
          "\tld (ix-4),l\n\tld (ix-3),h\n"
          "\tld (ix-2),e\n\tld (ix-1),d\n", out);
    mir_long_index_emit_long_check(
        out, plan, plan->unsafe_count_string_id,
        plan->unsafe_calls_expected < 0 ? -1 : 0,
        plan->unsafe_calls_expected, -2, -4);

    mir_machine_emit_global_word(
        out, plan->failures, 0);
    fputs("\tpush hl\n", out);
    mir_machine_emit_global_word(
        out, plan->checks, 0);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->summary_string_id);
    mir_machine_emit_symbol_call(
        out, plan->print_function);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n", out);

    mir_machine_emit_global_word(
        out, plan->failures, 0);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out,
            "\tjp nz,L%d\n"
            "\tld hl,S%d\n\tjp L%d\n"
            "L%d:\n\tld hl,S%d\n"
            "L%d:\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            result_nonzero,
            plan->zero_result_string_id, result_done,
            result_nonzero, plan->nonzero_result_string_id,
            result_done, plan->result_string_id);
    mir_machine_emit_symbol_call(
        out, plan->print_function);
    fputs("\tpop bc\n\tpop bc\n", out);

    mir_machine_emit_global_word(
        out, plan->failures, 0);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out,
            "\tjp nz,L%d\n\tld hl,0\n\tjp L%d\n"
            "L%d:\n\tld hl,1\n"
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            return_nonzero, return_done,
            return_nonzero, return_done);
}

static void mir_memory_runner_cleanup(FILE *out, int words)
{
    while (words-- > 0)
        fputs("\tpop bc\n", out);
}

static void mir_memory_runner_push_frame_word(FILE *out, int offset)
{
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n",
            offset, offset + 1);
}

static void mir_memory_runner_push_index(FILE *out)
{
    fputs("\tld l,(ix-2)\n\tld h,0\n\tpush hl\n", out);
}

static void mir_memory_runner_compute_size(FILE *out, int extra)
{
    fprintf(out,
            "\tld l,(ix-2)\n\tld h,0\n"
            "\tadd hl,hl\n\tld d,h\n\tld e,l\n"
            "\tadd hl,hl\n\tadd hl,hl\n\tadd hl,de\n"
            "\tld de,8\n\tadd hl,de\n"
            "\tld (ix-4),l\n\tld (ix-3),h\n"
            "\tld de,%d\n\tadd hl,de\n"
            "\tld (ix-6),l\n\tld (ix-5),h\n",
            extra);
}

static void mir_memory_runner_push_extended_size(FILE *out)
{
    fprintf(out,
            "\tld l,(ix-6)\n\tld h,(ix-5)\n\tpush hl\n");
}

static void mir_memory_runner_array_slot(
    FILE *out, const struct MirMemoryExerciseRunner *plan)
{
    fputs("\tld l,(ix-2)\n\tld h,0\n\tadd hl,hl\n", out);
    fprintf(out, "\tld de,%s\n\tadd hl,de\n",
            asm_name_for(plan->pointer_array_name));
}

static void mir_memory_runner_array_pointer(
    FILE *out, const struct MirMemoryExerciseRunner *plan)
{
    mir_memory_runner_array_slot(out, plan);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n", out);
}

static void mir_memory_runner_simple_print(
    FILE *out, const struct MirMemoryExerciseRunner *plan,
    int string)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string);
    mir_machine_emit_symbol_call(out, plan->print_function);
    fputs("\tpop bc\n", out);
}

static void mir_memory_runner_conditional_print(
    FILE *out, const struct MirMemoryExerciseRunner *plan,
    int string)
{
    int done = new_label();

    mir_machine_emit_global_word(out, plan->logging_root, 0);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", done);
    mir_memory_runner_simple_print(out, plan, string);
    fprintf(out, "L%d:\n", done);
}

static void mir_memory_runner_report(
    FILE *out, const struct MirMemoryExerciseRunner *plan)
{
    int done = new_label();

    mir_machine_emit_global_word(out, plan->logging_root, 0);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", done);
    mir_memory_runner_push_frame_word(out, -4);
    mir_memory_runner_push_index(out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[1]);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_memory_runner_cleanup(out, 3);
    fprintf(out, "L%d:\n", done);
}

static void mir_memory_runner_call_calloc(
    FILE *out, const struct MirMemoryExerciseRunner *plan)
{
    fputs("\tld hl,1\n\tpush hl\n", out);
    mir_memory_runner_push_extended_size(out);
    mir_machine_emit_symbol_call(out, plan->calloc_function);
    mir_memory_runner_cleanup(out, 2);
    fputs("\tld (ix-8),l\n\tld (ix-7),h\n", out);
}

static void mir_memory_runner_call_check_pc(
    FILE *out, const struct MirMemoryExerciseRunner *plan,
    int value)
{
    mir_memory_runner_push_extended_size(out);
    fprintf(out, "\tld hl,%d\n\tpush hl\n", value);
    mir_memory_runner_push_frame_word(out, -8);
    mir_machine_emit_symbol_call(out, plan->check_function);
    mir_memory_runner_cleanup(out, 3);
}

static void mir_memory_runner_call_memset_pc(
    FILE *out, const struct MirMemoryExerciseRunner *plan,
    int value)
{
    if (plan->memset_fastcall) {
        fputs("\tld l,(ix-6)\n\tld h,(ix-5)\n"
              "\tld b,h\n\tld c,l\n", out);
        fprintf(out, "\tld e,%d\n", value);
        fputs("\tld l,(ix-8)\n\tld h,(ix-7)\n", out);
        mir_emit_runtime_call(out, "__msf");
    } else {
        mir_memory_runner_push_extended_size(out);
        fprintf(out, "\tld hl,%d\n\tpush hl\n", value);
        mir_memory_runner_push_frame_word(out, -8);
        mir_machine_emit_symbol_call(
            out, plan->memset_function);
        mir_memory_runner_cleanup(out, 3);
    }
}

static void mir_memory_runner_call_saved_array(
    FILE *out, struct Sym *function, int value)
{
    if (value >= 0) {
        mir_memory_runner_push_frame_word(out, -4);
        fprintf(out, "\tld hl,%d\n\tpush hl\n", value);
    }
    mir_memory_runner_push_frame_word(out, -10);
    mir_machine_emit_symbol_call(out, function);
    mir_memory_runner_cleanup(out, value >= 0 ? 3 : 1);
}

static void mir_memory_runner_call_free_pc(
    FILE *out, const struct MirMemoryExerciseRunner *plan)
{
    mir_memory_runner_push_frame_word(out, -8);
    mir_machine_emit_symbol_call(out, plan->free_function);
    fputs("\tpop bc\n", out);
}

static void mir_memory_runner_allocate_array(
    FILE *out, const struct MirMemoryExerciseRunner *plan)
{
    mir_memory_runner_push_frame_word(out, -4);
    mir_machine_emit_symbol_call(out, plan->malloc_function);
    fputs("\tpop bc\n\tpush hl\n", out);
    mir_memory_runner_array_slot(out, plan);
    fputs("\tpop de\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n", out);
    if (plan->memset_fastcall) {
        fputs("\tex de,hl\n"
              "\tld e,170\n"
              "\tld c,(ix-4)\n\tld b,(ix-3)\n", out);
        mir_emit_runtime_call(out, "__msf");
    } else {
        mir_memory_runner_push_frame_word(out, -4);
        fputs("\tld hl,170\n\tpush hl\n\tpush de\n", out);
        mir_machine_emit_symbol_call(
            out, plan->memset_function);
        mir_memory_runner_cleanup(out, 3);
    }
}

static void mir_memory_runner_emit_release_loop(
    FILE *out, const struct MirMemoryExerciseRunner *plan,
    int initial, int extra)
{
    int loop = new_label();
    int done = new_label();

    fprintf(out, "\tld (ix-2),%d\nL%d:\n", initial, loop);
    fputs("\tld a,(ix-2)\n\tcp 66\n", out);
    fprintf(out, "\tjp nc,L%d\n", done);
    mir_memory_runner_compute_size(out, extra);
    mir_memory_runner_report(out, plan);
    mir_memory_runner_call_calloc(out, plan);
    mir_memory_runner_call_check_pc(out, plan, 0);
    mir_memory_runner_call_memset_pc(out, plan, 204);
    mir_memory_runner_array_pointer(out, plan);
    fputs("\tld (ix-10),l\n\tld (ix-9),h\n", out);
    mir_memory_runner_call_saved_array(
        out, plan->check_function, 170);
    if (plan->memset_fastcall) {
        fputs("\tld c,(ix-4)\n\tld b,(ix-3)\n"
              "\tld e,255\n"
              "\tld l,(ix-10)\n\tld h,(ix-9)\n", out);
        mir_emit_runtime_call(out, "__msf");
    } else {
        mir_memory_runner_call_saved_array(
            out, plan->memset_function, 255);
    }
    mir_memory_runner_call_saved_array(
        out, plan->free_function, -1);
    mir_memory_runner_call_check_pc(out, plan, 204);
    mir_memory_runner_call_free_pc(out, plan);
    fputs("\tinc (ix-2)\n\tinc (ix-2)\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n", loop, done);
}

static void mir_emit_memory_exercise_runner(
    FILE *out, const struct MirMemoryExerciseRunner *plan)
{
    int outer_loop = new_label();
    int outer_done = new_label();
    int allocate_loop = new_label();
    int allocate_done = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-10\n\tadd hl,sp\n\tld sp,hl\n", out);
    fprintf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld de,1\n",
            plan->argc_stack_offset + 2,
            plan->argc_stack_offset + 3);
    mir_emit_scalar_compare(out, '>', 0);
    mir_machine_emit_global_word_store(
        out, plan->logging_root, 0);

    fputs("\tld (ix-1),0\n", out);
    fprintf(out, "L%d:\n", outer_loop);
    fputs("\tld a,(ix-1)\n\tcp 10\n", out);
    fprintf(out, "\tjp nc,L%d\n", outer_done);
    mir_memory_runner_conditional_print(
        out, plan, plan->strings[0]);

    fputs("\tld (ix-2),0\n", out);
    fprintf(out, "L%d:\n", allocate_loop);
    fputs("\tld a,(ix-2)\n\tcp 66\n", out);
    fprintf(out, "\tjp nc,L%d\n", allocate_done);
    mir_memory_runner_compute_size(out, 5);
    mir_memory_runner_report(out, plan);
    mir_memory_runner_call_calloc(out, plan);
    mir_memory_runner_call_check_pc(out, plan, 0);
    mir_memory_runner_call_memset_pc(out, plan, 204);
    mir_memory_runner_allocate_array(out, plan);
    mir_memory_runner_call_check_pc(out, plan, 204);
    mir_memory_runner_call_free_pc(out, plan);
    fputs("\tinc (ix-2)\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n",
            allocate_loop, allocate_done);

    mir_memory_runner_conditional_print(
        out, plan, plan->strings[2]);
    mir_memory_runner_emit_release_loop(out, plan, 0, 3);
    mir_memory_runner_conditional_print(
        out, plan, plan->strings[3]);
    mir_memory_runner_emit_release_loop(out, plan, 1, 7);

    fputs("\tinc (ix-1)\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n", outer_loop, outer_done);
    mir_memory_runner_simple_print(
        out, plan, plan->strings[4]);
    fputs("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static void mir_allocation_runner_call_one(
    FILE *out, struct Sym *function, unsigned long argument)
{
    fprintf(out, "\tld hl,%lu\n\tpush hl\n", argument & 0xffffUL);
    mir_machine_emit_symbol_call(out, function);
    fputs("\tpop bc\n", out);
}

static void mir_allocation_runner_print(
    FILE *out, const struct MirAllocationLifetimeRunner *plan,
    int string)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string);
    mir_machine_emit_symbol_call(out, plan->print_function);
    fputs("\tpop bc\n", out);
}

static void mir_allocation_runner_free_slot(
    FILE *out, const struct MirAllocationLifetimeRunner *plan)
{
    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->free_function);
    fputs("\tpop bc\n", out);
}

static void mir_emit_allocation_lifetime_runner(
    FILE *out, const struct MirAllocationLifetimeRunner *plan)
{
    int first_ok = new_label();
    int sum_ok = new_label();
    int small_ok = new_label();
    int fill_loop = new_label();
    int small_check_failed = new_label();
    int small_check_ok = new_label();
    int optional_free_done = new_label();
    int final_small_ok = new_label();
    int wrap_failed = new_label();
    int done = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-2\n\tadd hl,sp\n\tld sp,hl\n", out);

    mir_allocation_runner_call_one(
        out, plan->allocate_function, 32768);
    fputs("\tld (ix-2),l\n\tld (ix-1),h\n"
          "\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", first_ok);
    mir_allocation_runner_print(out, plan, plan->strings[0]);
    fprintf(out, "\tld hl,1\n\tjp L%d\nL%d:\n", done, first_ok);

    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n"
          "\tld (hl),18\n"
          "\tld de,32767\n\tadd hl,de\n\tld (hl),52\n"
          "\tld l,(ix-2)\n\tld h,(ix-1)\n"
          "\tld e,(hl)\n\tld d,0\n"
          "\tld bc,32767\n\tadd hl,bc\n"
          "\tld a,(hl)\n\tld l,a\n\tld h,0\n\tadd hl,de\n"
          "\tpush hl\n\tld de,70\n\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp z,L%d\n\tpop hl\n", sum_ok);
    fputs("\tld de,0\n\tpush de\n"
          "\tld de,70\n\tpush de\n"
          "\tld de,0\n\tpush de\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[1]);
    mir_machine_emit_symbol_call(out, plan->print_function);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    fprintf(out, "\tld hl,1\n\tjp L%d\nL%d:\n\tpop bc\n", done, sum_ok);

    mir_allocation_runner_free_slot(out, plan);
    mir_allocation_runner_call_one(
        out, plan->allocate_function, 32);
    fputs("\tld (ix-2),l\n\tld (ix-1),h\n"
          "\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", small_ok);
    mir_allocation_runner_print(out, plan, plan->strings[2]);
    fprintf(out, "\tld hl,1\n\tjp L%d\nL%d:\n", done, small_ok);

    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n\tld bc,0\n", out);
    fprintf(out,
            "L%d:\n\tld (hl),c\n\tinc hl\n\tinc c\n"
            "\tld a,c\n\tcp 32\n\tjp c,L%d\n",
            fill_loop, fill_loop);
    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n"
          "\tld a,(hl)\n\tor a\n", out);
    fprintf(out, "\tjp nz,L%d\n", small_check_failed);
    fputs("\tld de,31\n\tadd hl,de\n\tld a,(hl)\n\tcp 31\n", out);
    fprintf(out, "\tjp z,L%d\n", small_check_ok);
    fprintf(out, "L%d:\n", small_check_failed);
    mir_allocation_runner_print(out, plan, plan->strings[3]);
    fprintf(out, "\tld hl,1\n\tjp L%d\nL%d:\n",
            done, small_check_ok);

    mir_allocation_runner_free_slot(out, plan);
    mir_allocation_runner_call_one(
        out, plan->allocate_function, 32768);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n\tpush hl\n", optional_free_done);
    mir_machine_emit_symbol_call(out, plan->free_function);
    fprintf(out, "\tpop bc\nL%d:\n", optional_free_done);

    mir_allocation_runner_call_one(
        out, plan->allocate_function, 1);
    fputs("\tld (ix-2),l\n\tld (ix-1),h\n"
          "\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", final_small_ok);
    mir_allocation_runner_print(out, plan, plan->strings[4]);
    fprintf(out, "\tld hl,1\n\tjp L%d\nL%d:\n",
            done, final_small_ok);

    mir_allocation_runner_call_one(
        out, plan->allocate_function, 65000);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", wrap_failed);
    mir_allocation_runner_print(out, plan, plan->strings[5]);
    fprintf(out, "\tld hl,1\n\tjp L%d\nL%d:\n", done, wrap_failed);

    mir_allocation_runner_free_slot(out, plan);
    mir_allocation_runner_print(out, plan, plan->strings[6]);
    fputs("\tld hl,0\n", out);
    fprintf(out, "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n", done);
}

static void mir_abort_runner_push_string(
    FILE *out, int string_id)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
}

static void mir_abort_runner_push_buffer(FILE *out)
{
    fputs("\tpush ix\n\tpop hl\n"
          "\tld de,-10\n\tadd hl,de\n\tpush hl\n", out);
}

static void mir_abort_runner_emit_call(
    FILE *out, struct Sym *function, int words)
{
    mir_machine_emit_symbol_call(out, function);
    mir_emit_final_call_cleanup(out, words);
}

static void mir_abort_runner_print(
    FILE *out, const struct MirAbortFileRunner *plan,
    int string_id)
{
    mir_abort_runner_push_string(out, string_id);
    mir_abort_runner_emit_call(out, plan->print_function, 1);
}

static void mir_abort_runner_increment_failures(
    FILE *out, const struct MirAbortFileRunner *plan)
{
    mir_machine_emit_global_word(out, plan->failures, 0);
    fputs("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->failures, 0);
}

static void mir_abort_runner_open(
    FILE *out, const struct MirAbortFileRunner *plan,
    int name_id, int mode_id)
{
    mir_abort_runner_push_string(out, mode_id);
    mir_abort_runner_push_string(out, name_id);
    mir_abort_runner_emit_call(out, plan->open_function, 2);
    fputs("\tld (ix-2),l\n\tld (ix-1),h\n", out);
}

static void mir_abort_runner_close(
    FILE *out, const struct MirAbortFileRunner *plan)
{
    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n", out);
    mir_abort_runner_emit_call(out, plan->close_function, 1);
}

static void mir_abort_runner_check_graph(
    FILE *out, const struct MirAbortFileRunner *plan,
    int item)
{
    int false_label = new_label();
    int join_label = new_label();

    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->graph_expected[item]);
    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->graph_characters[item]);
    mir_abort_runner_emit_call(out, plan->is_print_function, 1);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z, L%d\n", false_label);
    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->graph_characters[item]);
    mir_abort_runner_emit_call(out, plan->is_space_function, 1);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out,
            "\tjp nz, L%d\n\tld hl,1\n\tjp L%d\n"
            "L%d:\n\tld hl,0\nL%d:\n\tpush hl\n",
            false_label, join_label, false_label, join_label);
    mir_abort_runner_push_string(
        out, plan->strings[MIR_ABORT_GRAPH_FIRST + item]);
    mir_abort_runner_emit_call(out, plan->check_function, 3);
}

static void mir_emit_abort_file_runner(
    FILE *out, const struct MirAbortFileRunner *plan)
{
    int new_file_ok = new_label();
    int new_file_done = new_label();
    int old_file_done = new_label();
    int success = new_label();
    int done = new_label();
    int item;

    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-10\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    mir_abort_runner_open(
        out, plan,
        plan->strings[MIR_ABORT_OLD_NAME],
        plan->strings[MIR_ABORT_WRITE_MODE]);
    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n", out);
    mir_abort_runner_push_string(
        out, plan->strings[MIR_ABORT_CONTENT]);
    mir_abort_runner_emit_call(out, plan->puts_function, 2);
    mir_abort_runner_close(out, plan);

    fputs("\tld hl,0\n\tpush hl\n", out);
    mir_abort_runner_push_string(
        out, plan->strings[MIR_ABORT_NEW_NAME]);
    mir_abort_runner_push_string(
        out, plan->strings[MIR_ABORT_OLD_NAME]);
    mir_abort_runner_emit_call(out, plan->rename_function, 2);
    fputs("\tpush hl\n", out);
    mir_abort_runner_push_string(
        out, plan->strings[MIR_ABORT_RENAME_CHECK]);
    mir_abort_runner_emit_call(out, plan->check_function, 3);

    mir_abort_runner_open(
        out, plan,
        plan->strings[MIR_ABORT_NEW_NAME],
        plan->strings[MIR_ABORT_READ_MODE]);
    fputs("\tld a,(ix-2)\n\tor (ix-1)\n", out);
    fprintf(out, "\tjp nz, L%d\n", new_file_ok);
    mir_abort_runner_print(
        out, plan, plan->strings[MIR_ABORT_NEW_MISSING]);
    mir_abort_runner_increment_failures(out, plan);
    fprintf(out, "\tjp L%d\nL%d:\n",
            new_file_done, new_file_ok);
    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n"
          "\tld hl,8\n\tpush hl\n", out);
    mir_abort_runner_push_buffer(out);
    mir_abort_runner_emit_call(out, plan->gets_function, 3);
    mir_abort_runner_close(out, plan);

    fputs("\tld hl,0\n\tpush hl\n", out);
    mir_abort_runner_push_string(
        out, plan->strings[MIR_ABORT_CONTENT]);
    mir_abort_runner_push_buffer(out);
    mir_abort_runner_emit_call(out, plan->compare_function, 2);
    fputs("\tpush hl\n", out);
    mir_abort_runner_push_string(
        out, plan->strings[MIR_ABORT_CONTENT_CHECK]);
    mir_abort_runner_emit_call(out, plan->check_function, 3);
    fprintf(out, "L%d:\n", new_file_done);

    mir_abort_runner_open(
        out, plan,
        plan->strings[MIR_ABORT_OLD_NAME],
        plan->strings[MIR_ABORT_READ_MODE]);
    fputs("\tld a,(ix-2)\n\tor (ix-1)\n", out);
    fprintf(out, "\tjp z, L%d\n", old_file_done);
    mir_abort_runner_print(
        out, plan, plan->strings[MIR_ABORT_OLD_PRESENT]);
    mir_abort_runner_close(out, plan);
    mir_abort_runner_increment_failures(out, plan);
    fprintf(out, "L%d:\n", old_file_done);

    mir_abort_runner_push_string(
        out, plan->strings[MIR_ABORT_NEW_NAME]);
    mir_abort_runner_emit_call(out, plan->remove_function, 1);
    for (item = 0; item < 7; ++item)
        mir_abort_runner_check_graph(out, plan, item);

    mir_machine_emit_global_word(out, plan->failures, 0);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z, L%d\n", success);
    mir_machine_emit_global_word(out, plan->failures, 0);
    fputs("\tpush hl\n", out);
    mir_abort_runner_push_string(
        out, plan->strings[MIR_ABORT_FAILURE_SUMMARY]);
    mir_abort_runner_emit_call(out, plan->print_function, 2);
    fprintf(out, "\tld hl,1\n\tjp L%d\nL%d:\n",
            done, success);

    mir_abort_runner_print(
        out, plan, plan->strings[MIR_ABORT_SUCCESS]);
    mir_machine_emit_symbol_call(out, plan->abort_function);
    mir_abort_runner_print(
        out, plan, plan->strings[MIR_ABORT_RETURNED]);
    fprintf(out,
            "\tld hl,1\nL%d:\n"
            "\tld sp,ix\n\tpop ix\n\tret\n",
            done);
}

static void mir_file_io_push_string(FILE *out, int string_id)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
}

static void mir_read_validate_emit_call(
    FILE *out, struct Sym *function,
    const char *call_name, int words)
{
    if (!strcmp(
            call_name,
            asm_name_for(sym_asm_name(function))))
        mir_machine_emit_symbol_call(out, function);
    else
        fprintf(out, "\tcall %s\n", call_name);
    mir_emit_final_call_cleanup(out, words);
}

static void mir_read_validate_push_long_parameter(
    FILE *out, const struct MirReadValidateRunner *plan)
{
    int offset = plan->offset_stack_offset + 2;

    fprintf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld e,(ix+%d)\n\tld d,(ix+%d)\n"
            "\tpush de\n\tpush hl\n",
            offset, offset + 1, offset + 2, offset + 3);
}

static void mir_read_validate_print(
    FILE *out, const struct MirReadValidateRunner *plan,
    int call_slot, int string_slot)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[string_slot]);
    mir_read_validate_emit_call(
        out, plan->print_function,
        plan->print_names[call_slot], 1);
}

static void mir_read_validate_print_long(
    FILE *out, const struct MirReadValidateRunner *plan,
    int call_slot, int string_slot, int addend)
{
    int no_carry = new_label();
    int offset = plan->offset_stack_offset + 2;

    fprintf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld e,(ix+%d)\n\tld d,(ix+%d)\n",
            offset, offset + 1, offset + 2, offset + 3);
    if (addend != 0) {
        fprintf(out,
                "\tld bc,%d\n\tadd hl,bc\n"
                "\tjp nc,L%d\n\tinc de\nL%d:\n",
                addend, no_carry, no_carry);
    }
    fputs("\tpush de\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[string_slot]);
    mir_read_validate_emit_call(
        out, plan->print_function,
        plan->print_names[call_slot], 3);
}

static void mir_read_validate_buffer_check(
    FILE *out, const struct MirReadValidateRunner *plan,
    int buffer_offset, int expected,
    int call_slot, int string_slot)
{
    int equal = new_label();

    fprintf(out, "\tld a,(%s+%d)\n\tcp %d\n",
            asm_name_for(sym_asm_name(plan->buffer)),
            buffer_offset, expected);
    fprintf(out, "\tjp z,L%d\n", equal);
    mir_read_validate_print(
        out, plan, call_slot, string_slot);
    fprintf(out, "L%d:\n", equal);
}

static void mir_read_validate_offset_mismatch(
    FILE *out, const struct MirReadValidateRunner *plan,
    int value, int mismatch)
{
    int offset = plan->offset_stack_offset + 2;

    fprintf(out,
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
    FILE *out, const struct MirReadValidateRunner *plan)
{
    int result_nonzero = new_label();
    int not_fixed_middle = new_label();
    int not_file_end = new_label();
    int done = new_label();
    int stream = plan->stream_stack_offset + 2;
    int chunk = plan->chunk_stack_offset + 2;

    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-2\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    fprintf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n"
            "\tld hl,1\n\tpush hl\n"
            "\tld hl,%s\n\tpush hl\n",
            stream, stream + 1, chunk, chunk + 1,
            asm_name_for(sym_asm_name(plan->buffer)));
    mir_machine_emit_symbol_call(out, plan->read_function);
    mir_emit_final_call_cleanup(out, 4);
    fputs("\tld (ix-2),l\n\tld (ix-1),h\n"
          "\tpush hl\n", out);
    mir_read_validate_push_long_parameter(out, plan);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[0]);
    mir_read_validate_emit_call(
        out, plan->print_function,
        plan->print_names[0], 4);

    fputs("\tld a,(ix-2)\n\tor (ix-1)\n", out);
    fprintf(out, "\tjp nz,L%d\n", result_nonzero);
    mir_machine_emit_global_word(out, plan->error_object, 0);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[1]);
    mir_read_validate_emit_call(
        out, plan->print_function,
        plan->print_names[1], 2);
    fprintf(out, "\tjp L%d\nL%d:\n",
            done, result_nonzero);

    mir_read_validate_offset_mismatch(
        out, plan, 512, not_fixed_middle);
    mir_read_validate_buffer_check(
        out, plan, 0, 107, 2, 2);
    mir_read_validate_buffer_check(
        out, plan, 127, 107, 3, 3);
    mir_read_validate_buffer_check(
        out, plan, 128, 0, 4, 4);
    fprintf(out, "\tjp L%d\nL%d:\n",
            done, not_fixed_middle);

    mir_read_validate_offset_mismatch(
        out, plan, 8192, not_file_end);
    mir_read_validate_buffer_check(
        out, plan, 0, 106, 5, 5);
    mir_read_validate_buffer_check(
        out, plan, 127, 26, 6, 6);
    fprintf(out, "\tjp L%d\nL%d:\n",
            done, not_file_end);

    {
        int first_zero = new_label();
        int last_zero = new_label();

        fprintf(out, "\tld a,(%s+0)\n\tor a\n",
                asm_name_for(sym_asm_name(plan->buffer)));
        fprintf(out, "\tjp z,L%d\n", first_zero);
        mir_read_validate_print_long(
            out, plan, 7, 7, 0);
        fprintf(out, "L%d:\n", first_zero);
        fprintf(out, "\tld a,(%s+511)\n\tor a\n",
                asm_name_for(sym_asm_name(plan->buffer)));
        fprintf(out, "\tjp z,L%d\n", last_zero);
        mir_read_validate_print_long(
            out, plan, 8, 7, 511);
        fprintf(out, "L%d:\n", last_zero);
    }

    fprintf(out,
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            done);
}

static void mir_file_io_push_stream(FILE *out)
{
    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n", out);
}

static void mir_file_io_push_buffer(FILE *out)
{
    fputs("\tpush ix\n\tpop hl\n"
          "\tld de,-34\n\tadd hl,de\n\tpush hl\n", out);
}

static void mir_file_io_call(
    FILE *out, struct Sym *function, int words)
{
    mir_machine_emit_symbol_call(out, function);
    mir_emit_final_call_cleanup(out, words);
}

static void mir_file_io_print(
    FILE *out, const struct MirFileIoRunner *plan,
    int string_id)
{
    mir_file_io_push_string(out, string_id);
    mir_file_io_call(out, plan->print_function, 1);
}

static void mir_file_io_open(
    FILE *out, const struct MirFileIoRunner *plan,
    int name_id, int mode_id)
{
    mir_file_io_push_string(out, mode_id);
    mir_file_io_push_string(out, name_id);
    mir_file_io_call(out, plan->open_function, 2);
    fputs("\tld (ix-2),l\n\tld (ix-1),h\n", out);
}

static void mir_file_io_return_failure(
    FILE *out, const struct MirFileIoRunner *plan,
    int string_id, int done)
{
    mir_file_io_print(out, plan, string_id);
    fprintf(out, "\tld hl,1\n\tjp L%d\n", done);
}

static void mir_file_io_trim_line(FILE *out)
{
    int loop = new_label();
    int done = new_label();

    fputs("\tpush ix\n\tpop hl\n"
          "\tld de,-34\n\tadd hl,de\n", out);
    fprintf(out,
            "L%d:\n\tld a,(hl)\n\tor a\n\tjp z,L%d\n"
            "\tcp 10\n\tjp z,L%d\n\tcp 13\n\tjp z,L%d\n"
            "\tinc hl\n\tjp L%d\n"
            "L%d:\n\txor a\n\tld (hl),a\n",
            loop, done, done, done, loop, done);
}

static void mir_file_io_compare(
    FILE *out, const struct MirFileIoRunner *plan,
    int expected_id, int failure_id, int done)
{
    int equal = new_label();

    mir_file_io_push_string(out, expected_id);
    mir_file_io_push_buffer(out);
    mir_file_io_call(out, plan->compare_function, 2);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", equal);
    mir_file_io_push_buffer(out);
    mir_file_io_push_string(out, failure_id);
    mir_file_io_call(out, plan->print_function, 2);
    fprintf(out, "\tld hl,1\n\tjp L%d\nL%d:\n", done, equal);
}

static void mir_file_io_read_line(
    FILE *out, const struct MirFileIoRunner *plan,
    int failure_id, int done)
{
    int read = new_label();

    mir_file_io_push_stream(out);
    fputs("\tld hl,32\n\tpush hl\n", out);
    mir_file_io_push_buffer(out);
    mir_file_io_call(out, plan->read_function, 3);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", read);
    mir_file_io_return_failure(
        out, plan, failure_id, done);
    fprintf(out, "L%d:\n", read);
    mir_file_io_trim_line(out);
}

static void mir_emit_file_io_runner(
    FILE *out, const struct MirFileIoRunner *plan)
{
    int first_created = new_label();
    int second_created = new_label();
    int first_opened = new_label();
    int reopened = new_label();
    int missing_test_done = new_label();
    int missing_reopen_done = new_label();
    int done = new_label();

    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-34\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    mir_file_io_open(
        out, plan,
        plan->strings[MIR_FILE_IO_FIRST_NAME],
        plan->strings[MIR_FILE_IO_WRITE_MODE]);
    fputs("\tld a,(ix-2)\n\tor (ix-1)\n", out);
    fprintf(out, "\tjp nz,L%d\n", first_created);
    mir_file_io_return_failure(
        out, plan,
        plan->strings[MIR_FILE_IO_CREATE_FIRST_FAILURE], done);
    fprintf(out, "L%d:\n", first_created);
    mir_file_io_push_stream(out);
    mir_file_io_push_string(
        out, plan->strings[MIR_FILE_IO_FIRST_LINE]);
    mir_file_io_call(out, plan->write_function, 2);
    mir_file_io_push_stream(out);
    mir_file_io_call(out, plan->close_function, 1);

    mir_file_io_open(
        out, plan,
        plan->strings[MIR_FILE_IO_SECOND_NAME],
        plan->strings[MIR_FILE_IO_WRITE_MODE]);
    fputs("\tld a,(ix-2)\n\tor (ix-1)\n", out);
    fprintf(out, "\tjp nz,L%d\n", second_created);
    mir_file_io_return_failure(
        out, plan,
        plan->strings[MIR_FILE_IO_CREATE_SECOND_FAILURE], done);
    fprintf(out, "L%d:\n", second_created);
    mir_file_io_push_stream(out);
    mir_file_io_push_string(
        out, plan->strings[MIR_FILE_IO_SECOND_LINE]);
    mir_file_io_call(out, plan->write_function, 2);
    mir_file_io_push_stream(out);
    mir_file_io_call(out, plan->close_function, 1);

    mir_file_io_open(
        out, plan,
        plan->strings[MIR_FILE_IO_FIRST_NAME],
        plan->strings[MIR_FILE_IO_READ_MODE]);
    fputs("\tld a,(ix-2)\n\tor (ix-1)\n", out);
    fprintf(out, "\tjp nz,L%d\n", first_opened);
    mir_file_io_return_failure(
        out, plan,
        plan->strings[MIR_FILE_IO_OPEN_FIRST_FAILURE], done);
    fprintf(out, "L%d:\n", first_opened);
    mir_file_io_read_line(
        out, plan,
        plan->strings[MIR_FILE_IO_READ_FIRST_FAILURE], done);
    mir_file_io_compare(
        out, plan,
        plan->strings[MIR_FILE_IO_FIRST_CONTENT],
        plan->strings[MIR_FILE_IO_FIRST_CONTENT_FAILURE], done);

    mir_file_io_push_stream(out);
    mir_file_io_push_string(
        out, plan->strings[MIR_FILE_IO_READ_MODE]);
    mir_file_io_push_string(
        out, plan->strings[MIR_FILE_IO_SECOND_NAME]);
    mir_file_io_call(out, plan->reopen_function, 3);
    fputs("\tld (ix-2),l\n\tld (ix-1),h\n"
          "\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", reopened);
    mir_file_io_return_failure(
        out, plan,
        plan->strings[MIR_FILE_IO_REOPEN_FAILURE], done);
    fprintf(out, "L%d:\n", reopened);
    mir_file_io_read_line(
        out, plan,
        plan->strings[MIR_FILE_IO_READ_SECOND_FAILURE], done);
    mir_file_io_compare(
        out, plan,
        plan->strings[MIR_FILE_IO_SECOND_CONTENT],
        plan->strings[MIR_FILE_IO_SECOND_CONTENT_FAILURE], done);
    mir_file_io_push_stream(out);
    mir_file_io_call(out, plan->close_function, 1);

    mir_file_io_open(
        out, plan,
        plan->strings[MIR_FILE_IO_FIRST_NAME],
        plan->strings[MIR_FILE_IO_READ_MODE]);
    fputs("\tld a,(ix-2)\n\tor (ix-1)\n", out);
    fprintf(out, "\tjp z,L%d\n", missing_test_done);
    mir_file_io_push_stream(out);
    mir_file_io_push_string(
        out, plan->strings[MIR_FILE_IO_READ_MODE]);
    mir_file_io_push_string(
        out, plan->strings[MIR_FILE_IO_MISSING_NAME]);
    mir_file_io_call(out, plan->reopen_function, 3);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", missing_reopen_done);
    mir_file_io_return_failure(
        out, plan,
        plan->strings[MIR_FILE_IO_MISSING_FAILURE], done);
    fprintf(out, "L%d:\n", missing_reopen_done);
    fprintf(out, "L%d:\n", missing_test_done);

    mir_file_io_push_string(
        out, plan->strings[MIR_FILE_IO_FIRST_NAME]);
    mir_file_io_call(out, plan->remove_function, 1);
    mir_file_io_push_string(
        out, plan->strings[MIR_FILE_IO_SECOND_NAME]);
    mir_file_io_call(out, plan->remove_function, 1);
    mir_file_io_print(
        out, plan, plan->strings[MIR_FILE_IO_SUCCESS]);
    fputs("\tld hl,0\n", out);
    fprintf(out,
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            done);
}

static void mir_emit_callback_registration_runner(
    FILE *out, const struct MirCallbackRegistrationRunner *plan)
{
    static const int result_offsets[3] = {-2, -4, -6};
    int failure = new_label();
    int success = new_label();
    int done = new_label();
    int item;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-6\n\tadd hl,sp\n\tld sp,hl\n", out);
    for (item = 0; item < 3; ++item) {
        fprintf(out, "\tld hl,%s\n\tpush hl\n",
                asm_name_for(sym_asm_name(plan->callbacks[item])));
        mir_machine_emit_symbol_call(
            out, plan->register_function);
        fprintf(out,
                "\tpop bc\n"
                "\tld (ix%+d),l\n\tld (ix%+d),h\n",
                result_offsets[item], result_offsets[item] + 1);
    }
    for (item = 0; item < 2; ++item) {
        fprintf(out,
                "\tld a,(ix%+d)\n\tor (ix%+d)\n",
                result_offsets[item], result_offsets[item] + 1);
        fprintf(out, "\tjp nz,L%d\n", failure);
    }
    fprintf(out,
            "\tld a,(ix%+d)\n\tor (ix%+d)\n"
            "\tjp z,L%d\n"
            "L%d:\n\tld hl,S%d\n\tpush hl\n",
            result_offsets[2], result_offsets[2] + 1,
            success, failure, plan->failure_string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    fprintf(out, "\tpop bc\n\tjp L%d\nL%d:\n",
            done, success);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->success_string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    fprintf(out,
            "\tpop bc\nL%d:\n\tld hl,0\n"
            "\tld sp,ix\n\tpop ix\n\tret\n",
            done);
}

static void mir_emit_for_increment_runner(
    FILE *out, const struct MirForIncrementRunner *plan)
{
    static const int result_offsets[7] = {
        -2, -4, -6, -8, -10, -12, -14
    };
    int failure = new_label();
    int done = new_label();
    int item;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-14\n\tadd hl,sp\n\tld sp,hl\n", out);
    for (item = 0; item < 7; ++item) {
        if (item < 2 || item == 4) {
            fprintf(out, "\tld hl,%d\n\tpush hl\n",
                    item < 2 ? 4 : 5);
        } else if (item < 4) {
            fprintf(out,
                    "\tld hl,5\n\tpush hl\n"
                    "\tld hl,S%d\n\tpush hl\n",
                    plan->input_string_id);
        }
        mir_machine_emit_symbol_call(out, plan->helpers[item]);
        if (item < 2 || item == 4)
            fputs("\tpop bc\n", out);
        else if (item < 4)
            fputs("\tpop bc\n\tpop bc\n", out);
        fprintf(out,
                "\tld (ix%+d),l\n\tld (ix%+d),h\n",
                result_offsets[item], result_offsets[item] + 1);
    }

    for (item = 6; item >= 0; --item) {
        fprintf(out,
                "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n",
                result_offsets[item], result_offsets[item] + 1);
    }
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->format_string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    for (item = 0; item < 8; ++item)
        fputs("\tpop bc\n", out);

    for (item = 0; item < 7; ++item) {
        fprintf(out,
                "\tld a,(ix%+d)\n\txor %d\n"
                "\tld l,a\n\tld a,(ix%+d)\n\tor l\n"
                "\tjp nz,L%d\n",
                result_offsets[item], plan->expected[item] & 255,
                result_offsets[item] + 1, failure);
    }
    fprintf(out,
            "\tld hl,0\n\tjp L%d\n"
            "L%d:\n\tld hl,1\n"
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            done, failure, done);
}

static int mir_comma_word_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
           (type & 15) == TYPE_INT &&
           (type & TYPE_UNSIGNED) == 0 &&
           type_size(type) == 2;
}

static int mir_comma_local_word(
    const struct MirInsn *insn, int pointer, int *offset_out)
{
    int type;
    int storage;
    int offset;

    if (!mir_machine_named_nonvolatile(insn) ||
        !mir_scalar_memory_location(
            insn, &type, &storage, &offset) ||
        storage != SC_LOCAL ||
        (pointer ? !mir_comma_word_pointer_type(type)
                 : !mir_gnarly_word_type(type, 0)) ||
        insn->memory_size != 2 ||
        insn->bit_width != 0)
        return 0;
    if (offset_out != NULL)
        *offset_out = offset;
    return 1;
}

static int mir_comma_report_call(
    struct MirCommaLoopRunner *plan, int slot,
    int call_instruction, int string_instruction,
    int argument_count, const int *definitions)
{
    const struct MirInsn *call = &mir.insns[call_instruction];
    const struct MirInsn *string = &mir.insns[string_instruction];
    struct Sym *function;
    const char *assembly_name;
    int arguments[4];
    int argument;

    if (slot < 0 || slot >= 7 ||
        call->opcode != MIR_CALL || call->src1 >= 0 ||
        call->secondary_offset != slot ||
        call->memory_flags != MIR_CALL_FLAG_VARIADIC ||
        !mir_gnarly_word_type(call->type, 0) ||
        string->opcode != MIR_STRING_ADDRESS ||
        !mir_gnarly_string_type(string->type) ||
        string->immediate < 0 ||
        !mir_machine_call_arguments(
            call, argument_count, arguments) ||
        (function = find_global(call->name)) == NULL ||
        function->storage != SC_FUNC ||
        function->is_funcptr || function->is_noreturn ||
        !function->has_proto || function->proto_nargs != 1 ||
        !function->proto_variadic ||
        !mir_gnarly_string_type(function->proto_types[0]) ||
        !mir_gnarly_word_type(function->type, 0) ||
        (plan->print_function != NULL &&
         plan->print_function != function))
        return 0;
    assembly_name = asm_name_for(sym_asm_name(function));
    if (call->base_name[0] != 0 &&
        strcmp(call->base_name, assembly_name))
        return 0;
    for (argument = 0; argument < argument_count; ++argument)
        if (!mir_gnarly_value_from(
                arguments[argument], definitions[argument]))
            return 0;
    plan->print_function = function;
    plan->strings[slot] = (int)string->immediate;
    return 1;
}

static int mir_match_comma_loop_runner(
    struct MirCommaLoopRunner *plan)
{
    static const char expected_opcodes[] =
        "LCSCSCSCSCNSCNSANSLPPNCBFNDRBNSLNCBSDCBSJLTGNGNGDABCBNGKANSCNSCNSLPPNCBFNDRBNSLNCBNSDCBSJLTGNGNGDABC"
        "BNGKCNSCNSANSLPPNCBFNDRBNSDCBSNLNCBSJLTGNGNGDABCBNGKCNSCNSANSCNSLPPPNCBSDCBSNCBFNDNCIRBNSLNCBSJLTGNG"
        "NGDABCBNGKCNSCNSCNSLPPPNCBSNCBFNANIRBNSNCBSNLJLTGNGNGKANSDCBSDRNSTGNGDABCBNGKCNSANSNCBSDCBSUTGNGDABC"
        "BNGKNCBFNCBFLCJLCLPFDACCBBBFLCJLCLPFCLJLCLLPE";
    static const int constants[] = {
        1, 3, 5, 7, 9, 12, 22, 33, 37, 51, 59, 62, 69, 80,
        85, 99, 104, 107, 117, 127, 133, 147, 152, 155, 161,
        169, 173, 177, 183, 191, 205, 210, 213, 216, 224, 228,
        240, 258, 272, 277, 284, 288, 299, 305, 309, 313, 316,
        322, 323, 329, 332, 336, 340
    };
    static const long constant_values[] = {
        10, 20, 30, 0, 0, 0, 3, 1, 2, 2, 0, 0, 3, 1, 2, 2,
        0, 0, 3, 2, 1, 2, 0, 0, 0, 1, 2, 3, 65535, 1, 2, 0,
        0, 0, 1, 3, 1, 2, 2, 0, 1, 2, 2, 60, 1, 1, 0, 1, 2,
        1, 0, 0, 1
    };
    static const int pointer_constants[] = {
        37, 85, 127, 173, 258, 288
    };
    static const int untyped_constants[] = {
        313, 316, 329, 332
    };
    static const int binaries[][4] = {
        {23, 20, 22, '<'}, {28, 19, 27, '+'},
        {34, 20, 33, '+'}, {38, 36, 37, '+'},
        {50, 48, 49, '-'}, {52, 50, 51, '/'},
        {70, 67, 69, '<'}, {75, 66, 74, '+'},
        {81, 67, 80, '+'}, {86, 84, 85, '+'},
        {98, 96, 97, '-'}, {100, 98, 99, '/'},
        {118, 115, 117, '<'}, {123, 114, 122, '+'},
        {128, 126, 127, '+'}, {134, 115, 133, '+'},
        {146, 144, 145, '-'}, {148, 146, 147, '/'},
        {170, 167, 169, '+'}, {174, 172, 173, '+'},
        {178, 166, 177, '<'}, {186, 165, 185, '+'},
        {192, 166, 191, '+'}, {204, 202, 203, '-'},
        {206, 204, 205, '/'}, {225, 222, 224, '+'},
        {229, 221, 228, '<'}, {236, 220, 235, '+'},
        {241, 221, 240, '+'}, {259, 257, 258, '+'},
        {271, 269, 270, '-'}, {273, 271, 272, '/'},
        {285, 277, 284, '+'}, {289, 287, 288, '+'},
        {298, 296, 297, '-'}, {300, 298, 299, '/'},
        {306, 262, 305, TOK_EQ}, {310, 285, 309, TOK_EQ},
        {324, 322, 323, '*'}, {325, 321, 324, '+'},
        {326, 320, 325, TOK_EQ}
    };
    static const int pointer_binaries[] = {
        38, 86, 128, 174, 259, 289, 325
    };
    static const int stores[][2] = {
        {2, 1}, {4, 3}, {6, 5}, {8, 7},
        {11, 9}, {14, 12}, {17, 15}, {30, 28},
        {35, 34}, {39, 38}, {58, 56}, {61, 59},
        {64, 62}, {77, 75}, {83, 81}, {87, 86},
        {106, 104}, {109, 107}, {112, 110}, {125, 123},
        {129, 128}, {135, 134}, {154, 152}, {157, 155},
        {160, 158}, {163, 161}, {171, 170}, {175, 174},
        {188, 186}, {193, 192}, {212, 210}, {215, 213},
        {218, 216}, {226, 225}, {238, 236}, {242, 241},
        {256, 254}, {260, 259}, {264, 262}, {279, 277},
        {282, 280}, {286, 285}, {290, 289}
    };
    static const int sum_locations[] = {
        11, 19, 30, 61, 66, 77, 106, 114, 125, 154, 165, 188,
        212, 220, 238, 264, 10, 25, 29, 44, 60, 72, 76, 92,
        105, 120, 124, 140, 153, 180, 187, 198, 211, 231, 237,
        249, 263, 267, 304
    };
    static const int index_locations[] = {
        14, 20, 35, 64, 67, 83, 109, 115, 135, 163, 166, 193,
        218, 221, 242, 279, 286, 13, 21, 32, 46, 63, 68, 79,
        82, 94, 108, 116, 132, 142, 162, 176, 190, 217, 227,
        233, 239, 278, 283, 294, 308
    };
    static const int tick_locations[] = {
        157, 167, 171, 215, 222, 226,
        156, 168, 200, 214, 223, 251
    };
    static const int pointer_locations[] = {
        17, 26, 36, 39, 48, 58, 73, 84, 87, 96, 112, 121,
        126, 129, 144, 160, 172, 175, 181, 202, 256, 257, 260,
        261, 269, 282, 287, 290, 296, 320, 16, 57, 111, 159,
        255, 281
    };
    static const int array_addresses[] = {
        15, 49, 56, 97, 110, 145, 158, 203, 232, 254,
        270, 280, 297, 321
    };
    static const int phis[][5] = {
        {19, 9, 28, 0, 31}, {20, 12, 34, 0, 31},
        {66, 59, 75, 41, 78}, {67, 62, 81, 41, 78},
        {114, 104, 123, 89, 131}, {115, 107, 134, 89, 131},
        {165, 152, 186, 137, 189}, {166, 161, 192, 137, 189},
        {167, 155, 170, 137, 189},
        {220, 210, 236, 195, 244}, {221, 216, 241, 195, 244},
        {222, 213, 225, 195, 244},
        {318, 313, 316, 312, 315},
        {334, 329, 332, 328, 331},
        {343, 336, 340, 337, 341}
    };
    static const int branches[][3] = {
        {24, 23, 41}, {71, 70, 89}, {119, 118, 137},
        {179, 178, 195}, {230, 229, 246},
        {307, 306, 315}, {311, 310, 315},
        {319, 318, 331}, {327, 326, 331}, {335, 334, 339}
    };
    static const int jumps[][2] = {
        {40, 18}, {88, 65}, {136, 113}, {194, 164},
        {245, 219}, {314, 317}, {330, 333}, {338, 342}
    };
    static const int indirect_loads[][2] = {
        {27, 26}, {74, 73}, {122, 121},
        {185, 184}, {235, 234}, {262, 261}
    };
    static const int first_report[] = {42, 19, 20, 52};
    static const int second_report[] = {90, 66, 67, 100};
    static const int third_report[] = {138, 114, 115, 148};
    static const int condition_report[] = {196, 165, 170, 206};
    static const int while_report[] = {247, 220, 225};
    static const int value_report[] = {265, 262, 273};
    static const int statement_report[] = {292, 285, 300};
    int scalar_offsets[4];
    int array_storage;
    int array_offset;
    int array_type;
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 345 || mir_cfg_block_count() != 26 ||
        mir.has_vla || mir.local_bytes != 20 ||
        mir.aggregate_temp_bytes != 0 ||
        mir.object_count != 3 ||
        !mir_gnarly_word_type(mir.return_type, 0))
        return mir_machine_reject(
            "comma-loop-runner", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir_gnarly_opcode_code(
                mir.insns[instruction].opcode) !=
                expected_opcodes[instruction])
            return mir_machine_reject(
                "comma-loop-runner", "opcode");

    for (item = 0;
         item < (int)(sizeof(constants) / sizeof(constants[0]));
         ++item) {
        const struct MirInsn *constant = &mir.insns[constants[item]];
        int pointer = 0;
        int untyped = 0;
        int search;

        if (!mir_machine_constant_equals(
                constant->dst, constant_values[item]))
            return mir_machine_reject(
                "comma-loop-runner", "constant");
        for (search = 0;
             search < (int)(sizeof(pointer_constants) /
                            sizeof(pointer_constants[0]));
             ++search)
            if (constants[item] == pointer_constants[search])
                pointer = 1;
        for (search = 0;
             search < (int)(sizeof(untyped_constants) /
                            sizeof(untyped_constants[0]));
             ++search)
            if (constants[item] == untyped_constants[search])
                untyped = 1;
        if ((pointer &&
             !mir_comma_word_pointer_type(constant->type)) ||
            (!pointer && !untyped &&
             !mir_gnarly_word_type(constant->type, 0)) ||
            (untyped && constant->type != 0))
            return mir_machine_reject(
                "comma-loop-runner", "constant-type");
    }

    for (item = 0;
         item < (int)(sizeof(binaries) / sizeof(binaries[0]));
         ++item) {
        const struct MirInsn *binary = &mir.insns[binaries[item][0]];
        int pointer = 0;
        int search;

        if (!mir_gnarly_binary(
                binaries[item][0], binaries[item][1],
                binaries[item][2], binaries[item][3]))
            return mir_machine_reject(
                "comma-loop-runner", "binary");
        for (search = 0;
             search < (int)(sizeof(pointer_binaries) /
                            sizeof(pointer_binaries[0]));
             ++search)
            if (binaries[item][0] == pointer_binaries[search])
                pointer = 1;
        if ((pointer &&
             !mir_comma_word_pointer_type(binary->type)) ||
            (!pointer && !mir_gnarly_word_type(binary->type, 0)))
            return mir_machine_reject(
                "comma-loop-runner", "binary-type");
    }

    for (item = 0;
         item < (int)(sizeof(stores) / sizeof(stores[0]));
         ++item)
        if (mir.insns[stores[item][0]].src1 !=
                mir.insns[stores[item][1]].dst ||
            mir.insns[stores[item][0]].memory_size != 2 ||
            (mir.insns[stores[item][0]].memory_flags & (1 | 8)) != 0)
            return mir_machine_reject(
                "comma-loop-runner", "store");

    if (!mir_comma_local_word(&mir.insns[11], 0, &scalar_offsets[0]) ||
        !mir_comma_local_word(&mir.insns[14], 0, &scalar_offsets[1]) ||
        !mir_comma_local_word(&mir.insns[157], 0, &scalar_offsets[2]) ||
        !mir_comma_local_word(&mir.insns[17], 1, &scalar_offsets[3]) ||
        scalar_offsets[0] == scalar_offsets[1] ||
        scalar_offsets[0] == scalar_offsets[2] ||
        scalar_offsets[0] == scalar_offsets[3] ||
        scalar_offsets[1] == scalar_offsets[2] ||
        scalar_offsets[1] == scalar_offsets[3] ||
        scalar_offsets[2] == scalar_offsets[3])
        return mir_machine_reject(
            "comma-loop-runner", "locals");
    for (item = 0;
         item < (int)(sizeof(sum_locations) /
                      sizeof(sum_locations[0]));
         ++item)
        if (!mir_machine_same_location(
                &mir.insns[11],
                &mir.insns[sum_locations[item]]))
            return mir_machine_reject(
                "comma-loop-runner", "sum-location");
    for (item = 0;
         item < (int)(sizeof(index_locations) /
                      sizeof(index_locations[0]));
         ++item)
        if (!mir_machine_same_location(
                &mir.insns[14],
                &mir.insns[index_locations[item]]))
            return mir_machine_reject(
                "comma-loop-runner", "index-location");
    for (item = 0;
         item < (int)(sizeof(tick_locations) /
                      sizeof(tick_locations[0]));
         ++item)
        if (!mir_machine_same_location(
                &mir.insns[157],
                &mir.insns[tick_locations[item]]))
            return mir_machine_reject(
                "comma-loop-runner", "tick-location");
    for (item = 0;
         item < (int)(sizeof(pointer_locations) /
                      sizeof(pointer_locations[0]));
         ++item)
        if (!mir_machine_same_location(
                &mir.insns[17],
                &mir.insns[pointer_locations[item]]))
            return mir_machine_reject(
                "comma-loop-runner", "pointer-location");

    if (!mir_scalar_memory_location(
            &mir.insns[2], &array_type,
            &array_storage, &array_offset) ||
        array_storage != SC_LOCAL ||
        !mir_gnarly_word_type(array_type, 0) ||
        !mir_machine_named_nonvolatile(&mir.insns[2]) ||
        mir.insns[2].immediate != 0)
        return mir_machine_reject(
            "comma-loop-runner", "array");
    for (item = 0; item < 4; ++item) {
        const struct MirInsn *store = &mir.insns[2 + item * 2];
        int element_type;
        int element_storage;
        int element_offset;

        if (strcmp(store->name, mir.insns[2].name) ||
            store->immediate != item * 2 ||
            !mir_scalar_memory_location(
                store, &element_type,
                &element_storage, &element_offset) ||
            element_storage != array_storage ||
            element_offset != array_offset + item * 2 ||
            !mir_gnarly_word_type(element_type, 0))
            return mir_machine_reject(
                "comma-loop-runner", "array-store");
        plan->values[item] =
            (int)mir.insns[1 + item * 2].immediate;
    }
    if (!strcmp(mir.insns[2].name, mir.insns[11].name) ||
        !strcmp(mir.insns[2].name, mir.insns[14].name) ||
        !strcmp(mir.insns[2].name, mir.insns[157].name) ||
        !strcmp(mir.insns[2].name, mir.insns[17].name))
        return mir_machine_reject(
            "comma-loop-runner", "array-alias");
    for (item = 0; item < 4; ++item)
        if (scalar_offsets[item] < array_offset + 8 &&
            scalar_offsets[item] + 2 > array_offset)
            return mir_machine_reject(
                "comma-loop-runner", "array-overlap");
    for (item = 0;
         item < (int)(sizeof(array_addresses) /
                      sizeof(array_addresses[0]));
         ++item) {
        const struct MirInsn *address =
            &mir.insns[array_addresses[item]];

        if (!mir_machine_named_nonvolatile(address) ||
            strcmp(address->name, mir.insns[2].name) ||
            !mir_comma_word_pointer_type(address->type) ||
            address->secondary_offset != 0)
            return mir_machine_reject(
                "comma-loop-runner", "array-address");
    }

    for (item = 0;
         item < (int)(sizeof(phis) / sizeof(phis[0]));
         ++item)
        if (!mir_gnarly_phi(
                phis[item][0], phis[item][1], phis[item][2],
                phis[item][3], phis[item][4]))
            return mir_machine_reject(
                "comma-loop-runner", "phi");
    for (item = 0;
         item < (int)(sizeof(branches) / sizeof(branches[0]));
         ++item)
        if (!mir_gnarly_branch(
                branches[item][0], branches[item][1],
                branches[item][2]))
            return mir_machine_reject(
                "comma-loop-runner", "branch");
    for (item = 0;
         item < (int)(sizeof(jumps) / sizeof(jumps[0]));
         ++item)
        if (mir.insns[jumps[item][0]].label !=
                mir.insns[jumps[item][1]].label)
            return mir_machine_reject(
                "comma-loop-runner", "jump");

    for (item = 0;
         item < (int)(sizeof(indirect_loads) /
                      sizeof(indirect_loads[0]));
         ++item) {
        const struct MirInsn *load =
            &mir.insns[indirect_loads[item][0]];

        if (!mir_gnarly_value_from(
                load->src1, indirect_loads[item][1]) ||
            !mir_gnarly_word_type(load->type, 0) ||
            load->memory_size != 2 ||
            (load->memory_flags & (1 | 8)) != 0)
            return mir_machine_reject(
                "comma-loop-runner", "indirect-load");
    }
    if (!mir_gnarly_value_from(mir.insns[184].src1, 181) ||
        !mir_gnarly_value_from(mir.insns[184].src2, 183) ||
        !mir_comma_word_pointer_type(mir.insns[184].type) ||
        mir.insns[184].immediate != 2 ||
        mir.insns[184].memory_size != 2 ||
        !mir_gnarly_value_from(mir.insns[234].src1, 232) ||
        !mir_gnarly_value_from(mir.insns[234].src2, 221) ||
        !mir_comma_word_pointer_type(mir.insns[234].type) ||
        mir.insns[234].immediate != 2 ||
        mir.insns[234].memory_size != 2 ||
        !mir_gnarly_value_from(mir.insns[291].src1, 287) ||
        (mir.insns[291].type & 15) != TYPE_VOID ||
        mir.insns[291].immediate != 0)
        return mir_machine_reject(
            "comma-loop-runner", "index-update");

    if (!mir_comma_report_call(
            plan, 0, 55, 42, 4, first_report) ||
        !mir_comma_report_call(
            plan, 1, 103, 90, 4, second_report) ||
        !mir_comma_report_call(
            plan, 2, 151, 138, 4, third_report) ||
        !mir_comma_report_call(
            plan, 3, 209, 196, 4, condition_report) ||
        !mir_comma_report_call(
            plan, 4, 253, 247, 3, while_report) ||
        !mir_comma_report_call(
            plan, 5, 276, 265, 3, value_report) ||
        !mir_comma_report_call(
            plan, 6, 303, 292, 3, statement_report))
        return mir_machine_reject(
            "comma-loop-runner", "report-call");
    for (item = 0; item < 7; ++item) {
        int previous;

        for (previous = 0; previous < item; ++previous)
            if (plan->strings[item] == plan->strings[previous])
                return mir_machine_reject(
                    "comma-loop-runner", "report-string");
    }

    if (mir.insns[344].src1 != mir.insns[343].dst)
        return mir_machine_reject(
            "comma-loop-runner", "return");
    return 1;
}

static int mir_scope_long_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           (type & 15) == TYPE_LONG &&
           (type & TYPE_UNSIGNED) == 0 &&
           type_size(type) == 4;
}

static int mir_scope_function_types(
    const struct MirScopeBlockRunner *plan)
{
    int helper;

    if (type_ptr_depth(plan->check_function->type) != 0 ||
        (plan->check_function->type & 15) != TYPE_VOID ||
        !mir_scope_long_type(
            plan->check_function->proto_types[0]) ||
        !mir_scope_long_type(
            plan->check_function->proto_types[1]) ||
        !mir_abort_runner_pointer_type(
            plan->check_function->proto_types[2], TYPE_CHAR) ||
        !mir_abort_runner_word_type(
            plan->parameter_function->type) ||
        !mir_abort_runner_word_type(
            plan->parameter_function->proto_types[0]) ||
        !mir_abort_runner_word_type(plan->print_function->type) ||
        !mir_abort_runner_pointer_type(
            plan->print_function->proto_types[0], TYPE_CHAR))
        return 0;
    for (helper = 0; helper < 3; ++helper)
        if (!mir_abort_runner_word_type(
                plan->helper_functions[helper]->type))
            return 0;
    return 1;
}

static int mir_scope_block_instruction(int profile_instruction)
{
    if (profile_instruction < 552)
        return profile_instruction;
    if (profile_instruction < 633)
        return profile_instruction - 2;
    return profile_instruction - 4;
}

static int mir_match_scope_block_runner(
    struct MirScopeBlockRunner *plan)
{
    static const char expected_opcodes[] =
        "LNNNNNCSNCSNGCGTGKNNGCGTGKNNNNNNNNNNCSNCSNNBNSNNCSNNBNSNNGCGTGKNNNNNNNNCSNCSNCSNGCGTGKNNGCGTGKNNGCGT"
        "GKNNNNNNCSNCSNGCGTGKNNGCGTGKNNNNNNNNNNCSNCSLNNNNNNNNNPPNNCBFNCSNNBNSNLNCBSJLNGCGTGKNNNNNNNNCSNCSLNNN"
        "NNNNNNNNNNPNNCBFNCSNCBFDCBSLNLNCBSJLDGCGTGKNCGKUGCGTGKNNNNNCSNCBFNCSNGCGTGKNLNGCGTGKNNNNNNNNNNNCSNCS"
        "NNCSLNNNNNNNNNNNNNNNNNPNPNNCBFNNSNNBNSNCBSNLJLNGCGTGKNGCGTGKNNNNNNNNCSNCSNCBFJLJLNCSNNSNJNLNCNSNLPGC"
        "GTGKNNNNNNNNNNCSNCSNNBNSNNCSNNBNSNNGCGTGKNNNNNNNNNNCSNCSLNNNNNNNNNNNNNNNNNNNNNNNNNNPNNCBFNCSLNNNNNNN"
        "NNNNNNNNNNNNNNNNNNNNNDCBFDDBNSLDCBSJLLNCBSJLDGCGTGKNNNNNNNNNCSNNCSNCSLNNNNNNNNNPNNNNNNNNNNNNNNNNNNPN"
        "NCBFNNBNSLNCBSJLNGCGTGKNGCGTGKNKUGCGTGKKUGCGTGKKUGCGTGKKUGCGTGKKUGCGTGKKUGCGTGKDCBFLTGKJLTGDGKLDCBE";
    static const int constant_instructions[79] = {
        6, 9, 13, 21, 36, 39, 48, 58, 71, 74, 77, 81, 89, 97, 108, 111, 115,
        123, 138, 141, 157, 161, 171, 178, 191, 194, 213, 217, 220, 224, 231,
        238, 244, 249, 259, 262, 266, 270, 279, 295, 298, 302, 327, 339, 348,
        355, 368, 371, 374, 382, 392, 399, 414, 417, 426, 436, 451, 454, 486,
        490, 522, 532, 539, 546, 562, 566, 569, 603, 613, 620, 627, 638, 646,
        654, 662, 670, 678, 684, 700
    };
    static const long constant_values[79] = {
        10, 20, 20, 10, 0, 3, 4, 7, 1, 2, 3, 3, 2, 1, 100, 100000, 100000,
        100, 0, 0, 3, 50, 1, 150, 0, 0, 4, 999, 999, 1, 1, 4, 7, 12, 1, 1, 8,
        8, 1, 0, 77, 0, 2, 1, 77, 1, 1, 0, 1, 11, 65535, 11, 0, 5, 9, 14, 0,
        0, 3, 0, 2, 1, 1, 3, 42, 0, 0, 5, 1, 10, 42, 112, 114, 741, 742, 7,
        9, 0, 0
    };
    static const unsigned char constant_types[79] = {
        2, 2, 4, 4, 4, 2, 2, 4, 2, 2, 2, 4, 4, 4, 2, 4, 4, 4, 4, 2, 2, 2, 0,
        4, 2, 2, 2, 4, 4, 0, 0, 4, 2, 4, 2, 2, 2, 4, 4, 2, 2, 4, 2, 0, 4, 4,
        2, 2, 0, 2, 2, 4, 4, 2, 2, 4, 4, 2, 2, 2, 2, 0, 0, 4, 2, 4, 2, 2, 0,
        4, 4, 4, 4, 4, 4, 4, 4, 2, 2
    };
    static const int binaries[26][4] = {
        {43, 36, 39, '+'}, {52, 43, 48, '+'},
        {158, 154, 157, '<'}, {165, 153, 161, '+'},
        {172, 154, 171, '+'}, {214, 210, 213, '<'},
        {221, 217, 220, TOK_EQ}, {225, 223, 224, '+'},
        {232, 210, 231, '+'}, {263, 259, 262, TOK_EQ},
        {328, 322, 327, '<'}, {335, 324, 322, '+'},
        {340, 322, 339, '+'}, {375, 368, 374, TOK_EQ},
        {421, 414, 417, '+'}, {430, 421, 426, '+'},
        {487, 483, 486, '<'}, {523, 521, 522, '<'},
        {527, 525, 526, '+'}, {533, 531, 532, '+'},
        {540, 483, 539, '+'}, {604, 600, 603, '<'},
        {608, 581, 600, '+'}, {614, 600, 613, '+'},
        {685, 683, 684, TOK_EQ}, {701, 699, 700, TOK_NE}
    };
    static const unsigned char binary_types[26] = {
        4, 4, 2, 4, 0, 2, 2, 0, 0, 2, 2, 4, 0,
        2, 4, 4, 2, 2, 4, 0, 0, 2, 4, 0, 2, 2
    };
    static const unsigned char binary_widths[26] = {
        4, 4, 2, 4, 0, 2, 4, 0, 0, 2, 2, 4, 0,
        0, 4, 4, 2, 2, 4, 0, 0, 2, 4, 0, 2, 2
    };
    static const int unaries[7][2] = {
        {247, 246}, {636, 635}, {644, 643}, {652, 651},
        {660, 659}, {668, 667}, {676, 675}
    };
    static const int phis[9][5] = {
        {153, 138, 165, 0, 169},
        {154, 141, 172, 0, 169},
        {210, 194, 232, 175, 229},
        {322, 295, 340, 276, 343},
        {324, 302, 335, 276, 343},
        {397, 382, 392, 380, 390},
        {483, 454, 540, 396, 537},
        {581, 566, 608, 543, 611},
        {600, 569, 614, 543, 611}
    };
    static const unsigned char phi_types[9] = {
        4, 2, 2, 2, 4, 2, 2, 4, 2
    };
    static const unsigned char phi_objects[9] = {
        9, 10, 13, 17, 19, 22, 26, 9, 29
    };
    static const int branches[10][3] = {
        {159, 158, 175}, {215, 214, 235},
        {222, 221, 227}, {264, 263, 276},
        {329, 328, 345}, {376, 375, 390},
        {488, 487, 543}, {524, 523, 536},
        {605, 604, 617}, {686, 685, 692}
    };
    static const int jumps[10][2] = {
        {174, 143}, {234, 196}, {344, 304}, {377, 380},
        {379, 390}, {388, 396}, {535, 492}, {542, 456},
        {616, 571}, {691, 698}
    };
    static const int object_types[30] = {
        TYPE_INT, TYPE_INT, TYPE_LONG, TYPE_INT, TYPE_INT,
        TYPE_INT, TYPE_INT, TYPE_INT, TYPE_LONG, TYPE_LONG,
        TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_LONG,
        TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_LONG,
        TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_LONG,
        TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT
    };
    static const int stores[][4] = {
        {7, 0, 6, 2}, {10, 1, 9, 2}, {37, 2, 36, 4},
        {40, 3, 39, 2}, {45, 2, 43, 4}, {49, 3, 48, 2},
        {54, 2, 52, 4}, {72, 4, 71, 2}, {75, 5, 74, 2},
        {78, 6, 77, 2}, {109, 7, 108, 2}, {112, 8, 111, 4},
        {139, 9, 138, 4}, {142, 10, 141, 2},
        {162, 11, 161, 2}, {167, 9, 165, 4},
        {173, 10, 172, 2}, {192, 12, 191, 2},
        {195, 13, 194, 2}, {218, 14, 217, 4},
        {226, 12, 225, 2}, {233, 13, 232, 2},
        {260, 15, 259, 2}, {267, 16, 266, 2},
        {296, 17, 295, 2}, {299, 18, 298, 2},
        {303, 19, 302, 4}, {332, 20, 322, 2},
        {337, 19, 335, 4}, {341, 17, 340, 2},
        {369, 21, 368, 2}, {372, 22, 371, 2},
        {383, 23, 382, 2}, {386, 22, 382, 2},
        {394, 22, 392, 2}, {415, 24, 414, 4},
        {418, 25, 417, 2}, {423, 24, 421, 4},
        {427, 25, 426, 2}, {432, 24, 430, 4},
        {452, 9, 451, 4}, {455, 26, 454, 2},
        {491, 27, 490, 2}, {529, 9, 527, 4},
        {534, 27, 533, 2}, {541, 26, 540, 2},
        {563, 28, 562, 2},
        {567, 9, 566, 4}, {570, 29, 569, 2},
        {610, 9, 608, 4}, {615, 29, 614, 2}
    };
    static const int loads[][2] = {
        {223, 12}, {236, 12}, {521, 27}, {525, 9},
        {526, 27}, {531, 27}, {544, 9}
    };
    static const int check_calls[26] = {
        17, 25, 62, 85, 93, 101, 119, 127, 182, 242, 253,
        274, 283, 352, 359, 403, 440, 550, 624, 631, 642,
        650, 658, 666, 674, 682
    };
    static const int check_call_ids[26] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 14,
        15, 16, 17, 18, 19, 20, 21, 23, 25, 27, 29, 31
    };
    static const int check_arguments[26][3] = {
        {9, 13, 15}, {6, 21, 23}, {52, 58, 60},
        {77, 81, 83}, {74, 89, 91}, {71, 97, 99},
        {111, 115, 117}, {108, 123, 125},
        {153, 178, 180}, {236, 238, 240},
        {247, 249, 251}, {266, 270, 272},
        {259, 279, 281}, {298, 348, 350},
        {324, 355, 357}, {397, 399, 401},
        {430, 436, 438}, {544, 546, 548},
        {581, 620, 622}, {562, 627, 629},
        {636, 638, 640}, {644, 646, 648},
        {652, 654, 656}, {660, 662, 664},
        {668, 670, 672}, {676, 678, 680}
    };
    static const int check_string_instructions[26] = {
        15, 23, 60, 83, 91, 99, 117, 125, 180, 240, 251,
        272, 281, 350, 357, 401, 438, 548, 622, 629, 640,
        648, 656, 664, 672, 680
    };
    static const int helper_calls[6] = {
        635, 643, 651, 659, 667, 675
    };
    static const int helper_call_ids[6] = {
        22, 24, 26, 28, 30, 32
    };
    int arguments[3];
    int instruction;
    int item;
    int previous;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 699 || mir_cfg_block_count() != 28 ||
        mir.sink_purpose != EMIT_SINK_FINAL ||
        mir.has_vla || mir.local_bytes != 84 ||
        mir.aggregate_temp_bytes != 0 || mir.object_count != 30 ||
        !mir_abort_runner_word_type(mir.return_type) ||
        strlen(expected_opcodes) != (size_t)mir.count)
        return mir_machine_reject(
            "scope-block-runner", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir_gnarly_opcode_code(
                mir.insns[instruction].opcode) !=
                expected_opcodes[instruction])
            return mir_machine_reject(
                "scope-block-runner", "opcode");
    for (item = 0; item < 79; ++item) {
        int constant =
            mir_scope_block_instruction(
                constant_instructions[item]);

        if (!mir_machine_constant_equals(
                mir.insns[constant].dst,
                constant_values[item]) ||
            mir.insns[constant].type !=
                constant_types[item])
            return mir_machine_reject(
                "scope-block-runner", "constant");
    }
    for (item = 0; item < 26; ++item)
        if (!mir_gnarly_binary(
                mir_scope_block_instruction(binaries[item][0]),
                mir_scope_block_instruction(binaries[item][1]),
                mir_scope_block_instruction(binaries[item][2]),
                binaries[item][3]) ||
            mir.insns[mir_scope_block_instruction(
                binaries[item][0])].type !=
                binary_types[item] ||
            mir.insns[mir_scope_block_instruction(
                binaries[item][0])].secondary_offset !=
                binary_widths[item])
            return mir_machine_reject(
                "scope-block-runner", "binary");
    for (item = 0; item < 7; ++item)
        if (mir.insns[mir_scope_block_instruction(
                unaries[item][0])].opcode != MIR_UNARY ||
            !mir_gnarly_value_from(
                mir.insns[mir_scope_block_instruction(
                    unaries[item][0])].src1,
                mir_scope_block_instruction(unaries[item][1])) ||
            mir.insns[mir_scope_block_instruction(
                unaries[item][0])].immediate != 0 ||
            !mir_scope_long_type(
                mir.insns[mir_scope_block_instruction(
                    unaries[item][0])].type))
            return mir_machine_reject(
                "scope-block-runner", "conversion");
    for (item = 0; item < 9; ++item)
        if (!mir_gnarly_phi(
                mir_scope_block_instruction(phis[item][0]),
                mir_scope_block_instruction(phis[item][1]),
                mir_scope_block_instruction(phis[item][2]),
                mir_scope_block_instruction(phis[item][3]),
                mir_scope_block_instruction(phis[item][4])) ||
            mir.insns[mir_scope_block_instruction(
                phis[item][0])].type != phi_types[item] ||
            mir.insns[mir_scope_block_instruction(
                phis[item][0])].object != phi_objects[item])
            return mir_machine_reject(
                "scope-block-runner", "phi");
    for (item = 0; item < 10; ++item)
        if (!mir_gnarly_branch(
                mir_scope_block_instruction(branches[item][0]),
                mir_scope_block_instruction(branches[item][1]),
                mir_scope_block_instruction(branches[item][2])) ||
            mir.insns[mir_scope_block_instruction(
                jumps[item][0])].label !=
                mir.insns[mir_scope_block_instruction(
                    jumps[item][1])].label)
            return mir_machine_reject(
                "scope-block-runner", "control-flow");

    for (item = 0; item < 30; ++item)
        if (mir.objects[item].storage != SC_LOCAL ||
            mir.objects[item].type != object_types[item] ||
            mir.objects[item].is_register)
            return mir_machine_reject(
                "scope-block-runner", "object");
    for (item = 0;
         item < (int)(sizeof(stores) / sizeof(stores[0]));
         ++item) {
        const struct MirInsn *store =
            &mir.insns[mir_scope_block_instruction(
                stores[item][0])];

        if (store->opcode != MIR_STORE ||
            store->object != stores[item][1] ||
            !mir_gnarly_value_from(
                store->src1,
                mir_scope_block_instruction(
                    stores[item][2])) ||
            store->memory_size != stores[item][3] ||
            (store->memory_flags & (1 | 8)) != 0 ||
            !mir_machine_named_nonvolatile(store))
            return mir_machine_reject(
                "scope-block-runner", "store");
    }
    for (item = 0;
         item < (int)(sizeof(loads) / sizeof(loads[0]));
         ++item) {
        const struct MirInsn *load =
            &mir.insns[mir_scope_block_instruction(
                loads[item][0])];

        if (load->opcode != MIR_LOAD ||
            load->object != loads[item][1] ||
            load->type !=
                object_types[loads[item][1]] ||
            !mir_machine_named_nonvolatile(load))
            return mir_machine_reject(
                "scope-block-runner", "load");
    }

    plan->check_function =
        mir_abort_runner_function(
            mir_scope_block_instruction(17), 0, 3, 0);
    plan->parameter_function =
        mir_abort_runner_function(
            mir_scope_block_instruction(246), 0, 1, 0);
    plan->helper_functions[0] =
        mir_abort_runner_function(
            mir_scope_block_instruction(635), 0, 0, 0);
    plan->helper_functions[1] =
        mir_abort_runner_function(
            mir_scope_block_instruction(651), 0, 0, 0);
    plan->helper_functions[2] =
        mir_abort_runner_function(
            mir_scope_block_instruction(667), 0, 0, 0);
    plan->print_function =
        mir_abort_runner_function(
            mir_scope_block_instruction(690), 1, 1, 0);
    if (plan->check_function == NULL ||
        plan->parameter_function == NULL ||
        plan->helper_functions[0] == NULL ||
        plan->helper_functions[1] == NULL ||
        plan->helper_functions[2] == NULL ||
        plan->print_function == NULL ||
        !plan->check_function->is_defined ||
        !plan->check_function->is_static ||
        !plan->parameter_function->is_defined ||
        !plan->parameter_function->is_static ||
        !plan->helper_functions[0]->is_defined ||
        !plan->helper_functions[0]->is_static ||
        !plan->helper_functions[1]->is_defined ||
        !plan->helper_functions[1]->is_static ||
        !plan->helper_functions[2]->is_defined ||
        !plan->helper_functions[2]->is_static ||
        !mir_scope_function_types(plan))
        return mir_machine_reject(
            "scope-block-runner", "function");
    if (plan->check_function == plan->parameter_function ||
        plan->check_function == plan->helper_functions[0] ||
        plan->check_function == plan->helper_functions[1] ||
        plan->check_function == plan->helper_functions[2] ||
        plan->parameter_function == plan->helper_functions[0] ||
        plan->parameter_function == plan->helper_functions[1] ||
        plan->parameter_function == plan->helper_functions[2] ||
        plan->helper_functions[0] == plan->helper_functions[1] ||
        plan->helper_functions[0] == plan->helper_functions[2] ||
        plan->helper_functions[1] == plan->helper_functions[2])
        return mir_machine_reject(
            "scope-block-runner", "function-alias");

    for (item = 0; item < 26; ++item) {
        const struct MirInsn *string =
            &mir.insns[mir_scope_block_instruction(
                check_string_instructions[item])];
        int check_arguments_direct[3];

        check_arguments_direct[0] =
            mir_scope_block_instruction(
                check_arguments[item][0]);
        check_arguments_direct[1] =
            mir_scope_block_instruction(
                check_arguments[item][1]);
        check_arguments_direct[2] =
            mir_scope_block_instruction(
                check_arguments[item][2]);

        if (!mir_abort_runner_call(
                mir_scope_block_instruction(check_calls[item]),
                plan->check_function,
                check_call_ids[item], 3,
                check_arguments_direct) ||
            !mir_abort_runner_pointer_type(
                string->type, TYPE_CHAR) ||
            string->immediate < 0)
            return mir_machine_reject(
                "scope-block-runner", "check-call");
        plan->check_strings[item] = (int)string->immediate;
        for (previous = 0; previous < item; ++previous)
            if (plan->check_strings[item] ==
                plan->check_strings[previous])
                return mir_machine_reject(
                    "scope-block-runner", "check-string");
    }
    arguments[0] = mir_scope_block_instruction(244);
    if (!mir_abort_runner_call(
            mir_scope_block_instruction(246),
            plan->parameter_function, 11, 1, arguments))
        return mir_machine_reject(
            "scope-block-runner", "parameter-call");
    for (item = 0; item < 6; ++item)
        if (!mir_abort_runner_no_argument_call(
                mir_scope_block_instruction(helper_calls[item]),
                plan->helper_functions[item / 2],
                helper_call_ids[item]))
            return mir_machine_reject(
                "scope-block-runner", "helper-call");

    arguments[0] = mir_scope_block_instruction(688);
    if (!mir_abort_runner_call(
            mir_scope_block_instruction(690),
            plan->print_function, 33, 1, arguments))
        return mir_machine_reject(
            "scope-block-runner", "success-call");
    arguments[0] = mir_scope_block_instruction(693);
    arguments[1] = mir_scope_block_instruction(695);
    if (!mir_abort_runner_call(
            mir_scope_block_instruction(697),
            plan->print_function, 34, 2, arguments))
        return mir_machine_reject(
            "scope-block-runner", "failure-call");
    if (!mir_abort_runner_pointer_type(
            mir.insns[mir_scope_block_instruction(688)].type,
            TYPE_CHAR) ||
        !mir_abort_runner_pointer_type(
            mir.insns[mir_scope_block_instruction(693)].type,
            TYPE_CHAR) ||
        mir.insns[mir_scope_block_instruction(688)].immediate < 0 ||
        mir.insns[mir_scope_block_instruction(693)].immediate < 0)
        return mir_machine_reject(
            "scope-block-runner", "summary-string");
    plan->success_string =
        (int)mir.insns[mir_scope_block_instruction(688)].immediate;
    plan->failure_string =
        (int)mir.insns[mir_scope_block_instruction(693)].immediate;
    for (item = 0; item < 26; ++item)
        if (plan->success_string == plan->check_strings[item] ||
            plan->failure_string == plan->check_strings[item])
            return mir_machine_reject(
                "scope-block-runner", "summary-alias");
    if (plan->success_string == plan->failure_string)
        return mir_machine_reject(
            "scope-block-runner", "summary-alias");

    plan->failures = find_global(
        mir.insns[mir_scope_block_instruction(683)].name);
    if (plan->failures == NULL ||
        plan->failures->storage != SC_GLOBAL ||
        !plan->failures->is_defined ||
        !plan->failures->is_static ||
        plan->failures->is_array ||
        plan->failures->is_volatile ||
        !mir_abort_runner_word_type(plan->failures->type) ||
        mir.insns[mir_scope_block_instruction(683)].object >= 0 ||
        !mir_machine_same_location(
            &mir.insns[mir_scope_block_instruction(683)],
            &mir.insns[mir_scope_block_instruction(695)]) ||
        !mir_machine_same_location(
            &mir.insns[mir_scope_block_instruction(683)],
            &mir.insns[mir_scope_block_instruction(699)]) ||
        mir.insns[mir_scope_block_instruction(686)].label !=
            mir.insns[mir_scope_block_instruction(692)].label ||
        mir.insns[mir_scope_block_instruction(691)].label !=
            mir.insns[mir_scope_block_instruction(698)].label ||
        mir.insns[mir_scope_block_instruction(702)].src1 !=
            mir.insns[mir_scope_block_instruction(701)].dst)
        return mir_machine_reject(
            "scope-block-runner", "result-flow");
    return 1;
}

enum {
    MIR_COMMA_ARRAY = -8,
    MIR_COMMA_SCRATCH = -10,
    MIR_COMMA_FINAL_I = -11,
    MIR_COMMA_FINAL_PTR = -13,
    MIR_COMMA_FRAME_BYTES = 13
};

static void mir_comma_array_address(FILE *out)
{
    fputs("\tpush ix\n\tpop hl\n"
          "\tld de,-8\n\tadd hl,de\n\tex de,hl\n", out);
}

static void mir_comma_increment_scratch(FILE *out)
{
    int done = new_label();

    fprintf(out,
            "\tinc (ix%d)\n\tjp nz,L%d\n"
            "\tinc (ix%d)\nL%d:\n",
            MIR_COMMA_SCRATCH, done,
            MIR_COMMA_SCRATCH + 1, done);
}

static void mir_comma_push_pointer_difference(FILE *out)
{
    fputs("\tpush bc\n\tpush de\n\tpush ix\n\tpop hl\n"
          "\tld bc,-8\n\tadd hl,bc\n\tpop de\n"
          "\tex de,hl\n\tor a\n\tsbc hl,de\n"
          "\tsra h\n\trr l\n\tex de,hl\n\tpop bc\n"
          "\tex de,hl\n\tpush hl\n", out);
}

static void mir_comma_report(
    FILE *out, const struct MirCommaLoopRunner *plan,
    int slot, int argument_count)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[slot]);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_gnarly_cleanup(out, argument_count);
}

static void mir_comma_push_index(FILE *out)
{
    fputs("\tld l,a\n\tld h,0\n\tpush hl\n", out);
}

static void mir_comma_push_sum(FILE *out)
{
    fputs("\tld l,c\n\tld h,b\n\tpush hl\n", out);
}

static void mir_emit_comma_sum_loop(
    FILE *out, const struct MirCommaLoopRunner *plan,
    int report_slot, int pointer_first)
{
    int loop = new_label();
    int done = new_label();

    mir_comma_array_address(out);
    fputs("\tld bc,0\n\txor a\n", out);
    fprintf(out, "L%d:\n\tcp 3\n\tjp nc,L%d\n",
            loop, done);
    fputs("\tpush af\n\tld a,(de)\n\tld l,a\n\tinc de\n"
          "\tld a,(de)\n\tld h,a\n\tdec de\n"
          "\tadd hl,bc\n\tld b,h\n\tld c,l\n\tpop af\n",
          out);
    if (pointer_first)
        fputs("\tinc de\n\tinc de\n\tinc a\n", out);
    else
        fputs("\tinc a\n\tinc de\n\tinc de\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n", loop, done);
    mir_comma_push_pointer_difference(out);
    mir_comma_push_index(out);
    mir_comma_push_sum(out);
    mir_comma_report(out, plan, report_slot, 4);
}

static void mir_emit_comma_condition_loop(
    FILE *out, const struct MirCommaLoopRunner *plan)
{
    int loop = new_label();
    int done = new_label();

    fputs("\tld (ix-10),0\n\tld (ix-9),0\n", out);
    mir_comma_array_address(out);
    fputs("\tld bc,0\n\txor a\n", out);
    fprintf(out, "L%d:\n", loop);
    mir_comma_increment_scratch(out);
    fputs("\tinc de\n\tinc de\n\tcp 3\n", out);
    fprintf(out, "\tjp nc,L%d\n", done);
    fputs("\tpush af\n\tdec de\n\tld a,(de)\n\tld h,a\n"
          "\tdec de\n\tld a,(de)\n\tld l,a\n"
          "\tinc de\n\tinc de\n\tadd hl,bc\n"
          "\tld b,h\n\tld c,l\n\tpop af\n\tinc a\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n", loop, done);
    mir_comma_push_pointer_difference(out);
    fputs("\tld l,(ix-10)\n\tld h,(ix-9)\n\tpush hl\n", out);
    mir_comma_push_sum(out);
    mir_comma_report(out, plan, 3, 4);
}

static void mir_emit_comma_while_loop(
    FILE *out, const struct MirCommaLoopRunner *plan)
{
    int loop = new_label();
    int done = new_label();

    fputs("\tld (ix-10),0\n\tld (ix-9),0\n", out);
    mir_comma_array_address(out);
    fputs("\tld bc,0\n\txor a\n", out);
    fprintf(out, "L%d:\n", loop);
    mir_comma_increment_scratch(out);
    fputs("\tcp 3\n", out);
    fprintf(out, "\tjp nc,L%d\n", done);
    fputs("\tpush af\n\tld a,(de)\n\tld l,a\n\tinc de\n"
          "\tld a,(de)\n\tld h,a\n\tinc de\n"
          "\tadd hl,bc\n\tld b,h\n\tld c,l\n"
          "\tpop af\n\tinc a\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n", loop, done);
    fputs("\tld l,(ix-10)\n\tld h,(ix-9)\n\tpush hl\n", out);
    mir_comma_push_sum(out);
    mir_comma_report(out, plan, 4, 3);
}

static void mir_emit_comma_value_and_statement(
    FILE *out, const struct MirCommaLoopRunner *plan)
{
    mir_comma_array_address(out);
    fputs("\tinc de\n\tinc de\n"
          "\tld a,(de)\n\tld c,a\n\tinc de\n"
          "\tld a,(de)\n\tld b,a\n\tdec de\n"
          "\tld (ix-10),c\n\tld (ix-9),b\n", out);
    mir_comma_push_pointer_difference(out);
    mir_comma_push_sum(out);
    mir_comma_report(out, plan, 5, 3);

    mir_comma_array_address(out);
    fputs("\txor a\n\tinc a\n\tinc de\n\tinc de\n"
          "\tld (ix-11),a\n\tld (ix-13),e\n\tld (ix-12),d\n",
          out);
    mir_comma_push_pointer_difference(out);
    mir_comma_push_index(out);
    mir_comma_report(out, plan, 6, 3);
}

static void mir_emit_comma_loop_runner(
    FILE *out, const struct MirCommaLoopRunner *plan)
{
    int failure = new_label();
    int done = new_label();
    int item;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    fprintf(out,
            "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n",
            MIR_COMMA_FRAME_BYTES);
    for (item = 0; item < 4; ++item)
        fprintf(out,
                "\tld hl,%d\n\tld (ix%+d),l\n\tld (ix%+d),h\n",
                plan->values[item],
                MIR_COMMA_ARRAY + item * 2,
                MIR_COMMA_ARRAY + item * 2 + 1);

    mir_emit_comma_sum_loop(out, plan, 0, 0);
    mir_emit_comma_sum_loop(out, plan, 1, 0);
    mir_emit_comma_sum_loop(out, plan, 2, 1);
    mir_emit_comma_condition_loop(out, plan);
    mir_emit_comma_while_loop(out, plan);
    mir_emit_comma_value_and_statement(out, plan);

    fputs("\tld l,(ix-10)\n\tld h,(ix-9)\n"
          "\tld de,60\n\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp nz,L%d\n", failure);
    fputs("\tld a,(ix-11)\n\tcp 1\n", out);
    fprintf(out, "\tjp nz,L%d\n", failure);
    fputs("\tld e,(ix-13)\n\tld d,(ix-12)\n"
          "\tpush de\n\tpush ix\n\tpop hl\n\tld bc,-8\n"
          "\tadd hl,bc\n\tpop de\n\tex de,hl\n\tor a\n"
          "\tsbc hl,de\n\tdec hl\n\tdec hl\n"
          "\tld a,h\n\tor l\n", out);
    fprintf(out,
            "\tjp nz,L%d\n\tld hl,0\n\tjp L%d\n"
            "L%d:\n\tld hl,1\nL%d:\n"
            "\tld sp,ix\n\tpop ix\n\tret\n",
            failure, done, failure, done);
}

static void mir_scope_check_constant(
    FILE *out, const struct MirScopeBlockRunner *plan,
    int check, unsigned long got, unsigned long expected)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->check_strings[check]);
    mir_emit_final_call_constant(out, expected, 4);
    mir_emit_final_call_constant(out, got, 4);
    mir_machine_emit_symbol_call(out, plan->check_function);
    mir_gnarly_cleanup(out, 5);
}

static void mir_scope_check_word(
    FILE *out, const struct MirScopeBlockRunner *plan,
    int check, unsigned long expected)
{
    fputs("\tld b,h\n\tld c,l\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->check_strings[check]);
    mir_emit_final_call_constant(out, expected, 4);
    fputs("\tld h,b\n\tld l,c\n"
          "\tld a,h\n\trlca\n\tsbc a,a\n"
          "\tld d,a\n\tld e,a\n\tpush de\n\tpush hl\n",
          out);
    mir_machine_emit_symbol_call(out, plan->check_function);
    mir_gnarly_cleanup(out, 5);
}

static void mir_scope_emit_shadow_for(
    FILE *out, const struct MirScopeBlockRunner *plan)
{
    int loop = new_label();
    int done = new_label();

    fputs("\tld bc,0\n\txor a\n", out);
    fprintf(out, "L%d:\n\tcp 3\n\tjp nc,L%d\n",
            loop, done);
    fputs("\tpush af\n\tld hl,50\n\tadd hl,bc\n"
          "\tld b,h\n\tld c,l\n\tpop af\n\tinc a\n",
          out);
    fprintf(out, "\tjp L%d\nL%d:\n", loop, done);
    fputs("\tld h,b\n\tld l,c\n", out);
    mir_scope_check_word(out, plan, 8, 150);
}

static void mir_scope_emit_count_for(
    FILE *out, const struct MirScopeBlockRunner *plan)
{
    int loop = new_label();
    int skip = new_label();
    int done = new_label();

    fputs("\tld bc,0\n\txor a\n", out);
    fprintf(out, "L%d:\n\tcp 4\n\tjp nc,L%d\n",
            loop, done);
    fputs("\tpush af\n\tpush bc\n"
          "\tld hl,999\n\tld de,999\n"
          "\tor a\n\tsbc hl,de\n\tpop bc\n", out);
    fprintf(out, "\tjp nz,L%d\n\tinc bc\nL%d:\n",
            skip, skip);
    fputs("\tpop af\n\tinc a\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n", loop, done);
    fputs("\tld h,b\n\tld l,c\n", out);
    mir_scope_check_word(out, plan, 9, 4);
}

static void mir_scope_emit_while(
    FILE *out, const struct MirScopeBlockRunner *plan)
{
    int loop = new_label();
    int done = new_label();

    fputs("\tld bc,0\n\txor a\n", out);
    fprintf(out, "L%d:\n\tcp 2\n\tjp nc,L%d\n",
            loop, done);
    fputs("\tpush af\n\tld l,a\n\tld h,0\n"
          "\tadd hl,bc\n\tld b,h\n\tld c,l\n"
          "\tpop af\n\tinc a\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n", loop, done);
    fputs("\tpush bc\n", out);
    mir_scope_check_constant(out, plan, 13, 77, 77);
    fputs("\tpop bc\n", out);
    fputs("\tld h,b\n\tld l,c\n", out);
    mir_scope_check_word(out, plan, 14, 1);
}

static void mir_scope_emit_nested_for(
    FILE *out, const struct MirScopeBlockRunner *plan)
{
    int outer = new_label();
    int inner = new_label();
    int inner_done = new_label();
    int done = new_label();

    fputs("\tld bc,0\n\tld d,0\n", out);
    fprintf(out,
            "L%d:\n\tld a,d\n\tcp 3\n\tjp nc,L%d\n"
            "\txor a\n"
            "L%d:\n\tcp 2\n\tjp nc,L%d\n",
            outer, done, inner, inner_done);
    fputs("\tpush af\n\tld l,a\n\tld h,0\n"
          "\tadd hl,bc\n\tld b,h\n\tld c,l\n"
          "\tpop af\n\tinc a\n", out);
    fprintf(out,
            "\tjp L%d\nL%d:\n\tinc d\n\tjp L%d\nL%d:\n",
            inner, inner_done, outer, done);
    fputs("\tld h,b\n\tld l,c\n", out);
    mir_scope_check_word(out, plan, 17, 3);
}

static void mir_scope_emit_for_init(
    FILE *out, const struct MirScopeBlockRunner *plan)
{
    int loop = new_label();
    int done = new_label();

    fputs("\tld bc,0\n\txor a\n", out);
    fprintf(out, "L%d:\n\tcp 5\n\tjp nc,L%d\n",
            loop, done);
    fputs("\tpush af\n\tld l,a\n\tld h,0\n"
          "\tadd hl,bc\n\tld b,h\n\tld c,l\n"
          "\tpop af\n\tinc a\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n", loop, done);
    fputs("\tld h,b\n\tld l,c\n", out);
    mir_scope_check_word(out, plan, 18, 10);
    mir_scope_check_constant(out, plan, 19, 42, 42);
}

static void mir_scope_emit_helper_checks(
    FILE *out, const struct MirScopeBlockRunner *plan)
{
    static const unsigned long expected[6] = {
        112, 114, 741, 742, 7, 9
    };
    int helper;

    for (helper = 0; helper < 6; ++helper) {
        mir_machine_emit_symbol_call(
            out, plan->helper_functions[helper / 2]);
        mir_scope_check_word(
            out, plan, 20 + helper, expected[helper]);
    }
}

static void mir_emit_scope_block_runner(
    FILE *out, const struct MirScopeBlockRunner *plan)
{
    int skip_inner = new_label();
    int switch_default = new_label();
    int switch_done = new_label();
    int failure = new_label();
    int summary_done = new_label();
    int return_done = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);

    mir_scope_check_constant(out, plan, 0, 20, 20);
    mir_scope_check_constant(out, plan, 1, 10, 10);
    fputs("\tld hl,3\n\tld de,4\n\tadd hl,de\n", out);
    mir_scope_check_word(out, plan, 2, 7);
    mir_scope_check_constant(out, plan, 3, 3, 3);
    mir_scope_check_constant(out, plan, 4, 2, 2);
    mir_scope_check_constant(out, plan, 5, 1, 1);
    mir_scope_check_constant(out, plan, 6, 100000, 100000);
    mir_scope_check_constant(out, plan, 7, 100, 100);

    mir_scope_emit_shadow_for(out, plan);
    mir_scope_emit_count_for(out, plan);

    fputs("\tld hl,7\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->parameter_function);
    mir_gnarly_cleanup(out, 1);
    mir_scope_check_word(out, plan, 10, 12);

    fputs("\tld a,1\n\tcp 1\n", out);
    fprintf(out, "\tjp nz,L%d\n", skip_inner);
    mir_scope_check_constant(out, plan, 11, 8, 8);
    fprintf(out, "L%d:\n", skip_inner);
    mir_scope_check_constant(out, plan, 12, 1, 1);

    mir_scope_emit_while(out, plan);

    fputs("\tld hl,0\n\tld a,1\n\tcp 1\n", out);
    fprintf(out, "\tjp nz,L%d\n\tld hl,11\n\tjp L%d\n"
            "L%d:\n\tld hl,-1\nL%d:\n",
            switch_default, switch_done,
            switch_default, switch_done);
    mir_scope_check_word(out, plan, 15, 11);

    fputs("\tld hl,5\n\tld de,9\n\tadd hl,de\n", out);
    mir_scope_check_word(out, plan, 16, 14);

    mir_scope_emit_nested_for(out, plan);
    mir_scope_emit_for_init(out, plan);
    mir_scope_emit_helper_checks(out, plan);

    mir_machine_emit_global_word(out, plan->failures, 0);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", failure);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->success_string);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_gnarly_cleanup(out, 1);
    fprintf(out, "\tjp L%d\nL%d:\n",
            summary_done, failure);
    mir_machine_emit_global_word(out, plan->failures, 0);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->failure_string);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_gnarly_cleanup(out, 2);
    fprintf(out, "L%d:\n", summary_done);

    mir_machine_emit_global_word(out, plan->failures, 0);
    fputs("\tld a,h\n\tor l\n\tld hl,0\n", out);
    fprintf(out, "\tjp z,L%d\n\tinc hl\nL%d:\n"
            "\tld sp,ix\n\tpop ix\n\tret\n",
            return_done, return_done);
}

struct MirDirectoryEnumerationRunner {
    struct Sym *initialize_function;
    struct Sym *bdos_function;
    struct Sym *duplicate_function;
    struct Sym *sort_function;
    struct Sym *search_function;
    struct Sym *print_function;
    struct Sym *size_function;
    struct Sym *free_function;
    struct Sym *compare_function;
    struct Sym *quiet;
    char list_name[64];
    int strings[4];
    int parameter_stack_offset;
};

static struct Sym *mir_directory_function(
    int instruction, int variadic, int argument_count)
{
    const struct MirInsn *call = &mir.insns[instruction];
    struct Sym *function;
    const char *assembly_name;

    if (call->opcode != MIR_CALL || call->src1 >= 0 ||
        call->memory_flags !=
            (variadic ? MIR_CALL_FLAG_VARIADIC : 0) ||
        (function = find_global(call->name)) == NULL ||
        function->storage != SC_FUNC || function->is_funcptr ||
        function->is_noreturn || !function->has_proto ||
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

static int mir_directory_call(
    int instruction, struct Sym *function, int ordinal,
    int argument_count, const int *definitions)
{
    const struct MirInsn *call = &mir.insns[instruction];
    int arguments[5];
    int argument;

    if (function == NULL || argument_count > 5 ||
        find_global(call->name) != function ||
        call->secondary_offset != ordinal ||
        !mir_machine_call_arguments(
            call, argument_count, arguments))
        return 0;
    for (argument = 0; argument < argument_count; ++argument)
        if (arguments[argument] !=
            mir.insns[definitions[argument]].dst)
            return 0;
    return 1;
}

static int mir_directory_string(int instruction, int *id_out)
{
    const struct MirInsn *string = &mir.insns[instruction];

    if (string->opcode != MIR_STRING_ADDRESS ||
        !mir_abort_runner_pointer_type(string->type, TYPE_CHAR) ||
        string->immediate < 0)
        return 0;
    *id_out = (int)string->immediate;
    return 1;
}

static int mir_directory_local_address(
    int instruction, int pointer_depth, int base_type,
    int is_struct)
{
    const struct MirInsn *address = &mir.insns[instruction];

    return address->opcode == MIR_ADDRESS &&
           address->object < 0 &&
           (address->memory_flags & (1 | 8)) == 0 &&
           type_ptr_depth(address->type) == pointer_depth &&
           ((address->type & TYPE_STRUCT) != 0) == is_struct &&
           (is_struct || (address->type & 15) == base_type) &&
           type_size(address->type) == 2 &&
           mir_machine_named_nonvolatile(address);
}

static struct Sym *mir_directory_global_address(
    int instruction, int pointer_depth, int base_type)
{
    const struct MirInsn *address = &mir.insns[instruction];
    struct Sym *symbol;

    if (address->opcode != MIR_ADDRESS ||
        address->object >= 0 || address->memory_flags != 0 ||
        type_ptr_depth(address->type) != pointer_depth ||
        (address->type & 15) != base_type ||
        type_size(address->type) != 2 ||
        !mir_machine_named_nonvolatile(address) ||
        (symbol = find_global(address->name)) == NULL)
        return NULL;
    return symbol;
}

static int mir_directory_same_list_address(
    int instruction, const char *root_name)
{
    const struct MirInsn *address = &mir.insns[instruction];
    const char *separator;

    return address->opcode == MIR_ADDRESS &&
           address->object < 0 && address->memory_flags == 0 &&
           type_ptr_depth(address->type) == 2 &&
           (address->type & 15) == TYPE_CHAR &&
           type_size(address->type) == 2 &&
           mir_machine_named_nonvolatile(address) &&
           (separator = strchr(address->name, '#')) != NULL &&
           separator != address->name && separator[1] != 0 &&
           strchr(separator + 1, '#') == NULL &&
           strcmp(separator + 1, root_name) == 0;
}

static int mir_match_directory_enumeration_runner(
    struct MirDirectoryEnumerationRunner *plan)
{
    static const char expected_opcodes[] =
        "LQTSCNSANGDGKUFCELCGANGKNSLDPPNCBFNCBFLCJLCLPFCNCBBNUSCNSCNSLDNNNNDCBFCD"
        "MDIRUBFLADCBSIDMDIRWJLNJLNLDCBSJLCDMCIRUBFADCBSINCWCNSLDNNNNDCBFCDMDIRUB"
        "FLADCBSIDMDIRWJLNJLNLDCBSJLNLADINCWADCBSIAGKWCCBNNBFNJLCGANGKNSNLJLCDBFT"
        "GDGKCENLANGDGCNGAGKANGANGDGCNGAGKUSCDBFTGDRGKLCNSLDNNNPNDNBFANIRGKNSDUFT"
        "GNGANIRGNNGKLANIRNGKNLNCBSJLCE";
    static const int constant_instructions[37] = {
        4, 15, 18, 31, 35, 39, 42, 46, 48, 54, 57, 67, 70, 82,
        100, 105, 108, 116, 121, 123, 133, 136, 148, 166,
        177, 181, 189, 190, 199, 211, 220, 229, 243, 251,
        262, 311, 316
    };
    static const long constant_values[37] = {
        0, 0, 17, 0, 3, 1, 0, 128, 32, 0, 0, 8, 32, 1,
        1, 32, 0, 1, 46, 0, 3, 32, 1, 1,
        0, 1, 800, 2, 18, 255, 0, 2, 2, 0,
        0, 1, 1
    };
    static const struct {
        int instruction;
        int left;
        int right;
        int operation;
    } binaries[21] = {
        {32, 29, 31, TOK_GE}, {36, 29, 35, TOK_LE},
        {49, 29, 48, '*'}, {50, 46, 49, '+'},
        {68, 66, 67, '<'}, {77, 70, 76, TOK_NE},
        {83, 81, 82, '+'}, {101, 99, 100, '+'},
        {112, 105, 111, TOK_NE}, {117, 115, 116, '+'},
        {134, 132, 133, '<'}, {143, 136, 142, TOK_NE},
        {149, 147, 148, '+'}, {167, 165, 166, '+'},
        {182, 180, 181, '+'}, {191, 189, 190, '/'},
        {194, 191, 182, TOK_EQ}, {213, 211, 212, TOK_NE},
        {253, 251, 252, TOK_NE}, {274, 270, 272, '<'},
        {312, 270, 311, '+'}
    };
    static const struct {
        int instruction;
        int function;
        int argument_count;
        int definitions[5];
    } calls[11] = {
        {12, 0, 2, {7, 10, 0, 0, 0}},
        {23, 1, 2, {18, 20, 0, 0, 0}},
        {187, 2, 1, {185, 0, 0, 0, 0}},
        {204, 1, 2, {199, 201, 0, 0, 0}},
        {219, 5, 2, {215, 217, 0, 0, 0}},
        {234, 3, 4, {224, 227, 229, 232, 0}},
        {248, 4, 5, {235, 238, 241, 243, 246}},
        {260, 5, 2, {255, 258, 0, 0, 0}},
        {281, 6, 1, {279, 0, 0, 0, 0}},
        {299, 5, 4, {287, 270, 294, 281, 0}},
        {307, 7, 1, {304, 0, 0, 0, 0}}
    };
    static const int fcb_addresses[3] = {7, 20, 201};
    static const int file_addresses[5] = {80, 114, 146, 173, 185};
    static const int list_addresses[6] = {179, 224, 238, 276, 291, 301};
    static const int result_fcb_uses[6] = {53, 71, 86, 106, 137, 152};
    static const int len_uses[8] = {56, 81, 84, 115, 118, 147, 150, 174};
    static const int result_uses[6] = {25, 47, 206, 212, 217, 29};
    struct Sym *functions[8];
    int call_count = 0;
    int instruction;
    int item;
    int previous;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 318 || mir_cfg_block_count() != 28 ||
        mir.sink_purpose != EMIT_SINK_FINAL ||
        mir.has_vla || mir.local_bytes != 69 ||
        mir.aggregate_temp_bytes != 0 || mir.object_count != 5 ||
        !mir_abort_runner_word_type(mir.return_type) ||
        strlen(expected_opcodes) != (size_t)mir.count)
        return mir_machine_reject(
            "directory-enumeration-runner", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        if (mir_gnarly_opcode_code(
                mir.insns[instruction].opcode) !=
                expected_opcodes[instruction])
            return mir_machine_reject(
                "directory-enumeration-runner", "opcode");
        if (mir.insns[instruction].opcode == MIR_CALL)
            ++call_count;
    }
    if (call_count != 11)
        return mir_machine_reject(
            "directory-enumeration-runner", "call-count");
    for (item = 0; item < 37; ++item)
        if (!mir_machine_constant_equals(
                mir.insns[constant_instructions[item]].dst,
                constant_values[item]))
            return mir_machine_reject(
                "directory-enumeration-runner", "constant");
    if (!mir_gnarly_char_type(mir.insns[121].type) ||
        !mir_gnarly_char_type(mir.insns[177].type) ||
        !mir_gnarly_word_type(mir.insns[181].type, 1))
        return mir_machine_reject(
            "directory-enumeration-runner", "constant-type");
    for (item = 0; item < 21; ++item)
        if (!mir_gnarly_binary(
                binaries[item].instruction,
                binaries[item].left,
                binaries[item].right,
                binaries[item].operation))
            return mir_machine_reject(
                "directory-enumeration-runner", "binary");

    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->parameter_stack_offset) ||
        plan->parameter_stack_offset != 2 ||
        !mir_abort_runner_pointer_type(
            mir.insns[1].type, TYPE_CHAR) ||
        !mir_gnarly_phi(28, 4, 182, 17, 208) ||
        !mir_gnarly_phi(29, 23, 204, 17, 208) ||
        !mir_gnarly_phi(44, 39, 42, 38, 41) ||
        !mir_gnarly_phi(270, 262, 312, 261, 309))
        return mir_machine_reject(
            "directory-enumeration-runner", "parameter-phi");
    if (!mir_gnarly_branch(14, 13, 17) ||
        !mir_gnarly_branch(33, 32, 41) ||
        !mir_gnarly_branch(37, 36, 41) ||
        !mir_gnarly_branch(45, 44, 210) ||
        !mir_gnarly_branch(69, 68, 104) ||
        !mir_gnarly_branch(78, 77, 104) ||
        !mir_gnarly_branch(113, 112, 172) ||
        !mir_gnarly_branch(135, 134, 170) ||
        !mir_gnarly_branch(144, 143, 170) ||
        !mir_gnarly_branch(195, 194, 198) ||
        !mir_gnarly_branch(214, 213, 223) ||
        !mir_gnarly_branch(254, 253, 261) ||
        !mir_gnarly_branch(275, 274, 315) ||
        !mir_gnarly_branch(286, 285, 300) ||
        mir.insns[40].label != mir.insns[43].label ||
        mir.insns[92].label != mir.insns[96].label ||
        mir.insns[95].label != mir.insns[104].label ||
        mir.insns[103].label != mir.insns[60].label ||
        mir.insns[158].label != mir.insns[162].label ||
        mir.insns[161].label != mir.insns[170].label ||
        mir.insns[169].label != mir.insns[126].label ||
        mir.insns[197].label != mir.insns[210].label ||
        mir.insns[209].label != mir.insns[26].label ||
        mir.insns[314].label != mir.insns[265].label)
        return mir_machine_reject(
            "directory-enumeration-runner", "control-flow");

    for (item = 0; item < 5; ++item)
        if (mir.objects[item].storage != SC_LOCAL ||
            mir.objects[item].is_register)
            return mir_machine_reject(
                "directory-enumeration-runner", "object");
    if (!mir_gnarly_word_type(mir.objects[0].type, 1) ||
        !mir_gnarly_word_type(mir.objects[1].type, 0) ||
        !mir_gnarly_word_type(mir.objects[2].type, 0) ||
        !mir_gnarly_word_type(mir.objects[3].type, 0) ||
        type_ptr_depth(mir.objects[4].type) != 0 ||
        (mir.objects[4].type & 15) != TYPE_LONG ||
        (mir.objects[4].type & TYPE_UNSIGNED) == 0 ||
        type_size(mir.objects[4].type) != 4)
        return mir_machine_reject(
            "directory-enumeration-runner", "object-type");
    for (item = 0; item < 6; ++item)
        if (!mir_machine_same_location(
                &mir.insns[result_uses[0]],
                &mir.insns[result_uses[item]]))
            return mir_machine_reject(
                "directory-enumeration-runner", "result-local");
    for (item = 0; item < 6; ++item)
        if (!mir_machine_same_location(
                &mir.insns[result_fcb_uses[0]],
                &mir.insns[result_fcb_uses[item]]))
            return mir_machine_reject(
                "directory-enumeration-runner",
                "result-fcb-local");
    for (item = 0; item < 8; ++item)
        if (!mir_machine_same_location(
                &mir.insns[len_uses[0]],
                &mir.insns[len_uses[item]]))
            return mir_machine_reject(
                "directory-enumeration-runner", "length-local");
    if (mir.insns[6].object != 0 ||
        mir.insns[25].object != 1 ||
        mir.insns[56].object != 2 ||
        mir.insns[59].object != 3 ||
        mir.insns[283].object != 4 ||
        mir.insns[183].object != 0 ||
        mir.insns[206].object != 1 ||
        mir.insns[313].object != 3)
        return mir_machine_reject(
            "directory-enumeration-runner", "object-use");

    for (item = 0; item < 3; ++item)
        if (!mir_directory_local_address(
                fcb_addresses[item], 1, 0, 1) ||
            !mir_machine_same_location(
                &mir.insns[fcb_addresses[0]],
                &mir.insns[fcb_addresses[item]]))
            return mir_machine_reject(
                "directory-enumeration-runner", "fcb-address");
    for (item = 0; item < 5; ++item)
        if (!mir_directory_local_address(
                file_addresses[item], 1, TYPE_CHAR, 0) ||
            !mir_machine_same_location(
                &mir.insns[file_addresses[0]],
                &mir.insns[file_addresses[item]]))
            return mir_machine_reject(
                "directory-enumeration-runner", "file-address");
    if (mir_machine_same_location(
            &mir.insns[fcb_addresses[0]],
            &mir.insns[file_addresses[0]]) ||
        !mir_directory_local_address(235, 2, TYPE_CHAR, 0) ||
        !mir_machine_same_location(
            &mir.insns[3], &mir.insns[235]))
        return mir_machine_reject(
            "directory-enumeration-runner", "local-alias");

    if (!mir_memory_runner_array_root(
            179, plan->list_name))
        return mir_machine_reject(
            "directory-enumeration-runner", "list");
    for (item = 0; item < 6; ++item)
        if (!mir_directory_same_list_address(
                list_addresses[item], plan->list_name))
            return mir_machine_reject(
                "directory-enumeration-runner", "list-address");

    plan->quiet = find_global(mir.insns[284].name);
    if (plan->quiet == NULL ||
        plan->quiet->storage != SC_GLOBAL ||
        !plan->quiet->is_defined || plan->quiet->is_array ||
        plan->quiet->is_volatile ||
        type_ptr_depth(plan->quiet->type) != 0 ||
        (plan->quiet->type & 15) != TYPE_BOOL ||
        type_size(plan->quiet->type) != 1 ||
        mir.insns[284].object >= 0 ||
        !mir_machine_named_nonvolatile(&mir.insns[284]))
        return mir_machine_reject(
            "directory-enumeration-runner", "quiet");

    if (mir.insns[72].immediate != 1 ||
        mir.insns[72].memory_size != 8 ||
        mir.insns[72].src1 != mir.insns[71].dst ||
        mir.insns[87].immediate != 1 ||
        mir.insns[87].memory_size != 8 ||
        mir.insns[87].src1 != mir.insns[86].dst ||
        mir.insns[107].immediate != 9 ||
        mir.insns[107].memory_size != 3 ||
        mir.insns[107].src1 != mir.insns[106].dst ||
        mir.insns[138].immediate != 9 ||
        mir.insns[138].memory_size != 3 ||
        mir.insns[138].src1 != mir.insns[137].dst ||
        mir.insns[153].immediate != 9 ||
        mir.insns[153].memory_size != 3 ||
        mir.insns[153].src1 != mir.insns[152].dst)
        return mir_machine_reject(
            "directory-enumeration-runner", "fcb-member");
    if (mir.insns[74].src1 != mir.insns[72].dst ||
        mir.insns[74].src2 != mir.insns[73].dst ||
        mir.insns[74].immediate != 1 ||
        mir.insns[75].src1 != mir.insns[74].dst ||
        mir.insns[85].src1 != mir.insns[80].dst ||
        mir.insns[85].src2 != mir.insns[81].dst ||
        mir.insns[89].src1 != mir.insns[87].dst ||
        mir.insns[89].src2 != mir.insns[88].dst ||
        mir.insns[90].src1 != mir.insns[89].dst ||
        mir.insns[91].src1 != mir.insns[85].dst ||
        mir.insns[91].src2 != mir.insns[90].dst)
        return mir_machine_reject(
            "directory-enumeration-runner", "name-copy");
    if (mir.insns[109].src1 != mir.insns[107].dst ||
        mir.insns[109].src2 != mir.insns[108].dst ||
        mir.insns[110].src1 != mir.insns[109].dst ||
        mir.insns[119].src1 != mir.insns[114].dst ||
        mir.insns[119].src2 != mir.insns[115].dst ||
        mir.insns[122].src1 != mir.insns[119].dst)
        return mir_machine_reject(
            "directory-enumeration-runner", "extension-prefix");
    if (mir.insns[140].src1 != mir.insns[138].dst ||
        mir.insns[140].src2 != mir.insns[139].dst ||
        mir.insns[141].src1 != mir.insns[140].dst ||
        mir.insns[151].src1 != mir.insns[146].dst ||
        mir.insns[151].src2 != mir.insns[147].dst ||
        mir.insns[155].src1 != mir.insns[153].dst ||
        mir.insns[155].src2 != mir.insns[154].dst ||
        mir.insns[156].src1 != mir.insns[155].dst ||
        mir.insns[157].src1 != mir.insns[151].dst ||
        mir.insns[157].src2 != mir.insns[156].dst)
        return mir_machine_reject(
            "directory-enumeration-runner", "extension-copy");
    if (mir.insns[175].src1 != mir.insns[173].dst ||
        mir.insns[175].src2 != mir.insns[174].dst ||
        mir.insns[178].src1 != mir.insns[175].dst)
        return mir_machine_reject(
            "directory-enumeration-runner", "file-copy");
    if (mir.insns[184].src1 != mir.insns[179].dst ||
        mir.insns[184].src2 != mir.insns[180].dst ||
        mir.insns[184].immediate != 2 ||
        mir.insns[188].src1 != mir.insns[184].dst ||
        mir.insns[188].src2 != mir.insns[187].dst ||
        mir.insns[278].src1 != mir.insns[276].dst ||
        mir.insns[278].src2 != mir.insns[270].dst ||
        mir.insns[278].immediate != 2 ||
        mir.insns[279].src1 != mir.insns[278].dst ||
        mir.insns[293].src1 != mir.insns[291].dst ||
        mir.insns[293].src2 != mir.insns[270].dst ||
        mir.insns[294].src1 != mir.insns[293].dst ||
        mir.insns[303].src1 != mir.insns[301].dst ||
        mir.insns[303].src2 != mir.insns[270].dst ||
        mir.insns[304].src1 != mir.insns[303].dst)
        return mir_machine_reject(
            "directory-enumeration-runner", "list-access");

    functions[0] = mir_directory_function(12, 0, 2);
    functions[1] = mir_directory_function(23, 0, 2);
    functions[2] = mir_directory_function(187, 0, 1);
    functions[3] = mir_directory_function(234, 0, 4);
    functions[4] = mir_directory_function(248, 0, 5);
    functions[5] = mir_directory_function(219, 1, 1);
    functions[6] = mir_directory_function(281, 0, 1);
    functions[7] = mir_directory_function(307, 0, 1);
    for (item = 0; item < 8; ++item) {
        if (functions[item] == NULL)
            return mir_machine_reject(
                "directory-enumeration-runner", "function");
        for (previous = 0; previous < item; ++previous)
            if (functions[item] == functions[previous])
                return mir_machine_reject(
                    "directory-enumeration-runner",
                    "function-alias");
    }
    plan->initialize_function = functions[0];
    plan->bdos_function = functions[1];
    plan->duplicate_function = functions[2];
    plan->sort_function = functions[3];
    plan->search_function = functions[4];
    plan->print_function = functions[5];
    plan->size_function = functions[6];
    plan->free_function = functions[7];

    plan->compare_function =
        mir_directory_global_address(232, 1, TYPE_INT);
    if (plan->compare_function == NULL ||
        plan->compare_function->storage != SC_FUNC ||
        !plan->compare_function->is_defined ||
        plan->compare_function->is_funcptr ||
        plan->compare_function->is_noreturn ||
        !plan->compare_function->has_proto ||
        plan->compare_function->proto_variadic ||
        plan->compare_function->proto_nargs != 2 ||
        !mir_abort_runner_word_type(
            plan->compare_function->type) ||
        type_ptr_depth(
            plan->compare_function->proto_types[0]) != 2 ||
        (plan->compare_function->proto_types[0] & 15) !=
            TYPE_CHAR ||
        plan->compare_function->proto_types[0] !=
            plan->compare_function->proto_types[1] ||
        mir_directory_global_address(246, 1, TYPE_INT) !=
            plan->compare_function)
        return mir_machine_reject(
            "directory-enumeration-runner", "callback");

    if ((plan->initialize_function->type & 15) != TYPE_BOOL ||
        type_ptr_depth(plan->initialize_function->type) != 0 ||
        type_size(plan->initialize_function->type) != 1 ||
        type_ptr_depth(
            plan->initialize_function->proto_types[0]) != 1 ||
        !mir_abort_runner_pointer_type(
            plan->initialize_function->proto_types[1],
            TYPE_CHAR) ||
        !mir_abort_runner_word_type(
            plan->bdos_function->type) ||
        !mir_abort_runner_word_type(
            plan->bdos_function->proto_types[0]) ||
        !mir_abort_runner_word_type(
            plan->bdos_function->proto_types[1]) ||
        !mir_abort_runner_pointer_type(
            plan->duplicate_function->type, TYPE_CHAR) ||
        !mir_abort_runner_pointer_type(
            plan->duplicate_function->proto_types[0],
            TYPE_CHAR) ||
        (plan->sort_function->type & 15) != TYPE_VOID ||
        type_ptr_depth(plan->sort_function->type) != 0 ||
        type_ptr_depth(
            plan->sort_function->proto_types[0]) != 1 ||
        !mir_gnarly_word_type(
            plan->sort_function->proto_types[1], 1) ||
        !mir_gnarly_word_type(
            plan->sort_function->proto_types[2], 1) ||
        type_ptr_depth(
            plan->sort_function->proto_types[3]) != 1 ||
        type_ptr_depth(plan->search_function->type) != 1 ||
        type_ptr_depth(
            plan->search_function->proto_types[0]) != 1 ||
        type_ptr_depth(
            plan->search_function->proto_types[1]) != 1 ||
        !mir_gnarly_word_type(
            plan->search_function->proto_types[2], 1) ||
        !mir_gnarly_word_type(
            plan->search_function->proto_types[3], 1) ||
        type_ptr_depth(
            plan->search_function->proto_types[4]) != 1 ||
        !mir_abort_runner_word_type(
            plan->print_function->type) ||
        !mir_abort_runner_pointer_type(
            plan->print_function->proto_types[0], TYPE_CHAR) ||
        type_ptr_depth(plan->size_function->type) != 0 ||
        (plan->size_function->type & 15) != TYPE_LONG ||
        (plan->size_function->type & TYPE_UNSIGNED) == 0 ||
        type_size(plan->size_function->type) != 4 ||
        !mir_abort_runner_pointer_type(
            plan->size_function->proto_types[0], TYPE_CHAR) ||
        (plan->free_function->type & 15) != TYPE_VOID ||
        type_ptr_depth(plan->free_function->type) != 0 ||
        type_ptr_depth(
            plan->free_function->proto_types[0]) != 1)
        return mir_machine_reject(
            "directory-enumeration-runner", "prototype");
    for (item = 0; item < 11; ++item)
        if (!mir_directory_call(
                calls[item].instruction,
                functions[calls[item].function],
                item, calls[item].argument_count,
                calls[item].definitions))
            return mir_machine_reject(
                "directory-enumeration-runner", "call");

    if (!mir_directory_string(2, &plan->strings[0]) ||
        !mir_directory_string(215, &plan->strings[1]) ||
        !mir_directory_string(255, &plan->strings[2]) ||
        !mir_directory_string(287, &plan->strings[3]))
        return mir_machine_reject(
            "directory-enumeration-runner", "string");
    for (item = 0; item < 4; ++item)
        for (previous = 0; previous < item; ++previous)
            if (plan->strings[item] == plan->strings[previous])
                return mir_machine_reject(
                    "directory-enumeration-runner",
                    "string-alias");

    if (mir.insns[13].immediate != '!' ||
        mir.insns[13].src1 != mir.insns[12].dst ||
        mir.insns[52].immediate != 0 ||
        mir.insns[52].src1 != mir.insns[50].dst ||
        mir.insns[76].immediate != 0 ||
        mir.insns[76].src1 != mir.insns[75].dst ||
        mir.insns[111].immediate != 0 ||
        mir.insns[111].src1 != mir.insns[110].dst ||
        mir.insns[142].immediate != 0 ||
        mir.insns[142].src1 != mir.insns[141].dst ||
        mir.insns[249].immediate != 0 ||
        mir.insns[249].src1 != mir.insns[248].dst ||
        mir.insns[285].immediate != '!' ||
        mir.insns[285].src1 != mir.insns[284].dst ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[221].src1 != mir.insns[220].dst ||
        mir.insns[317].src1 != mir.insns[316].dst)
        return mir_machine_reject(
            "directory-enumeration-runner", "result-flow");
    return 1;
}

enum {
    MIR_DIRECTORY_FCB = -36,
    MIR_DIRECTORY_FILE = -49,
    MIR_DIRECTORY_LIST_LENGTH = -51,
    MIR_DIRECTORY_TEMP = -53,
    MIR_DIRECTORY_FRAME_BYTES = 53
};

static void mir_directory_emit_local_address(FILE *out, int offset)
{
    fputs("\tpush ix\n\tpop hl\n", out);
    if (offset != 0)
        fprintf(out, "\tld de,%d\n\tadd hl,de\n", offset);
}

static void mir_directory_emit_symbol_address(
    FILE *out, struct Sym *symbol)
{
    fprintf(out, "\tld hl,%s\n",
            asm_name_for(sym_asm_name(symbol)));
}

static void mir_directory_emit_list_item(
    FILE *out, const struct MirDirectoryEnumerationRunner *plan,
    int index_offset)
{
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tadd hl,hl\n\tld de,%s\n\tadd hl,de\n",
            index_offset, index_offset + 1,
            asm_name_for(plan->list_name));
}

static void mir_directory_emit_cleanup(FILE *out, int words)
{
    while (words-- > 0)
        fputs("\tpop bc\n", out);
}

static void mir_directory_emit_bdos(
    FILE *out, const struct MirDirectoryEnumerationRunner *plan,
    int function_number)
{
    mir_directory_emit_local_address(out, MIR_DIRECTORY_FCB);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,%d\n\tpush hl\n", function_number);
    mir_machine_emit_symbol_call(out, plan->bdos_function);
    mir_directory_emit_cleanup(out, 2);
    fprintf(out,
            "\tld (ix%+d),l\n\tld (ix%+d),h\n",
            MIR_DIRECTORY_TEMP, MIR_DIRECTORY_TEMP + 1);
}

static void mir_emit_directory_enumeration_runner(
    FILE *out, const struct MirDirectoryEnumerationRunner *plan)
{
    int initialized = new_label();
    int search_loop = new_label();
    int search_done = new_label();
    int name_loop = new_label();
    int name_done = new_label();
    int extension_loop = new_label();
    int extension_done = new_label();
    int search_result_ok = new_label();
    int not_found = new_label();
    int free_loop = new_label();
    int free_done = new_label();
    int quiet = new_label();
    int return_done = new_label();
    const char *list_name =
        asm_name_for(plan->list_name);
    const char *quiet_name =
        asm_name_for(sym_asm_name(plan->quiet));

    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    fprintf(out,
            "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n",
            MIR_DIRECTORY_FRAME_BYTES);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,S%d\n"
            "\tld (ix%+d),l\n\tld (ix%+d),h\n"
            "\txor a\n\tld (ix%+d),a\n\tld (ix%+d),a\n",
            plan->strings[0],
            MIR_DIRECTORY_TEMP, MIR_DIRECTORY_TEMP + 1,
            MIR_DIRECTORY_LIST_LENGTH,
            MIR_DIRECTORY_LIST_LENGTH + 1);

    fprintf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n",
            plan->parameter_stack_offset + 2,
            plan->parameter_stack_offset + 3);
    mir_directory_emit_local_address(out, MIR_DIRECTORY_FCB);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->initialize_function);
    mir_directory_emit_cleanup(out, 2);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out,
            "\tjp nz,L%d\n\tld hl,0\n\tjp L%d\n"
            "L%d:\n",
            initialized, return_done, initialized);

    mir_directory_emit_bdos(out, plan, 17);
    fprintf(out,
            "L%d:\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tbit 7,h\n\tjp nz,L%d\n"
            "\tld a,h\n\tor a\n\tjp nz,L%d\n"
            "\tld a,l\n\tcp 4\n\tjp nc,L%d\n"
            "\tld h,0\n"
            "\tadd hl,hl\n\tadd hl,hl\n\tadd hl,hl\n"
            "\tadd hl,hl\n\tadd hl,hl\n"
            "\tld de,128\n\tadd hl,de\n\tex de,hl\n"
            "\tinc de\n",
            search_loop,
            MIR_DIRECTORY_TEMP, MIR_DIRECTORY_TEMP + 1,
            search_done, search_done, search_done);
    fputs("\tpush de\n", out);
    mir_directory_emit_local_address(out, MIR_DIRECTORY_FILE);
    fputs("\tpop de\n", out);
    fprintf(out,
            "\tld b,8\n"
            "L%d:\n\tld a,(de)\n\tcp 32\n\tjp z,L%d\n"
            "\tld (hl),a\n\tinc hl\n\tinc de\n"
            "\tdjnz L%d\n"
            "L%d:\n\tpush hl\n"
            "\tld l,(ix%+d)\n\tld h,0\n"
            "\tadd hl,hl\n\tadd hl,hl\n\tadd hl,hl\n"
            "\tadd hl,hl\n\tadd hl,hl\n"
            "\tld de,137\n\tadd hl,de\n\tex de,hl\n"
            "\tpop hl\n\tld a,(de)\n\tcp 32\n"
            "\tjp z,L%d\n\tld (hl),46\n\tinc hl\n"
            "\tld b,3\n"
            "L%d:\n\tld a,(de)\n\tcp 32\n\tjp z,L%d\n"
            "\tld (hl),a\n\tinc hl\n\tinc de\n"
            "\tdjnz L%d\n"
            "L%d:\n\txor a\n\tld (hl),a\n",
            name_loop, name_done, name_loop, name_done,
            MIR_DIRECTORY_TEMP,
            extension_done, extension_loop, extension_done,
            extension_loop, extension_done);

    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tinc hl\n\tld (ix%+d),l\n\tld (ix%+d),h\n",
            MIR_DIRECTORY_LIST_LENGTH,
            MIR_DIRECTORY_LIST_LENGTH + 1,
            MIR_DIRECTORY_LIST_LENGTH,
            MIR_DIRECTORY_LIST_LENGTH + 1);
    mir_directory_emit_local_address(out, MIR_DIRECTORY_FILE);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->duplicate_function);
    fputs("\tpop bc\n\tex de,hl\n", out);
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tdec hl\n\tadd hl,hl\n\tld bc,%s\n"
            "\tadd hl,bc\n\tld (hl),e\n\tinc hl\n"
            "\tld (hl),d\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld de,400\n\tor a\n\tsbc hl,de\n"
            "\tjp z,L%d\n",
            MIR_DIRECTORY_LIST_LENGTH,
            MIR_DIRECTORY_LIST_LENGTH + 1,
            list_name,
            MIR_DIRECTORY_LIST_LENGTH,
            MIR_DIRECTORY_LIST_LENGTH + 1,
            search_done);
    mir_directory_emit_bdos(out, plan, 18);
    fprintf(out, "\tjp L%d\nL%d:\n",
            search_loop, search_done);

    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld de,255\n\tor a\n\tsbc hl,de\n"
            "\tjp z,L%d\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            MIR_DIRECTORY_TEMP, MIR_DIRECTORY_TEMP + 1,
            search_result_ok,
            MIR_DIRECTORY_TEMP, MIR_DIRECTORY_TEMP + 1,
            plan->strings[1]);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_directory_emit_cleanup(out, 2);
    fprintf(out,
            "\tld hl,0\n\tjp L%d\n"
            "L%d:\n"
            "\tld hl,S%d\n"
            "\tld (ix%+d),l\n\tld (ix%+d),h\n",
            return_done, search_result_ok, plan->strings[0],
            MIR_DIRECTORY_TEMP, MIR_DIRECTORY_TEMP + 1);

    mir_directory_emit_symbol_address(out, plan->compare_function);
    fputs("\tpush hl\n\tld hl,2\n\tpush hl\n", out);
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n"
            "\tld hl,%s\n\tpush hl\n",
            MIR_DIRECTORY_LIST_LENGTH,
            MIR_DIRECTORY_LIST_LENGTH + 1,
            list_name);
    mir_machine_emit_symbol_call(out, plan->sort_function);
    mir_directory_emit_cleanup(out, 4);

    mir_directory_emit_symbol_address(out, plan->compare_function);
    fputs("\tpush hl\n\tld hl,2\n\tpush hl\n", out);
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n"
            "\tld hl,%s\n\tpush hl\n",
            MIR_DIRECTORY_LIST_LENGTH,
            MIR_DIRECTORY_LIST_LENGTH + 1,
            list_name);
    mir_directory_emit_local_address(out, MIR_DIRECTORY_TEMP);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->search_function);
    mir_directory_emit_cleanup(out, 5);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", not_found);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[2]);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_directory_emit_cleanup(out, 2);
    fprintf(out,
            "L%d:\n\txor a\n"
            "\tld (ix%+d),a\n\tld (ix%+d),a\n"
            "L%d:\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n"
            "\tor a\n\tsbc hl,de\n\tjp nc,L%d\n",
            not_found,
            MIR_DIRECTORY_TEMP, MIR_DIRECTORY_TEMP + 1,
            free_loop,
            MIR_DIRECTORY_TEMP, MIR_DIRECTORY_TEMP + 1,
            MIR_DIRECTORY_LIST_LENGTH,
            MIR_DIRECTORY_LIST_LENGTH + 1,
            free_done);

    mir_directory_emit_list_item(out, plan, MIR_DIRECTORY_TEMP);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tpush de\n", out);
    mir_machine_emit_symbol_call(out, plan->size_function);
    fputs("\tpop bc\n", out);
    fprintf(out, "\tld a,(%s)\n\tor a\n\tjp nz,L%d\n",
            quiet_name, quiet);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_directory_emit_list_item(out, plan, MIR_DIRECTORY_TEMP);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n", out);
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            MIR_DIRECTORY_TEMP, MIR_DIRECTORY_TEMP + 1,
            plan->strings[3]);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_directory_emit_cleanup(out, 5);
    fprintf(out, "L%d:\n", quiet);

    mir_directory_emit_list_item(out, plan, MIR_DIRECTORY_TEMP);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n", out);
    mir_machine_emit_symbol_call(out, plan->free_function);
    fputs("\tpop bc\n", out);
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tinc hl\n"
            "\tld (ix%+d),l\n\tld (ix%+d),h\n"
            "\tjp L%d\n"
            "L%d:\n\tld hl,1\n"
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            MIR_DIRECTORY_TEMP, MIR_DIRECTORY_TEMP + 1,
            MIR_DIRECTORY_TEMP, MIR_DIRECTORY_TEMP + 1,
            free_loop, free_done, return_done);
}

static int mir_cast_log_word_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           (type & 15) == TYPE_INT &&
           (type & TYPE_UNSIGNED) == 0 &&
           type_size(type) == 2;
}

static int mir_cast_log_char_type(int type, int is_unsigned)
{
    return type_ptr_depth(type) == 0 &&
           (type & 15) == TYPE_CHAR &&
           ((type & TYPE_UNSIGNED) != 0) == is_unsigned &&
           type_size(type) == 1;
}

static int mir_cast_log_char_pointer_type(int type, int is_unsigned)
{
    return type_ptr_depth(type) == 1 &&
           (type & 15) == TYPE_CHAR &&
           ((type & TYPE_UNSIGNED) != 0) == is_unsigned &&
           type_size(type) == 2;
}

static int mir_cast_log_constant(int instruction, long value)
{
    const struct MirInsn *constant = &mir.insns[instruction];

    /*
     * This schedule depends on the source-width loop bounds and indices.
     * Accept only literal MIR constants, rather than broadening the shape
     * through the shared evaluator.
     */
    return constant->opcode == MIR_CONST &&
           constant->immediate == value;
}

static int mir_cast_log_binary(
    int instruction, int left, int right, int operation)
{
    const struct MirInsn *binary = &mir.insns[instruction];

    return binary->opcode == MIR_BINARY &&
           binary->src1 == mir.insns[left].dst &&
           binary->src2 == mir.insns[right].dst &&
           binary->immediate == operation &&
           mir_cast_log_word_type(binary->type) &&
           mir_cast_log_word_type(binary->secondary_offset);
}

static int mir_cast_log_branch(
    int instruction, int value, int target)
{
    const struct MirInsn *branch = &mir.insns[instruction];

    return branch->opcode == MIR_BRANCH_FALSE &&
           branch->src1 == mir.insns[value].dst &&
           branch->label == mir.insns[target].label;
}

static int mir_cast_log_jump(int instruction, int target)
{
    return mir.insns[instruction].opcode == MIR_JUMP &&
           mir.insns[instruction].label ==
               mir.insns[target].label;
}

static int mir_cast_log_phi(
    int instruction, int left, int right,
    int left_predecessor, int right_predecessor)
{
    const struct MirInsn *phi = &mir.insns[instruction];

    return phi->opcode == MIR_PHI &&
           phi->src1 == mir.insns[left].dst &&
           phi->src2 == mir.insns[right].dst &&
           phi->phi_pred1 ==
               mir.insns[left_predecessor].label &&
           phi->phi_pred2 ==
               mir.insns[right_predecessor].label;
}

static int mir_cast_log_same_address(int left, int right)
{
    const struct MirInsn *a = &mir.insns[left];
    const struct MirInsn *b = &mir.insns[right];

    return a->opcode == MIR_ADDRESS &&
           b->opcode == MIR_ADDRESS &&
           a->object < 0 && b->object < 0 &&
           a->memory_flags == 0 && b->memory_flags == 0 &&
           !strcmp(a->name, b->name) &&
           a->type == b->type;
}

static int mir_cast_log_index(
    int instruction, int address, int subscript,
    int is_unsigned)
{
    const struct MirInsn *index = &mir.insns[instruction];

    return index->opcode == MIR_INDEX_ADDRESS &&
           index->src1 == mir.insns[address].dst &&
           index->src2 == mir.insns[subscript].dst &&
           index->immediate == 1 &&
           index->memory_size == 1 &&
           index->memory_flags == 0 &&
           mir_cast_log_char_pointer_type(
               index->type, is_unsigned);
}

static int mir_cast_log_load(
    int instruction, int address, int is_unsigned)
{
    const struct MirInsn *load = &mir.insns[instruction];

    return load->opcode == MIR_LOAD_INDIRECT &&
           load->src1 == mir.insns[address].dst &&
           load->memory_size == 1 &&
           load->memory_flags == 0 &&
           mir_cast_log_char_type(load->type, is_unsigned);
}

static int mir_cast_log_store(
    int instruction, int address, int value)
{
    const struct MirInsn *store = &mir.insns[instruction];

    return store->opcode == MIR_STORE_INDIRECT &&
           store->src1 == mir.insns[address].dst &&
           store->src2 == mir.insns[value].dst &&
           store->memory_size == 1 &&
           store->memory_flags == 0;
}

static int mir_match_cast_logical_runner(
    struct MirCastLogicalRunner *plan)
{
    static const int expected_opcodes[198] = {
        MIR_LABEL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_PHI,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_PHI, MIR_UNARY, MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_LABEL, MIR_PHI, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_UNARY,
        MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BRANCH_FALSE, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_PHI, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_PHI, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_UNARY,
        MIR_NOP, MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_NOP,
        MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN
    };
    static const int constants[][2] = {
        {1, 0}, {7, 6}, {14, 2}, {18, 1}, {22, 4},
        {26, 1}, {29, 0}, {40, 1}, {45, 0}, {51, 5},
        {58, 0}, {62, 4}, {66, 1}, {69, 0}, {76, 1},
        {82, 2}, {87, 3}, {92, 1}, {95, 0}, {100, 1},
        {104, 0}, {109, 1}, {112, 0}, {121, 1}, {127, 1},
        {130, 0}, {139, 0}, {144, 1}, {149, 2}, {154, 3},
        {159, 4}, {164, 5}, {169, 0}, {174, 1}, {179, 2},
        {184, 3}, {189, 4}, {196, 0}
    };
    static const int ors_addresses[] = {
        10, 81, 103, 120, 138, 143, 148, 153, 158, 163
    };
    static const int ands_addresses[] = {
        54, 86, 168, 173, 178, 183, 188
    };
    const struct MirInsn *call = &mir.insns[195];
    const char *assembly_name;
    int arguments[13];
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 198 || mir_cfg_block_count() != 30 ||
        mir.local_bytes != 18 || mir.aggregate_temp_bytes != 0 ||
        mir.has_vla || !mir_has_cfg_backedge() ||
        !mir_cast_log_word_type(mir.return_type))
        return mir_machine_reject(
            "cast-logical-runner", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "cast-logical-runner", "opcodes");
    for (item = 0;
         item < (int)(sizeof(constants) / sizeof(constants[0]));
         ++item)
        if (!mir_cast_log_constant(
                constants[item][0], constants[item][1]))
            return mir_machine_reject(
                "cast-logical-runner", "constants");

    if (!mir_cast_log_word_type(mir.insns[1].type) ||
        !mir_cast_log_word_type(mir.insns[7].type) ||
        !mir_cast_log_word_type(mir.insns[14].type) ||
        !mir_cast_log_word_type(mir.insns[22].type) ||
        !mir_cast_log_word_type(mir.insns[40].type) ||
        !mir_cast_log_word_type(mir.insns[45].type) ||
        !mir_cast_log_word_type(mir.insns[51].type) ||
        !mir_cast_log_word_type(mir.insns[58].type) ||
        !mir_cast_log_word_type(mir.insns[62].type) ||
        !mir_cast_log_word_type(mir.insns[76].type) ||
        !mir_cast_log_word_type(mir.insns[196].type))
        return mir_machine_reject(
            "cast-logical-runner", "constant-types");

    if (mir.insns[3].src1 != mir.insns[1].dst ||
        !mir_machine_same_location(
            &mir.insns[3], &mir.insns[5]) ||
        !mir_machine_same_location(
            &mir.insns[3], &mir.insns[42]) ||
        mir.insns[5].object < 0 ||
        mir.insns[5].object != mir.insns[3].object ||
        mir.insns[42].src1 != mir.insns[41].dst ||
        !mir_cast_log_phi(5, 1, 41, 0, 38) ||
        !mir_cast_log_binary(8, 5, 7, '<') ||
        !mir_cast_log_branch(9, 8, 44) ||
        !mir_cast_log_binary(15, 5, 14, TOK_EQ) ||
        !mir_cast_log_branch(16, 15, 20) ||
        !mir_cast_log_jump(19, 34) ||
        !mir_cast_log_binary(23, 5, 22, TOK_EQ) ||
        !mir_cast_log_branch(24, 23, 28) ||
        !mir_cast_log_jump(27, 30) ||
        !mir_cast_log_phi(31, 26, 29, 25, 28) ||
        !mir_cast_log_jump(33, 34) ||
        !mir_cast_log_phi(35, 18, 31, 17, 32) ||
        mir.insns[36].src1 != mir.insns[35].dst ||
        mir.insns[36].immediate != 0 ||
        !mir_cast_log_char_type(mir.insns[36].type, 1) ||
        !mir_cast_log_binary(41, 5, 40, '+') ||
        !mir_cast_log_jump(43, 4))
        return mir_machine_reject(
            "cast-logical-runner", "or-loop");

    if (mir.insns[47].src1 != mir.insns[45].dst ||
        !mir_machine_same_location(
            &mir.insns[47], &mir.insns[49]) ||
        !mir_machine_same_location(
            &mir.insns[47], &mir.insns[78]) ||
        !mir_machine_same_location(
            &mir.insns[3], &mir.insns[47]) ||
        mir.insns[49].object < 0 ||
        mir.insns[49].object != mir.insns[47].object ||
        mir.insns[49].object != mir.insns[5].object ||
        mir.insns[78].src1 != mir.insns[77].dst ||
        !mir_cast_log_phi(49, 45, 77, 44, 74) ||
        !mir_cast_log_binary(52, 49, 51, '<') ||
        !mir_cast_log_branch(53, 52, 80) ||
        !mir_cast_log_binary(59, 49, 58, '>') ||
        !mir_cast_log_branch(60, 59, 68) ||
        !mir_cast_log_binary(63, 49, 62, '<') ||
        !mir_cast_log_branch(64, 63, 68) ||
        !mir_cast_log_jump(67, 70) ||
        !mir_cast_log_phi(71, 66, 69, 65, 68) ||
        mir.insns[72].src1 != mir.insns[71].dst ||
        mir.insns[72].immediate != 0 ||
        !mir_cast_log_char_type(mir.insns[72].type, 0) ||
        !mir_cast_log_binary(77, 49, 76, '+') ||
        !mir_cast_log_jump(79, 48))
        return mir_machine_reject(
            "cast-logical-runner", "and-loop");

    if (!mir_cast_log_char_pointer_type(mir.insns[10].type, 1) ||
        !mir_cast_log_char_pointer_type(mir.insns[54].type, 0))
        return mir_machine_reject(
            "cast-logical-runner", "arrays");
    for (item = 1;
         item < (int)(sizeof(ors_addresses) /
                      sizeof(ors_addresses[0]));
         ++item)
        if (!mir_cast_log_same_address(
                ors_addresses[0], ors_addresses[item]))
            return mir_machine_reject(
                "cast-logical-runner", "or-array-identity");
    for (item = 1;
         item < (int)(sizeof(ands_addresses) /
                      sizeof(ands_addresses[0]));
         ++item)
        if (!mir_cast_log_same_address(
                ands_addresses[0], ands_addresses[item]))
            return mir_machine_reject(
                "cast-logical-runner", "and-array-identity");
    if (!strcmp(mir.insns[10].name, mir.insns[54].name) ||
        !mir_cast_log_index(12, 10, 5, 1) ||
        !mir_cast_log_store(37, 12, 36) ||
        !mir_cast_log_index(56, 54, 49, 0) ||
        !mir_cast_log_store(73, 56, 72))
        return mir_machine_reject(
            "cast-logical-runner", "loop-array-access");

    if (!mir_cast_log_index(83, 81, 82, 1) ||
        !mir_cast_log_load(84, 83, 1) ||
        !mir_cast_log_branch(85, 84, 94) ||
        !mir_cast_log_index(88, 86, 87, 0) ||
        !mir_cast_log_load(89, 88, 0) ||
        !mir_cast_log_branch(90, 89, 94) ||
        !mir_cast_log_jump(93, 96) ||
        !mir_cast_log_phi(97, 92, 95, 91, 94) ||
        !mir_cast_log_branch(98, 97, 102) ||
        !mir_cast_log_jump(101, 117) ||
        !mir_cast_log_index(105, 103, 104, 1) ||
        !mir_cast_log_load(106, 105, 1) ||
        !mir_cast_log_branch(107, 106, 111) ||
        !mir_cast_log_jump(110, 113) ||
        !mir_cast_log_phi(114, 109, 112, 108, 111) ||
        !mir_cast_log_jump(116, 117) ||
        !mir_cast_log_phi(118, 100, 114, 99, 115) ||
        !mir_cast_log_branch(119, 118, 129) ||
        !mir_cast_log_index(122, 120, 121, 1) ||
        !mir_cast_log_load(123, 122, 1) ||
        mir.insns[124].src1 != mir.insns[123].dst ||
        mir.insns[124].immediate != '!' ||
        !mir_cast_log_word_type(mir.insns[124].type) ||
        !mir_cast_log_branch(125, 124, 129) ||
        !mir_cast_log_jump(128, 131) ||
        !mir_cast_log_phi(132, 127, 130, 126, 129) ||
        mir.insns[133].src1 != mir.insns[132].dst ||
        mir.insns[133].immediate != 0 ||
        !mir_cast_log_char_type(mir.insns[133].type, 1))
        return mir_machine_reject(
            "cast-logical-runner", "nested-logical");

    if (mir.insns[135].src1 != mir.insns[133].dst ||
        mir.insns[135].memory_size != 1 ||
        mir.insns[135].memory_flags != 0 ||
        mir.insns[135].object < 0 ||
        mir.insns[135].object == mir.insns[5].object ||
        mir.insns[135].object == mir.insns[49].object ||
        !mir_machine_same_location(
            &mir.insns[134], &mir.insns[135]) ||
        !mir_machine_same_location(
            &mir.insns[135], &mir.insns[193]) ||
        !mir_cast_log_char_type(mir.insns[135].type, 1) ||
        mir.insns[194].src1 != mir.insns[133].dst)
        return mir_machine_reject(
            "cast-logical-runner", "nested-store");

    for (item = 0; item < 6; ++item) {
        int base = 138 + item * 5;

        if (!mir_cast_log_index(
                base + 2, base, base + 1, 1) ||
            !mir_cast_log_load(base + 3, base + 2, 1))
            return mir_machine_reject(
                "cast-logical-runner", "or-report");
    }
    for (item = 0; item < 5; ++item) {
        int base = 168 + item * 5;

        if (!mir_cast_log_index(
                base + 2, base, base + 1, 0) ||
            !mir_cast_log_load(base + 3, base + 2, 0))
            return mir_machine_reject(
                "cast-logical-runner", "and-report");
    }

    plan->print_function = find_global(call->name);
    if (plan->print_function == NULL ||
        plan->print_function->storage != SC_FUNC ||
        plan->print_function->is_funcptr ||
        plan->print_function->is_noreturn ||
        !plan->print_function->has_proto ||
        !plan->print_function->proto_variadic ||
        plan->print_function->proto_nargs != 1 ||
        !mir_cast_log_word_type(plan->print_function->type) ||
        !mir_cast_log_char_pointer_type(
            plan->print_function->proto_types[0], 0) ||
        call->src1 >= 0 ||
        call->memory_flags != MIR_CALL_FLAG_VARIADIC ||
        call->type != plan->print_function->type ||
        !mir_machine_call_arguments(call, 13, arguments))
        return mir_machine_reject(
            "cast-logical-runner", "report-call");
    assembly_name =
        asm_name_for(sym_asm_name(plan->print_function));
    if (call->base_name[0] != 0 &&
        strcmp(call->base_name, assembly_name))
        return mir_machine_reject(
            "cast-logical-runner", "report-symbol");
    if (!mir_cast_log_char_pointer_type(
            mir.insns[136].type, 0) ||
        mir.insns[136].immediate < 0 ||
        arguments[0] != mir.insns[136].dst)
        return mir_machine_reject(
            "cast-logical-runner", "report-format");
    for (item = 0; item < 6; ++item)
        if (arguments[item + 1] !=
            mir.insns[141 + item * 5].dst)
            return mir_machine_reject(
                "cast-logical-runner", "report-or-arguments");
    for (item = 0; item < 5; ++item)
        if (arguments[item + 7] !=
            mir.insns[171 + item * 5].dst)
            return mir_machine_reject(
                "cast-logical-runner", "report-and-arguments");
    if (arguments[12] != mir.insns[133].dst ||
        mir.insns[197].src1 != mir.insns[196].dst)
        return mir_machine_reject(
            "cast-logical-runner", "return");

    plan->format_string_id = (int)mir.insns[136].immediate;
    return 1;
}

static void mir_cast_log_emit_signed_byte_argument(
    FILE *out, int offset)
{
    fprintf(out,
            "\tld l,(ix%+d)\n"
            "\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n"
            "\tpush hl\n",
            offset);
}

static void mir_emit_cast_logical_runner(
    FILE *out, const struct MirCastLogicalRunner *plan)
{
    int or_loop = new_label();
    int or_second = new_label();
    int or_value = new_label();
    int and_loop = new_label();
    int and_false = new_label();
    int and_value = new_label();
    int nested_or = new_label();
    int nested_false = new_label();
    int nested_value = new_label();
    int offset;
    int cleanup;

    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-12\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    fprintf(out,
            "\tld b,0\n"
            "L%d:\n\tld a,b\n\tcp 2\n\tjp nz,L%d\n"
            "\tld a,1\n\tjp L%d\n"
            "L%d:\n\tld a,b\n\tcp 4\n\tld a,0\n"
            "\tjp nz,L%d\n\tinc a\n"
            "L%d:\n\tpush af\n\tpush ix\n\tpop hl\n"
            "\tld e,b\n\tld d,0\n\tadd hl,de\n"
            "\tld de,-6\n\tadd hl,de\n"
            "\tpop af\n\tld (hl),a\n"
            "\tinc b\n\tld a,b\n\tcp 6\n\tjp c,L%d\n",
            or_loop, or_second, or_value,
            or_second, or_value, or_value, or_loop);

    fprintf(out,
            "\tld b,0\n"
            "L%d:\n\tld a,b\n\tor a\n\tjp z,L%d\n"
            "\tcp 4\n\tjp nc,L%d\n"
            "\tld a,1\n\tjp L%d\n"
            "L%d:\n\txor a\n"
            "L%d:\n\tpush af\n\tpush ix\n\tpop hl\n"
            "\tld e,b\n\tld d,0\n\tadd hl,de\n"
            "\tld de,-11\n\tadd hl,de\n"
            "\tpop af\n\tld (hl),a\n"
            "\tinc b\n\tld a,b\n\tcp 5\n\tjp c,L%d\n",
            and_loop, and_false, and_false, and_value,
            and_false, and_value, and_loop);

    fprintf(out,
            "\tld a,(ix-4)\n\tor a\n\tjp z,L%d\n"
            "\tld a,(ix-8)\n\tor a\n\tjp nz,L%d\n"
            "L%d:\n\tld a,(ix-6)\n\tor a\n\tjp z,L%d\n"
            "L%d:\n\tld a,(ix-5)\n\tor a\n\tjp nz,L%d\n"
            "\tld a,1\n\tjp L%d\n"
            "L%d:\n\txor a\n"
            "L%d:\n\tld (ix-12),a\n",
            nested_or, nested_value,
            nested_or, nested_false,
            nested_value, nested_false,
            nested_value, nested_false, nested_value);

    fputs("\tld l,(ix-12)\n\tld h,0\n\tpush hl\n", out);
    for (offset = -7; offset >= -11; --offset)
        mir_cast_log_emit_signed_byte_argument(out, offset);
    for (offset = -1; offset >= -6; --offset)
        fprintf(out,
                "\tld l,(ix%+d)\n\tld h,0\n\tpush hl\n",
                offset);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->format_string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    for (cleanup = 0; cleanup < 13; ++cleanup)
        fputs("\tpop bc\n", out);
    fputs("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_buffered_console_word_type(int type, int is_unsigned)
{
    return type_ptr_depth(type) == 0 &&
           (type & 15) == TYPE_INT &&
           ((type & TYPE_UNSIGNED) != 0) == is_unsigned &&
           type_size(type) == 2;
}

static int mir_buffered_console_pointer_type(int type, int base_type)
{
    return type_ptr_depth(type) == 1 &&
           (type & 15) == base_type &&
           type_size(type) == 2;
}

static struct Sym *mir_buffered_console_function(
    int instruction, int variadic, int fixed_arguments)
{
    const struct MirInsn *call = &mir.insns[instruction];
    struct Sym *function;

    if (call->opcode != MIR_CALL || call->src1 >= 0 ||
        call->memory_flags !=
            (variadic ? MIR_CALL_FLAG_VARIADIC : 0) ||
        (function = find_global(call->name)) == NULL ||
        function->storage != SC_FUNC || function->is_funcptr ||
        function->is_noreturn || !function->has_proto ||
        function->proto_variadic != variadic ||
        function->proto_nargs != fixed_arguments ||
        call->type != function->type)
        return NULL;
    return function;
}

static int mir_buffered_console_function_types(
    const struct MirBufferedConsoleRunner *plan)
{
    struct Sym *print = plan->functions[MIR_BUFFER_PRINT];
    struct Sym *allocate = plan->functions[MIR_BUFFER_ALLOCATE];
    struct Sym *configure = plan->functions[MIR_BUFFER_CONFIGURE];
    struct Sym *check = plan->functions[MIR_BUFFER_CHECK];
    struct Sym *flush = plan->functions[MIR_BUFFER_FLUSH];
    struct Sym *free_function = plan->functions[MIR_BUFFER_FREE];
    struct Sym *puts_stream = plan->functions[MIR_BUFFER_PUTS_STREAM];
    struct Sym *puts_function = plan->functions[MIR_BUFFER_PUTS];
    struct Sym *putchar_function = plan->functions[MIR_BUFFER_PUTCHAR];
    struct Sym *setbuf_function = plan->functions[MIR_BUFFER_SETBUF];
    struct Sym *fprint = plan->functions[MIR_BUFFER_FPRINT];
    struct Sym *clear = plan->functions[MIR_BUFFER_CLEAR];
    struct Sym *open_function = plan->functions[MIR_BUFFER_OPEN];
    struct Sym *close_function = plan->functions[MIR_BUFFER_CLOSE];
    struct Sym *remove_function = plan->functions[MIR_BUFFER_REMOVE];

    return
        mir_buffered_console_word_type(print->type, 0) &&
        mir_buffered_console_pointer_type(
            print->proto_types[0], TYPE_CHAR) &&
        mir_buffered_console_pointer_type(
            allocate->type, TYPE_CHAR) &&
        mir_buffered_console_word_type(
            allocate->proto_types[0], 1) &&
        mir_buffered_console_word_type(configure->type, 0) &&
        mir_buffered_console_pointer_type(
            configure->proto_types[0], TYPE_INT) &&
        mir_buffered_console_pointer_type(
            configure->proto_types[1], TYPE_CHAR) &&
        mir_buffered_console_word_type(
            configure->proto_types[2], 0) &&
        mir_buffered_console_word_type(
            configure->proto_types[3], 1) &&
        type_ptr_depth(check->type) == 0 &&
        (check->type & 15) == TYPE_VOID &&
        mir_buffered_console_pointer_type(
            check->proto_types[0], TYPE_CHAR) &&
        mir_buffered_console_pointer_type(
            check->proto_types[1], TYPE_CHAR) &&
        mir_buffered_console_pointer_type(
            check->proto_types[2], TYPE_CHAR) &&
        mir_buffered_console_word_type(flush->type, 0) &&
        mir_buffered_console_pointer_type(
            flush->proto_types[0], TYPE_INT) &&
        type_ptr_depth(free_function->type) == 0 &&
        (free_function->type & 15) == TYPE_VOID &&
        mir_buffered_console_pointer_type(
            free_function->proto_types[0], TYPE_VOID) &&
        mir_buffered_console_word_type(puts_stream->type, 0) &&
        mir_buffered_console_pointer_type(
            puts_stream->proto_types[0], TYPE_CHAR) &&
        mir_buffered_console_pointer_type(
            puts_stream->proto_types[1], TYPE_INT) &&
        mir_buffered_console_word_type(puts_function->type, 0) &&
        mir_buffered_console_pointer_type(
            puts_function->proto_types[0], TYPE_CHAR) &&
        mir_buffered_console_word_type(putchar_function->type, 0) &&
        mir_buffered_console_word_type(
            putchar_function->proto_types[0], 0) &&
        type_ptr_depth(setbuf_function->type) == 0 &&
        (setbuf_function->type & 15) == TYPE_VOID &&
        mir_buffered_console_pointer_type(
            setbuf_function->proto_types[0], TYPE_INT) &&
        mir_buffered_console_pointer_type(
            setbuf_function->proto_types[1], TYPE_CHAR) &&
        mir_buffered_console_word_type(fprint->type, 0) &&
        mir_buffered_console_pointer_type(
            fprint->proto_types[0], TYPE_INT) &&
        mir_buffered_console_pointer_type(
            fprint->proto_types[1], TYPE_CHAR) &&
        mir_buffered_console_pointer_type(clear->type, TYPE_VOID) &&
        mir_buffered_console_pointer_type(
            clear->proto_types[0], TYPE_VOID) &&
        mir_buffered_console_word_type(clear->proto_types[1], 0) &&
        mir_buffered_console_word_type(clear->proto_types[2], 1) &&
        mir_buffered_console_pointer_type(
            open_function->type, TYPE_INT) &&
        mir_buffered_console_pointer_type(
            open_function->proto_types[0], TYPE_CHAR) &&
        mir_buffered_console_pointer_type(
            open_function->proto_types[1], TYPE_CHAR) &&
        mir_buffered_console_word_type(close_function->type, 0) &&
        mir_buffered_console_pointer_type(
            close_function->proto_types[0], TYPE_INT) &&
        mir_buffered_console_word_type(remove_function->type, 0) &&
        mir_buffered_console_pointer_type(
            remove_function->proto_types[0], TYPE_CHAR);
}

static int mir_buffered_console_local(
    int instruction, int expected_offset, int pointer_base)
{
    int type;
    int storage;
    int offset;

    return mir_scalar_memory_location(
               &mir.insns[instruction], &type, &storage, &offset) &&
           storage == SC_LOCAL &&
           offset == expected_offset &&
           (pointer_base < 0
                ? mir_buffered_console_word_type(type, 0)
                : mir_buffered_console_pointer_type(type, pointer_base));
}

static int mir_match_buffered_console_runner(
    struct MirBufferedConsoleRunner *plan)
{
    static const char expected_opcodes[] =
        "LTGKCNGKNSCNGDGCGCNGKCBFTGKLTGKTGKTGKDGTGTGKCNGKTGKTGKTGKTGKTGKTGKTGKCNGKNNNNNNCNGKSCNGDGCGCNGKT"
        "GKCNGKDGKNCNGDGCGCNGKCNGKNSCNSLPNCBFDNICNCBBUWNCBCBFDNINCWLNLNCBNSJLDCINCWDCINCWTGDGKCNGKDNGKCNG"
        "DGCGCNGKTGCNGKTGKCNGCNGCGCNGKTGKCGKCGKCGKCGKNNNNNNCNGKSCNGDGKTGKDGTGTGKCNGKDGKCNGCNGKTGKNCNGTGCG"
        "CGKCNGTGKCNGCNGCGCNGKCBFLTGKJLTGKLNNDNGCGCNGKCNGDGCGCNGKTGKTGTGKNSDCBFLTGKNJLCNGKNSDGDGCGCNGKCBF"
        "TGKLDGDGKDGTGKDGKDGKNLTGKDGTGTGKCNGKTGKNCNGCNGCGCNGKDNGKDCBFLTGKJLTGDGKLTGKCE";
    static const int string_instructions[39] = {
        1, 24, 28, 31, 34, 39, 41, 48, 51, 54,
        57, 60, 63, 66, 95, 176, 200, 206, 221, 253,
        258, 260, 277, 284, 294, 313, 318, 344, 347, 349,
        359, 384, 395, 406, 411, 413, 445, 450, 456
    };
    static const struct {
        int instruction;
        int type;
        long value;
    } constants[] = {
        {4, 2, 4096}, {10, 2, 1}, {15, 2, 0}, {17, 2, 4096},
        {21, 2, 0}, {44, 2, 1}, {69, 2, 1}, {79, 2, 32},
        {84, 2, 1}, {89, 2, 0}, {91, 2, 32}, {98, 2, 1},
        {106, 2, 1}, {111, 2, 0}, {113, 2, 4096},
        {117, 2, 256}, {123, 2, 0}, {129, 2, 200},
        {135, 2, 48}, {137, 2, 10}, {143, 2, 37}, {145, 2, 0},
        {152, 1, 36}, {158, 2, 1}, {165, 2, 200},
        {168, 1, 10}, {171, 2, 201}, {174, 1, 0}, {181, 2, 1},
        {189, 2, 1}, {194, 2, 1}, {196, 2, 4096},
        {202, 2, 1}, {209, 2, 1}, {212, 2, 0}, {215, 2, 2},
        {217, 2, 0}, {224, 2, 88}, {227, 2, 36}, {230, 2, 89},
        {233, 2, 10}, {242, 2, 256}, {247, 2, 1}, {263, 2, 1},
        {270, 2, 1}, {273, 2, 0}, {281, 2, 1}, {286, 2, 3},
        {288, 2, 100}, {291, 2, 2}, {297, 2, 1}, {300, 2, 0},
        {303, 2, 7}, {305, 2, 0}, {309, 2, 0}, {327, 2, 0},
        {329, 2, 4096}, {333, 2, 1}, {338, 2, 0},
        {340, 2, 4096}, {355, 2, 0}, {365, 2, 64},
        {375, 2, 2}, {377, 2, 64}, {381, 2, 0}, {416, 2, 1},
        {424, 2, 1}, {427, 2, 0}, {430, 2, 1}, {432, 2, 0},
        {441, 2, 0}, {459, 2, 0}
    };
    static const struct {
        int instruction;
        int call_id;
        int function;
        int argument_count;
        int definitions[4];
    } calls[] = {
        {3, 0, MIR_BUFFER_PRINT, 1, {1}},
        {7, 1, MIR_BUFFER_ALLOCATE, 1, {4}},
        {20, 2, MIR_BUFFER_CONFIGURE, 4, {10, 13, 15, 17}},
        {26, 3, MIR_BUFFER_PRINT, 1, {24}},
        {30, 4, MIR_BUFFER_PRINT, 1, {28}},
        {33, 5, MIR_BUFFER_PRINT, 1, {31}},
        {36, 6, MIR_BUFFER_PRINT, 1, {34}},
        {43, 7, MIR_BUFFER_CHECK, 3, {37, 39, 41}},
        {47, 8, MIR_BUFFER_FLUSH, 1, {44}},
        {50, 9, MIR_BUFFER_PRINT, 1, {48}},
        {53, 10, MIR_BUFFER_PRINT, 1, {51}},
        {56, 11, MIR_BUFFER_PRINT, 1, {54}},
        {59, 12, MIR_BUFFER_PRINT, 1, {57}},
        {62, 13, MIR_BUFFER_PRINT, 1, {60}},
        {65, 14, MIR_BUFFER_PRINT, 1, {63}},
        {68, 15, MIR_BUFFER_PRINT, 1, {66}},
        {72, 16, MIR_BUFFER_FLUSH, 1, {69}},
        {82, 22, MIR_BUFFER_ALLOCATE, 1, {79}},
        {94, 18, MIR_BUFFER_CONFIGURE, 4, {84, 87, 89, 91}},
        {97, 19, MIR_BUFFER_PRINT, 1, {95}},
        {101, 20, MIR_BUFFER_FLUSH, 1, {98}},
        {104, 21, MIR_BUFFER_FREE, 1, {102}},
        {116, 23, MIR_BUFFER_CONFIGURE, 4, {106, 109, 111, 113}},
        {120, 24, MIR_BUFFER_ALLOCATE, 1, {117}},
        {180, 25, MIR_BUFFER_PRINT, 2, {176, 178}},
        {184, 26, MIR_BUFFER_FLUSH, 1, {181}},
        {188, 27, MIR_BUFFER_FREE, 1, {185}},
        {199, 28, MIR_BUFFER_CONFIGURE, 4, {189, 192, 194, 196}},
        {205, 29, MIR_BUFFER_PUTS_STREAM, 2, {200, 202}},
        {208, 30, MIR_BUFFER_PUTS, 1, {206}},
        {220, 31, MIR_BUFFER_CONFIGURE, 4, {209, 212, 215, 217}},
        {223, 32, MIR_BUFFER_PRINT, 1, {221}},
        {226, 33, MIR_BUFFER_PUTCHAR, 1, {224}},
        {229, 34, MIR_BUFFER_PUTCHAR, 1, {227}},
        {232, 35, MIR_BUFFER_PUTCHAR, 1, {230}},
        {235, 36, MIR_BUFFER_PUTCHAR, 1, {233}},
        {245, 45, MIR_BUFFER_ALLOCATE, 1, {242}},
        {252, 38, MIR_BUFFER_SETBUF, 2, {247, 250}},
        {255, 39, MIR_BUFFER_PRINT, 1, {253}},
        {262, 40, MIR_BUFFER_CHECK, 3, {256, 258, 260}},
        {266, 41, MIR_BUFFER_FLUSH, 1, {263}},
        {269, 42, MIR_BUFFER_FREE, 1, {267}},
        {276, 43, MIR_BUFFER_SETBUF, 2, {270, 273}},
        {279, 44, MIR_BUFFER_PRINT, 1, {277}},
        {290, 46, MIR_BUFFER_FPRINT, 4, {281, 284, 286, 288}},
        {296, 47, MIR_BUFFER_FPRINT, 2, {291, 294}},
        {308, 48, MIR_BUFFER_CONFIGURE, 4, {297, 300, 303, 305}},
        {315, 49, MIR_BUFFER_PRINT, 1, {313}},
        {320, 50, MIR_BUFFER_PRINT, 1, {318}},
        {332, 51, MIR_BUFFER_CLEAR, 3, {324, 327, 329}},
        {343, 52, MIR_BUFFER_CONFIGURE, 4, {333, 336, 338, 340}},
        {346, 53, MIR_BUFFER_PRINT, 1, {344}},
        {351, 54, MIR_BUFFER_OPEN, 2, {347, 349}},
        {361, 55, MIR_BUFFER_PRINT, 1, {359}},
        {368, 56, MIR_BUFFER_ALLOCATE, 1, {365}},
        {380, 57, MIR_BUFFER_CONFIGURE, 4, {371, 373, 375, 377}},
        {386, 58, MIR_BUFFER_PRINT, 1, {384}},
        {392, 59, MIR_BUFFER_SETBUF, 2, {388, 390}},
        {397, 60, MIR_BUFFER_FPRINT, 2, {393, 395}},
        {400, 61, MIR_BUFFER_CLOSE, 1, {398}},
        {403, 62, MIR_BUFFER_FREE, 1, {401}},
        {408, 63, MIR_BUFFER_PRINT, 1, {406}},
        {415, 64, MIR_BUFFER_CHECK, 3, {409, 411, 413}},
        {419, 65, MIR_BUFFER_FLUSH, 1, {416}},
        {422, 66, MIR_BUFFER_REMOVE, 1, {420}},
        {435, 67, MIR_BUFFER_CONFIGURE, 4, {424, 427, 430, 432}},
        {439, 68, MIR_BUFFER_FREE, 1, {436}},
        {447, 69, MIR_BUFFER_PRINT, 1, {445}},
        {454, 70, MIR_BUFFER_PRINT, 2, {450, 452}},
        {458, 71, MIR_BUFFER_PRINT, 1, {456}}
    };
    static const int first_calls[MIR_BUFFER_FUNCTION_COUNT] = {
        3, 7, 20, 43, 47, 104, 205, 208, 226, 252,
        290, 332, 351, 400, 422
    };
    static const int fixed_arguments[MIR_BUFFER_FUNCTION_COUNT] = {
        1, 1, 4, 3, 1, 1, 2, 1, 1, 2, 2, 3, 2, 1, 1
    };
    static const int variadic[MIR_BUFFER_FUNCTION_COUNT] = {
        1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0
    };
    static const int big_locations[] = {
        9, 13, 37, 109, 192, 324, 336, 409, 436
    };
    static const int small_locations[] = {83, 87, 102};
    static const int line_locations[] = {
        122, 132, 148, 164, 170, 178, 185
    };
    static const int index_locations[] = {125, 127, 161};
    static const int setbuf_locations[] = {246, 250, 256, 267};
    static const int stream_locations[] = {
        353, 354, 371, 388, 393, 398
    };
    static const int file_buffer_locations[] = {370, 373, 390, 401};
    int arguments[4];
    int instruction;
    int first;
    int second;
    int item;
    int clear_destination;
    int clear_fill;
    int clear_count;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 461 || mir_cfg_block_count() != 16 ||
        mir.has_vla || mir.local_bytes != 14 ||
        mir.aggregate_temp_bytes != 0 ||
        !mir_buffered_console_word_type(mir.return_type, 0) ||
        strlen(expected_opcodes) != (size_t)mir.count)
        return mir_machine_reject(
            "buffered-console-runner", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir_gnarly_opcode_code(mir.insns[instruction].opcode) !=
                expected_opcodes[instruction])
            return mir_machine_reject(
                "buffered-console-runner", "opcode");

    for (item = 0; item < MIR_BUFFER_FUNCTION_COUNT; ++item) {
        const struct MirInsn *call = &mir.insns[first_calls[item]];
        const char *call_name;

        plan->functions[item] = mir_buffered_console_function(
            first_calls[item], variadic[item],
            fixed_arguments[item]);
        if (plan->functions[item] == NULL)
            return mir_machine_reject(
                "buffered-console-runner", "function");
        call_name = call->base_name[0] != 0
            ? call->base_name
            : asm_name_for(sym_asm_name(plan->functions[item]));
        snprintf(plan->call_names[item],
                 sizeof(plan->call_names[item]), "%s", call_name);
        for (first = 0; first < item; ++first)
            if (plan->functions[first] == plan->functions[item])
                return mir_machine_reject(
                    "buffered-console-runner", "function-alias");
    }
    if (!mir_buffered_console_function_types(plan))
        return mir_machine_reject(
            "buffered-console-runner", "function-type");

    for (item = 0;
         item < (int)(sizeof(calls) / sizeof(calls[0]));
         ++item) {
        const struct MirInsn *call =
            &mir.insns[calls[item].instruction];
        const char *call_name = call->base_name[0] != 0
            ? call->base_name
            : asm_name_for(sym_asm_name(
                plan->functions[calls[item].function]));

        if (call->secondary_offset != calls[item].call_id ||
            find_global(call->name) !=
                plan->functions[calls[item].function] ||
            call->memory_flags !=
                (plan->functions[calls[item].function]->proto_variadic
                    ? MIR_CALL_FLAG_VARIADIC : 0) ||
            call->type !=
                plan->functions[calls[item].function]->type ||
            strcmp(call_name,
                   plan->call_names[calls[item].function]) ||
            !mir_machine_call_arguments(
                call, calls[item].argument_count, arguments))
            return mir_machine_reject(
                "buffered-console-runner", "call");
        for (first = 0; first < calls[item].argument_count; ++first)
            if (arguments[first] !=
                    mir.insns[calls[item].definitions[first]].dst)
                return mir_machine_reject(
                    "buffered-console-runner", "call-argument");
    }

    for (item = 0;
         item < (int)(sizeof(constants) / sizeof(constants[0]));
         ++item)
        if (mir.insns[constants[item].instruction].type !=
                constants[item].type ||
            !mir_machine_constant_equals(
                mir.insns[constants[item].instruction].dst,
                constants[item].value))
            return mir_machine_reject(
                "buffered-console-runner", "constant");

    for (item = 0; item < 39; ++item) {
        const struct MirInsn *string =
            &mir.insns[string_instructions[item]];

        if (!mir_buffered_console_pointer_type(
                string->type, TYPE_CHAR))
            return mir_machine_reject(
                "buffered-console-runner", "string-type");
        plan->strings[item] = (int)string->immediate;
        for (second = 0; second < item; ++second)
            if (plan->strings[item] == plan->strings[second])
                return mir_machine_reject(
                    "buffered-console-runner", "string-alias");
    }
    if (mir.insns[420].immediate != plan->strings[28] ||
        mir.insns[420].type != mir.insns[347].type)
        return mir_machine_reject(
            "buffered-console-runner", "string-reuse");

#define MIR_BUFFER_LOCAL_GROUP(group, offset, base)                         \
    do {                                                                    \
        if (!mir_buffered_console_local((group)[0], (offset), (base)))      \
            return mir_machine_reject(                                      \
                "buffered-console-runner", "local");                       \
        for (item = 1;                                                      \
             item < (int)(sizeof(group) / sizeof((group)[0]));              \
             ++item)                                                        \
            if (!mir_machine_same_location(                                 \
                    &mir.insns[(group)[0]], &mir.insns[(group)[item]]))      \
                return mir_machine_reject(                                  \
                    "buffered-console-runner", "local-alias");             \
    } while (0)
    MIR_BUFFER_LOCAL_GROUP(big_locations, -2, TYPE_CHAR);
    MIR_BUFFER_LOCAL_GROUP(line_locations, -4, TYPE_CHAR);
    MIR_BUFFER_LOCAL_GROUP(index_locations, -6, -1);
    MIR_BUFFER_LOCAL_GROUP(small_locations, -8, TYPE_CHAR);
    MIR_BUFFER_LOCAL_GROUP(setbuf_locations, -10, TYPE_CHAR);
    MIR_BUFFER_LOCAL_GROUP(stream_locations, -12, TYPE_INT);
    MIR_BUFFER_LOCAL_GROUP(file_buffer_locations, -14, TYPE_CHAR);
#undef MIR_BUFFER_LOCAL_GROUP

    if (mir.insns[9].src1 != mir.insns[7].dst ||
        mir.insns[83].src1 != mir.insns[82].dst ||
        mir.insns[122].src1 != mir.insns[120].dst ||
        mir.insns[246].src1 != mir.insns[245].dst ||
        mir.insns[353].src1 != mir.insns[351].dst ||
        mir.insns[370].src1 != mir.insns[368].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[9]) ||
        !mir_machine_unobservable_local_store(&mir.insns[83]) ||
        !mir_machine_unobservable_local_store(&mir.insns[122]) ||
        !mir_machine_unobservable_local_store(&mir.insns[246]) ||
        !mir_machine_unobservable_local_store(&mir.insns[353]) ||
        !mir_machine_unobservable_local_store(&mir.insns[370]))
        return mir_machine_reject(
            "buffered-console-runner", "local-flow");

    if (mir.insns[22].immediate != TOK_NE ||
        mir.insns[22].src1 != mir.insns[20].dst ||
        mir.insns[22].src2 != mir.insns[21].dst ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[23].label != mir.insns[27].label ||
        mir.insns[310].immediate != TOK_EQ ||
        mir.insns[310].src1 != mir.insns[308].dst ||
        mir.insns[310].src2 != mir.insns[309].dst ||
        mir.insns[311].src1 != mir.insns[310].dst ||
        mir.insns[311].label != mir.insns[317].label ||
        mir.insns[316].label != mir.insns[321].label ||
        mir.insns[356].immediate != TOK_EQ ||
        mir.insns[356].src1 != mir.insns[354].dst ||
        mir.insns[356].src2 != mir.insns[355].dst ||
        mir.insns[357].src1 != mir.insns[356].dst ||
        mir.insns[357].label != mir.insns[364].label ||
        mir.insns[363].label != mir.insns[405].label ||
        mir.insns[382].immediate != TOK_NE ||
        mir.insns[382].src1 != mir.insns[380].dst ||
        mir.insns[382].src2 != mir.insns[381].dst ||
        mir.insns[383].src1 != mir.insns[382].dst ||
        mir.insns[383].label != mir.insns[387].label ||
        mir.insns[442].immediate != TOK_EQ ||
        mir.insns[442].src1 != mir.insns[440].dst ||
        mir.insns[442].src2 != mir.insns[441].dst ||
        mir.insns[443].src1 != mir.insns[442].dst ||
        mir.insns[443].label != mir.insns[449].label ||
        mir.insns[448].label != mir.insns[455].label)
        return mir_machine_reject(
            "buffered-console-runner", "branch");

    if (mir.insns[127].src1 != mir.insns[123].dst ||
        mir.insns[127].src2 != mir.insns[159].dst ||
        mir.insns[127].phi_pred1 != mir.insns[27].label ||
        mir.insns[127].phi_pred2 != mir.insns[156].label ||
        mir.insns[130].immediate != '<' ||
        mir.insns[130].src1 != mir.insns[127].dst ||
        mir.insns[130].src2 != mir.insns[129].dst ||
        mir.insns[131].src1 != mir.insns[130].dst ||
        mir.insns[131].label != mir.insns[163].label ||
        mir.insns[134].src1 != mir.insns[132].dst ||
        mir.insns[134].src2 != mir.insns[127].dst ||
        mir.insns[134].immediate != 1 ||
        mir.insns[134].memory_size != 1 ||
        mir.insns[138].immediate != '%' ||
        mir.insns[138].src1 != mir.insns[127].dst ||
        mir.insns[138].src2 != mir.insns[137].dst ||
        mir.insns[139].immediate != '+' ||
        mir.insns[139].src1 != mir.insns[135].dst ||
        mir.insns[139].src2 != mir.insns[138].dst ||
        mir.insns[140].immediate != 0 ||
        mir.insns[140].src1 != mir.insns[139].dst ||
        mir.insns[141].src1 != mir.insns[134].dst ||
        mir.insns[141].src2 != mir.insns[140].dst ||
        mir.insns[141].memory_size != 1 ||
        mir.insns[144].immediate != '%' ||
        mir.insns[144].src1 != mir.insns[127].dst ||
        mir.insns[144].src2 != mir.insns[143].dst ||
        mir.insns[146].immediate != TOK_EQ ||
        mir.insns[146].src1 != mir.insns[144].dst ||
        mir.insns[146].src2 != mir.insns[145].dst ||
        mir.insns[147].src1 != mir.insns[146].dst ||
        mir.insns[147].label != mir.insns[154].label ||
        mir.insns[150].src1 != mir.insns[148].dst ||
        mir.insns[150].src2 != mir.insns[127].dst ||
        mir.insns[150].immediate != 1 ||
        mir.insns[153].src1 != mir.insns[150].dst ||
        mir.insns[153].src2 != mir.insns[152].dst ||
        mir.insns[153].memory_size != 1 ||
        mir.insns[159].immediate != '+' ||
        mir.insns[159].src1 != mir.insns[127].dst ||
        mir.insns[159].src2 != mir.insns[158].dst ||
        mir.insns[161].src1 != mir.insns[159].dst ||
        mir.insns[162].label != mir.insns[126].label ||
        mir.insns[166].src1 != mir.insns[164].dst ||
        mir.insns[166].src2 != mir.insns[165].dst ||
        mir.insns[166].immediate != 1 ||
        mir.insns[169].src1 != mir.insns[166].dst ||
        mir.insns[169].src2 != mir.insns[168].dst ||
        mir.insns[169].memory_size != 1 ||
        mir.insns[172].src1 != mir.insns[170].dst ||
        mir.insns[172].src2 != mir.insns[171].dst ||
        mir.insns[172].immediate != 1 ||
        mir.insns[175].src1 != mir.insns[172].dst ||
        mir.insns[175].src2 != mir.insns[174].dst ||
        mir.insns[175].memory_size != 1)
        return mir_machine_reject(
            "buffered-console-runner", "line-build");

    if (!mir_machine_named_nonvolatile(&mir.insns[440]) ||
        (plan->failures = find_global(mir.insns[440].name)) == NULL ||
        plan->failures->storage == SC_FUNC ||
        !plan->failures->is_defined || plan->failures->is_array ||
        plan->failures->is_volatile ||
        !mir_buffered_console_word_type(plan->failures->type, 0) ||
        !mir_machine_same_location(
            &mir.insns[440], &mir.insns[452]) ||
        mir.insns[460].src1 != mir.insns[459].dst)
        return mir_machine_reject(
            "buffered-console-runner", "result");
    if (!mir_call_is_memset_fastcall(
            332, &clear_destination, &clear_fill, &clear_count) ||
        clear_destination != mir.insns[324].dst ||
        clear_fill != mir.insns[327].dst ||
        clear_count != mir.insns[329].dst)
        return mir_machine_reject(
            "buffered-console-runner", "clear-fastcall");
    return 1;
}

static void mir_buffered_console_emit_call(
    FILE *out, const struct MirBufferedConsoleRunner *plan,
    int function, int words)
{
    if (!strcmp(
            plan->call_names[function],
            asm_name_for(sym_asm_name(plan->functions[function]))))
        mir_machine_emit_symbol_call(out, plan->functions[function]);
    else
        fprintf(out, "\tcall %s\n", plan->call_names[function]);
    mir_emit_final_call_cleanup(out, words);
}

static void mir_buffered_console_push_string(FILE *out, int string_id)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
}

static void mir_buffered_console_push_local(FILE *out, int offset)
{
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n",
            offset, offset + 1);
}

static void mir_buffered_console_print(
    FILE *out, const struct MirBufferedConsoleRunner *plan,
    int string)
{
    mir_buffered_console_push_string(out, plan->strings[string]);
    mir_buffered_console_emit_call(
        out, plan, MIR_BUFFER_PRINT, 1);
}

static void mir_buffered_console_allocate(
    FILE *out, const struct MirBufferedConsoleRunner *plan,
    int size, int offset)
{
    fprintf(out, "\tld hl,%d\n\tpush hl\n", size);
    mir_buffered_console_emit_call(
        out, plan, MIR_BUFFER_ALLOCATE, 1);
    fprintf(out,
            "\tld (ix%+d),l\n\tld (ix%+d),h\n",
            offset, offset + 1);
}

static void mir_buffered_console_setvbuf(
    FILE *out, const struct MirBufferedConsoleRunner *plan,
    int stream_offset, int stream_value,
    int buffer_offset, int mode, int size)
{
    fprintf(out, "\tld hl,%d\n\tpush hl\n", size);
    fprintf(out, "\tld hl,%d\n\tpush hl\n", mode);
    if (buffer_offset != 0)
        mir_buffered_console_push_local(out, buffer_offset);
    else
        fputs("\tld hl,0\n\tpush hl\n", out);
    if (stream_offset != 0)
        mir_buffered_console_push_local(out, stream_offset);
    else
        fprintf(out, "\tld hl,%d\n\tpush hl\n", stream_value);
    mir_buffered_console_emit_call(
        out, plan, MIR_BUFFER_CONFIGURE, 4);
}

static void mir_buffered_console_setbuf(
    FILE *out, const struct MirBufferedConsoleRunner *plan,
    int stream_offset, int stream_value, int buffer_offset)
{
    if (buffer_offset != 0)
        mir_buffered_console_push_local(out, buffer_offset);
    else
        fputs("\tld hl,0\n\tpush hl\n", out);
    if (stream_offset != 0)
        mir_buffered_console_push_local(out, stream_offset);
    else
        fprintf(out, "\tld hl,%d\n\tpush hl\n", stream_value);
    mir_buffered_console_emit_call(
        out, plan, MIR_BUFFER_SETBUF, 2);
}

static void mir_buffered_console_free(
    FILE *out, const struct MirBufferedConsoleRunner *plan,
    int offset)
{
    mir_buffered_console_push_local(out, offset);
    mir_buffered_console_emit_call(
        out, plan, MIR_BUFFER_FREE, 1);
}

static void mir_buffered_console_expect(
    FILE *out, const struct MirBufferedConsoleRunner *plan,
    int buffer_offset, int want, int tag)
{
    mir_buffered_console_push_string(out, plan->strings[tag]);
    mir_buffered_console_push_string(out, plan->strings[want]);
    mir_buffered_console_push_local(out, buffer_offset);
    mir_buffered_console_emit_call(
        out, plan, MIR_BUFFER_CHECK, 3);
}

static void mir_buffered_console_flush(
    FILE *out, const struct MirBufferedConsoleRunner *plan)
{
    fputs("\tld hl,1\n\tpush hl\n", out);
    mir_buffered_console_emit_call(
        out, plan, MIR_BUFFER_FLUSH, 1);
}

static void mir_emit_buffered_console_runner(
    FILE *out, const struct MirBufferedConsoleRunner *plan)
{
    int initial_buffer_ok = new_label();
    int line_loop = new_label();
    int digit_ready = new_label();
    int ordinary_character = new_label();
    int invalid_rejected = new_label();
    int invalid_done = new_label();
    int file_opened = new_label();
    int file_done = new_label();
    int file_buffer_ok = new_label();
    int success = new_label();
    int result_done = new_label();

    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-6\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    mir_buffered_console_print(out, plan, 0);
    mir_buffered_console_allocate(out, plan, 4096, -2);
    mir_buffered_console_setvbuf(out, plan, 0, 1, -2, 0, 4096);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", initial_buffer_ok);
    mir_buffered_console_print(out, plan, 1);
    fprintf(out, "L%d:\n", initial_buffer_ok);

    mir_buffered_console_print(out, plan, 2);
    mir_buffered_console_print(out, plan, 3);
    mir_buffered_console_print(out, plan, 4);
    mir_buffered_console_expect(out, plan, -2, 5, 6);
    mir_buffered_console_flush(out, plan);
    mir_buffered_console_print(out, plan, 7);
    mir_buffered_console_print(out, plan, 8);
    mir_buffered_console_print(out, plan, 9);
    mir_buffered_console_print(out, plan, 10);
    mir_buffered_console_print(out, plan, 11);
    mir_buffered_console_print(out, plan, 12);
    mir_buffered_console_print(out, plan, 13);
    mir_buffered_console_flush(out, plan);

    mir_buffered_console_allocate(out, plan, 32, -4);
    mir_buffered_console_setvbuf(out, plan, 0, 1, -4, 0, 32);
    mir_buffered_console_print(out, plan, 14);
    mir_buffered_console_flush(out, plan);
    mir_buffered_console_free(out, plan, -4);

    mir_buffered_console_setvbuf(out, plan, 0, 1, -2, 0, 4096);
    mir_buffered_console_allocate(out, plan, 256, -4);
    fputs("\tld l,(ix-4)\n\tld h,(ix-3)\n"
          "\tld b,200\n\tld c,0\n\tld d,0\n", out);
    fprintf(out,
            "L%d:\n"
            "\tld a,c\n\tadd a,48\n"
            "\tld e,a\n"
            "\tld a,d\n\tor a\n"
            "\tjp nz,L%d\n"
            "\tld e,36\n\tld d,37\n"
            "L%d:\n"
            "\tld a,e\n\tld (hl),a\n\tinc hl\n"
            "\tinc c\n\tld a,c\n\tcp 10\n"
            "\tjp c,L%d\n\txor a\n\tld c,a\n"
            "L%d:\n"
            "\tdec d\n\tdjnz L%d\n"
            "\tld (hl),10\n\tinc hl\n\tld (hl),0\n",
            line_loop, ordinary_character, ordinary_character,
            digit_ready, digit_ready, line_loop);
    mir_buffered_console_push_local(out, -4);
    mir_buffered_console_push_string(out, plan->strings[15]);
    mir_buffered_console_emit_call(
        out, plan, MIR_BUFFER_PRINT, 2);
    mir_buffered_console_flush(out, plan);
    mir_buffered_console_free(out, plan, -4);

    mir_buffered_console_setvbuf(out, plan, 0, 1, -2, 1, 4096);
    fputs("\tld hl,1\n\tpush hl\n", out);
    mir_buffered_console_push_string(out, plan->strings[16]);
    mir_buffered_console_emit_call(
        out, plan, MIR_BUFFER_PUTS_STREAM, 2);
    mir_buffered_console_push_string(out, plan->strings[17]);
    mir_buffered_console_emit_call(
        out, plan, MIR_BUFFER_PUTS, 1);

    mir_buffered_console_setvbuf(out, plan, 0, 1, 0, 2, 0);
    mir_buffered_console_print(out, plan, 18);
    fputs("\tld hl,88\n\tpush hl\n", out);
    mir_buffered_console_emit_call(
        out, plan, MIR_BUFFER_PUTCHAR, 1);
    fputs("\tld hl,36\n\tpush hl\n", out);
    mir_buffered_console_emit_call(
        out, plan, MIR_BUFFER_PUTCHAR, 1);
    fputs("\tld hl,89\n\tpush hl\n", out);
    mir_buffered_console_emit_call(
        out, plan, MIR_BUFFER_PUTCHAR, 1);
    fputs("\tld hl,10\n\tpush hl\n", out);
    mir_buffered_console_emit_call(
        out, plan, MIR_BUFFER_PUTCHAR, 1);

    mir_buffered_console_allocate(out, plan, 256, -4);
    mir_buffered_console_setbuf(out, plan, 0, 1, -4);
    mir_buffered_console_print(out, plan, 19);
    mir_buffered_console_expect(out, plan, -4, 20, 21);
    mir_buffered_console_flush(out, plan);
    mir_buffered_console_free(out, plan, -4);
    mir_buffered_console_setbuf(out, plan, 0, 1, 0);
    mir_buffered_console_print(out, plan, 22);

    fputs("\tld hl,100\n\tpush hl\n"
          "\tld hl,3\n\tpush hl\n", out);
    mir_buffered_console_push_string(out, plan->strings[23]);
    fputs("\tld hl,1\n\tpush hl\n", out);
    mir_buffered_console_emit_call(
        out, plan, MIR_BUFFER_FPRINT, 4);
    mir_buffered_console_push_string(out, plan->strings[24]);
    fputs("\tld hl,2\n\tpush hl\n", out);
    mir_buffered_console_emit_call(
        out, plan, MIR_BUFFER_FPRINT, 2);

    mir_buffered_console_setvbuf(out, plan, 0, 1, 0, 7, 0);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", invalid_rejected);
    mir_buffered_console_print(out, plan, 25);
    fprintf(out, "\tjp L%d\nL%d:\n",
            invalid_done, invalid_rejected);
    mir_buffered_console_print(out, plan, 26);
    fprintf(out, "L%d:\n", invalid_done);

    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n"
          "\tld bc,4096\n\tld de,0\n", out);
    mir_emit_runtime_call(out, "__msf");
    mir_buffered_console_setvbuf(out, plan, 0, 1, -2, 0, 4096);
    mir_buffered_console_print(out, plan, 27);

    mir_buffered_console_push_string(out, plan->strings[29]);
    mir_buffered_console_push_string(out, plan->strings[28]);
    mir_buffered_console_emit_call(
        out, plan, MIR_BUFFER_OPEN, 2);
    fputs("\tld (ix-4),l\n\tld (ix-3),h\n"
          "\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", file_opened);
    mir_buffered_console_print(out, plan, 30);
    fprintf(out, "\tjp L%d\nL%d:\n", file_done, file_opened);

    mir_buffered_console_allocate(out, plan, 64, -6);
    mir_buffered_console_setvbuf(out, plan, -4, 0, -6, 2, 64);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", file_buffer_ok);
    mir_buffered_console_print(out, plan, 31);
    fprintf(out, "L%d:\n", file_buffer_ok);
    mir_buffered_console_setbuf(out, plan, -4, 0, -6);
    mir_buffered_console_push_string(out, plan->strings[32]);
    mir_buffered_console_push_local(out, -4);
    mir_buffered_console_emit_call(
        out, plan, MIR_BUFFER_FPRINT, 2);
    mir_buffered_console_push_local(out, -4);
    mir_buffered_console_emit_call(
        out, plan, MIR_BUFFER_CLOSE, 1);
    mir_buffered_console_free(out, plan, -6);
    fprintf(out, "L%d:\n", file_done);

    mir_buffered_console_print(out, plan, 33);
    mir_buffered_console_expect(out, plan, -2, 34, 35);
    mir_buffered_console_flush(out, plan);
    mir_buffered_console_push_string(out, plan->strings[28]);
    mir_buffered_console_emit_call(
        out, plan, MIR_BUFFER_REMOVE, 1);

    mir_buffered_console_setvbuf(out, plan, 0, 1, 0, 1, 0);
    mir_buffered_console_free(out, plan, -2);
    mir_machine_emit_global_word(out, plan->failures, 0);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", success);
    mir_machine_emit_global_word(out, plan->failures, 0);
    fputs("\tpush hl\n", out);
    mir_buffered_console_push_string(out, plan->strings[37]);
    mir_buffered_console_emit_call(
        out, plan, MIR_BUFFER_PRINT, 2);
    fprintf(out, "\tjp L%d\nL%d:\n", result_done, success);
    mir_buffered_console_print(out, plan, 36);
    fprintf(out, "L%d:\n", result_done);
    mir_buffered_console_print(out, plan, 38);
    fputs("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

struct MirPromotionOperation {
    int instruction;
    int type;
    int operand_type;
    int operation;
};

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
        if (mir.insns[instruction].opcode != expected[instruction])
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
            insn->object != stores[item].object ||
            insn->memory_size != stores[item].bytes ||
            (insn->memory_flags & (1 | 8)) != 0)
            return 0;
    }
    return mir_machine_same_location(
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
    FILE *out, const struct MirPromotionCallRunner *plan,
    int string_id, int print_slot, int words)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_emit_runtime_call(out, plan->print_names[print_slot]);
    mir_emit_final_call_cleanup(out, words);
}

static void mir_long_subtraction_emit_wide_constant(
    FILE *out, unsigned long value)
{
    fprintf(out, "\tld hl,%lu\n\tld de,%lu\n",
            value & 0xffffUL, (value >> 16) & 0xffffUL);
}

static void mir_long_subtraction_emit_local_load(
    FILE *out, int index)
{
    mir_machine_emit_ix_wide_load(out, -16 + index * 4);
}

static void mir_long_subtraction_emit_local_store(
    FILE *out, int index, unsigned long value)
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
    FILE *out, const struct MirLongSubtractionRunner *plan,
    int index)
{
    const char *name =
        mir_long_subtraction_symbol_name(plan->global_array);
    int offset = index * 4;

    if (offset == 0)
        fprintf(out, "\tld hl,(%s)\n\tld de,(%s+2)\n",
                name, name);
    else
        fprintf(out,
                "\tld hl,(%s+%d)\n\tld de,(%s+%d)\n",
                name, offset, name, offset + 2);
}

static void mir_long_subtraction_emit_global_store(
    FILE *out, const struct MirLongSubtractionRunner *plan,
    int index, unsigned long value)
{
    const char *name =
        mir_long_subtraction_symbol_name(plan->global_array);
    int offset = index * 4;

    fprintf(out, "\tld hl,%lu\n",
            value & 0xffffUL);
    if (offset == 0)
        fprintf(out, "\tld (%s),hl\n", name);
    else
        fprintf(out, "\tld (%s+%d),hl\n", name, offset);
    fprintf(out, "\tld hl,%lu\n",
            (value >> 16) & 0xffffUL);
    if (offset == 0)
        fprintf(out, "\tld (%s+2),hl\n", name);
    else
        fprintf(out, "\tld (%s+%d),hl\n",
                name, offset + 2);
}

static void mir_long_subtraction_emit_add(
    FILE *out, unsigned long value)
{
    fprintf(out,
            "\tld bc,%lu\n\tadd hl,bc\n"
            "\tex de,hl\n\tld bc,%lu\n"
            "\tadc hl,bc\n\tex de,hl\n",
            value & 0xffffUL, (value >> 16) & 0xffffUL);
}

static void mir_long_subtraction_cleanup_wide_call(
    FILE *out)
{
    fputs("\tex de,hl\n\tld hl,8\n\tadd hl,sp\n"
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
    FILE *out, const struct MirLongSubtractionRunner *plan,
    int global, int index)
{
    if (global)
        mir_long_subtraction_emit_global_load(
            out, plan, index);
    else
        mir_long_subtraction_emit_local_load(out, index);
}

static void mir_long_subtraction_finish_check(
    FILE *out, const struct MirLongSubtractionRunner *plan,
    int check)
{
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->check_strings[check]);
    mir_machine_emit_symbol_call(out, plan->check_function);
    mir_emit_final_call_cleanup(out, 3);
}

static void mir_long_subtraction_emit_direct_check(
    FILE *out, const struct MirLongSubtractionRunner *plan,
    int check, int global, int left, int right,
    int operation, int addend)
{
    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->check_wants[check]);
    mir_long_subtraction_emit_load(
        out, plan, global, left);
    if (addend >= 0)
        mir_long_subtraction_emit_add(
            out, plan->direct_addends[addend]);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_long_subtraction_emit_load(
        out, plan, global, right);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_emit_runtime_call(
        out, mir_long_subtraction_compare_name(operation));
    mir_long_subtraction_cleanup_wide_call(out);
    mir_long_subtraction_finish_check(out, plan, check);
}

static void mir_long_subtraction_emit_counter_check(
    FILE *out, const struct MirLongSubtractionRunner *plan,
    int check, int boolean)
{
    int done = new_label();

    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->check_wants[check]);
    fputs("\tld l,(ix-18)\n\tld h,(ix-17)\n", out);
    if (boolean) {
        fputs("\tld a,h\n\tor l\n\tld hl,0\n", out);
        fprintf(out, "\tjp z,L%d\n\tinc hl\nL%d:\n",
                done, done);
    }
    mir_long_subtraction_finish_check(out, plan, check);
}

static void mir_long_subtraction_emit_helper_check(
    FILE *out, const struct MirLongSubtractionRunner *plan,
    int check, int left, int right, int add)
{
    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->check_wants[check]);
    mir_long_subtraction_emit_local_load(out, right);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_long_subtraction_emit_local_load(out, left);
    if (add)
        mir_long_subtraction_emit_add(
            out, plan->helper_addend);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->helper_function);
    mir_long_subtraction_cleanup_wide_call(out);
    mir_long_subtraction_finish_check(out, plan, check);
}

static void mir_long_subtraction_emit_condition(
    FILE *out, int left, int right,
    unsigned long addend, int has_addend)
{
    mir_long_subtraction_emit_local_load(out, left);
    if (has_addend)
        mir_long_subtraction_emit_add(out, addend);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_long_subtraction_emit_local_load(out, right);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_emit_runtime_call(out, "__lts");
    mir_long_subtraction_cleanup_wide_call(out);
}

static void mir_emit_long_subtraction_runner(
    FILE *out, const struct MirLongSubtractionRunner *plan)
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

    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
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
    fputs("\tld hl,0\n\tld (ix-18),l\n"
          "\tld (ix-17),h\n", out);
    fprintf(out, "L%d:\n", while_loop);
    mir_long_subtraction_emit_condition(
        out, 0, 1,
        plan->while_condition_addend, 1);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", while_done);
    fputs("\tld l,(ix-18)\n\tld h,(ix-17)\n\tinc hl\n"
          "\tld (ix-18),l\n\tld (ix-17),h\n", out);
    mir_long_subtraction_emit_local_load(out, 0);
    mir_long_subtraction_emit_add(out, plan->while_step);
    mir_machine_emit_ix_wide_store(out, -16);
    fprintf(out,
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
    fputs("\tld hl,0\n\tld (ix-18),l\n"
          "\tld (ix-17),h\n", out);
    fprintf(out, "L%d:\n", for_loop);
    mir_long_subtraction_emit_condition(
        out, 1, 0, 0, 0);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", for_done);
    mir_long_subtraction_emit_local_load(out, 1);
    mir_long_subtraction_emit_add(out, plan->for_step);
    mir_machine_emit_ix_wide_store(out, -12);
    fprintf(out,
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
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n\tpush hl\n"
                 "\tld hl,S%d\n\tpush hl\n",
            success, plan->failure_string);
    mir_emit_runtime_call(out, plan->print_names[1]);
    mir_emit_final_call_cleanup(out, 2);
    fprintf(out, "\tjp L%d\nL%d:\n"
                 "\tld hl,S%d\n\tpush hl\n",
            finish, success, plan->success_string);
    mir_emit_runtime_call(out, plan->print_names[0]);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out, "L%d:\n", finish);
    mir_machine_emit_global_word(
        out, plan->failures, 0);
    fputs("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static void mir_emit_promotion_call_runner(
    FILE *out, const struct MirPromotionCallRunner *plan)
{
    int success = new_label();
    int check;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_promotion_print(
        out, plan, plan->intro_string, 0, 1);
    fputs("\tld hl,0\n", out);
    mir_machine_emit_global_word_store(
        out, plan->failures, 0);
    for (check = 0; check < MIR_PROMOTION_CHECK_COUNT; ++check) {
        mir_emit_final_call_constant(
            out, plan->values[check], 4);
        mir_emit_final_call_constant(
            out, plan->values[check], 4);
        fprintf(out, "\tld hl,S%d\n\tpush hl\n",
                plan->check_strings[check]);
        mir_machine_emit_symbol_call(
            out, plan->check_function);
        mir_emit_final_call_cleanup(out, 5);
    }
    mir_machine_emit_global_word(
        out, plan->failures, 0);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n\tpush hl\n", success);
    mir_promotion_print(
        out, plan, plan->failure_string, 1, 2);
    fputs("\tld hl,1\n\tret\n", out);
    fprintf(out, "L%d:\n", success);
    mir_promotion_print(
        out, plan, plan->success_string, 2, 1);
    fputs("\tld hl,0\n\tret\n", out);
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
    FILE *out, struct Sym *array, int index_offset)
{
    const char *array_name =
        asm_name_for(sym_asm_name(array));

    fprintf(out,
            "\tpush iy\n\tpop hl\n\tld h,l\n\tld l,0\n"
            "\tadd hl,hl\n\tadd hl,hl\n"
            "\tld de,%s\n\tadd hl,de\n\tpush hl\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tadd hl,hl\n\tadd hl,hl\n\tadd hl,hl\n"
            "\tpop de\n\tadd hl,de\n",
            array_name, index_offset, index_offset + 1);
}

static void mir_emit_legal_filter_count_address(
    FILE *out, const struct MirLegalMoveFilterSchedule *plan)
{
    const char *count_name =
        asm_name_for(sym_asm_name(plan->move_counts));

    fprintf(out,
            "\tpush iy\n\tpop hl\n\tadd hl,hl\n"
            "\tld de,%s\n\tadd hl,de\n",
            count_name);
}

static void mir_emit_legal_move_filter_schedule(
    FILE *out, const struct MirLegalMoveFilterSchedule *plan)
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

    fputs(";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-6\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tpush hl\n\tpop iy\n\tpush iy\n",
            plan->ply_stack_offset + 4,
            plan->ply_stack_offset + 5);
    mir_machine_emit_symbol_call(out, plan->generate_function);
    fputs("\tpop bc\n", out);
    mir_emit_legal_filter_count_address(out, plan);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld (ix-4),e\n\tld (ix-3),d\n"
          "\txor a\n\tld (ix-2),a\n\tld (ix-1),a\n", out);

    fprintf(out, "L%d:\n", copy_loop);
    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n"
          "\tld e,(ix-4)\n\tld d,(ix-3)\n"
          "\tld a,h\n\txor 80h\n\tld h,a\n"
          "\tld a,d\n\txor 80h\n\tld d,a\n"
          "\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp nc,L%d\n", copy_done);
    mir_emit_legal_filter_array_address(
        out, plan->moves, -2);
    fputs("\tpush hl\n", out);
    mir_emit_legal_filter_array_address(
        out, plan->temporary_moves, -2);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->copy_function);
    fprintf(out,
            "\tpop bc\n\tpop bc\n\tinc (ix-2)\n\tjp nz,L%d\n"
            "\tinc (ix-1)\n\tjp L%d\nL%d:\n",
            copy_loop, copy_loop, copy_done);

    mir_emit_legal_filter_count_address(out, plan);
    fputs("\txor a\n\tld (hl),a\n\tinc hl\n\tld (hl),a\n", out);
    fprintf(out,
            "\tld hl,(%s)\n\tld (ix-6),l\n\tld (ix-5),h\n"
            "\txor a\n\tld (ix-2),a\n\tld (ix-1),a\n"
            "L%d:\n",
            side_name, filter_loop);
    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n"
          "\tld e,(ix-4)\n\tld d,(ix-3)\n"
          "\tld a,h\n\txor 80h\n\tld h,a\n"
          "\tld a,d\n\txor 80h\n\tld d,a\n"
          "\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp nc,L%d\n", filter_done);
    mir_emit_legal_filter_array_address(
        out, plan->temporary_moves, -2);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->apply_function);
    fputs("\tpop bc\n\tld l,(ix-6)\n\tld h,(ix-5)\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->check_function);
    fprintf(out,
            "\tpop bc\n\tld a,h\n\tor l\n\tjp nz,L%d\n",
            reject_move);

    mir_emit_legal_filter_array_address(
        out, plan->temporary_moves, -2);
    fputs("\tpush hl\n", out);
    mir_emit_legal_filter_count_address(out, plan);
    fprintf(out,
          "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tpush iy\n\tpop hl\n\tld h,l\n\tld l,0\n"
          "\tadd hl,hl\n\tadd hl,hl\n"
          "\tld de,%s\n\tadd hl,de\n\tpush hl\n"
          "\tld l,c\n\tld h,b\n"
          "\tadd hl,hl\n\tadd hl,hl\n\tadd hl,hl\n"
          "\tpop de\n\tadd hl,de\n\tpush hl\n",
          moves_name);
    mir_machine_emit_symbol_call(out, plan->copy_function);
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_emit_legal_filter_count_address(out, plan);
    fputs("\tinc (hl)\n\tjp nz,", out);
    fprintf(out, "L%d\n\tinc hl\n\tinc (hl)\n", reject_move);

    fprintf(out, "L%d:\n", reject_move);
    mir_emit_legal_filter_array_address(
        out, plan->temporary_moves, -2);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->undo_function);
    fprintf(out,
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

enum MirStatementVmFrameOffset {
    MIR_STMT_END = -2,
    MIR_STMT_POINTER = -4,
    MIR_STMT_VALUE = -6,
    MIR_STMT_INDEX = -8,
    MIR_STMT_TEMP = -10
};

static void mir_statement_vm_sync_pc(
    FILE *out, const struct MirStatementVmSchedule *plan)
{
    fputs("\tpush iy\n\tpop hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->program_counter, 0);
}

static void mir_statement_vm_load_iy_word(
    FILE *out, int offset)
{
    fprintf(out,
            "\tld l,(iy%+d)\n\tld h,(iy%+d)\n",
            offset, offset + 1);
}

static void mir_statement_vm_load_pointer_word(
    FILE *out, int pointer_frame, int offset)
{
    fprintf(out,
            "\tld l,(ix%d)\n\tld h,(ix%d)\n",
            pointer_frame, pointer_frame + 1);
    if (offset != 0)
        fprintf(out, "\tld de,%d\n\tadd hl,de\n", offset);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n", out);
}

static void mir_statement_vm_store_pointer_word(
    FILE *out, int pointer_frame, int offset)
{
    fputs("\tpush hl\n", out);
    fprintf(out,
            "\tld l,(ix%d)\n\tld h,(ix%d)\n",
            pointer_frame, pointer_frame + 1);
    if (offset != 0)
        fprintf(out, "\tld de,%d\n\tadd hl,de\n", offset);
    fputs("\tpop de\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n",
          out);
}

static void mir_statement_vm_die(
    FILE *out, const struct MirStatementVmSchedule *plan,
    int string_id)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_machine_emit_symbol_call(out, plan->die_function);
    fputs("\tpop bc\n", out);
}

static void mir_statement_vm_indexed_global(
    FILE *out, struct Sym *root, int index_frame, int stride)
{
    fprintf(out,
            "\tld l,(ix%d)\n\tld h,(ix%d)\n",
            index_frame, index_frame + 1);
    mir_emit_mul_hl_const(out, (unsigned long)stride);
    fputs("\tex de,hl\n", out);
    mir_machine_emit_global_word(out, root, 0);
    fputs("\tadd hl,de\n", out);
}

static void mir_statement_vm_symbol_bounds(
    FILE *out, const struct MirStatementVmSchedule *plan,
    int ok_label)
{
    int nonnegative = new_label();

    fputs("\tld l,(ix-10)\n\tld h,(ix-9)\n"
          "\tbit 7,h\n", out);
    fprintf(out, "\tjp z,L%d\n", nonnegative);
    mir_statement_vm_die(
        out, plan, plan->bounds_string_id);
    fprintf(out, "L%d:\n", nonnegative);
    mir_machine_emit_global_word(
        out, plan->memory_capacity, 0);
    fputs("\tex de,hl\n\tld l,(ix-10)\n\tld h,(ix-9)\n"
          "\tinc hl\n\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp c,L%d\n", ok_label);
    mir_statement_vm_die(
        out, plan, plan->bounds_string_id);
}

static void mir_statement_vm_store_symbol_value(
    FILE *out, const struct MirStatementVmSchedule *plan)
{
    int word_value = new_label();
    int bounds_ok = new_label();
    int done = new_label();

    mir_statement_vm_indexed_global(
        out, plan->symbols, MIR_STMT_INDEX,
        plan->symbol_stride);
    fputs("\tld (ix-4),l\n\tld (ix-3),h\n", out);
    fprintf(out, "\tld de,%d\n\tadd hl,de\n"
                 "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                 "\tld hl,%d\n\tor a\n\tsbc hl,de\n",
            plan->symbol_type_offset, plan->byte_type);
    fprintf(out, "\tjp nz,L%d\n", word_value);
    mir_statement_vm_load_pointer_word(
        out, MIR_STMT_POINTER, plan->symbol_base_offset);
    fputs("\tld (ix-10),l\n\tld (ix-9),h\n", out);
    mir_machine_emit_global_word(out, plan->memory, 0);
    fputs("\tld e,(ix-10)\n\tld d,(ix-9)\n\tadd hl,de\n"
          "\tld a,(ix-6)\n\tld (hl),a\n", out);
    fprintf(out, "\tjp L%d\n", done);

    fprintf(out, "L%d:\n", word_value);
    mir_statement_vm_load_pointer_word(
        out, MIR_STMT_POINTER, plan->symbol_base_offset);
    fputs("\tld (ix-10),l\n\tld (ix-9),h\n", out);
    mir_statement_vm_symbol_bounds(out, plan, bounds_ok);
    fprintf(out, "L%d:\n", bounds_ok);
    mir_machine_emit_global_word(out, plan->memory, 0);
    fputs("\tld e,(ix-10)\n\tld d,(ix-9)\n\tadd hl,de\n"
          "\tld e,(ix-6)\n\tld d,(ix-5)\n"
          "\tld (hl),e\n\tinc hl\n\tld (hl),d\n", out);
    fprintf(out, "L%d:\n", done);
}

static void mir_statement_vm_evaluate_field(
    FILE *out, const struct MirStatementVmSchedule *plan,
    int field_offset)
{
    mir_statement_vm_load_iy_word(out, field_offset);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->evaluate_function);
    fputs("\tpop bc\n", out);
}

static void mir_statement_vm_advance(
    FILE *out, const struct MirStatementVmSchedule *plan,
    int dispatch)
{
    fprintf(out, "\tld de,%d\n\tadd iy,de\n\tjp L%d\n",
            plan->statement_stride, dispatch);
}

static void mir_emit_statement_vm_schedule(
    FILE *out, const struct MirStatementVmSchedule *plan)
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
    fputs(";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-10\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_word(out, plan->statements, 0);
    fputs("\tpush hl\n\tpop iy\n", out);
    mir_machine_emit_global_word(out, plan->statement_count, 0);
    mir_emit_mul_hl_const(
        out, (unsigned long)plan->statement_stride);
    fputs("\tpush iy\n\tpop de\n\tadd hl,de\n"
          "\tld (ix-2),l\n\tld (ix-1),h\n", out);

    fprintf(out, "L%d:\n", dispatch);
    mir_statement_vm_sync_pc(out, plan);
    mir_machine_emit_global_word(out, plan->halted, 0);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", done);
    fputs("\tpush iy\n\tpop hl\n", out);
    mir_machine_emit_global_word(out, plan->statements, 0);
    fputs("\tex de,hl\n\tpush iy\n\tpop hl\n"
          "\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp c,L%d\n", done);
    fputs("\tpush iy\n\tpop hl\n"
          "\tld e,(ix-2)\n\tld d,(ix-1)\n"
          "\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp nc,L%d\n", done);
    fprintf(out,
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
        fprintf(out, "\tdw L%d\n", cases[item]);
    fprintf(out, "L%d:\n\tjp L%d\n", bad_opcode, advance);

    fprintf(out, "L%d:\n\tjp L%d\n", cases[0], advance);
    fprintf(out, "L%d:\n\tld hl,1\n", cases[1]);
    mir_machine_emit_global_word_store(out, plan->halted, 0);
    fprintf(out, "\tjp L%d\n", dispatch);

    fprintf(out, "L%d:\n", cases[2]);
    mir_machine_emit_symbol_call(out, plan->return_function);
    mir_machine_emit_global_word(out, plan->program_counter, 0);
    fputs("\tpush hl\n\tpop iy\n", out);
    fprintf(out, "\tjp L%d\n", dispatch);

    fprintf(out, "L%d:\n\tpush iy\n", cases[3]);
    mir_machine_emit_symbol_call(out, plan->write_function);
    fputs("\tpop bc\n", out);
    fprintf(out, "\tjp L%d\n", advance);

    {
        int target_ok = new_label();
        int stack_ok = new_label();

        fprintf(out, "L%d:\n", cases[4]);
        mir_statement_vm_load_iy_word(
            out, plan->statement_target_offset);
        fputs("\tld a,h\n\tor l\n", out);
        fprintf(out, "\tjp nz,L%d\n", target_ok);
        mir_statement_vm_die(
            out, plan, plan->bad_call_string_id);
        fprintf(out, "L%d:\n"
                     "\tld (ix-4),l\n\tld (ix-3),h\n",
                target_ok);
        mir_machine_emit_global_word(out, plan->call_count, 0);
        fprintf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                     "\tjp c,L%d\n",
                plan->call_limit, stack_ok);
        mir_statement_vm_die(
            out, plan, plan->call_stack_string_id);
        fprintf(out, "L%d:\n", stack_ok);
        mir_machine_emit_global_word(out, plan->call_count, 0);
        fputs("\tld (ix-8),l\n\tld (ix-7),h\n\tinc hl\n",
              out);
        mir_machine_emit_global_word_store(
            out, plan->call_count, 0);
        mir_statement_vm_indexed_global(
            out, plan->calls, MIR_STMT_INDEX, 2);
        fputs("\tpush iy\n\tpop de\n", out);
        fprintf(out, "\tld bc,%d\n\tex de,hl\n\tadd hl,bc\n"
                     "\tex de,hl\n\tld (hl),e\n\tinc hl\n"
                     "\tld (hl),d\n",
                plan->statement_stride);
        fputs("\tld l,(ix-4)\n\tld h,(ix-3)\n"
              "\tpush hl\n\tpop iy\n", out);
        fprintf(out, "\tjp L%d\n", dispatch);
    }

    {
        int stack_ok = new_label();

        fprintf(out, "L%d:\n", cases[5]);
        mir_machine_emit_global_word(out, plan->loop_count, 0);
        fprintf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                     "\tjp c,L%d\n",
                plan->loop_limit, stack_ok);
        mir_statement_vm_die(
            out, plan, plan->loop_stack_string_id);
        fprintf(out, "L%d:\n", stack_ok);
        mir_statement_vm_evaluate_field(
            out, plan, plan->statement_ae_offset);
        fputs("\tld (ix-6),l\n\tld (ix-5),h\n", out);
        mir_statement_vm_load_iy_word(
            out, plan->statement_symbol_offset);
        fputs("\tld (ix-8),l\n\tld (ix-7),h\n", out);
        mir_statement_vm_store_symbol_value(out, plan);

        mir_machine_emit_global_word(out, plan->loop_count, 0);
        fputs("\tld (ix-8),l\n\tld (ix-7),h\n\tinc hl\n",
              out);
        mir_machine_emit_global_word_store(
            out, plan->loop_count, 0);
        mir_statement_vm_indexed_global(
            out, plan->loops, MIR_STMT_INDEX,
            plan->loop_stride);
        fputs("\tld (ix-4),l\n\tld (ix-3),h\n", out);
        mir_statement_vm_load_iy_word(
            out, plan->statement_target_offset);
        mir_statement_vm_store_pointer_word(
            out, MIR_STMT_POINTER, plan->loop_label_offset);
        fputs("\tpush iy\n\tpop hl\n", out);
        fprintf(out, "\tld de,%d\n\tadd hl,de\n",
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
        fprintf(out, "\tjp L%d\n", advance);
    }

    {
        int no_loop = new_label();
        int label_match = new_label();
        int negative_step = new_label();
        int continue_loop = new_label();
        int finish_loop = new_label();

        fprintf(out, "L%d:\n", cases[6]);
        mir_machine_emit_global_word(out, plan->loop_count, 0);
        fputs("\tld a,h\n\tor l\n", out);
        fprintf(out, "\tjp z,L%d\n", no_loop);
        fputs("\tdec hl\n\tld (ix-8),l\n\tld (ix-7),h\n",
              out);
        mir_statement_vm_indexed_global(
            out, plan->loops, MIR_STMT_INDEX,
            plan->loop_stride);
        fputs("\tld (ix-4),l\n\tld (ix-3),h\n", out);
        mir_statement_vm_load_pointer_word(
            out, MIR_STMT_POINTER, plan->loop_label_offset);
        fputs("\tex de,hl\n\tpush iy\n\tpop hl\n"
              "\tor a\n\tsbc hl,de\n", out);
        fprintf(out, "\tjp z,L%d\n\tjp L%d\n",
                label_match, no_loop);
        fprintf(out, "L%d:\n", label_match);
        mir_statement_vm_load_pointer_word(
            out, MIR_STMT_POINTER, plan->loop_step_offset);
        fputs("\tpush hl\n", out);
        mir_statement_vm_load_pointer_word(
            out, MIR_STMT_POINTER, plan->loop_symbol_offset);
        fputs("\tpush hl\n", out);
        mir_machine_emit_symbol_call(out, plan->bump_function);
        fputs("\tpop bc\n\tpop bc\n"
              "\tld (ix-6),l\n\tld (ix-5),h\n", out);
        mir_statement_vm_load_pointer_word(
            out, MIR_STMT_POINTER, plan->loop_step_offset);
        fputs("\tbit 7,h\n", out);
        fprintf(out, "\tjp nz,L%d\n", negative_step);
        fputs("\tld l,(ix-6)\n\tld h,(ix-5)\n"
              "\tld (ix-10),l\n\tld (ix-9),h\n", out);
        mir_statement_vm_load_pointer_word(
            out, MIR_STMT_POINTER, plan->loop_end_offset);
        fputs("\tex de,hl\t\n\tld l,(ix-10)\n\tld h,(ix-9)\n"
              "\tld a,h\n\txor 128\n\tld h,a\n"
              "\tld a,d\n\txor 128\n\tld d,a\n"
              "\tor a\n\tsbc hl,de\n", out);
        fprintf(out, "\tjp c,L%d\n\tjp z,L%d\n\tjp L%d\n",
                continue_loop, continue_loop, finish_loop);
        fprintf(out, "L%d:\n", negative_step);
        fputs("\tld l,(ix-6)\n\tld h,(ix-5)\n"
              "\tld (ix-10),l\n\tld (ix-9),h\n", out);
        mir_statement_vm_load_pointer_word(
            out, MIR_STMT_POINTER, plan->loop_end_offset);
        fputs("\tex de,hl\n\tld l,(ix-10)\n\tld h,(ix-9)\n"
              "\tld a,h\n\txor 128\n\tld h,a\n"
              "\tld a,d\n\txor 128\n\tld d,a\n"
              "\tor a\n\tsbc hl,de\n", out);
        fprintf(out, "\tjp nc,L%d\n\tjp L%d\n",
                continue_loop, finish_loop);
        fprintf(out, "L%d:\n", continue_loop);
        mir_statement_vm_load_pointer_word(
            out, MIR_STMT_POINTER, plan->loop_pc_offset);
        fputs("\tpush hl\n\tpop iy\n", out);
        fprintf(out, "\tjp L%d\n", dispatch);
        fprintf(out, "L%d:\n", finish_loop);
        mir_machine_emit_global_word(out, plan->loop_count, 0);
        fputs("\tdec hl\n", out);
        mir_machine_emit_global_word_store(
            out, plan->loop_count, 0);
        fprintf(out, "L%d:\n\tjp L%d\n", no_loop, advance);
    }

    {
        int false_path = new_label();
        int action_goto = new_label();
        int action_return = new_label();
        int action_assign = new_label();
        int target_ok = new_label();

        fprintf(out, "L%d:\n", cases[7]);
        mir_statement_vm_evaluate_field(
            out, plan, plan->statement_ae_offset);
        fputs("\tld a,h\n\tor l\n", out);
        fprintf(out, "\tjp z,L%d\n", false_path);
        mir_statement_vm_load_iy_word(
            out, plan->statement_action_offset);
        fprintf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                     "\tjp z,L%d\n",
                plan->action_goto, action_goto);
        mir_statement_vm_load_iy_word(
            out, plan->statement_action_offset);
        fprintf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                     "\tjp z,L%d\n",
                plan->action_return, action_return);
        mir_statement_vm_load_iy_word(
            out, plan->statement_action_offset);
        fprintf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                     "\tjp z,L%d\n\tjp L%d\n",
                plan->action_assign, action_assign, false_path);
        fprintf(out, "L%d:\n", action_goto);
        mir_statement_vm_load_iy_word(
            out, plan->statement_action_target_offset);
        fputs("\tld a,h\n\tor l\n", out);
        fprintf(out, "\tjp nz,L%d\n", target_ok);
        mir_statement_vm_die(
            out, plan, plan->bad_label_string_id);
        fprintf(out, "L%d:\n\tpush hl\n\tpop iy\n\tjp L%d\n",
                target_ok, dispatch);
        fprintf(out, "L%d:\n", action_return);
        mir_machine_emit_symbol_call(out, plan->return_function);
        mir_machine_emit_global_word(out, plan->program_counter, 0);
        fputs("\tpush hl\n\tpop iy\n", out);
        fprintf(out, "\tjp L%d\n", dispatch);
        fprintf(out, "L%d:\n", action_assign);
        mir_statement_vm_load_iy_word(
            out, plan->statement_action_rhs_offset);
        fputs("\tpush hl\n", out);
        mir_statement_vm_load_iy_word(
            out, plan->statement_action_index_offset);
        fputs("\tpush hl\n", out);
        mir_statement_vm_load_iy_word(
            out, plan->statement_action_symbol_offset);
        fputs("\tpush hl\n", out);
        mir_machine_emit_symbol_call(out, plan->assign_function);
        fputs("\tpop bc\n\tpop bc\n\tpop bc\n", out);
        fprintf(out, "L%d:\n\tjp L%d\n", false_path, advance);
    }

    {
        int target_ok = new_label();

        fprintf(out, "L%d:\n", cases[8]);
        mir_statement_vm_load_iy_word(
            out, plan->statement_target_offset);
        fputs("\tld a,h\n\tor l\n", out);
        fprintf(out, "\tjp nz,L%d\n", target_ok);
        mir_statement_vm_die(
            out, plan, plan->bad_label_string_id);
        fprintf(out, "L%d:\n\tpush hl\n\tpop iy\n\tjp L%d\n",
                target_ok, dispatch);
    }

    {
        int out_of_range = new_label();
        int target_ok = new_label();

        fprintf(out, "L%d:\n", cases[9]);
        mir_statement_vm_evaluate_field(
            out, plan, plan->statement_ae_offset);
        fputs("\tld (ix-8),l\n\tld (ix-7),h\n"
              "\tbit 7,h\n", out);
        fprintf(out, "\tjp nz,L%d\n", out_of_range);
        fputs("\tld a,h\n\tor l\n", out);
        fprintf(out, "\tjp z,L%d\n", out_of_range);
        mir_statement_vm_load_iy_word(
            out, plan->statement_target_count_offset);
        fputs("\tex de,hl\n\tld l,(ix-8)\n\tld h,(ix-7)\n"
              "\tld a,h\n\txor 128\n\tld h,a\n"
              "\tld a,d\n\txor 128\n\tld d,a\n"
              "\tor a\n\tsbc hl,de\n", out);
        fprintf(out, "\tjp c,L%d\n\tjp z,L%d\n",
                target_ok, target_ok);
        fprintf(out, "\tjp L%d\nL%d:\n",
                out_of_range, target_ok);
        fputs("\tld l,(ix-8)\n\tld h,(ix-7)\n"
              "\tdec hl\n\tadd hl,hl\n", out);
        fprintf(out, "\tld de,%d\n\tadd hl,de\n"
                     "\tpush iy\n\tpop de\n\tadd hl,de\n"
                     "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                     "\tld a,d\n\tor e\n",
                plan->statement_targets_offset);
        {
            int nonnull = new_label();

            fprintf(out, "\tjp nz,L%d\n", nonnull);
            mir_statement_vm_die(
                out, plan, plan->bad_label_string_id);
            fprintf(out, "L%d:\n\tpush de\n\tpop iy\n\tjp L%d\n",
                    nonnull, dispatch);
        }
        fprintf(out, "L%d:\n\tjp L%d\n", out_of_range, advance);
    }

    fprintf(out, "L%d:\n", cases[10]);
    mir_statement_vm_load_iy_word(
        out, plan->statement_be_offset);
    fputs("\tpush hl\n", out);
    mir_statement_vm_load_iy_word(
        out, plan->statement_ae_offset);
    fputs("\tpush hl\n", out);
    mir_statement_vm_load_iy_word(
        out, plan->statement_symbol_offset);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->assign_function);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n", out);

    fprintf(out, "L%d:\n", advance);
    mir_statement_vm_advance(out, plan, dispatch);
    fprintf(out,
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
    FILE *out, const struct MirFormatWalkSchedule *plan)
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

    fputs(";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-2\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
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
    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n", out);
    fprintf(out,
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
            count_offset, count_offset + 1);
    fputs("\tld a,h\n\txor 128\n\tld h,a\n"
          "\tld a,d\n\txor 128\n\tld d,a\n"
          "\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp nc,L%d\n", next_character);
    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n\tadd hl,hl\n",
          out);
    fprintf(out,
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
            values_offset, values_offset + 1);
    fputs("\tadd hl,de\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tpush de\n\tinc (ix-2)\n", out);
    {
        int no_carry = new_label();

        fprintf(out,
                "\tjp nz,L%d\n\tinc (ix-1)\nL%d:\n",
                no_carry, no_carry);
    }
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->integer_format_string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    fputs("\tpop bc\n\tpop bc\n", out);
    fprintf(out, "\tjp L%d\n", next_character);

    fprintf(out, "L%d:\n\tld l,a\n", ordinary);
    fputs("\trla\n\tsbc a,a\n\tld h,a\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->putchar_function);
    fputs("\tpop bc\n", out);

    fprintf(out,
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
    FILE *out, const struct MirInlineCallSumSchedule *plan)
{
    int loop = new_label();
    int done = new_label();
    int increment_high = new_label();

    fputs(";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-4\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
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
    fputs("\tpop bc\n\tadd hl,hl\n\tpush iy\n\tpop de\n"
          "\tadd hl,de\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld l,(ix-2)\n\tld h,(ix-1)\n\tadd hl,de\n"
          "\tld (ix-2),l\n\tld (ix-1),h\n"
          "\tinc (ix-4)\n", out);
    fprintf(out,
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

static int mir_call_safe_same_direct_function(
    const struct MirInsn *call, struct Sym **function)
{
    struct Sym *candidate;

    if (call->opcode != MIR_CALL ||
        !strcmp(call->name, "<indirect>") ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0 ||
        (type_size(call->type) != 0 &&
         !mir_call_safe_signed_word_type(call->type)))
        return 0;
    candidate = find_global(call->name);
    if (candidate == NULL || candidate->is_funcptr)
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
    int argument;
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
        !mir_machine_single_call_argument(
            &mir.insns[25], &argument) ||
        argument != mir.insns[23].dst ||
        !mir_call_safe_signed_word_type(mir.insns[25].type) ||
        !mir_call_safe_same_direct_function(
            &mir.insns[25], &call_function))
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
        !mir_machine_single_call_argument(
            &mir.insns[36], &argument) ||
        argument != mir.insns[34].dst ||
        !mir_call_safe_signed_word_type(mir.insns[36].type) ||
        !mir_call_safe_same_direct_function(
            &mir.insns[36], &call_function) ||
        !mir_machine_constant_equals(mir.insns[38].dst, 1) ||
        mir.insns[39].src1 != mir.insns[15].dst ||
        mir.insns[39].src2 != mir.insns[38].dst ||
        mir.insns[39].immediate != '-' ||
        !mir_machine_single_call_argument(
            &mir.insns[41], &argument) ||
        argument != mir.insns[39].dst ||
        !mir_call_safe_signed_word_type(mir.insns[41].type) ||
        !mir_call_safe_same_direct_function(
            &mir.insns[41], &call_function) ||
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
    int argument;
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

        if (!mir_machine_single_call_argument(call, &argument) ||
            argument != mir.insns[20 + member * 9].dst ||
            !mir_call_safe_signed_word_type(call->type) ||
            !mir_call_safe_same_direct_function(
                call, &call_function))
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
    FILE *out, const struct MirCallSafeWordLoopSchedule *plan)
{
    int loop = new_label();
    int done = new_label();

    fputs(";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tdec sp\n\tdec sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
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
    fputs("\tpop bc\n\tpush hl\n\tpush iy\n\tpop hl\n"
          "\tex de,hl\n\tpop hl\n\tadd hl,de\n\tex de,hl\n"
          "\tpop hl\n\tadd hl,de\n\tpush hl\n"
          "\tpush iy\n\tpop hl\n\tinc hl\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->call_function);
    fputs("\tpop bc\n\tpush hl\n\tpush iy\n\tpop hl\n"
          "\tdec hl\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->call_function);
    fputs("\tpop bc\n\tex de,hl\n\tpop hl\n\tadd hl,de\n"
          "\tpush hl\n\tpush iy\n\tpop hl\n\tex de,hl\n"
          "\tpop hl\n\tadd hl,de\n\tex de,hl\n"
          "\tpop hl\n\tadd hl,de\n"
          "\tld (ix-2),l\n\tld (ix-1),h\n", out);
    fprintf(out,
            "\tjp L%d\n"
            "L%d:\n\tld l,(ix-2)\n\tld h,(ix-1)\n"
            "\tld sp,ix\n\tpop ix\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            loop, done);
}

static void mir_emit_call_safe_member_sum_schedule(
    FILE *out, const struct MirCallSafeWordLoopSchedule *plan)
{
    int loop = new_label();
    int done = new_label();
    int member;

    fputs(";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tdec sp\n\tdec sp\n\tdec sp\n\tdec sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
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
        fputs("\tpush hl\n", out);
        fprintf(out,
                "\tld l,(iy%+d)\n\tld h,(iy%+d)\n\tpush hl\n",
                plan->member_offsets[member],
                plan->member_offsets[member] + 1);
        mir_machine_emit_symbol_call(out, plan->call_function);
        fputs("\tpop bc\n\tex de,hl\n\tpop hl\n\tadd hl,de\n",
              out);
    }
    fputs("\tpush hl\n", out);
    fprintf(out,
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
    FILE *out, const struct MirCallSafeWordLoopSchedule *plan)
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
    int argument;
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
        if (!mir_machine_single_call_argument(
                &mir.insns[call_instructions[call]], &argument) ||
            argument != mir.insns[argument_values[call]].dst ||
            mir.insns[argument_values[call]].src1 !=
                mir.insns[comparison_values[call]].dst ||
            mir.insns[argument_values[call]].immediate != 0 ||
            type_size(
                mir.insns[call_instructions[call]].type) != 0 ||
            !mir_call_safe_same_direct_function(
                &mir.insns[call_instructions[call]],
                &check_function))
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
    FILE *out, struct Sym *function)
{
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, function);
    fputs("\tpop bc\n", out);
}

static void mir_emit_bc_equal_constant(FILE *out, int value)
{
    int done = new_label();

    fprintf(out,
            "\tld h,b\n\tld l,c\n\tld de,%d\n"
            "\tor a\n\tsbc hl,de\n\tld hl,0\n"
            "\tjp nz,L%d\n\tinc l\nL%d:\n",
            value, done, done);
}

static void mir_emit_de_equal_constant(FILE *out, int value)
{
    int done = new_label();

    fprintf(out,
            "\tld h,d\n\tld l,e\n\tld bc,%d\n"
            "\tor a\n\tsbc hl,bc\n\tld hl,0\n"
            "\tjp nz,L%d\n\tinc l\nL%d:\n",
            value, done, done);
}

static void mir_emit_byte_equal_constant(
    FILE *out, char reg, int value)
{
    int done = new_label();

    fprintf(out,
            "\tld hl,0\n\tld a,%c\n\tcp %d\n"
            "\tjp nz,L%d\n\tinc l\nL%d:\n",
            reg, value, done, done);
}

static void mir_emit_constant_do_while_schedule(
    FILE *out, const struct MirConstantDoWhileSchedule *plan)
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

    fprintf(out,
            "\tld bc,0\n\tld de,0\n"
            "L%d:\n\tinc bc\n\tld a,d\n\tor e\n\tjp nz,L%d\n",
            first, first);
    mir_emit_bc_equal_constant(out, 1);
    mir_emit_call_safe_check(out, plan->check_function);

    fprintf(out,
            "\tld bc,0\n\tld de,%d\n"
            "L%d:\n\tinc bc\n\tdec de\n"
            "\tld a,d\n\tor e\n\tjp nz,L%d\n"
            "L%d:\n\tpush de\n",
            plan->second_count, second, second, second_done);
    mir_emit_bc_equal_constant(out, plan->second_count);
    mir_emit_call_safe_check(out, plan->check_function);
    fputs("\tpop de\n", out);
    mir_emit_de_equal_constant(out, 0);
    mir_emit_call_safe_check(out, plan->check_function);

    fprintf(out,
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
    fputs("\tpop de\n", out);
    mir_emit_de_equal_constant(
        out, plan->third_initial - plan->break_count);
    mir_emit_call_safe_check(out, plan->check_function);

    fprintf(out,
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
    fputs("\tpop de\n\tpop bc\n\tpush de\n", out);
    mir_emit_byte_equal_constant(out, 'c', 0);
    mir_emit_call_safe_check(out, plan->check_function);
    fputs("\tpop de\n", out);
    mir_emit_byte_equal_constant(
        out, 'e', (plan->fourth_count + 1) / 2);
    mir_emit_call_safe_check(out, plan->check_function);
    fputs("\tret\n", out);
}

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

static void mir_temp_store_hl(FILE *out, int offset);
static void mir_temp_load_hl(FILE *out, int offset);

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
    FILE *out, struct Sym *function, const char *call_name)
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
    FILE *out, const struct MirZeroAllocationSchedule *plan)
{
    int allocated = new_label();

    fprintf(out,
            "%s\n"
            ";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
            "\tpush iy\n",
            MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fputs("\tld hl,0\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->free_function);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tld hl,0\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->allocate_function);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tpush hl\n\tpop iy\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n\tld hl,S%d\n\tpush hl\n",
            allocated, plan->failure_string);
    mir_machine_emit_symbol_call(out, plan->failure_function);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out,
            "L%d:\n\tpush iy\n\tpop hl\n"
            "\tld (hl),%d\n\tpush hl\n",
            allocated, plan->stored_byte);
    mir_machine_emit_symbol_call(out, plan->free_function);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tpop iy\n;@dcc.reg free=iy\n\tret\n", out);
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
    FILE *out, struct Sym *function, int value)
{
    fprintf(out, "\tld hl,%d\n\tpush hl\n", value);
    mir_machine_emit_symbol_call(out, function);
    mir_emit_final_call_cleanup(out, 1);
}

static void mir_allocator_fail(
    FILE *out, struct Sym *failure_function, int string_id)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_machine_emit_symbol_call(out, failure_function);
    mir_emit_final_call_cleanup(out, 1);
}

static void mir_emit_allocator_reuse_schedule(
    FILE *out, const struct MirAllocatorReuseSchedule *plan)
{
    int setup_failed = new_label();
    int setup_ok = new_label();
    int first_reuse_ok = new_label();
    int second_reuse_ok = new_label();

    fprintf(out,
            "%s\n"
            ";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
            "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tdec sp\n\tdec sp\n\tdec sp\n\tdec sp\n",
            MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_allocator_call_one(
        out, plan->allocate_function, plan->sizes[0]);
    fputs("\tpush hl\n\tpop iy\n", out);
    mir_allocator_call_one(
        out, plan->allocate_function, plan->sizes[1]);
    mir_temp_store_hl(out, -2);
    fputs("\tpush iy\n\tpop hl\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", setup_failed);
    mir_temp_load_hl(out, -2);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out,
            "\tjp nz,L%d\n"
            "L%d:\n",
            setup_ok, setup_failed);
    mir_allocator_fail(
        out, plan->failure_function, plan->failure_strings[0]);
    fprintf(out, "L%d:\n", setup_ok);
    fputs("\tpush iy\n\tpop hl\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->free_function);
    mir_emit_final_call_cleanup(out, 1);

    mir_allocator_call_one(
        out, plan->allocate_function, plan->sizes[2]);
    mir_temp_store_hl(out, -4);
    fputs("\tpush iy\n\tpop de\n\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp z,L%d\n", first_reuse_ok);
    mir_allocator_fail(
        out, plan->failure_function, plan->failure_strings[1]);
    fprintf(out, "L%d:\n", first_reuse_ok);
    mir_temp_load_hl(out, -4);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->free_function);
    mir_emit_final_call_cleanup(out, 1);

    mir_allocator_call_one(
        out, plan->allocate_function, plan->sizes[0]);
    mir_temp_store_hl(out, -4);
    fputs("\tpush iy\n\tpop de\n\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp z,L%d\n", second_reuse_ok);
    mir_allocator_fail(
        out, plan->failure_function, plan->failure_strings[2]);
    fprintf(out, "L%d:\n", second_reuse_ok);
    mir_temp_load_hl(out, -4);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->free_function);
    mir_emit_final_call_cleanup(out, 1);
    mir_temp_load_hl(out, -2);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->free_function);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tld sp,ix\n\tpop ix\n\tpop iy\n"
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
    FILE *out, const struct MirAllocationGuardSchedule *plan)
{
    int guard_ok = new_label();
    int rejected_ok = new_label();

    fprintf(out,
            "%s\n"
            ";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
            "\tpush iy\n",
            MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_allocator_call_one(
        out, plan->allocate_function, plan->guard_size);
    fputs("\tpush hl\n\tpop iy\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", guard_ok);
    mir_allocator_fail(
        out, plan->failure_function,
        plan->failure_strings[0]);
    fprintf(out, "L%d:\n", guard_ok);
    mir_allocator_call_one(
        out, plan->allocate_function, plan->rejected_size);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", rejected_ok);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->failure_strings[1]);
    mir_machine_emit_symbol_call(out, plan->failure_function);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out, "L%d:\n\tpush iy\n\tpop hl\n\tpush hl\n",
            rejected_ok);
    mir_machine_emit_symbol_call(out, plan->free_function);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tpop iy\n;@dcc.reg free=iy\n\tret\n", out);
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
    FILE *out, const struct MirAllocatorTrimSchedule *plan)
{
    int initial_ok = new_label();
    int first_reuse_ok = new_label();
    int final_reuse_ok = new_label();

    fprintf(out,
            "%s\n"
            ";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
            "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tdec sp\n\tdec sp\n",
            MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_allocator_call_one(
        out, plan->allocate_function, plan->first_size);
    fputs("\tpush hl\n\tpop iy\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", initial_ok);
    mir_allocator_fail(
        out, plan->failure_function, plan->failure_strings[0]);
    fprintf(out, "L%d:\n\tpush iy\n\tpop hl\n\tpush hl\n",
            initial_ok);
    mir_machine_emit_symbol_call(out, plan->free_function);
    mir_emit_final_call_cleanup(out, 1);

    mir_allocator_call_one(
        out, plan->allocate_function, plan->first_size);
    mir_temp_store_hl(out, -2);
    fputs("\tpush iy\n\tpop de\n\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp z,L%d\n", first_reuse_ok);
    mir_allocator_fail(
        out, plan->failure_function, plan->failure_strings[1]);
    fprintf(out, "L%d:\n", first_reuse_ok);
    mir_temp_load_hl(out, -2);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->free_function);
    mir_emit_final_call_cleanup(out, 1);

    mir_allocator_call_one(
        out, plan->allocate_function, plan->final_size);
    mir_temp_store_hl(out, -2);
    fputs("\tpush iy\n\tpop de\n\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp z,L%d\n", final_reuse_ok);
    mir_allocator_fail(
        out, plan->failure_function, plan->failure_strings[2]);
    fprintf(out, "L%d:\n", final_reuse_ok);
    mir_temp_load_hl(out, -2);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->free_function);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tld sp,ix\n\tpop ix\n\tpop iy\n"
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
    FILE *out, struct Sym *free_function, int offset)
{
    mir_temp_load_hl(out, offset);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, free_function);
    mir_emit_final_call_cleanup(out, 1);
}

static void mir_emit_allocator_coalesce_schedule(
    FILE *out, const struct MirAllocatorCoalesceSchedule *plan)
{
    int setup_failed = new_label();
    int setup_ok = new_label();
    int result_ok = new_label();

    fprintf(out,
            "%s\n"
            ";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
            "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tld hl,-6\n\tadd hl,sp\n\tld sp,hl\n",
            MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_allocator_call_one(
        out, plan->allocate_function, plan->allocation_size);
    fputs("\tpush hl\n\tpop iy\n", out);
    mir_allocator_call_one(
        out, plan->allocate_function, plan->allocation_size);
    mir_temp_store_hl(out, -2);
    mir_allocator_call_one(
        out, plan->allocate_function, plan->allocation_size);
    mir_temp_store_hl(out, -4);
    fputs("\tpush iy\n\tpop hl\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", setup_failed);
    mir_temp_load_hl(out, -2);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", setup_failed);
    mir_temp_load_hl(out, -4);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\nL%d:\n",
            setup_ok, setup_failed);
    mir_allocator_fail(
        out, plan->failure_function, plan->failure_strings[0]);
    fprintf(out, "L%d:\n", setup_ok);

    fputs("\tpush iy\n\tpop hl\n\tpush hl\n", out);
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
    fputs("\tpush iy\n\tpop de\n\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp z,L%d\n", result_ok);
    mir_allocator_fail(
        out, plan->failure_function, plan->failure_strings[1]);
    fprintf(out, "L%d:\n", result_ok);

    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->fill_value);
    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->merged_size);
    mir_temp_load_hl(out, -6);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->fill_function);
    mir_emit_final_call_cleanup(out, 3);
    mir_allocator_free_ix(out, plan->free_function, -6);
    if (plan->kind == MIR_ALLOCATOR_REVERSE)
        mir_allocator_free_ix(out, plan->free_function, -4);
    fputs("\tld sp,ix\n\tpop ix\n\tpop iy\n"
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
    FILE *out, struct Sym *function)
{
    const char *name = asm_name_for(sym_asm_name(function));

    if ((function->storage == SC_EXTERN || function->needs_extrn) &&
        mir_extrn_should_emit(function))
        fprintf(out, "\textrn %s\n", name);
    fprintf(out, "\tld hl,%s\n", name);
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
    FILE *out, const char *call_name,
    int function_number, int argument)
{
    fprintf(out, "\tld c,%d\n\tld de,%d\n",
            function_number & 0xff, argument);
    mir_emit_runtime_call(out, call_name);
}

static void mir_emit_bios_indirect_call(
    FILE *out, struct Sym *function, int function_number)
{
    fputs("\tld hl,0\n\tpush hl\n", out);
    fprintf(out, "\tld hl,%d\n\tpush hl\n", function_number);
    mir_call_recovery_emit_function_address(out, function);
    mir_emit_runtime_call(out, "__call_hl");
    mir_emit_final_call_cleanup(out, 2);
}

static void mir_emit_bios_print(
    FILE *out, const struct MirBiosCallSchedule *plan,
    int format_string, int value_string)
{
    if (value_string >= 0)
        fprintf(out, "\tld hl,S%d\n\tpush hl\n", value_string);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", format_string);
    mir_emit_runtime_call(out, plan->print_name);
    mir_emit_final_call_cleanup(
        out, value_string >= 0 ? 2 : 1);
}

static void mir_emit_bios_choose_agreement(
    FILE *out, const struct MirBiosCallSchedule *plan)
{
    int disagree = new_label();
    int ready = new_label();

    fputs("\tpush iy\n\tpop de\n\tor a\n\tsbc hl,de\n", out);
    fprintf(out,
            "\tjp nz,L%d\n\tld hl,S%d\n\tjp L%d\n"
            "L%d:\n\tld hl,S%d\nL%d:\n",
            disagree, plan->strings[3], ready,
            disagree, plan->strings[4], ready);
}

static void mir_emit_bios_agreement(
    FILE *out, const struct MirBiosCallSchedule *plan,
    int format_string)
{
    mir_emit_bios_choose_agreement(out, plan);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", format_string);
    mir_emit_runtime_call(out, plan->print_name);
    mir_emit_final_call_cleanup(out, 2);
}

static void mir_emit_bios_call_schedule(
    FILE *out, const struct MirBiosCallSchedule *plan)
{
    int item;

    fprintf(out,
            "%s\n"
            ";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
            "\tpush iy\n",
            MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_emit_bios_print(out, plan, plan->strings[0], -1);
    mir_emit_bios_direct_call(
        out, plan->bios_name, plan->status_function, 0);
    fputs("\tpush hl\n\tpop iy\n", out);
    mir_emit_bios_indirect_call(
        out, plan->bios_function, plan->status_function);
    mir_emit_bios_choose_agreement(out, plan);
    fputs("\tpush hl\n", out);
    mir_emit_bios_print(out, plan, plan->strings[1], -1);
    fputs("\tpop hl\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[2]);
    mir_emit_runtime_call(out, plan->print_name);
    mir_emit_final_call_cleanup(out, 2);
    for (item = 0; item < 5; ++item)
        mir_emit_bios_direct_call(
            out, plan->bios_name, plan->output_function,
            plan->output_characters[item]);
    mir_emit_bios_direct_call(
        out, plan->bioshl_name, plan->status_function, 0);
    fputs("\tpush hl\n\tpop iy\n", out);
    mir_emit_bios_indirect_call(
        out, plan->bioshl_function, plan->status_function);
    mir_emit_bios_agreement(out, plan, plan->strings[5]);
    mir_emit_bios_print(out, plan, plan->strings[6], -1);
    fputs("\tld hl,0\n\tpop iy\n"
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
    FILE *out, int stack_offset)
{
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            stack_offset + 2, stack_offset + 3);
}

static void mir_exec_print_string(
    FILE *out, const struct MirExecArgumentSchedule *plan,
    int string_id)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_call_recovery_emit_named_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 1);
}

static void mir_exec_failure(
    FILE *out, const struct MirExecArgumentSchedule *plan,
    int string_id)
{
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_call_recovery_emit_named_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 2);
}

static void mir_emit_exec_argument_schedule(
    FILE *out, const struct MirExecArgumentSchedule *plan)
{
    int parent = new_label();
    int first_ok = new_label();
    int second_ok = new_label();
    int return_one = new_label();
    int done = new_label();

    fprintf(out,
            "%s\n"
            "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tdec sp\n\tdec sp\n\tdec sp\n\tdec sp\n",
            MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_exec_load_parameter(out, plan->argc_stack_offset);
    fprintf(out,
            "\tbit 7,h\n\tjp nz,L%d\n"
            "\tld de,2\n\tor a\n\tsbc hl,de\n\tjp c,L%d\n",
            parent, parent);
    mir_exec_load_parameter(out, plan->argv_stack_offset);
    fputs("\tinc hl\n\tinc hl\n"
          "\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n"
          "\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[0]);
    mir_call_recovery_emit_named_call(
        out, plan->compare_function, plan->compare_name);
    mir_emit_final_call_cleanup(out, 2);
    fprintf(out, "\tld a,h\n\tor l\n\tjp nz,L%d\n", parent);
    mir_exec_load_parameter(out, plan->argv_stack_offset);
    fputs("\tpush hl\n", out);
    mir_exec_load_parameter(out, plan->argc_stack_offset);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->child_function);
    mir_emit_final_call_cleanup(out, 2);
    fprintf(out, "\tjp L%d\nL%d:\n", done, parent);

    mir_exec_print_string(out, plan, plan->strings[1]);
    fprintf(out,
            "\tld hl,S%d\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->strings[3], plan->strings[2]);
    mir_call_recovery_emit_named_call(
        out, plan->exec_function, plan->exec_name);
    mir_emit_final_call_cleanup(out, 2);
    fputs("\tld de,-1\n\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp z,L%d\n\tld de,-1\n\tadd hl,de\n",
            first_ok);
    mir_exec_failure(out, plan, plan->strings[4]);
    fprintf(out, "\tjp L%d\nL%d:\n", return_one, first_ok);

    mir_exec_print_string(out, plan, plan->strings[5]);
    fputs("\tpush ix\n\tpop hl\n\tld de,-4\n\tadd hl,de\n", out);
    fprintf(out,
            "\tld de,S%d\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n"
            "\tinc hl\n\txor a\n\tld (hl),a\n\tinc hl\n\tld (hl),a\n",
            plan->strings[2]);
    fputs("\tpush ix\n\tpop hl\n\tld de,-4\n\tadd hl,de\n"
          "\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[2]);
    mir_call_recovery_emit_named_call(
        out, plan->execv_function, plan->execv_name);
    mir_emit_final_call_cleanup(out, 2);
    fputs("\tld de,-1\n\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp z,L%d\n\tld de,-1\n\tadd hl,de\n",
            second_ok);
    mir_exec_failure(out, plan, plan->strings[6]);
    fprintf(out, "\tjp L%d\nL%d:\n", return_one, second_ok);

    mir_exec_print_string(out, plan, plan->strings[7]);
    fprintf(out,
            "\tld hl,S%d\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->strings[9], plan->strings[8]);
    mir_call_recovery_emit_named_call(
        out, plan->exec_function, plan->exec_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_exec_print_string(out, plan, plan->strings[10]);
    fprintf(out,
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

static void mir_temp_emit_ix_address(FILE *out, int offset)
{
    fputs("\tpush ix\n\tpop hl\n", out);
    if (offset != 0)
        fprintf(out, "\tld de,%d\n\tadd hl,de\n", offset);
}

static void mir_temp_store_hl(FILE *out, int offset)
{
    fprintf(out,
            "\tld (ix%+d),l\n\tld (ix%+d),h\n",
            offset, offset + 1);
}

static void mir_temp_load_hl(FILE *out, int offset)
{
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            offset, offset + 1);
}

static void mir_temp_increment_failures(
    FILE *out, const struct MirTemporaryFileSchedule *plan)
{
    mir_machine_emit_global_word(out, plan->failure_count, 0);
    fputs("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->failure_count, 0);
}

static void mir_temp_print_one(
    FILE *out, const struct MirTemporaryFileSchedule *plan,
    int string_id)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_call_recovery_emit_named_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 1);
}

static void mir_temp_print_pointer(
    FILE *out, const struct MirTemporaryFileSchedule *plan,
    int string_id, int pointer_offset)
{
    mir_temp_load_hl(out, pointer_offset);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_call_recovery_emit_named_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 2);
}

static void mir_temp_call_check(
    FILE *out, const struct MirTemporaryFileSchedule *plan,
    int name_string, int expected)
{
    fprintf(out,
            "\tld de,%d\n\tpush de\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            expected, name_string);
    mir_call_recovery_emit_named_call(
        out, plan->check_function, plan->check_name);
    mir_emit_final_call_cleanup(out, 3);
}

static void mir_emit_temporary_file_schedule(
    FILE *out, const struct MirTemporaryFileSchedule *plan)
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

    fprintf(out,
            "%s\n"
            "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n",
            MIR_EXACT_KERNEL_MARKER, plan->frame_bytes);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fputs("\tld hl,0\n\tpush hl\n", out);
    mir_call_recovery_emit_named_call(
        out, plan->tmpnam_function, plan->tmpnam_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_temp_store_hl(out, plan->first_name_offset);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", first_ok);
    mir_temp_print_one(out, plan, plan->strings[0]);
    mir_temp_increment_failures(out, plan);
    fprintf(out, "\tjp L%d\nL%d:\n", first_done, first_ok);
    mir_temp_print_pointer(
        out, plan, plan->strings[1],
        plan->first_name_offset);
    fprintf(out, "L%d:\n", first_done);

    mir_temp_emit_ix_address(out, plan->buffer_offset);
    fputs("\tpush hl\n", out);
    mir_call_recovery_emit_named_call(
        out, plan->tmpnam_function, plan->tmpnam_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_temp_store_hl(out, plan->second_name_offset);
    mir_temp_emit_ix_address(out, plan->buffer_offset);
    fputs("\tex de,hl\n", out);
    mir_temp_load_hl(out, plan->second_name_offset);
    fputs("\tor a\n\tsbc hl,de\n", out);
    fprintf(out,
            "\tjp z,L%d\n\tld hl,0\n\tjp L%d\n"
            "L%d:\n\tld hl,1\nL%d:\n",
            pointer_equal, pointer_checked,
            pointer_equal, pointer_checked);
    mir_temp_call_check(out, plan, plan->strings[2], 1);
    mir_temp_emit_ix_address(out, plan->buffer_offset);
    fputs("\tex de,hl\n", out);
    mir_temp_load_hl(out, plan->second_name_offset);
    fputs("\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp nz,L%d\n", duplicate_done);
    mir_temp_print_pointer(
        out, plan, plan->strings[3],
        plan->second_name_offset);
    fprintf(out, "L%d:\n", duplicate_done);

    mir_temp_load_hl(out, plan->first_name_offset);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", names_checked);
    mir_temp_load_hl(out, plan->second_name_offset);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", names_checked);
    mir_temp_emit_ix_address(out, plan->buffer_offset);
    fputs("\tpush hl\n", out);
    mir_temp_load_hl(out, plan->first_name_offset);
    fputs("\tpush hl\n", out);
    mir_call_recovery_emit_named_call(
        out, plan->compare_function, plan->compare_name);
    mir_emit_final_call_cleanup(out, 2);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", names_checked);
    mir_temp_print_one(out, plan, plan->strings[4]);
    mir_temp_increment_failures(out, plan);
    fprintf(out, "L%d:\n", names_checked);

    mir_call_recovery_emit_named_call(
        out, plan->tmpfile_function, plan->tmpfile_name);
    mir_temp_store_hl(out, plan->stream_offset);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", file_ok);
    mir_temp_print_one(out, plan, plan->strings[5]);
    mir_temp_increment_failures(out, plan);
    fprintf(out, "\tjp L%d\nL%d:\n", file_done, file_ok);
    mir_temp_load_hl(out, plan->stream_offset);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[6]);
    mir_call_recovery_emit_named_call(
        out, plan->puts_function, plan->puts_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_temp_load_hl(out, plan->stream_offset);
    fputs("\tpush hl\n", out);
    mir_call_recovery_emit_named_call(
        out, plan->rewind_function, plan->rewind_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_temp_load_hl(out, plan->stream_offset);
    fputs("\tpush hl\n\tld hl,32\n\tpush hl\n", out);
    mir_temp_emit_ix_address(out, plan->read_buffer_offset);
    fputs("\tpush hl\n", out);
    mir_call_recovery_emit_named_call(
        out, plan->gets_function, plan->gets_name);
    mir_emit_final_call_cleanup(out, 3);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", content_ok);
    mir_temp_print_one(out, plan, plan->strings[7]);
    mir_temp_increment_failures(out, plan);
    fprintf(out, "\tjp L%d\nL%d:\n", close_stream, content_ok);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[6]);
    mir_temp_emit_ix_address(out, plan->read_buffer_offset);
    fputs("\tpush hl\n", out);
    mir_call_recovery_emit_named_call(
        out, plan->compare_function, plan->compare_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_temp_call_check(out, plan, plan->strings[8], 0);
    fprintf(out, "L%d:\n", close_stream);
    mir_temp_load_hl(out, plan->stream_offset);
    fputs("\tpush hl\n", out);
    mir_call_recovery_emit_named_call(
        out, plan->close_function, plan->close_name);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out, "L%d:\n", file_done);

    mir_machine_emit_global_word(out, plan->failure_count, 0);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n\tpush hl\n", summary_success);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[9]);
    mir_call_recovery_emit_named_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 2);
    fprintf(out, "\tjp L%d\nL%d:\n", done, summary_success);
    mir_temp_print_one(out, plan, plan->strings[10]);
    fprintf(out,
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
    FILE *out, const struct MirNullableStringCheckSchedule *plan)
{
    int failed = new_label();
    int got_ready = new_label();
    int want_ready = new_label();
    int done = new_label();

    fprintf(out,
            "%s\n"
            "\tpush ix\n\tld ix,0\n\tadd ix,sp\n",
            MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
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
    fprintf(out,
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
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_global_word(out, plan->failure_count, 0);
    fputs("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->failure_count, 0);
    fprintf(out,
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            done);
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
    FILE *out, struct Sym *symbol, int offset)
{
    const char *name = asm_name_for(sym_asm_name(symbol));

    if ((symbol->storage == SC_EXTERN || symbol->needs_extrn) &&
        mir_extrn_should_emit(symbol))
        fprintf(out, "\textrn %s\n", name);
    if (offset == 0)
        fprintf(out, "\tld hl,%s\n", name);
    else
        fprintf(out, "\tld hl,%s%+d\n", name, offset);
}

static void mir_emit_inline_parameter_value_call(
    FILE *out, struct Sym *function, const int arguments[2],
    int frame_offset)
{
    fprintf(out,
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,%d\n\tpush hl\n",
            arguments[1], arguments[0]);
    mir_machine_emit_symbol_call(out, function);
    fprintf(out,
            "\tpop bc\n\tpop bc\n"
            "\tld (ix%+d),l\n\tld (ix%+d),h\n",
            frame_offset, frame_offset + 1);
}

static void mir_emit_inline_parameter_call_schedule(
    FILE *out, const struct MirInlineParameterCallSchedule *plan)
{
    fprintf(out,
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
    fputs(";@dcc.mir inline-simple-store\n", out);
    mir_call_emit_global_address(
        out, plan->array, plan->array_offset);
    fprintf(out,
            "\tld a,%d\n\tld (hl),a\n"
            "\tld a,(hl)\n\tld l,a\n\tld h,0\n\tpush hl\n"
            "\tld l,(ix-4)\n\tld h,(ix-3)\n\tpush hl\n"
            "\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->store_value, plan->format_string);
    mir_emit_runtime_call(out, plan->print_name);
    fputs("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
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
    FILE *out, const struct MirGlobalMemsetSchedule *plan)
{
    fputs(MIR_EXACT_KERNEL_MARKER "\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tld d,0\n",
            plan->value_stack_offset);
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n"
            "\tpush hl\n\tpush de\n",
            plan->size_stack_offset);
    mir_machine_emit_global_address_de(
        out, plan->buffer, plan->buffer_offset);
    fputs("\tpush de\n", out);
    mir_machine_emit_symbol_call(out, plan->function);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n\tret\n", out);
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
    FILE *out, const struct MirWordTableRunnerSchedule *plan)
{
    int loop = new_label();
    int done = new_label();

    fputs(MIR_EXACT_KERNEL_MARKER "\n\tpush iy\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->start_string_id);
    mir_emit_runtime_call(out, plan->print_name);
    fputs("\tpop bc\n", out);
    mir_machine_emit_global_address_de(
        out, plan->table, plan->table_offset);
    fputs("\tpush de\n\tpop iy\n", out);
    fprintf(out,
            "L%d:\n\tld l,(iy+0)\n\tld h,(iy+1)\n"
            "\tld a,h\n\tor l\n\tjp z,L%d\n"
            "\tpush hl\n",
            loop, done);
    mir_machine_emit_symbol_call(out, plan->run_function);
    fputs("\tpop bc\n\tinc iy\n\tinc iy\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n\tld hl,S%d\n\tpush hl\n",
            loop, done, plan->done_string_id);
    mir_emit_runtime_call(out, plan->print_name);
    fputs("\tpop bc\n\tld hl,0\n\tpop iy\n\tret\n", out);
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
    FILE *out, const struct MirSeekCheckSchedule *plan)
{
    int done = new_label();

    fputs(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-4\n\tadd hl,sp\n\tld sp,hl\n",
          out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fputs("\tld hl,0\n\tpush hl\n", out);
    fprintf(out,
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
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tld (ix-2),l\n\tld (ix-1),h\n"
          "\tld (ix-4),e\n\tld (ix-3),d\n", out);
    fprintf(out,
            "\tld a,l\n\txor (ix%+d)\n\tld b,a\n"
            "\tld a,h\n\txor (ix%+d)\n\tor b\n\tld b,a\n"
            "\tld a,e\n\txor (ix%+d)\n\tor b\n\tld b,a\n"
            "\tld a,d\n\txor (ix%+d)\n\tor b\n\tjp z,L%d\n",
            plan->offset_stack_offset + 2,
            plan->offset_stack_offset + 3,
            plan->offset_stack_offset + 4,
            plan->offset_stack_offset + 5, done);
    fprintf(out,
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
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n",
            plan->where_stack_offset + 2,
            plan->where_stack_offset + 3);
    mir_machine_emit_symbol_call(out, plan->fail_function);
    fputs("\tpop bc\n", out);
    fprintf(out,
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

static void mir_roundtrip_cleanup(FILE *out, int words)
{
    while (words-- > 0)
        fputs("\tpop bc\n", out);
}

static void mir_intel_buffer_address(FILE *out, int offset)
{
    fputs("\tpush ix\n\tpop hl\n", out);
    fprintf(out, "\tld de,%d\n\tadd hl,de\n", -120 + offset);
}

static void mir_intel_error(
    FILE *out, const struct MirIntelHexLoadSchedule *plan,
    int string_id)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_machine_emit_symbol_call(out, plan->error_function);
    fputs("\tpop bc\n", out);
}

static void mir_intel_read_hex(
    FILE *out, const struct MirIntelHexLoadSchedule *plan,
    int offset, int length)
{
    fprintf(out, "\tld hl,%d\n\tpush hl\n", length);
    mir_intel_buffer_address(out, offset);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->hex_function);
    mir_roundtrip_cleanup(out, 2);
}

static void mir_emit_intel_hex_load_schedule(
    FILE *out, const struct MirIntelHexLoadSchedule *plan)
{
    int loop = new_label();
    int done = new_label();
    int header_ok = new_label();
    int data_record = new_label();
    int data_loop = new_label();
    int data_done = new_label();
    int eof_ok = new_label();

    fputs(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-126\n\tadd hl,sp\n\tld sp,hl\n",
          out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tpush hl\n\tpop iy\nL%d:\n"
            "\tpush iy\n\tld hl,120\n\tpush hl\n",
            plan->file_stack_offset + 4,
            plan->file_stack_offset + 5, loop);
    mir_intel_buffer_address(out, 0);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->line_function);
    mir_roundtrip_cleanup(out, 3);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", done);
    mir_intel_buffer_address(out, 0);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->length_function);
    fputs("\tpop bc\n\tld de,11\n\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp c,L%d\n", done);
    fputs("\tld a,(ix-120)\n\tcp ':'\n", out);
    fprintf(out, "\tjp z,L%d\n", header_ok);
    mir_intel_error(out, plan, plan->strings[0]);
    fprintf(out, "L%d:\n", header_ok);

    mir_intel_read_hex(out, plan, 1, 2);
    fputs("\tld (ix-122),l\n", out);
    mir_intel_read_hex(out, plan, 3, 4);
    fputs("\tld (ix-124),l\n\tld (ix-123),h\n", out);
    mir_intel_read_hex(out, plan, 7, 2);
    fputs("\tld a,l\n\tcp 1\n", out);
    fprintf(out, "\tjp z,L%d\n\tor a\n\tjp z,L%d\n",
            done, data_record);
    mir_intel_error(out, plan, plan->strings[1]);
    fprintf(out,
            "L%d:\n\txor a\n\tld (ix-125),a\n"
            "L%d:\n\tld a,(ix-125)\n\tld b,a\n"
            "\tld a,(ix-122)\n\tcp b\n\tjp z,L%d\n"
            "\tpush iy\n",
            data_record, data_loop, data_done);
    mir_machine_emit_symbol_call(out, plan->eof_function);
    fputs("\tpop bc\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", eof_ok);
    mir_intel_error(out, plan, plan->strings[2]);
    fprintf(out, "L%d:\n", eof_ok);
    fputs("\tld a,(ix-125)\n\tadd a,a\n\tadd a,9\n"
          "\tld c,a\n\tld hl,2\n\tpush hl\n", out);
    mir_intel_buffer_address(out, 0);
    fputs("\tld e,c\n\tld d,0\n\tadd hl,de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->hex_function);
    mir_roundtrip_cleanup(out, 2);
    fputs("\tld (ix-126),l\n"
          "\tld l,(ix-124)\n\tld h,(ix-123)\n"
          "\tld e,(ix-125)\n\tld d,0\n\tadd hl,de\n"
          "\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->memory_function);
    fputs("\tpop bc\n\tld a,(ix-126)\n\tld (hl),a\n"
          "\tld a,(ix-125)\n\tinc a\n\tld (ix-125),a\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n\tjp L%d\n",
            data_loop, data_done, loop);
    fprintf(out, "L%d:\n\tpush iy\n", done);
    mir_machine_emit_symbol_call(out, plan->close_function);
    fputs("\tpop bc\n\tld hl,1\n"
          "\tld sp,ix\n\tpop ix\n\tpop iy\n\tret\n", out);
}

static void mir_roundtrip_load_ix_word(
    FILE *out, int offset)
{
    fprintf(out, "\tld l,(ix%d)\n\tld h,(ix%d)\n",
            offset, offset + 1);
}

static void mir_roundtrip_store_ix_word(
    FILE *out, int offset)
{
    fprintf(out, "\tld (ix%d),l\n\tld (ix%d),h\n",
            offset, offset + 1);
}

static void mir_roundtrip_push_size(FILE *out)
{
    fputs("\tpush iy\n", out);
}

static void mir_roundtrip_push_buffer(
    FILE *out, const struct MirFileRoundtripSchedule *plan)
{
    mir_machine_emit_global_address_de(
        out, plan->buffer, plan->buffer_offset);
    fputs("\tpush de\n", out);
}

static void mir_roundtrip_call_one_ix(
    FILE *out, struct Sym *function, int offset)
{
    mir_roundtrip_load_ix_word(out, offset);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, function);
    fputs("\tpop bc\n", out);
}

static void mir_roundtrip_call_pattern(
    FILE *out, struct Sym *function)
{
    mir_roundtrip_call_one_ix(out, function, -4);
    mir_roundtrip_store_ix_word(out, -8);
}

static void mir_roundtrip_call_fill_check(
    FILE *out, struct Sym *function)
{
    mir_roundtrip_load_ix_word(out, -8);
    fputs("\tpush hl\n", out);
    mir_roundtrip_push_size(out);
    mir_machine_emit_symbol_call(out, function);
    mir_roundtrip_cleanup(out, 2);
}

static void mir_roundtrip_call_io(
    FILE *out, const struct MirFileRoundtripSchedule *plan,
    struct Sym *function)
{
    mir_roundtrip_push_size(out);
    mir_roundtrip_push_buffer(out, plan);
    mir_roundtrip_load_ix_word(out, -2);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, function);
    mir_roundtrip_cleanup(out, 3);
    mir_roundtrip_store_ix_word(out, -6);
}

static void mir_roundtrip_call_fail(
    FILE *out, const struct MirFileRoundtripSchedule *plan,
    int string_id)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_machine_emit_symbol_call(out, plan->fail_function);
    fputs("\tpop bc\n", out);
}

static void mir_roundtrip_open(
    FILE *out, const struct MirFileRoundtripSchedule *plan,
    int flags, int failure_string)
{
    int opened = new_label();

    fputs("\tld hl,0\n\tpush hl\n", out);
    fprintf(out, "\tld hl,%d\n\tpush hl\n", flags);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[0]);
    mir_machine_emit_symbol_call(out, plan->open_function);
    mir_roundtrip_cleanup(out, 3);
    mir_roundtrip_store_ix_word(out, -2);
    fputs("\tbit 7,h\n", out);
    fprintf(out, "\tjp z,L%d\n", opened);
    mir_roundtrip_call_fail(out, plan, failure_string);
    fprintf(out, "L%d:\n", opened);
}

static void mir_roundtrip_print_failure(
    FILE *out, const struct MirFileRoundtripSchedule *plan,
    int print_index, int format_string)
{
    fputs("\tld hl,0\n\tpush hl\n", out);
    mir_roundtrip_load_ix_word(out, -4);
    fputs("\tpush hl\n", out);
    mir_roundtrip_load_ix_word(out, -6);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", format_string);
    mir_emit_runtime_call(out, plan->print_names[print_index]);
    mir_roundtrip_cleanup(out, 4);
}

static void mir_roundtrip_multiply_offset(FILE *out)
{
    mir_roundtrip_load_ix_word(out, -4);
    fputs("\tld de,0\n\tpush de\n\tpush hl\n"
          "\tpush iy\n\tpop hl\n\tld a,h\n\trlca\n\tsbc a,a\n"
          "\tld d,a\n\tld e,a\n", out);
    mir_emit_runtime_call(out, "__lmul");
    mir_roundtrip_cleanup(out, 2);
}

static void mir_roundtrip_seek(
    FILE *out, const struct MirFileRoundtripSchedule *plan,
    int where_string)
{
    mir_roundtrip_multiply_offset(out);
    fprintf(out, "\tld bc,S%d\n\tpush bc\n", where_string);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_roundtrip_load_ix_word(out, -2);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->seek_function);
    mir_roundtrip_cleanup(out, 4);
}

static void mir_emit_file_roundtrip_schedule(
    FILE *out, const struct MirFileRoundtripSchedule *plan)
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

    fputs(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-8\n\tadd hl,sp\n\tld sp,hl\n",
          out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tpush hl\n\tpop iy\n\tpush iy\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->size_stack_offset + 4,
            plan->size_stack_offset + 5,
            plan->start_string_id);
    mir_emit_runtime_call(out, plan->print_names[0]);
    mir_roundtrip_cleanup(out, 2);
    mir_roundtrip_open(out, plan, 578, plan->strings[1]);

    fputs("\tld hl,0\n", out);
    mir_roundtrip_store_ix_word(out, -4);
    fprintf(out, "L%d:\n", first_loop);
    fputs("\tld a,(ix-3)\n\tor a\n", out);
    fprintf(out, "\tjp nz,L%d\n", first_done);
    mir_roundtrip_call_pattern(out, plan->pattern_function);
    mir_roundtrip_call_fill_check(out, plan->fill_function);
    mir_roundtrip_call_fill_check(out, plan->check_function);
    mir_roundtrip_call_io(out, plan, plan->write_function);
    mir_roundtrip_load_ix_word(out, -6);
    fputs("\tpush iy\n\tpop de\n\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp z,L%d\n", first_ok);
    mir_roundtrip_print_failure(out, plan, 1, plan->strings[2]);
    mir_roundtrip_call_fail(out, plan, plan->strings[3]);
    fprintf(out, "L%d:\n", first_ok);
    mir_roundtrip_load_ix_word(out, -4);
    fputs("\tinc hl\n", out);
    mir_roundtrip_store_ix_word(out, -4);
    fprintf(out, "\tjp L%d\nL%d:\n", first_loop, first_done);
    mir_roundtrip_call_one_ix(out, plan->sync_function, -2);
    mir_roundtrip_call_one_ix(out, plan->close_function, -2);

    mir_roundtrip_open(out, plan, 0, plan->strings[5]);
    fputs("\tld hl,0\n", out);
    mir_roundtrip_store_ix_word(out, -4);
    fprintf(out, "L%d:\n", second_loop);
    fputs("\tld a,(ix-3)\n\tor a\n", out);
    fprintf(out, "\tjp nz,L%d\n", second_done);
    mir_roundtrip_push_size(out);
    mir_machine_emit_symbol_call(out, plan->clear_function);
    fputs("\tpop bc\n", out);
    mir_roundtrip_call_io(out, plan, plan->read_function);
    mir_roundtrip_load_ix_word(out, -6);
    fputs("\tpush iy\n\tpop de\n\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp z,L%d\n", second_ok);
    mir_roundtrip_print_failure(out, plan, 2, plan->strings[6]);
    mir_roundtrip_call_fail(out, plan, plan->strings[7]);
    fprintf(out, "L%d:\n", second_ok);
    mir_roundtrip_call_pattern(out, plan->pattern_function);
    mir_roundtrip_call_fill_check(out, plan->check_function);
    mir_roundtrip_load_ix_word(out, -4);
    fputs("\tinc hl\n", out);
    mir_roundtrip_store_ix_word(out, -4);
    fprintf(out, "\tjp L%d\nL%d:\n", second_loop, second_done);
    mir_roundtrip_call_one_ix(out, plan->close_function, -2);

    mir_roundtrip_open(out, plan, 2, plan->strings[9]);
    fputs("\tld hl,0\n", out);
    mir_roundtrip_store_ix_word(out, -4);
    fprintf(out, "L%d:\n", third_loop);
    fputs("\tld a,(ix-3)\n\tor a\n", out);
    fprintf(out, "\tjp nz,L%d\n", third_done);
    fputs("\tld a,(ix-4)\n\tand 7\n", out);
    fprintf(out, "\tjp nz,L%d\n", third_skip);
    mir_roundtrip_seek(out, plan, plan->strings[10]);
    mir_roundtrip_call_pattern(
        out, plan->reverse_pattern_function);
    mir_roundtrip_call_fill_check(out, plan->fill_function);
    mir_roundtrip_call_fill_check(out, plan->check_function);
    mir_roundtrip_call_io(out, plan, plan->write_function);
    mir_roundtrip_load_ix_word(out, -6);
    fputs("\tpush iy\n\tpop de\n\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp z,L%d\n", third_ok);
    mir_roundtrip_print_failure(out, plan, 3, plan->strings[11]);
    mir_roundtrip_call_fail(out, plan, plan->strings[12]);
    fprintf(out, "L%d:\n", third_ok);
    fprintf(out, "L%d:\n", third_skip);
    mir_roundtrip_load_ix_word(out, -4);
    fputs("\tinc hl\n", out);
    mir_roundtrip_store_ix_word(out, -4);
    fprintf(out, "\tjp L%d\nL%d:\n", third_loop, third_done);
    mir_roundtrip_call_one_ix(out, plan->sync_function, -2);
    mir_roundtrip_call_one_ix(out, plan->close_function, -2);

    mir_roundtrip_open(out, plan, 0, plan->strings[14]);
    fputs("\tld hl,255\n", out);
    mir_roundtrip_store_ix_word(out, -4);
    fprintf(out, "L%d:\n", fourth_loop);
    fputs("\tbit 7,(ix-3)\n", out);
    fprintf(out, "\tjp nz,L%d\n", fourth_done);
    mir_roundtrip_seek(out, plan, plan->strings[15]);
    mir_roundtrip_push_size(out);
    mir_machine_emit_symbol_call(out, plan->clear_function);
    fputs("\tpop bc\n", out);
    mir_roundtrip_call_io(out, plan, plan->read_function);
    mir_roundtrip_load_ix_word(out, -6);
    fputs("\tpush iy\n\tpop de\n\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp z,L%d\n", fourth_ok);
    mir_roundtrip_print_failure(out, plan, 4, plan->strings[16]);
    mir_roundtrip_call_fail(out, plan, plan->strings[17]);
    fprintf(out, "L%d:\n", fourth_ok);
    fputs("\tld a,(ix-4)\n\tand 7\n", out);
    fprintf(out, "\tjp nz,L%d\n", use_pattern);
    mir_roundtrip_call_pattern(
        out, plan->reverse_pattern_function);
    fprintf(out, "\tjp L%d\nL%d:\n", value_ready, use_pattern);
    mir_roundtrip_call_pattern(out, plan->pattern_function);
    fprintf(out, "L%d:\n", value_ready);
    mir_roundtrip_call_fill_check(out, plan->check_function);
    mir_roundtrip_load_ix_word(out, -4);
    fputs("\tdec hl\n", out);
    mir_roundtrip_store_ix_word(out, -4);
    fprintf(out, "\tjp L%d\nL%d:\n", fourth_loop, fourth_done);
    mir_roundtrip_call_one_ix(out, plan->close_function, -2);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[18]);
    mir_machine_emit_symbol_call(out, plan->unlink_function);
    fputs("\tpop bc\n\tpush iy\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[19]);
    mir_emit_runtime_call(out, plan->print_names[5]);
    mir_roundtrip_cleanup(out, 2);
    fputs("\tld sp,ix\n\tpop ix\n\tpop iy\n\tret\n", out);
}

static void mir_emit_room_message(
    FILE *out, const struct MirRoomResolutionSchedule *plan,
    int string_id)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_emit_runtime_call(out, plan->print_name);
    fputs("\tpop bc\n", out);
    mir_machine_emit_symbol_call(out, plan->flush_function);
}

static void mir_emit_room_compare(
    FILE *out, const struct MirRoomResolutionSchedule *plan,
    int index, int target)
{
    fprintf(out,
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
    FILE *out, const struct MirRoomResolutionSchedule *plan)
{
    int loop = new_label();
    int hazard = new_label();
    int pit = new_label();
    int bat = new_label();
    int done = new_label();

    fputs(MIR_EXACT_KERNEL_MARKER "\n\tpush iy\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tpush de\n\tpop iy\nL%d:\n",
            plan->game_stack_offset + 2, loop);
    mir_emit_room_compare(out, plan, 1, hazard);
    mir_emit_room_compare(out, plan, 2, pit);
    mir_emit_room_compare(out, plan, 3, pit);
    mir_emit_room_compare(out, plan, 4, bat);
    mir_emit_room_compare(out, plan, 5, bat);
    fprintf(out, "\tld hl,0\n\tjp L%d\nL%d:\n",
            done, hazard);
    mir_emit_room_message(out, plan, plan->string_ids[0]);
    fputs("\tpush iy\n", out);
    mir_machine_emit_symbol_call(out, plan->wake_function);
    fputs("\tpop bc\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n", done, pit);
    mir_emit_room_message(out, plan, plan->string_ids[1]);
    fprintf(out, "\tld hl,2\n\tjp L%d\nL%d:\n", done, bat);
    mir_emit_room_message(out, plan, plan->string_ids[2]);
    mir_machine_emit_symbol_call(out, plan->random_room_function);
    fprintf(out,
            "\tld (iy%+d),l\n\tld (iy%+d),h\n\tjp L%d\n"
            "L%d:\n\tpop iy\n\tret\n",
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
    FILE *out, const struct MirArrowPathSchedule *plan,
    int string_id)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_emit_runtime_call(out, plan->print_name);
    fputs("\tpop bc\n", out);
    mir_machine_emit_symbol_call(out, plan->flush_function);
}

static void mir_emit_arrow_decrement(
    FILE *out, const struct MirArrowPathSchedule *plan)
{
    fprintf(out,
            "\tld l,(iy%+d)\n\tld h,(iy%+d)\n\tdec hl\n"
            "\tld (iy%+d),l\n\tld (iy%+d),h\n",
            plan->arrows_offset, plan->arrows_offset + 1,
            plan->arrows_offset, plan->arrows_offset + 1);
}

static void mir_emit_arrow_path_schedule(
    FILE *out, const struct MirArrowPathSchedule *plan)
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

    fputs(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-6\n\tadd hl,sp\n\tld sp,hl\n",
          out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
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
    fputs("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    fprintf(out,
            "\tjp z,L%d\n\tld l,(ix-6)\n\tld h,(ix-5)\n"
            "\tjp L%d\nL%d:\n\tld hl,3\n\tpush hl\n",
            random_room, room_ready, random_room);
    mir_machine_emit_symbol_call(out, plan->random_function);
    fputs("\tpop bc\n\tadd hl,hl\n\tpush hl\n"
          "\tld l,(ix-2)\n\tld h,(ix-1)\n"
          "\tadd hl,hl\n\tld d,h\n\tld e,l\n"
          "\tadd hl,hl\n\tadd hl,de\n\tpush hl\n",
          out);
    mir_machine_emit_global_address_de(
        out, plan->cave, plan->cave_offset);
    fputs("\tpop hl\n\tadd hl,de\n\tpop de\n\tadd hl,de\n"
          "\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n",
          out);
    fprintf(out,
            "L%d:\n\tld (ix-2),l\n\tld (ix-1),h\n"
            "\tld e,(iy%+d)\n\tld d,(iy%+d)\n"
            "\tor a\n\tsbc hl,de\n\tjp nz,L%d\n",
            room_ready,
            plan->location_offset + 2,
            plan->location_offset + 3,
            not_wumpus);
    mir_emit_arrow_print(out, plan, plan->string_ids[0]);
    fprintf(out, "\tjp L%d\nL%d:\n", return_win, not_wumpus);
    fprintf(out,
            "\tld l,(ix-2)\n\tld h,(ix-1)\n"
            "\tld e,(iy%+d)\n\tld d,(iy%+d)\n"
            "\tor a\n\tsbc hl,de\n\tjp nz,L%d\n",
            plan->location_offset,
            plan->location_offset + 1, not_player);
    mir_emit_arrow_print(out, plan, plan->string_ids[1]);
    mir_emit_arrow_decrement(out, plan);
    fprintf(out, "\tjp L%d\nL%d:\n", return_loss, not_player);
    fputs("\tld l,(ix-4)\n\tld h,(ix-3)\n"
          "\tinc hl\n\tld (ix-4),l\n\tld (ix-3),h\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n", loop, missed);
    mir_emit_arrow_decrement(out, plan);
    mir_emit_arrow_print(out, plan, plan->string_ids[2]);
    fprintf(out,
            "\tld l,(iy%+d)\n\tld h,(iy%+d)\n"
            "\tld a,h\n\tor a\n\tjp m,L%d\n"
            "\tor l\n\tjp nz,L%d\n",
            plan->arrows_offset, plan->arrows_offset + 1,
            no_arrows, have_arrows);
    fprintf(out, "L%d:\n", no_arrows);
    mir_emit_arrow_print(out, plan, plan->string_ids[3]);
    fprintf(out, "\tjp L%d\nL%d:\n", return_loss, have_arrows);
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n",
            plan->game_stack_offset + 4,
            plan->game_stack_offset + 5);
    mir_machine_emit_symbol_call(out, plan->wake_function);
    fputs("\tpop bc\n", out);
    fprintf(out,
            "\tjp L%d\nL%d:\n\tld hl,1\n\tjp L%d\n"
            "L%d:\n\tld hl,2\nL%d:\n"
            "\tld sp,ix\n\tpop ix\n\tpop iy\n\tret\n",
            epilogue, return_win, epilogue,
            return_loss, epilogue);
}

int mir_try_emit_call_runners(FILE *out, int phase)
{
    if (phase == 0) {
        struct MirInlineParameterCallSchedule inline_parameter;
        struct MirNullableStringCheckSchedule nullable_string;
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
        struct MirBufferedConsoleRunner buffered_console_plan;
        struct MirDirectoryEnumerationRunner directory_plan;
        struct MirFileIoRunner file_io_plan;
        struct MirAbortFileRunner abort_plan;
        struct MirMemoryExerciseRunner memory_plan;
        struct MirAllocationLifetimeRunner allocation_plan;
        struct MirCallbackRegistrationRunner callback_plan;
        struct MirForIncrementRunner for_increment_plan;
        struct MirCommaLoopRunner comma_loop_plan;

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
        struct MirScopeBlockRunner scope_plan;
        struct MirByteEqualityRunner byte_equality_plan;
        struct MirGnarlyRunner gnarly_plan;
        struct MirNestedForRunner nested_for_plan;
        struct MirWideStringRunner wide_string_plan;
        struct MirLongIndexCallRunner long_index_plan;
        struct MirCastLogicalRunner cast_logical_plan;
        struct MirFixedCallCheckRunner plan;

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
        if (mir_match_buffered_console_runner(
                &buffered_console_plan)) {
            mir_emit_buffered_console_runner(
                out, &buffered_console_plan);
            return 1;
        }
        if (mir_match_directory_enumeration_runner(
                &directory_plan)) {
            mir_emit_directory_enumeration_runner(
                out, &directory_plan);
            return 1;
        }
        if (mir_match_file_io_runner(&file_io_plan)) {
            mir_emit_file_io_runner(out, &file_io_plan);
            return 1;
        }
        if (mir_match_abort_file_runner(&abort_plan)) {
            mir_emit_abort_file_runner(out, &abort_plan);
            return 1;
        }
        if (mir_match_memory_exercise_runner(&memory_plan)) {
            mir_emit_memory_exercise_runner(out, &memory_plan);
            return 1;
        }
        if (mir_match_allocation_lifetime_runner(
                &allocation_plan)) {
            mir_emit_allocation_lifetime_runner(
                out, &allocation_plan);
            return 1;
        }
        if (mir_match_callback_registration_runner(
                &callback_plan)) {
            mir_emit_callback_registration_runner(
                out, &callback_plan);
            return 1;
        }
        if (mir_match_for_increment_runner(
                &for_increment_plan)) {
            mir_emit_for_increment_runner(
                out, &for_increment_plan);
            return 1;
        }
        if (mir_match_comma_loop_runner(&comma_loop_plan)) {
            mir_emit_comma_loop_runner(out, &comma_loop_plan);
            return 1;
        }
        if (mir_match_scope_block_runner(&scope_plan)) {
            mir_emit_scope_block_runner(out, &scope_plan);
            return 1;
        }
        if (mir_match_byte_equality_runner(
                &byte_equality_plan)) {
            mir_emit_byte_equality_runner(
                out, &byte_equality_plan);
            return 1;
        }
        if (mir_match_gnarly_runner(&gnarly_plan)) {
            mir_emit_gnarly_runner(out, &gnarly_plan);
            return 1;
        }
        if (mir_match_nested_for_runner(&nested_for_plan)) {
            mir_emit_nested_for_runner(out, &nested_for_plan);
            return 1;
        }
        if (mir_match_wide_string_runner(&wide_string_plan)) {
            mir_emit_wide_string_runner(out, &wide_string_plan);
            return 1;
        }
        if (mir_match_long_index_call_runner(&long_index_plan)) {
            mir_emit_long_index_call_runner(
                out, &long_index_plan);
            return 1;
        }
        if (mir_match_cast_logical_runner(
                &cast_logical_plan)) {
            mir_emit_cast_logical_runner(
                out, &cast_logical_plan);
            return 1;
        }
        if (mir_match_fixed_call_check_runner(&plan)) {
            mir_emit_fixed_call_check_runner(out, &plan);
            return 1;
        }
    } else if (phase == 1) {
        struct MirFixedIndexCallRunner plan;

        if (mir_match_fixed_index_call_runner(&plan)) {
            mir_emit_fixed_index_call_runner(out, &plan);
            return 1;
        }
    }
    return -1;
}
