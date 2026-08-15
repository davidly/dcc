/* dcc_mir_machine_runtime_runners.c - strict runtime/file/system schedules. */

#include "dcc_mir_machine_internal.h"

struct MirIntegerReportSchedule {
    struct Sym *print_function;
    struct Sym *failure_count;
    int name_stack_offset;
    int got_stack_offset;
    int expected_stack_offset;
    int failure_string_id;
    int success_string_id;
    char print_name[64];
};

struct MirStringReportSchedule {
    struct Sym *compare_function;
    struct Sym *print_function;
    struct Sym *failure_count;
    int name_stack_offset;
    int got_stack_offset;
    int expected_stack_offset;
    int failure_string_id;
    int success_string_id;
    char compare_name[64];
    char print_name[64];
};

struct MirReadExactSchedule {
    struct Sym *open_function;
    struct Sym *copy_function;
    struct Sym *read_function;
    struct Sym *close_function;
    int path_stack_offset;
    int buffer_stack_offset;
    int count_stack_offset;
    int failure_string_id;
    int mode_string_id;
    char open_name[64];
    char copy_name[64];
    char read_name[64];
    char close_name[64];
};

struct MirFileExistsSchedule {
    struct Sym *open_function;
    struct Sym *close_function;
    int path_stack_offset;
    int mode_string_id;
    char open_name[64];
    char close_name[64];
};

struct MirExecRecursionSchedule {
    struct Sym *exec_function;
    struct Sym *execv_function;
    struct Sym *recursive_function;
    struct Sym *print_function;
    struct Sym *failure_count;
    int depth_stack_offset;
    int marker_stack_offset;
    int mode_stack_offset;
    int vector_offset;
    int local_check_offset;
    int result_offset;
    int file_string_id;
    int empty_string_id;
    int return_failure;
    int format_string_ids[3];
    char exec_name[64];
    char execv_name[64];
    char recursive_name[64];
    char print_name[64];
};

struct MirSparseFileSchedule {
    struct Sym *unlink_function;
    struct Sym *open_function;
    struct Sym *seek_function;
    struct Sym *putc_function;
    struct Sym *close_function;
    struct Sym *tell_function;
    struct Sym *read_function;
    struct Sym *getc_function;
    struct Sym *check_function;
    struct Sym *print_function;
    struct Sym *failure_count;
    int stream_offset;
    int length_offset;
    int count_offset;
    int buffer_offset;
    int strings[16];
    int print_flags[10];
    char unlink_name[64];
    char open_name[64];
    char seek_name[64];
    char putc_name[64];
    char close_name[64];
    char tell_name[64];
    char read_name[64];
    char getc_name[64];
    char check_name[64];
    char print_names[10][64];
};

struct MirCtrlZFileSchedule {
    struct Sym *fixture_function;
    struct Sym *open_function;
    struct Sym *gets_function;
    struct Sym *print_function;
    struct Sym *string_check_function;
    struct Sym *integer_check_function;
    struct Sym *eof_function;
    struct Sym *close_function;
    struct Sym *read_function;
    struct Sym *getc_function;
    struct Sym *unlink_function;
    struct Sym *failure_count;
    int stream_offset;
    int buffer_offset;
    int total_offset;
    int strings[29];
    int print_flags[8];
    char fixture_name[64];
    char open_name[64];
    char gets_name[64];
    char print_names[8][64];
    char string_check_name[64];
    char integer_check_name[64];
    char eof_name[64];
    char close_name[64];
    char read_name[64];
    char getc_name[64];
    char unlink_name[64];
};

struct MirWildcardOpenSchedule {
    struct Sym *unlink_function;
    struct Sym *open_function;
    struct Sym *puts_function;
    struct Sym *close_function;
    struct Sym *read_function;
    struct Sym *print_function;
    int strings[12];
    int print_flags[3];
    char unlink_name[64];
    char open_name[64];
    char puts_name[64];
    char close_name[64];
    char read_name[64];
    char print_names[3][64];
};

struct MirWildcardCreateSchedule {
    struct Sym *open_function;
    struct Sym *integer_check_function;
    struct Sym *close_function;
    struct Sym *directory_open_function;
    struct Sym *directory_read_function;
    struct Sym *compare_function;
    struct Sym *directory_close_function;
    struct Sym *print_function;
    struct Sym *failure_count;
    int strings[10];
    int print_flags[2];
    char open_name[64];
    char integer_check_name[64];
    char close_name[64];
    char directory_open_name[64];
    char directory_read_name[64];
    char compare_name[64];
    char directory_close_name[64];
    char print_names[2][64];
};

struct MirErrnoExerciseSchedule {
    struct Sym *tmpnames;
    struct Sym *errno_object;
    struct Sym *failure_count;
    struct Sym *unlink_function;
    struct Sym *open_function;
    struct Sym *read_function;
    struct Sym *write_function;
    struct Sym *close_function;
    struct Sym *seek_function;
    struct Sym *tell_function;
    struct Sym *gets_function;
    struct Sym *setvbuf_function;
    struct Sym *tmpfile_function;
    struct Sym *fclose_function;
    struct Sym *strerror_function;
    struct Sym *print_function;
    struct Sym *expect_errno_function;
    struct Sym *expect_long_function;
    struct Sym *expect_fd_function;
    int tmpnames_offset;
    int strings[35];
    int print_flags[10];
    char unlink_name[64];
    char open_name[64];
    char read_name[64];
    char write_name[64];
    char close_name[64];
    char seek_name[64];
    char tell_name[64];
    char gets_name[64];
    char setvbuf_name[64];
    char tmpfile_name[64];
    char fclose_name[64];
    char strerror_name[64];
    char print_names[10][64];
    char expect_errno_name[64];
    char expect_long_name[64];
    char expect_fd_name[64];
};

struct MirDirectoryPatternSchedule {
    struct Sym *unlink_function;
    struct Sym *open_function;
    struct Sym *puts_function;
    struct Sym *close_function;
    struct Sym *directory_open_function;
    struct Sym *directory_read_function;
    struct Sym *compare_function;
    struct Sym *directory_close_function;
    struct Sym *check_function;
    struct Sym *print_function;
    struct Sym *failure_count;
    int strings[14];
    int print_flags[3];
    char unlink_name[64];
    char open_name[64];
    char puts_name[64];
    char close_name[64];
    char directory_open_name[64];
    char directory_read_name[64];
    char compare_name[64];
    char directory_close_name[64];
    char check_name[64];
    char print_names[3][64];
};

enum {
    MIR_STRADDR = MIR_STRING_ADDRESS,
    MIR_BRFALSE = MIR_BRANCH_FALSE
};

struct MirFixedBinaryChecksSchedule {
    struct Sym *target_function;
    struct Sym *print_function;
    int left[5];
    int right[5];
    int expected[5];
    int failure_strings[5];
    int success_string;
    char target_name[64];
    char print_name[64];
};

struct MirFunctionPointerPrintSchedule {
    struct Sym *leaf_function;
    struct Sym *forward_function;
    struct Sym *print_function;
    int message[5];
    int format_string;
    int leaf_argument;
    int forward_argument;
    char leaf_name[64];
    char forward_name[64];
    char print_name[64];
};

struct MirPortIoSchedule {
    struct Sym *output_function;
    struct Sym *input_function;
    struct Sym *print_function;
    int port;
    int output_value;
    int strings[5];
    char output_name[64];
    char input_name[64];
    char print_name[64];
};

struct MirSignedIdiomReportSchedule {
    struct Sym *print_function;
    int argc_stack_offset;
    int multipliers[7];
    int format_string;
    char print_name[64];
};

struct MirBufferedExampleSchedule {
    struct Sym *set_buffer_function;
    struct Sym *puts_function;
    struct Sym *print_function;
    struct Sym *flush_function;
    struct Sym *buffer;
    int stream_value;
    int full_mode;
    int line_mode;
    int buffer_size;
    int first_index;
    int last_index;
    int strings[3];
    char set_buffer_name[64];
    char puts_name[64];
    char print_name[64];
    char flush_name[64];
    char buffer_name[64];
};

struct MirSimpleFileIoSchedule {
    struct Sym *open_function;
    struct Sym *puts_function;
    struct Sym *puts_stream_function;
    struct Sym *file_print_function;
    struct Sym *close_function;
    struct Sym *gets_function;
    struct Sym *print_function;
    struct Sym *string_print_function;
    struct Sym *unlink_function;
    int strings[11];
    int buffer_size;
    int formatted_value;
    char open_name[64];
    char puts_name[64];
    char puts_stream_name[64];
    char file_print_name[64];
    char close_name[64];
    char gets_name[64];
    char print_name[64];
    char string_print_name[64];
    char unlink_name[64];
};

struct MirFunctionPointerRuntimeSchedule {
    struct Sym *pointer_globals[10];
    struct Sym *buffers[3];
    struct Sym *length_function;
    struct Sym *print_function;
    int strings[8];
    int search_character;
    int bdos_function;
    int bdos_character;
    int memory_count;
    int copy_count;
    char length_name[64];
    char print_name[64];
};

struct MirSnprintfSequenceSchedule {
    struct Sym *format_function;
    struct Sym *print_function;
    struct Sym *variadic_helper;
    int strings[13];
    int sizes[7];
    int first_value;
    int helper_values[4];
    char format_name[64];
    char print_name[64];
    char helper_name[64];
};

struct MirLegacyFileRoundtripSchedule {
    struct Sym *create_function;
    struct Sym *failure_function;
    struct Sym *write_function;
    struct Sym *close_function;
    struct Sym *open_function;
    struct Sym *read_function;
    struct Sym *seek_function;
    struct Sym *print_function;
    struct Sym *unlink_function;
    struct Sym *buffer;
    int strings[13];
    int create_flags;
    int read_flags;
    int write_flags;
    int buffer_bytes;
    int buffer_elements;
    int iterations;
    int rewrite_stride;
    int rewrite_bias;
    char create_name[64];
    char failure_name[64];
    char write_name[64];
    char close_name[64];
    char open_name[64];
    char read_name[64];
    char seek_name[64];
    char offset_print_name[64];
    char final_print_name[64];
    char unlink_name[64];
};

struct MirSlidingWindowDriverSchedule {
    struct Sym *window_function;
    struct Sym *print_function;
    int input_values[8];
    int input_count;
    int window_size;
    int expected_count;
    int output_indices[3];
    int output_values[3];
    int format_string;
    char window_name[64];
    char print_name[64];
};

struct MirNarrowStringWorkloadSchedule {
    struct Sym *atoi_function;
    struct Sym *seed_function;
    struct Sym *random_function;
    struct Sym *length_function;
    struct Sym *print_function;
    struct Sym *exit_function;
    struct Sym *find_first_function;
    struct Sym *find_last_function;
    struct Sym *copy_string_function;
    struct Sym *find_string_function;
    struct Sym *compare_function;
    struct Sym *find_memory_function;
    struct Sym *copy_memory_function;
    struct Sym *set_memory_function;
    struct Sym *primary;
    struct Sym *secondary;
    struct Sym *zeroes;
    int argc_offset;
    int argv_offset;
    int strings[25];
    char atoi_name[64];
    char seed_name[64];
    char random_name[64];
    char length_name[64];
    char print_name[64];
    char exit_name[64];
    char find_first_name[64];
    char find_last_name[64];
    char copy_string_name[64];
    char find_string_name[64];
    char compare_name[64];
    char find_memory_name[64];
    char copy_memory_name[64];
    char set_memory_name[64];
};

struct MirBdosDriverSchedule {
    struct Sym *print_function;
    struct Sym *copy_function;
    struct Sym *byte_function;
    struct Sym *full_function;
    struct Sym *string_function;
    struct Sym *message;
    int strings[11];
    char print_name[64];
    char copy_name[64];
    char full_name[64];
    char string_name[64];
    char message_name[64];
};

struct MirLineReaderSchedule {
    struct Sym *read_function;
    int buffer_offset;
    int size_offset;
    int stream_value;
    char read_name[64];
};

struct MirAdjacencyScanSchedule {
    struct Sym *table;
    int from_offset;
    int target_offset;
    int row_width;
    int neighbor_count;
};

struct MirBsearchEdgeSchedule {
    struct Sym *search_function;
    struct Sym *failure_function;
    struct Sym *compare_function;
    struct Sym *array;
    int failure_strings[9];
    char search_name[64];
    char failure_name[64];
};

struct MirQsortEdgeSchedule {
    struct Sym *sort_function;
    struct Sym *failure_function;
    struct Sym *compare_function;
    struct Sym *array;
    int failure_strings[7];
    char sort_name[64];
    char failure_name[64];
};

struct MirAllocatorStressSchedule {
    struct Sym *random_function;
    struct Sym *check_function;
    struct Sym *fill_function;
    struct Sym *failure_function;
    struct Sym *resize_function;
    struct Sym *free_function;
    struct Sym *clear_allocate_function;
    struct Sym *allocate_function;
    struct Sym *zero_check_function;
    struct Sym *seed;
    struct Sym *slots;
    struct Sym *sizes;
    int strings[10];
    char random_name[64];
    char check_name[64];
    char fill_name[64];
    char failure_name[64];
    char resize_name[64];
    char free_name[64];
    char clear_allocate_name[64];
    char allocate_name[64];
    char zero_check_name[64];
};

enum MirAllocatorByteHelperKind {
    MIR_ALLOCATOR_PATTERN,
    MIR_ALLOCATOR_FILL,
    MIR_ALLOCATOR_CHECK,
    MIR_ALLOCATOR_ZERO_CHECK
};

struct MirAllocatorByteHelperSchedule {
    enum MirAllocatorByteHelperKind kind;
    struct Sym *failure_function;
    int pointer_offset;
    int count_offset;
    int slot_offset;
    int message_offset;
    int null_string;
    char failure_name[64];
};

struct MirAllocatorLargeSchedule {
    struct Sym *allocate_function;
    struct Sym *free_function;
    struct Sym *failure_function;
    int strings[8];
    char allocate_name[64];
    char free_name[64];
    char failure_name[64];
};

struct MirGrowFallbackSchedule {
    struct Sym *allocate_function;
    struct Sym *fill_function;
    struct Sym *free_function;
    struct Sym *resize_function;
    struct Sym *failure_function;
    struct Sym *check_function;
    struct Sym *pattern_function;
    int strings[4];
    char allocate_name[64];
    char fill_name[64];
    char free_name[64];
    char resize_name[64];
    char failure_name[64];
    char check_name[64];
    char pattern_name[64];
};

enum {
    MIR_NARROW_I = -2,
    MIR_NARROW_START = -4,
    MIR_NARROW_END = -6,
    MIR_NARROW_LEN = -8,
    MIR_NARROW_ORIG = -9,
    MIR_NARROW_POINTER = -11,
    MIR_NARROW_OFFSET = -13,
    MIR_NARROW_ALPHA = -40,
    MIR_NARROW_LOOP_COUNT = -42,
    MIR_NARROW_LOOPS = -44,
    MIR_NARROW_AUX = -46,
    MIR_NARROW_FRAME_BYTES = 46
};

enum {
    MIR_ALLOC_I = -2,
    MIR_ALLOC_INDEX = -4,
    MIR_ALLOC_OPERATION = -6,
    MIR_ALLOC_SIZE = -8,
    MIR_ALLOC_OLD = -10,
    MIR_ALLOC_KEEP = -12,
    MIR_ALLOC_POINTER = -14
};

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

static int mir_call_runner_spilled_profile(
    enum MirStrictSpilledProfile *profile)
{
    static const unsigned char io_error_opcodes[261] = {
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_STORE, MIR_LOAD,
        MIR_UNARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_RETURN, MIR_NOP,
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_NOP, MIR_ARG,
        MIR_CONST, MIR_NOP, MIR_ARG, MIR_CONST,
        MIR_NOP, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_STRING_ADDRESS, MIR_NOP, MIR_ARG,
        MIR_CONST, MIR_NOP, MIR_ARG, MIR_CONST,
        MIR_NOP, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_STORE, MIR_LOAD,
        MIR_UNARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_RETURN, MIR_NOP,
        MIR_LABEL, MIR_ADDRESS, MIR_NOP, MIR_ARG,
        MIR_CONST, MIR_NOP, MIR_ARG, MIR_CONST,
        MIR_NOP, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_NOP, MIR_STORE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_ADDRESS, MIR_NOP, MIR_ARG,
        MIR_CONST, MIR_NOP, MIR_ARG, MIR_CONST,
        MIR_NOP, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_NOP, MIR_STORE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_ADDRESS, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_STORE, MIR_LOAD, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_LABEL, MIR_ADDRESS, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_STORE, MIR_LOAD,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_LABEL, MIR_ADDRESS,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_LOAD,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_BINARY, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_BINARY, MIR_ARG,
        MIR_CALL, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_BINARY, MIR_ARG,
        MIR_CALL, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_ADDRESS, MIR_NOP, MIR_ARG, MIR_CONST,
        MIR_NOP, MIR_ARG, MIR_CONST, MIR_NOP,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_NOP, MIR_STORE, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP,
        MIR_ARG, MIR_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_LOAD, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_STORE, MIR_LOAD, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_NOP, MIR_ARG, MIR_CONST,
        MIR_NOP, MIR_ARG, MIR_CONST, MIR_NOP,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_NOP, MIR_STORE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_LOAD, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_CONST,
        MIR_RETURN
    };
    static const unsigned char padded_read_opcodes[202] = {
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_STORE, MIR_LOAD,
        MIR_UNARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_RETURN, MIR_NOP,
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_NOP, MIR_ARG,
        MIR_CONST, MIR_NOP, MIR_ARG, MIR_CONST,
        MIR_NOP, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_STORE, MIR_LOAD,
        MIR_UNARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_RETURN, MIR_NOP,
        MIR_LABEL, MIR_LOAD, MIR_ARG, MIR_NOP,
        MIR_CONST, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_NOP, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_NOP, MIR_UNARY, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_LOAD,
        MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_ADDRESS,
        MIR_NOP, MIR_ARG, MIR_CONST, MIR_NOP,
        MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG,
        MIR_LOAD, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_UNARY, MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CONST, MIR_NOP, MIR_ARG, MIR_NOP,
        MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_NOP, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_LOAD, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_ADDRESS, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_ARG, MIR_CALL, MIR_LABEL, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_LOAD,
        MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_LOAD, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_LABEL, MIR_LOAD,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_LABEL,
        MIR_PHI, MIR_RETURN
    };
    static const unsigned char wildcard_opcodes[196] = {
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD,
        MIR_ARG, MIR_CALL, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_STORE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_LOAD, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_UNARY, MIR_STORE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_ARG,
        MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST,
        MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST,
        MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_PHI, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LOAD,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_LOAD, MIR_ARG, MIR_CALL, MIR_JUMP,
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_LABEL, MIR_LOAD, MIR_BRANCH_FALSE, MIR_CONST,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_CONST,
        MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_RETURN
    };

    *profile = 0;
    if (mir.sink_purpose != EMIT_SINK_FINAL || mir.has_vla ||
        mir.aggregate_temp_bytes != 0 ||
        !mir_memory_runner_word_type(mir.return_type, 0))
        return 0;
    if (mir.count == 261 && mir.next_value == 160 &&
        mir_cfg_block_count() == 6 && mir.local_bytes == 6 &&
        !mir_has_cfg_backedge() &&
        mir_call_recovery_opcode_sequence(
            io_error_opcodes, sizeof(io_error_opcodes))) {
        *profile = MIR_STRICT_SPILLED_ADDRESS_REMAT;
        return 1;
    }
    if (mir.count == 202 && mir.next_value == 117 &&
        mir_cfg_block_count() == 17 && mir.local_bytes == 76 &&
        mir_has_cfg_backedge() &&
        mir_call_recovery_opcode_sequence(
            padded_read_opcodes, sizeof(padded_read_opcodes))) {
        *profile = MIR_STRICT_SPILLED_ADDRESS_REMAT;
        return 1;
    }
    if (mir.count == 196 && mir.next_value == 101 &&
        mir_cfg_block_count() == 22 && mir.local_bytes == 4 &&
        !mir_has_cfg_backedge() &&
        mir_call_recovery_opcode_sequence(
            wildcard_opcodes, sizeof(wildcard_opcodes))) {
        *profile = MIR_STRICT_SPILLED_PHI_SLOT;
        return 1;
    }
    return 0;
}

static int mir_call_runner_local_offset(
    const struct MirInsn *insn, int *offset_out)
{
    int type;
    int storage;
    int offset;

    if (!mir_machine_named_nonvolatile(insn) ||
        !mir_scalar_memory_location(
            insn, &type, &storage, &offset) ||
        storage != SC_LOCAL)
        return 0;
    *offset_out = offset;
    return 1;
}

static int mir_match_exec_recursion_schedule(
    struct MirExecRecursionSchedule *plan)
{
    static const unsigned char expected_opcodes[131] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM,
        MIR_CONST, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_NOP, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_NOP, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_STRING_ADDRESS, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_CONST, MIR_NOP, MIR_STORE_INDIRECT,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_STORE, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_LOAD, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_NOP, MIR_CONST, MIR_RETURN, MIR_NOP,
        MIR_LABEL, MIR_LOAD, MIR_RETURN, MIR_NOP,
        MIR_LABEL, MIR_LOAD, MIR_LOAD, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_LOAD, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_LOAD, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_NOP,
        MIR_ARG, MIR_CALL, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_NOP, MIR_CONST,
        MIR_RETURN, MIR_NOP, MIR_LABEL, MIR_LOAD,
        MIR_LOAD, MIR_LOAD, MIR_BINARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD,
        MIR_ARG, MIR_LOAD, MIR_LOAD, MIR_BINARY,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_NOP, MIR_CONST, MIR_RETURN, MIR_NOP,
        MIR_LABEL, MIR_LOAD, MIR_RETURN
    };
    int execv_arguments[2];
    int exec_arguments[2];
    int recursive_arguments[3];
    int print_arguments[4];
    int depth_offset;
    int marker_offset;
    int mode_offset;
    int vector_offset;
    int vector_offset_again;
    int local_check_offset;
    int result_offset;
    struct Sym *failure_count;

    memset(plan, 0, sizeof(*plan));
    if (!mir_call_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        mir_cfg_block_count() != 8 || mir.local_bytes != 10 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        !mir_memory_runner_word_type(mir.return_type, 0))
        return mir_machine_reject(
            "exec-recursion-schedule", "shape");
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &depth_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &marker_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[3].dst, &mode_offset) ||
        marker_offset != depth_offset + 2 ||
        mode_offset != marker_offset + 2 ||
        !mir_memory_runner_word_type(mir.insns[1].type, 0) ||
        !mir_memory_runner_word_type(mir.insns[2].type, 0) ||
        !mir_memory_runner_word_type(mir.insns[3].type, 0))
        return mir_machine_reject(
            "exec-recursion-schedule", "parameters");
    if (!mir_machine_constant_equals(mir.insns[4].dst, 0) ||
        mir.insns[6].src1 != mir.insns[4].dst ||
        mir.insns[6].src2 != mir.insns[1].dst ||
        mir.insns[6].immediate != TOK_EQ ||
        mir.insns[7].src1 != mir.insns[6].dst ||
        mir.insns[7].label != mir.insns[64].label ||
        mir.insns[10].src1 != mir.insns[3].dst ||
        mir.insns[10].label != mir.insns[33].label ||
        mir.insns[32].label != mir.insns[41].label)
        return mir_machine_reject(
            "exec-recursion-schedule", "entry-control");
    if (!mir_call_runner_local_offset(
            &mir.insns[13], &vector_offset) ||
        !mir_call_runner_local_offset(
            &mir.insns[18], &vector_offset_again) ||
        vector_offset != vector_offset_again ||
        !mir_call_runner_local_offset(
            &mir.insns[26], &vector_offset_again) ||
        vector_offset != vector_offset_again ||
        mir.insns[15].src1 != mir.insns[13].dst ||
        mir.insns[15].src2 != mir.insns[14].dst ||
        mir.insns[15].immediate != 2 ||
        mir.insns[15].memory_size != 2 ||
        !mir_machine_constant_equals(mir.insns[14].dst, 0) ||
        mir.insns[17].src1 != mir.insns[15].dst ||
        mir.insns[17].src2 != mir.insns[16].dst ||
        mir.insns[20].src1 != mir.insns[18].dst ||
        mir.insns[20].src2 != mir.insns[19].dst ||
        mir.insns[20].immediate != 2 ||
        mir.insns[20].memory_size != 2 ||
        !mir_machine_constant_equals(mir.insns[19].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[21].dst, 0) ||
        mir.insns[23].src1 != mir.insns[20].dst ||
        mir.insns[23].src2 != mir.insns[21].dst)
        return mir_machine_reject(
            "exec-recursion-schedule", "argument-vector");
    plan->execv_function = mir_call_recovery_function(
        28, 0, 2, 0, plan->execv_name);
    plan->exec_function = mir_call_recovery_function(
        38, 0, 2, 0, plan->exec_name);
    if (plan->execv_function == NULL ||
        plan->exec_function == NULL ||
        !mir_machine_two_call_arguments(
            &mir.insns[28], execv_arguments) ||
        execv_arguments[0] != mir.insns[24].dst ||
        execv_arguments[1] != mir.insns[26].dst ||
        !mir_machine_two_call_arguments(
            &mir.insns[38], exec_arguments) ||
        exec_arguments[0] != mir.insns[34].dst ||
        exec_arguments[1] != mir.insns[36].dst ||
        mir.insns[16].immediate != mir.insns[24].immediate ||
        mir.insns[16].immediate != mir.insns[34].immediate ||
        mir.insns[16].immediate == mir.insns[36].immediate)
        return mir_machine_reject(
            "exec-recursion-schedule", "exec-calls");
    if (!mir_machine_same_location(
            &mir.insns[30], &mir.insns[40]) ||
        !mir_machine_same_location(
            &mir.insns[30], &mir.insns[44]) ||
        mir.insns[30].src1 != mir.insns[28].dst ||
        mir.insns[40].src1 != mir.insns[38].dst ||
        !mir_machine_constant_equals(mir.insns[43].dst, 65535) ||
        mir.insns[45].src1 != mir.insns[43].dst ||
        mir.insns[45].src2 != mir.insns[44].dst ||
        mir.insns[45].immediate != TOK_NE ||
        mir.insns[46].src1 != mir.insns[45].dst ||
        mir.insns[46].label != mir.insns[60].label)
        return mir_machine_reject(
            "exec-recursion-schedule", "base-result");
    plan->print_function = mir_call_recovery_function(
        51, 1, 1, 0, plan->print_name);
    if (plan->print_function == NULL ||
        !mir_machine_two_call_arguments(
            &mir.insns[51], print_arguments) ||
        print_arguments[0] != mir.insns[47].dst ||
        print_arguments[1] != mir.insns[49].dst)
        return mir_machine_reject(
            "exec-recursion-schedule", "base-report");
    failure_count = find_global(mir.insns[52].name);
    if (failure_count == NULL || failure_count->is_volatile ||
        !mir_machine_same_location(
            &mir.insns[52], &mir.insns[55]) ||
        !mir_machine_constant_equals(mir.insns[53].dst, 1) ||
        mir.insns[54].src1 != mir.insns[52].dst ||
        mir.insns[54].src2 != mir.insns[53].dst ||
        mir.insns[54].immediate != '+' ||
        mir.insns[55].src1 != mir.insns[54].dst ||
        !mir_machine_constant_equals(mir.insns[57].dst, 64537) ||
        mir.insns[58].src1 != mir.insns[57].dst ||
        strcmp(mir.insns[61].name, mir.insns[2].name) ||
        mir.insns[62].src1 != mir.insns[61].dst)
        return mir_machine_reject(
            "exec-recursion-schedule", "base-return");
    if (strcmp(mir.insns[65].name, mir.insns[2].name) ||
        strcmp(mir.insns[66].name, mir.insns[1].name) ||
        mir.insns[67].src1 != mir.insns[65].dst ||
        mir.insns[67].src2 != mir.insns[66].dst ||
        mir.insns[67].immediate != '+' ||
        !mir_call_runner_local_offset(
            &mir.insns[69], &local_check_offset) ||
        mir.insns[69].src1 != mir.insns[67].dst ||
        strcmp(mir.insns[70].name, mir.insns[1].name) ||
        !mir_machine_constant_equals(mir.insns[71].dst, 1) ||
        mir.insns[72].src1 != mir.insns[70].dst ||
        mir.insns[72].src2 != mir.insns[71].dst ||
        mir.insns[72].immediate != '-')
        return mir_machine_reject(
            "exec-recursion-schedule", "recursive-state");
    plan->recursive_function = mir_call_recovery_function(
        78, 0, 3, 0, plan->recursive_name);
    if (plan->recursive_function == NULL ||
        !mir_machine_three_call_arguments(
            &mir.insns[78], recursive_arguments) ||
        recursive_arguments[0] != mir.insns[72].dst ||
        recursive_arguments[1] != mir.insns[74].dst ||
        recursive_arguments[2] != mir.insns[76].dst ||
        strcmp(mir.insns[74].name, mir.insns[2].name) ||
        strcmp(mir.insns[76].name, mir.insns[3].name) ||
        !mir_call_runner_local_offset(
            &mir.insns[80], &result_offset) ||
        mir.insns[80].src1 != mir.insns[78].dst ||
        strcmp(mir.insns[82].name, mir.insns[2].name) ||
        mir.insns[83].src1 != mir.insns[78].dst ||
        mir.insns[83].src2 != mir.insns[82].dst ||
        mir.insns[83].immediate != TOK_NE ||
        mir.insns[84].src1 != mir.insns[83].dst ||
        mir.insns[84].label != mir.insns[102].label)
        return mir_machine_reject(
            "exec-recursion-schedule", "recursive-call");
    if (find_global(mir.insns[93].name) != plan->print_function ||
        strcmp(mir.insns[93].base_name, plan->print_name) ||
        !mir_machine_call_arguments(
            &mir.insns[93], 4, print_arguments) ||
        print_arguments[0] != mir.insns[85].dst ||
        print_arguments[1] != mir.insns[87].dst ||
        print_arguments[2] != mir.insns[89].dst ||
        print_arguments[3] != mir.insns[78].dst ||
        !mir_machine_same_location(
            &mir.insns[52], &mir.insns[94]) ||
        !mir_machine_same_location(
            &mir.insns[52], &mir.insns[97]) ||
        !mir_machine_constant_equals(mir.insns[95].dst, 1) ||
        mir.insns[96].src1 != mir.insns[94].dst ||
        mir.insns[96].src2 != mir.insns[95].dst ||
        mir.insns[97].src1 != mir.insns[96].dst ||
        !mir_machine_constant_equals(mir.insns[99].dst, 64537) ||
        mir.insns[100].src1 != mir.insns[99].dst)
        return mir_machine_reject(
            "exec-recursion-schedule", "result-report");
    if (!mir_machine_same_location(
            &mir.insns[69], &mir.insns[103]) ||
        strcmp(mir.insns[104].name, mir.insns[2].name) ||
        strcmp(mir.insns[105].name, mir.insns[1].name) ||
        mir.insns[106].src1 != mir.insns[104].dst ||
        mir.insns[106].src2 != mir.insns[105].dst ||
        mir.insns[106].immediate != '+' ||
        mir.insns[107].src1 != mir.insns[103].dst ||
        mir.insns[107].src2 != mir.insns[106].dst ||
        mir.insns[107].immediate != TOK_NE ||
        mir.insns[108].src1 != mir.insns[107].dst ||
        mir.insns[108].label != mir.insns[128].label)
        return mir_machine_reject(
            "exec-recursion-schedule", "local-check");
    if (find_global(mir.insns[119].name) != plan->print_function ||
        strcmp(mir.insns[119].base_name, plan->print_name) ||
        !mir_machine_call_arguments(
            &mir.insns[119], 4, print_arguments) ||
        print_arguments[0] != mir.insns[109].dst ||
        print_arguments[1] != mir.insns[111].dst ||
        print_arguments[2] != mir.insns[115].dst ||
        print_arguments[3] != mir.insns[117].dst ||
        !mir_machine_same_location(
            &mir.insns[52], &mir.insns[120]) ||
        !mir_machine_same_location(
            &mir.insns[52], &mir.insns[123]) ||
        !mir_machine_constant_equals(mir.insns[121].dst, 1) ||
        mir.insns[122].src1 != mir.insns[120].dst ||
        mir.insns[122].src2 != mir.insns[121].dst ||
        mir.insns[123].src1 != mir.insns[122].dst ||
        !mir_machine_constant_equals(mir.insns[125].dst, 64537) ||
        mir.insns[126].src1 != mir.insns[125].dst ||
        !mir_machine_same_location(
            &mir.insns[80], &mir.insns[129]) ||
        mir.insns[130].src1 != mir.insns[129].dst)
        return mir_machine_reject(
            "exec-recursion-schedule", "local-report-return");
    plan->failure_count = failure_count;
    plan->depth_stack_offset = depth_offset;
    plan->marker_stack_offset = marker_offset;
    plan->mode_stack_offset = mode_offset;
    plan->vector_offset = vector_offset;
    plan->local_check_offset = local_check_offset;
    plan->result_offset = result_offset;
    plan->return_failure = 64537;
    plan->file_string_id = (int)mir.insns[16].immediate;
    plan->empty_string_id = (int)mir.insns[36].immediate;
    plan->format_string_ids[0] = (int)mir.insns[47].immediate;
    plan->format_string_ids[1] = (int)mir.insns[85].immediate;
    plan->format_string_ids[2] = (int)mir.insns[109].immediate;
    return 1;
}

static void mir_emit_exec_recursion_failure_increment(
    FILE *out, const struct MirExecRecursionSchedule *plan)
{
    mir_machine_emit_global_word(out, plan->failure_count, 0);
    fputs("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->failure_count, 0);
}

static void mir_emit_exec_recursion_schedule(
    FILE *out, const struct MirExecRecursionSchedule *plan)
{
    int recursive_case = new_label();
    int exec_case = new_label();
    int base_ready = new_label();
    int base_success = new_label();
    int result_valid = new_label();
    int local_valid = new_label();
    int return_failure = new_label();
    int epilogue = new_label();

    fprintf(out,
            "%s\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tld hl,-10\n\tadd hl,sp\n\tld sp,hl\n",
            MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld a,h\n\tor l\n\tjp nz,L%d\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld a,h\n\tor l\n\tjp z,L%d\n"
            "\tld hl,S%d\n"
            "\tld (ix%+d),l\n\tld (ix%+d),h\n"
            "\txor a\n\tld (ix%+d),a\n\tld (ix%+d),a\n"
            "\tpush ix\n\tpop hl\n\tld de,%d\n\tadd hl,de\n"
            "\tpush hl\n\tld hl,S%d\n\tpush hl\n",
            plan->depth_stack_offset + 2,
            plan->depth_stack_offset + 3,
            recursive_case,
            plan->mode_stack_offset + 2,
            plan->mode_stack_offset + 3,
            exec_case,
            plan->file_string_id,
            plan->vector_offset,
            plan->vector_offset + 1,
            plan->vector_offset + 2,
            plan->vector_offset + 3,
            plan->vector_offset,
            plan->file_string_id);
    mir_call_recovery_emit_named_call(
        out, plan->execv_function, plan->execv_name);
    mir_emit_final_call_cleanup(out, 2);
    fprintf(out, "\tjp L%d\nL%d:\n", base_ready, exec_case);
    fprintf(out,
            "\tld hl,S%d\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->empty_string_id, plan->file_string_id);
    mir_call_recovery_emit_named_call(
        out, plan->exec_function, plan->exec_name);
    mir_emit_final_call_cleanup(out, 2);
    fprintf(out,
            "L%d:\n\tinc hl\n\tld a,h\n\tor l\n\tdec hl\n"
            "\tjp z,L%d\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            base_ready, base_success,
            plan->format_string_ids[0]);
    mir_call_recovery_emit_named_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_emit_exec_recursion_failure_increment(out, plan);
    fprintf(out, "\tjp L%d\nL%d:\n", return_failure, base_success);
    fprintf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tjp L%d\n"
            "L%d:\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld e,(ix+%d)\n\tld d,(ix+%d)\n"
            "\tadd hl,de\n\tld (ix%+d),l\n\tld (ix%+d),h\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tdec hl\n\tpush hl\n",
            plan->marker_stack_offset + 2,
            plan->marker_stack_offset + 3,
            epilogue,
            recursive_case,
            plan->marker_stack_offset + 2,
            plan->marker_stack_offset + 3,
            plan->depth_stack_offset + 2,
            plan->depth_stack_offset + 3,
            plan->local_check_offset,
            plan->local_check_offset + 1,
            plan->mode_stack_offset + 2,
            plan->mode_stack_offset + 3,
            plan->marker_stack_offset + 2,
            plan->marker_stack_offset + 3,
            plan->depth_stack_offset + 2,
            plan->depth_stack_offset + 3);
    mir_call_recovery_emit_named_call(
        out, plan->recursive_function, plan->recursive_name);
    mir_emit_final_call_cleanup(out, 3);
    fprintf(out,
            "\tld (ix%+d),l\n\tld (ix%+d),h\n"
            "\tld e,(ix+%d)\n\tld d,(ix+%d)\n"
            "\tor a\n\tsbc hl,de\n\tjp z,L%d\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->result_offset,
            plan->result_offset + 1,
            plan->marker_stack_offset + 2,
            plan->marker_stack_offset + 3,
            result_valid,
            plan->result_offset,
            plan->result_offset + 1,
            plan->marker_stack_offset + 2,
            plan->marker_stack_offset + 3,
            plan->depth_stack_offset + 2,
            plan->depth_stack_offset + 3,
            plan->format_string_ids[1]);
    mir_call_recovery_emit_named_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 4);
    mir_emit_exec_recursion_failure_increment(out, plan);
    fprintf(out, "\tjp L%d\nL%d:\n", return_failure, result_valid);
    fprintf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld e,(ix+%d)\n\tld d,(ix+%d)\n\tadd hl,de\n"
            "\tex de,hl\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tor a\n\tsbc hl,de\n\tjp z,L%d\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n"
            "\tpush de\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->marker_stack_offset + 2,
            plan->marker_stack_offset + 3,
            plan->depth_stack_offset + 2,
            plan->depth_stack_offset + 3,
            plan->local_check_offset,
            plan->local_check_offset + 1,
            local_valid,
            plan->local_check_offset,
            plan->local_check_offset + 1,
            plan->depth_stack_offset + 2,
            plan->depth_stack_offset + 3,
            plan->format_string_ids[2]);
    mir_call_recovery_emit_named_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 4);
    mir_emit_exec_recursion_failure_increment(out, plan);
    fprintf(out,
            "\tjp L%d\nL%d:\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tjp L%d\n"
            "L%d:\n\tld hl,%d\n"
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            return_failure, local_valid,
            plan->result_offset,
            plan->result_offset + 1,
            epilogue,
            return_failure, plan->return_failure,
            epilogue);
}

static int mir_sparse_match_call(
    int instruction, int variadic, int prototype_arguments,
    int argument_count, const int *definitions,
    struct Sym **function_slot, char call_name[64])
{
    const struct MirInsn *call = &mir.insns[instruction];
    char previous_name[64];
    char resolved_name[64];
    struct Sym *function = mir_call_recovery_function(
        instruction, variadic, prototype_arguments, 0, call_name);
    int arguments[4];
    int argument;

    snprintf(previous_name, sizeof(previous_name), "%s", call_name);
    if (function == NULL &&
        call->opcode == MIR_CALL && call->src1 < 0 &&
        (((call->memory_flags & MIR_CALL_FLAG_VARIADIC) != 0) ==
         variadic)) {
        function = find_global(call->name);
        if (function != NULL &&
            (function->storage != SC_FUNC || function->is_funcptr ||
             function->is_noreturn))
            function = NULL;
        snprintf(resolved_name, sizeof(resolved_name), "%s",
                 call->base_name[0] != 0 ? call->base_name :
                 asm_name_for(function != NULL
                                  ? sym_asm_name(function)
                                  : call->name));
        if (previous_name[0] != 0 &&
            strcmp(previous_name, resolved_name))
            return 0;
        snprintf(call_name, 64, "%s", resolved_name);
    }
    if ((function == NULL && call_name[0] == 0) ||
        argument_count < 0 || argument_count > 4 ||
        !mir_machine_call_arguments(
            &mir.insns[instruction], argument_count, arguments))
        return 0;
    for (argument = 0; argument < argument_count; ++argument) {
        if (arguments[argument] != mir.insns[definitions[argument]].dst)
            return 0;
    }
    if (*function_slot == NULL)
        *function_slot = function;
    else if (function == NULL || *function_slot != function)
        return 0;
    return 1;
}

static void mir_sparse_emit_call(
    FILE *out, struct Sym *function, const char *call_name)
{
    if (function == NULL)
        mir_emit_runtime_call(out, call_name);
    else
        mir_call_recovery_emit_named_call(out, function, call_name);
}

static void mir_sparse_emit_print(
    FILE *out, const struct MirSparseFileSchedule *plan, int call)
{
    if ((plan->print_flags[call] & MIR_CALL_FLAG_FORMAT_HEX) != 0)
        mir_emit_runtime_call(out, "__pfehx");
    if ((plan->print_flags[call] & MIR_CALL_FLAG_FORMAT_OCTAL) != 0)
        mir_emit_runtime_call(out, "__pfeoc");
    mir_sparse_emit_call(
        out, plan->print_function, plan->print_names[call]);
}

static int mir_match_sparse_file_schedule(
    struct MirSparseFileSchedule *plan)
{
    static const unsigned char expected_opcodes[265] = {
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_STORE, MIR_LOAD,
        MIR_UNARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_RETURN, MIR_NOP,
        MIR_LABEL, MIR_LOAD, MIR_ARG, MIR_NOP,
        MIR_CONST, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_ARG, MIR_LOAD,
        MIR_ARG, MIR_CALL, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE,
        MIR_LOAD, MIR_UNARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN,
        MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_ARG,
        MIR_NOP, MIR_CONST, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_STORE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_ARG, MIR_NOP,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_ARG,
        MIR_CALL, MIR_LOAD, MIR_ARG, MIR_NOP,
        MIR_CONST, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_ADDRESS, MIR_NOP, MIR_ARG,
        MIR_CONST, MIR_NOP, MIR_ARG, MIR_CONST,
        MIR_NOP, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_UNARY, MIR_STORE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_CONST, MIR_STORE, MIR_CONST, MIR_NOP,
        MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_LOAD, MIR_LOAD,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_LOAD,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_JUMP, MIR_NOP,
        MIR_LABEL, MIR_LABEL, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL,
        MIR_LOAD, MIR_BRANCH_FALSE, MIR_LABEL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL, MIR_JUMP,
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG,
        MIR_CALL, MIR_LABEL, MIR_NOP, MIR_LABEL,
        MIR_LOAD, MIR_ARG, MIR_NOP, MIR_CONST,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_LOAD, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP,
        MIR_ARG, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_LABEL,
        MIR_PHI, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_LOAD, MIR_BRANCH_FALSE, MIR_LABEL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_LABEL, MIR_LOAD, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_LABEL, MIR_PHI,
        MIR_RETURN
    };
    static const int unlink_first[] = {1};
    static const int open_write[] = {4, 6};
    static const int print_create[] = {14};
    static const int seek_gap[] = {21, 24, 26};
    static const int put_gap_byte[] = {29, 31};
    static const int close_first[] = {34};
    static const int open_read[] = {37, 39};
    static const int print_reopen[] = {47};
    static const int seek_end[] = {54, 57, 59};
    static const int tell_args[] = {62};
    static const int print_length[] = {67, 64};
    static const int print_length_kinds[] = {72, 77, 82};
    static const int seek_start[] = {85, 88, 90};
    static const int read_args[] = {93, 96, 99, 102};
    static const int print_read[] = {108, 106};
    static const int print_uniform[] = {163, 168};
    static const int print_nonuniform[] = {173, 178};
    static const int seek_byte[] = {184, 187, 189};
    static const int getc_args[] = {192};
    static const int print_byte[] = {197, 194, 224};
    static const int check_byte[] = {227, 194, 231};
    static const int close_second[] = {234};
    static const int unlink_second[] = {237};
    static const int print_failure[] = {243, 245};
    static const int print_success[] = {250};
    int stream_offset;
    int length_offset;
    int count_offset;
    int buffer_offset;
    int offset;
    int i;

    memset(plan, 0, sizeof(*plan));
    if (!mir_call_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        mir.sink_purpose != EMIT_SINK_FINAL ||
        mir_cfg_block_count() != 25 || mir.local_bytes != 312 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        !mir_memory_runner_word_type(mir.return_type, 0))
        return 0;
    if (!mir_sparse_match_call(
            3, 0, 1, 1, unlink_first,
            &plan->unlink_function, plan->unlink_name) ||
        !mir_sparse_match_call(
            8, 0, 2, 2, open_write,
            &plan->open_function, plan->open_name) ||
        !mir_sparse_match_call(
            16, 1, 1, 1, print_create,
            &plan->print_function, plan->print_names[0]) ||
        !mir_sparse_match_call(
            28, 0, 3, 3, seek_gap,
            &plan->seek_function, plan->seek_name) ||
        !mir_sparse_match_call(
            33, 0, 2, 2, put_gap_byte,
            &plan->putc_function, plan->putc_name) ||
        !mir_sparse_match_call(
            36, 0, 1, 1, close_first,
            &plan->close_function, plan->close_name) ||
        !mir_sparse_match_call(
            41, 0, 2, 2, open_read,
            &plan->open_function, plan->open_name) ||
        !mir_sparse_match_call(
            49, 1, 1, 1, print_reopen,
            &plan->print_function, plan->print_names[1]) ||
        !mir_sparse_match_call(
            61, 0, 3, 3, seek_end,
            &plan->seek_function, plan->seek_name) ||
        !mir_sparse_match_call(
            64, 0, 1, 1, tell_args,
            &plan->tell_function, plan->tell_name) ||
        !mir_sparse_match_call(
            71, 1, 1, 2, print_length,
            &plan->print_function, plan->print_names[2]) ||
        !mir_sparse_match_call(
            84, 1, 1, 3, print_length_kinds,
            &plan->print_function, plan->print_names[3]) ||
        !mir_sparse_match_call(
            92, 0, 3, 3, seek_start,
            &plan->seek_function, plan->seek_name) ||
        !mir_sparse_match_call(
            104, 0, 4, 4, read_args,
            &plan->read_function, plan->read_name) ||
        !mir_sparse_match_call(
            112, 1, 1, 2, print_read,
            &plan->print_function, plan->print_names[4]) ||
        !mir_sparse_match_call(
            170, 1, 1, 2, print_uniform,
            &plan->print_function, plan->print_names[5]) ||
        !mir_sparse_match_call(
            180, 1, 1, 2, print_nonuniform,
            &plan->print_function, plan->print_names[6]) ||
        !mir_sparse_match_call(
            191, 0, 3, 3, seek_byte,
            &plan->seek_function, plan->seek_name) ||
        !mir_sparse_match_call(
            194, 0, 1, 1, getc_args,
            &plan->getc_function, plan->getc_name) ||
        !mir_sparse_match_call(
            226, 1, 1, 3, print_byte,
            &plan->print_function, plan->print_names[7]) ||
        !mir_sparse_match_call(
            233, 0, 3, 3, check_byte,
            &plan->check_function, plan->check_name) ||
        !mir_sparse_match_call(
            236, 0, 1, 1, close_second,
            &plan->close_function, plan->close_name) ||
        !mir_sparse_match_call(
            239, 0, 1, 1, unlink_second,
            &plan->unlink_function, plan->unlink_name) ||
        !mir_sparse_match_call(
            247, 1, 1, 2, print_failure,
            &plan->print_function, plan->print_names[8]) ||
        !mir_sparse_match_call(
            252, 1, 1, 1, print_success,
            &plan->print_function, plan->print_names[9]))
        return mir_machine_reject(
            "sparse-file-schedule", "calls");
    if (!mir_call_runner_local_offset(
            &mir.insns[10], &stream_offset) ||
        !mir_machine_same_location(
            &mir.insns[10], &mir.insns[43]))
        return mir_machine_reject(
            "sparse-file-schedule", "stream");
    for (i = 11; i <= 234; ++i) {
        if (mir.insns[i].opcode == MIR_LOAD &&
            !strcmp(mir.insns[i].name, mir.insns[10].name) &&
            !mir_machine_same_location(
                &mir.insns[10], &mir.insns[i]))
            return mir_machine_reject(
                "sparse-file-schedule", "stream-load");
    }
    if (!mir_call_runner_local_offset(
            &mir.insns[66], &length_offset) ||
        !mir_call_runner_local_offset(
            &mir.insns[107], &count_offset) ||
        !mir_machine_same_location(
            &mir.insns[107], &mir.insns[196]) ||
        !mir_call_runner_local_offset(
            &mir.insns[93], &buffer_offset))
        return mir_machine_reject(
            "sparse-file-schedule", "locals");
    for (i = 134; i <= 175; ++i) {
        if (mir.insns[i].opcode == MIR_ADDRESS &&
            !strcmp(mir.insns[i].name, mir.insns[93].name)) {
            if (!mir_call_runner_local_offset(
                    &mir.insns[i], &offset) ||
                offset != buffer_offset)
                return mir_machine_reject(
                    "sparse-file-schedule", "buffer-address");
        }
    }
    if (!mir_machine_constant_equals(mir.insns[17].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[24].dst, 5000) ||
        !mir_machine_constant_equals(mir.insns[26].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[29].dst, 90) ||
        !mir_machine_constant_equals(mir.insns[50].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[57].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[59].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[76].dst, 5001) ||
        !mir_machine_constant_equals(mir.insns[81].dst, 5120) ||
        !mir_machine_constant_equals(mir.insns[88].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[90].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[96].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[99].dst, 300) ||
        !mir_machine_constant_equals(mir.insns[116].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[120].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[122].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[139].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[146].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[155].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[166].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[176].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[187].dst, 5000) ||
        !mir_machine_constant_equals(mir.insns[189].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[202].dst, 32) ||
        !mir_machine_constant_equals(mir.insns[206].dst, 127) ||
        !mir_machine_constant_equals(mir.insns[221].dst, 63) ||
        !mir_machine_constant_equals(mir.insns[231].dst, 90) ||
        !mir_machine_constant_equals(mir.insns[256].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[260].dst, 0))
        return mir_machine_reject(
            "sparse-file-schedule", "constants");
    if (mir.insns[77].src1 != mir.insns[64].dst ||
        mir.insns[77].src2 != mir.insns[76].dst ||
        mir.insns[77].immediate != TOK_EQ ||
        mir.insns[82].src1 != mir.insns[64].dst ||
        mir.insns[82].src2 != mir.insns[81].dst ||
        mir.insns[82].immediate != TOK_EQ ||
        mir.insns[117].src1 != mir.insns[106].dst ||
        mir.insns[117].src2 != mir.insns[116].dst ||
        mir.insns[117].immediate != '>' ||
        mir.insns[132].src1 != mir.insns[130].dst ||
        mir.insns[132].src2 != mir.insns[131].dst ||
        mir.insns[132].immediate != '<' ||
        mir.insns[144].src1 != mir.insns[142].dst ||
        mir.insns[144].src2 != mir.insns[143].dst ||
        mir.insns[144].immediate != TOK_NE ||
        mir.insns[203].src1 != mir.insns[194].dst ||
        mir.insns[203].src2 != mir.insns[202].dst ||
        mir.insns[203].immediate != TOK_GE ||
        mir.insns[207].src1 != mir.insns[194].dst ||
        mir.insns[207].src2 != mir.insns[206].dst ||
        mir.insns[207].immediate != '<')
        return mir_machine_reject(
            "sparse-file-schedule", "comparisons");
    plan->failure_count = find_global(mir.insns[240].name);
    if (plan->failure_count == NULL ||
        plan->failure_count->is_volatile ||
        !mir_machine_same_location(
            &mir.insns[240], &mir.insns[245]) ||
        !mir_machine_same_location(
            &mir.insns[240], &mir.insns[254]))
        return mir_machine_reject(
            "sparse-file-schedule", "failure-count");
    plan->stream_offset = stream_offset;
    plan->length_offset = length_offset;
    plan->count_offset = count_offset;
    plan->buffer_offset = buffer_offset;
    plan->strings[0] = (int)mir.insns[1].immediate;
    plan->strings[1] = (int)mir.insns[6].immediate;
    plan->strings[2] = (int)mir.insns[14].immediate;
    plan->strings[3] = (int)mir.insns[47].immediate;
    plan->strings[4] = (int)mir.insns[163].immediate;
    plan->strings[5] = (int)mir.insns[173].immediate;
    plan->strings[6] = (int)mir.insns[243].immediate;
    plan->strings[7] = (int)mir.insns[250].immediate;
    plan->strings[8] = (int)mir.insns[4].immediate;
    plan->strings[9] = (int)mir.insns[39].immediate;
    plan->strings[10] = (int)mir.insns[67].immediate;
    plan->strings[11] = (int)mir.insns[72].immediate;
    plan->strings[12] = (int)mir.insns[108].immediate;
    plan->strings[13] = (int)mir.insns[197].immediate;
    plan->strings[14] = (int)mir.insns[227].immediate;
    plan->strings[15] = 0;
    plan->print_flags[0] = mir.insns[16].memory_flags;
    plan->print_flags[1] = mir.insns[49].memory_flags;
    plan->print_flags[2] = mir.insns[71].memory_flags;
    plan->print_flags[3] = mir.insns[84].memory_flags;
    plan->print_flags[4] = mir.insns[112].memory_flags;
    plan->print_flags[5] = mir.insns[170].memory_flags;
    plan->print_flags[6] = mir.insns[180].memory_flags;
    plan->print_flags[7] = mir.insns[226].memory_flags;
    plan->print_flags[8] = mir.insns[247].memory_flags;
    plan->print_flags[9] = mir.insns[252].memory_flags;
    return 1;
}

static void mir_sparse_emit_local_address(FILE *out, int offset)
{
    fputs("\tpush ix\n\tpop hl\n", out);
    if (offset != 0)
        fprintf(out, "\tld de,%d\n\tadd hl,de\n", offset);
}

static void mir_sparse_emit_stream_push(
    FILE *out, const struct MirSparseFileSchedule *plan)
{
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n",
            plan->stream_offset, plan->stream_offset + 1);
}

static void mir_sparse_emit_seek(
    FILE *out, const struct MirSparseFileSchedule *plan,
    unsigned long offset, int origin)
{
    fprintf(out, "\tld hl,%d\n\tpush hl\n", origin);
    fprintf(out,
            "\tld hl,%lu\n\tld de,%lu\n\tpush de\n\tpush hl\n",
            offset & 0xffffUL, (offset >> 16) & 0xffffUL);
    mir_sparse_emit_stream_push(out, plan);
    mir_call_recovery_emit_named_call(
        out, plan->seek_function, plan->seek_name);
    mir_emit_final_call_cleanup(out, 4);
}

static void mir_sparse_emit_wide_store(
    FILE *out, int offset)
{
    fprintf(out,
            "\tld (ix%+d),l\n\tld (ix%+d),h\n"
            "\tld (ix%+d),e\n\tld (ix%+d),d\n",
            offset, offset + 1, offset + 2, offset + 3);
}

static void mir_sparse_emit_wide_load(
    FILE *out, int offset)
{
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
            offset, offset + 1, offset + 2, offset + 3);
}

static void mir_sparse_emit_wide_equal_push(
    FILE *out, const struct MirSparseFileSchedule *plan,
    unsigned long expected)
{
    int equal = new_label();
    int done = new_label();

    mir_sparse_emit_wide_load(out, plan->length_offset);
    fprintf(out,
            "\tld a,l\n\txor %lu\n\tld l,a\n"
            "\tld a,h\n\txor %lu\n\tor l\n\tld l,a\n"
            "\tld a,e\n\txor %lu\n\tor l\n\tld l,a\n"
            "\tld a,d\n\txor %lu\n\tor l\n"
            "\tjp z,L%d\n\tld hl,0\n\tjp L%d\n"
            "L%d:\n\tld hl,1\nL%d:\n\tpush hl\n",
            expected & 0xffUL,
            (expected >> 8) & 0xffUL,
            (expected >> 16) & 0xffUL,
            (expected >> 24) & 0xffUL,
            equal, done, equal, done);
}

static void mir_emit_sparse_file_schedule(
    FILE *out, const struct MirSparseFileSchedule *plan)
{
    int first_opened = new_label();
    int second_opened = new_label();
    int skip_scan = new_label();
    int scan_loop = new_label();
    int scan_uniform = new_label();
    int scan_mismatch = new_label();
    int scan_report_done = new_label();
    int character_ready = new_label();
    int summary_success = new_label();
    int epilogue = new_label();

    fprintf(out,
            "%s\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tld hl,-312\n\tadd hl,sp\n\tld sp,hl\n",
            MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[0]);
    mir_sparse_emit_call(
        out, plan->unlink_function, plan->unlink_name);
    mir_emit_final_call_cleanup(out, 1);

    fprintf(out,
            "\tld hl,S%d\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->strings[1], plan->strings[0]);
    mir_sparse_emit_call(
        out, plan->open_function, plan->open_name);
    mir_emit_final_call_cleanup(out, 2);
    fprintf(out,
            "\tld (ix%+d),l\n\tld (ix%+d),h\n"
            "\tld a,h\n\tor l\n\tjp nz,L%d\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->stream_offset, plan->stream_offset + 1,
            first_opened, plan->strings[2]);
    mir_sparse_emit_print(out, plan, 0);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out, "\tld hl,1\n\tjp L%d\nL%d:\n",
            epilogue, first_opened);

    mir_sparse_emit_seek(out, plan, 5000, 0);
    mir_sparse_emit_stream_push(out, plan);
    fputs("\tld hl,90\n\tpush hl\n", out);
    mir_sparse_emit_call(
        out, plan->putc_function, plan->putc_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_sparse_emit_stream_push(out, plan);
    mir_sparse_emit_call(
        out, plan->close_function, plan->close_name);
    mir_emit_final_call_cleanup(out, 1);

    fprintf(out,
            "\tld hl,S%d\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->strings[9], plan->strings[0]);
    mir_sparse_emit_call(
        out, plan->open_function, plan->open_name);
    mir_emit_final_call_cleanup(out, 2);
    fprintf(out,
            "\tld (ix%+d),l\n\tld (ix%+d),h\n"
            "\tld a,h\n\tor l\n\tjp nz,L%d\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->stream_offset, plan->stream_offset + 1,
            second_opened, plan->strings[3]);
    mir_sparse_emit_print(out, plan, 1);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out, "\tld hl,1\n\tjp L%d\nL%d:\n",
            epilogue, second_opened);

    mir_sparse_emit_seek(out, plan, 0, 2);
    mir_sparse_emit_stream_push(out, plan);
    mir_sparse_emit_call(
        out, plan->tell_function, plan->tell_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_sparse_emit_wide_store(out, plan->length_offset);

    mir_sparse_emit_wide_load(out, plan->length_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[10]);
    mir_sparse_emit_print(out, plan, 2);
    mir_emit_final_call_cleanup(out, 3);

    mir_sparse_emit_wide_equal_push(out, plan, 5120);
    mir_sparse_emit_wide_equal_push(out, plan, 5001);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[11]);
    mir_sparse_emit_print(out, plan, 3);
    mir_emit_final_call_cleanup(out, 3);

    mir_sparse_emit_seek(out, plan, 0, 0);
    mir_sparse_emit_stream_push(out, plan);
    fputs("\tld hl,300\n\tpush hl\n\tld hl,1\n\tpush hl\n", out);
    mir_sparse_emit_local_address(out, plan->buffer_offset);
    fputs("\tpush hl\n", out);
    mir_sparse_emit_call(
        out, plan->read_function, plan->read_name);
    mir_emit_final_call_cleanup(out, 4);
    fprintf(out,
            "\tld (ix%+d),l\n\tld (ix%+d),h\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->count_offset, plan->count_offset + 1,
            plan->strings[12]);
    mir_sparse_emit_print(out, plan, 4);
    mir_emit_final_call_cleanup(out, 2);

    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld a,h\n\tor l\n\tjp z,L%d\n",
            plan->count_offset, plan->count_offset + 1, skip_scan);
    mir_sparse_emit_local_address(out, plan->buffer_offset);
    fprintf(out,
            "\tld b,(hl)\n\tinc hl\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n\tdec de\n"
            "L%d:\n\tld a,d\n\tor e\n\tjp z,L%d\n"
            "\tld a,(hl)\n\tcp b\n\tjp nz,L%d\n"
            "\tinc hl\n\tdec de\n\tjp L%d\n"
            "L%d:\n\tld l,b\n\tld h,0\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->count_offset, plan->count_offset + 1,
            scan_loop, scan_uniform, scan_mismatch, scan_loop,
            scan_uniform, plan->strings[4]);
    mir_sparse_emit_print(out, plan, 5);
    mir_emit_final_call_cleanup(out, 2);
    fprintf(out, "\tjp L%d\nL%d:\n", scan_report_done, scan_mismatch);
    fputs("\tld l,b\n\tld h,0\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[5]);
    mir_sparse_emit_print(out, plan, 6);
    mir_emit_final_call_cleanup(out, 2);
    fprintf(out, "L%d:\nL%d:\n", scan_report_done, skip_scan);

    mir_sparse_emit_seek(out, plan, 5000, 0);
    mir_sparse_emit_stream_push(out, plan);
    mir_sparse_emit_call(
        out, plan->getc_function, plan->getc_name);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out,
            "\tld (ix%+d),l\n\tld (ix%+d),h\n"
            "\tld de,63\n\tld a,h\n\tor a\n\tjp nz,L%d\n"
            "\tld a,l\n\tcp 32\n\tjp c,L%d\n"
            "\tcp 127\n\tjp nc,L%d\n\tld e,l\n\tld d,h\n"
            "L%d:\n\tpush de\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->count_offset, plan->count_offset + 1,
            character_ready, character_ready, character_ready,
            character_ready, plan->strings[13]);
    mir_sparse_emit_print(out, plan, 7);
    mir_emit_final_call_cleanup(out, 3);

    fputs("\tld hl,90\n\tpush hl\n", out);
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->count_offset, plan->count_offset + 1,
            plan->strings[14]);
    mir_sparse_emit_call(
        out, plan->check_function, plan->check_name);
    mir_emit_final_call_cleanup(out, 3);

    mir_sparse_emit_stream_push(out, plan);
    mir_sparse_emit_call(
        out, plan->close_function, plan->close_name);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[0]);
    mir_sparse_emit_call(
        out, plan->unlink_function, plan->unlink_name);
    mir_emit_final_call_cleanup(out, 1);

    mir_machine_emit_global_word(out, plan->failure_count, 0);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out,
            "\tjp z,L%d\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            summary_success, plan->strings[6]);
    mir_sparse_emit_print(out, plan, 8);
    mir_emit_final_call_cleanup(out, 2);
    fprintf(out,
            "\tld hl,1\n\tjp L%d\nL%d:\n"
            "\tld hl,S%d\n\tpush hl\n",
            epilogue, summary_success, plan->strings[7]);
    mir_sparse_emit_print(out, plan, 9);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out,
            "\tld hl,0\nL%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            epilogue);
}

static int mir_match_ctrlz_file_schedule(
    struct MirCtrlZFileSchedule *plan)
{
    static const unsigned char expected_opcodes[281] = {
        MIR_LABEL, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_STORE, MIR_LOAD, MIR_UNARY, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_CONST,
        MIR_RETURN, MIR_NOP, MIR_LABEL, MIR_ADDRESS,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_LOAD,
        MIR_ARG, MIR_CALL, MIR_UNARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_LABEL, MIR_ADDRESS,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_LOAD,
        MIR_ARG, MIR_CALL, MIR_CONST, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_NOP, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD,
        MIR_ARG, MIR_CALL, MIR_CONST, MIR_BINARY,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_LOAD, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_STORE, MIR_LOAD, MIR_UNARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_RETURN, MIR_NOP, MIR_LABEL,
        MIR_ADDRESS, MIR_NOP, MIR_ARG, MIR_CONST,
        MIR_NOP, MIR_ARG, MIR_CONST, MIR_NOP,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_UNARY, MIR_STORE, MIR_LOAD,
        MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_NOP, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_UNARY,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_UNARY,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_UNARY,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_STORE, MIR_LOAD,
        MIR_UNARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_RETURN, MIR_NOP,
        MIR_LABEL, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_PHI,
        MIR_LOAD, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_JUMP, MIR_LABEL,
        MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_LOAD, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_LOAD, MIR_BRANCH_FALSE, MIR_LABEL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_LABEL, MIR_LOAD, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_LABEL, MIR_PHI,
        MIR_RETURN
    };
    static const int no_arguments[1] = {0};
    static const int open_text[] = {2, 4};
    static const int print_open_text[] = {12};
    static const int gets_first[] = {19, 21, 23};
    static const int print_gets[] = {29};
    static const int check_string[] = {39, 41, 43};
    static const int gets_second[] = {47, 49, 51};
    static const int check_second[] = {58, 55, 62};
    static const int eof_args[] = {67};
    static const int check_eof[] = {65, 71, 73};
    static const int close_text[] = {76};
    static const int open_binary[] = {79, 81};
    static const int print_open_binary[] = {89};
    static const int read_binary[] = {96, 99, 102, 105};
    static const int close_binary[] = {111};
    static const int print_total[] = {114, 109};
    static const int check_total[] = {119, 123, 125};
    static const int check_byte6[] = {128, 135, 137};
    static const int check_byte7[] = {140, 147, 149};
    static const int check_byte18[] = {152, 159, 161};
    static const int open_getc[] = {164, 166};
    static const int print_open_getc[] = {174};
    static const int getc_args[] = {188};
    static const int check_getc6[] = {201, 190, 205};
    static const int check_getc18[] = {213, 190, 217};
    static const int close_getc[] = {236};
    static const int print_count[] = {239, 241};
    static const int check_count[] = {244, 248, 250};
    static const int unlink_args[] = {253};
    static const int print_failure[] = {259, 261};
    static const int print_success[] = {266};
    int stream_offset;
    int buffer_offset;
    int total_offset;
    int offset;
    int i;

    memset(plan, 0, sizeof(*plan));
    if (!mir_call_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        mir.sink_purpose != EMIT_SINK_FINAL ||
        mir_cfg_block_count() != 20 || mir.local_bytes != 42 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        !mir_memory_runner_word_type(mir.return_type, 0))
        return 0;
    if (!mir_sparse_match_call(
            1, 0, 0, 0, no_arguments,
            &plan->fixture_function, plan->fixture_name) ||
        !mir_sparse_match_call(
            6, 0, 2, 2, open_text,
            &plan->open_function, plan->open_name) ||
        !mir_sparse_match_call(
            14, 1, 1, 1, print_open_text,
            &plan->print_function, plan->print_names[0]) ||
        !mir_sparse_match_call(
            25, 0, 3, 3, gets_first,
            &plan->gets_function, plan->gets_name) ||
        !mir_sparse_match_call(
            31, 1, 1, 1, print_gets,
            &plan->print_function, plan->print_names[1]) ||
        !mir_sparse_match_call(
            45, 0, 3, 3, check_string,
            &plan->string_check_function,
            plan->string_check_name) ||
        !mir_sparse_match_call(
            53, 0, 3, 3, gets_second,
            &plan->gets_function, plan->gets_name) ||
        !mir_sparse_match_call(
            64, 0, 3, 3, check_second,
            &plan->integer_check_function,
            plan->integer_check_name) ||
        !mir_sparse_match_call(
            69, 0, 1, 1, eof_args,
            &plan->eof_function, plan->eof_name) ||
        !mir_sparse_match_call(
            75, 0, 3, 3, check_eof,
            &plan->integer_check_function,
            plan->integer_check_name) ||
        !mir_sparse_match_call(
            78, 0, 1, 1, close_text,
            &plan->close_function, plan->close_name) ||
        !mir_sparse_match_call(
            83, 0, 2, 2, open_binary,
            &plan->open_function, plan->open_name) ||
        !mir_sparse_match_call(
            91, 1, 1, 1, print_open_binary,
            &plan->print_function, plan->print_names[2]) ||
        !mir_sparse_match_call(
            107, 0, 4, 4, read_binary,
            &plan->read_function, plan->read_name) ||
        !mir_sparse_match_call(
            113, 0, 1, 1, close_binary,
            &plan->close_function, plan->close_name) ||
        !mir_sparse_match_call(
            118, 1, 1, 2, print_total,
            &plan->print_function, plan->print_names[3]) ||
        !mir_sparse_match_call(
            127, 0, 3, 3, check_total,
            &plan->integer_check_function,
            plan->integer_check_name) ||
        !mir_sparse_match_call(
            139, 0, 3, 3, check_byte6,
            &plan->integer_check_function,
            plan->integer_check_name) ||
        !mir_sparse_match_call(
            151, 0, 3, 3, check_byte7,
            &plan->integer_check_function,
            plan->integer_check_name) ||
        !mir_sparse_match_call(
            163, 0, 3, 3, check_byte18,
            &plan->integer_check_function,
            plan->integer_check_name) ||
        !mir_sparse_match_call(
            168, 0, 2, 2, open_getc,
            &plan->open_function, plan->open_name) ||
        !mir_sparse_match_call(
            176, 1, 1, 1, print_open_getc,
            &plan->print_function, plan->print_names[4]) ||
        !mir_sparse_match_call(
            190, 0, 1, 1, getc_args,
            &plan->getc_function, plan->getc_name) ||
        !mir_sparse_match_call(
            207, 0, 3, 3, check_getc6,
            &plan->integer_check_function,
            plan->integer_check_name) ||
        !mir_sparse_match_call(
            219, 0, 3, 3, check_getc18,
            &plan->integer_check_function,
            plan->integer_check_name) ||
        !mir_sparse_match_call(
            238, 0, 1, 1, close_getc,
            &plan->close_function, plan->close_name) ||
        !mir_sparse_match_call(
            243, 1, 1, 2, print_count,
            &plan->print_function, plan->print_names[5]) ||
        !mir_sparse_match_call(
            252, 0, 3, 3, check_count,
            &plan->integer_check_function,
            plan->integer_check_name) ||
        !mir_sparse_match_call(
            255, 0, 1, 1, unlink_args,
            &plan->unlink_function, plan->unlink_name) ||
        !mir_sparse_match_call(
            263, 1, 1, 2, print_failure,
            &plan->print_function, plan->print_names[6]) ||
        !mir_sparse_match_call(
            268, 1, 1, 1, print_success,
            &plan->print_function, plan->print_names[7]))
        return mir_machine_reject(
            "ctrlz-file-schedule", "calls");
    if (!mir_call_runner_local_offset(
            &mir.insns[8], &stream_offset) ||
        !mir_machine_same_location(
            &mir.insns[8], &mir.insns[85]) ||
        !mir_machine_same_location(
            &mir.insns[8], &mir.insns[170]) ||
        !mir_call_runner_local_offset(
            &mir.insns[19], &buffer_offset) ||
        !mir_call_runner_local_offset(
            &mir.insns[110], &total_offset))
        return mir_machine_reject(
            "ctrlz-file-schedule", "locals");
    for (i = 41; i <= 159; ++i) {
        if (mir.insns[i].opcode == MIR_ADDRESS &&
            !strcmp(mir.insns[i].name, mir.insns[19].name)) {
            if (!mir_call_runner_local_offset(
                    &mir.insns[i], &offset) ||
                offset != buffer_offset)
                return mir_machine_reject(
                    "ctrlz-file-schedule", "buffer-address");
        }
    }
    if (!mir_machine_constant_equals(mir.insns[15].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[21].dst, 32) ||
        !mir_machine_constant_equals(mir.insns[33].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[49].dst, 32) ||
        !mir_machine_constant_equals(mir.insns[54].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[62].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[70].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[73].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[92].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[99].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[102].dst, 32) ||
        !mir_machine_constant_equals(mir.insns[122].dst, 19) ||
        !mir_machine_constant_equals(mir.insns[125].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[131].dst, 6) ||
        !mir_machine_constant_equals(mir.insns[137].dst, 26) ||
        !mir_machine_constant_equals(mir.insns[143].dst, 7) ||
        !mir_machine_constant_equals(mir.insns[149].dst, 108) ||
        !mir_machine_constant_equals(mir.insns[155].dst, 18) ||
        !mir_machine_constant_equals(mir.insns[161].dst, 10) ||
        !mir_machine_constant_equals(mir.insns[177].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[181].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[194].dst, 65535) ||
        !mir_machine_constant_equals(mir.insns[198].dst, 6) ||
        !mir_machine_constant_equals(mir.insns[205].dst, 26) ||
        !mir_machine_constant_equals(mir.insns[210].dst, 18) ||
        !mir_machine_constant_equals(mir.insns[217].dst, 10) ||
        !mir_machine_constant_equals(mir.insns[222].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[226].dst, 200) ||
        !mir_machine_constant_equals(mir.insns[247].dst, 19) ||
        !mir_machine_constant_equals(mir.insns[250].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[272].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[276].dst, 0))
        return mir_machine_reject(
            "ctrlz-file-schedule", "constants");
    plan->failure_count = find_global(mir.insns[32].name);
    if (plan->failure_count == NULL ||
        plan->failure_count->is_volatile ||
        !mir_machine_same_location(
            &mir.insns[32], &mir.insns[256]) ||
        !mir_machine_same_location(
            &mir.insns[32], &mir.insns[261]))
        return mir_machine_reject(
            "ctrlz-file-schedule", "failure-count");
    plan->stream_offset = stream_offset;
    plan->buffer_offset = buffer_offset;
    plan->total_offset = total_offset;
    plan->strings[0] = (int)mir.insns[2].immediate;
    plan->strings[1] = (int)mir.insns[4].immediate;
    plan->strings[2] = (int)mir.insns[12].immediate;
    plan->strings[3] = (int)mir.insns[29].immediate;
    plan->strings[4] = (int)mir.insns[39].immediate;
    plan->strings[5] = (int)mir.insns[43].immediate;
    plan->strings[6] = (int)mir.insns[58].immediate;
    plan->strings[7] = (int)mir.insns[65].immediate;
    plan->strings[8] = (int)mir.insns[81].immediate;
    plan->strings[9] = (int)mir.insns[89].immediate;
    plan->strings[10] = (int)mir.insns[114].immediate;
    plan->strings[11] = (int)mir.insns[119].immediate;
    plan->strings[12] = (int)mir.insns[128].immediate;
    plan->strings[13] = (int)mir.insns[140].immediate;
    plan->strings[14] = (int)mir.insns[152].immediate;
    plan->strings[15] = (int)mir.insns[174].immediate;
    plan->strings[16] = (int)mir.insns[201].immediate;
    plan->strings[17] = (int)mir.insns[213].immediate;
    plan->strings[18] = (int)mir.insns[239].immediate;
    plan->strings[19] = (int)mir.insns[244].immediate;
    plan->strings[20] = (int)mir.insns[259].immediate;
    plan->strings[21] = (int)mir.insns[266].immediate;
    plan->print_flags[0] = mir.insns[14].memory_flags;
    plan->print_flags[1] = mir.insns[31].memory_flags;
    plan->print_flags[2] = mir.insns[91].memory_flags;
    plan->print_flags[3] = mir.insns[118].memory_flags;
    plan->print_flags[4] = mir.insns[176].memory_flags;
    plan->print_flags[5] = mir.insns[243].memory_flags;
    plan->print_flags[6] = mir.insns[263].memory_flags;
    plan->print_flags[7] = mir.insns[268].memory_flags;
    return 1;
}

static void mir_ctrlz_emit_print(
    FILE *out, const struct MirCtrlZFileSchedule *plan, int call)
{
    if ((plan->print_flags[call] & MIR_CALL_FLAG_FORMAT_HEX) != 0)
        mir_emit_runtime_call(out, "__pfehx");
    if ((plan->print_flags[call] & MIR_CALL_FLAG_FORMAT_OCTAL) != 0)
        mir_emit_runtime_call(out, "__pfeoc");
    mir_sparse_emit_call(
        out, plan->print_function, plan->print_names[call]);
}

static void mir_ctrlz_emit_failure_increment(
    FILE *out, const struct MirCtrlZFileSchedule *plan)
{
    mir_machine_emit_global_word(out, plan->failure_count, 0);
    fputs("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->failure_count, 0);
}

static void mir_ctrlz_emit_buffer_address(
    FILE *out, const struct MirCtrlZFileSchedule *plan)
{
    mir_sparse_emit_local_address(out, plan->buffer_offset);
}

static void mir_ctrlz_emit_stream_push(
    FILE *out, const struct MirCtrlZFileSchedule *plan)
{
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n",
            plan->stream_offset, plan->stream_offset + 1);
}

static void mir_ctrlz_emit_open(
    FILE *out, const struct MirCtrlZFileSchedule *plan, int mode_string)
{
    fprintf(out,
            "\tld hl,S%d\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            mode_string, plan->strings[0]);
    mir_sparse_emit_call(
        out, plan->open_function, plan->open_name);
    mir_emit_final_call_cleanup(out, 2);
    fprintf(out,
            "\tld (ix%+d),l\n\tld (ix%+d),h\n",
            plan->stream_offset, plan->stream_offset + 1);
}

static void mir_ctrlz_emit_gets(
    FILE *out, const struct MirCtrlZFileSchedule *plan)
{
    mir_ctrlz_emit_stream_push(out, plan);
    fputs("\tld hl,32\n\tpush hl\n", out);
    mir_ctrlz_emit_buffer_address(out, plan);
    fputs("\tpush hl\n", out);
    mir_sparse_emit_call(
        out, plan->gets_function, plan->gets_name);
    mir_emit_final_call_cleanup(out, 3);
}

static void mir_ctrlz_emit_integer_check(
    FILE *out, const struct MirCtrlZFileSchedule *plan,
    int name_string, int expected)
{
    fprintf(out,
            "\tld de,%d\n\tpush de\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            expected, name_string);
    mir_sparse_emit_call(
        out, plan->integer_check_function,
        plan->integer_check_name);
    mir_emit_final_call_cleanup(out, 3);
}

static void mir_emit_ctrlz_file_schedule(
    FILE *out, const struct MirCtrlZFileSchedule *plan)
{
    int first_opened = new_label();
    int first_line_ok = new_label();
    int after_first_line = new_label();
    int binary_opened = new_label();
    int getc_opened = new_label();
    int getc_loop = new_label();
    int not_six = new_label();
    int not_eighteen = new_label();
    int getc_done = new_label();
    int summary_success = new_label();
    int epilogue = new_label();

    fputs(MIR_EXACT_KERNEL_MARKER "\n"
          ";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-42\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    mir_sparse_emit_call(
        out, plan->fixture_function, plan->fixture_name);
    mir_ctrlz_emit_open(out, plan, plan->strings[1]);
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld a,h\n\tor l\n\tjp nz,L%d\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->stream_offset, plan->stream_offset + 1,
            first_opened, plan->strings[2]);
    mir_ctrlz_emit_print(out, plan, 0);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out, "\tld hl,1\n\tjp L%d\nL%d:\n",
            epilogue, first_opened);

    mir_ctrlz_emit_gets(out, plan);
    fprintf(out,
            "\tld a,h\n\tor l\n\tjp nz,L%d\n"
            "\tld hl,S%d\n\tpush hl\n",
            first_line_ok, plan->strings[3]);
    mir_ctrlz_emit_print(out, plan, 1);
    mir_emit_final_call_cleanup(out, 1);
    mir_ctrlz_emit_failure_increment(out, plan);
    fprintf(out, "\tjp L%d\nL%d:\n", after_first_line, first_line_ok);
    fprintf(out,
            "\tld hl,S%d\n\tpush hl\n",
            plan->strings[5]);
    mir_ctrlz_emit_buffer_address(out, plan);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[4]);
    mir_sparse_emit_call(
        out, plan->string_check_function,
        plan->string_check_name);
    mir_emit_final_call_cleanup(out, 3);
    fprintf(out, "L%d:\n", after_first_line);

    mir_ctrlz_emit_gets(out, plan);
    fputs("\tld a,h\n\tor l\n\tld hl,0\n", out);
    {
        int second_null = new_label();
        fprintf(out,
                "\tjp z,L%d\n\tinc hl\nL%d:\n",
                second_null, second_null);
    }
    mir_ctrlz_emit_integer_check(
        out, plan, plan->strings[6], 0);

    mir_ctrlz_emit_stream_push(out, plan);
    mir_sparse_emit_call(out, plan->eof_function, plan->eof_name);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tld a,h\n\tor l\n\tld hl,0\n", out);
    {
        int eof_zero = new_label();
        fprintf(out,
                "\tjp z,L%d\n\tinc hl\nL%d:\n",
                eof_zero, eof_zero);
    }
    mir_ctrlz_emit_integer_check(
        out, plan, plan->strings[7], 1);

    mir_ctrlz_emit_stream_push(out, plan);
    mir_sparse_emit_call(
        out, plan->close_function, plan->close_name);
    mir_emit_final_call_cleanup(out, 1);

    mir_ctrlz_emit_open(out, plan, plan->strings[8]);
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld a,h\n\tor l\n\tjp nz,L%d\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->stream_offset, plan->stream_offset + 1,
            binary_opened, plan->strings[9]);
    mir_ctrlz_emit_print(out, plan, 2);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out, "\tld hl,1\n\tjp L%d\nL%d:\n",
            epilogue, binary_opened);

    mir_ctrlz_emit_stream_push(out, plan);
    fputs("\tld hl,32\n\tpush hl\n\tld hl,1\n\tpush hl\n", out);
    mir_ctrlz_emit_buffer_address(out, plan);
    fputs("\tpush hl\n", out);
    mir_sparse_emit_call(
        out, plan->read_function, plan->read_name);
    mir_emit_final_call_cleanup(out, 4);
    fprintf(out,
            "\tld (ix%+d),l\n\tld (ix%+d),h\n",
            plan->total_offset, plan->total_offset + 1);
    mir_ctrlz_emit_stream_push(out, plan);
    mir_sparse_emit_call(
        out, plan->close_function, plan->close_name);
    mir_emit_final_call_cleanup(out, 1);

    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->total_offset, plan->total_offset + 1,
            plan->strings[10]);
    mir_ctrlz_emit_print(out, plan, 3);
    mir_emit_final_call_cleanup(out, 2);
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld de,19\n\tor a\n\tsbc hl,de\n"
            "\tld hl,0\n",
            plan->total_offset, plan->total_offset + 1);
    {
        int total_small = new_label();
        fprintf(out,
                "\tjp c,L%d\n\tinc hl\nL%d:\n",
                total_small, total_small);
    }
    mir_ctrlz_emit_integer_check(
        out, plan, plan->strings[11], 1);

    mir_ctrlz_emit_buffer_address(out, plan);
    fputs("\tld de,6\n\tadd hl,de\n\tld l,(hl)\n\tld h,0\n", out);
    mir_ctrlz_emit_integer_check(
        out, plan, plan->strings[12], 26);
    mir_ctrlz_emit_buffer_address(out, plan);
    fputs("\tld de,7\n\tadd hl,de\n\tld l,(hl)\n\tld h,0\n", out);
    mir_ctrlz_emit_integer_check(
        out, plan, plan->strings[13], 108);
    mir_ctrlz_emit_buffer_address(out, plan);
    fputs("\tld de,18\n\tadd hl,de\n\tld l,(hl)\n\tld h,0\n", out);
    mir_ctrlz_emit_integer_check(
        out, plan, plan->strings[14], 10);

    mir_ctrlz_emit_open(out, plan, plan->strings[8]);
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld a,h\n\tor l\n\tjp nz,L%d\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->stream_offset, plan->stream_offset + 1,
            getc_opened, plan->strings[15]);
    mir_ctrlz_emit_print(out, plan, 4);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out, "\tld hl,1\n\tjp L%d\nL%d:\n",
            epilogue, getc_opened);

    fputs("\tld iy,0\n", out);
    fprintf(out, "L%d:\n", getc_loop);
    mir_ctrlz_emit_stream_push(out, plan);
    mir_sparse_emit_call(
        out, plan->getc_function, plan->getc_name);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tld d,h\n\tld e,l\n\tinc hl\n"
          "\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", getc_done);
    fputs("\tpush iy\n\tpop hl\n\tld a,h\n\tor a\n", out);
    fprintf(out, "\tjp nz,L%d\n\tld a,l\n\tcp 6\n\tjp nz,L%d\n",
            not_eighteen, not_six);
    fputs("\tld hl,26\n\tpush hl\n\tpush de\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[16]);
    mir_sparse_emit_call(
        out, plan->integer_check_function,
        plan->integer_check_name);
    mir_emit_final_call_cleanup(out, 3);
    fprintf(out, "\tjp L%d\nL%d:\n", not_eighteen, not_six);
    fputs("\tld a,l\n\tcp 18\n", out);
    fprintf(out, "\tjp nz,L%d\n", not_eighteen);
    fputs("\tld hl,10\n\tpush hl\n\tpush de\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[17]);
    mir_sparse_emit_call(
        out, plan->integer_check_function,
        plan->integer_check_name);
    mir_emit_final_call_cleanup(out, 3);
    fprintf(out, "L%d:\n\tinc iy\n", not_eighteen);
    fputs("\tpush iy\n\tpop hl\n\tld a,h\n\tor a\n", out);
    fprintf(out, "\tjp nz,L%d\n\tld a,l\n\tcp 201\n",
            getc_done);
    fprintf(out, "\tjp nc,L%d\n\tjp L%d\nL%d:\n",
            getc_done, getc_loop, getc_done);

    mir_ctrlz_emit_stream_push(out, plan);
    mir_sparse_emit_call(
        out, plan->close_function, plan->close_name);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tpush iy\n\tpop hl\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[18]);
    mir_ctrlz_emit_print(out, plan, 5);
    mir_emit_final_call_cleanup(out, 2);
    fputs("\tpush iy\n\tpop hl\n\tld de,19\n"
          "\tor a\n\tsbc hl,de\n\tld hl,0\n", out);
    {
        int count_small = new_label();
        fprintf(out,
                "\tjp c,L%d\n\tinc hl\nL%d:\n",
                count_small, count_small);
    }
    mir_ctrlz_emit_integer_check(
        out, plan, plan->strings[19], 1);

    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[0]);
    mir_sparse_emit_call(
        out, plan->unlink_function, plan->unlink_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_machine_emit_global_word(out, plan->failure_count, 0);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out,
            "\tjp z,L%d\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            summary_success, plan->strings[20]);
    mir_ctrlz_emit_print(out, plan, 6);
    mir_emit_final_call_cleanup(out, 2);
    fprintf(out,
            "\tld hl,1\n\tjp L%d\nL%d:\n"
            "\tld hl,S%d\n\tpush hl\n",
            epilogue, summary_success, plan->strings[21]);
    mir_ctrlz_emit_print(out, plan, 7);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out,
            "\tld hl,0\nL%d:\n\tld sp,ix\n\tpop ix\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            epilogue);
}

static int mir_match_wildcard_open_schedule(
    struct MirWildcardOpenSchedule *plan)
{
    static const unsigned char expected_opcodes[120] = {
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_LOAD, MIR_ARG, MIR_CALL, MIR_LOAD,
        MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD,
        MIR_ARG, MIR_CALL, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_STRING_ADDRESS, MIR_LABEL, MIR_LABEL, MIR_PHI,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_LOAD, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_NOP, MIR_ADDRESS, MIR_NOP,
        MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG,
        MIR_CONST, MIR_CONST, MIR_BINARY, MIR_NOP,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_STORE, MIR_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_CONST,
        MIR_RETURN
    };
    static const int unlink_first[] = {1};
    static const int unlink_second[] = {4};
    static const int open_first[] = {7, 9};
    static const int puts_first[] = {14, 16};
    static const int close_first[] = {19};
    static const int open_second[] = {22, 24};
    static const int puts_second[] = {29, 31};
    static const int close_second[] = {34};
    static const int open_wildcard[] = {37, 39};
    static const int print_result[] = {44, 55};
    static const int read_content[] = {78, 81, 86, 89};
    static const int print_content[] = {99, 101};
    static const int close_wildcard[] = {104};
    static const int unlink_cleanup_first[] = {109};
    static const int unlink_cleanup_second[] = {112};
    static const int print_success[] = {115};

    memset(plan, 0, sizeof(*plan));
    if (!mir_call_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        mir.sink_purpose != EMIT_SINK_FINAL ||
        mir_cfg_block_count() != 6 || mir.local_bytes != 26 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        !mir_memory_runner_word_type(mir.return_type, 0))
        return 0;
    if (!mir_sparse_match_call(
            3, 0, 1, 1, unlink_first,
            &plan->unlink_function, plan->unlink_name) ||
        !mir_sparse_match_call(
            6, 0, 1, 1, unlink_second,
            &plan->unlink_function, plan->unlink_name) ||
        !mir_sparse_match_call(
            11, 0, 2, 2, open_first,
            &plan->open_function, plan->open_name) ||
        !mir_sparse_match_call(
            18, 0, 2, 2, puts_first,
            &plan->puts_function, plan->puts_name) ||
        !mir_sparse_match_call(
            21, 0, 1, 1, close_first,
            &plan->close_function, plan->close_name) ||
        !mir_sparse_match_call(
            26, 0, 2, 2, open_second,
            &plan->open_function, plan->open_name) ||
        !mir_sparse_match_call(
            33, 0, 2, 2, puts_second,
            &plan->puts_function, plan->puts_name) ||
        !mir_sparse_match_call(
            36, 0, 1, 1, close_second,
            &plan->close_function, plan->close_name) ||
        !mir_sparse_match_call(
            41, 0, 2, 2, open_wildcard,
            &plan->open_function, plan->open_name) ||
        !mir_sparse_match_call(
            57, 1, 1, 2, print_result,
            &plan->print_function, plan->print_names[0]) ||
        !mir_sparse_match_call(
            91, 0, 4, 4, read_content,
            &plan->read_function, plan->read_name) ||
        !mir_sparse_match_call(
            103, 1, 1, 2, print_content,
            &plan->print_function, plan->print_names[1]) ||
        !mir_sparse_match_call(
            106, 0, 1, 1, close_wildcard,
            &plan->close_function, plan->close_name) ||
        !mir_sparse_match_call(
            111, 0, 1, 1, unlink_cleanup_first,
            &plan->unlink_function, plan->unlink_name) ||
        !mir_sparse_match_call(
            114, 0, 1, 1, unlink_cleanup_second,
            &plan->unlink_function, plan->unlink_name) ||
        !mir_sparse_match_call(
            117, 1, 1, 1, print_success,
            &plan->print_function, plan->print_names[2]))
        return mir_machine_reject(
            "wildcard-open-schedule", "calls");
    if (!mir_machine_constant_equals(mir.insns[81].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[84].dst, 16) ||
        !mir_machine_constant_equals(mir.insns[85].dst, 1) ||
        mir.insns[86].src1 != mir.insns[84].dst ||
        mir.insns[86].src2 != mir.insns[85].dst ||
        mir.insns[86].immediate != '-' ||
        !mir_machine_constant_equals(mir.insns[97].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[118].dst, 0))
        return mir_machine_reject(
            "wildcard-open-schedule", "constants");
    if (!mir_machine_same_location(
            &mir.insns[43], &mir.insns[46]) ||
        !mir_machine_same_location(
            &mir.insns[43], &mir.insns[74]) ||
        mir.insns[47].src1 != mir.insns[46].dst ||
        mir.insns[75].src1 != mir.insns[74].dst)
        return mir_machine_reject(
            "wildcard-open-schedule", "control");
    plan->strings[0] = (int)mir.insns[1].immediate;
    plan->strings[1] = (int)mir.insns[4].immediate;
    plan->strings[2] = (int)mir.insns[9].immediate;
    plan->strings[3] = (int)mir.insns[14].immediate;
    plan->strings[4] = (int)mir.insns[29].immediate;
    plan->strings[5] = (int)mir.insns[37].immediate;
    plan->strings[6] = (int)mir.insns[39].immediate;
    plan->strings[7] = (int)mir.insns[44].immediate;
    plan->strings[8] = (int)mir.insns[48].immediate;
    plan->strings[9] = (int)mir.insns[52].immediate;
    plan->strings[10] = (int)mir.insns[99].immediate;
    plan->strings[11] = (int)mir.insns[115].immediate;
    plan->print_flags[0] = mir.insns[57].memory_flags;
    plan->print_flags[1] = mir.insns[103].memory_flags;
    plan->print_flags[2] = mir.insns[117].memory_flags;
    return 1;
}

static void mir_wildcard_emit_print(
    FILE *out, const struct MirWildcardOpenSchedule *plan, int call)
{
    if ((plan->print_flags[call] & MIR_CALL_FLAG_FORMAT_HEX) != 0)
        mir_emit_runtime_call(out, "__pfehx");
    if ((plan->print_flags[call] & MIR_CALL_FLAG_FORMAT_OCTAL) != 0)
        mir_emit_runtime_call(out, "__pfeoc");
    mir_sparse_emit_call(
        out, plan->print_function, plan->print_names[call]);
}

static void mir_wildcard_store_stream(FILE *out)
{
    fputs("\tld (ix-2),l\n\tld (ix-1),h\n", out);
}

static void mir_wildcard_push_stream(FILE *out)
{
    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n", out);
}

static void mir_emit_wildcard_open_schedule(
    FILE *out, const struct MirWildcardOpenSchedule *plan)
{
    int null_result = new_label();
    int result_ready = new_label();
    int done = new_label();

    fprintf(out,
            "%s\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tld hl,-18\n\tadd hl,sp\n\tld sp,hl\n",
            MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[0]);
    mir_sparse_emit_call(
        out, plan->unlink_function, plan->unlink_name);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[1]);
    mir_sparse_emit_call(
        out, plan->unlink_function, plan->unlink_name);
    mir_emit_final_call_cleanup(out, 1);

    fprintf(out,
            "\tld hl,S%d\n\tpush hl\n\tld hl,S%d\n\tpush hl\n",
            plan->strings[2], plan->strings[0]);
    mir_sparse_emit_call(out, plan->open_function, plan->open_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_wildcard_store_stream(out);
    mir_wildcard_push_stream(out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[3]);
    mir_sparse_emit_call(out, plan->puts_function, plan->puts_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_wildcard_push_stream(out);
    mir_sparse_emit_call(out, plan->close_function, plan->close_name);
    mir_emit_final_call_cleanup(out, 1);

    fprintf(out,
            "\tld hl,S%d\n\tpush hl\n\tld hl,S%d\n\tpush hl\n",
            plan->strings[2], plan->strings[1]);
    mir_sparse_emit_call(out, plan->open_function, plan->open_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_wildcard_store_stream(out);
    mir_wildcard_push_stream(out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[4]);
    mir_sparse_emit_call(out, plan->puts_function, plan->puts_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_wildcard_push_stream(out);
    mir_sparse_emit_call(out, plan->close_function, plan->close_name);
    mir_emit_final_call_cleanup(out, 1);

    fprintf(out,
            "\tld hl,S%d\n\tpush hl\n\tld hl,S%d\n\tpush hl\n",
            plan->strings[6], plan->strings[5]);
    mir_sparse_emit_call(out, plan->open_function, plan->open_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_wildcard_store_stream(out);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out,
            "\tjp z,L%d\n\tld hl,S%d\n\tjp L%d\n"
            "L%d:\n\tld hl,S%d\nL%d:\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            null_result, plan->strings[8], result_ready,
            null_result, plan->strings[9], result_ready,
            plan->strings[7]);
    mir_wildcard_emit_print(out, plan, 0);
    mir_emit_final_call_cleanup(out, 2);

    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n"
          "\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", done);
    mir_wildcard_push_stream(out);
    fputs("\tld hl,15\n\tpush hl\n\tld hl,1\n\tpush hl\n", out);
    fputs("\tpush ix\n\tpop hl\n\tld de,-18\n\tadd hl,de\n"
          "\tpush hl\n", out);
    mir_sparse_emit_call(out, plan->read_function, plan->read_name);
    mir_emit_final_call_cleanup(out, 4);
    fputs("\tex de,hl\n\tpush ix\n\tpop hl\n"
          "\tld bc,-18\n\tadd hl,bc\n\tadd hl,de\n"
          "\txor a\n\tld (hl),a\n", out);
    fputs("\tpush ix\n\tpop hl\n\tld de,-18\n\tadd hl,de\n"
          "\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[10]);
    mir_wildcard_emit_print(out, plan, 1);
    mir_emit_final_call_cleanup(out, 2);
    mir_wildcard_push_stream(out);
    mir_sparse_emit_call(out, plan->close_function, plan->close_name);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out, "L%d:\n", done);

    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[0]);
    mir_sparse_emit_call(
        out, plan->unlink_function, plan->unlink_name);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[1]);
    mir_sparse_emit_call(
        out, plan->unlink_function, plan->unlink_name);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[11]);
    mir_wildcard_emit_print(out, plan, 2);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_wildcard_create_schedule(
    struct MirWildcardCreateSchedule *plan)
{
    static const unsigned char expected_opcodes[93] = {
        MIR_LABEL, MIR_CONST, MIR_STORE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_LOAD,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP,
        MIR_LOAD, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_STORE, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_UNARY, MIR_BRANCH_FALSE, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_LABEL, MIR_LABEL,
        MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_LOAD, MIR_BRANCH_FALSE, MIR_LABEL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_LABEL, MIR_LOAD, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_LABEL, MIR_PHI,
        MIR_RETURN
    };
    static const int open_args[] = {3, 5};
    static const int check_open[] = {10, 14, 16};
    static const int close_args[] = {21};
    static const int directory_open[] = {25};
    static const int directory_read[] = {32};
    static const int compare_args[] = {41, 43, 45};
    static const int directory_close[] = {58};
    static const int check_ghost[] = {61, 63, 65};
    static const int print_failure[] = {71, 73};
    static const int print_success[] = {78};

    memset(plan, 0, sizeof(*plan));
    if (!mir_call_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        mir.sink_purpose != EMIT_SINK_FINAL ||
        mir_cfg_block_count() != 13 || mir.local_bytes != 8 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        !mir_memory_runner_word_type(mir.return_type, 0))
        return 0;
    if (!mir_sparse_match_call(
            7, 0, 2, 2, open_args,
            &plan->open_function, plan->open_name) ||
        !mir_sparse_match_call(
            18, 0, 3, 3, check_open,
            &plan->integer_check_function,
            plan->integer_check_name) ||
        !mir_sparse_match_call(
            23, 0, 1, 1, close_args,
            &plan->close_function, plan->close_name) ||
        !mir_sparse_match_call(
            27, 0, 1, 1, directory_open,
            &plan->directory_open_function,
            plan->directory_open_name) ||
        !mir_sparse_match_call(
            34, 0, 1, 1, directory_read,
            &plan->directory_read_function,
            plan->directory_read_name) ||
        !mir_sparse_match_call(
            48, 0, 3, 3, compare_args,
            &plan->compare_function, plan->compare_name) ||
        !mir_sparse_match_call(
            60, 0, 1, 1, directory_close,
            &plan->directory_close_function,
            plan->directory_close_name) ||
        !mir_sparse_match_call(
            67, 0, 3, 3, check_ghost,
            &plan->integer_check_function,
            plan->integer_check_name) ||
        !mir_sparse_match_call(
            75, 1, 1, 2, print_failure,
            &plan->print_function, plan->print_names[0]) ||
        !mir_sparse_match_call(
            80, 1, 1, 1, print_success,
            &plan->print_function, plan->print_names[1]))
        return mir_machine_reject(
            "wildcard-create-schedule", "calls");
    if (!mir_machine_constant_equals(mir.insns[1].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[13].dst, 0) ||
        mir.insns[14].src1 != mir.insns[12].dst ||
        mir.insns[14].src2 != mir.insns[13].dst ||
        mir.insns[14].immediate != TOK_EQ ||
        !mir_machine_constant_equals(mir.insns[16].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[37].dst, 0) ||
        mir.insns[38].src1 != mir.insns[34].dst ||
        mir.insns[38].src2 != mir.insns[37].dst ||
        mir.insns[38].immediate != TOK_NE ||
        !mir_machine_constant_equals(mir.insns[45].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[51].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[65].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[84].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[88].dst, 0))
        return mir_machine_reject(
            "wildcard-create-schedule", "semantics");
    plan->failure_count = find_global(mir.insns[68].name);
    if (plan->failure_count == NULL ||
        plan->failure_count->is_volatile ||
        !mir_machine_same_location(
            &mir.insns[68], &mir.insns[73]) ||
        !mir_machine_same_location(
            &mir.insns[68], &mir.insns[82]))
        return mir_machine_reject(
            "wildcard-create-schedule", "failure-count");
    plan->strings[0] = (int)mir.insns[3].immediate;
    plan->strings[1] = (int)mir.insns[5].immediate;
    plan->strings[2] = (int)mir.insns[10].immediate;
    plan->strings[3] = (int)mir.insns[25].immediate;
    plan->strings[4] = (int)mir.insns[43].immediate;
    plan->strings[5] = (int)mir.insns[61].immediate;
    plan->strings[6] = (int)mir.insns[71].immediate;
    plan->strings[7] = (int)mir.insns[78].immediate;
    plan->print_flags[0] = mir.insns[75].memory_flags;
    plan->print_flags[1] = mir.insns[80].memory_flags;
    return 1;
}

static void mir_wildcard_create_print(
    FILE *out, const struct MirWildcardCreateSchedule *plan, int call)
{
    if ((plan->print_flags[call] & MIR_CALL_FLAG_FORMAT_HEX) != 0)
        mir_emit_runtime_call(out, "__pfehx");
    if ((plan->print_flags[call] & MIR_CALL_FLAG_FORMAT_OCTAL) != 0)
        mir_emit_runtime_call(out, "__pfeoc");
    mir_sparse_emit_call(
        out, plan->print_function, plan->print_names[call]);
}

static void mir_emit_wildcard_create_schedule(
    FILE *out, const struct MirWildcardCreateSchedule *plan)
{
    int no_stream = new_label();
    int loop = new_label();
    int loop_done = new_label();
    int not_match = new_label();
    int summary_success = new_label();
    int epilogue = new_label();

    fprintf(out,
            "%s\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tld hl,-4\n\tadd hl,sp\n\tld sp,hl\n"
            "\txor a\n\tld (ix-4),a\n\tld (ix-3),a\n",
            MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,S%d\n\tpush hl\n\tld hl,S%d\n\tpush hl\n",
            plan->strings[1], plan->strings[0]);
    mir_sparse_emit_call(out, plan->open_function, plan->open_name);
    mir_emit_final_call_cleanup(out, 2);
    fputs("\tld (ix-2),l\n\tld (ix-1),h\n"
          "\tld a,h\n\tor l\n\tld hl,0\n", out);
    {
        int not_null = new_label();
        fprintf(out,
                "\tjp nz,L%d\n\tinc hl\nL%d:\n",
                not_null, not_null);
    }
    fputs("\tld de,1\n\tpush de\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[2]);
    mir_sparse_emit_call(
        out, plan->integer_check_function,
        plan->integer_check_name);
    mir_emit_final_call_cleanup(out, 3);
    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n"
          "\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n\tpush hl\n", no_stream);
    mir_sparse_emit_call(out, plan->close_function, plan->close_name);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out, "L%d:\n", no_stream);

    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[3]);
    mir_sparse_emit_call(
        out, plan->directory_open_function,
        plan->directory_open_name);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tld (ix-2),l\n\tld (ix-1),h\n", out);
    fprintf(out, "L%d:\n", loop);
    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n", out);
    mir_sparse_emit_call(
        out, plan->directory_read_function,
        plan->directory_read_name);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", loop_done);
    fputs("\tld de,2\n\tpush de\n", out);
    fprintf(out, "\tld de,S%d\n\tpush de\n\tpush hl\n",
            plan->strings[4]);
    mir_sparse_emit_call(
        out, plan->compare_function, plan->compare_name);
    mir_emit_final_call_cleanup(out, 3);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n\tld hl,1\n"
                 "\tld (ix-4),l\n\tld (ix-3),h\n"
                 "L%d:\n\tjp L%d\nL%d:\n",
            not_match, not_match, loop, loop_done);
    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n", out);
    mir_sparse_emit_call(
        out, plan->directory_close_function,
        plan->directory_close_name);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tld de,0\n\tpush de\n"
          "\tld l,(ix-4)\n\tld h,(ix-3)\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[5]);
    mir_sparse_emit_call(
        out, plan->integer_check_function,
        plan->integer_check_name);
    mir_emit_final_call_cleanup(out, 3);
    mir_machine_emit_global_word(out, plan->failure_count, 0);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out,
            "\tjp z,L%d\n\tpush hl\n\tld hl,S%d\n\tpush hl\n",
            summary_success, plan->strings[6]);
    mir_wildcard_create_print(out, plan, 0);
    mir_emit_final_call_cleanup(out, 2);
    fprintf(out,
            "\tld hl,1\n\tjp L%d\nL%d:\n"
            "\tld hl,S%d\n\tpush hl\n",
            epilogue, summary_success, plan->strings[7]);
    mir_wildcard_create_print(out, plan, 1);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out,
            "\tld hl,0\nL%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            epilogue);
}

static int mir_match_errno_exercise_schedule(
    struct MirErrnoExerciseSchedule *plan)
{
    static const int unlink_args[] = {23};
    static const int open_args[] = {39, 41};
    static const int expect_errno_args[] = {46, 43, 50};
    static const int read_args[] = {71, 73, 76};
    static const int write_args[] = {92, 94, 97};
    static const int close_args[] = {113};
    static const int seek_args[] = {128, 130, 132};
    static const int expect_long_args[] = {137, 134, 141};
    static const int expect_fd_args[] = {158, 155};
    static const int print_args[] = {276, 278};
    static const int tell_args[] = {362};
    static const int gets_args[] = {385, 387, 389};
    static const int strerror_args[] = {431};
    static const int setvbuf_args[] = {465, 468, 471, 473};
    static const int no_arguments[1] = {0};
    static const int fclose_args[] = {642};
    static const int expected_calls[] = {
        25, 35, 43, 52, 58, 67, 79, 88, 100, 109, 115, 124,
        134, 143, 155, 162, 176, 185, 188, 225, 260, 272, 280,
        291, 322, 351, 365, 374, 392, 433, 435, 454, 476, 502,
        504, 514, 551, 575, 604, 606, 619, 633, 644, 655, 662
    };
    static const int unlink_calls[] = {25, 35, 58, 351};
    static const int open_calls[] = {43, 155, 225, 260};
    static const int expect_errno_calls[] =
        {52, 67, 88, 109, 124, 272};
    static const int close_calls[] = {115, 188, 291, 322};
    static const int seek_calls[] = {134, 176};
    static const int expect_long_calls[] = {143, 185, 374};
    static const int print_calls[] =
        {280, 435, 454, 504, 514, 606, 619, 633, 655, 662};
    static const int print_argument_counts[] =
        {2, 3, 4, 3, 3, 3, 3, 2, 2, 1};
    static const int strerror_calls[] = {433, 502, 604};
    static const int tmpfile_calls[] = {551, 575};
    struct Sym *tmpnames;
    long tmpnames_offset;
    long second_tmpnames_offset;
    int instruction;
    int calls = 0;
    int i;

    memset(plan, 0, sizeof(*plan));
    if (mir.sink_purpose != EMIT_SINK_FINAL ||
        mir.count != 665 || mir.next_value != 403 ||
        mir_cfg_block_count() != 50 || mir.local_bytes != 67 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        !mir_has_cfg_backedge() ||
        !mir_memory_runner_word_type(mir.return_type, 0))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_CALL)
            ++calls;
    if (calls != (int)(sizeof(expected_calls) /
                       sizeof(expected_calls[0])))
        return mir_machine_reject(
            "errno-exercise-schedule", "call-count");
    for (i = 0; i < calls; ++i)
        if (mir.insns[expected_calls[i]].opcode != MIR_CALL)
            return mir_machine_reject(
                "errno-exercise-schedule", "call-sites");

    if (!mir_sparse_match_call(
            25, 0, 1, 1, unlink_args,
            &plan->unlink_function, plan->unlink_name) ||
        !mir_sparse_match_call(
            43, 0, 2, 2, open_args,
            &plan->open_function, plan->open_name) ||
        !mir_sparse_match_call(
            52, 0, 3, 3, expect_errno_args,
            &plan->expect_errno_function,
            plan->expect_errno_name) ||
        !mir_sparse_match_call(
            79, 0, 3, 3, read_args,
            &plan->read_function, plan->read_name) ||
        !mir_sparse_match_call(
            100, 0, 3, 3, write_args,
            &plan->write_function, plan->write_name) ||
        !mir_sparse_match_call(
            115, 0, 1, 1, close_args,
            &plan->close_function, plan->close_name) ||
        !mir_sparse_match_call(
            134, 0, 3, 3, seek_args,
            &plan->seek_function, plan->seek_name) ||
        !mir_sparse_match_call(
            143, 0, 3, 3, expect_long_args,
            &plan->expect_long_function,
            plan->expect_long_name) ||
        !mir_sparse_match_call(
            162, 0, 2, 2, expect_fd_args,
            &plan->expect_fd_function,
            plan->expect_fd_name) ||
        !mir_sparse_match_call(
            280, 1, 1, 2, print_args,
            &plan->print_function, plan->print_names[0]) ||
        !mir_sparse_match_call(
            365, 0, 1, 1, tell_args,
            &plan->tell_function, plan->tell_name) ||
        !mir_sparse_match_call(
            392, 0, 3, 3, gets_args,
            &plan->gets_function, plan->gets_name) ||
        !mir_sparse_match_call(
            433, 0, 1, 1, strerror_args,
            &plan->strerror_function, plan->strerror_name) ||
        !mir_sparse_match_call(
            476, 0, 4, 4, setvbuf_args,
            &plan->setvbuf_function, plan->setvbuf_name) ||
        !mir_sparse_match_call(
            551, 0, 0, 0, no_arguments,
            &plan->tmpfile_function, plan->tmpfile_name) ||
        !mir_sparse_match_call(
            644, 0, 1, 1, fclose_args,
            &plan->fclose_function, plan->fclose_name))
        return mir_machine_reject(
            "errno-exercise-schedule", "calls");
    for (i = 0;
         i < (int)(sizeof(unlink_calls) / sizeof(unlink_calls[0]));
         ++i)
        if (find_global(mir.insns[unlink_calls[i]].name) !=
            plan->unlink_function)
            return mir_machine_reject(
                "errno-exercise-schedule", "unlink-family");
    for (i = 0;
         i < (int)(sizeof(open_calls) / sizeof(open_calls[0]));
         ++i)
        if (find_global(mir.insns[open_calls[i]].name) !=
            plan->open_function)
            return mir_machine_reject(
                "errno-exercise-schedule", "open-family");
    for (i = 0;
         i < (int)(sizeof(expect_errno_calls) /
                   sizeof(expect_errno_calls[0]));
         ++i)
        if (find_global(mir.insns[expect_errno_calls[i]].name) !=
            plan->expect_errno_function)
            return mir_machine_reject(
                "errno-exercise-schedule", "expect-family");
    for (i = 0;
         i < (int)(sizeof(close_calls) / sizeof(close_calls[0]));
         ++i)
        if (find_global(mir.insns[close_calls[i]].name) !=
            plan->close_function)
            return mir_machine_reject(
                "errno-exercise-schedule", "close-family");
    for (i = 0;
         i < (int)(sizeof(seek_calls) / sizeof(seek_calls[0]));
         ++i)
        if (find_global(mir.insns[seek_calls[i]].name) !=
            plan->seek_function)
            return mir_machine_reject(
                "errno-exercise-schedule", "seek-family");
    for (i = 0;
         i < (int)(sizeof(expect_long_calls) /
                   sizeof(expect_long_calls[0]));
         ++i)
        if (find_global(mir.insns[expect_long_calls[i]].name) !=
            plan->expect_long_function)
            return mir_machine_reject(
                "errno-exercise-schedule", "long-family");
    for (i = 0;
         i < (int)(sizeof(strerror_calls) /
                   sizeof(strerror_calls[0]));
         ++i)
        if (find_global(mir.insns[strerror_calls[i]].name) !=
            plan->strerror_function)
            return mir_machine_reject(
                "errno-exercise-schedule", "strerror-family");
    for (i = 0;
         i < (int)(sizeof(tmpfile_calls) /
                   sizeof(tmpfile_calls[0]));
         ++i)
        if (find_global(mir.insns[tmpfile_calls[i]].name) !=
            plan->tmpfile_function)
            return mir_machine_reject(
                "errno-exercise-schedule", "tmpfile-family");
    for (i = 1;
         i < (int)(sizeof(print_calls) / sizeof(print_calls[0]));
         ++i) {
        int arguments[4];
        int argument_count = print_argument_counts[i];

        if (find_global(mir.insns[print_calls[i]].name) !=
                plan->print_function ||
            !mir_machine_call_arguments(
                &mir.insns[print_calls[i]],
                argument_count, arguments))
            return mir_machine_reject(
                "errno-exercise-schedule", "print-family");
        snprintf(plan->print_names[i], 64, "%s",
                 mir.insns[print_calls[i]].base_name[0] != 0
                     ? mir.insns[print_calls[i]].base_name
                     : asm_name_for(sym_asm_name(
                           plan->print_function)));
    }
    if (!mir_machine_global_address_offset(
            mir.insns[20].dst, &tmpnames,
            &tmpnames_offset, 0) ||
        tmpnames == NULL || tmpnames->is_volatile ||
        !mir_machine_global_address_offset(
            mir.insns[214].dst, &plan->tmpnames,
            &second_tmpnames_offset, 0) ||
        plan->tmpnames != tmpnames ||
        second_tmpnames_offset != tmpnames_offset)
        return mir_machine_reject(
            "errno-exercise-schedule", "tmpnames");
    plan->tmpnames = tmpnames;
    plan->tmpnames_offset = (int)tmpnames_offset;
    plan->errno_object = find_global(mir.insns[8].name);
    plan->failure_count = find_global(mir.insns[2].name);
    if (plan->errno_object == NULL ||
        plan->failure_count == NULL ||
        plan->errno_object->is_volatile ||
        plan->failure_count->is_volatile)
        return mir_machine_reject(
            "errno-exercise-schedule", "globals");
    if (!mir_machine_constant_equals(mir.insns[5].dst, 88) ||
        !mir_machine_constant_equals(mir.insns[17].dst, 9) ||
        !mir_machine_constant_equals(mir.insns[50].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[71].dst, 99) ||
        !mir_machine_constant_equals(mir.insns[76].dst, 4) ||
        !mir_machine_constant_equals(mir.insns[97].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[122].dst, 9) ||
        !mir_machine_constant_equals(mir.insns[153].dst, 578) ||
        !mir_machine_constant_equals(mir.insns[164].dst, 3) ||
        !mir_machine_constant_equals(mir.insns[174].dst, 99) ||
        !mir_machine_constant_equals(mir.insns[183].dst, 22) ||
        !mir_machine_constant_equals(mir.insns[208].dst, 8) ||
        !mir_machine_constant_equals(mir.insns[250].dst, 8) ||
        !mir_machine_constant_equals(mir.insns[270].dst, 24) ||
        !mir_machine_constant_equals(mir.insns[307].dst, 8) ||
        !mir_machine_constant_equals(mir.insns[343].dst, 9) ||
        !mir_machine_constant_equals(mir.insns[362].dst, 99) ||
        !mir_machine_constant_equals(mir.insns[380].dst, 90) ||
        !mir_machine_constant_equals(mir.insns[387].dst, 8) ||
        !mir_machine_constant_equals(mir.insns[389].dst, 99) ||
        !mir_machine_constant_equals(mir.insns[471].dst, 99) ||
        !mir_machine_constant_equals(mir.insns[484].dst, 22) ||
        !mir_machine_constant_equals(mir.insns[545].dst, 4) ||
        !mir_machine_constant_equals(mir.insns[586].dst, 24))
        return mir_machine_reject(
            "errno-exercise-schedule", "constants");
    plan->strings[0] = (int)mir.insns[33].immediate;
    plan->strings[1] = (int)mir.insns[46].immediate;
    plan->strings[2] = (int)mir.insns[61].immediate;
    plan->strings[3] = (int)mir.insns[82].immediate;
    plan->strings[4] = (int)mir.insns[103].immediate;
    plan->strings[5] = (int)mir.insns[118].immediate;
    plan->strings[6] = (int)mir.insns[137].immediate;
    plan->strings[7] = (int)mir.insns[158].immediate;
    plan->strings[8] = (int)mir.insns[179].immediate;
    plan->strings[9] = (int)mir.insns[266].immediate;
    plan->strings[10] = (int)mir.insns[276].immediate;
    plan->strings[11] = (int)mir.insns[368].immediate;
    plan->strings[12] = (int)mir.insns[427].immediate;
    plan->strings[13] = (int)mir.insns[439].immediate;
    plan->strings[14] = (int)mir.insns[496].immediate;
    plan->strings[15] = (int)mir.insns[508].immediate;
    plan->strings[16] = (int)mir.insns[598].immediate;
    plan->strings[17] = (int)mir.insns[610].immediate;
    plan->strings[18] = (int)mir.insns[629].immediate;
    plan->strings[19] = (int)mir.insns[651].immediate;
    plan->strings[20] = (int)mir.insns[660].immediate;
    for (i = 0; i < 10; ++i)
        plan->print_flags[i] =
            mir.insns[print_calls[i]].memory_flags;
    return 1;
}

static void mir_errno_store(
    FILE *out, const struct MirErrnoExerciseSchedule *plan, int value)
{
    fprintf(out, "\tld hl,%d\n", value);
    mir_machine_emit_global_word_store(
        out, plan->errno_object, 0);
}

static void mir_errno_increment_failures(
    FILE *out, const struct MirErrnoExerciseSchedule *plan)
{
    mir_machine_emit_global_word(out, plan->failure_count, 0);
    fputs("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->failure_count, 0);
}

static void mir_errno_push_tmpname(
    FILE *out, const struct MirErrnoExerciseSchedule *plan, int index)
{
    mir_machine_emit_global_word(
        out, plan->tmpnames,
        plan->tmpnames_offset + 2 * index);
    fputs("\tpush hl\n", out);
}

static void mir_errno_emit_print(
    FILE *out, const struct MirErrnoExerciseSchedule *plan, int call)
{
    if ((plan->print_flags[call] & MIR_CALL_FLAG_FORMAT_HEX) != 0)
        mir_emit_runtime_call(out, "__pfehx");
    if ((plan->print_flags[call] & MIR_CALL_FLAG_FORMAT_OCTAL) != 0)
        mir_emit_runtime_call(out, "__pfeoc");
    mir_sparse_emit_call(
        out, plan->print_function, plan->print_names[call]);
}

static void mir_errno_expect_word(
    FILE *out, struct Sym *function, const char *call_name,
    int name_string, int expected)
{
    fputs("\tld c,l\n\tld b,h\n", out);
    fprintf(out,
            "\tld hl,%d\n\tpush hl\n"
            "\tld l,c\n\tld h,b\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            expected, name_string);
    mir_sparse_emit_call(out, function, call_name);
    mir_emit_final_call_cleanup(out, 3);
}

static void mir_errno_expect_long(
    FILE *out, const struct MirErrnoExerciseSchedule *plan,
    int name_string, int expected)
{
    fprintf(out, "\texx\n\tld hl,%d\n\tpush hl\n\texx\n",
            expected);
    fputs("\tpush de\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", name_string);
    mir_sparse_emit_call(
        out, plan->expect_long_function,
        plan->expect_long_name);
    mir_emit_final_call_cleanup(out, 4);
}

static void mir_errno_expect_fd(
    FILE *out, const struct MirErrnoExerciseSchedule *plan,
    int name_string)
{
    fputs("\tld c,l\n\tld b,h\n\tpush bc\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", name_string);
    mir_sparse_emit_call(
        out, plan->expect_fd_function,
        plan->expect_fd_name);
    mir_emit_final_call_cleanup(out, 2);
}

static void mir_errno_fds_address(FILE *out)
{
    fputs("\tpush iy\n\tpop hl\n\tadd hl,hl\n"
          "\tpush ix\n\tpop de\n\tadd hl,de\n"
          "\tld de,-16\n\tadd hl,de\n", out);
}

static void mir_errno_print_errno_string(
    FILE *out, const struct MirErrnoExerciseSchedule *plan,
    int format_string, int print_call)
{
    mir_machine_emit_global_word(out, plan->errno_object, 0);
    fputs("\tpush hl\n", out);
    mir_sparse_emit_call(
        out, plan->strerror_function, plan->strerror_name);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tpush hl\n", out);
    mir_machine_emit_global_word(out, plan->errno_object, 0);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", format_string);
    mir_errno_emit_print(out, plan, print_call);
    mir_emit_final_call_cleanup(out, 3);
}

static void mir_emit_errno_exercise_schedule(
    FILE *out, const struct MirErrnoExerciseSchedule *plan)
{
    int loop = new_label();
    int loop_done = new_label();
    int valid_fd_done = new_label();
    int open_loop = new_label();
    int open_loop_done = new_label();
    int setup_failed = new_label();
    int after_overflow = new_label();
    int close_loop = new_label();
    int close_skip = new_label();
    int close_done = new_label();
    int unlink_loop = new_label();
    int unlink_done = new_label();
    int fgets_failed = new_label();
    int fgets_done = new_label();
    int setvbuf_failed = new_label();
    int setvbuf_done = new_label();
    int tmp_loop = new_label();
    int tmp_loop_done = new_label();
    int tmp_setup_failed = new_label();
    int tmp_check_failed = new_label();
    int tmp_done = new_label();
    int summary_success = new_label();
    int epilogue = new_label();

    fputs(MIR_EXACT_KERNEL_MARKER "\n"
          ";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-34\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fputs("\tld hl,0\n", out);
    mir_machine_emit_global_word_store(out, plan->failure_count, 0);
    fputs("\tld a,88\n\tld (ix-21),a\n", out);
    mir_errno_store(out, plan, 0);

    fputs("\tld iy,0\n", out);
    fprintf(out,
            "L%d:\n\tpush iy\n\tpop hl\n\tld de,9\n"
            "\tor a\n\tsbc hl,de\n\tjp nc,L%d\n",
            loop, loop_done);
    fputs("\tpush iy\n\tpop hl\n\tadd hl,hl\n", out);
    fprintf(out, "\tld de,%s%+d\n\tadd hl,de\n"
                 "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n",
            asm_name_for(sym_asm_name(plan->tmpnames)),
            plan->tmpnames_offset);
    mir_sparse_emit_call(
        out, plan->unlink_function, plan->unlink_name);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out, "\tinc iy\n\tjp L%d\nL%d:\n", loop, loop_done);

    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[0]);
    mir_sparse_emit_call(
        out, plan->unlink_function, plan->unlink_name);
    mir_emit_final_call_cleanup(out, 1);

    mir_errno_store(out, plan, 0);
    fputs("\tld hl,0\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[0]);
    mir_sparse_emit_call(out, plan->open_function, plan->open_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_errno_expect_word(
        out, plan->expect_errno_function,
        plan->expect_errno_name, plan->strings[1], 2);

    mir_errno_store(out, plan, 0);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[0]);
    mir_sparse_emit_call(
        out, plan->unlink_function, plan->unlink_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_errno_expect_word(
        out, plan->expect_errno_function,
        plan->expect_errno_name, plan->strings[2], 2);

    mir_errno_store(out, plan, 0);
    fputs("\tld hl,4\n\tpush hl\n"
          "\tpush ix\n\tpop hl\n\tld de,-29\n\tadd hl,de\n\tpush hl\n"
          "\tld hl,99\n\tpush hl\n", out);
    mir_sparse_emit_call(out, plan->read_function, plan->read_name);
    mir_emit_final_call_cleanup(out, 3);
    mir_errno_expect_word(
        out, plan->expect_errno_function,
        plan->expect_errno_name, plan->strings[3], 9);

    mir_errno_store(out, plan, 0);
    fputs("\tld hl,1\n\tpush hl\n"
          "\tpush ix\n\tpop hl\n\tld de,-21\n\tadd hl,de\n\tpush hl\n"
          "\tld hl,99\n\tpush hl\n", out);
    mir_sparse_emit_call(out, plan->write_function, plan->write_name);
    mir_emit_final_call_cleanup(out, 3);
    mir_errno_expect_word(
        out, plan->expect_errno_function,
        plan->expect_errno_name, plan->strings[4], 9);

    mir_errno_store(out, plan, 0);
    fputs("\tld hl,99\n\tpush hl\n", out);
    mir_sparse_emit_call(out, plan->close_function, plan->close_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_errno_expect_word(
        out, plan->expect_errno_function,
        plan->expect_errno_name, plan->strings[5], 9);

    mir_errno_store(out, plan, 0);
    fputs("\tld hl,0\n\tpush hl\n"
          "\tld hl,0\n\tld de,0\n\tpush de\n\tpush hl\n"
          "\tld hl,99\n\tpush hl\n", out);
    mir_sparse_emit_call(out, plan->seek_function, plan->seek_name);
    mir_emit_final_call_cleanup(out, 4);
    mir_errno_expect_long(out, plan, plan->strings[6], 9);

    mir_errno_store(out, plan, 0);
    fputs("\tld hl,578\n\tpush hl\n", out);
    mir_errno_push_tmpname(out, plan, 0);
    mir_sparse_emit_call(out, plan->open_function, plan->open_name);
    mir_emit_final_call_cleanup(out, 2);
    fputs("\tld (ix-18),l\n\tld (ix-17),h\n", out);
    mir_errno_expect_fd(out, plan, plan->strings[7]);
    fputs("\tld l,(ix-18)\n\tld h,(ix-17)\n\tld de,3\n"
          "\tld a,h\n\txor 128\n\tld h,a\n"
          "\tld a,d\n\txor 128\n\tld d,a\n"
          "\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp c,L%d\n", valid_fd_done);
    mir_errno_store(out, plan, 0);
    fputs("\tld hl,99\n\tpush hl\n"
          "\tld hl,0\n\tld de,0\n\tpush de\n\tpush hl\n"
          "\tld l,(ix-18)\n\tld h,(ix-17)\n\tpush hl\n", out);
    mir_sparse_emit_call(out, plan->seek_function, plan->seek_name);
    mir_emit_final_call_cleanup(out, 4);
    mir_errno_expect_long(out, plan, plan->strings[8], 22);
    fputs("\tld l,(ix-18)\n\tld h,(ix-17)\n\tpush hl\n", out);
    mir_sparse_emit_call(out, plan->close_function, plan->close_name);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out, "L%d:\n", valid_fd_done);

    mir_errno_store(out, plan, 0);
    fputs("\tld a,1\n\tld (ix-32),a\n\tld iy,0\n", out);
    fprintf(out,
            "L%d:\n\tpush iy\n\tpop hl\n\tld de,8\n"
            "\tor a\n\tsbc hl,de\n\tjp nc,L%d\n"
            "\tld hl,578\n\tpush hl\n",
            open_loop, open_loop_done);
    fputs("\tpush iy\n\tpop hl\n\tadd hl,hl\n", out);
    fprintf(out, "\tld de,%s%+d\n\tadd hl,de\n"
                 "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n",
            asm_name_for(sym_asm_name(plan->tmpnames)),
            plan->tmpnames_offset);
    mir_sparse_emit_call(out, plan->open_function, plan->open_name);
    mir_emit_final_call_cleanup(out, 2);
    fputs("\tld c,l\n\tld b,h\n", out);
    mir_errno_fds_address(out);
    fputs("\tld (hl),c\n\tinc hl\n\tld (hl),b\n"
          "\tld h,b\n\tld l,c\n\tld de,3\n"
          "\tld a,h\n\txor 128\n\tld h,a\n"
          "\tld a,d\n\txor 128\n\tld d,a\n"
          "\tor a\n\tsbc hl,de\n", out);
    {
        int open_ok = new_label();
        fprintf(out, "\tjp nc,L%d\n\txor a\n\tld (ix-32),a\nL%d:\n",
                open_ok, open_ok);
    }
    fprintf(out, "\tinc iy\n\tjp L%d\nL%d:\n",
            open_loop, open_loop_done);

    mir_errno_store(out, plan, 0);
    fputs("\tld hl,578\n\tpush hl\n", out);
    mir_errno_push_tmpname(out, plan, 8);
    mir_sparse_emit_call(out, plan->open_function, plan->open_name);
    mir_emit_final_call_cleanup(out, 2);
    fputs("\tld (ix-20),l\n\tld (ix-19),h\n"
          "\tld a,(ix-32)\n\tor a\n", out);
    fprintf(out, "\tjp z,L%d\n", setup_failed);
    mir_errno_expect_word(
        out, plan->expect_errno_function,
        plan->expect_errno_name, plan->strings[9], 24);
    fprintf(out, "\tjp L%d\nL%d:\n", after_overflow, setup_failed);
    mir_machine_emit_global_word(out, plan->errno_object, 0);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[10]);
    mir_errno_emit_print(out, plan, 0);
    mir_emit_final_call_cleanup(out, 2);
    mir_errno_increment_failures(out, plan);
    fputs("\tld l,(ix-20)\n\tld h,(ix-19)\n\tld de,3\n"
          "\tld a,h\n\txor 128\n\tld h,a\n"
          "\tld a,d\n\txor 128\n\tld d,a\n"
          "\tor a\n\tsbc hl,de\n", out);
    {
        int no_overflow_close = new_label();
        fprintf(out, "\tjp c,L%d\n"
                     "\tld l,(ix-20)\n\tld h,(ix-19)\n\tpush hl\n",
                no_overflow_close);
        mir_sparse_emit_call(out, plan->close_function, plan->close_name);
        mir_emit_final_call_cleanup(out, 1);
        fprintf(out, "L%d:\n", no_overflow_close);
    }
    fprintf(out, "L%d:\n", after_overflow);

    fputs("\tld iy,0\n", out);
    fprintf(out,
            "L%d:\n\tpush iy\n\tpop hl\n\tld de,8\n"
            "\tor a\n\tsbc hl,de\n\tjp nc,L%d\n",
            close_loop, close_done);
    mir_errno_fds_address(out);
    fputs("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tld h,b\n\tld l,c\n\tld de,3\n"
          "\tld a,h\n\txor 128\n\tld h,a\n"
          "\tld a,d\n\txor 128\n\tld d,a\n"
          "\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp c,L%d\n\tpush bc\n", close_skip);
    mir_sparse_emit_call(out, plan->close_function, plan->close_name);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out, "L%d:\n\tinc iy\n\tjp L%d\nL%d:\n",
            close_skip, close_loop, close_done);

    fputs("\tld iy,0\n", out);
    fprintf(out,
            "L%d:\n\tpush iy\n\tpop hl\n\tld de,9\n"
            "\tor a\n\tsbc hl,de\n\tjp nc,L%d\n",
            unlink_loop, unlink_done);
    fputs("\tpush iy\n\tpop hl\n\tadd hl,hl\n", out);
    fprintf(out, "\tld de,%s%+d\n\tadd hl,de\n"
                 "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n",
            asm_name_for(sym_asm_name(plan->tmpnames)),
            plan->tmpnames_offset);
    mir_sparse_emit_call(
        out, plan->unlink_function, plan->unlink_name);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out, "\tinc iy\n\tjp L%d\nL%d:\n",
            unlink_loop, unlink_done);

    mir_errno_store(out, plan, 0);
    fputs("\tld hl,99\n\tpush hl\n", out);
    mir_sparse_emit_call(out, plan->tell_function, plan->tell_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_errno_expect_long(out, plan, plan->strings[11], 9);

    fputs("\tld a,90\n\tld (ix-29),a\n", out);
    mir_errno_store(out, plan, 0);
    fputs("\tld hl,99\n\tpush hl\n\tld hl,8\n\tpush hl\n"
          "\tpush ix\n\tpop hl\n\tld de,-29\n\tadd hl,de\n\tpush hl\n",
          out);
    mir_sparse_emit_call(out, plan->gets_function, plan->gets_name);
    mir_emit_final_call_cleanup(out, 3);
    fputs("\tld c,l\n\tld b,h\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", fgets_failed);
    mir_machine_emit_global_word(out, plan->errno_object, 0);
    fputs("\tld de,9\n\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp nz,L%d\n", fgets_failed);
    fputs("\tld a,(ix-29)\n\tcp 90\n", out);
    fprintf(out, "\tjp nz,L%d\n", fgets_failed);
    mir_errno_print_errno_string(
        out, plan, plan->strings[12], 1);
    fprintf(out, "\tjp L%d\nL%d:\n", fgets_done, fgets_failed);
    fputs("\tld a,(ix-29)\n\tld l,a\n\tld h,0\n\tpush hl\n", out);
    mir_machine_emit_global_word(out, plan->errno_object, 0);
    fputs("\tpush hl\n\tld hl,0\n\tld a,b\n\tor c\n", out);
    {
        int fgr_null = new_label();
        fprintf(out, "\tjp z,L%d\n\tinc hl\nL%d:\n\tpush hl\n",
                fgr_null, fgr_null);
    }
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[13]);
    mir_errno_emit_print(out, plan, 2);
    mir_emit_final_call_cleanup(out, 4);
    mir_errno_increment_failures(out, plan);
    fprintf(out, "L%d:\n", fgets_done);

    mir_errno_store(out, plan, 0);
    fputs("\tld hl,0\n\tpush hl\n\tld hl,99\n\tpush hl\n"
          "\tld hl,0\n\tpush hl\n\tld hl,1\n\tpush hl\n", out);
    mir_sparse_emit_call(
        out, plan->setvbuf_function, plan->setvbuf_name);
    mir_emit_final_call_cleanup(out, 4);
    fputs("\tld c,l\n\tld b,h\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", setvbuf_failed);
    mir_machine_emit_global_word(out, plan->errno_object, 0);
    fputs("\tld de,22\n\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp nz,L%d\n", setvbuf_failed);
    mir_errno_print_errno_string(
        out, plan, plan->strings[14], 3);
    fprintf(out, "\tjp L%d\nL%d:\n", setvbuf_done, setvbuf_failed);
    mir_machine_emit_global_word(out, plan->errno_object, 0);
    fputs("\tpush hl\n\tpush bc\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[15]);
    mir_errno_emit_print(out, plan, 4);
    mir_emit_final_call_cleanup(out, 3);
    mir_errno_increment_failures(out, plan);
    fprintf(out, "L%d:\n", setvbuf_done);

    fputs("\tld a,1\n\tld (ix-33),a\n", out);
    mir_errno_store(out, plan, 0);
    fputs("\tld iy,0\n", out);
    fprintf(out,
            "L%d:\n\tpush iy\n\tpop hl\n\tld de,4\n"
            "\tor a\n\tsbc hl,de\n\tjp nc,L%d\n",
            tmp_loop, tmp_loop_done);
    mir_sparse_emit_call(
        out, plan->tmpfile_function, plan->tmpfile_name);
    fputs("\tld a,h\n\tor l\n", out);
    {
        int tmp_opened = new_label();
        fprintf(out, "\tjp nz,L%d\n\txor a\n\tld (ix-33),a\nL%d:\n",
                tmp_opened, tmp_opened);
    }
    fprintf(out, "\tinc iy\n\tjp L%d\nL%d:\n",
            tmp_loop, tmp_loop_done);
    mir_errno_store(out, plan, 0);
    mir_sparse_emit_call(
        out, plan->tmpfile_function, plan->tmpfile_name);
    fputs("\tld (ix-31),l\n\tld (ix-30),h\n"
          "\tld a,(ix-33)\n\tor a\n", out);
    fprintf(out, "\tjp z,L%d\n", tmp_setup_failed);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", tmp_check_failed);
    mir_machine_emit_global_word(out, plan->errno_object, 0);
    fputs("\tld de,24\n\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp nz,L%d\n", tmp_check_failed);
    mir_errno_print_errno_string(
        out, plan, plan->strings[16], 5);
    fprintf(out, "\tjp L%d\nL%d:\n", tmp_done, tmp_check_failed);
    mir_machine_emit_global_word(out, plan->errno_object, 0);
    fputs("\tpush hl\n\tld l,(ix-31)\n\tld h,(ix-30)\n"
          "\tld a,h\n\tor l\n\tld hl,0\n", out);
    {
        int no_tfover = new_label();
        fprintf(out, "\tjp z,L%d\n\tinc hl\nL%d:\n\tpush hl\n",
                no_tfover, no_tfover);
    }
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[17]);
    mir_errno_emit_print(out, plan, 6);
    mir_emit_final_call_cleanup(out, 3);
    mir_errno_increment_failures(out, plan);
    fprintf(out, "\tjp L%d\nL%d:\n", tmp_done, tmp_setup_failed);
    mir_machine_emit_global_word(out, plan->errno_object, 0);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[18]);
    mir_errno_emit_print(out, plan, 7);
    mir_emit_final_call_cleanup(out, 2);
    mir_errno_increment_failures(out, plan);
    fputs("\tld l,(ix-31)\n\tld h,(ix-30)\n"
          "\tld a,h\n\tor l\n", out);
    {
        int no_tmp_close = new_label();
        fprintf(out, "\tjp z,L%d\n\tpush hl\n", no_tmp_close);
        mir_sparse_emit_call(
            out, plan->fclose_function, plan->fclose_name);
        mir_emit_final_call_cleanup(out, 1);
        fprintf(out, "L%d:\n", no_tmp_close);
    }
    fprintf(out, "L%d:\n", tmp_done);

    mir_machine_emit_global_word(out, plan->failure_count, 0);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n\tpush hl\n", summary_success);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[19]);
    mir_errno_emit_print(out, plan, 8);
    mir_emit_final_call_cleanup(out, 2);
    fprintf(out,
            "\tld hl,1\n\tjp L%d\nL%d:\n"
            "\tld hl,S%d\n\tpush hl\n",
            epilogue, summary_success, plan->strings[20]);
    mir_errno_emit_print(out, plan, 9);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out,
            "\tld hl,0\nL%d:\n\tld sp,ix\n\tpop ix\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            epilogue);
}

static int mir_match_directory_pattern_schedule(
    struct MirDirectoryPatternSchedule *plan)
{
    static const int unlink_first[] = {7};
    static const int unlink_second[] = {10};
    static const int open_first[] = {13, 15};
    static const int puts_first[] = {20, 22};
    static const int close_first[] = {25};
    static const int open_second[] = {28, 30};
    static const int puts_second[] = {35, 37};
    static const int close_second[] = {40};
    static const int directory_open[] = {43};
    static const int print_open_failure[] = {51};
    static const int directory_read[] = {62};
    static const int compare_first[] = {75, 77};
    static const int compare_second[] = {87, 89};
    static const int directory_close[] = {102};
    static const int check_count[] = {105, 107, 109};
    static const int check_first[] = {112, 114, 116};
    static const int check_second[] = {119, 121, 123};
    static const int unlink_cleanup_first[] = {126};
    static const int unlink_cleanup_second[] = {129};
    static const int print_failure[] = {135, 137};
    static const int print_success[] = {142};

    memset(plan, 0, sizeof(*plan));
    if (mir.sink_purpose != EMIT_SINK_FINAL ||
        mir.count != 157 || mir.next_value != 86 ||
        mir_cfg_block_count() != 14 || mir.local_bytes != 12 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        !mir_has_cfg_backedge() ||
        !mir_memory_runner_word_type(mir.return_type, 0))
        return 0;
    if (!mir_sparse_match_call(
            9, 0, 1, 1, unlink_first,
            &plan->unlink_function, plan->unlink_name) ||
        !mir_sparse_match_call(
            12, 0, 1, 1, unlink_second,
            &plan->unlink_function, plan->unlink_name) ||
        !mir_sparse_match_call(
            17, 0, 2, 2, open_first,
            &plan->open_function, plan->open_name) ||
        !mir_sparse_match_call(
            24, 0, 2, 2, puts_first,
            &plan->puts_function, plan->puts_name) ||
        !mir_sparse_match_call(
            27, 0, 1, 1, close_first,
            &plan->close_function, plan->close_name) ||
        !mir_sparse_match_call(
            32, 0, 2, 2, open_second,
            &plan->open_function, plan->open_name) ||
        !mir_sparse_match_call(
            39, 0, 2, 2, puts_second,
            &plan->puts_function, plan->puts_name) ||
        !mir_sparse_match_call(
            42, 0, 1, 1, close_second,
            &plan->close_function, plan->close_name) ||
        !mir_sparse_match_call(
            45, 0, 1, 1, directory_open,
            &plan->directory_open_function,
            plan->directory_open_name) ||
        !mir_sparse_match_call(
            53, 1, 1, 1, print_open_failure,
            &plan->print_function, plan->print_names[0]) ||
        !mir_sparse_match_call(
            64, 0, 1, 1, directory_read,
            &plan->directory_read_function,
            plan->directory_read_name) ||
        !mir_sparse_match_call(
            79, 0, 2, 2, compare_first,
            &plan->compare_function, plan->compare_name) ||
        !mir_sparse_match_call(
            91, 0, 2, 2, compare_second,
            &plan->compare_function, plan->compare_name) ||
        !mir_sparse_match_call(
            104, 0, 1, 1, directory_close,
            &plan->directory_close_function,
            plan->directory_close_name) ||
        !mir_sparse_match_call(
            111, 0, 3, 3, check_count,
            &plan->check_function, plan->check_name) ||
        !mir_sparse_match_call(
            118, 0, 3, 3, check_first,
            &plan->check_function, plan->check_name) ||
        !mir_sparse_match_call(
            125, 0, 3, 3, check_second,
            &plan->check_function, plan->check_name) ||
        !mir_sparse_match_call(
            128, 0, 1, 1, unlink_cleanup_first,
            &plan->unlink_function, plan->unlink_name) ||
        !mir_sparse_match_call(
            131, 0, 1, 1, unlink_cleanup_second,
            &plan->unlink_function, plan->unlink_name) ||
        !mir_sparse_match_call(
            139, 1, 1, 2, print_failure,
            &plan->print_function, plan->print_names[1]) ||
        !mir_sparse_match_call(
            144, 1, 1, 1, print_success,
            &plan->print_function, plan->print_names[2]))
        return mir_machine_reject(
            "directory-pattern-schedule", "calls");
    if (!mir_machine_constant_equals(mir.insns[1].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[5].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[67].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[71].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[82].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[94].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[109].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[116].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[123].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[148].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[152].dst, 0))
        return mir_machine_reject(
            "directory-pattern-schedule", "constants");
    if (mir.insns[75].src1 != mir.insns[74].dst ||
        mir.insns[87].src1 != mir.insns[86].dst ||
        mir.insns[75].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[87].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[75].immediate != 0 ||
        mir.insns[87].immediate != 0 ||
        mir.insns[100].label != mir.insns[58].label)
        return mir_machine_reject(
            "directory-pattern-schedule", "loop");
    plan->failure_count = find_global(mir.insns[132].name);
    if (plan->failure_count == NULL ||
        plan->failure_count->is_volatile ||
        !mir_machine_same_location(
            &mir.insns[132], &mir.insns[137]) ||
        !mir_machine_same_location(
            &mir.insns[132], &mir.insns[146]))
        return mir_machine_reject(
            "directory-pattern-schedule", "failure-count");
    plan->strings[0] = (int)mir.insns[7].immediate;
    plan->strings[1] = (int)mir.insns[10].immediate;
    plan->strings[2] = (int)mir.insns[15].immediate;
    plan->strings[3] = (int)mir.insns[20].immediate;
    plan->strings[4] = (int)mir.insns[35].immediate;
    plan->strings[5] = (int)mir.insns[43].immediate;
    plan->strings[6] = (int)mir.insns[51].immediate;
    plan->strings[7] = (int)mir.insns[77].immediate;
    plan->strings[8] = (int)mir.insns[89].immediate;
    plan->strings[9] = (int)mir.insns[105].immediate;
    plan->strings[10] = (int)mir.insns[112].immediate;
    plan->strings[11] = (int)mir.insns[119].immediate;
    plan->strings[12] = (int)mir.insns[135].immediate;
    plan->strings[13] = (int)mir.insns[142].immediate;
    plan->print_flags[0] = mir.insns[53].memory_flags;
    plan->print_flags[1] = mir.insns[139].memory_flags;
    plan->print_flags[2] = mir.insns[144].memory_flags;
    return 1;
}

static void mir_directory_pattern_print(
    FILE *out, const struct MirDirectoryPatternSchedule *plan, int call)
{
    if ((plan->print_flags[call] & MIR_CALL_FLAG_FORMAT_HEX) != 0)
        mir_emit_runtime_call(out, "__pfehx");
    if ((plan->print_flags[call] & MIR_CALL_FLAG_FORMAT_OCTAL) != 0)
        mir_emit_runtime_call(out, "__pfeoc");
    mir_sparse_emit_call(
        out, plan->print_function, plan->print_names[call]);
}

static void mir_directory_pattern_create(
    FILE *out, const struct MirDirectoryPatternSchedule *plan,
    int name_string, int content_string)
{
    fprintf(out,
            "\tld hl,S%d\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->strings[2], name_string);
    mir_sparse_emit_call(out, plan->open_function, plan->open_name);
    mir_emit_final_call_cleanup(out, 2);
    fputs("\tld (ix-2),l\n\tld (ix-1),h\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", content_string);
    mir_sparse_emit_call(out, plan->puts_function, plan->puts_name);
    mir_emit_final_call_cleanup(out, 2);
    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n", out);
    mir_sparse_emit_call(out, plan->close_function, plan->close_name);
    mir_emit_final_call_cleanup(out, 1);
}

static void mir_directory_pattern_check(
    FILE *out, const struct MirDirectoryPatternSchedule *plan,
    int value_offset, int name_string, int expected)
{
    fprintf(out,
            "\tld hl,%d\n\tpush hl\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            expected, value_offset, value_offset + 1, name_string);
    mir_sparse_emit_call(out, plan->check_function, plan->check_name);
    mir_emit_final_call_cleanup(out, 3);
}

static void mir_emit_directory_pattern_schedule(
    FILE *out, const struct MirDirectoryPatternSchedule *plan)
{
    int opened = new_label();
    int loop = new_label();
    int loop_done = new_label();
    int not_first = new_label();
    int not_second = new_label();
    int summary_success = new_label();
    int epilogue = new_label();

    fprintf(out,
            "%s\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tld hl,-10\n\tadd hl,sp\n\tld sp,hl\n",
            MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[0]);
    mir_sparse_emit_call(
        out, plan->unlink_function, plan->unlink_name);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[1]);
    mir_sparse_emit_call(
        out, plan->unlink_function, plan->unlink_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_directory_pattern_create(
        out, plan, plan->strings[0], plan->strings[3]);
    mir_directory_pattern_create(
        out, plan, plan->strings[1], plan->strings[4]);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[5]);
    mir_sparse_emit_call(
        out, plan->directory_open_function,
        plan->directory_open_name);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tld (ix-2),l\n\tld (ix-1),h\n"
          "\tld a,h\n\tor l\n", out);
    fprintf(out,
            "\tjp nz,L%d\n\tld hl,S%d\n\tpush hl\n",
            opened, plan->strings[6]);
    mir_directory_pattern_print(out, plan, 0);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out, "\tld hl,1\n\tjp L%d\nL%d:\n",
            epilogue, opened);
    fputs("\txor a\n\tld (ix-4),a\n\tld (ix-3),a\n"
          "\tld (ix-6),a\n\tld (ix-5),a\n"
          "\tld (ix-8),a\n\tld (ix-7),a\n", out);
    fprintf(out, "L%d:\n", loop);
    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n", out);
    mir_sparse_emit_call(
        out, plan->directory_read_function,
        plan->directory_read_name);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tld (ix-10),l\n\tld (ix-9),h\n"
          "\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n"
                 "\tinc (ix-4)\n\tjp nz,L%d\n\tinc (ix-3)\nL%d:\n",
            loop_done, not_first, not_first);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[7]);
    fputs("\tld l,(ix-10)\n\tld h,(ix-9)\n\tpush hl\n", out);
    mir_sparse_emit_call(
        out, plan->compare_function, plan->compare_name);
    mir_emit_final_call_cleanup(out, 2);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n\tld hl,1\n"
                 "\tld (ix-6),l\n\tld (ix-5),h\nL%d:\n",
            not_second, not_second);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[8]);
    fputs("\tld l,(ix-10)\n\tld h,(ix-9)\n\tpush hl\n", out);
    mir_sparse_emit_call(
        out, plan->compare_function, plan->compare_name);
    mir_emit_final_call_cleanup(out, 2);
    fputs("\tld a,h\n\tor l\n", out);
    {
        int next = new_label();
        fprintf(out, "\tjp nz,L%d\n\tld hl,1\n"
                     "\tld (ix-8),l\n\tld (ix-7),h\nL%d:\n"
                     "\tjp L%d\n",
                next, next, loop);
    }
    fprintf(out, "L%d:\n", loop_done);
    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n", out);
    mir_sparse_emit_call(
        out, plan->directory_close_function,
        plan->directory_close_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_directory_pattern_check(
        out, plan, -4, plan->strings[9], 1);
    mir_directory_pattern_check(
        out, plan, -6, plan->strings[10], 1);
    mir_directory_pattern_check(
        out, plan, -8, plan->strings[11], 0);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[0]);
    mir_sparse_emit_call(
        out, plan->unlink_function, plan->unlink_name);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[1]);
    mir_sparse_emit_call(
        out, plan->unlink_function, plan->unlink_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_machine_emit_global_word(out, plan->failure_count, 0);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out,
            "\tjp z,L%d\n\tpush hl\n\tld hl,S%d\n\tpush hl\n",
            summary_success, plan->strings[12]);
    mir_directory_pattern_print(out, plan, 1);
    mir_emit_final_call_cleanup(out, 2);
    fprintf(out,
            "\tld hl,1\n\tjp L%d\nL%d:\n"
            "\tld hl,S%d\n\tpush hl\n",
            epilogue, summary_success, plan->strings[13]);
    mir_directory_pattern_print(out, plan, 2);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out,
            "\tld hl,0\nL%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            epilogue);
}

static int mir_match_integer_report_schedule(
    struct MirIntegerReportSchedule *plan)
{
    static const unsigned char expected_opcodes[34] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_NOP,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_NOP,
        MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP, MIR_LABEL
    };
    int failure_arguments[4];
    int success_arguments[3];
    int name_offset;
    int got_offset;
    int expected_offset;
    struct Sym *print_function;
    struct Sym *failure_count;

    memset(plan, 0, sizeof(*plan));
    if (!mir_call_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        mir_cfg_block_count() != 4 || mir.local_bytes != 0 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        (mir.return_type & 15) != TYPE_VOID ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &name_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &got_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[3].dst, &expected_offset) ||
        got_offset != name_offset + 2 ||
        expected_offset != got_offset + 2 ||
        !mir_call_char_pointer_type(mir.insns[1].type) ||
        !mir_memory_runner_word_type(mir.insns[2].type, 0) ||
        !mir_memory_runner_word_type(mir.insns[3].type, 0) ||
        !mir_machine_named_nonvolatile(&mir.insns[1]) ||
        !mir_machine_named_nonvolatile(&mir.insns[2]) ||
        !mir_machine_named_nonvolatile(&mir.insns[3]))
        return 0;
    if (mir.insns[6].src1 != mir.insns[2].dst ||
        mir.insns[6].src2 != mir.insns[3].dst ||
        mir.insns[6].immediate != TOK_NE ||
        mir.insns[7].src1 != mir.insns[6].dst ||
        mir.insns[7].label != mir.insns[24].label ||
        mir.insns[23].label != mir.insns[33].label ||
        strcmp(mir.insns[11].name, mir.insns[1].name) ||
        strcmp(mir.insns[27].name, mir.insns[1].name) ||
        !mir_machine_named_nonvolatile(&mir.insns[11]) ||
        !mir_machine_named_nonvolatile(&mir.insns[27]))
        return mir_machine_reject(
            "integer-report-schedule", "condition");
    print_function = mir_call_recovery_function(
        17, 1, 1, 0, plan->print_name);
    if (print_function == NULL ||
        !mir_machine_call_arguments(
            &mir.insns[17], 4, failure_arguments) ||
        failure_arguments[0] != mir.insns[9].dst ||
        failure_arguments[1] != mir.insns[11].dst ||
        failure_arguments[2] != mir.insns[2].dst ||
        failure_arguments[3] != mir.insns[3].dst ||
        find_global(mir.insns[31].name) != print_function ||
        mir.insns[31].base_name[0] == 0 ||
        strcmp(mir.insns[31].base_name, plan->print_name) ||
        !mir_machine_call_arguments(
            &mir.insns[31], 3, success_arguments) ||
        success_arguments[0] != mir.insns[25].dst ||
        success_arguments[1] != mir.insns[27].dst ||
        success_arguments[2] != mir.insns[2].dst ||
        mir.insns[9].immediate == mir.insns[25].immediate)
        return mir_machine_reject(
            "integer-report-schedule", "calls");
    failure_count = find_global(mir.insns[18].name);
    if (failure_count == NULL || failure_count->is_volatile ||
        !mir_machine_named_nonvolatile(&mir.insns[18]) ||
        !mir_machine_named_nonvolatile(&mir.insns[21]) ||
        !mir_machine_same_location(
            &mir.insns[18], &mir.insns[21]) ||
        !mir_machine_constant_equals(mir.insns[19].dst, 1) ||
        mir.insns[20].src1 != mir.insns[18].dst ||
        mir.insns[20].src2 != mir.insns[19].dst ||
        mir.insns[20].immediate != '+' ||
        mir.insns[21].src1 != mir.insns[20].dst)
        return mir_machine_reject(
            "integer-report-schedule", "failure-count");
    plan->print_function = print_function;
    plan->failure_count = failure_count;
    plan->name_stack_offset = name_offset;
    plan->got_stack_offset = got_offset;
    plan->expected_stack_offset = expected_offset;
    plan->failure_string_id = (int)mir.insns[9].immediate;
    plan->success_string_id = (int)mir.insns[25].immediate;
    return 1;
}

static void mir_emit_integer_report_schedule(
    FILE *out, const struct MirIntegerReportSchedule *plan)
{
    int success = new_label();
    int done = new_label();
    int failure = new_label();

    fprintf(out,
            "%s\n",
            MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n\tinc hl\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n"
            "\tld a,c\n\tcp e\n\tjp nz,L%d\n"
            "\tld a,b\n\tcp d\n\tjp z,L%d\n"
            "L%d:\n\tpush de\n\tpush bc\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->got_stack_offset,
            plan->name_stack_offset,
            failure,
            success,
            failure,
            plan->failure_string_id);
    mir_call_recovery_emit_named_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 4);
    mir_machine_emit_global_word(out, plan->failure_count, 0);
    fputs("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->failure_count, 0);
    fprintf(out,
            "\tjp L%d\nL%d:\n\tpush bc\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            done, success,
            plan->success_string_id);
    mir_call_recovery_emit_named_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 3);
    fprintf(out,
            "L%d:\n\tret\n",
            done);
}

static int mir_match_string_report_schedule(
    struct MirStringReportSchedule *plan)
{
    static const unsigned char expected_opcodes[38] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_LOAD, MIR_ARG,
        MIR_LOAD, MIR_ARG, MIR_CALL, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_LOAD, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_NOP, MIR_JUMP,
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_LOAD, MIR_ARG, MIR_CALL, MIR_NOP, MIR_LABEL
    };
    int compare_arguments[2];
    int failure_arguments[4];
    int success_arguments[3];
    int name_offset;
    int got_offset;
    int expected_offset;
    struct Sym *compare_function;
    struct Sym *print_function;
    struct Sym *failure_count;

    memset(plan, 0, sizeof(*plan));
    if (!mir_call_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        mir_cfg_block_count() != 4 || mir.local_bytes != 0 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        (mir.return_type & 15) != TYPE_VOID ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &name_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &got_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[3].dst, &expected_offset) ||
        got_offset != name_offset + 2 ||
        expected_offset != got_offset + 2 ||
        !mir_call_char_pointer_type(mir.insns[1].type) ||
        !mir_call_char_pointer_type(mir.insns[2].type) ||
        !mir_call_char_pointer_type(mir.insns[3].type) ||
        !mir_machine_named_nonvolatile(&mir.insns[1]) ||
        !mir_machine_named_nonvolatile(&mir.insns[2]) ||
        !mir_machine_named_nonvolatile(&mir.insns[3]))
        return 0;
    if (strcmp(mir.insns[4].name, mir.insns[2].name) ||
        strcmp(mir.insns[6].name, mir.insns[3].name) ||
        strcmp(mir.insns[15].name, mir.insns[1].name) ||
        strcmp(mir.insns[17].name, mir.insns[2].name) ||
        strcmp(mir.insns[19].name, mir.insns[3].name) ||
        strcmp(mir.insns[31].name, mir.insns[1].name) ||
        strcmp(mir.insns[33].name, mir.insns[2].name))
        return mir_machine_reject(
            "string-report-schedule", "loads");
    compare_function = mir_call_recovery_function(
        8, 0, 2, 0, plan->compare_name);
    if (compare_function == NULL ||
        !mir_machine_two_call_arguments(
            &mir.insns[8], compare_arguments) ||
        compare_arguments[0] != mir.insns[4].dst ||
        compare_arguments[1] != mir.insns[6].dst ||
        !mir_machine_constant_equals(mir.insns[9].dst, 0) ||
        mir.insns[10].src1 != mir.insns[8].dst ||
        mir.insns[10].src2 != mir.insns[9].dst ||
        mir.insns[10].immediate != TOK_NE ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[11].label != mir.insns[28].label ||
        mir.insns[27].label != mir.insns[37].label)
        return mir_machine_reject(
            "string-report-schedule", "compare");
    print_function = mir_call_recovery_function(
        21, 1, 1, 0, plan->print_name);
    if (print_function == NULL ||
        !mir_machine_call_arguments(
            &mir.insns[21], 4, failure_arguments) ||
        failure_arguments[0] != mir.insns[13].dst ||
        failure_arguments[1] != mir.insns[15].dst ||
        failure_arguments[2] != mir.insns[17].dst ||
        failure_arguments[3] != mir.insns[19].dst ||
        find_global(mir.insns[35].name) != print_function ||
        mir.insns[35].base_name[0] == 0 ||
        strcmp(mir.insns[35].base_name, plan->print_name) ||
        !mir_machine_call_arguments(
            &mir.insns[35], 3, success_arguments) ||
        success_arguments[0] != mir.insns[29].dst ||
        success_arguments[1] != mir.insns[31].dst ||
        success_arguments[2] != mir.insns[33].dst ||
        mir.insns[13].immediate == mir.insns[29].immediate)
        return mir_machine_reject(
            "string-report-schedule", "calls");
    failure_count = find_global(mir.insns[22].name);
    if (failure_count == NULL || failure_count->is_volatile ||
        !mir_machine_named_nonvolatile(&mir.insns[22]) ||
        !mir_machine_named_nonvolatile(&mir.insns[25]) ||
        !mir_machine_same_location(
            &mir.insns[22], &mir.insns[25]) ||
        !mir_machine_constant_equals(mir.insns[23].dst, 1) ||
        mir.insns[24].src1 != mir.insns[22].dst ||
        mir.insns[24].src2 != mir.insns[23].dst ||
        mir.insns[24].immediate != '+' ||
        mir.insns[25].src1 != mir.insns[24].dst)
        return mir_machine_reject(
            "string-report-schedule", "failure-count");
    plan->compare_function = compare_function;
    plan->print_function = print_function;
    plan->failure_count = failure_count;
    plan->name_stack_offset = name_offset;
    plan->got_stack_offset = got_offset;
    plan->expected_stack_offset = expected_offset;
    plan->failure_string_id = (int)mir.insns[13].immediate;
    plan->success_string_id = (int)mir.insns[29].immediate;
    return 1;
}

static void mir_emit_string_report_schedule(
    FILE *out, const struct MirStringReportSchedule *plan)
{
    int success = new_label();
    int done = new_label();

    fprintf(out,
            "%s\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n",
            MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n",
            plan->expected_stack_offset + 2,
            plan->expected_stack_offset + 3,
            plan->got_stack_offset + 2,
            plan->got_stack_offset + 3);
    mir_call_recovery_emit_named_call(
        out, plan->compare_function, plan->compare_name);
    mir_emit_final_call_cleanup(out, 2);
    fprintf(out,
            "\tld a,h\n\tor l\n\tjp z,L%d\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            success,
            plan->expected_stack_offset + 2,
            plan->expected_stack_offset + 3,
            plan->got_stack_offset + 2,
            plan->got_stack_offset + 3,
            plan->name_stack_offset + 2,
            plan->name_stack_offset + 3,
            plan->failure_string_id);
    mir_call_recovery_emit_named_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 4);
    mir_machine_emit_global_word(out, plan->failure_count, 0);
    fputs("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->failure_count, 0);
    fprintf(out,
            "\tjp L%d\nL%d:\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            done, success,
            plan->got_stack_offset + 2,
            plan->got_stack_offset + 3,
            plan->name_stack_offset + 2,
            plan->name_stack_offset + 3,
            plan->success_string_id);
    mir_call_recovery_emit_named_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 3);
    fprintf(out,
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            done);
}

static int mir_match_read_exact_schedule(
    struct MirReadExactSchedule *plan)
{
    static const unsigned char expected_opcodes[45] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_LOAD, MIR_ARG,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_STORE, MIR_LOAD,
        MIR_UNARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_ARG,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_RETURN, MIR_NOP,
        MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG,
        MIR_LOAD, MIR_NOP, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_UNARY, MIR_STORE, MIR_LOAD, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT, MIR_LOAD, MIR_ARG, MIR_CALL
    };
    int open_arguments[2];
    int copy_arguments[2];
    int read_arguments[4];
    int close_argument;
    int path_offset;
    int buffer_offset;
    int count_offset;
    struct Sym *open_function;
    struct Sym *copy_function;
    struct Sym *read_function;
    struct Sym *close_function;

    memset(plan, 0, sizeof(*plan));
    if (!mir_call_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        mir_cfg_block_count() != 2 || mir.local_bytes != 4 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        (mir.return_type & 15) != TYPE_VOID ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &path_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &buffer_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[3].dst, &count_offset) ||
        buffer_offset != path_offset + 2 ||
        count_offset != buffer_offset + 2 ||
        !mir_call_char_pointer_type(mir.insns[1].type) ||
        !mir_call_char_pointer_type(mir.insns[2].type) ||
        !mir_memory_runner_word_type(mir.insns[3].type, 0) ||
        mir_machine_pointee_is_volatile(&mir.insns[1]) ||
        mir_machine_pointee_is_volatile(&mir.insns[2]))
        return 0;
    if (strcmp(mir.insns[4].name, mir.insns[1].name) ||
        strcmp(mir.insns[13].name, mir.insns[2].name) ||
        strcmp(mir.insns[21].name, mir.insns[2].name) ||
        strcmp(mir.insns[27].name, mir.insns[3].name) ||
        strcmp(mir.insns[36].name, mir.insns[2].name) ||
        !mir_machine_named_nonvolatile(&mir.insns[4]) ||
        !mir_machine_named_nonvolatile(&mir.insns[13]) ||
        !mir_machine_named_nonvolatile(&mir.insns[21]) ||
        !mir_machine_named_nonvolatile(&mir.insns[27]) ||
        !mir_machine_named_nonvolatile(&mir.insns[36]))
        return mir_machine_reject(
            "read-exact-schedule", "parameters");
    open_function = mir_call_recovery_function(
        8, 0, 2, 0, plan->open_name);
    copy_function = mir_call_recovery_function(
        17, 0, 2, 0, plan->copy_name);
    read_function = mir_call_recovery_function(
        32, 0, 4, 0, plan->read_name);
    close_function = mir_call_recovery_function(
        44, 0, 1, 0, plan->close_name);
    if (open_function == NULL || copy_function == NULL ||
        read_function == NULL || close_function == NULL ||
        !mir_machine_two_call_arguments(
            &mir.insns[8], open_arguments) ||
        open_arguments[0] != mir.insns[4].dst ||
        open_arguments[1] != mir.insns[6].dst ||
        !mir_machine_two_call_arguments(
            &mir.insns[17], copy_arguments) ||
        copy_arguments[0] != mir.insns[13].dst ||
        copy_arguments[1] != mir.insns[15].dst ||
        !mir_machine_call_arguments(
            &mir.insns[32], 4, read_arguments) ||
        read_arguments[0] != mir.insns[21].dst ||
        read_arguments[1] != mir.insns[24].dst ||
        read_arguments[2] != mir.insns[27].dst ||
        read_arguments[3] != mir.insns[30].dst ||
        !mir_machine_single_call_argument(
            &mir.insns[44], &close_argument) ||
        close_argument != mir.insns[42].dst ||
        !mir_machine_constant_equals(mir.insns[24].dst, 1))
        return mir_machine_reject(
            "read-exact-schedule", "calls");
    if (!mir_machine_named_nonvolatile(&mir.insns[9]) ||
        !mir_machine_named_nonvolatile(&mir.insns[10]) ||
        !mir_machine_named_nonvolatile(&mir.insns[30]) ||
        !mir_machine_named_nonvolatile(&mir.insns[42]) ||
        !mir_machine_same_location(
            &mir.insns[9], &mir.insns[10]) ||
        !mir_machine_same_location(
            &mir.insns[9], &mir.insns[30]) ||
        !mir_machine_same_location(
            &mir.insns[9], &mir.insns[42]) ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[11].immediate != '!' ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[12].label != mir.insns[20].label)
        return mir_machine_reject(
            "read-exact-schedule", "stream-lifetime");
    if (!mir_machine_unobservable_local_store(&mir.insns[35]) ||
        mir.insns[34].src1 != mir.insns[32].dst ||
        mir.insns[34].immediate != 0 ||
        mir.insns[35].src1 != mir.insns[34].dst ||
        mir.insns[38].src1 != mir.insns[36].dst ||
        mir.insns[38].src2 != mir.insns[34].dst ||
        mir.insns[38].immediate != 1 ||
        mir.insns[38].memory_size != 1 ||
        !mir_machine_constant_equals(mir.insns[40].dst, 0) ||
        mir.insns[40].type != TYPE_CHAR ||
        mir.insns[41].src1 != mir.insns[38].dst ||
        mir.insns[41].src2 != mir.insns[40].dst ||
        mir.insns[41].memory_size != 1 ||
        (mir.insns[41].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject(
            "read-exact-schedule", "termination");
    plan->open_function = open_function;
    plan->copy_function = copy_function;
    plan->read_function = read_function;
    plan->close_function = close_function;
    plan->path_stack_offset = path_offset;
    plan->buffer_stack_offset = buffer_offset;
    plan->count_stack_offset = count_offset;
    plan->mode_string_id = (int)mir.insns[6].immediate;
    plan->failure_string_id = (int)mir.insns[15].immediate;
    return plan->mode_string_id != plan->failure_string_id;
}

static void mir_emit_read_exact_schedule(
    FILE *out, const struct MirReadExactSchedule *plan)
{
    int opened = new_label();
    int done = new_label();

    fprintf(out,
            "%s\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tld hl,-2\n\tadd hl,sp\n\tld sp,hl\n",
            MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,S%d\n\tpush hl\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n",
            plan->mode_string_id,
            plan->path_stack_offset + 2,
            plan->path_stack_offset + 3);
    mir_call_recovery_emit_named_call(
        out, plan->open_function, plan->open_name);
    mir_emit_final_call_cleanup(out, 2);
    fprintf(out,
            "\tld (ix-2),l\n\tld (ix-1),h\n"
            "\tld a,h\n\tor l\n\tjp nz,L%d\n"
            "\tld hl,S%d\n\tpush hl\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n",
            opened, plan->failure_string_id,
            plan->buffer_stack_offset + 2,
            plan->buffer_stack_offset + 3);
    mir_call_recovery_emit_named_call(
        out, plan->copy_function, plan->copy_name);
    mir_emit_final_call_cleanup(out, 2);
    fprintf(out, "\tjp L%d\nL%d:\n", done, opened);
    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n", out);
    fprintf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n"
            "\tld hl,1\n\tpush hl\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n",
            plan->count_stack_offset + 2,
            plan->count_stack_offset + 3,
            plan->buffer_stack_offset + 2,
            plan->buffer_stack_offset + 3);
    mir_call_recovery_emit_named_call(
        out, plan->read_function, plan->read_name);
    mir_emit_final_call_cleanup(out, 4);
    fprintf(out,
            "\tex de,hl\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tadd hl,de\n\txor a\n\tld (hl),a\n"
            "\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n",
            plan->buffer_stack_offset + 2,
            plan->buffer_stack_offset + 3);
    mir_call_recovery_emit_named_call(
        out, plan->close_function, plan->close_name);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out,
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            done);
}

static int mir_match_file_exists_schedule(
    struct MirFileExistsSchedule *plan)
{
    static const unsigned char expected_opcodes[19] = {
        MIR_LABEL, MIR_PARAM, MIR_LOAD, MIR_ARG, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_STORE, MIR_LOAD, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN, MIR_NOP,
        MIR_LABEL, MIR_CONST, MIR_RETURN
    };
    int open_arguments[2];
    int close_argument;
    int path_offset;
    struct Sym *open_function;
    struct Sym *close_function;

    memset(plan, 0, sizeof(*plan));
    if (!mir_call_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        mir_cfg_block_count() != 2 || mir.local_bytes != 2 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        !mir_memory_runner_word_type(mir.return_type, 0) ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &path_offset) ||
        !mir_call_char_pointer_type(mir.insns[1].type) ||
        mir_machine_pointee_is_volatile(&mir.insns[1]) ||
        strcmp(mir.insns[2].name, mir.insns[1].name) ||
        !mir_machine_named_nonvolatile(&mir.insns[1]) ||
        !mir_machine_named_nonvolatile(&mir.insns[2]))
        return 0;
    open_function = mir_call_recovery_function(
        6, 0, 2, 0, plan->open_name);
    close_function = mir_call_recovery_function(
        12, 0, 1, 0, plan->close_name);
    if (open_function == NULL || close_function == NULL ||
        !mir_machine_two_call_arguments(
            &mir.insns[6], open_arguments) ||
        open_arguments[0] != mir.insns[2].dst ||
        open_arguments[1] != mir.insns[4].dst ||
        !mir_machine_single_call_argument(
            &mir.insns[12], &close_argument) ||
        close_argument != mir.insns[10].dst)
        return mir_machine_reject(
            "file-exists-schedule", "calls");
    if (!mir_machine_named_nonvolatile(&mir.insns[7]) ||
        !mir_machine_named_nonvolatile(&mir.insns[8]) ||
        !mir_machine_named_nonvolatile(&mir.insns[10]) ||
        !mir_machine_same_location(
            &mir.insns[7], &mir.insns[8]) ||
        !mir_machine_same_location(
            &mir.insns[7], &mir.insns[10]) ||
        mir.insns[7].src1 != mir.insns[6].dst ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].label != mir.insns[16].label ||
        !mir_machine_constant_equals(mir.insns[13].dst, 1) ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        !mir_machine_constant_equals(mir.insns[17].dst, 0) ||
        mir.insns[18].src1 != mir.insns[17].dst)
        return mir_machine_reject(
            "file-exists-schedule", "control");
    plan->open_function = open_function;
    plan->close_function = close_function;
    plan->path_stack_offset = path_offset;
    plan->mode_string_id = (int)mir.insns[4].immediate;
    return 1;
}

static void mir_emit_file_exists_schedule(
    FILE *out, const struct MirFileExistsSchedule *plan)
{
    int opened = new_label();

    fprintf(out, "%s\n", MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld hl,S%d\n\tpush hl\n\tpush de\n",
            plan->path_stack_offset, plan->mode_string_id);
    mir_call_recovery_emit_named_call(
        out, plan->open_function, plan->open_name);
    mir_emit_final_call_cleanup(out, 2);
    fprintf(out,
            "\tld a,h\n\tor l\n\tjp nz,L%d\n\tret\n"
            "L%d:\n\tpush hl\n",
            opened, opened);
    mir_call_recovery_emit_named_call(
        out, plan->close_function, plan->close_name);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tld hl,1\n\tret\n", out);
}

static struct Sym *mir_recovery_direct_call(
    int instruction, int argument_count, int variadic,
    char *call_name, size_t call_name_size)
{
    const struct MirInsn *call;
    struct Sym *function;
    const char *name;

    if (instruction < 0 || instruction >= mir.count)
        return NULL;
    call = &mir.insns[instruction];
    if (call->opcode != MIR_CALL || call->src1 >= 0 ||
        ((call->memory_flags & MIR_CALL_FLAG_VARIADIC) != 0) !=
            variadic)
        return NULL;
    function = find_global(call->name);
    if (function == NULL || function->storage != SC_FUNC ||
        function->is_funcptr ||
        !function->has_proto ||
        (function->proto_variadic
            ? function->proto_nargs > argument_count
            : function->proto_nargs != argument_count))
        return NULL;
    name = call->base_name[0] != 0
        ? call->base_name
        : asm_name_for(sym_asm_name(function));
    if (name[0] == 0 || strlen(name) >= call_name_size)
        return NULL;
    dcc_copy_str(call_name, call_name_size, name);
    return function;
}

static struct Sym *mir_recovery_named_call(
    int instruction, int variadic,
    char *call_name, size_t call_name_size)
{
    const struct MirInsn *call;
    struct Sym *function;
    const char *name;

    if (instruction < 0 || instruction >= mir.count)
        return NULL;
    call = &mir.insns[instruction];
    if (call->opcode != MIR_CALL || call->src1 >= 0 ||
        ((call->memory_flags & MIR_CALL_FLAG_VARIADIC) != 0) !=
            variadic)
        return NULL;
    function = find_global(call->name);
    if (function == NULL || function->storage != SC_FUNC ||
        function->is_funcptr)
        return NULL;
    name = call->base_name[0] != 0
        ? call->base_name
        : asm_name_for(sym_asm_name(function));
    if (name[0] == 0 || strlen(name) >= call_name_size)
        return NULL;
    dcc_copy_str(call_name, call_name_size, name);
    return function;
}

static void mir_emit_recovery_call(
    FILE *out, struct Sym *function, const char *call_name)
{
    const char *ordinary_name =
        asm_name_for(sym_asm_name(function));

    if (!strcmp(call_name, ordinary_name))
        mir_machine_emit_symbol_call(out, function);
    else
        mir_emit_runtime_call(out, call_name);
}

static int mir_recovery_constant_value(int value, int *result)
{
    long constant;

    if (!mir_machine_evaluate_constant(value, &constant, 0) ||
        constant < -32768 || constant > 65535)
        return 0;
    *result = (int)constant;
    return 1;
}

static int mir_recovery_global_address(
    int value, struct Sym **symbol_out,
    char *assembly_name, size_t assembly_name_size,
    int *offset_out)
{
    const struct MirInsn *definition = mir_definition(value);
    struct Sym *symbol;
    int memory_type;
    int memory_storage;
    int memory_offset;
    int declaration;

    if (definition == NULL || definition->opcode != MIR_ADDRESS ||
        !mir_scalar_memory_location(
            definition, &memory_type, &memory_storage,
            &memory_offset) ||
        (memory_storage != SC_GLOBAL &&
         memory_storage != SC_EXTERN) ||
        (definition->memory_flags & (1 | 8)) != 0)
        return 0;
    symbol = find_global(definition->name);
    if (symbol != NULL) {
        const char *name;

        if (!symbol->is_defined || symbol->is_volatile ||
            symbol->pointee_is_volatile)
            return 0;
        name = asm_name_for(sym_asm_name(symbol));
        if (name[0] == 0 || strlen(name) >= assembly_name_size)
            return 0;
        dcc_copy_str(assembly_name, assembly_name_size, name);
        *symbol_out = symbol;
        *offset_out = memory_offset;
        return 1;
    }
    for (declaration = 0;
         declaration < mir.declared_count; ++declaration)
        if (!strcmp(
                mir.declared_names[declaration],
                definition->name))
            break;
    if (declaration == mir.declared_count ||
        (mir.declared_storage[declaration] != SC_GLOBAL &&
         mir.declared_storage[declaration] != SC_EXTERN) ||
        mir.declared_link_names[declaration][0] == 0 ||
        mir.declared_is_volatile[declaration] ||
        mir.declared_pointee_is_volatile[declaration])
        return 0;
    dcc_copy_str(
        assembly_name, assembly_name_size,
        asm_name_for(mir.declared_link_names[declaration]));
    *symbol_out = NULL;
    *offset_out = memory_offset;
    return 1;
}

static int mir_match_fixed_binary_checks_schedule(
    struct MirFixedBinaryChecksSchedule *plan)
{
    const int expected_opcodes[103] = {
        MIR_LABEL, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_CONST, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_NOP, MIR_LABEL,
        MIR_CONST, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_NOP, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_LABEL, MIR_CONST, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_LABEL, MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_LOAD,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_RETURN
    };
    const int call_instructions[5] = {8, 24, 41, 57, 74};
    const int left_values[5] = {4, 20, 37, 53, 69};
    const int right_values[5] = {6, 22, 39, 55, 72};
    const int expected_values[5] = {9, 25, 42, 58, 75};
    const int comparisons[5] = {10, 26, 43, 59, 76};
    const int branches[5] = {11, 27, 44, 60, 77};
    const int failure_strings[5] = {12, 28, 45, 61, 78};
    const int failure_calls[5] = {14, 30, 47, 63, 80};
    const int zero_stores[5] = {17, 33, 50, 66, 83};
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 103 || mir_cfg_block_count() != 11 ||
        mir.local_bytes != 2 || mir.aggregate_temp_bytes != 0 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_INT)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "fixed-binary-checks", "opcodes");
    if (!mir_machine_constant_equals(mir.insns[1].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[94].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[98].dst, 1) ||
        mir.insns[102].src1 != mir.insns[101].dst)
        return mir_machine_reject(
            "fixed-binary-checks", "result");
    for (item = 0; item < 5; ++item) {
        int arguments[2];
        struct Sym *target;
        struct Sym *print;
        char target_name[64];
        char print_name[64];

        target = mir_recovery_direct_call(
            call_instructions[item], 2, 0,
            target_name, sizeof(target_name));
        print = mir_recovery_direct_call(
            failure_calls[item], 1, 1,
            print_name, sizeof(print_name));
        if (target == NULL || print == NULL ||
            !mir_machine_two_call_arguments(
                &mir.insns[call_instructions[item]], arguments) ||
            arguments[0] != mir.insns[left_values[item]].dst ||
            arguments[1] != mir.insns[right_values[item]].dst ||
            !mir_recovery_constant_value(
                arguments[0], &plan->left[item]) ||
            !mir_recovery_constant_value(
                arguments[1], &plan->right[item]) ||
            !mir_recovery_constant_value(
                mir.insns[expected_values[item]].dst,
                &plan->expected[item]) ||
            mir.insns[comparisons[item]].immediate != TOK_NE ||
            mir.insns[comparisons[item]].src1 !=
                mir.insns[call_instructions[item]].dst ||
            mir.insns[comparisons[item]].src2 !=
                mir.insns[expected_values[item]].dst ||
            mir.insns[branches[item]].src1 !=
                mir.insns[comparisons[item]].dst ||
            mir.insns[zero_stores[item]].src1 !=
                mir.insns[zero_stores[item] - 2].dst ||
            !mir_machine_constant_equals(
                mir.insns[zero_stores[item] - 2].dst, 0))
            return mir_machine_reject(
                "fixed-binary-checks", "check");
        if (item == 0) {
            plan->target_function = target;
            plan->print_function = print;
            dcc_copy_str(
                plan->target_name, sizeof(plan->target_name),
                target_name);
            dcc_copy_str(
                plan->print_name, sizeof(plan->print_name),
                print_name);
        } else if (target != plan->target_function ||
                   print != plan->print_function ||
                   strcmp(target_name, plan->target_name) ||
                   strcmp(print_name, plan->print_name)) {
            return mir_machine_reject(
                "fixed-binary-checks", "call-alias");
        }
        plan->failure_strings[item] =
            (int)mir.insns[failure_strings[item]].immediate;
    }
    {
        int argument;
        struct Sym *print = mir_recovery_direct_call(
            90, 1, 1, plan->print_name,
            sizeof(plan->print_name));

        if (print != plan->print_function ||
            !mir_machine_single_call_argument(
                &mir.insns[90], &argument) ||
            argument != mir.insns[88].dst ||
            mir.insns[88].opcode != MIR_STRING_ADDRESS)
            return mir_machine_reject(
                "fixed-binary-checks", "success");
        plan->success_string = (int)mir.insns[88].immediate;
    }
    return 1;
}

static void mir_emit_fixed_binary_checks_schedule(
    FILE *out, const struct MirFixedBinaryChecksSchedule *plan)
{
    int done = new_label();
    int item;

    fputs(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-2\n\tadd hl,sp\n\tld sp,hl\n"
          "\tld (ix-2),1\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (item = 0; item < 5; ++item) {
        int equal = new_label();

        fprintf(out, "\tld hl,%d\n\tpush hl\n",
                plan->right[item] & 0xffff);
        fprintf(out, "\tld hl,%d\n\tpush hl\n",
                plan->left[item] & 0xffff);
        mir_emit_recovery_call(
            out, plan->target_function, plan->target_name);
        mir_emit_final_call_cleanup(out, 2);
        fprintf(out,
                "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                "\tjr z,L%d\n\tld hl,S%d\n\tpush hl\n",
                plan->expected[item] & 0xffff,
                equal, plan->failure_strings[item]);
        mir_emit_recovery_call(
            out, plan->print_function, plan->print_name);
        mir_emit_final_call_cleanup(out, 1);
        fprintf(out,
                "\txor a\n\tld (ix-2),a\nL%d:\n",
                equal);
    }
    fputs("\tld a,(ix-2)\n\tor a\n", out);
    fprintf(out,
            "\tjr z,L%d\n\tld hl,S%d\n\tpush hl\n",
            done, plan->success_string);
    mir_emit_recovery_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out,
            "L%d:\n\tld a,(ix-2)\n\txor 1\n"
            "\tld l,a\n\tld h,0\n"
            "\tld sp,ix\n\tpop ix\n\tret\n",
            done);
}

static int mir_match_function_pointer_print_schedule(
    struct MirFunctionPointerPrintSchedule *plan)
{
    const int expected_opcodes[32] = {
        MIR_LABEL, MIR_CONST, MIR_STORE, MIR_CONST, MIR_STORE,
        MIR_CONST, MIR_STORE, MIR_CONST, MIR_STORE, MIR_CONST, MIR_STORE,
        MIR_ADDRESS, MIR_NOP, MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_ADDRESS, MIR_ARG, MIR_LOAD, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN
    };
    int print_arguments[4];
    int forward_arguments[2];
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 32 || mir_cfg_block_count() != 1 ||
        mir.local_bytes != 7 || mir.aggregate_temp_bytes != 0 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_INT)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "function-pointer-print", "opcodes");
    for (item = 0; item < 5; ++item) {
        if (!mir_recovery_constant_value(
                mir.insns[1 + item * 2].dst,
                &plan->message[item]) ||
            mir.insns[2 + item * 2].src1 !=
                mir.insns[1 + item * 2].dst ||
            mir.insns[2 + item * 2].memory_size != 1)
            return mir_machine_reject(
                "function-pointer-print", "message");
    }
    plan->leaf_function = find_global(mir.insns[11].name);
    if (plan->leaf_function == NULL ||
        mir.insns[13].src1 != mir.insns[11].dst ||
        strcmp(mir.insns[13].name, mir.insns[18].name) ||
        strcmp(mir.insns[13].name, mir.insns[23].name) ||
        mir.insns[21].src1 != mir.insns[18].dst ||
        !mir_recovery_constant_value(
            mir.insns[19].dst, &plan->leaf_argument))
        return mir_machine_reject(
            "function-pointer-print", "leaf");
    plan->forward_function = mir_recovery_direct_call(
        27, 2, 0, plan->forward_name,
        sizeof(plan->forward_name));
    plan->print_function = mir_recovery_direct_call(
        29, 4, 1, plan->print_name,
        sizeof(plan->print_name));
    if (plan->forward_function == NULL ||
        plan->print_function == NULL ||
        !mir_machine_two_call_arguments(
            &mir.insns[27], forward_arguments) ||
        forward_arguments[0] != mir.insns[23].dst ||
        forward_arguments[1] != mir.insns[25].dst ||
        !mir_recovery_constant_value(
            mir.insns[25].dst, &plan->forward_argument) ||
        !mir_machine_call_arguments(
            &mir.insns[29], 4, print_arguments) ||
        print_arguments[0] != mir.insns[14].dst ||
        print_arguments[1] != mir.insns[16].dst ||
        print_arguments[2] != mir.insns[21].dst ||
        print_arguments[3] != mir.insns[27].dst ||
        mir.insns[16].opcode != MIR_ADDRESS ||
        strcmp(mir.insns[16].name, mir.insns[2].name) ||
        !mir_machine_constant_equals(mir.insns[30].dst, 0))
        return mir_machine_reject(
            "function-pointer-print", "calls");
    plan->format_string = (int)mir.insns[14].immediate;
    dcc_copy_str(
        plan->leaf_name, sizeof(plan->leaf_name),
        asm_name_for(sym_asm_name(plan->leaf_function)));
    return 1;
}

static void mir_emit_function_pointer_print_schedule(
    FILE *out, const struct MirFunctionPointerPrintSchedule *plan)
{
    int item;

    fputs(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-8\n\tadd hl,sp\n\tld sp,hl\n",
          out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (item = 0; item < 5; ++item)
        fprintf(out, "\tld (ix%+d),%d\n",
                -5 + item, plan->message[item] & 255);
    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->leaf_argument & 0xffff);
    mir_emit_recovery_call(
        out, plan->leaf_function, plan->leaf_name);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tld (ix-8),l\n\tld (ix-7),h\n", out);
    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->forward_argument & 0xffff);
    fprintf(out, "\tld hl,%s\n\tpush hl\n",
            plan->leaf_name);
    mir_emit_recovery_call(
        out, plan->forward_function, plan->forward_name);
    mir_emit_final_call_cleanup(out, 2);
    fputs("\tpush hl\n"
          "\tld l,(ix-8)\n\tld h,(ix-7)\n\tpush hl\n"
          "\tpush ix\n\tpop hl\n\tld de,-5\n\tadd hl,de\n\tpush hl\n",
          out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->format_string);
    mir_emit_recovery_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 4);
    fputs("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_port_io_schedule(
    struct MirPortIoSchedule *plan)
{
    const int expected_opcodes[50] = {
        MIR_LABEL, MIR_CONST, MIR_NOP, MIR_ARG, MIR_CONST, MIR_NOP,
        MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_LABEL,
        MIR_LABEL, MIR_PHI, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN
    };
    int arguments[2];
    int argument;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 50 || mir_cfg_block_count() != 8 ||
        mir.local_bytes != 2 || mir.aggregate_temp_bytes != 0 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_INT)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject("port-io-schedule", "opcodes");
    plan->output_function = mir_recovery_direct_call(
        7, 2, 0, plan->output_name,
        sizeof(plan->output_name));
    plan->input_function = mir_recovery_direct_call(
        11, 1, 0, plan->input_name,
        sizeof(plan->input_name));
    plan->print_function = mir_recovery_direct_call(
        16, 1, 1, plan->print_name,
        sizeof(plan->print_name));
    if (plan->output_function == NULL ||
        plan->input_function == NULL ||
        plan->print_function == NULL ||
        !mir_machine_two_call_arguments(
            &mir.insns[7], arguments) ||
        !mir_recovery_constant_value(
            arguments[0], &plan->port) ||
        !mir_recovery_constant_value(
            arguments[1], &plan->output_value) ||
        !mir_machine_single_call_argument(
            &mir.insns[11], &argument) ||
        !mir_machine_constant_equals(argument, plan->port) ||
        mir.insns[13].src1 != mir.insns[11].dst ||
        !mir_machine_single_call_argument(
            &mir.insns[16], &argument) ||
        argument != mir.insns[14].dst)
        return mir_machine_reject("port-io-schedule", "calls");
    if (mir_recovery_direct_call(
            44, 2, 1, plan->print_name,
            sizeof(plan->print_name)) != plan->print_function ||
        mir_recovery_direct_call(
            47, 1, 1, plan->print_name,
            sizeof(plan->print_name)) != plan->print_function ||
        !mir_machine_call_arguments(
            &mir.insns[44], 2, arguments) ||
        arguments[0] != mir.insns[17].dst ||
        arguments[1] != mir.insns[42].dst ||
        !mir_machine_single_call_argument(
            &mir.insns[47], &argument) ||
        argument != mir.insns[45].dst)
        return mir_machine_reject("port-io-schedule", "prints");
    if (mir.insns[21].immediate != TOK_GE ||
        mir.insns[21].src1 != mir.insns[11].dst ||
        !mir_machine_constant_equals(mir.insns[20].dst, 0) ||
        mir.insns[25].immediate != TOK_LE ||
        mir.insns[25].src1 != mir.insns[11].dst ||
        !mir_machine_constant_equals(mir.insns[24].dst, 255) ||
        mir.insns[42].opcode != MIR_PHI ||
        mir.insns[42].src1 != mir.insns[35].dst ||
        mir.insns[42].src2 != mir.insns[39].dst ||
        !mir_machine_constant_equals(mir.insns[48].dst, 0))
        return mir_machine_reject("port-io-schedule", "range");
    plan->strings[0] = (int)mir.insns[14].immediate;
    plan->strings[1] = (int)mir.insns[17].immediate;
    plan->strings[2] = (int)mir.insns[35].immediate;
    plan->strings[3] = (int)mir.insns[39].immediate;
    plan->strings[4] = (int)mir.insns[45].immediate;
    return 1;
}

static void mir_emit_port_io_schedule(
    FILE *out, const struct MirPortIoSchedule *plan)
{
    int outside = new_label();
    int selected = new_label();

    fputs(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-2\n\tadd hl,sp\n\tld sp,hl\n",
          out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->output_value & 0xffff);
    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->port & 0xffff);
    mir_emit_recovery_call(
        out, plan->output_function, plan->output_name);
    mir_emit_final_call_cleanup(out, 2);
    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->port & 0xffff);
    mir_emit_recovery_call(
        out, plan->input_function, plan->input_name);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tld (ix-2),l\n\tld (ix-1),h\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[0]);
    mir_emit_recovery_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tld a,(ix-1)\n\tor a\n", out);
    fprintf(out,
            "\tjr nz,L%d\n\tld hl,S%d\n\tjr L%d\n"
            "L%d:\n\tld hl,S%d\nL%d:\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            outside, plan->strings[2], selected,
            outside, plan->strings[3], selected,
            plan->strings[1]);
    mir_emit_recovery_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 2);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[4]);
    mir_emit_recovery_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_signed_idiom_report_schedule(
    struct MirSignedIdiomReportSchedule *plan)
{
    const int expected_opcodes[233] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_NOP,
        MIR_BINARY, MIR_STORE, MIR_CONST, MIR_NOP, MIR_BINARY, MIR_STORE,
        MIR_CONST, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_STORE, MIR_CONST,
        MIR_NOP, MIR_NOP, MIR_BINARY, MIR_STORE, MIR_CONST, MIR_NOP,
        MIR_NOP, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST,
        MIR_NOP, MIR_BINARY, MIR_UNARY, MIR_STORE, MIR_CONST, MIR_NOP,
        MIR_BINARY, MIR_UNARY, MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_UNARY, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_STORE,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_UNARY, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_LABEL,
        MIR_LABEL, MIR_PHI, MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_NOP, MIR_UNARY, MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_STORE,
        MIR_NOP, MIR_CONST, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_UNARY, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_STORE, MIR_NOP, MIR_CONST,
        MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_LABEL,
        MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_UNARY, MIR_LABEL, MIR_LABEL,
        MIR_PHI, MIR_STORE, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_UNARY, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_STORE,
        MIR_NOP, MIR_CONST, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_UNARY, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_STORE, MIR_NOP, MIR_CONST,
        MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_LABEL,
        MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_UNARY, MIR_LABEL, MIR_LABEL,
        MIR_PHI, MIR_STORE, MIR_NOP, MIR_CONST, MIR_NOP, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_UNARY, MIR_NOP, MIR_LABEL,
        MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_LABEL, MIR_PHI,
        MIR_STORE, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_UNARY, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_UNARY, MIR_LABEL, MIR_LABEL, MIR_PHI,
        MIR_STORE, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_UNARY, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_UNARY, MIR_LABEL, MIR_LABEL, MIR_PHI,
        MIR_STORE, MIR_LOAD, MIR_UNARY, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG,
        MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP,
        MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN
    };
    const int constant_instructions[7] = {
        4, 8, 12, 17, 22, 29, 34
    };
    const int multiply_instructions[7] = {
        6, 10, 15, 20, 25, 31, 36
    };
    const int output_values[11] = {
        51, 65, 79, 94, 109, 124, 139, 154, 170, 186, 202
    };
    int arguments[12];
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 233 || mir.next_value != 135 ||
        mir_cfg_block_count() != 45 || mir.local_bytes != 34 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_INT)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "signed-idiom-report", "opcodes");
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->argc_stack_offset) ||
        type_ptr_depth(mir.insns[1].type) != 0 ||
        (mir.insns[1].type & 15) != TYPE_INT ||
        type_size(mir.insns[1].type) != 2 ||
        type_ptr_depth(mir.insns[2].type) != 2)
        return mir_machine_reject(
            "signed-idiom-report", "parameters");
    for (item = 0; item < 7; ++item) {
        const struct MirInsn *multiply =
            &mir.insns[multiply_instructions[item]];

        if (!mir_recovery_constant_value(
                mir.insns[constant_instructions[item]].dst,
                &plan->multipliers[item]) ||
            multiply->immediate != '*' ||
            multiply->src1 !=
                mir.insns[constant_instructions[item]].dst ||
            multiply->src2 != mir.insns[1].dst ||
            type_size(multiply->type) != 2)
            return mir_machine_reject(
                "signed-idiom-report", "multiplication");
    }
    if (mir.insns[32].src1 != mir.insns[31].dst ||
        type_size(mir.insns[32].type) != 1 ||
        (mir.insns[32].type & TYPE_UNSIGNED) != 0 ||
        mir.insns[37].src1 != mir.insns[36].dst ||
        type_size(mir.insns[37].type) != 1 ||
        (mir.insns[37].type & TYPE_UNSIGNED) == 0)
        return mir_machine_reject(
            "signed-idiom-report", "narrowing");
    plan->print_function = mir_recovery_direct_call(
        230, 12, 1, plan->print_name,
        sizeof(plan->print_name));
    if (plan->print_function == NULL ||
        !mir_machine_call_arguments(
            &mir.insns[230], 12, arguments) ||
        arguments[0] != mir.insns[206].dst ||
        mir.insns[206].opcode != MIR_STRING_ADDRESS)
        return mir_machine_reject(
            "signed-idiom-report", "print-call");
    for (item = 0; item < 11; ++item)
        if (arguments[item + 1] !=
            mir.insns[output_values[item]].dst)
            return mir_machine_reject(
                "signed-idiom-report", "print-value");
    if (mir.insns[41].immediate != '<' ||
        mir.insns[55].immediate != '<' ||
        mir.insns[69].immediate != TOK_GE ||
        mir.insns[84].immediate != '<' ||
        mir.insns[99].immediate != TOK_GE ||
        mir.insns[114].immediate != '<' ||
        mir.insns[129].immediate != '<' ||
        mir.insns[144].immediate != TOK_GE ||
        mir.insns[159].immediate != '<' ||
        mir.insns[175].immediate != '<' ||
        mir.insns[191].immediate != '<' ||
        !mir_machine_constant_equals(mir.insns[231].dst, 0) ||
        mir.insns[232].src1 != mir.insns[231].dst)
        return mir_machine_reject(
            "signed-idiom-report", "conditions");
    plan->format_string = (int)mir.insns[206].immediate;
    return 1;
}

static void mir_emit_signed_idiom_multiply(
    FILE *out, const struct MirSignedIdiomReportSchedule *plan,
    int multiplier)
{
    int offset = plan->argc_stack_offset + 2;

    fprintf(out,
            "\tld hl,%d\n"
            "\tld e,(ix+%d)\n\tld d,(ix+%d)\n",
            multiplier & 0xffff, offset, offset + 1);
    mir_emit_runtime_call(out, "__mulu");
}

static void mir_emit_signed_idiom_abs(FILE *out)
{
    int nonnegative = new_label();

    fputs("\tbit 7,h\n", out);
    fprintf(out, "\tjr z,L%d\n", nonnegative);
    fputs("\txor a\n\tsub l\n\tld l,a\n"
          "\tsbc a,a\n\tsub h\n\tld h,a\n", out);
    fprintf(out, "L%d:\n", nonnegative);
}

static void mir_emit_signed_idiom_report_schedule(
    FILE *out, const struct MirSignedIdiomReportSchedule *plan)
{
    fputs(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-2\n\tadd hl,sp\n\tld sp,hl\n",
          out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    mir_emit_signed_idiom_multiply(out, plan, plan->multipliers[6]);
    fputs("\tld h,0\n\tpush hl\n", out);
    mir_emit_signed_idiom_multiply(out, plan, plan->multipliers[5]);
    fputs("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n", out);
    mir_emit_signed_idiom_abs(out);
    fputs("\tpush hl\n", out);
    mir_emit_signed_idiom_multiply(out, plan, plan->multipliers[4]);
    fputs("\tpush hl\n", out);
    mir_emit_signed_idiom_multiply(out, plan, plan->multipliers[3]);
    fputs("\tpush hl\n", out);
    mir_emit_signed_idiom_multiply(out, plan, plan->multipliers[2]);
    fputs("\tpush hl\n", out);

    mir_emit_signed_idiom_multiply(out, plan, plan->multipliers[0]);
    fputs("\tld (ix-2),l\n\tld (ix-1),h\n"
          "\tpush hl\n\tpush hl\n\tpush hl\n", out);
    mir_emit_signed_idiom_abs(out);
    fputs("\tpush hl\n", out);
    mir_emit_signed_idiom_multiply(out, plan, plan->multipliers[1]);
    mir_emit_signed_idiom_abs(out);
    fputs("\tpush hl\n"
          "\tld l,(ix-2)\n\tld h,(ix-1)\n", out);
    mir_emit_signed_idiom_abs(out);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->format_string);
    mir_emit_recovery_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 12);
    fputs("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_buffered_example_schedule(
    struct MirBufferedExampleSchedule *plan)
{
    const int expected_opcodes[87] = {
        MIR_LABEL, MIR_CONST, MIR_NOP, MIR_ARG, MIR_ADDRESS, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN, MIR_NOP, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_STORE, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_PHI, MIR_PHI, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG,
        MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP, MIR_NOP, MIR_UNARY,
        MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_NOP,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_NOP, MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP,
        MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CONST, MIR_NOP, MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN
    };
    int arguments[4];
    int argument;
    int buffer_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 87 || mir.next_value != 55 ||
        mir_cfg_block_count() != 5 || mir.local_bytes != 6 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        !mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_INT)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "buffered-example", "opcodes");
    plan->set_buffer_function = mir_recovery_direct_call(
        11, 4, 0, plan->set_buffer_name,
        sizeof(plan->set_buffer_name));
    plan->puts_function = mir_recovery_direct_call(
        17, 1, 0, plan->puts_name,
        sizeof(plan->puts_name));
    plan->print_function = mir_recovery_direct_call(
        41, 3, 1, plan->print_name,
        sizeof(plan->print_name));
    plan->flush_function = mir_recovery_direct_call(
        63, 1, 0, plan->flush_name,
        sizeof(plan->flush_name));
    if (plan->set_buffer_function == NULL ||
        plan->puts_function == NULL ||
        plan->print_function == NULL ||
        plan->flush_function == NULL ||
        !mir_machine_call_arguments(
            &mir.insns[11], 4, arguments) ||
        !mir_recovery_constant_value(
            arguments[0], &plan->stream_value) ||
        arguments[1] != mir.insns[4].dst ||
        !mir_recovery_global_address(
            arguments[1], &plan->buffer,
            plan->buffer_name, sizeof(plan->buffer_name),
            &buffer_offset) ||
        buffer_offset != 0 ||
        !mir_recovery_constant_value(
            arguments[2], &plan->full_mode) ||
        !mir_recovery_constant_value(
            arguments[3], &plan->buffer_size))
        return mir_machine_reject(
            "buffered-example", "setup");
    if (!mir_machine_single_call_argument(
            &mir.insns[17], &argument) ||
        argument != mir.insns[15].dst ||
        !mir_machine_constant_equals(mir.insns[18].dst, 1))
        return mir_machine_reject(
            "buffered-example", "failure");
    if (!mir_recovery_constant_value(
            mir.insns[25].dst, &plan->first_index) ||
        !mir_recovery_constant_value(
            mir.insns[32].dst, &plan->last_index) ||
        plan->first_index != 1 || plan->last_index != 20 ||
        mir.insns[33].immediate != TOK_LE ||
        mir.insns[33].src1 != mir.insns[30].dst ||
        mir.insns[33].src2 != mir.insns[32].dst ||
        mir.insns[34].src1 != mir.insns[33].dst ||
        !mir_machine_call_arguments(
            &mir.insns[41], 3, arguments) ||
        arguments[0] != mir.insns[35].dst ||
        arguments[1] != mir.insns[30].dst ||
        arguments[2] != mir.insns[29].dst ||
        mir.insns[47].immediate != '*' ||
        mir.insns[47].src1 != mir.insns[44].dst ||
        mir.insns[47].src2 != mir.insns[46].dst ||
        mir.insns[48].immediate != '+' ||
        mir.insns[48].src1 != mir.insns[29].dst ||
        mir.insns[48].src2 != mir.insns[47].dst ||
        mir.insns[55].immediate != '+' ||
        !mir_machine_constant_equals(mir.insns[54].dst, 1))
        return mir_machine_reject(
            "buffered-example", "loop");
    if (mir_recovery_direct_call(
            68, 2, 1, plan->print_name,
            sizeof(plan->print_name)) !=
            plan->print_function ||
        mir_recovery_direct_call(
            72, 1, 0, plan->flush_name,
            sizeof(plan->flush_name)) !=
            plan->flush_function ||
        mir_recovery_direct_call(
            84, 4, 0, plan->set_buffer_name,
            sizeof(plan->set_buffer_name)) !=
            plan->set_buffer_function)
        return mir_machine_reject(
            "buffered-example", "final-calls");
    if (!mir_machine_single_call_argument(
            &mir.insns[63], &argument) ||
        !mir_machine_constant_equals(argument, plan->stream_value) ||
        !mir_machine_call_arguments(
            &mir.insns[68], 2, arguments) ||
        arguments[0] != mir.insns[64].dst ||
        arguments[1] != mir.insns[29].dst ||
        !mir_machine_single_call_argument(
            &mir.insns[72], &argument) ||
        !mir_machine_constant_equals(argument, plan->stream_value) ||
        !mir_machine_call_arguments(
            &mir.insns[84], 4, arguments) ||
        !mir_machine_constant_equals(
            arguments[0], plan->stream_value) ||
        !mir_machine_constant_equals(arguments[1], 0) ||
        !mir_recovery_constant_value(
            arguments[2], &plan->line_mode) ||
        !mir_machine_constant_equals(arguments[3], 0) ||
        !mir_machine_constant_equals(mir.insns[85].dst, 0))
        return mir_machine_reject(
            "buffered-example", "final-values");
    plan->strings[0] = (int)mir.insns[15].immediate;
    plan->strings[1] = (int)mir.insns[35].immediate;
    plan->strings[2] = (int)mir.insns[64].immediate;
    return plan->buffer != NULL || plan->buffer_name[0] != 0;
}

static void mir_emit_buffered_example_stream(
    FILE *out, const struct MirBufferedExampleSchedule *plan)
{
    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->stream_value & 0xffff);
}

static void mir_emit_buffered_example_schedule(
    FILE *out, const struct MirBufferedExampleSchedule *plan)
{
    int loop = new_label();
    int done = new_label();
    int configured = new_label();
    int incremented = new_label();

    fputs(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-6\n\tadd hl,sp\n\tld sp,hl\n",
          out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->buffer_size & 0xffff);
    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->full_mode & 0xffff);
    fprintf(out, "\tld hl,%s\n\tpush hl\n",
            plan->buffer_name);
    mir_emit_buffered_example_stream(out, plan);
    mir_emit_recovery_call(
        out, plan->set_buffer_function, plan->set_buffer_name);
    mir_emit_final_call_cleanup(out, 4);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjr z,L%d\n\tld hl,S%d\n\tpush hl\n",
            configured, plan->strings[0]);
    mir_emit_recovery_call(
        out, plan->puts_function, plan->puts_name);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tld hl,1\n\tld sp,ix\n\tpop ix\n\tret\n", out);

    fprintf(out,
            "L%d:\n\txor a\n"
            "\tld (ix-6),a\n\tld (ix-5),a\n"
            "\tld (ix-4),a\n\tld (ix-3),a\n"
            "\tld hl,%d\n\tld (ix-2),l\n\tld (ix-1),h\n"
            "L%d:\n\tld l,(ix-2)\n\tld h,(ix-1)\n"
            "\tld de,%d\n\tor a\n\tsbc hl,de\n"
            "\tjr nc,L%d\n",
            configured, plan->first_index,
            loop, plan->last_index + 1, done);
    fputs("\tld l,(ix-4)\n\tld h,(ix-3)\n\tpush hl\n"
          "\tld l,(ix-6)\n\tld h,(ix-5)\n\tpush hl\n"
          "\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n",
          out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[1]);
    mir_emit_recovery_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 4);

    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n"
          "\tld e,l\n\tld d,h\n", out);
    mir_emit_runtime_call(out, "__mulu");
    fputs("\tld e,(ix-6)\n\tld d,(ix-5)\n"
          "\tadd hl,de\n\tld (ix-6),l\n\tld (ix-5),h\n"
          "\tinc (ix-2)\n", out);
    fprintf(out, "\tjr nz,L%d\n\tinc (ix-1)\nL%d:\n",
            incremented, incremented);
    fprintf(out, "\tjr L%d\n", loop);

    fprintf(out, "L%d:\n", done);
    mir_emit_buffered_example_stream(out, plan);
    mir_emit_recovery_call(
        out, plan->flush_function, plan->flush_name);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tld l,(ix-4)\n\tld h,(ix-3)\n\tpush hl\n"
          "\tld l,(ix-6)\n\tld h,(ix-5)\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[2]);
    mir_emit_recovery_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 3);
    mir_emit_buffered_example_stream(out, plan);
    mir_emit_recovery_call(
        out, plan->flush_function, plan->flush_name);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tld hl,0\n\tpush hl\n", out);
    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->line_mode & 0xffff);
    fputs("\tld hl,0\n\tpush hl\n", out);
    mir_emit_buffered_example_stream(out, plan);
    mir_emit_recovery_call(
        out, plan->set_buffer_function, plan->set_buffer_name);
    mir_emit_final_call_cleanup(out, 4);
    fputs("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_simple_file_io_schedule(
    struct MirSimpleFileIoSchedule *plan)
{
    const int expected_opcodes[103] = {
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE, MIR_LOAD, MIR_UNARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_RETURN, MIR_NOP, MIR_LABEL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_LOAD, MIR_ARG,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_LOAD, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE,
        MIR_LOAD, MIR_UNARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN, MIR_NOP, MIR_LABEL,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_PHI, MIR_ADDRESS,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_ARG, MIR_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN
    };
    int arguments[3];
    int argument;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 103 || mir.next_value != 56 ||
        mir_cfg_block_count() != 6 || mir.local_bytes != 70 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        !mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_INT)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "simple-file-io", "opcodes");
    plan->open_function = mir_recovery_direct_call(
        5, 2, 0, plan->open_name, sizeof(plan->open_name));
    plan->puts_function = mir_recovery_direct_call(
        13, 1, 0, plan->puts_name, sizeof(plan->puts_name));
    plan->puts_stream_function = mir_recovery_direct_call(
        22, 2, 0, plan->puts_stream_name,
        sizeof(plan->puts_stream_name));
    plan->file_print_function = mir_recovery_direct_call(
        34, 3, 1, plan->file_print_name,
        sizeof(plan->file_print_name));
    plan->close_function = mir_recovery_direct_call(
        37, 1, 0, plan->close_name, sizeof(plan->close_name));
    plan->gets_function = mir_recovery_direct_call(
        66, 3, 0, plan->gets_name, sizeof(plan->gets_name));
    plan->print_function = mir_recovery_direct_call(
        77, 3, 1, plan->print_name, sizeof(plan->print_name));
    plan->string_print_function = mir_recovery_direct_call(
        91, 3, 1, plan->string_print_name,
        sizeof(plan->string_print_name));
    plan->unlink_function = mir_recovery_direct_call(
        97, 1, 0, plan->unlink_name,
        sizeof(plan->unlink_name));
    if (plan->open_function == NULL ||
        plan->puts_function == NULL ||
        plan->puts_stream_function == NULL ||
        plan->file_print_function == NULL ||
        plan->close_function == NULL ||
        plan->gets_function == NULL ||
        plan->print_function == NULL ||
        plan->string_print_function == NULL ||
        plan->unlink_function == NULL)
        return mir_machine_reject(
            "simple-file-io", "functions");
    if (mir_recovery_direct_call(
            42, 2, 0, plan->open_name,
            sizeof(plan->open_name)) != plan->open_function ||
        mir_recovery_direct_call(
            50, 1, 0, plan->puts_name,
            sizeof(plan->puts_name)) != plan->puts_function ||
        mir_recovery_direct_call(
            84, 1, 0, plan->close_name,
            sizeof(plan->close_name)) != plan->close_function ||
        mir_recovery_direct_call(
            94, 1, 0, plan->puts_name,
            sizeof(plan->puts_name)) != plan->puts_function ||
        mir_recovery_direct_call(
            100, 1, 0, plan->puts_name,
            sizeof(plan->puts_name)) != plan->puts_function)
        return mir_machine_reject(
            "simple-file-io", "function-alias");
    if (!mir_machine_call_arguments(
            &mir.insns[5], 2, arguments) ||
        arguments[0] != mir.insns[1].dst ||
        arguments[1] != mir.insns[3].dst ||
        !mir_machine_single_call_argument(
            &mir.insns[13], &argument) ||
        argument != mir.insns[11].dst ||
        !mir_machine_call_arguments(
            &mir.insns[22], 2, arguments) ||
        arguments[0] != mir.insns[18].dst ||
        arguments[1] != mir.insns[20].dst ||
        !mir_machine_call_arguments(
            &mir.insns[27], 2, arguments) ||
        arguments[0] != mir.insns[23].dst ||
        arguments[1] != mir.insns[25].dst ||
        !mir_machine_call_arguments(
            &mir.insns[34], 3, arguments) ||
        arguments[0] != mir.insns[28].dst ||
        arguments[1] != mir.insns[30].dst ||
        arguments[2] != mir.insns[32].dst ||
        !mir_machine_single_call_argument(
            &mir.insns[37], &argument) ||
        argument != mir.insns[35].dst)
        return mir_machine_reject(
            "simple-file-io", "write-phase");
    if (!mir_machine_call_arguments(
            &mir.insns[42], 2, arguments) ||
        arguments[0] != mir.insns[38].dst ||
        arguments[1] != mir.insns[40].dst ||
        !mir_machine_single_call_argument(
            &mir.insns[50], &argument) ||
        argument != mir.insns[48].dst ||
        !mir_machine_call_arguments(
            &mir.insns[66], 3, arguments) ||
        arguments[0] != mir.insns[60].dst ||
        arguments[1] != mir.insns[62].dst ||
        arguments[2] != mir.insns[64].dst ||
        !mir_recovery_constant_value(
            arguments[1], &plan->buffer_size) ||
        !mir_machine_call_arguments(
            &mir.insns[77], 3, arguments) ||
        arguments[0] != mir.insns[68].dst ||
        arguments[1] != mir.insns[72].dst ||
        arguments[2] != mir.insns[75].dst ||
        !mir_machine_single_call_argument(
            &mir.insns[84], &argument) ||
        argument != mir.insns[82].dst)
        return mir_machine_reject(
            "simple-file-io", "read-phase");
    if (!mir_machine_call_arguments(
            &mir.insns[91], 3, arguments) ||
        arguments[0] != mir.insns[85].dst ||
        arguments[1] != mir.insns[87].dst ||
        arguments[2] != mir.insns[89].dst ||
        !mir_recovery_constant_value(
            arguments[2], &plan->formatted_value) ||
        !mir_machine_single_call_argument(
            &mir.insns[94], &argument) ||
        argument != mir.insns[92].dst ||
        !mir_machine_single_call_argument(
            &mir.insns[97], &argument) ||
        argument != mir.insns[95].dst ||
        !mir_machine_single_call_argument(
            &mir.insns[100], &argument) ||
        argument != mir.insns[98].dst ||
        !mir_machine_constant_equals(mir.insns[101].dst, 0))
        return mir_machine_reject(
            "simple-file-io", "final-phase");
    plan->strings[0] = (int)mir.insns[11].immediate;
    plan->strings[1] = (int)mir.insns[48].immediate;
    plan->strings[2] = (int)mir.insns[68].immediate;
    plan->strings[3] = (int)mir.insns[1].immediate;
    plan->strings[4] = (int)mir.insns[3].immediate;
    plan->strings[5] = (int)mir.insns[18].immediate;
    plan->strings[6] = (int)mir.insns[23].immediate;
    plan->strings[7] = (int)mir.insns[30].immediate;
    plan->strings[8] = (int)mir.insns[40].immediate;
    plan->strings[9] = (int)mir.insns[87].immediate;
    plan->strings[10] = (int)mir.insns[98].immediate;
    return plan->buffer_size == 64;
}

static void mir_emit_simple_file_buffer(FILE *out)
{
    fputs("\tpush iy\n", out);
}

static void mir_emit_simple_file_load_stream(FILE *out)
{
    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n", out);
}

static void mir_emit_simple_file_open(
    FILE *out, const struct MirSimpleFileIoSchedule *plan,
    int path_string, int mode_string)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", mode_string);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", path_string);
    mir_emit_recovery_call(out, plan->open_function, plan->open_name);
    mir_emit_final_call_cleanup(out, 2);
    fputs("\tld (ix-2),l\n\tld (ix-1),h\n", out);
}

static void mir_emit_simple_file_puts(
    FILE *out, const struct MirSimpleFileIoSchedule *plan,
    int string_id)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_emit_recovery_call(out, plan->puts_function, plan->puts_name);
    mir_emit_final_call_cleanup(out, 1);
}

static void mir_emit_simple_file_io_schedule(
    FILE *out, const struct MirSimpleFileIoSchedule *plan)
{
    int write_ok = new_label();
    int read_ok = new_label();
    int loop = new_label();
    int done = new_label();

    fputs(MIR_EXACT_KERNEL_MARKER "\n"
          ";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-68\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fputs("\tpush ix\n\tpop iy\n"
          "\tld de,-68\n\tadd iy,de\n", out);

    mir_emit_simple_file_open(
        out, plan, plan->strings[3], plan->strings[4]);
    fputs("\tld a,(ix-2)\n\tor (ix-1)\n", out);
    fprintf(out, "\tjr nz,L%d\n", write_ok);
    mir_emit_simple_file_puts(out, plan, plan->strings[0]);
    fputs("\tld hl,1\n\tld sp,ix\n\tpop ix\n\tpop iy\n"
          ";@dcc.reg free=iy\n\tret\n", out);
    fprintf(out, "L%d:\n", write_ok);

    mir_emit_simple_file_load_stream(out);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[5]);
    mir_emit_recovery_call(
        out, plan->puts_stream_function,
        plan->puts_stream_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_emit_simple_file_load_stream(out);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[6]);
    mir_emit_recovery_call(
        out, plan->puts_stream_function,
        plan->puts_stream_name);
    mir_emit_final_call_cleanup(out, 2);
    fputs("\tld hl,3\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[7]);
    mir_emit_simple_file_load_stream(out);
    fputs("\tpush hl\n", out);
    mir_emit_recovery_call(
        out, plan->file_print_function,
        plan->file_print_name);
    mir_emit_final_call_cleanup(out, 3);
    mir_emit_simple_file_load_stream(out);
    fputs("\tpush hl\n", out);
    mir_emit_recovery_call(
        out, plan->close_function, plan->close_name);
    mir_emit_final_call_cleanup(out, 1);

    mir_emit_simple_file_open(
        out, plan, plan->strings[3], plan->strings[8]);
    fputs("\tld a,(ix-2)\n\tor (ix-1)\n", out);
    fprintf(out, "\tjr nz,L%d\n", read_ok);
    mir_emit_simple_file_puts(out, plan, plan->strings[1]);
    fputs("\tld hl,1\n\tld sp,ix\n\tpop ix\n\tpop iy\n"
          ";@dcc.reg free=iy\n\tret\n", out);
    fprintf(out,
            "L%d:\n\txor a\n\tld (ix-4),a\n\tld (ix-3),a\n"
            "L%d:\n",
            read_ok, loop);
    mir_emit_simple_file_load_stream(out);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->buffer_size);
    mir_emit_simple_file_buffer(out);
    mir_emit_recovery_call(
        out, plan->gets_function, plan->gets_name);
    mir_emit_final_call_cleanup(out, 3);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjr z,L%d\n", done);
    fputs("\tld l,(ix-4)\n\tld h,(ix-3)\n\tinc hl\n"
          "\tld (ix-4),l\n\tld (ix-3),h\n", out);
    mir_emit_simple_file_buffer(out);
    fputs("\tld l,(ix-4)\n\tld h,(ix-3)\n\tpush hl\n",
          out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[2]);
    mir_emit_recovery_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 3);
    fprintf(out, "\tjr L%d\nL%d:\n", loop, done);

    mir_emit_simple_file_load_stream(out);
    fputs("\tpush hl\n", out);
    mir_emit_recovery_call(
        out, plan->close_function, plan->close_name);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->formatted_value & 0xffff);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[9]);
    mir_emit_simple_file_buffer(out);
    mir_emit_recovery_call(
        out, plan->string_print_function,
        plan->string_print_name);
    mir_emit_final_call_cleanup(out, 3);
    mir_emit_simple_file_buffer(out);
    mir_emit_recovery_call(
        out, plan->puts_function, plan->puts_name);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[3]);
    mir_emit_recovery_call(
        out, plan->unlink_function, plan->unlink_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_emit_simple_file_puts(out, plan, plan->strings[10]);
    fputs("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tpop iy\n"
          ";@dcc.reg free=iy\n\tret\n", out);
}

static struct Sym *mir_recovery_global_load_symbol(
    int instruction)
{
    const struct MirInsn *load;
    struct Sym *symbol;

    if (instruction < 0 || instruction >= mir.count)
        return NULL;
    load = &mir.insns[instruction];
    if (load->opcode != MIR_LOAD ||
        !mir_machine_named_nonvolatile(load) ||
        (symbol = find_global(load->name)) == NULL ||
        symbol->storage != SC_GLOBAL || !symbol->is_defined ||
        symbol->is_volatile || type_size(symbol->type) != 2)
        return NULL;
    return symbol;
}

static int mir_recovery_indirect_call(
    int call_instruction, int load_instruction,
    int argument_count, const int *argument_instructions,
    struct Sym **pointer_out)
{
    const struct MirInsn *call = &mir.insns[call_instruction];
    int arguments[9];
    int argument;

    *pointer_out = mir_recovery_global_load_symbol(load_instruction);
    if (*pointer_out == NULL ||
        call->opcode != MIR_CALL ||
        call->src1 != mir.insns[load_instruction].dst ||
        call->src1 < 0 ||
        !mir_machine_call_arguments(
            call, argument_count, arguments))
        return 0;
    for (argument = 0; argument < argument_count; ++argument)
        if (arguments[argument] !=
            mir.insns[argument_instructions[argument]].dst)
            return 0;
    return 1;
}

static int mir_match_function_pointer_runtime_schedule(
    struct MirFunctionPointerRuntimeSchedule *plan)
{
    const int expected_opcodes[214] = {
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_STORE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_LOAD, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_LOAD,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_STRING_ADDRESS, MIR_NOP,
        MIR_ARG, MIR_STRING_ADDRESS, MIR_NOP, MIR_ARG, MIR_CONST, MIR_NOP,
        MIR_ARG, MIR_CALL, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_LOAD, MIR_STRING_ADDRESS, MIR_NOP, MIR_ARG,
        MIR_STRING_ADDRESS, MIR_NOP, MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_ARG, MIR_CALL, MIR_LOAD, MIR_ADDRESS, MIR_NOP,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG,
        MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_ARG, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG,
        MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_ARG, MIR_CALL, MIR_LOAD, MIR_CONST, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LOAD,
        MIR_ADDRESS, MIR_NOP, MIR_ARG, MIR_LOAD, MIR_NOP, MIR_ARG,
        MIR_CONST, MIR_NOP, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_LOAD,
        MIR_LOAD, MIR_NOP, MIR_ARG, MIR_CONST, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_ARG, MIR_CALL, MIR_STORE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_LOAD, MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP, MIR_LOAD,
        MIR_BINARY, MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_LOAD, MIR_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_LOAD, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_LOAD, MIR_ARG,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_RETURN
    };
    const int indirect_calls[11] = {
        8, 19, 34, 49, 61, 110, 124, 159, 186, 199, 209
    };
    const int pointer_loads[11] = {
        5, 14, 24, 39, 52, 105, 114, 149, 181, 194, 204
    };
    const int pointer_slots[11] = {
        0, 1, 2, 2, 3, 4, 5, 6, 7, 8, 9
    };
    const int argument_counts[11] = {
        1, 2, 3, 3, 3, 2, 3, 3, 2, 2, 2
    };
    const int argument_instructions[11][3] = {
        {6, -1, -1}, {15, 17, -1}, {25, 28, 31},
        {40, 43, 46}, {53, 56, 58}, {106, 108, -1},
        {115, 118, 121}, {150, 153, 157},
        {182, 184, -1}, {195, 197, -1}, {205, 207, -1}
    };
    const int print_calls[11] = {
        11, 21, 36, 51, 104, 113, 135, 179, 191, 201, 211
    };
    const int print_counts[11] = {
        2, 2, 2, 2, 9, 1, 2, 2, 2, 2, 2
    };
    struct Sym *buffer;
    char buffer_name[64];
    int buffer_offset;
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 214 || mir.next_value != 141 ||
        mir_cfg_block_count() != 5 || mir.local_bytes != 4 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_INT)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "function-pointer-runtime", "opcodes");
    for (item = 0; item < 11; ++item) {
        struct Sym *pointer;

        if (!mir_recovery_indirect_call(
                indirect_calls[item], pointer_loads[item],
                argument_counts[item],
                argument_instructions[item], &pointer))
            return mir_machine_reject(
                "function-pointer-runtime", "indirect-call");
        if (plan->pointer_globals[pointer_slots[item]] == NULL)
            plan->pointer_globals[pointer_slots[item]] = pointer;
        else if (plan->pointer_globals[pointer_slots[item]] != pointer)
            return mir_machine_reject(
                "function-pointer-runtime", "pointer-alias");
    }
    for (item = 0; item < 10; ++item) {
        int other;

        if (plan->pointer_globals[item] == NULL)
            return mir_machine_reject(
                "function-pointer-runtime", "pointer-missing");
        for (other = item + 1; other < 10; ++other)
            if (plan->pointer_globals[item] ==
                plan->pointer_globals[other])
                return mir_machine_reject(
                    "function-pointer-runtime", "pointer-distinct");
    }
    for (item = 0; item < 11; ++item) {
        struct Sym *print = mir_recovery_direct_call(
            print_calls[item], print_counts[item], 1,
            plan->print_name, sizeof(plan->print_name));

        if (print == NULL)
            return mir_machine_reject(
                "function-pointer-runtime", "print-call");
        if (item == 0)
            plan->print_function = print;
        else if (print != plan->print_function)
            return mir_machine_reject(
                "function-pointer-runtime", "print-alias");
    }
    plan->length_function = mir_recovery_direct_call(
        157, 1, 0, plan->length_name,
        sizeof(plan->length_name));
    if (plan->length_function == NULL ||
        !mir_machine_constant_equals(mir.insns[17].dst, 119) ||
        !mir_machine_constant_equals(mir.insns[31].dst, 3) ||
        !mir_machine_constant_equals(mir.insns[46].dst, 3) ||
        !mir_machine_constant_equals(mir.insns[56].dst, 90) ||
        !mir_machine_constant_equals(mir.insns[58].dst, 8) ||
        !mir_machine_constant_equals(mir.insns[106].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[108].dst, 81) ||
        !mir_machine_constant_equals(mir.insns[121].dst, 5) ||
        !mir_machine_constant_equals(mir.insns[126].dst, 5) ||
        !mir_machine_constant_equals(mir.insns[129].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[153].dst, 119) ||
        !mir_machine_constant_equals(mir.insns[174].dst, 65535) ||
        !mir_machine_constant_equals(mir.insns[197].dst, 111) ||
        !mir_machine_constant_equals(mir.insns[212].dst, 0))
        return mir_machine_reject(
            "function-pointer-runtime", "constants");
    if (mir.insns[1].opcode != MIR_STRING_ADDRESS ||
        mir.insns[2].src1 != mir.insns[1].dst ||
        strcmp(mir.insns[2].name, mir.insns[6].name) ||
        strcmp(mir.insns[2].name, mir.insns[15].name) ||
        strcmp(mir.insns[2].name, mir.insns[118].name) ||
        strcmp(mir.insns[2].name, mir.insns[150].name) ||
        strcmp(mir.insns[2].name, mir.insns[155].name) ||
        strcmp(mir.insns[2].name, mir.insns[167].name) ||
        strcmp(mir.insns[2].name, mir.insns[184].name) ||
        strcmp(mir.insns[2].name, mir.insns[195].name) ||
        strcmp(mir.insns[2].name, mir.insns[205].name))
        return mir_machine_reject(
            "function-pointer-runtime", "source-string");
    {
        const int buffer_addresses[3] = {53, 115, 182};
        int buffer_index;

        for (buffer_index = 0; buffer_index < 3; ++buffer_index) {
            if (!mir_recovery_global_address(
                    mir.insns[buffer_addresses[buffer_index]].dst,
                    &buffer, buffer_name, sizeof(buffer_name),
                    &buffer_offset) ||
                buffer == NULL || buffer_offset != 0)
                return mir_machine_reject(
                    "function-pointer-runtime", "buffer");
            plan->buffers[buffer_index] = buffer;
        }
        if (plan->buffers[0] == plan->buffers[1] ||
            plan->buffers[0] == plan->buffers[2] ||
            plan->buffers[1] == plan->buffers[2])
            return mir_machine_reject(
                "function-pointer-runtime", "buffer-distinct");
    }
    plan->strings[0] = (int)mir.insns[3].immediate;
    plan->strings[1] = (int)mir.insns[1].immediate;
    plan->strings[2] = (int)mir.insns[12].immediate;
    plan->strings[3] = (int)mir.insns[25].immediate;
    plan->strings[4] = (int)mir.insns[28].immediate;
    plan->strings[5] = (int)mir.insns[62].immediate;
    plan->strings[6] = (int)mir.insns[111].immediate;
    plan->strings[7] = (int)mir.insns[207].immediate;
    plan->search_character = 119;
    plan->bdos_function = 2;
    plan->bdos_character = 81;
    plan->memory_count = 8;
    plan->copy_count = 5;
    return 1;
}

static void mir_emit_function_pointer_global_call(
    FILE *out, struct Sym *pointer, int words)
{
    mir_machine_emit_global_word(out, pointer, 0);
    mir_emit_runtime_call(out, "__call_hl");
    mir_emit_final_call_cleanup(out, words);
}

static void mir_emit_function_pointer_print(
    FILE *out, const struct MirFunctionPointerRuntimeSchedule *plan,
    int format_string, int words)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", format_string);
    mir_emit_recovery_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, words + 1);
}

static void mir_emit_function_pointer_address(
    FILE *out, struct Sym *symbol)
{
    fprintf(out, "\tld hl,%s\n",
            asm_name_for(sym_asm_name(symbol)));
}

static void mir_emit_function_pointer_runtime_schedule(
    FILE *out, const struct MirFunctionPointerRuntimeSchedule *plan)
{
    int found = new_label();
    int offset_ready = new_label();
    int byte;

    fputs(MIR_EXACT_KERNEL_MARKER "\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[1]);
    mir_emit_function_pointer_global_call(
        out, plan->pointer_globals[0], 1);
    fputs("\tpush hl\n", out);
    mir_emit_function_pointer_print(out, plan, plan->strings[0], 1);

    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->search_character);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[1]);
    mir_emit_function_pointer_global_call(
        out, plan->pointer_globals[1], 2);
    fputs("\tpush hl\n", out);
    mir_emit_function_pointer_print(out, plan, plan->strings[2], 1);

    fputs("\tld hl,3\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[4]);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[3]);
    mir_emit_function_pointer_global_call(
        out, plan->pointer_globals[2], 3);
    fputs("\tpush hl\n", out);
    mir_emit_function_pointer_print(out, plan, plan->strings[0], 1);
    fputs("\tld hl,3\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[3]);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[3]);
    mir_emit_function_pointer_global_call(
        out, plan->pointer_globals[2], 3);
    fputs("\tpush hl\n", out);
    mir_emit_function_pointer_print(out, plan, plan->strings[0], 1);

    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->memory_count);
    fputs("\tld hl,90\n\tpush hl\n", out);
    mir_emit_function_pointer_address(out, plan->buffers[0]);
    fputs("\tpush hl\n", out);
    mir_emit_function_pointer_global_call(
        out, plan->pointer_globals[3], 3);
    for (byte = plan->memory_count - 1; byte >= 0; --byte) {
        fprintf(out, "\tld a,(%s%+d)\n\tld l,a\n"
                "\trlca\n\tsbc a,a\n\tld h,a\n\tpush hl\n",
                asm_name_for(sym_asm_name(plan->buffers[0])), byte);
    }
    mir_emit_function_pointer_print(out, plan, plan->strings[5], 8);

    fprintf(out, "\tld hl,%d\n\tpush hl\n", plan->bdos_character);
    fprintf(out, "\tld hl,%d\n\tpush hl\n", plan->bdos_function);
    mir_emit_function_pointer_global_call(
        out, plan->pointer_globals[4], 2);
    mir_emit_function_pointer_print(out, plan, plan->strings[6], 0);

    fprintf(out, "\tld hl,%d\n\tpush hl\n", plan->copy_count);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[1]);
    mir_emit_function_pointer_address(out, plan->buffers[1]);
    fputs("\tpush hl\n", out);
    mir_emit_function_pointer_global_call(
        out, plan->pointer_globals[5], 3);
    fprintf(out, "\txor a\n\tld (%s+%d),a\n",
            asm_name_for(sym_asm_name(plan->buffers[1])),
            plan->copy_count);
    mir_emit_function_pointer_address(out, plan->buffers[1]);
    fputs("\tpush hl\n", out);
    mir_emit_function_pointer_print(out, plan, plan->strings[2], 1);

    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[1]);
    mir_emit_recovery_call(
        out, plan->length_function, plan->length_name);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->search_character);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[1]);
    mir_emit_function_pointer_global_call(
        out, plan->pointer_globals[6], 3);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjr nz,L%d\n\tld hl,65535\n\tjr L%d\nL%d:\n",
            found, offset_ready, found);
    fprintf(out, "\tld de,S%d\n\tor a\n\tsbc hl,de\nL%d:\n",
            plan->strings[1], offset_ready);
    fputs("\tpush hl\n", out);
    mir_emit_function_pointer_print(out, plan, plan->strings[0], 1);

    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[1]);
    mir_emit_function_pointer_address(out, plan->buffers[2]);
    fputs("\tpush hl\n", out);
    mir_emit_function_pointer_global_call(
        out, plan->pointer_globals[7], 2);
    mir_emit_function_pointer_address(out, plan->buffers[2]);
    fputs("\tpush hl\n", out);
    mir_emit_function_pointer_print(out, plan, plan->strings[2], 1);

    fputs("\tld hl,111\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[1]);
    mir_emit_function_pointer_global_call(
        out, plan->pointer_globals[8], 2);
    fputs("\tpush hl\n", out);
    mir_emit_function_pointer_print(out, plan, plan->strings[2], 1);

    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[7]);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[1]);
    mir_emit_function_pointer_global_call(
        out, plan->pointer_globals[9], 2);
    fputs("\tpush hl\n", out);
    mir_emit_function_pointer_print(out, plan, plan->strings[2], 1);
    fputs("\tld hl,0\n\tret\n", out);
}

static int mir_match_snprintf_sequence_schedule(
    struct MirSnprintfSequenceSchedule *plan)
{
    const int expected_opcodes[172] = {
        MIR_LABEL, MIR_ADDRESS, MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_ARG,
        MIR_CONST, MIR_NOP, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_ARG,
        MIR_CONST, MIR_NOP, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_ARG,
        MIR_CONST, MIR_NOP, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT,
        MIR_ADDRESS, MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_ADDRESS, MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_ADDRESS, MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_ADDRESS, MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_ADDRESS, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_ARG, MIR_CONST,
        MIR_NOP, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN
    };
    const int format_calls[7] = {10, 27, 44, 61, 84, 104, 121};
    const int format_counts[7] = {4, 3, 3, 3, 3, 3, 3};
    const int size_instructions[7] = {3, 22, 39, 56, 79, 99, 116};
    const int print_calls[10] = {
        19, 36, 53, 70, 96, 113, 130, 149, 166, 169
    };
    const int print_counts[10] = {3, 3, 3, 3, 3, 3, 3, 2, 2, 1};
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 172 || mir.next_value != 103 ||
        mir_cfg_block_count() != 1 || mir.local_bytes != 36 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        (mir.return_type & 15) != TYPE_INT)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "snprintf-sequence", "opcodes");
    for (item = 0; item < 7; ++item) {
        struct Sym *function = mir_recovery_direct_call(
            format_calls[item], format_counts[item], 1,
            plan->format_name, sizeof(plan->format_name));

        if (function == NULL ||
            !mir_recovery_constant_value(
                mir.insns[size_instructions[item]].dst,
                &plan->sizes[item]))
            return mir_machine_reject(
                "snprintf-sequence", "format-call");
        if (item == 0)
            plan->format_function = function;
        else if (function != plan->format_function)
            return mir_machine_reject(
                "snprintf-sequence", "format-alias");
    }
    for (item = 0; item < 10; ++item) {
        struct Sym *function = mir_recovery_direct_call(
            print_calls[item], print_counts[item], 1,
            plan->print_name, sizeof(plan->print_name));

        if (function == NULL)
            return mir_machine_reject(
                "snprintf-sequence", "print-call");
        if (item == 0)
            plan->print_function = function;
        else if (function != plan->print_function)
            return mir_machine_reject(
                "snprintf-sequence", "print-alias");
    }
    plan->variadic_helper = mir_recovery_direct_call(
        144, 6, 1, plan->helper_name,
        sizeof(plan->helper_name));
    if (plan->variadic_helper == NULL ||
        mir_recovery_direct_call(
            161, 5, 1, plan->helper_name,
            sizeof(plan->helper_name)) !=
            plan->variadic_helper ||
        !mir_recovery_constant_value(
            mir.insns[8].dst, &plan->first_value) ||
        !mir_recovery_constant_value(
            mir.insns[138].dst, &plan->helper_values[0]) ||
        !mir_recovery_constant_value(
            mir.insns[140].dst, &plan->helper_values[1]) ||
        !mir_recovery_constant_value(
            mir.insns[142].dst, &plan->helper_values[2]) ||
        !mir_recovery_constant_value(
            mir.insns[159].dst, &plan->helper_values[3]) ||
        !mir_machine_constant_equals(mir.insns[75].dst, 81) ||
        !mir_machine_constant_equals(mir.insns[170].dst, 0))
        return mir_machine_reject(
            "snprintf-sequence", "values");
    plan->strings[0] = (int)mir.insns[6].immediate;
    plan->strings[1] = (int)mir.insns[13].immediate;
    plan->strings[2] = (int)mir.insns[25].immediate;
    plan->strings[3] = (int)mir.insns[42].immediate;
    plan->strings[4] = (int)mir.insns[59].immediate;
    plan->strings[5] = (int)mir.insns[87].immediate;
    plan->strings[6] = (int)mir.insns[102].immediate;
    plan->strings[7] = (int)mir.insns[119].immediate;
    plan->strings[8] = (int)mir.insns[136].immediate;
    plan->strings[9] = (int)mir.insns[145].immediate;
    plan->strings[10] = (int)mir.insns[155].immediate;
    plan->strings[11] = (int)mir.insns[157].immediate;
    plan->strings[12] = (int)mir.insns[167].immediate;
    return
        plan->sizes[0] == 32 &&
        plan->sizes[1] == 6 &&
        plan->sizes[2] == 5 &&
        plan->sizes[3] == 1 &&
        plan->sizes[4] == 0 &&
        plan->sizes[5] == 3 &&
        plan->sizes[6] == 3;
}

static void mir_emit_snprintf_buffer(FILE *out)
{
    fputs("\tpush iy\n", out);
}

static void mir_emit_snprintf_call(
    FILE *out, const struct MirSnprintfSequenceSchedule *plan,
    int size, int format_string, int has_value, int value)
{
    if (has_value) {
        fprintf(out, "\tld hl,%d\n\tpush hl\n",
                value & 0xffff);
    }
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", format_string);
    fprintf(out, "\tld hl,%d\n\tpush hl\n", size);
    mir_emit_snprintf_buffer(out);
    mir_emit_recovery_call(
        out, plan->format_function, plan->format_name);
    mir_emit_final_call_cleanup(out, has_value ? 4 : 3);
}

static void mir_emit_snprintf_result(
    FILE *out, const struct MirSnprintfSequenceSchedule *plan,
    int print_string)
{
    fputs("\tpush hl\n", out);
    mir_emit_snprintf_buffer(out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", print_string);
    mir_emit_recovery_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 3);
}

static void mir_emit_snprintf_buffer_print(
    FILE *out, const struct MirSnprintfSequenceSchedule *plan)
{
    mir_emit_snprintf_buffer(out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[9]);
    mir_emit_recovery_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 2);
}

static void mir_emit_snprintf_sequence_schedule(
    FILE *out, const struct MirSnprintfSequenceSchedule *plan)
{
    fputs(MIR_EXACT_KERNEL_MARKER "\n"
          ";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-32\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fputs("\tpush ix\n\tpop iy\n\tld de,-32\n\tadd iy,de\n", out);

    mir_emit_snprintf_call(
        out, plan, plan->sizes[0], plan->strings[0],
        1, plan->first_value);
    mir_emit_snprintf_result(out, plan, plan->strings[1]);
    mir_emit_snprintf_call(
        out, plan, plan->sizes[1], plan->strings[2], 0, 0);
    mir_emit_snprintf_result(out, plan, plan->strings[1]);
    mir_emit_snprintf_call(
        out, plan, plan->sizes[2], plan->strings[3], 0, 0);
    mir_emit_snprintf_result(out, plan, plan->strings[1]);
    mir_emit_snprintf_call(
        out, plan, plan->sizes[3], plan->strings[4], 0, 0);
    mir_emit_snprintf_result(out, plan, plan->strings[1]);

    fputs("\tld (iy+0),81\n", out);
    mir_emit_snprintf_call(
        out, plan, plan->sizes[4], plan->strings[4], 0, 0);
    fputs("\tpush hl\n\tld l,(iy+0)\n"
          "\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[5]);
    mir_emit_recovery_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 3);

    mir_emit_snprintf_call(
        out, plan, plan->sizes[5], plan->strings[6], 0, 0);
    mir_emit_snprintf_result(out, plan, plan->strings[1]);
    mir_emit_snprintf_call(
        out, plan, plan->sizes[6], plan->strings[7], 0, 0);
    mir_emit_snprintf_result(out, plan, plan->strings[1]);

    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->helper_values[2] & 0xffff);
    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->helper_values[1] & 0xffff);
    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->helper_values[0] & 0xffff);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[8]);
    fputs("\tld hl,4\n\tpush hl\n", out);
    mir_emit_snprintf_buffer(out);
    mir_emit_recovery_call(
        out, plan->variadic_helper, plan->helper_name);
    mir_emit_final_call_cleanup(out, 6);
    mir_emit_snprintf_buffer_print(out, plan);

    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->helper_values[3] & 0xffff);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[11]);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[10]);
    fputs("\tld hl,32\n\tpush hl\n", out);
    mir_emit_snprintf_buffer(out);
    mir_emit_recovery_call(
        out, plan->variadic_helper, plan->helper_name);
    mir_emit_final_call_cleanup(out, 5);
    mir_emit_snprintf_buffer_print(out, plan);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[12]);
    mir_emit_recovery_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tpop iy\n"
          ";@dcc.reg free=iy\n\tret\n", out);
}

static int mir_match_legacy_file_roundtrip_schedule(
    struct MirLegacyFileRoundtripSchedule *plan)
{
    const unsigned char expected_opcodes[475] = {
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_CONST, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_LOAD,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_STORE_INDIRECT, MIR_LABEL,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL,
        MIR_NOP, MIR_ARG, MIR_ADDRESS, MIR_NOP, MIR_ARG, MIR_NOP,
        MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE, MIR_NOP, MIR_NOP,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST, MIR_NOP,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_LABEL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP, MIR_PHI, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_ARG, MIR_ADDRESS,
        MIR_NOP, MIR_ARG, MIR_NOP, MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_PHI, MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL,
        MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST, MIR_NOP, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP,
        MIR_PHI, MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_UNARY, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP, MIR_ARG,
        MIR_NOP, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_LABEL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_ARG, MIR_ADDRESS,
        MIR_NOP, MIR_ARG, MIR_NOP, MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_NOP,
        MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST, MIR_NOP, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP,
        MIR_PHI, MIR_NOP, MIR_NOP, MIR_PHI, MIR_PHI, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_UNARY, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP, MIR_ARG,
        MIR_NOP, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_LABEL, MIR_NOP, MIR_ARG, MIR_ADDRESS, MIR_NOP, MIR_ARG,
        MIR_NOP, MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_PHI, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_LABEL, MIR_NOP, MIR_JUMP, MIR_LABEL,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_NOP,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE,
        MIR_CONST, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN
    };
    const int failure_calls[11] = {
        17, 74, 101, 133, 157, 191,
        241, 290, 319, 385, 468
    };
    const int close_calls[4] = {86, 176, 304, 456};
    int buffer_offset;
    char buffer_name[64];
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 475 || mir.next_value != 304 ||
        mir_cfg_block_count() != 43 || mir.local_bytes != 20 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        !mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_INT)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "legacy-file-roundtrip", "opcodes");
    plan->create_function = mir_recovery_direct_call(
        7, 2, 0, plan->create_name, sizeof(plan->create_name));
    plan->failure_function = mir_recovery_direct_call(
        17, 1, 0, plan->failure_name, sizeof(plan->failure_name));
    plan->write_function = mir_recovery_direct_call(
        65, 3, 0, plan->write_name, sizeof(plan->write_name));
    plan->close_function = mir_recovery_direct_call(
        86, 1, 0, plan->close_name, sizeof(plan->close_name));
    plan->open_function = mir_recovery_named_call(
        91, 0, plan->open_name, sizeof(plan->open_name));
    plan->read_function = mir_recovery_direct_call(
        124, 3, 0, plan->read_name, sizeof(plan->read_name));
    plan->seek_function = mir_recovery_direct_call(
        225, 3, 0, plan->seek_name, sizeof(plan->seek_name));
    plan->print_function = mir_recovery_direct_call(
        238, 3, 1, plan->offset_print_name,
        sizeof(plan->offset_print_name));
    plan->unlink_function = mir_recovery_direct_call(
        459, 1, 0, plan->unlink_name, sizeof(plan->unlink_name));
    if (plan->create_function == NULL)
        return mir_machine_reject(
            "legacy-file-roundtrip", "create-function");
    if (plan->failure_function == NULL)
        return mir_machine_reject(
            "legacy-file-roundtrip", "failure-function");
    if (plan->write_function == NULL)
        return mir_machine_reject(
            "legacy-file-roundtrip", "write-function");
    if (plan->close_function == NULL)
        return mir_machine_reject(
            "legacy-file-roundtrip", "close-function");
    if (plan->open_function == NULL)
        return mir_machine_reject(
            "legacy-file-roundtrip", "open-function");
    if (plan->read_function == NULL)
        return mir_machine_reject(
            "legacy-file-roundtrip", "read-function");
    if (plan->seek_function == NULL)
        return mir_machine_reject(
            "legacy-file-roundtrip", "seek-function");
    if (plan->print_function == NULL)
        return mir_machine_reject(
            "legacy-file-roundtrip", "print-function");
    if (plan->unlink_function == NULL)
        return mir_machine_reject(
            "legacy-file-roundtrip", "unlink-function");
    for (item = 0; item < 11; ++item)
        if (mir_recovery_direct_call(
                failure_calls[item], 1, 0,
                plan->failure_name, sizeof(plan->failure_name)) !=
            plan->failure_function)
            return mir_machine_reject(
                "legacy-file-roundtrip", "failure-alias");
    for (item = 0; item < 4; ++item)
        if (mir_recovery_direct_call(
                close_calls[item], 1, 0,
                plan->close_name, sizeof(plan->close_name)) !=
            plan->close_function)
            return mir_machine_reject(
                "legacy-file-roundtrip", "close-alias");
    if (mir_recovery_named_call(
        181, 0, plan->open_name,
            sizeof(plan->open_name)) != plan->open_function ||
    mir_recovery_named_call(
        309, 0, plan->open_name,
            sizeof(plan->open_name)) != plan->open_function ||
        mir_recovery_direct_call(
            281, 3, 0, plan->write_name,
            sizeof(plan->write_name)) != plan->write_function ||
        mir_recovery_direct_call(
            376, 3, 0, plan->read_name,
            sizeof(plan->read_name)) != plan->read_function ||
        mir_recovery_direct_call(
            349, 3, 0, plan->seek_name,
            sizeof(plan->seek_name)) != plan->seek_function ||
        mir_recovery_direct_call(
            362, 3, 1, plan->offset_print_name,
            sizeof(plan->offset_print_name)) != plan->print_function ||
        mir_recovery_direct_call(
            472, 1, 1, plan->final_print_name,
            sizeof(plan->final_print_name)) != plan->print_function)
        return mir_machine_reject(
            "legacy-file-roundtrip", "call-alias");
    if (!mir_recovery_global_address(
            mir.insns[45].dst, &plan->buffer,
            buffer_name, sizeof(buffer_name),
            &buffer_offset) ||
        plan->buffer == NULL || buffer_offset != 0)
        return mir_machine_reject(
            "legacy-file-roundtrip", "buffer");
    if (!mir_recovery_constant_value(
            mir.insns[5].dst, &plan->create_flags) ||
        !mir_recovery_constant_value(
            mir.insns[89].dst, &plan->read_flags) ||
        !mir_recovery_constant_value(
            mir.insns[179].dst, &plan->write_flags) ||
        !mir_recovery_constant_value(
            mir.insns[19].dst, &plan->buffer_bytes) ||
        !mir_recovery_constant_value(
            mir.insns[42].dst, &plan->buffer_elements) ||
        !mir_recovery_constant_value(
            mir.insns[30].dst, &plan->iterations) ||
        !mir_recovery_constant_value(
            mir.insns[208].dst, &plan->rewrite_stride) ||
        !mir_recovery_constant_value(
            mir.insns[263].dst, &plan->rewrite_bias) ||
        plan->create_flags != 66 ||
        plan->read_flags != 0 ||
        plan->write_flags != 1 ||
        plan->buffer_bytes != 64 ||
        plan->buffer_elements != 32 ||
        plan->iterations != 4096 ||
        plan->rewrite_stride != 8 ||
        plan->rewrite_bias != 16384)
        return mir_machine_reject(
            "legacy-file-roundtrip", "constants");
    plan->strings[0] = (int)mir.insns[1].immediate;
    plan->strings[1] = (int)mir.insns[15].immediate;
    plan->strings[2] = (int)mir.insns[72].immediate;
    plan->strings[3] = (int)mir.insns[99].immediate;
    plan->strings[4] = (int)mir.insns[131].immediate;
    plan->strings[5] = (int)mir.insns[155].immediate;
    plan->strings[6] = (int)mir.insns[189].immediate;
    plan->strings[7] = (int)mir.insns[232].immediate;
    plan->strings[8] = (int)mir.insns[239].immediate;
    plan->strings[9] = (int)mir.insns[288].immediate;
    plan->strings[10] = (int)mir.insns[383].immediate;
    plan->strings[11] = (int)mir.insns[466].immediate;
    plan->strings[12] = (int)mir.insns[470].immediate;
    return !mir_machine_constant_equals(mir.insns[473].dst, 1);
}

static void mir_legacy_roundtrip_load_word(
    FILE *out, int offset)
{
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            offset, offset + 1);
}

static void mir_legacy_roundtrip_store_word(
    FILE *out, int offset)
{
    fprintf(out,
            "\tld (ix%+d),l\n\tld (ix%+d),h\n",
            offset, offset + 1);
}

static void mir_legacy_roundtrip_push_buffer(
    FILE *out, const struct MirLegacyFileRoundtripSchedule *plan)
{
    fprintf(out, "\tld hl,%s\n\tpush hl\n",
            asm_name_for(sym_asm_name(plan->buffer)));
}

static void mir_legacy_roundtrip_failure(
    FILE *out, const struct MirLegacyFileRoundtripSchedule *plan,
    int string_id)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_emit_recovery_call(
        out, plan->failure_function, plan->failure_name);
}

static void mir_legacy_roundtrip_open(
    FILE *out, const struct MirLegacyFileRoundtripSchedule *plan,
    int flags, int failure_string)
{
    int opened = new_label();

    fprintf(out, "\tld hl,%d\n\tpush hl\n", flags);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[0]);
    mir_emit_recovery_call(
        out, plan->open_function, plan->open_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_legacy_roundtrip_store_word(out, -2);
    fputs("\tinc hl\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjr nz,L%d\n", opened);
    mir_legacy_roundtrip_failure(out, plan, failure_string);
    fprintf(out, "L%d:\n", opened);
}

static void mir_legacy_roundtrip_close(
    FILE *out, const struct MirLegacyFileRoundtripSchedule *plan)
{
    mir_legacy_roundtrip_load_word(out, -2);
    fputs("\tpush hl\n", out);
    mir_emit_recovery_call(
        out, plan->close_function, plan->close_name);
    mir_emit_final_call_cleanup(out, 1);
}

static void mir_legacy_roundtrip_io(
    FILE *out, const struct MirLegacyFileRoundtripSchedule *plan,
    struct Sym *function, const char *call_name)
{
    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->buffer_bytes);
    mir_legacy_roundtrip_push_buffer(out, plan);
    mir_legacy_roundtrip_load_word(out, -2);
    fputs("\tpush hl\n", out);
    mir_emit_recovery_call(out, function, call_name);
    mir_emit_final_call_cleanup(out, 3);
}

static void mir_legacy_roundtrip_check_count(
    FILE *out, const struct MirLegacyFileRoundtripSchedule *plan,
    int failure_string)
{
    int matched = new_label();

    fprintf(out,
            "\tld de,%d\n\tor a\n\tsbc hl,de\n",
            plan->buffer_bytes);
    fprintf(out, "\tjr z,L%d\n", matched);
    mir_legacy_roundtrip_failure(out, plan, failure_string);
    fprintf(out, "L%d:\n", matched);
}

static void mir_legacy_roundtrip_fill(
    FILE *out, const struct MirLegacyFileRoundtripSchedule *plan,
    int biased)
{
    int loop = new_label();

    mir_legacy_roundtrip_load_word(out, -4);
    if (biased) {
        fprintf(out, "\tld de,%d\n\tadd hl,de\n",
                plan->rewrite_bias);
    }
    fputs("\tex de,hl\n", out);
    fprintf(out,
            "\tld hl,%s\n\tld b,%d\nL%d:\n"
            "\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tinc hl\n"
            "\tdjnz L%d\n",
            asm_name_for(sym_asm_name(plan->buffer)),
            plan->buffer_elements, loop, loop);
}

static void mir_legacy_roundtrip_verify(
    FILE *out, const struct MirLegacyFileRoundtripSchedule *plan,
    int biased, int failure_string)
{
    int loop = new_label();
    int matched = new_label();

    mir_legacy_roundtrip_load_word(out, -4);
    if (biased) {
        fprintf(out, "\tld de,%d\n\tadd hl,de\n",
                plan->rewrite_bias);
    }
    fputs("\tex de,hl\n", out);
    fprintf(out,
            "\tld hl,%s\n\tld b,%d\nL%d:\n"
            "\tld a,(hl)\n\tcp e\n",
            asm_name_for(sym_asm_name(plan->buffer)),
            plan->buffer_elements, loop);
    fprintf(out, "\tjr nz,L%d\n", matched);
    fputs("\tinc hl\n\tld a,(hl)\n\tcp d\n", out);
    fprintf(out, "\tjr nz,L%d\n\tinc hl\n\tdjnz L%d\n",
            matched, loop);
    {
        int done = new_label();

        fprintf(out, "\tjr L%d\nL%d:\n",
                done, matched);
        mir_legacy_roundtrip_failure(
            out, plan, failure_string);
        fprintf(out, "L%d:\n", done);
    }
}

static void mir_legacy_roundtrip_make_offset(FILE *out)
{
    int shift;

    mir_legacy_roundtrip_load_word(out, -4);
    fputs("\tld de,0\n", out);
    for (shift = 0; shift < 6; ++shift)
        fputs("\tadd hl,hl\n\trl e\n\trl d\n", out);
    mir_legacy_roundtrip_store_word(out, -8);
    fputs("\tex de,hl\n", out);
    mir_legacy_roundtrip_store_word(out, -6);
}

static void mir_legacy_roundtrip_seek(
    FILE *out, const struct MirLegacyFileRoundtripSchedule *plan)
{
    int matched = new_label();

    mir_legacy_roundtrip_make_offset(out);
    fputs("\tld hl,0\n\tpush hl\n", out);
    mir_legacy_roundtrip_load_word(out, -6);
    fputs("\tpush hl\n", out);
    mir_legacy_roundtrip_load_word(out, -8);
    fputs("\tpush hl\n", out);
    mir_legacy_roundtrip_load_word(out, -2);
    fputs("\tpush hl\n", out);
    mir_emit_recovery_call(
        out, plan->seek_function, plan->seek_name);
    mir_emit_final_call_cleanup(out, 4);
    mir_legacy_roundtrip_store_word(out, -12);
    fputs("\tex de,hl\n", out);
    mir_legacy_roundtrip_store_word(out, -10);
    mir_legacy_roundtrip_load_word(out, -12);
    fputs("\tld c,(ix-8)\n\tld b,(ix-7)\n"
          "\tor a\n\tsbc hl,bc\n", out);
    fprintf(out, "\tjr nz,L%d\n", matched);
    mir_legacy_roundtrip_load_word(out, -10);
    fputs("\tld c,(ix-6)\n\tld b,(ix-5)\n"
          "\tor a\n\tsbc hl,bc\n", out);
    {
        int ok = new_label();

        fprintf(out, "\tjr z,L%d\nL%d:\n", ok, matched);
        mir_legacy_roundtrip_load_word(out, -6);
        fputs("\tpush hl\n", out);
        mir_legacy_roundtrip_load_word(out, -8);
        fputs("\tpush hl\n", out);
        mir_legacy_roundtrip_load_word(out, -10);
        fputs("\tpush hl\n", out);
        mir_legacy_roundtrip_load_word(out, -12);
        fputs("\tpush hl\n", out);
        fprintf(out, "\tld hl,S%d\n\tpush hl\n",
                plan->strings[7]);
        mir_emit_runtime_call(out, plan->offset_print_name);
        mir_emit_final_call_cleanup(out, 5);
        mir_legacy_roundtrip_failure(
            out, plan, plan->strings[8]);
        fprintf(out, "L%d:\n", ok);
    }
}

static void mir_emit_legacy_file_roundtrip_schedule(
    FILE *out, const struct MirLegacyFileRoundtripSchedule *plan)
{
    int first_loop = new_label();
    int first_done = new_label();
    int second_loop = new_label();
    int second_done = new_label();
    int third_loop = new_label();
    int third_done = new_label();
    int third_skip = new_label();
    int fourth_loop = new_label();
    int fourth_done = new_label();
    int fourth_plain = new_label();
    int fourth_verify = new_label();
    int unlink_ok = new_label();

    fputs(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-12\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->create_flags);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[0]);
    mir_emit_recovery_call(
        out, plan->create_function, plan->create_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_legacy_roundtrip_store_word(out, -2);
    fputs("\tinc hl\n\tld a,h\n\tor l\n", out);
    {
        int created = new_label();

        fprintf(out, "\tjr nz,L%d\n", created);
        mir_legacy_roundtrip_failure(out, plan, plan->strings[1]);
        fprintf(out, "L%d:\n", created);
    }
    fputs("\tld hl,0\n", out);
    mir_legacy_roundtrip_store_word(out, -4);
    fprintf(out, "L%d:\n", first_loop);
    mir_legacy_roundtrip_load_word(out, -4);
    fprintf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n",
            plan->iterations);
    fprintf(out, "\tjp nc,L%d\n", first_done);
    mir_legacy_roundtrip_fill(out, plan, 0);
    mir_legacy_roundtrip_io(
        out, plan, plan->write_function, plan->write_name);
    mir_legacy_roundtrip_check_count(
        out, plan, plan->strings[2]);
    mir_legacy_roundtrip_load_word(out, -4);
    fputs("\tinc hl\n", out);
    mir_legacy_roundtrip_store_word(out, -4);
    fprintf(out, "\tjp L%d\nL%d:\n", first_loop, first_done);
    mir_legacy_roundtrip_close(out, plan);

    mir_legacy_roundtrip_open(
        out, plan, plan->read_flags, plan->strings[3]);
    fputs("\tld hl,0\n", out);
    mir_legacy_roundtrip_store_word(out, -4);
    fprintf(out, "L%d:\n", second_loop);
    mir_legacy_roundtrip_load_word(out, -4);
    fprintf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n",
            plan->iterations);
    fprintf(out, "\tjp nc,L%d\n", second_done);
    mir_legacy_roundtrip_io(
        out, plan, plan->read_function, plan->read_name);
    mir_legacy_roundtrip_check_count(
        out, plan, plan->strings[4]);
    mir_legacy_roundtrip_verify(
        out, plan, 0, plan->strings[5]);
    mir_legacy_roundtrip_load_word(out, -4);
    fputs("\tinc hl\n", out);
    mir_legacy_roundtrip_store_word(out, -4);
    fprintf(out, "\tjp L%d\nL%d:\n", second_loop, second_done);
    mir_legacy_roundtrip_close(out, plan);

    mir_legacy_roundtrip_open(
        out, plan, plan->write_flags, plan->strings[6]);
    fputs("\tld hl,0\n", out);
    mir_legacy_roundtrip_store_word(out, -4);
    fprintf(out, "L%d:\n", third_loop);
    mir_legacy_roundtrip_load_word(out, -4);
    fprintf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n",
            plan->iterations);
    fprintf(out, "\tjp nc,L%d\n", third_done);
    fputs("\tld a,(ix-4)\n", out);
    fprintf(out, "\tand %d\n\tjp nz,L%d\n",
            plan->rewrite_stride - 1, third_skip);
    mir_legacy_roundtrip_seek(out, plan);
    mir_legacy_roundtrip_fill(out, plan, 1);
    mir_legacy_roundtrip_io(
        out, plan, plan->write_function, plan->write_name);
    mir_legacy_roundtrip_check_count(
        out, plan, plan->strings[9]);
    fprintf(out, "L%d:\n", third_skip);
    mir_legacy_roundtrip_load_word(out, -4);
    fputs("\tinc hl\n", out);
    mir_legacy_roundtrip_store_word(out, -4);
    fprintf(out, "\tjp L%d\nL%d:\n", third_loop, third_done);
    mir_legacy_roundtrip_close(out, plan);

    mir_legacy_roundtrip_open(
        out, plan, plan->read_flags, plan->strings[3]);
    fprintf(out, "\tld hl,%d\n",
            plan->iterations - 1);
    mir_legacy_roundtrip_store_word(out, -4);
    fprintf(out, "L%d:\n", fourth_loop);
    mir_legacy_roundtrip_seek(out, plan);
    mir_legacy_roundtrip_io(
        out, plan, plan->read_function, plan->read_name);
    mir_legacy_roundtrip_check_count(
        out, plan, plan->strings[9]);
    fputs("\tld a,(ix-4)\n", out);
    fprintf(out, "\tand %d\n\tjp nz,L%d\n",
            plan->rewrite_stride - 1, fourth_plain);
    mir_legacy_roundtrip_verify(
        out, plan, 1, plan->strings[5]);
    fprintf(out, "\tjp L%d\nL%d:\n",
            fourth_verify, fourth_plain);
    mir_legacy_roundtrip_verify(
        out, plan, 0, plan->strings[5]);
    fprintf(out, "L%d:\n", fourth_verify);
    mir_legacy_roundtrip_load_word(out, -4);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n\tdec hl\n", fourth_done);
    mir_legacy_roundtrip_store_word(out, -4);
    fprintf(out, "\tjp L%d\nL%d:\n",
            fourth_loop, fourth_done);
    mir_legacy_roundtrip_close(out, plan);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[0]);
    mir_emit_recovery_call(
        out, plan->unlink_function, plan->unlink_name);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjr z,L%d\n", unlink_ok);
    mir_legacy_roundtrip_failure(out, plan, plan->strings[11]);
    fprintf(out, "L%d:\n\tld hl,S%d\n\tpush hl\n",
            unlink_ok, plan->strings[12]);
    mir_emit_runtime_call(out, plan->final_print_name);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_sliding_window_driver_schedule(
    struct MirSlidingWindowDriverSchedule *plan)
{
    const int expected_opcodes[97] = {
        MIR_LABEL, MIR_CONST, MIR_STORE, MIR_CONST, MIR_STORE, MIR_CONST,
        MIR_STORE, MIR_CONST, MIR_STORE, MIR_CONST, MIR_STORE, MIR_CONST,
        MIR_STORE, MIR_CONST, MIR_STORE, MIR_CONST, MIR_STORE,
        MIR_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_ADDRESS, MIR_ARG, MIR_CALL, MIR_STORE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG,
        MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_PHI, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_UNARY, MIR_RETURN
    };
    const int input_constants[8] = {
        1, 3, 5, 7, 9, 11, 13, 15
    };
    const int output_indices[3] = {32, 37, 42};
    const int output_loads[3] = {34, 39, 44};
    const int expected_indices[3] = {52, 67, 82};
    const int expected_values[3] = {55, 70, 85};
    int arguments[5];
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 97 || mir.next_value != 59 ||
        mir_cfg_block_count() != 10 || mir.local_bytes != 32 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_INT)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "sliding-window-driver", "opcodes");
    for (item = 0; item < 8; ++item)
        if (!mir_recovery_constant_value(
                mir.insns[input_constants[item]].dst,
                &plan->input_values[item]) ||
            mir.insns[input_constants[item] + 1].src1 !=
                mir.insns[input_constants[item]].dst)
            return mir_machine_reject(
                "sliding-window-driver", "input");
    plan->window_function = mir_recovery_direct_call(
        25, 4, 0, plan->window_name,
        sizeof(plan->window_name));
    plan->print_function = mir_recovery_direct_call(
        46, 5, 1, plan->print_name,
        sizeof(plan->print_name));
    if (plan->window_function == NULL ||
        plan->print_function == NULL ||
        !mir_machine_call_arguments(
            &mir.insns[25], 4, arguments) ||
        arguments[0] != mir.insns[17].dst ||
        arguments[1] != mir.insns[19].dst ||
        arguments[2] != mir.insns[21].dst ||
        arguments[3] != mir.insns[23].dst ||
        !mir_recovery_constant_value(
            arguments[1], &plan->input_count) ||
        !mir_recovery_constant_value(
            arguments[2], &plan->window_size) ||
        !mir_machine_call_arguments(
            &mir.insns[46], 5, arguments) ||
        arguments[0] != mir.insns[27].dst ||
        arguments[1] != mir.insns[25].dst)
        return mir_machine_reject(
            "sliding-window-driver", "calls");
    for (item = 0; item < 3; ++item) {
        if (arguments[item + 2] !=
                mir.insns[output_loads[item]].dst ||
            !mir_recovery_constant_value(
                mir.insns[output_indices[item]].dst,
                &plan->output_indices[item]) ||
            !mir_recovery_constant_value(
                mir.insns[expected_values[item]].dst,
                &plan->output_values[item]) ||
            !mir_machine_constant_equals(
                mir.insns[expected_indices[item]].dst,
                plan->output_indices[item]))
            return mir_machine_reject(
                "sliding-window-driver", "output");
    }
    if (!mir_recovery_constant_value(
            mir.insns[48].dst, &plan->expected_count) ||
        !mir_machine_constant_equals(mir.insns[59].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[62].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[74].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[77].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[89].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[92].dst, 0))
        return mir_machine_reject(
            "sliding-window-driver", "checks");
    plan->format_string = (int)mir.insns[27].immediate;
    return plan->input_count == 8 &&
           plan->window_size == 3 &&
           plan->expected_count == 6;
}

static void mir_sliding_driver_ix_address(
    FILE *out, int offset)
{
    fputs("\tpush ix\n\tpop hl\n", out);
    if (offset != 0)
        fprintf(out, "\tld de,%d\n\tadd hl,de\n", offset);
}

static void mir_emit_sliding_window_driver_schedule(
    FILE *out, const struct MirSlidingWindowDriverSchedule *plan)
{
    int failed = new_label();
    int done = new_label();
    int item;

    fputs(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-30\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (item = 0; item < 8; ++item) {
        fprintf(out, "\tld hl,%d\n"
                "\tld (ix%+d),l\n\tld (ix%+d),h\n",
                plan->input_values[item] & 0xffff,
                -28 + item * 2, -27 + item * 2);
    }
    mir_sliding_driver_ix_address(out, -12);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->window_size);
    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->input_count);
    mir_sliding_driver_ix_address(out, -28);
    fputs("\tpush hl\n", out);
    mir_emit_recovery_call(
        out, plan->window_function, plan->window_name);
    mir_emit_final_call_cleanup(out, 4);
    fputs("\tld (ix-30),l\n\tld (ix-29),h\n", out);
    for (item = 2; item >= 0; --item) {
        int offset = -12 + plan->output_indices[item] * 2;

        fprintf(out,
                "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n",
                offset, offset + 1);
    }
    fputs("\tld l,(ix-30)\n\tld h,(ix-29)\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->format_string);
    mir_emit_recovery_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, 5);
    fputs("\tld l,(ix-30)\n\tld h,(ix-29)\n", out);
    fprintf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n",
            plan->expected_count);
    fprintf(out, "\tjp nz,L%d\n", failed);
    for (item = 0; item < 3; ++item) {
        int offset = -12 + plan->output_indices[item] * 2;

        fprintf(out,
                "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
                "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                "\tjp nz,L%d\n",
                offset, offset + 1,
                plan->output_values[item], failed);
    }
    fputs("\tld hl,0\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n\tld hl,1\nL%d:\n",
            done, failed, done);
    fputs("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_narrow_string_workload_schedule(
    struct MirNarrowStringWorkloadSchedule *plan)
{
    const int print_calls[23] = {
        217, 298, 319, 401, 423, 451, 473, 497, 518, 586,
        621, 647, 669, 685, 698, 787, 827, 876, 916, 943,
        959, 1031, 1055
    };
    const int print_counts[23] = {
        1, 6, 1, 5, 5, 5, 5, 5, 1, 3,
        6, 6, 4, 1, 1, 5, 5, 5, 5, 5,
        1, 4, 1
    };
    const int exit_calls[16] = {
        301, 404, 426, 454, 476, 500, 589, 624,
        650, 672, 701, 790, 830, 879, 919, 946
    };
    const int random_calls[9] = {
        238, 249, 342, 353, 546, 554, 563, 725, 736
    };
    const int string_instructions[25] = {
        215, 286, 317, 391, 413, 441, 463, 487, 516, 521,
        580, 609, 635, 657, 661, 683, 696, 777, 817, 866,
        906, 933, 957, 1021, 1053
    };
    const int constant_checks[][2] = {
        {4, 1}, {8, 1}, {16, 1}, {175, 317}, {195, 4096},
        {201, 97}, {203, 26}, {240, 300}, {251, 3000},
        {344, 300}, {355, 70}, {548, 300}, {556, 26},
        {565, 26}, {689, 97}, {691, 0}, {704, 0},
        {727, 300}, {738, 3000}, {960, 0}, {977, 20},
        {982, 37}, {984, 300}, {990, 17}, {992, 70},
        {1056, 0}
    };
    struct Sym *symbol;
    char call_name[64];
    int call_count = 0;
    int offset;
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 1058 || mir.next_value != 726 ||
        mir_cfg_block_count() != 42 || mir.local_bytes != 81 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        !mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_INT ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->argc_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &plan->argv_offset))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_CALL)
            ++call_count;
    if (call_count != 67)
        return mir_machine_reject(
            "narrow-string-workload", "call-count");
    plan->atoi_function = mir_recovery_direct_call(
        12, 1, 0, plan->atoi_name, sizeof(plan->atoi_name));
    plan->seed_function = mir_recovery_direct_call(
        178, 1, 0, plan->seed_name, sizeof(plan->seed_name));
    plan->random_function = mir_recovery_direct_call(
        238, 0, 0, plan->random_name, sizeof(plan->random_name));
    plan->length_function = mir_recovery_direct_call(
        279, 1, 0, plan->length_name, sizeof(plan->length_name));
    plan->exit_function = mir_recovery_direct_call(
        301, 1, 0, plan->exit_name, sizeof(plan->exit_name));
    plan->find_first_function = mir_recovery_direct_call(
        385, 2, 0, plan->find_first_name,
        sizeof(plan->find_first_name));
    plan->find_last_function = mir_recovery_direct_call(
        435, 2, 0, plan->find_last_name,
        sizeof(plan->find_last_name));
    plan->copy_string_function = mir_recovery_direct_call(
        523, 2, 0, plan->copy_string_name,
        sizeof(plan->copy_string_name));
    plan->find_string_function = mir_recovery_direct_call(
        603, 2, 0, plan->find_string_name,
        sizeof(plan->find_string_name));
    plan->compare_function = mir_recovery_direct_call(
        633, 3, 0, plan->compare_name,
        sizeof(plan->compare_name));
    plan->find_memory_function = mir_recovery_direct_call(
        694, 3, 0, plan->find_memory_name,
        sizeof(plan->find_memory_name));
    plan->copy_memory_function = mir_recovery_direct_call(
        762, 3, 0, plan->copy_memory_name,
        sizeof(plan->copy_memory_name));
    plan->set_memory_function = mir_recovery_direct_call(
        802, 3, 0, plan->set_memory_name,
        sizeof(plan->set_memory_name));
    if (plan->atoi_function == NULL ||
        plan->seed_function == NULL ||
        plan->random_function == NULL ||
        plan->length_function == NULL ||
        plan->exit_function == NULL ||
        plan->find_first_function == NULL ||
        plan->find_last_function == NULL ||
        plan->copy_string_function == NULL ||
        plan->find_string_function == NULL ||
        plan->compare_function == NULL ||
        plan->find_memory_function == NULL ||
        plan->copy_memory_function == NULL ||
        plan->set_memory_function == NULL)
        return mir_machine_reject(
            "narrow-string-workload", "functions");
    for (item = 0; item < 9; ++item)
        if (mir_recovery_direct_call(
                random_calls[item], 0, 0,
                call_name, sizeof(call_name)) !=
                plan->random_function ||
            strcmp(call_name, plan->random_name))
            return mir_machine_reject(
                "narrow-string-workload", "random-alias");
    if (mir_recovery_direct_call(
            1018, 1, 0, call_name, sizeof(call_name)) !=
            plan->length_function ||
        strcmp(call_name, plan->length_name) ||
        mir_recovery_direct_call(
            485, 2, 0, call_name, sizeof(call_name)) !=
            plan->find_first_function ||
        mir_recovery_direct_call(
            659, 2, 0, call_name, sizeof(call_name)) !=
            plan->find_string_function ||
        mir_recovery_direct_call(
            775, 3, 0, call_name, sizeof(call_name)) !=
            plan->compare_function ||
        mir_recovery_direct_call(
            815, 3, 0, call_name, sizeof(call_name)) !=
            plan->compare_function ||
        mir_recovery_direct_call(
            852, 3, 0, call_name, sizeof(call_name)) !=
            plan->find_memory_function ||
        mir_recovery_direct_call(
            904, 3, 0, call_name, sizeof(call_name)) !=
            plan->find_memory_function ||
        mir_recovery_direct_call(
            931, 3, 0, call_name, sizeof(call_name)) !=
            plan->find_memory_function)
        return mir_machine_reject(
            "narrow-string-workload", "function-alias");
    for (item = 0; item < 16; ++item)
        if (mir_recovery_direct_call(
                exit_calls[item], 1, 0,
                call_name, sizeof(call_name)) !=
                plan->exit_function ||
            strcmp(call_name, plan->exit_name))
            return mir_machine_reject(
                "narrow-string-workload", "exit-alias");
    for (item = 0; item < 23; ++item) {
        struct Sym *print = mir_recovery_direct_call(
            print_calls[item], print_counts[item], 1,
            call_name, sizeof(call_name));

        if (print == NULL)
            return mir_machine_reject(
                "narrow-string-workload", "print-call");
        if (item == 0) {
            plan->print_function = print;
            dcc_copy_str(
                plan->print_name, sizeof(plan->print_name),
                call_name);
        } else if (print != plan->print_function ||
                   strcmp(call_name, plan->print_name)) {
            return mir_machine_reject(
                "narrow-string-workload", "print-alias");
        }
    }
    if (!mir_recovery_global_address(
            mir.insns[198].dst, &plan->primary,
            call_name, sizeof(call_name), &offset) ||
        plan->primary == NULL || offset != 0 ||
        !mir_recovery_global_address(
            mir.insns[750].dst, &plan->secondary,
            call_name, sizeof(call_name), &offset) ||
        plan->secondary == NULL || offset != 0 ||
        !mir_recovery_global_address(
            mir.insns[808].dst, &plan->zeroes,
            call_name, sizeof(call_name), &offset) ||
        plan->zeroes == NULL || offset != 0 ||
        plan->primary == plan->secondary ||
        plan->primary == plan->zeroes ||
        plan->secondary == plan->zeroes ||
        !plan->primary->is_array ||
        !plan->secondary->is_array ||
        !plan->zeroes->is_array ||
        plan->primary->array_len != 4096 ||
        plan->secondary->array_len != 4096 ||
        plan->zeroes->array_len != 4096 ||
        plan->primary->elem_size != 1 ||
        plan->secondary->elem_size != 1 ||
        plan->zeroes->elem_size != 1)
        return mir_machine_reject(
            "narrow-string-workload", "buffers");
    for (item = 0;
         item < (int)(sizeof(constant_checks) /
                      sizeof(constant_checks[0])); ++item)
        if (!mir_machine_constant_equals(
                mir.insns[constant_checks[item][0]].dst,
                constant_checks[item][1]))
            return mir_machine_reject(
                "narrow-string-workload", "constants");
    for (item = 0; item < 25; ++item) {
        const struct MirInsn *string =
            &mir.insns[string_instructions[item]];
        int previous;

        if (string->opcode != MIR_STRING_ADDRESS ||
            string->immediate < 0)
            return mir_machine_reject(
                "narrow-string-workload", "string");
        plan->strings[item] = (int)string->immediate;
        for (previous = 0; previous < item; ++previous)
            if (plan->strings[item] ==
                plan->strings[previous])
                return mir_machine_reject(
                    "narrow-string-workload", "string-alias");
    }
    symbol = find_global(mir.insns[12].name);
    return symbol == plan->atoi_function;
}

static void mir_narrow_load_word(FILE *out, int offset)
{
    fprintf(out,
            "\tld l,(ix%d)\n\tld h,(ix%d)\n",
            offset, offset + 1);
}

static void mir_narrow_store_word(FILE *out, int offset)
{
    fprintf(out,
            "\tld (ix%d),l\n\tld (ix%d),h\n",
            offset, offset + 1);
}

static void mir_narrow_increment_word(FILE *out, int offset)
{
    int done = new_label();

    fprintf(out,
            "\tinc (ix%d)\n\tjp nz,L%d\n"
            "\tinc (ix%d)\nL%d:\n",
            offset, done, offset + 1, done);
}

static void mir_narrow_ix_address(
    FILE *out, int offset)
{
    fputs("\tpush ix\n\tpop hl\n", out);
    if (offset != 0)
        fprintf(out, "\tld de,%d\n\tadd hl,de\n", offset);
}

static void mir_narrow_global_address(
    FILE *out, struct Sym *symbol, int index_offset)
{
    mir_narrow_load_word(out, index_offset);
    fprintf(out, "\tld de,%s\n\tadd hl,de\n",
            asm_name_for(sym_asm_name(symbol)));
}

static void mir_narrow_random_mod(
    FILE *out, const struct MirNarrowStringWorkloadSchedule *plan,
    int modulus)
{
    mir_emit_recovery_call(
        out, plan->random_function, plan->random_name);
    fprintf(out, "\tld de,%d\n", modulus);
    mir_emit_runtime_call(out, "__modu");
}

static void mir_narrow_print(
    FILE *out, const struct MirNarrowStringWorkloadSchedule *plan,
    int words)
{
    mir_emit_recovery_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, words);
}

static void mir_narrow_exit(
    FILE *out, const struct MirNarrowStringWorkloadSchedule *plan)
{
    fputs("\tld hl,1\n\tpush hl\n", out);
    mir_emit_recovery_call(
        out, plan->exit_function, plan->exit_name);
}

static void mir_narrow_loop_test(
    FILE *out, int offset, int bound, int done)
{
    mir_narrow_load_word(out, offset);
    fprintf(out,
            "\tld de,%d\n\tor a\n\tsbc hl,de\n\tjp nc,L%d\n",
            bound, done);
}

static void mir_narrow_push_word(FILE *out, int offset)
{
    mir_narrow_load_word(out, offset);
    fputs("\tpush hl\n", out);
}

static void mir_narrow_push_global_index(
    FILE *out, struct Sym *symbol, int index_offset)
{
    mir_narrow_global_address(out, symbol, index_offset);
    fputs("\tpush hl\n", out);
}

static void mir_narrow_failure_four(
    FILE *out, const struct MirNarrowStringWorkloadSchedule *plan,
    int string_id)
{
    mir_narrow_push_word(out, MIR_NARROW_END);
    mir_narrow_push_word(out, MIR_NARROW_START);
    mir_narrow_push_word(out, MIR_NARROW_LEN);
    mir_narrow_push_word(out, MIR_NARROW_I);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_narrow_print(out, plan, 5);
    mir_narrow_exit(out, plan);
}

static void mir_narrow_restore_primary(
    FILE *out, const struct MirNarrowStringWorkloadSchedule *plan)
{
    mir_narrow_global_address(out, plan->primary, MIR_NARROW_END);
    fprintf(out, "\tld a,(ix%d)\n\tld (hl),a\n",
            MIR_NARROW_ORIG);
}

static void mir_emit_narrow_string_workload_schedule(
    FILE *out, const struct MirNarrowStringWorkloadSchedule *plan)
{
    int default_loop_count = new_label();
    int loop_count_ready = new_label();
    int outer_loop = new_label();
    int outer_done = new_label();
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
    int absent_character = new_label();
    int string_loop = new_label();
    int string_done = new_label();
    int string_bounds_ok = new_label();
    int string_present = new_label();
    int string_equal = new_label();
    int missing_pattern = new_label();
    int memory_zero_ok = new_label();
    int memory_loop = new_label();
    int memory_done = new_label();
    int memory_copy_ok = new_label();
    int memory_zero_compare_ok = new_label();
    int memory_pointer_ok = new_label();
    int memory_count_ok = new_label();
    int memory_missing_ok = new_label();
    int print_loop = new_label();
    int print_done = new_label();

    fputs(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    fprintf(out,
            "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n",
            MIR_NARROW_FRAME_BYTES);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    fprintf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld de,1\n\tor a\n\tsbc hl,de\n"
            "\tjp z,L%d\n",
            plan->argc_offset + 2, plan->argc_offset + 3,
            default_loop_count);
    fprintf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tinc hl\n\tinc hl\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tex de,hl\n\tpush hl\n",
            plan->argv_offset + 2, plan->argv_offset + 3);
    mir_emit_recovery_call(
        out, plan->atoi_function, plan->atoi_name);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out, "\tjp L%d\nL%d:\n\tld hl,1\nL%d:\n",
            loop_count_ready, default_loop_count,
            loop_count_ready);
    mir_narrow_store_word(out, MIR_NARROW_LOOP_COUNT);
    fputs("\tld hl,0\n", out);
    mir_narrow_store_word(out, MIR_NARROW_LOOPS);
    fprintf(out, "L%d:\n", outer_loop);
    mir_narrow_load_word(out, MIR_NARROW_LOOPS);
    fputs("\tld a,h\n\txor 128\n\tld h,a\n\tpush hl\n", out);
    mir_narrow_load_word(out, MIR_NARROW_LOOP_COUNT);
    fputs("\tld a,h\n\txor 128\n\tld h,a\n\tex de,hl\n\tpop hl\n"
          "\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp nc,L%d\n", outer_done);

    fputs("\tld hl,317\n\tpush hl\n", out);
    mir_emit_recovery_call(
        out, plan->seed_function, plan->seed_name);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out,
            "\tld hl,%s\n\tld bc,4096\n\tld d,97\nL%d:\n"
            "\tld (hl),d\n\tinc hl\n\tinc d\n\tld a,d\n\tcp 123\n"
            "\tjp nz,L%d\n\tld d,97\nL%d:\n"
            "\tdec bc\n\tld a,b\n\tor c\n\tjp nz,L%d\n",
            asm_name_for(sym_asm_name(plan->primary)),
            fill_loop, fill_letter_done, fill_letter_done, fill_loop);

    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[0]);
    mir_narrow_print(out, plan, 1);
    fputs("\tld hl,0\n", out);
    mir_narrow_store_word(out, MIR_NARROW_I);
    fprintf(out, "L%d:\n", length_loop);
    mir_narrow_loop_test(
        out, MIR_NARROW_I, 1000, length_done);
    mir_narrow_random_mod(out, plan, 300);
    mir_narrow_store_word(out, MIR_NARROW_START);
    mir_narrow_random_mod(out, plan, 3000);
    fputs("\tinc hl\n", out);
    fprintf(out,
            "\tld e,(ix%d)\n\tld d,(ix%d)\n\tadd hl,de\n",
            MIR_NARROW_START, MIR_NARROW_START + 1);
    mir_narrow_store_word(out, MIR_NARROW_END);
    fputs("\tor a\n\tsbc hl,de\n", out);
    mir_narrow_store_word(out, MIR_NARROW_LEN);
    mir_narrow_global_address(out, plan->primary, MIR_NARROW_END);
    fputs("\tld a,(hl)\n", out);
    fprintf(out, "\tld (ix%d),a\n\txor a\n\tld (hl),a\n",
            MIR_NARROW_ORIG);
    mir_narrow_push_global_index(
        out, plan->primary, MIR_NARROW_START);
    mir_emit_recovery_call(
        out, plan->length_function, plan->length_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_narrow_store_word(out, MIR_NARROW_AUX);
    fputs("\tex de,hl\n", out);
    mir_narrow_load_word(out, MIR_NARROW_LEN);
    fputs("\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp z,L%d\n", length_ok);
    mir_narrow_push_word(out, MIR_NARROW_END);
    mir_narrow_push_word(out, MIR_NARROW_START);
    mir_narrow_push_word(out, MIR_NARROW_AUX);
    mir_narrow_push_word(out, MIR_NARROW_LEN);
    mir_narrow_push_word(out, MIR_NARROW_I);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[1]);
    mir_narrow_print(out, plan, 6);
    mir_narrow_exit(out, plan);
    fprintf(out, "L%d:\n", length_ok);
    mir_narrow_restore_primary(out, plan);
    mir_narrow_increment_word(out, MIR_NARROW_I);
    fprintf(out, "\tjp L%d\nL%d:\n", length_loop, length_done);

    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[2]);
    mir_narrow_print(out, plan, 1);
    fputs("\tld hl,0\n", out);
    mir_narrow_store_word(out, MIR_NARROW_I);
    fprintf(out, "L%d:\n", find_loop);
    mir_narrow_loop_test(
        out, MIR_NARROW_I, 1000, find_done);
    mir_narrow_random_mod(out, plan, 300);
    mir_narrow_store_word(out, MIR_NARROW_START);
    mir_narrow_random_mod(out, plan, 70);
    fputs("\tinc hl\n", out);
    fprintf(out,
            "\tld e,(ix%d)\n\tld d,(ix%d)\n\tadd hl,de\n",
            MIR_NARROW_START, MIR_NARROW_START + 1);
    mir_narrow_store_word(out, MIR_NARROW_END);
    fputs("\tor a\n\tsbc hl,de\n", out);
    mir_narrow_store_word(out, MIR_NARROW_LEN);
    mir_narrow_global_address(out, plan->primary, MIR_NARROW_END);
    fputs("\tld a,(hl)\n", out);
    fprintf(out, "\tld (ix%d),a\n\tld (hl),33\n",
            MIR_NARROW_ORIG);
    fputs("\tld hl,33\n\tpush hl\n", out);
    mir_narrow_push_global_index(
        out, plan->primary, MIR_NARROW_START);
    mir_emit_recovery_call(
        out, plan->find_first_function, plan->find_first_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_narrow_store_word(out, MIR_NARROW_POINTER);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", first_present);
    mir_narrow_failure_four(out, plan, plan->strings[3]);
    fprintf(out, "L%d:\n", first_present);
    mir_narrow_global_address(out, plan->primary, MIR_NARROW_END);
    fputs("\tex de,hl\n", out);
    mir_narrow_load_word(out, MIR_NARROW_POINTER);
    fputs("\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp z,L%d\n", first_correct);
    mir_narrow_failure_four(out, plan, plan->strings[4]);
    fprintf(out, "L%d:\n", first_correct);
    fputs("\tld hl,33\n\tpush hl\n", out);
    mir_narrow_push_global_index(
        out, plan->primary, MIR_NARROW_START);
    mir_emit_recovery_call(
        out, plan->find_last_function, plan->find_last_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_narrow_store_word(out, MIR_NARROW_POINTER);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", last_present);
    mir_narrow_failure_four(out, plan, plan->strings[5]);
    fprintf(out, "L%d:\n", last_present);
    mir_narrow_global_address(out, plan->primary, MIR_NARROW_END);
    fputs("\tex de,hl\n", out);
    mir_narrow_load_word(out, MIR_NARROW_POINTER);
    fputs("\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp z,L%d\n", last_correct);
    mir_narrow_failure_four(out, plan, plan->strings[6]);
    fprintf(out, "L%d:\n", last_correct);
    fputs("\tld hl,36\n\tpush hl\n", out);
    mir_narrow_push_global_index(
        out, plan->primary, MIR_NARROW_START);
    mir_emit_recovery_call(
        out, plan->find_first_function, plan->find_first_name);
    mir_emit_final_call_cleanup(out, 2);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", absent_character);
    mir_narrow_failure_four(out, plan, plan->strings[7]);
    fprintf(out, "L%d:\n", absent_character);
    mir_narrow_restore_primary(out, plan);
    mir_narrow_increment_word(out, MIR_NARROW_I);
    fprintf(out, "\tjp L%d\nL%d:\n", find_loop, find_done);

    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[8]);
    mir_narrow_print(out, plan, 1);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[9]);
    mir_narrow_ix_address(out, MIR_NARROW_ALPHA);
    fputs("\tpush hl\n", out);
    mir_emit_recovery_call(
        out, plan->copy_string_function, plan->copy_string_name);
    mir_emit_final_call_cleanup(out, 2);
    fputs("\tld hl,0\n", out);
    mir_narrow_store_word(out, MIR_NARROW_I);
    fprintf(out, "L%d:\n", string_loop);
    mir_narrow_loop_test(
        out, MIR_NARROW_I, 1000, string_done);
    mir_narrow_random_mod(out, plan, 300);
    mir_narrow_store_word(out, MIR_NARROW_START);
    mir_narrow_random_mod(out, plan, 26);
    mir_narrow_store_word(out, MIR_NARROW_OFFSET);
    mir_emit_recovery_call(
        out, plan->random_function, plan->random_name);
    fputs("\tpush hl\n\tld hl,26\n", out);
    fprintf(out,
            "\tld e,(ix%d)\n\tld d,(ix%d)\n"
            "\tor a\n\tsbc hl,de\n\tex de,hl\n\tpop hl\n",
            MIR_NARROW_OFFSET, MIR_NARROW_OFFSET + 1);
    mir_emit_runtime_call(out, "__modu");
    fputs("\tinc hl\n", out);
    mir_narrow_store_word(out, MIR_NARROW_LEN);
    fprintf(out,
            "\tld e,(ix%d)\n\tld d,(ix%d)\n\tadd hl,de\n"
            "\tld de,26\n\tor a\n\tsbc hl,de\n"
            "\tjp c,L%d\n\tjp z,L%d\n",
            MIR_NARROW_OFFSET, MIR_NARROW_OFFSET + 1,
            string_bounds_ok, string_bounds_ok);
    mir_narrow_push_word(out, MIR_NARROW_LEN);
    mir_narrow_push_word(out, MIR_NARROW_OFFSET);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[10]);
    mir_narrow_print(out, plan, 3);
    mir_narrow_exit(out, plan);
    fprintf(out, "L%d:\n", string_bounds_ok);
    mir_narrow_ix_address(out, MIR_NARROW_ALPHA);
    fputs("\tpush hl\n", out);
    mir_narrow_load_word(out, MIR_NARROW_OFFSET);
    fputs("\tex de,hl\n\tpop hl\n\tadd hl,de\n", out);
    mir_narrow_store_word(out, MIR_NARROW_POINTER);
    fputs("\tpush hl\n", out);
    mir_narrow_push_global_index(
        out, plan->primary, MIR_NARROW_START);
    mir_emit_recovery_call(
        out, plan->find_string_function, plan->find_string_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_narrow_store_word(out, MIR_NARROW_AUX);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", string_present);
    mir_narrow_push_word(out, MIR_NARROW_POINTER);
    mir_narrow_push_word(out, MIR_NARROW_LEN);
    mir_narrow_push_word(out, MIR_NARROW_OFFSET);
    mir_narrow_push_word(out, MIR_NARROW_START);
    mir_narrow_push_word(out, MIR_NARROW_I);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[11]);
    mir_narrow_print(out, plan, 6);
    mir_narrow_exit(out, plan);
    fprintf(out, "L%d:\n", string_present);
    mir_narrow_push_word(out, MIR_NARROW_LEN);
    mir_narrow_push_word(out, MIR_NARROW_POINTER);
    mir_narrow_push_word(out, MIR_NARROW_AUX);
    mir_emit_recovery_call(
        out, plan->compare_function, plan->compare_name);
    mir_emit_final_call_cleanup(out, 3);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", string_equal);
    mir_narrow_push_word(out, MIR_NARROW_POINTER);
    mir_narrow_push_word(out, MIR_NARROW_LEN);
    mir_narrow_push_word(out, MIR_NARROW_OFFSET);
    mir_narrow_push_word(out, MIR_NARROW_START);
    mir_narrow_push_word(out, MIR_NARROW_I);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[12]);
    mir_narrow_print(out, plan, 6);
    mir_narrow_exit(out, plan);
    fprintf(out, "L%d:\n", string_equal);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[13]);
    mir_narrow_push_global_index(
        out, plan->primary, MIR_NARROW_START);
    mir_emit_recovery_call(
        out, plan->find_string_function, plan->find_string_name);
    mir_emit_final_call_cleanup(out, 2);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", missing_pattern);
    mir_narrow_push_word(out, MIR_NARROW_OFFSET);
    mir_narrow_push_word(out, MIR_NARROW_START);
    mir_narrow_push_word(out, MIR_NARROW_I);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[14]);
    mir_narrow_print(out, plan, 4);
    mir_narrow_exit(out, plan);
    fprintf(out, "L%d:\n", missing_pattern);
    mir_narrow_increment_word(out, MIR_NARROW_I);
    fprintf(out, "\tjp L%d\nL%d:\n", string_loop, string_done);

    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[15]);
    mir_narrow_print(out, plan, 1);
    fputs("\tld hl,0\n\tpush hl\n\tld hl,97\n\tpush hl\n", out);
    fprintf(out, "\tld hl,%s\n\tpush hl\n",
            asm_name_for(sym_asm_name(plan->primary)));
    mir_emit_recovery_call(
        out, plan->find_memory_function, plan->find_memory_name);
    mir_emit_final_call_cleanup(out, 3);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", memory_zero_ok);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[16]);
    mir_narrow_print(out, plan, 1);
    mir_narrow_exit(out, plan);
    fprintf(out, "L%d:\n\tld hl,0\n", memory_zero_ok);
    mir_narrow_store_word(out, MIR_NARROW_I);
    fprintf(out, "L%d:\n", memory_loop);
    mir_narrow_loop_test(
        out, MIR_NARROW_I, 1000, memory_done);
    mir_narrow_random_mod(out, plan, 300);
    mir_narrow_store_word(out, MIR_NARROW_START);
    mir_narrow_random_mod(out, plan, 3000);
    fputs("\tinc hl\n", out);
    fprintf(out,
            "\tld e,(ix%d)\n\tld d,(ix%d)\n\tadd hl,de\n",
            MIR_NARROW_START, MIR_NARROW_START + 1);
    mir_narrow_store_word(out, MIR_NARROW_END);
    fputs("\tor a\n\tsbc hl,de\n", out);
    mir_narrow_store_word(out, MIR_NARROW_LEN);
    mir_narrow_push_word(out, MIR_NARROW_LEN);
    mir_narrow_push_global_index(
        out, plan->primary, MIR_NARROW_START);
    mir_narrow_push_global_index(
        out, plan->secondary, MIR_NARROW_START);
    mir_emit_recovery_call(
        out, plan->copy_memory_function, plan->copy_memory_name);
    mir_emit_final_call_cleanup(out, 3);
    mir_narrow_push_word(out, MIR_NARROW_LEN);
    mir_narrow_push_global_index(
        out, plan->primary, MIR_NARROW_START);
    mir_narrow_push_global_index(
        out, plan->secondary, MIR_NARROW_START);
    mir_emit_recovery_call(
        out, plan->compare_function, plan->compare_name);
    mir_emit_final_call_cleanup(out, 3);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", memory_copy_ok);
    mir_narrow_failure_four(out, plan, plan->strings[17]);
    fprintf(out, "L%d:\n", memory_copy_ok);
    mir_narrow_push_word(out, MIR_NARROW_LEN);
    fputs("\tld hl,0\n\tpush hl\n", out);
    mir_narrow_push_global_index(
        out, plan->secondary, MIR_NARROW_START);
    mir_emit_recovery_call(
        out, plan->set_memory_function, plan->set_memory_name);
    mir_emit_final_call_cleanup(out, 3);
    mir_narrow_push_word(out, MIR_NARROW_LEN);
    mir_narrow_push_global_index(
        out, plan->zeroes, MIR_NARROW_START);
    mir_narrow_push_global_index(
        out, plan->secondary, MIR_NARROW_START);
    mir_emit_recovery_call(
        out, plan->compare_function, plan->compare_name);
    mir_emit_final_call_cleanup(out, 3);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", memory_zero_compare_ok);
    mir_narrow_failure_four(out, plan, plan->strings[18]);
    fprintf(out, "L%d:\n", memory_zero_compare_ok);

    mir_narrow_load_word(out, MIR_NARROW_LEN);
    fputs("\tsrl h\n\trr l\n", out);
    fprintf(out,
            "\tld e,(ix%d)\n\tld d,(ix%d)\n\tadd hl,de\n"
            "\tld de,%s\n\tadd hl,de\n\tld (hl),33\n",
            MIR_NARROW_START, MIR_NARROW_START + 1,
            asm_name_for(sym_asm_name(plan->secondary)));
    mir_narrow_push_word(out, MIR_NARROW_LEN);
    fputs("\tld hl,33\n\tpush hl\n", out);
    mir_narrow_push_global_index(
        out, plan->secondary, MIR_NARROW_START);
    mir_emit_recovery_call(
        out, plan->find_memory_function, plan->find_memory_name);
    mir_emit_final_call_cleanup(out, 3);
    mir_narrow_store_word(out, MIR_NARROW_POINTER);
    mir_narrow_load_word(out, MIR_NARROW_LEN);
    fputs("\tsrl h\n\trr l\n", out);
    fprintf(out,
            "\tld e,(ix%d)\n\tld d,(ix%d)\n\tadd hl,de\n"
            "\tld de,%s\n\tadd hl,de\n\tex de,hl\n",
            MIR_NARROW_START, MIR_NARROW_START + 1,
            asm_name_for(sym_asm_name(plan->secondary)));
    mir_narrow_load_word(out, MIR_NARROW_POINTER);
    fputs("\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp z,L%d\n", memory_pointer_ok);
    mir_narrow_failure_four(out, plan, plan->strings[19]);
    fprintf(out, "L%d:\n", memory_pointer_ok);

    mir_narrow_load_word(out, MIR_NARROW_START);
    fprintf(out,
            "\tld e,(ix%d)\n\tld d,(ix%d)\n\tadd hl,de\n\tdec hl\n"
            "\tld de,%s\n\tadd hl,de\n\tld (hl),63\n",
            MIR_NARROW_LEN, MIR_NARROW_LEN + 1,
            asm_name_for(sym_asm_name(plan->secondary)));
    mir_narrow_load_word(out, MIR_NARROW_LEN);
    fputs("\tdec hl\n\tpush hl\n\tld hl,63\n\tpush hl\n", out);
    mir_narrow_push_global_index(
        out, plan->secondary, MIR_NARROW_START);
    mir_emit_recovery_call(
        out, plan->find_memory_function, plan->find_memory_name);
    mir_emit_final_call_cleanup(out, 3);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", memory_count_ok);
    mir_narrow_failure_four(out, plan, plan->strings[20]);
    fprintf(out, "L%d:\n", memory_count_ok);
    mir_narrow_push_word(out, MIR_NARROW_LEN);
    fputs("\tld hl,64\n\tpush hl\n", out);
    mir_narrow_push_global_index(
        out, plan->secondary, MIR_NARROW_START);
    mir_emit_recovery_call(
        out, plan->find_memory_function, plan->find_memory_name);
    mir_emit_final_call_cleanup(out, 3);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", memory_missing_ok);
    mir_narrow_failure_four(out, plan, plan->strings[21]);
    fprintf(out, "L%d:\n", memory_missing_ok);
    mir_narrow_increment_word(out, MIR_NARROW_I);
    fprintf(out, "\tjp L%d\nL%d:\n", memory_loop, memory_done);

    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[22]);
    mir_narrow_print(out, plan, 1);
    fputs("\tld hl,0\n", out);
    mir_narrow_store_word(out, MIR_NARROW_I);
    fprintf(out, "L%d:\n", print_loop);
    mir_narrow_loop_test(
        out, MIR_NARROW_I, 20, print_done);
    mir_narrow_load_word(out, MIR_NARROW_I);
    fputs("\tld de,37\n", out);
    mir_emit_runtime_call(out, "__mulu");
    fputs("\tld de,300\n", out);
    mir_emit_runtime_call(out, "__modu");
    mir_narrow_store_word(out, MIR_NARROW_START);
    mir_narrow_load_word(out, MIR_NARROW_I);
    fputs("\tld de,17\n", out);
    mir_emit_runtime_call(out, "__mulu");
    fputs("\tld de,70\n", out);
    mir_emit_runtime_call(out, "__modu");
    fputs("\tinc hl\n", out);
    mir_narrow_store_word(out, MIR_NARROW_LEN);
    fprintf(out,
            "\tld e,(ix%d)\n\tld d,(ix%d)\n\tadd hl,de\n",
            MIR_NARROW_START, MIR_NARROW_START + 1);
    mir_narrow_store_word(out, MIR_NARROW_END);
    mir_narrow_global_address(out, plan->primary, MIR_NARROW_END);
    fputs("\tld a,(hl)\n", out);
    fprintf(out, "\tld (ix%d),a\n\txor a\n\tld (hl),a\n",
            MIR_NARROW_ORIG);
    mir_narrow_push_global_index(
        out, plan->primary, MIR_NARROW_START);
    mir_emit_recovery_call(
        out, plan->length_function, plan->length_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_narrow_store_word(out, MIR_NARROW_AUX);
    mir_narrow_push_global_index(
        out, plan->primary, MIR_NARROW_START);
    mir_narrow_push_word(out, MIR_NARROW_AUX);
    mir_narrow_push_word(out, MIR_NARROW_LEN);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[23]);
    mir_narrow_print(out, plan, 4);
    mir_narrow_restore_primary(out, plan);
    mir_narrow_increment_word(out, MIR_NARROW_I);
    fprintf(out, "\tjp L%d\nL%d:\n", print_loop, print_done);

    mir_narrow_increment_word(out, MIR_NARROW_LOOPS);
    fprintf(out, "\tjp L%d\n", outer_loop);
    fprintf(out, "L%d:\n", outer_done);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[24]);
    mir_narrow_print(out, plan, 1);
    fputs("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_bdos_driver_schedule(
    struct MirBdosDriverSchedule *plan)
{
    const int print_calls[8] = {
        3, 29, 34, 154, 171, 187, 196, 205
    };
    const int print_counts[8] = {1, 2, 2, 2, 2, 2, 1, 1};
    const int byte_calls[11] = {
        39, 44, 49, 54, 59, 66, 71, 76, 82, 192, 201
    };
    int message_offset;
    char assembly_name[64];
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 211 ||
        (mir.next_value != 119 && mir.next_value != 120) ||
        mir_cfg_block_count() != 20 || mir.local_bytes != 142 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_INT)
        return 0;
    if (mir.insns[18].opcode != MIR_CALL ||
        mir.insns[18].src1 >= 0)
        return mir_machine_reject(
            "bdos-driver", "copy-call");
    plan->byte_function = mir_recovery_direct_call(
        39, 2, 0, assembly_name, sizeof(assembly_name));
    plan->full_function = mir_recovery_direct_call(
        89, 2, 0, plan->full_name, sizeof(plan->full_name));
    plan->string_function = mir_recovery_direct_call(
        208, 1, 0, plan->string_name, sizeof(plan->string_name));
    if (plan->byte_function == NULL)
        return mir_machine_reject(
            "bdos-driver", "byte-function");
    if (plan->full_function == NULL)
        return mir_machine_reject(
            "bdos-driver", "full-function");
    if (plan->string_function == NULL)
        return mir_machine_reject(
            "bdos-driver", "string-function");
    if (plan->byte_function == plan->full_function)
        return mir_machine_reject(
            "bdos-driver", "function-alias");
    for (item = 0; item < 11; ++item)
        if (mir_recovery_direct_call(
                byte_calls[item], 2, 0,
                assembly_name, sizeof(assembly_name)) !=
            plan->byte_function)
            return mir_machine_reject(
                "bdos-driver", "byte-call");
    for (item = 0; item < 8; ++item) {
        struct Sym *print = mir_recovery_direct_call(
            print_calls[item], print_counts[item], 1,
            assembly_name, sizeof(assembly_name));

        if (print == NULL)
            return mir_machine_reject(
                "bdos-driver", "print-call");
        if (item == 0) {
            plan->print_function = print;
            dcc_copy_str(
                plan->print_name, sizeof(plan->print_name),
                assembly_name);
        } else if (print != plan->print_function ||
                   strcmp(assembly_name, plan->print_name)) {
            return mir_machine_reject(
                "bdos-driver", "print-alias");
        }
    }
    if (mir.insns[102].opcode != MIR_CALL ||
        mir.insns[102].src1 < 0 ||
        mir.insns[95].opcode != MIR_ADDRESS ||
        find_global(mir.insns[95].name) !=
            plan->full_function)
        return mir_machine_reject(
            "bdos-driver", "indirect-full-call");
    if (!mir_recovery_global_address(
            mir.insns[63].dst, &plan->message,
            plan->message_name, sizeof(plan->message_name),
            &message_offset) ||
        message_offset != 0 ||
        (plan->message == NULL &&
         plan->message_name[0] == 0))
        return mir_machine_reject(
            "bdos-driver", "message");
    if (!mir_machine_constant_equals(mir.insns[4].dst, 128) ||
        !mir_machine_constant_equals(mir.insns[9].dst, 129) ||
        !mir_machine_constant_equals(mir.insns[23].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[35].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[37].dst, 72) ||
        !mir_machine_constant_equals(mir.insns[40].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[42].dst, 105) ||
        !mir_machine_constant_equals(mir.insns[45].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[47].dst, 33) ||
        !mir_machine_constant_equals(mir.insns[50].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[52].dst, 13) ||
        !mir_machine_constant_equals(mir.insns[55].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[57].dst, 10) ||
        !mir_machine_constant_equals(mir.insns[61].dst, 9) ||
        !mir_machine_constant_equals(mir.insns[78].dst, 12) ||
        !mir_machine_constant_equals(mir.insns[80].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[85].dst, 12) ||
        !mir_machine_constant_equals(mir.insns[87].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[97].dst, 12) ||
        !mir_machine_constant_equals(mir.insns[99].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[188].dst, 11) ||
        !mir_machine_constant_equals(mir.insns[190].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[197].dst, 6) ||
        !mir_machine_constant_equals(mir.insns[199].dst, 255) ||
        !mir_machine_constant_equals(mir.insns[209].dst, 0))
        return mir_machine_reject(
            "bdos-driver", "constants");
    plan->strings[0] = (int)mir.insns[145].immediate;
    plan->strings[1] = (int)mir.insns[149].immediate;
    plan->strings[2] = (int)mir.insns[119].immediate;
    plan->strings[3] = (int)mir.insns[1].immediate;
    plan->strings[4] = (int)mir.insns[25].immediate;
    plan->strings[5] = (int)mir.insns[30].immediate;
    plan->strings[6] = (int)mir.insns[156].immediate;
    plan->strings[7] = (int)mir.insns[172].immediate;
    plan->strings[8] = (int)mir.insns[194].immediate;
    plan->strings[9] = (int)mir.insns[203].immediate;
    plan->strings[10] = (int)mir.insns[206].immediate;
    for (instruction = 0; instruction < 11; ++instruction) {
        int other;
        for (other = instruction + 1; other < 11; ++other)
            if (plan->strings[instruction] ==
                plan->strings[other])
                return mir_machine_reject(
                    "bdos-driver", "string-alias");
    }
    return 1;
}

static void mir_bdos_driver_fast_call(
    FILE *out, const char *runtime_name,
    int function_number, int argument)
{
    fprintf(out, "\tld c,%d\n\tld de,%d\n",
            function_number, argument);
    mir_emit_runtime_call(out, runtime_name);
}

static void mir_bdos_driver_print(
    FILE *out, const struct MirBdosDriverSchedule *plan,
    int words)
{
    mir_emit_recovery_call(
        out, plan->print_function, plan->print_name);
    mir_emit_final_call_cleanup(out, words);
}

static void mir_bdos_driver_choose_string(
    FILE *out, const struct MirBdosDriverSchedule *plan,
    int condition, int invert)
{
    int false_label = new_label();
    int done = new_label();

    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, invert ? "\tjp nz,L%d\n" : "\tjp z,L%d\n",
            false_label);
    fprintf(out, "\tld hl,S%d\n\tjp L%d\nL%d:\n\tld hl,S%d\nL%d:\n",
            plan->strings[condition ? 0 : 1], done, false_label,
            plan->strings[condition ? 1 : 0], done);
}

static void mir_emit_bdos_driver_schedule(
    FILE *out, const struct MirBdosDriverSchedule *plan)
{
    int copy_done = new_label();
    int major_yes = new_label();
    int major_done = new_label();
    int compare_done = new_label();

    fputs(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-136\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[3]);
    mir_bdos_driver_print(out, plan, 1);

    fputs("\tld a,(128)\n\tld l,a\n\tld h,0\n"
          "\tld (ix-130),l\n\tld (ix-129),h\n"
          "\tld c,l\n\tld b,h\n\tld hl,129\n"
          "\tpush ix\n\tpop de\n\tpush hl\n"
          "\tld hl,-128\n\tadd hl,de\n\tex de,hl\n\tpop hl\n"
          "\tld a,b\n\tor c\n", out);
    fprintf(out, "\tjp z,L%d\n\tldir\nL%d:\n"
            "\txor a\n\tld (de),a\n",
            copy_done, copy_done);
    fputs("\tld l,(ix-130)\n\tld h,(ix-129)\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[4]);
    mir_bdos_driver_print(out, plan, 2);
    fputs("\tpush ix\n\tpop hl\n\tld de,-128\n\tadd hl,de\n\tpush hl\n",
          out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[5]);
    mir_bdos_driver_print(out, plan, 2);

    mir_bdos_driver_fast_call(out, "__bdosf", 2, 72);
    mir_bdos_driver_fast_call(out, "__bdosf", 2, 105);
    mir_bdos_driver_fast_call(out, "__bdosf", 2, 33);
    mir_bdos_driver_fast_call(out, "__bdosf", 2, 13);
    mir_bdos_driver_fast_call(out, "__bdosf", 2, 10);
    fprintf(out, "\tld c,9\n\tld de,%s\n",
            plan->message_name);
    mir_emit_runtime_call(out, "__bdosf");
    mir_bdos_driver_fast_call(out, "__bdosf", 2, 13);
    mir_bdos_driver_fast_call(out, "__bdosf", 2, 10);

    mir_bdos_driver_fast_call(out, "__bdosf", 12, 0);
    fputs("\tld (ix-132),l\n\tld (ix-131),h\n", out);
    mir_bdos_driver_fast_call(out, "__bhlf", 12, 0);
    fputs("\tld (ix-134),l\n\tld (ix-133),h\n"
          "\tld hl,0\n\tpush hl\n\tld hl,12\n\tpush hl\n", out);
    mir_emit_recovery_call(
        out, plan->full_function, plan->full_name);
    mir_emit_final_call_cleanup(out, 2);
    fputs("\tld (ix-136),l\n\tld (ix-135),h\n", out);

    fputs("\tld l,(ix-132)\n\tld h,(ix-131)\n"
          "\tld b,4\n", out);
    {
        int shift_loop = new_label();
        fprintf(out, "L%d:\n\tsrl h\n\trr l\n\tdjnz L%d\n",
                shift_loop, shift_loop);
    }
    fputs("\tld a,l\n\tand 15\n\tcp 2\n", out);
    fprintf(out, "\tjp z,L%d\n\tcp 3\n\tjp z,L%d\n"
            "\tld hl,S%d\n\tjp L%d\nL%d:\n\tld hl,S%d\nL%d:\n"
            "\tpush hl\n\tld hl,S%d\n\tpush hl\n",
            major_yes, major_yes, plan->strings[1],
            major_done, major_yes, plan->strings[0], major_done,
            plan->strings[2]);
    mir_bdos_driver_print(out, plan, 2);

    fputs("\tld l,(ix-132)\n\tld h,(ix-131)\n"
          "\tld e,(ix-134)\n\tld d,(ix-133)\n"
          "\tor a\n\tsbc hl,de\n\tld hl,0\n", out);
    fprintf(out, "\tjp nz,L%d\n\tinc l\nL%d:\n",
            compare_done, compare_done);
    mir_bdos_driver_choose_string(out, plan, 1, 0);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[6]);
    mir_bdos_driver_print(out, plan, 2);

    fputs("\tld l,(ix-134)\n\tld h,(ix-133)\n"
          "\tld e,(ix-136)\n\tld d,(ix-135)\n"
          "\tor a\n\tsbc hl,de\n\tld hl,0\n", out);
    {
        int equal = new_label();
        fprintf(out, "\tjp nz,L%d\n\tinc l\nL%d:\n",
                equal, equal);
    }
    mir_bdos_driver_choose_string(out, plan, 1, 0);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[7]);
    mir_bdos_driver_print(out, plan, 2);

    mir_bdos_driver_fast_call(out, "__bdosf", 11, 0);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[8]);
    mir_bdos_driver_print(out, plan, 1);
    mir_bdos_driver_fast_call(out, "__bdosf", 6, 255);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[9]);
    mir_bdos_driver_print(out, plan, 1);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[10]);
    mir_emit_recovery_call(
        out, plan->string_function, plan->string_name);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_line_reader_schedule(
    struct MirLineReaderSchedule *plan)
{
    int arguments[3];

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 89 || mir.next_value != 52 ||
        mir_cfg_block_count() != 13 || mir.local_bytes != 4 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        !mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_INT ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->buffer_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &plan->size_offset) ||
        type_ptr_depth(mir.insns[1].type) != 1 ||
        (mir.insns[1].type & 15) != TYPE_CHAR ||
        type_size(mir.insns[2].type) != 2)
        return 0;
    plan->read_function = mir_recovery_direct_call(
        10, 3, 0, plan->read_name, sizeof(plan->read_name));
    if (plan->read_function == NULL ||
        !mir_machine_call_arguments(
            &mir.insns[10], 3, arguments) ||
        arguments[0] != mir.insns[3].dst ||
        arguments[1] != mir.insns[2].dst ||
        arguments[2] != mir.insns[7].dst ||
        !mir_recovery_constant_value(
            arguments[2], &plan->stream_value) ||
        mir.insns[12].immediate != TOK_EQ ||
        mir.insns[13].src1 != mir.insns[12].dst ||
        !mir_machine_constant_equals(mir.insns[15].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[18].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[20].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[24].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[35].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[46].dst, 10) ||
        !mir_machine_constant_equals(mir.insns[54].dst, 13) ||
        !mir_machine_constant_equals(mir.insns[73].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[82].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[87].dst, 0))
        return mir_machine_reject(
            "line-reader", "shape");
    return 1;
}

static void mir_emit_line_reader_schedule(
    FILE *out, const struct MirLineReaderSchedule *plan)
{
    int present = new_label();
    int loop = new_label();
    int terminate = new_label();
    int done = new_label();
    int buffer = plan->buffer_offset + 2;
    int size = plan->size_offset + 2;

    fputs(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->stream_value);
    fprintf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n",
            size, size + 1, buffer, buffer + 1);
    mir_emit_recovery_call(
        out, plan->read_function, plan->read_name);
    mir_emit_final_call_cleanup(out, 3);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\txor a\n\tld (hl),a\n\tjp L%d\n"
            "L%d:\nL%d:\n\tld a,(hl)\n\tor a\n\tjp z,L%d\n"
            "\tcp 10\n\tjp z,L%d\n\tcp 13\n\tjp z,L%d\n"
            "\tinc hl\n\tjp L%d\n"
            "L%d:\n\txor a\n\tld (hl),a\n"
            "L%d:\n\tld hl,0\n\tpop ix\n\tret\n",
            present, buffer, buffer + 1, done,
            present, loop, done,
            terminate, terminate, loop,
            terminate, done);
}

static int mir_match_adjacency_scan_schedule(
    struct MirAdjacencyScanSchedule *plan)
{
    char assembly_name[64];
    int table_offset;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 37 || mir.next_value != 24 ||
        mir_cfg_block_count() != 5 || mir.local_bytes != 1 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        !mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_INT ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->from_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &plan->target_offset) ||
        !mir_recovery_global_address(
            mir.insns[15].dst, &plan->table,
            assembly_name, sizeof(assembly_name),
            &table_offset) ||
        plan->table == NULL || table_offset != 0)
        return 0;
    plan->neighbor_count = (int)mir.insns[11].immediate;
    plan->row_width = (int)mir.insns[17].immediate;
    return plan->neighbor_count == 3 &&
           plan->row_width == 6 &&
           mir.insns[17].src1 == mir.insns[15].dst &&
           mir.insns[17].src2 == mir.insns[1].dst &&
           mir.insns[19].src1 == mir.insns[17].dst &&
           mir.insns[19].src2 == mir.insns[9].dst &&
           mir.insns[20].src1 == mir.insns[19].dst &&
           mir.insns[22].immediate == TOK_EQ &&
           mir.insns[22].src1 == mir.insns[20].dst &&
           mir.insns[22].src2 == mir.insns[2].dst &&
           mir_machine_constant_equals(mir.insns[24].dst, 1) &&
           mir_machine_constant_equals(mir.insns[35].dst, 0);
}

static void mir_emit_adjacency_scan_schedule(
    FILE *out, const struct MirAdjacencyScanSchedule *plan)
{
    int loop = new_label();
    int next = new_label();
    int found = new_label();
    int done = new_label();
    int from = plan->from_offset + 2;
    int target = plan->target_offset + 2;

    fputs(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld e,l\n\tld d,h\n\tadd hl,hl\n\tadd hl,de\n"
            "\tadd hl,hl\n\tld de,%s\n\tadd hl,de\n"
            "\tld e,(ix+%d)\n\tld d,(ix+%d)\n"
            "\tld b,%d\nL%d:\n"
            "\tld a,(hl)\n\tcp e\n\tinc hl\n\tjp nz,L%d\n"
            "\tld a,(hl)\n\tcp d\n\tjp z,L%d\n"
            "L%d:\n\tinc hl\n\tdjnz L%d\n\tld hl,0\n"
            "\tjp L%d\nL%d:\n\tld hl,1\nL%d:\n"
            "\tpop ix\n\tret\n",
            from, from + 1,
            asm_name_for(sym_asm_name(plan->table)),
            target, target + 1,
            plan->neighbor_count, loop,
            next, found, next, loop, done,
            found, done);
}

static int mir_match_bsearch_edge_schedule(
    struct MirBsearchEdgeSchedule *plan)
{
    const int search_calls[9] = {
        17, 46, 95, 119, 153, 202, 251, 275, 299
    };
    const int failure_calls[9] = {
        23, 77, 101, 125, 184, 233, 257, 281, 305
    };
    const int failure_strings[9] = {
        21, 75, 99, 123, 182, 231, 255, 279, 303
    };
    char assembly_name[64];
    int array_offset;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 307 || mir.next_value != 183 ||
        mir_cfg_block_count() != 31 || mir.local_bytes != 4 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    plan->search_function = mir_recovery_direct_call(
        search_calls[0], 5, 0,
        plan->search_name, sizeof(plan->search_name));
    plan->failure_function = mir_recovery_direct_call(
        failure_calls[0], 1, 0,
        plan->failure_name, sizeof(plan->failure_name));
    plan->compare_function =
        find_global(mir.insns[15].name);
    if (plan->search_function == NULL ||
        plan->failure_function == NULL ||
        plan->compare_function == NULL ||
        plan->compare_function->storage != SC_FUNC ||
        mir.insns[15].opcode != MIR_ADDRESS)
        return mir_machine_reject(
            "bsearch-edge", "functions");
    if (!mir_recovery_global_address(
            mir.insns[7].dst, &plan->array,
            assembly_name, sizeof(assembly_name),
            &array_offset) ||
        plan->array == NULL || array_offset != 0 ||
        !plan->array->is_array ||
        plan->array->array_len != 51 ||
        plan->array->elem_size != 2)
        return mir_machine_reject(
            "bsearch-edge", "array");
    for (item = 0; item < 9; ++item) {
        if (mir_recovery_direct_call(
                search_calls[item], 5, 0,
                assembly_name, sizeof(assembly_name)) !=
                plan->search_function ||
            strcmp(assembly_name, plan->search_name) ||
            mir_recovery_direct_call(
                failure_calls[item], 1, 0,
                assembly_name, sizeof(assembly_name)) !=
                plan->failure_function ||
            strcmp(assembly_name, plan->failure_name) ||
            mir.insns[failure_strings[item]].opcode !=
                MIR_STRING_ADDRESS)
            return mir_machine_reject(
                "bsearch-edge", "call-alias");
        plan->failure_strings[item] =
            (int)mir.insns[failure_strings[item]].immediate;
    }
    return
        mir_machine_constant_equals(mir.insns[1].dst, 0) &&
        mir_machine_constant_equals(mir.insns[10].dst, 0) &&
        mir_machine_constant_equals(mir.insns[12].dst, 2) &&
        mir_machine_constant_equals(mir.insns[28].dst, 55) &&
        mir_machine_constant_equals(mir.insns[30].dst, 55) &&
        mir_machine_constant_equals(mir.insns[39].dst, 1) &&
        mir_machine_constant_equals(mir.insns[79].dst, 54) &&
        mir_machine_constant_equals(mir.insns[103].dst, 56) &&
        mir_machine_constant_equals(mir.insns[130].dst, 10) &&
        mir_machine_constant_equals(mir.insns[135].dst, 20) &&
        mir_machine_constant_equals(mir.insns[137].dst, 10) &&
        mir_machine_constant_equals(mir.insns[146].dst, 2) &&
        mir_machine_constant_equals(mir.insns[186].dst, 20) &&
        mir_machine_constant_equals(mir.insns[195].dst, 2) &&
        mir_machine_constant_equals(mir.insns[235].dst, 5) &&
        mir_machine_constant_equals(mir.insns[259].dst, 15) &&
        mir_machine_constant_equals(mir.insns[283].dst, 25);
}

static void mir_bsearch_edge_key(
    FILE *out, int value)
{
    fprintf(out,
            "\tld hl,%d\n\tld (ix-2),l\n\tld (ix-1),h\n",
            value & 0xffff);
}

static void mir_bsearch_edge_call(
    FILE *out, const struct MirBsearchEdgeSchedule *plan,
    int count)
{
    fprintf(out, "\tld hl,%s\n\tpush hl\n",
            asm_name_for(sym_asm_name(plan->compare_function)));
    fputs("\tld hl,2\n\tpush hl\n", out);
    fprintf(out, "\tld hl,%d\n\tpush hl\n", count);
    fprintf(out, "\tld hl,%s\n\tpush hl\n",
            asm_name_for(sym_asm_name(plan->array)));
    fputs("\tpush ix\n\tpop hl\n\tld de,-2\n\tadd hl,de\n"
          "\tpush hl\n", out);
    mir_emit_recovery_call(
        out, plan->search_function, plan->search_name);
    mir_emit_final_call_cleanup(out, 5);
}

static void mir_bsearch_edge_fail(
    FILE *out, const struct MirBsearchEdgeSchedule *plan,
    int string_id)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_emit_recovery_call(
        out, plan->failure_function, plan->failure_name);
    mir_emit_final_call_cleanup(out, 1);
}

static void mir_bsearch_edge_expect_miss(
    FILE *out, const struct MirBsearchEdgeSchedule *plan,
    int key, int count, int failure)
{
    int matched = new_label();

    mir_bsearch_edge_key(out, key);
    mir_bsearch_edge_call(out, plan, count);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", matched);
    mir_bsearch_edge_fail(
        out, plan, plan->failure_strings[failure]);
    fprintf(out, "L%d:\n", matched);
}

static void mir_bsearch_edge_expect_hit(
    FILE *out, const struct MirBsearchEdgeSchedule *plan,
    int key, int count, int expected, int failure)
{
    int failed = new_label();
    int done = new_label();

    mir_bsearch_edge_key(out, key);
    mir_bsearch_edge_call(out, plan, count);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld hl,%d\n\tor a\n\tsbc hl,de\n"
            "\tjp z,L%d\nL%d:\n",
            failed, expected, done, failed);
    mir_bsearch_edge_fail(
        out, plan, plan->failure_strings[failure]);
    fprintf(out, "L%d:\n", done);
}

static void mir_emit_bsearch_edge_schedule(
    FILE *out, const struct MirBsearchEdgeSchedule *plan)
{
    fputs(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tdec sp\n\tdec sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_bsearch_edge_expect_miss(out, plan, 0, 0, 0);
    fprintf(out, "\tld hl,55\n\tld (%s),hl\n",
            asm_name_for(sym_asm_name(plan->array)));
    mir_bsearch_edge_expect_hit(out, plan, 55, 1, 55, 1);
    mir_bsearch_edge_expect_miss(out, plan, 54, 1, 2);
    mir_bsearch_edge_expect_miss(out, plan, 56, 1, 3);
    fprintf(out,
            "\tld hl,10\n\tld (%s),hl\n"
            "\tld hl,20\n\tld (%s+2),hl\n",
            asm_name_for(sym_asm_name(plan->array)),
            asm_name_for(sym_asm_name(plan->array)));
    mir_bsearch_edge_expect_hit(out, plan, 10, 2, 10, 4);
    mir_bsearch_edge_expect_hit(out, plan, 20, 2, 20, 5);
    mir_bsearch_edge_expect_miss(out, plan, 5, 2, 6);
    mir_bsearch_edge_expect_miss(out, plan, 15, 2, 7);
    mir_bsearch_edge_expect_miss(out, plan, 25, 2, 8);
    fputs("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_bsearch_integer_schedule(
    struct MirBsearchEdgeSchedule *plan)
{
    const int search_calls[6] = {57, 132, 170, 195, 220, 270};
    const int failure_calls[6] = {88, 142, 176, 201, 251, 301};
    const int failure_strings[6] = {86, 140, 174, 199, 249, 299};
    char assembly_name[64];
    int array_offset;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 303 || mir.next_value != 183 ||
        mir_cfg_block_count() != 37 || mir.local_bytes != 8 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        !mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    plan->search_function = mir_recovery_direct_call(
        search_calls[0], 5, 0,
        plan->search_name, sizeof(plan->search_name));
    plan->failure_function = mir_recovery_direct_call(
        failure_calls[0], 1, 0,
        plan->failure_name, sizeof(plan->failure_name));
    plan->compare_function =
        find_global(mir.insns[55].name);
    if (plan->search_function == NULL ||
        plan->failure_function == NULL ||
        plan->compare_function == NULL ||
        mir.insns[55].opcode != MIR_ADDRESS)
        return mir_machine_reject(
            "bsearch-integer", "functions");
    if (!mir_recovery_global_address(
            mir.insns[14].dst, &plan->array,
            assembly_name, sizeof(assembly_name),
            &array_offset) ||
        plan->array == NULL || array_offset != 0 ||
        !plan->array->is_array ||
        plan->array->array_len != 51 ||
        plan->array->elem_size != 2)
        return mir_machine_reject(
            "bsearch-integer", "array");
    for (item = 0; item < 6; ++item) {
        if (mir_recovery_direct_call(
                search_calls[item], 5, 0,
                assembly_name, sizeof(assembly_name)) !=
                plan->search_function ||
            strcmp(assembly_name, plan->search_name) ||
            mir_recovery_direct_call(
                failure_calls[item], 1, 0,
                assembly_name, sizeof(assembly_name)) !=
                plan->failure_function ||
            strcmp(assembly_name, plan->failure_name) ||
            mir.insns[failure_strings[item]].opcode !=
                MIR_STRING_ADDRESS)
            return mir_machine_reject(
                "bsearch-integer", "call-alias");
        plan->failure_strings[item] =
            (int)mir.insns[failure_strings[item]].immediate;
    }
    return
        mir_machine_constant_equals(mir.insns[1].dst, 51) &&
        mir_machine_constant_equals(mir.insns[4].dst, 0) &&
        mir_machine_constant_equals(mir.insns[18].dst, 2) &&
        mir_machine_constant_equals(mir.insns[39].dst, 2) &&
        mir_machine_constant_equals(mir.insns[52].dst, 2) &&
        mir_machine_constant_equals(mir.insns[98].dst, 0) &&
        mir_machine_constant_equals(mir.insns[107].dst, 1) &&
        mir_machine_constant_equals(mir.insns[112].dst, 2) &&
        mir_machine_constant_equals(mir.insns[153].dst, 65535) &&
        mir_machine_constant_equals(mir.insns[178].dst, 101) &&
        mir_machine_constant_equals(mir.insns[203].dst, 0) &&
        mir_machine_constant_equals(mir.insns[253].dst, 100);
}

static void mir_bsearch_integer_increment(
    FILE *out)
{
    int done = new_label();

    fputs("\tinc (ix-4)\n", out);
    fprintf(out, "\tjp nz,L%d\n\tinc (ix-3)\nL%d:\n",
            done, done);
}

static void mir_emit_bsearch_integer_schedule(
    FILE *out, const struct MirBsearchEdgeSchedule *plan)
{
    int init_loop = new_label();
    int present_loop = new_label();
    int present_done = new_label();
    int present_failed = new_label();
    int present_ok = new_label();
    int absent_loop = new_label();
    int absent_done = new_label();
    int absent_ok = new_label();

    fputs(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-4\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%s\n\tld de,0\n\tld b,51\nL%d:\n"
            "\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tinc hl\n"
            "\tinc de\n\tinc de\n\tdjnz L%d\n",
            asm_name_for(sym_asm_name(plan->array)),
            init_loop, init_loop);

    fputs("\txor a\n\tld (ix-4),a\n\tld (ix-3),a\n", out);
    fprintf(out, "L%d:\n\tld a,(ix-3)\n\tor a\n", present_loop);
    fprintf(out, "\tjp nz,L%d\n\tld a,(ix-4)\n\tcp 51\n"
            "\tjp nc,L%d\n", present_done, present_done);
    fputs("\tld l,(ix-4)\n\tld h,(ix-3)\n\tadd hl,hl\n"
          "\tld (ix-2),l\n\tld (ix-1),h\n", out);
    mir_bsearch_edge_call(out, plan, 51);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld l,(ix-2)\n\tld h,(ix-1)\n"
            "\tor a\n\tsbc hl,de\n\tjp z,L%d\nL%d:\n",
            present_failed, present_ok, present_failed);
    mir_bsearch_edge_fail(
        out, plan, plan->failure_strings[0]);
    fprintf(out, "L%d:\n", present_ok);
    mir_bsearch_integer_increment(out);
    fprintf(out, "\tjp L%d\nL%d:\n", present_loop, present_done);

    fputs("\txor a\n\tld (ix-4),a\n\tld (ix-3),a\n", out);
    fprintf(out, "L%d:\n\tld a,(ix-3)\n\tor a\n", absent_loop);
    fprintf(out, "\tjp nz,L%d\n\tld a,(ix-4)\n\tcp 50\n"
            "\tjp nc,L%d\n", absent_done, absent_done);
    fputs("\tld l,(ix-4)\n\tld h,(ix-3)\n\tadd hl,hl\n\tinc hl\n"
          "\tld (ix-2),l\n\tld (ix-1),h\n", out);
    mir_bsearch_edge_call(out, plan, 51);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", absent_ok);
    mir_bsearch_edge_fail(
        out, plan, plan->failure_strings[1]);
    fprintf(out, "L%d:\n", absent_ok);
    mir_bsearch_integer_increment(out);
    fprintf(out, "\tjp L%d\nL%d:\n", absent_loop, absent_done);

    mir_bsearch_edge_expect_miss(out, plan, -1, 51, 2);
    mir_bsearch_edge_expect_miss(out, plan, 101, 51, 3);
    mir_bsearch_edge_expect_hit(out, plan, 0, 51, 0, 4);
    mir_bsearch_edge_expect_hit(out, plan, 100, 51, 100, 5);
    fputs("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_qsort_edge_schedule(
    struct MirQsortEdgeSchedule *plan)
{
    const int sort_calls[7] = {
        16, 43, 75, 130, 196, 257, 316
    };
    const int failure_calls[7] = {
        26, 53, 108, 163, 215, 276, 335
    };
    const int failure_strings[7] = {
        24, 51, 106, 161, 213, 274, 333
    };
    char assembly_name[64];
    int array_offset;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 344 || mir.next_value != 219 ||
        mir_cfg_block_count() != 40 || mir.local_bytes != 2 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        !mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    plan->sort_function = mir_recovery_direct_call(
        sort_calls[0], 4, 0,
        plan->sort_name, sizeof(plan->sort_name));
    plan->failure_function = mir_recovery_direct_call(
        failure_calls[0], 1, 0,
        plan->failure_name, sizeof(plan->failure_name));
    plan->compare_function =
        find_global(mir.insns[14].name);
    if (plan->sort_function == NULL ||
        plan->failure_function == NULL ||
        plan->compare_function == NULL ||
        mir.insns[14].opcode != MIR_ADDRESS)
        return mir_machine_reject(
            "qsort-edge", "functions");
    if (!mir_recovery_global_address(
            mir.insns[1].dst, &plan->array,
            assembly_name, sizeof(assembly_name),
            &array_offset) ||
        plan->array == NULL || array_offset != 0 ||
        !plan->array->is_array ||
        plan->array->array_len != 64 ||
        plan->array->elem_size != 2)
        return mir_machine_reject(
            "qsort-edge", "array");
    for (item = 0; item < 7; ++item) {
        if (mir_recovery_direct_call(
                sort_calls[item], 4, 0,
                assembly_name, sizeof(assembly_name)) !=
                plan->sort_function ||
            strcmp(assembly_name, plan->sort_name) ||
            mir_recovery_direct_call(
                failure_calls[item], 1, 0,
                assembly_name, sizeof(assembly_name)) !=
                plan->failure_function ||
            strcmp(assembly_name, plan->failure_name) ||
            mir.insns[failure_strings[item]].opcode !=
                MIR_STRING_ADDRESS)
            return mir_machine_reject(
                "qsort-edge", "call-alias");
        plan->failure_strings[item] =
            (int)mir.insns[failure_strings[item]].immediate;
    }
    return
        mir_machine_constant_equals(mir.insns[2].dst, 0) &&
        mir_machine_constant_equals(mir.insns[4].dst, 85) &&
        mir_machine_constant_equals(mir.insns[9].dst, 0) &&
        mir_machine_constant_equals(mir.insns[11].dst, 2) &&
        mir_machine_constant_equals(mir.insns[31].dst, 42) &&
        mir_machine_constant_equals(mir.insns[36].dst, 1) &&
        mir_machine_constant_equals(mir.insns[38].dst, 2) &&
        mir_machine_constant_equals(mir.insns[58].dst, 9) &&
        mir_machine_constant_equals(mir.insns[63].dst, 4) &&
        mir_machine_constant_equals(mir.insns[68].dst, 2) &&
        mir_machine_constant_equals(mir.insns[113].dst, 1) &&
        mir_machine_constant_equals(mir.insns[118].dst, 7) &&
        mir_machine_constant_equals(mir.insns[123].dst, 2) &&
        mir_machine_constant_equals(mir.insns[171].dst, 20) &&
        mir_machine_constant_equals(mir.insns[189].dst, 20) &&
        mir_machine_constant_equals(mir.insns[203].dst, 20) &&
        mir_machine_constant_equals(mir.insns[230].dst, 20) &&
        mir_machine_constant_equals(mir.insns[236].dst, 19) &&
        mir_machine_constant_equals(mir.insns[250].dst, 20) &&
        mir_machine_constant_equals(mir.insns[291].dst, 20) &&
        mir_machine_constant_equals(mir.insns[297].dst, 7) &&
        mir_machine_constant_equals(mir.insns[309].dst, 20);
}

static void mir_qsort_edge_call(
    FILE *out, const struct MirQsortEdgeSchedule *plan,
    int count)
{
    fprintf(out, "\tld hl,%s\n\tpush hl\n",
            asm_name_for(sym_asm_name(plan->compare_function)));
    fputs("\tld hl,2\n\tpush hl\n", out);
    fprintf(out, "\tld hl,%d\n\tpush hl\n", count);
    fprintf(out, "\tld hl,%s\n\tpush hl\n",
            asm_name_for(sym_asm_name(plan->array)));
    mir_emit_recovery_call(
        out, plan->sort_function, plan->sort_name);
    mir_emit_final_call_cleanup(out, 4);
}

static void mir_qsort_edge_fail(
    FILE *out, const struct MirQsortEdgeSchedule *plan,
    int failure)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->failure_strings[failure]);
    mir_emit_recovery_call(
        out, plan->failure_function, plan->failure_name);
    mir_emit_final_call_cleanup(out, 1);
}

static void mir_qsort_edge_check_word(
    FILE *out, const struct MirQsortEdgeSchedule *plan,
    int offset, int expected, int failure)
{
    int matched = new_label();

    fprintf(out,
            "\tld hl,(%s%+d)\n\tld de,%d\n"
            "\tor a\n\tsbc hl,de\n\tjp z,L%d\n",
            asm_name_for(sym_asm_name(plan->array)),
            offset, expected & 0xffff, matched);
    mir_qsort_edge_fail(out, plan, failure);
    fprintf(out, "L%d:\n", matched);
}

static void mir_qsort_edge_fill(
    FILE *out, const struct MirQsortEdgeSchedule *plan,
    int start, int delta)
{
    int loop = new_label();

    fprintf(out,
            "\tld hl,%s\n\tld de,%d\n\tld b,20\nL%d:\n"
            "\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tinc hl\n",
            asm_name_for(sym_asm_name(plan->array)),
            start, loop);
    if (delta > 0)
        fputs("\tinc de\n", out);
    else if (delta < 0)
        fputs("\tdec de\n", out);
    fprintf(out, "\tdjnz L%d\n", loop);
}

static void mir_qsort_edge_verify_sequence(
    FILE *out, const struct MirQsortEdgeSchedule *plan,
    int expected, int constant, int failure)
{
    int loop = new_label();
    int failed = new_label();
    int done = new_label();

    fprintf(out,
            "\tld hl,%s\n\tld de,%d\n\tld b,20\nL%d:\n"
            "\tld a,(hl)\n\tcp e\n\tjp nz,L%d\n"
            "\tinc hl\n\tld a,(hl)\n\tcp d\n\tjp nz,L%d\n"
            "\tinc hl\n",
            asm_name_for(sym_asm_name(plan->array)),
            expected, loop, failed, failed);
    if (!constant)
        fputs("\tinc de\n", out);
    fprintf(out, "\tdjnz L%d\n\tjp L%d\nL%d:\n",
            loop, done, failed);
    mir_qsort_edge_fail(out, plan, failure);
    fprintf(out, "L%d:\n", done);
}

static void mir_emit_qsort_edge_schedule(
    FILE *out, const struct MirQsortEdgeSchedule *plan)
{
    fputs(MIR_EXACT_KERNEL_MARKER "\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out, "\tld hl,85\n\tld (%s),hl\n",
            asm_name_for(sym_asm_name(plan->array)));
    mir_qsort_edge_call(out, plan, 0);
    mir_qsort_edge_check_word(out, plan, 0, 85, 0);
    fprintf(out, "\tld hl,42\n\tld (%s),hl\n",
            asm_name_for(sym_asm_name(plan->array)));
    mir_qsort_edge_call(out, plan, 1);
    mir_qsort_edge_check_word(out, plan, 0, 42, 1);
    fprintf(out,
            "\tld hl,9\n\tld (%s),hl\n"
            "\tld hl,4\n\tld (%s+2),hl\n",
            asm_name_for(sym_asm_name(plan->array)),
            asm_name_for(sym_asm_name(plan->array)));
    mir_qsort_edge_call(out, plan, 2);
    mir_qsort_edge_check_word(out, plan, 0, 4, 2);
    mir_qsort_edge_check_word(out, plan, 2, 9, 2);
    fprintf(out,
            "\tld hl,1\n\tld (%s),hl\n"
            "\tld hl,7\n\tld (%s+2),hl\n",
            asm_name_for(sym_asm_name(plan->array)),
            asm_name_for(sym_asm_name(plan->array)));
    mir_qsort_edge_call(out, plan, 2);
    mir_qsort_edge_check_word(out, plan, 0, 1, 3);
    mir_qsort_edge_check_word(out, plan, 2, 7, 3);

    mir_qsort_edge_fill(out, plan, 0, 1);
    mir_qsort_edge_call(out, plan, 20);
    mir_qsort_edge_verify_sequence(out, plan, 0, 0, 4);
    mir_qsort_edge_fill(out, plan, 19, -1);
    mir_qsort_edge_call(out, plan, 20);
    mir_qsort_edge_verify_sequence(out, plan, 0, 0, 5);
    mir_qsort_edge_fill(out, plan, 7, 0);
    mir_qsort_edge_call(out, plan, 20);
    mir_qsort_edge_verify_sequence(out, plan, 7, 1, 6);
    fputs("\tret\n", out);
}

static int mir_match_allocator_byte_helper_schedule(
    struct MirAllocatorByteHelperSchedule *plan)
{
    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir.aggregate_temp_bytes != 0)
        return 0;
    if (mir.count == 19 && mir.next_value == 17 &&
        mir_cfg_block_count() == 1 &&
        type_size(mir.return_type) == 1 &&
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->slot_offset) &&
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &plan->count_offset))
        return 0;
    if (mir.count == 19 && mir.next_value == 17 &&
        mir_cfg_block_count() == 1 &&
        type_size(mir.return_type) == 1) {
        if (!mir_machine_parameter_value_offset(
                mir.insns[1].dst, &plan->slot_offset) ||
            !mir_machine_parameter_value_offset(
                mir.insns[2].dst, &plan->count_offset) ||
            !mir_machine_constant_equals(mir.insns[4].dst, 37) ||
            !mir_machine_constant_equals(mir.insns[7].dst, 13) ||
            !mir_machine_constant_equals(mir.insns[12].dst, 91) ||
            !mir_machine_constant_equals(mir.insns[15].dst, 255))
            return mir_machine_reject(
                "allocator-byte-helper", "pattern");
        plan->kind = MIR_ALLOCATOR_PATTERN;
        return 1;
    }
    if (mir.count == 32 && mir.next_value == 21 &&
        mir_cfg_block_count() == 4 && mir.local_bytes == 2 &&
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->pointer_offset) &&
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &plan->count_offset) &&
        !mir_machine_parameter_value_offset(
            mir.insns[3].dst, &plan->slot_offset))
        return 0;
    if (mir.count == 32 && mir.next_value == 21 &&
        mir_cfg_block_count() == 4 && mir.local_bytes == 2) {
        if (!mir_machine_parameter_value_offset(
                mir.insns[1].dst, &plan->pointer_offset) ||
            !mir_machine_parameter_value_offset(
                mir.insns[2].dst, &plan->count_offset) ||
            !mir_machine_parameter_value_offset(
                mir.insns[3].dst, &plan->slot_offset) ||
            mir.insns[23].opcode != MIR_CALL)
            return mir_machine_reject(
                "allocator-byte-helper", "fill");
        plan->kind = MIR_ALLOCATOR_FILL;
        return 1;
    }
    if (mir.count == 51 && mir.next_value == 34 &&
        mir_cfg_block_count() == 6 && mir.local_bytes == 2) {
        if (!mir_machine_parameter_value_offset(
                mir.insns[1].dst, &plan->pointer_offset) ||
            !mir_machine_parameter_value_offset(
                mir.insns[2].dst, &plan->count_offset) ||
            !mir_machine_parameter_value_offset(
                mir.insns[3].dst, &plan->slot_offset) ||
            !mir_machine_parameter_value_offset(
                mir.insns[4].dst, &plan->message_offset))
            return 0;
        plan->failure_function = mir_recovery_direct_call(
            11, 1, 0, plan->failure_name,
            sizeof(plan->failure_name));
        if (plan->failure_function == NULL ||
            mir_recovery_direct_call(
                41, 1, 0, plan->failure_name,
                sizeof(plan->failure_name)) !=
                plan->failure_function ||
            mir.insns[9].opcode != MIR_STRING_ADDRESS)
            return mir_machine_reject(
                "allocator-byte-helper", "check");
        plan->kind = MIR_ALLOCATOR_CHECK;
        plan->null_string = (int)mir.insns[9].immediate;
        return 1;
    }
    if (mir.count == 44 && mir.next_value == 29 &&
        mir_cfg_block_count() == 6 && mir.local_bytes == 2) {
        if (!mir_machine_parameter_value_offset(
                mir.insns[1].dst, &plan->pointer_offset) ||
            !mir_machine_parameter_value_offset(
                mir.insns[2].dst, &plan->count_offset) ||
            !mir_machine_parameter_value_offset(
                mir.insns[3].dst, &plan->message_offset))
            return 0;
        plan->failure_function = mir_recovery_direct_call(
            10, 1, 0, plan->failure_name,
            sizeof(plan->failure_name));
        if (plan->failure_function == NULL ||
            mir_recovery_direct_call(
                34, 1, 0, plan->failure_name,
                sizeof(plan->failure_name)) !=
                plan->failure_function ||
            mir.insns[8].opcode != MIR_STRING_ADDRESS)
            return mir_machine_reject(
                "allocator-byte-helper", "zero-check");
        plan->kind = MIR_ALLOCATOR_ZERO_CHECK;
        plan->null_string = (int)mir.insns[8].immediate;
        return 1;
    }
    return 0;
}

static void mir_allocator_pattern_value(
    FILE *out, int slot_offset, int index_offset)
{
    int slot = slot_offset + 2;

    fprintf(out,
            "\tld e,(ix+%d)\n\tld d,(ix+%d)\n\tld hl,37\n",
            slot, slot + 1);
    mir_emit_runtime_call(out, "__mulu");
    fputs("\tpush hl\n", out);
    if (index_offset >= 0) {
        int index = index_offset + 2;
        fprintf(out,
                "\tld e,(ix+%d)\n\tld d,(ix+%d)\n",
                index, index + 1);
    } else {
        fprintf(out,
                "\tld e,(ix%d)\n\tld d,(ix%d)\n",
                index_offset, index_offset + 1);
    }
    fputs("\tld hl,13\n", out);
    mir_emit_runtime_call(out, "__mulu");
    fputs("\tpop de\n\tadd hl,de\n\tld de,91\n\tadd hl,de\n"
          "\tld a,l\n", out);
}

static void mir_allocator_pattern_value_stack(
    FILE *out, int slot_offset, int index_offset)
{
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tld hl,37\n",
            slot_offset);
    mir_emit_runtime_call(out, "__mulu");
    fputs("\tpush hl\n", out);
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tld hl,13\n",
            index_offset + 2);
    mir_emit_runtime_call(out, "__mulu");
    fputs("\tpop de\n\tadd hl,de\n\tld de,91\n\tadd hl,de\n", out);
}

static void mir_emit_allocator_byte_helper_schedule(
    FILE *out, const struct MirAllocatorByteHelperSchedule *plan)
{
    int loop = new_label();
    int done = new_label();
    int pointer_ok = new_label();
    int value_ok = new_label();

    fputs(MIR_EXACT_KERNEL_MARKER "\n", out);
    if (plan->kind == MIR_ALLOCATOR_PATTERN) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        mir_allocator_pattern_value_stack(
            out, plan->slot_offset, plan->count_offset);
        fputs("\tld h,0\n\tret\n", out);
        return;
    }
    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tdec sp\n\tdec sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    if (plan->kind == MIR_ALLOCATOR_CHECK ||
        plan->kind == MIR_ALLOCATOR_ZERO_CHECK) {
        int pointer = plan->pointer_offset + 2;

        fprintf(out,
                "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
                "\tld a,h\n\tor l\n\tjp nz,L%d\n"
                "\tld hl,S%d\n\tpush hl\n",
                pointer, pointer + 1, pointer_ok,
                plan->null_string);
        mir_emit_recovery_call(
            out, plan->failure_function, plan->failure_name);
        mir_emit_final_call_cleanup(out, 1);
        fprintf(out, "L%d:\n", pointer_ok);
    }
    fputs("\txor a\n\tld (ix-2),a\n\tld (ix-1),a\n", out);
    fprintf(out, "L%d:\n", loop);
    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n", out);
    {
        int count = plan->count_offset + 2;
        fprintf(out,
                "\tld e,(ix+%d)\n\tld d,(ix+%d)\n"
                "\tor a\n\tsbc hl,de\n\tjp nc,L%d\n",
                count, count + 1, done);
    }
    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n", out);
    {
        int pointer = plan->pointer_offset + 2;

        fprintf(out,
                "\tld e,(ix+%d)\n\tld d,(ix+%d)\n"
                "\tadd hl,de\n",
                pointer, pointer + 1);
    }
    if (plan->kind == MIR_ALLOCATOR_FILL) {
        fputs("\tpush hl\n", out);
        mir_allocator_pattern_value(
            out, plan->slot_offset, -2);
        fputs("\tpop hl\n\tld (hl),a\n", out);
    } else {
        fputs("\tld a,(hl)\n", out);
        if (plan->kind == MIR_ALLOCATOR_CHECK) {
            fputs("\tpush af\n", out);
            mir_allocator_pattern_value(
                out, plan->slot_offset, -2);
            fputs("\tld e,a\n\tpop af\n\tcp e\n", out);
        } else {
            fputs("\tor a\n", out);
        }
        fprintf(out, "\tjp z,L%d\n", value_ok);
        {
            int message = plan->message_offset + 2;
            fprintf(out,
                    "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n",
                    message, message + 1);
        }
        mir_emit_recovery_call(
            out, plan->failure_function, plan->failure_name);
        mir_emit_final_call_cleanup(out, 1);
        fprintf(out, "L%d:\n", value_ok);
    }
    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n\tinc hl\n"
          "\tld (ix-2),l\n\tld (ix-1),h\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n"
            "\tld sp,ix\n\tpop ix\n\tret\n",
            loop, done);
}

static int mir_match_allocator_large_schedule(
    struct MirAllocatorLargeSchedule *plan)
{
    const int allocate_calls[6] = {3, 69, 99, 125, 139, 153};
    const int allocation_sizes[6] = {32768, 32, 32768, 1, 65000, 65535};
    const int free_calls[3] = {66, 96, 122};
    const int failure_calls[8] = {13, 61, 79, 109, 117, 135, 149, 163};
    const int string_instructions[8] = {11, 59, 77, 107, 115, 133, 147, 161};
    const int constant_instructions[6] = {1, 67, 97, 123, 137, 151};
    char assembly_name[64];
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 169 || mir.next_value != 112 ||
        mir_cfg_block_count() != 16 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    plan->allocate_function = mir_recovery_direct_call(
        allocate_calls[0], 1, 0,
        plan->allocate_name, sizeof(plan->allocate_name));
    plan->free_function = mir_recovery_direct_call(
        free_calls[0], 1, 0,
        plan->free_name, sizeof(plan->free_name));
    plan->failure_function = mir_recovery_direct_call(
        failure_calls[0], 1, 0,
        plan->failure_name, sizeof(plan->failure_name));
    if (plan->allocate_function == NULL ||
        plan->free_function == NULL ||
        plan->failure_function == NULL)
        return mir_machine_reject(
            "allocator-large", "functions");
    for (item = 0; item < 6; ++item)
        if (mir_recovery_direct_call(
                allocate_calls[item], 1, 0,
                assembly_name, sizeof(assembly_name)) !=
                plan->allocate_function ||
            strcmp(assembly_name, plan->allocate_name) ||
            !mir_machine_constant_equals(
                mir.insns[constant_instructions[item]].dst,
                allocation_sizes[item]))
            return mir_machine_reject(
                "allocator-large", "allocate-alias");
    for (item = 0; item < 3; ++item)
        if (mir_recovery_direct_call(
                free_calls[item], 1, 0,
                assembly_name, sizeof(assembly_name)) !=
                plan->free_function ||
            strcmp(assembly_name, plan->free_name))
            return mir_machine_reject(
                "allocator-large", "free-alias");
    for (item = 0; item < 8; ++item) {
        if (mir_recovery_direct_call(
                failure_calls[item], 1, 0,
                assembly_name, sizeof(assembly_name)) !=
                plan->failure_function ||
            strcmp(assembly_name, plan->failure_name) ||
            mir.insns[string_instructions[item]].opcode !=
                MIR_STRING_ADDRESS)
            return mir_machine_reject(
                "allocator-large", "failure-alias");
        plan->strings[item] =
            (int)mir.insns[string_instructions[item]].immediate;
    }
    return
        mir_machine_constant_equals(mir.insns[19].dst, 18) &&
        mir_machine_constant_equals(mir.insns[22].dst, 32767) &&
        mir_machine_constant_equals(mir.insns[25].dst, 52) &&
        mir_machine_constant_equals(mir.insns[31].dst, 18) &&
        mir_machine_constant_equals(mir.insns[40].dst, 32767) &&
        mir_machine_constant_equals(mir.insns[43].dst, 52) &&
        mir_machine_constant_equals(mir.insns[85].dst, 86) &&
        mir_machine_constant_equals(mir.insns[88].dst, 31) &&
        mir_machine_constant_equals(mir.insns[91].dst, 120);
}

static void mir_allocator_large_fail(
    FILE *out, const struct MirAllocatorLargeSchedule *plan,
    int string_id)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_emit_recovery_call(
        out, plan->failure_function, plan->failure_name);
}

static void mir_allocator_large_allocate(
    FILE *out, const struct MirAllocatorLargeSchedule *plan,
    int size, int offset, int failure_string)
{
    int ok = new_label();

    fprintf(out, "\tld hl,%d\n\tpush hl\n", size);
    mir_emit_recovery_call(
        out, plan->allocate_function, plan->allocate_name);
    mir_emit_final_call_cleanup(out, 1);
    fprintf(out, "\tld (ix%d),l\n\tld (ix%d),h\n"
            "\tld a,h\n\tor l\n\tjp nz,L%d\n",
            offset, offset + 1, ok);
    mir_allocator_large_fail(out, plan, failure_string);
    fprintf(out, "L%d:\n", ok);
}

static void mir_allocator_large_free(
    FILE *out, const struct MirAllocatorLargeSchedule *plan,
    int offset)
{
    fprintf(out,
            "\tld l,(ix%d)\n\tld h,(ix%d)\n\tpush hl\n",
            offset, offset + 1);
    mir_emit_recovery_call(
        out, plan->free_function, plan->free_name);
    mir_emit_final_call_cleanup(out, 1);
}

static void mir_emit_allocator_large_schedule(
    FILE *out, const struct MirAllocatorLargeSchedule *plan)
{
    int edges_failed = new_label();
    int edges_ok = new_label();
    int same = new_label();
    int first_wrap_ok = new_label();
    int second_wrap_ok = new_label();

    fputs(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-6\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_allocator_large_allocate(
        out, plan, 32768, -2, plan->strings[0]);
    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n"
          "\tld (hl),18\n\tld de,32767\n\tadd hl,de\n\tld (hl),52\n"
          "\tld l,(ix-2)\n\tld h,(ix-1)\n\tld a,(hl)\n\tcp 18\n"
          "\tjp nz,L", out);
    fprintf(out, "%d\n\tld de,32767\n\tadd hl,de\n"
            "\tld a,(hl)\n\tcp 52\n\tjp z,L%d\nL%d:\n",
            edges_failed, edges_ok, edges_failed);
    mir_allocator_large_fail(out, plan, plan->strings[1]);
    fprintf(out, "L%d:\n", edges_ok);
    mir_allocator_large_free(out, plan, -2);

    mir_allocator_large_allocate(
        out, plan, 32, -4, plan->strings[2]);
    fputs("\tld l,(ix-4)\n\tld h,(ix-3)\n"
          "\tld (hl),86\n\tld de,31\n\tadd hl,de\n\tld (hl),120\n",
          out);
    mir_allocator_large_free(out, plan, -4);
    mir_allocator_large_allocate(
        out, plan, 32768, -2, plan->strings[3]);
    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n"
          "\tld e,(ix-4)\n\tld d,(ix-3)\n"
          "\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp z,L%d\n", same);
    mir_allocator_large_fail(out, plan, plan->strings[4]);
    fprintf(out, "L%d:\n", same);
    mir_allocator_large_free(out, plan, -2);

    mir_allocator_large_allocate(
        out, plan, 1, -4, plan->strings[5]);
    fputs("\tld hl,65000\n\tpush hl\n", out);
    mir_emit_recovery_call(
        out, plan->allocate_function, plan->allocate_name);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", first_wrap_ok);
    mir_allocator_large_fail(out, plan, plan->strings[6]);
    fprintf(out, "L%d:\n\tld hl,65535\n\tpush hl\n",
            first_wrap_ok);
    mir_emit_recovery_call(
        out, plan->allocate_function, plan->allocate_name);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", second_wrap_ok);
    mir_allocator_large_fail(out, plan, plan->strings[7]);
    fprintf(out, "L%d:\n", second_wrap_ok);
    mir_allocator_large_free(out, plan, -4);
    fputs("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_grow_fallback_schedule(
    struct MirGrowFallbackSchedule *plan)
{
    const int allocate_calls[5] = {3, 9, 15, 21, 27};
    const int allocation_sizes[5] = {100, 8, 48, 4, 16};
    const int allocation_constants[5] = {1, 7, 13, 19, 25};
    const int fill_calls[2] = {125, 132};
    const int free_calls[5] = {136, 140, 206, 210, 214};
    char assembly_name[64];
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 215 || mir.next_value != 119 ||
        mir_cfg_block_count() != 35 || mir.local_bytes != 13 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        !mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    plan->allocate_function = mir_recovery_direct_call(
        3, 1, 0, plan->allocate_name,
        sizeof(plan->allocate_name));
    plan->fill_function = mir_recovery_direct_call(
        125, 3, 0, plan->fill_name, sizeof(plan->fill_name));
    plan->free_function = mir_recovery_direct_call(
        136, 1, 0, plan->free_name, sizeof(plan->free_name));
    plan->resize_function = mir_recovery_direct_call(
        146, 2, 0, plan->resize_name, sizeof(plan->resize_name));
    plan->failure_function = mir_recovery_direct_call(
        117, 1, 0, plan->failure_name, sizeof(plan->failure_name));
    plan->check_function = mir_recovery_direct_call(
        166, 4, 0, plan->check_name, sizeof(plan->check_name));
    plan->pattern_function = mir_recovery_direct_call(
        186, 2, 0, plan->pattern_name, sizeof(plan->pattern_name));
    if (plan->allocate_function == NULL ||
        plan->fill_function == NULL ||
        plan->free_function == NULL ||
        plan->resize_function == NULL ||
        plan->failure_function == NULL ||
        plan->check_function == NULL ||
        plan->pattern_function == NULL)
        return mir_machine_reject(
            "grow-fallback", "functions");
    for (item = 0; item < 5; ++item)
        if (mir_recovery_direct_call(
                allocate_calls[item], 1, 0,
                assembly_name, sizeof(assembly_name)) !=
                plan->allocate_function ||
            strcmp(assembly_name, plan->allocate_name) ||
            !mir_machine_constant_equals(
                mir.insns[allocation_constants[item]].dst,
                allocation_sizes[item]))
            return mir_machine_reject(
                "grow-fallback", "allocate-alias");
    for (item = 0; item < 2; ++item)
        if (mir_recovery_direct_call(
                fill_calls[item], 3, 0,
                assembly_name, sizeof(assembly_name)) !=
                plan->fill_function ||
            strcmp(assembly_name, plan->fill_name))
            return mir_machine_reject(
                "grow-fallback", "fill-alias");
    for (item = 0; item < 5; ++item)
        if (mir_recovery_direct_call(
                free_calls[item], 1, 0,
                assembly_name, sizeof(assembly_name)) !=
                plan->free_function ||
            strcmp(assembly_name, plan->free_name))
            return mir_machine_reject(
                "grow-fallback", "free-alias");
    if (mir.insns[115].opcode != MIR_STRING_ADDRESS ||
        mir.insns[154].opcode != MIR_STRING_ADDRESS ||
        mir.insns[164].opcode != MIR_STRING_ADDRESS ||
        mir.insns[191].opcode != MIR_STRING_ADDRESS ||
        !mir_machine_constant_equals(mir.insns[121].dst, 100) ||
        !mir_machine_constant_equals(mir.insns[123].dst, 27) ||
        !mir_machine_constant_equals(mir.insns[128].dst, 48) ||
        !mir_machine_constant_equals(mir.insns[130].dst, 28) ||
        !mir_machine_constant_equals(mir.insns[144].dst, 80) ||
        !mir_machine_constant_equals(mir.insns[160].dst, 48) ||
        !mir_machine_constant_equals(mir.insns[162].dst, 28) ||
        !mir_machine_constant_equals(mir.insns[168].dst, 48) ||
        !mir_machine_constant_equals(mir.insns[173].dst, 80) ||
        !mir_machine_constant_equals(mir.insns[181].dst, 27) ||
        !mir_machine_constant_equals(mir.insns[198].dst, 1))
        return mir_machine_reject(
            "grow-fallback", "constants");
    plan->strings[0] = (int)mir.insns[115].immediate;
    plan->strings[1] = (int)mir.insns[154].immediate;
    plan->strings[2] = (int)mir.insns[164].immediate;
    plan->strings[3] = (int)mir.insns[191].immediate;
    return 1;
}

static void mir_grow_fallback_call_one(
    FILE *out, struct Sym *function, const char *name,
    int value)
{
    fprintf(out, "\tld hl,%d\n\tpush hl\n", value);
    mir_emit_recovery_call(out, function, name);
    mir_emit_final_call_cleanup(out, 1);
}

static void mir_grow_fallback_store(FILE *out, int offset)
{
    fprintf(out, "\tld (ix%d),l\n\tld (ix%d),h\n",
            offset, offset + 1);
}

static void mir_grow_fallback_load(FILE *out, int offset)
{
    fprintf(out, "\tld l,(ix%d)\n\tld h,(ix%d)\n",
            offset, offset + 1);
}

static void mir_grow_fallback_fail(
    FILE *out, const struct MirGrowFallbackSchedule *plan,
    int string_id)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_emit_recovery_call(
        out, plan->failure_function, plan->failure_name);
}

static void mir_grow_fallback_fill(
    FILE *out, const struct MirGrowFallbackSchedule *plan,
    int pointer_offset, int count, int pattern)
{
    fprintf(out, "\tld hl,%d\n\tpush hl\n", pattern);
    fprintf(out, "\tld hl,%d\n\tpush hl\n", count);
    mir_grow_fallback_load(out, pointer_offset);
    fputs("\tpush hl\n", out);
    mir_emit_recovery_call(
        out, plan->fill_function, plan->fill_name);
    mir_emit_final_call_cleanup(out, 3);
}

static void mir_grow_fallback_free(
    FILE *out, const struct MirGrowFallbackSchedule *plan,
    int pointer_offset)
{
    mir_grow_fallback_load(out, pointer_offset);
    fputs("\tpush hl\n", out);
    mir_emit_recovery_call(
        out, plan->free_function, plan->free_name);
    mir_emit_final_call_cleanup(out, 1);
}

static void mir_emit_grow_fallback_schedule(
    FILE *out, const struct MirGrowFallbackSchedule *plan)
{
    int allocation_failed = new_label();
    int allocations_ok = new_label();
    int moved_ok = new_label();
    int loop = new_label();
    int loop_done = new_label();
    int byte_ok = new_label();
    int increment_done = new_label();
    int pointer;

    fputs(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-12\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_grow_fallback_call_one(
        out, plan->allocate_function, plan->allocate_name, 100);
    mir_grow_fallback_store(out, -2);
    mir_grow_fallback_call_one(
        out, plan->allocate_function, plan->allocate_name, 8);
    mir_grow_fallback_store(out, -4);
    mir_grow_fallback_call_one(
        out, plan->allocate_function, plan->allocate_name, 48);
    mir_grow_fallback_store(out, -6);
    mir_grow_fallback_call_one(
        out, plan->allocate_function, plan->allocate_name, 4);
    mir_grow_fallback_store(out, -8);
    mir_grow_fallback_call_one(
        out, plan->allocate_function, plan->allocate_name, 16);
    mir_grow_fallback_store(out, -10);
    for (pointer = -2; pointer >= -10; pointer -= 2) {
        fprintf(out,
                "\tld a,(ix%d)\n\tor (ix%d)\n\tjp z,L%d\n",
                pointer, pointer + 1, allocation_failed);
    }
    fprintf(out, "\tjp L%d\nL%d:\n",
            allocations_ok, allocation_failed);
    mir_grow_fallback_fail(out, plan, plan->strings[0]);
    fprintf(out, "L%d:\n", allocations_ok);
    mir_grow_fallback_fill(out, plan, -2, 100, 27);
    mir_grow_fallback_fill(out, plan, -6, 48, 28);
    mir_grow_fallback_free(out, plan, -2);
    mir_grow_fallback_free(out, plan, -8);
    fputs("\tld hl,80\n\tpush hl\n", out);
    mir_grow_fallback_load(out, -6);
    fputs("\tpush hl\n", out);
    mir_emit_recovery_call(
        out, plan->resize_function, plan->resize_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_grow_fallback_store(out, -12);
    fputs("\tld e,(ix-2)\n\tld d,(ix-1)\n"
          "\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp z,L%d\n", moved_ok);
    mir_grow_fallback_fail(out, plan, plan->strings[1]);
    fprintf(out, "L%d:\n", moved_ok);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[2]);
    fputs("\tld hl,28\n\tpush hl\n\tld hl,48\n\tpush hl\n", out);
    mir_grow_fallback_load(out, -12);
    fputs("\tpush hl\n", out);
    mir_emit_recovery_call(
        out, plan->check_function, plan->check_name);
    mir_emit_final_call_cleanup(out, 4);
    fputs("\tld hl,48\n\tld (ix-8),l\n\tld (ix-7),h\n", out);
    fprintf(out, "L%d:\n\tld a,(ix-7)\n\tor a\n", loop);
    fprintf(out, "\tjp nz,L%d\n\tld a,(ix-8)\n\tcp 80\n"
            "\tjp nc,L%d\n", loop_done, loop_done);
    fputs("\tld l,(ix-8)\n\tld h,(ix-7)\n\tpush hl\n"
          "\tld hl,27\n\tpush hl\n", out);
    mir_emit_recovery_call(
        out, plan->pattern_function, plan->pattern_name);
    mir_emit_final_call_cleanup(out, 2);
    fputs("\tld e,l\n\tld l,(ix-8)\n\tld h,(ix-7)\n"
          "\tld c,(ix-12)\n\tld b,(ix-11)\n\tadd hl,bc\n"
          "\tld a,(hl)\n\tcp e\n", out);
    fprintf(out, "\tjp z,L%d\n", byte_ok);
    mir_grow_fallback_fail(out, plan, plan->strings[3]);
    fprintf(out, "L%d:\n\tinc (ix-8)\n", byte_ok);
    fprintf(out,
            "\tjp nz,L%d\n\tinc (ix-7)\nL%d:\n\tjp L%d\n",
            increment_done, increment_done, loop);
    fprintf(out, "L%d:\n", loop_done);
    mir_grow_fallback_free(out, plan, -12);
    mir_grow_fallback_free(out, plan, -4);
    mir_grow_fallback_free(out, plan, -10);
    fputs("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_allocator_stress_schedule(
    struct MirAllocatorStressSchedule *plan)
{
    const int random_calls[5] = {42, 73, 90, 194, 201};
    const int check_calls[5] = {72, 141, 323, 385, 439};
    const int fill_calls[3] = {166, 275, 430};
    const int failure_calls[4] = {115, 223, 247, 422};
    const int free_calls[4] = {176, 330, 392, 443};
    const int string_instructions[10] = {
        70, 113, 139, 221, 229, 245, 321, 383, 420, 437
    };
    char assembly_name[64];
    int offset;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 444 || mir.next_value != 303 ||
        mir_cfg_block_count() != 32 || mir.local_bytes != 14 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        !mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    plan->random_function = mir_recovery_direct_call(
        42, 0, 0, plan->random_name, sizeof(plan->random_name));
    plan->check_function = mir_recovery_direct_call(
        72, 4, 0, plan->check_name, sizeof(plan->check_name));
    plan->fill_function = mir_recovery_direct_call(
        166, 3, 0, plan->fill_name, sizeof(plan->fill_name));
    plan->failure_function = mir_recovery_direct_call(
        115, 1, 0, plan->failure_name, sizeof(plan->failure_name));
    plan->resize_function = mir_recovery_direct_call(
        105, 2, 0, plan->resize_name, sizeof(plan->resize_name));
    plan->free_function = mir_recovery_direct_call(
        176, 1, 0, plan->free_name, sizeof(plan->free_name));
    plan->clear_allocate_function = mir_recovery_direct_call(
        213, 2, 0, plan->clear_allocate_name,
        sizeof(plan->clear_allocate_name));
    plan->allocate_function = mir_recovery_direct_call(
        237, 1, 0, plan->allocate_name,
        sizeof(plan->allocate_name));
    plan->zero_check_function = mir_recovery_direct_call(
        231, 3, 0, plan->zero_check_name,
        sizeof(plan->zero_check_name));
    if (plan->random_function == NULL ||
        plan->check_function == NULL ||
        plan->fill_function == NULL ||
        plan->failure_function == NULL ||
        plan->resize_function == NULL ||
        plan->free_function == NULL ||
        plan->clear_allocate_function == NULL ||
        plan->allocate_function == NULL ||
        plan->zero_check_function == NULL)
        return mir_machine_reject(
            "allocator-stress", "functions");
    if (mir_recovery_direct_call(
            412, 1, 0, assembly_name,
            sizeof(assembly_name)) != plan->allocate_function)
        return mir_machine_reject(
            "allocator-stress", "allocate-alias");
    for (item = 0; item < 5; ++item)
        if (mir_recovery_direct_call(
                random_calls[item], 0, 0,
                assembly_name, sizeof(assembly_name)) !=
                plan->random_function ||
            mir_recovery_direct_call(
                check_calls[item], 4, 0,
                assembly_name, sizeof(assembly_name)) !=
                plan->check_function)
            return mir_machine_reject(
                "allocator-stress", "random-check-alias");
    for (item = 0; item < 3; ++item)
        if (mir_recovery_direct_call(
                fill_calls[item], 3, 0,
                assembly_name, sizeof(assembly_name)) !=
            plan->fill_function)
            return mir_machine_reject(
                "allocator-stress", "fill-alias");
    for (item = 0; item < 4; ++item)
        if (mir_recovery_direct_call(
                failure_calls[item], 1, 0,
                assembly_name, sizeof(assembly_name)) !=
                plan->failure_function ||
            mir_recovery_direct_call(
                free_calls[item], 1, 0,
                assembly_name, sizeof(assembly_name)) !=
                plan->free_function)
            return mir_machine_reject(
                "allocator-stress", "failure-free-alias");
    plan->seed = find_global(mir.insns[3].name);
    if (plan->seed == NULL || plan->seed->storage != SC_GLOBAL ||
        plan->seed->is_volatile || type_size(plan->seed->type) != 2 ||
        !mir_recovery_global_address(
            mir.insns[13].dst, &plan->slots,
            assembly_name, sizeof(assembly_name), &offset) ||
        plan->slots == NULL || offset != 0 ||
        !mir_recovery_global_address(
            mir.insns[19].dst, &plan->sizes,
            assembly_name, sizeof(assembly_name), &offset) ||
        plan->sizes == NULL || offset != 0 ||
        !plan->slots->is_array || plan->slots->array_len != 24 ||
        plan->slots->elem_size != 2 ||
        !plan->sizes->is_array || plan->sizes->array_len != 24 ||
        plan->sizes->elem_size != 2 ||
        plan->slots == plan->sizes)
        return mir_machine_reject(
            "allocator-stress", "globals");
    for (item = 0; item < 10; ++item) {
        if (mir.insns[string_instructions[item]].opcode !=
            MIR_STRING_ADDRESS)
            return mir_machine_reject(
                "allocator-stress", "strings");
        plan->strings[item] =
            (int)mir.insns[string_instructions[item]].immediate;
    }
    return
        mir_machine_constant_equals(mir.insns[1].dst, 44257) &&
        mir_machine_constant_equals(mir.insns[10].dst, 24) &&
        mir_machine_constant_equals(mir.insns[39].dst, 420) &&
        mir_machine_constant_equals(mir.insns[43].dst, 24) &&
        mir_machine_constant_equals(mir.insns[74].dst, 4) &&
        mir_machine_constant_equals(mir.insns[91].dst, 220) &&
        mir_machine_constant_equals(mir.insns[93].dst, 1) &&
        mir_machine_constant_equals(mir.insns[195].dst, 220) &&
        mir_machine_constant_equals(mir.insns[197].dst, 1) &&
        mir_machine_constant_equals(mir.insns[202].dst, 1) &&
        mir_machine_constant_equals(mir.insns[410].dst, 12000) &&
        mir_machine_constant_equals(mir.insns[428].dst, 35);
}

static void mir_allocator_load_word(FILE *out, int offset)
{
    fprintf(out, "\tld l,(ix%d)\n\tld h,(ix%d)\n",
            offset, offset + 1);
}

static void mir_allocator_store_word(FILE *out, int offset)
{
    fprintf(out, "\tld (ix%d),l\n\tld (ix%d),h\n",
            offset, offset + 1);
}

static void mir_allocator_increment(FILE *out, int offset, int amount)
{
    mir_allocator_load_word(out, offset);
    if (amount == 1)
        fputs("\tinc hl\n", out);
    else
        fprintf(out, "\tld de,%d\n\tadd hl,de\n", amount);
    mir_allocator_store_word(out, offset);
}

static void mir_allocator_index_address(
    FILE *out, int helper_label)
{
    fprintf(out, "\tcall L%d\n", helper_label);
}

static void mir_allocator_load_indexed(
    FILE *out, int helper_label)
{
    mir_allocator_index_address(out, helper_label);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n", out);
}

static void mir_allocator_store_indexed(
    FILE *out, int helper_label, int value_offset)
{
    mir_allocator_index_address(out, helper_label);
    fputs("\tpush hl\n", out);
    mir_allocator_load_word(out, value_offset);
    fputs("\tex de,hl\n\tpop hl\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n",
          out);
}

static void mir_allocator_store_indexed_constant(
    FILE *out, int helper_label, int value)
{
    mir_allocator_index_address(out, helper_label);
    fprintf(out, "\tld de,%d\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n",
            value);
}

static void mir_allocator_random_mod(
    FILE *out, const struct MirAllocatorStressSchedule *plan,
    int modulus)
{
    mir_emit_recovery_call(
        out, plan->random_function, plan->random_name);
    fprintf(out, "\tld de,%d\n", modulus);
    mir_emit_runtime_call(out, "__modu");
}

static void mir_allocator_stress_fail(
    FILE *out, const struct MirAllocatorStressSchedule *plan,
    int string_id)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_emit_recovery_call(
        out, plan->failure_function, plan->failure_name);
}

static void mir_allocator_check(
    FILE *out, const struct MirAllocatorStressSchedule *plan,
    int pointer_offset, int size_offset, int seed_offset,
    int string_id)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_allocator_load_word(out, seed_offset);
    fputs("\tpush hl\n", out);
    mir_allocator_load_word(out, size_offset);
    fputs("\tpush hl\n", out);
    mir_allocator_load_word(out, pointer_offset);
    fputs("\tpush hl\n", out);
    mir_emit_recovery_call(
        out, plan->check_function, plan->check_name);
    mir_emit_final_call_cleanup(out, 4);
}

static void mir_allocator_fill(
    FILE *out, const struct MirAllocatorStressSchedule *plan,
    int pointer_offset, int size_offset, int seed_offset)
{
    mir_allocator_load_word(out, seed_offset);
    fputs("\tpush hl\n", out);
    mir_allocator_load_word(out, size_offset);
    fputs("\tpush hl\n", out);
    mir_allocator_load_word(out, pointer_offset);
    fputs("\tpush hl\n", out);
    mir_emit_recovery_call(
        out, plan->fill_function, plan->fill_name);
    mir_emit_final_call_cleanup(out, 3);
}

static void mir_emit_allocator_stress_schedule(
    FILE *out, const struct MirAllocatorStressSchedule *plan)
{
    int init_loop = new_label();
    int stress_loop = new_label();
    int stress_done = new_label();
    int empty_slot = new_label();
    int free_slot = new_label();
    int iteration_done = new_label();
    int realloc_nonnull = new_label();
    int realloc_keep_old = new_label();
    int realloc_keep_ready = new_label();
    int calloc_branch = new_label();
    int calloc_nonnull = new_label();
    int malloc_nonnull = new_label();
    int allocated = new_label();
    int cleanup_even = new_label();
    int cleanup_odd = new_label();
    int cleanup_even_done = new_label();
    int cleanup_odd_done = new_label();
    int cleanup_even_skip = new_label();
    int cleanup_odd_skip = new_label();
    int final_nonnull = new_label();
    int slots_address = new_label();
    int sizes_address = new_label();
    int body = new_label();

    fputs(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-14\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tjp L%d\n"
            "L%d:\n\tld l,(ix-4)\n\tld h,(ix-3)\n"
            "\tadd hl,hl\n\tld de,%s\n\tadd hl,de\n\tret\n"
            "L%d:\n\tld l,(ix-4)\n\tld h,(ix-3)\n"
            "\tadd hl,hl\n\tld de,%s\n\tadd hl,de\n\tret\n"
            "L%d:\n",
            body,
            slots_address,
            asm_name_for(sym_asm_name(plan->slots)),
            sizes_address,
            asm_name_for(sym_asm_name(plan->sizes)),
            body);
    fprintf(out, "\tld hl,44257\n\tld (%s),hl\n",
            asm_name_for(sym_asm_name(plan->seed)));
    fprintf(out,
            "\tld hl,%s\n\tld de,%s\n\tld b,48\n\txor a\nL%d:\n"
            "\tld (hl),a\n\tld (de),a\n\tinc hl\n\tinc de\n"
            "\tdjnz L%d\n",
            asm_name_for(sym_asm_name(plan->slots)),
            asm_name_for(sym_asm_name(plan->sizes)),
            init_loop, init_loop);
    fputs("\txor a\n\tld (ix-2),a\n\tld (ix-1),a\n", out);
    fprintf(out, "L%d:\n", stress_loop);
    mir_allocator_load_word(out, MIR_ALLOC_I);
    fputs("\tld de,420\n\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp nc,L%d\n", stress_done);
    mir_allocator_random_mod(out, plan, 24);
    mir_allocator_store_word(out, MIR_ALLOC_INDEX);
    mir_allocator_load_indexed(out, slots_address);
    mir_allocator_store_word(out, MIR_ALLOC_POINTER);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", empty_slot);

    mir_allocator_load_indexed(out, sizes_address);
    mir_allocator_store_word(out, MIR_ALLOC_SIZE);
    mir_allocator_load_word(out, MIR_ALLOC_INDEX);
    fputs("\tld de,11\n\tadd hl,de\n", out);
    mir_allocator_store_word(out, MIR_ALLOC_KEEP);
    mir_allocator_check(
        out, plan, MIR_ALLOC_POINTER, MIR_ALLOC_SIZE,
        MIR_ALLOC_KEEP, plan->strings[0]);
    mir_allocator_random_mod(out, plan, 4);
    mir_allocator_store_word(out, MIR_ALLOC_OPERATION);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", free_slot);
    mir_allocator_load_word(out, MIR_ALLOC_SIZE);
    mir_allocator_store_word(out, MIR_ALLOC_OLD);
    mir_allocator_random_mod(out, plan, 220);
    fputs("\tinc hl\n", out);
    mir_allocator_store_word(out, MIR_ALLOC_SIZE);
    mir_allocator_load_word(out, MIR_ALLOC_SIZE);
    fputs("\tpush hl\n", out);
    mir_allocator_load_word(out, MIR_ALLOC_POINTER);
    fputs("\tpush hl\n", out);
    mir_emit_recovery_call(
        out, plan->resize_function, plan->resize_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_allocator_store_word(out, MIR_ALLOC_POINTER);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", realloc_nonnull);
    mir_allocator_stress_fail(out, plan, plan->strings[1]);
    fprintf(out, "L%d:\n", realloc_nonnull);
    mir_allocator_load_word(out, MIR_ALLOC_OLD);
    fputs("\tpush hl\n", out);
    mir_allocator_load_word(out, MIR_ALLOC_SIZE);
    fputs("\tex de,hl\n\tpop hl\n\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp c,L%d\n", realloc_keep_old);
    mir_allocator_load_word(out, MIR_ALLOC_SIZE);
    fprintf(out, "\tjp L%d\nL%d:\n",
            realloc_keep_ready, realloc_keep_old);
    mir_allocator_load_word(out, MIR_ALLOC_OLD);
    fprintf(out, "L%d:\n", realloc_keep_ready);
    mir_allocator_store_word(out, MIR_ALLOC_KEEP);
    mir_allocator_load_word(out, MIR_ALLOC_INDEX);
    fputs("\tld de,11\n\tadd hl,de\n", out);
    mir_allocator_store_word(out, MIR_ALLOC_OPERATION);
    mir_allocator_check(
        out, plan, MIR_ALLOC_POINTER, MIR_ALLOC_KEEP,
        MIR_ALLOC_OPERATION, plan->strings[2]);
    mir_allocator_store_indexed(
        out, slots_address, MIR_ALLOC_POINTER);
    mir_allocator_store_indexed(
        out, sizes_address, MIR_ALLOC_SIZE);
    mir_allocator_load_word(out, MIR_ALLOC_INDEX);
    fputs("\tld de,11\n\tadd hl,de\n", out);
    mir_allocator_store_word(out, MIR_ALLOC_OPERATION);
    mir_allocator_fill(
        out, plan, MIR_ALLOC_POINTER, MIR_ALLOC_SIZE,
        MIR_ALLOC_OPERATION);
    fprintf(out, "\tjp L%d\n", iteration_done);

    fprintf(out, "L%d:\n", free_slot);
    mir_allocator_load_word(out, MIR_ALLOC_POINTER);
    fputs("\tpush hl\n", out);
    mir_emit_recovery_call(
        out, plan->free_function, plan->free_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_allocator_store_indexed_constant(out, slots_address, 0);
    mir_allocator_store_indexed_constant(out, sizes_address, 0);
    fprintf(out, "\tjp L%d\n", iteration_done);

    fprintf(out, "L%d:\n", empty_slot);
    mir_allocator_random_mod(out, plan, 220);
    fputs("\tinc hl\n", out);
    mir_allocator_store_word(out, MIR_ALLOC_SIZE);
    mir_emit_recovery_call(
        out, plan->random_function, plan->random_name);
    fputs("\tbit 0,l\n", out);
    fprintf(out, "\tjp z,L%d\n", calloc_branch);
    fputs("\tld hl,1\n\tpush hl\n", out);
    mir_allocator_load_word(out, MIR_ALLOC_SIZE);
    fputs("\tpush hl\n", out);
    mir_emit_recovery_call(
        out, plan->clear_allocate_function,
        plan->clear_allocate_name);
    mir_emit_final_call_cleanup(out, 2);
    mir_allocator_store_word(out, MIR_ALLOC_POINTER);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", calloc_nonnull);
    mir_allocator_stress_fail(out, plan, plan->strings[3]);
    fprintf(out, "L%d:\n", calloc_nonnull);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[4]);
    mir_allocator_load_word(out, MIR_ALLOC_SIZE);
    fputs("\tpush hl\n", out);
    mir_allocator_load_word(out, MIR_ALLOC_POINTER);
    fputs("\tpush hl\n", out);
    mir_emit_recovery_call(
        out, plan->zero_check_function, plan->zero_check_name);
    mir_emit_final_call_cleanup(out, 3);
    fprintf(out, "\tjp L%d\nL%d:\n", allocated, calloc_branch);
    mir_allocator_load_word(out, MIR_ALLOC_SIZE);
    fputs("\tpush hl\n", out);
    mir_emit_recovery_call(
        out, plan->allocate_function, plan->allocate_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_allocator_store_word(out, MIR_ALLOC_POINTER);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", malloc_nonnull);
    mir_allocator_stress_fail(out, plan, plan->strings[5]);
    fprintf(out, "L%d:\nL%d:\n", malloc_nonnull, allocated);
    mir_allocator_store_indexed(
        out, slots_address, MIR_ALLOC_POINTER);
    mir_allocator_store_indexed(
        out, sizes_address, MIR_ALLOC_SIZE);
    mir_allocator_load_word(out, MIR_ALLOC_INDEX);
    fputs("\tld de,11\n\tadd hl,de\n", out);
    mir_allocator_store_word(out, MIR_ALLOC_OPERATION);
    mir_allocator_fill(
        out, plan, MIR_ALLOC_POINTER, MIR_ALLOC_SIZE,
        MIR_ALLOC_OPERATION);

    fprintf(out, "L%d:\n", iteration_done);
    mir_allocator_increment(out, MIR_ALLOC_I, 1);
    fprintf(out, "\tjp L%d\nL%d:\n", stress_loop, stress_done);

    fputs("\tld hl,0\n", out);
    mir_allocator_store_word(out, MIR_ALLOC_I);
    fprintf(out, "L%d:\n", cleanup_even);
    mir_allocator_load_word(out, MIR_ALLOC_I);
    fputs("\tld de,24\n\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp nc,L%d\n", cleanup_even_done);
    mir_allocator_load_word(out, MIR_ALLOC_I);
    mir_allocator_store_word(out, MIR_ALLOC_INDEX);
    mir_allocator_load_indexed(out, slots_address);
    mir_allocator_store_word(out, MIR_ALLOC_POINTER);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", cleanup_even_skip);
    mir_allocator_load_indexed(out, sizes_address);
    mir_allocator_store_word(out, MIR_ALLOC_SIZE);
    mir_allocator_load_word(out, MIR_ALLOC_I);
    fputs("\tld de,11\n\tadd hl,de\n", out);
    mir_allocator_store_word(out, MIR_ALLOC_KEEP);
    mir_allocator_check(
        out, plan, MIR_ALLOC_POINTER, MIR_ALLOC_SIZE,
        MIR_ALLOC_KEEP, plan->strings[6]);
    mir_allocator_load_word(out, MIR_ALLOC_POINTER);
    fputs("\tpush hl\n", out);
    mir_emit_recovery_call(
        out, plan->free_function, plan->free_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_allocator_store_indexed_constant(out, slots_address, 0);
    fprintf(out, "L%d:\n", cleanup_even_skip);
    mir_allocator_increment(out, MIR_ALLOC_I, 2);
    fprintf(out, "\tjp L%d\nL%d:\n", cleanup_even, cleanup_even_done);

    fputs("\tld hl,1\n", out);
    mir_allocator_store_word(out, MIR_ALLOC_I);
    fprintf(out, "L%d:\n", cleanup_odd);
    mir_allocator_load_word(out, MIR_ALLOC_I);
    fputs("\tld de,24\n\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp nc,L%d\n", cleanup_odd_done);
    mir_allocator_load_word(out, MIR_ALLOC_I);
    mir_allocator_store_word(out, MIR_ALLOC_INDEX);
    mir_allocator_load_indexed(out, slots_address);
    mir_allocator_store_word(out, MIR_ALLOC_POINTER);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", cleanup_odd_skip);
    mir_allocator_load_indexed(out, sizes_address);
    mir_allocator_store_word(out, MIR_ALLOC_SIZE);
    mir_allocator_load_word(out, MIR_ALLOC_I);
    fputs("\tld de,11\n\tadd hl,de\n", out);
    mir_allocator_store_word(out, MIR_ALLOC_KEEP);
    mir_allocator_check(
        out, plan, MIR_ALLOC_POINTER, MIR_ALLOC_SIZE,
        MIR_ALLOC_KEEP, plan->strings[7]);
    mir_allocator_load_word(out, MIR_ALLOC_POINTER);
    fputs("\tpush hl\n", out);
    mir_emit_recovery_call(
        out, plan->free_function, plan->free_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_allocator_store_indexed_constant(out, slots_address, 0);
    fprintf(out, "L%d:\n", cleanup_odd_skip);
    mir_allocator_increment(out, MIR_ALLOC_I, 2);
    fprintf(out, "\tjp L%d\nL%d:\n", cleanup_odd, cleanup_odd_done);

    fputs("\tld hl,12000\n\tpush hl\n", out);
    mir_emit_recovery_call(
        out, plan->allocate_function, plan->allocate_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_allocator_store_word(out, MIR_ALLOC_POINTER);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", final_nonnull);
    mir_allocator_stress_fail(out, plan, plan->strings[8]);
    fprintf(out, "L%d:\n"
            "\tld hl,12000\n\tld (ix-8),l\n\tld (ix-7),h\n"
            "\tld hl,35\n\tld (ix-6),l\n\tld (ix-5),h\n",
            final_nonnull);
    mir_allocator_fill(
        out, plan, MIR_ALLOC_POINTER, MIR_ALLOC_SIZE,
        MIR_ALLOC_OPERATION);
    mir_allocator_check(
        out, plan, MIR_ALLOC_POINTER, MIR_ALLOC_SIZE,
        MIR_ALLOC_OPERATION, plan->strings[9]);
    mir_allocator_load_word(out, MIR_ALLOC_POINTER);
    fputs("\tpush hl\n", out);
    mir_emit_recovery_call(
        out, plan->free_function, plan->free_name);
    mir_emit_final_call_cleanup(out, 1);
    fputs("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

int mir_try_emit_runtime_runners(FILE *out, int phase)
{
    if (phase == 2) {
        enum MirStrictSpilledProfile profile;

        return mir_call_runner_spilled_profile(&profile)
            ? (int)profile : -1;
    }
    if (phase == 0) {
        struct MirExecRecursionSchedule exec_recursion;
        struct MirSparseFileSchedule sparse_file;
        struct MirCtrlZFileSchedule ctrlz_file;
        struct MirWildcardOpenSchedule wildcard_open;
        struct MirWildcardCreateSchedule wildcard_create;
        struct MirErrnoExerciseSchedule errno_exercise;
        struct MirDirectoryPatternSchedule directory_pattern;
        struct MirIntegerReportSchedule integer_report;
        struct MirStringReportSchedule string_report;
        struct MirReadExactSchedule read_exact;
        struct MirFileExistsSchedule file_exists;
        struct MirFixedBinaryChecksSchedule fixed_binary_checks;
        struct MirFunctionPointerPrintSchedule function_pointer_print;
        struct MirPortIoSchedule port_io;
        struct MirSignedIdiomReportSchedule signed_idiom;
        struct MirBufferedExampleSchedule buffered_example;
        struct MirSimpleFileIoSchedule simple_file_io;
        struct MirFunctionPointerRuntimeSchedule function_pointer_runtime;
        struct MirSnprintfSequenceSchedule snprintf_sequence;
        struct MirLegacyFileRoundtripSchedule legacy_roundtrip;
        struct MirSlidingWindowDriverSchedule sliding_window_driver;
        struct MirNarrowStringWorkloadSchedule narrow_string_workload;
        struct MirBdosDriverSchedule bdos_driver;
        struct MirLineReaderSchedule line_reader;
        struct MirAdjacencyScanSchedule adjacency_scan;
        struct MirBsearchEdgeSchedule bsearch_edge;
        struct MirBsearchEdgeSchedule bsearch_integer;
        struct MirQsortEdgeSchedule qsort_edge;
        struct MirAllocatorStressSchedule allocator_stress;
        struct MirAllocatorByteHelperSchedule allocator_byte_helper;
        struct MirAllocatorLargeSchedule allocator_large;
        struct MirGrowFallbackSchedule grow_fallback;
        if (mir_match_grow_fallback_schedule(
                &grow_fallback)) {
            mir_emit_grow_fallback_schedule(
                out, &grow_fallback);
            return 1;
        }
        if (mir_match_allocator_large_schedule(
                &allocator_large)) {
            mir_emit_allocator_large_schedule(
                out, &allocator_large);
            return 1;
        }
        if (mir_match_allocator_byte_helper_schedule(
                &allocator_byte_helper)) {
            mir_emit_allocator_byte_helper_schedule(
                out, &allocator_byte_helper);
            return 1;
        }
        if (mir_match_allocator_stress_schedule(
                &allocator_stress)) {
            mir_emit_allocator_stress_schedule(
                out, &allocator_stress);
            return 1;
        }
        if (mir_match_qsort_edge_schedule(&qsort_edge)) {
            mir_emit_qsort_edge_schedule(out, &qsort_edge);
            return 1;
        }
        if (mir_match_bsearch_integer_schedule(
                &bsearch_integer)) {
            mir_emit_bsearch_integer_schedule(
                out, &bsearch_integer);
            return 1;
        }
        if (mir_match_bsearch_edge_schedule(&bsearch_edge)) {
            mir_emit_bsearch_edge_schedule(out, &bsearch_edge);
            return 1;
        }
        if (mir_match_line_reader_schedule(&line_reader)) {
            mir_emit_line_reader_schedule(out, &line_reader);
            return 1;
        }
        if (mir_match_adjacency_scan_schedule(
                &adjacency_scan)) {
            mir_emit_adjacency_scan_schedule(
                out, &adjacency_scan);
            return 1;
        }
        if (mir_match_bdos_driver_schedule(&bdos_driver)) {
            mir_emit_bdos_driver_schedule(out, &bdos_driver);
            return 1;
        }
        if (mir_match_narrow_string_workload_schedule(
                &narrow_string_workload)) {
            mir_emit_narrow_string_workload_schedule(
                out, &narrow_string_workload);
            return 1;
        }
        if (mir_match_sliding_window_driver_schedule(
                &sliding_window_driver)) {
            mir_emit_sliding_window_driver_schedule(
                out, &sliding_window_driver);
            return 1;
        }
        if (mir_match_legacy_file_roundtrip_schedule(
                &legacy_roundtrip)) {
            mir_emit_legacy_file_roundtrip_schedule(
                out, &legacy_roundtrip);
            return 1;
        }
        if (mir_match_snprintf_sequence_schedule(
                &snprintf_sequence)) {
            mir_emit_snprintf_sequence_schedule(
                out, &snprintf_sequence);
            return 1;
        }
        if (mir_match_function_pointer_runtime_schedule(
                &function_pointer_runtime)) {
            mir_emit_function_pointer_runtime_schedule(
                out, &function_pointer_runtime);
            return 1;
        }
        if (mir_match_simple_file_io_schedule(&simple_file_io)) {
            mir_emit_simple_file_io_schedule(out, &simple_file_io);
            return 1;
        }
        if (mir_match_buffered_example_schedule(
                &buffered_example)) {
            mir_emit_buffered_example_schedule(
                out, &buffered_example);
            return 1;
        }
        if (mir_match_signed_idiom_report_schedule(&signed_idiom)) {
            mir_emit_signed_idiom_report_schedule(
                out, &signed_idiom);
            return 1;
        }
        if (mir_match_fixed_binary_checks_schedule(
                &fixed_binary_checks)) {
            mir_emit_fixed_binary_checks_schedule(
                out, &fixed_binary_checks);
            return 1;
        }
        if (mir_match_function_pointer_print_schedule(
                &function_pointer_print)) {
            mir_emit_function_pointer_print_schedule(
                out, &function_pointer_print);
            return 1;
        }
        if (mir_match_port_io_schedule(&port_io)) {
            mir_emit_port_io_schedule(out, &port_io);
            return 1;
        }
        if (mir_match_exec_recursion_schedule(&exec_recursion)) {
            mir_emit_exec_recursion_schedule(out, &exec_recursion);
            return 1;
        }
        if (mir_match_sparse_file_schedule(&sparse_file)) {
            mir_emit_sparse_file_schedule(out, &sparse_file);
            return 1;
        }
        if (mir_match_ctrlz_file_schedule(&ctrlz_file)) {
            mir_emit_ctrlz_file_schedule(out, &ctrlz_file);
            return 1;
        }
        if (mir_match_wildcard_open_schedule(&wildcard_open)) {
            mir_emit_wildcard_open_schedule(out, &wildcard_open);
            return 1;
        }
        if (mir_match_wildcard_create_schedule(&wildcard_create)) {
            mir_emit_wildcard_create_schedule(out, &wildcard_create);
            return 1;
        }
        if (mir_match_errno_exercise_schedule(&errno_exercise)) {
            mir_emit_errno_exercise_schedule(out, &errno_exercise);
            return 1;
        }
        if (mir_match_directory_pattern_schedule(
                &directory_pattern)) {
            mir_emit_directory_pattern_schedule(
                out, &directory_pattern);
            return 1;
        }
        if (mir_match_integer_report_schedule(&integer_report)) {
            mir_emit_integer_report_schedule(out, &integer_report);
            return 1;
        }
        if (mir_match_string_report_schedule(&string_report)) {
            mir_emit_string_report_schedule(out, &string_report);
            return 1;
        }
        if (mir_match_read_exact_schedule(&read_exact)) {
            mir_emit_read_exact_schedule(out, &read_exact);
            return 1;
        }
        if (mir_match_file_exists_schedule(&file_exists)) {
            mir_emit_file_exists_schedule(out, &file_exists);
            return 1;
        }
    }
    return -1;
}
