/**
 * @file dcc_mir_machine_aggregate_checks.c
 * @brief Emits exact schedules for aggregate and data-layout operations.
 *
 * @par Role
 * Handles proven array, matrix, VLA, record, union, bitfield, initializer,
 * pointer-layout, and aggregate validation shapes that benefit from a
 * dedicated machine schedule.
 *
 * @par Key entry point
 * mir_try_emit_aggregate_checks().
 */

#include <limits.h>

#include "dcc_mir_machine_internal.h"

struct MirVlaSmoothPlan {
    int n_stack_offset;
    int w_stack_offset;
    int src_stack_offset;
    int dst_stack_offset;
};

struct MirVlaFillCallSchedule {
    struct Sym *callee;
    int rows_stack_offset;
};

struct MirMatrixMultiplySchedule {
    int left_stack_offset;
    int right_stack_offset;
};

struct MirMatrixBitopsSchedule {
    int matrix_stack_offset;
};

struct MirBoardMatrixPrintSchedule {
    struct Sym *board;
    struct Sym *print_function;
    int size_stack_offset;
    int row_stride;
    int value_string_id;
    int newline_string_id;
    char value_call_name[64];
    char newline_call_name[64];
};

struct MirByteSumLoopSchedule {
    int pointer_stack_offset;
    int count_stack_offset;
    int skip_zero;
};

static int mir_machine_fold_integer_binary(
    int operation, long left, long right, int type, long *result)
{
    int width = type_size(type);
    int is_unsigned = (type & TYPE_UNSIGNED) != 0;
    unsigned long long mask;
    unsigned long long sign;
    unsigned long long modulus;
    unsigned long long lhs;
    unsigned long long rhs;
    unsigned long long bits;

    if (width != 1 && width != 2 && width != 4)
        return 0;
    mask = width == 1 ? 0xffULL :
           width == 2 ? 0xffffULL : 0xffffffffULL;
    sign = width == 1 ? 0x80ULL :
           width == 2 ? 0x8000ULL : 0x80000000ULL;
    modulus = mask + 1ULL;
    lhs = (unsigned long long)(unsigned long)left & mask;
    rhs = (unsigned long long)(unsigned long)right & mask;
    if (operation == '&') {
        bits = lhs & rhs;
        goto convert_result;
    }
    if (operation == '|') {
        bits = lhs | rhs;
        goto convert_result;
    }
    if (operation == '^') {
        bits = lhs ^ rhs;
        goto convert_result;
    }
    if (is_unsigned) {
        switch (operation) {
        case '+': bits = lhs + rhs; break;
        case '-': bits = lhs - rhs; break;
        case '*': bits = lhs * rhs; break;
        case '/':
            if (rhs == 0)
                return 0;
            bits = lhs / rhs;
            break;
        case '%':
            if (rhs == 0)
                return 0;
            bits = lhs % rhs;
            break;
        default:
            return 0;
        }
    } else {
        long long signed_lhs = (lhs & sign) != 0
            ? (long long)(lhs - modulus) : (long long)lhs;
        long long signed_rhs = (rhs & sign) != 0
            ? (long long)(rhs - modulus) : (long long)rhs;
        long long signed_value;

        switch (operation) {
        case '+': signed_value = signed_lhs + signed_rhs; break;
        case '-': signed_value = signed_lhs - signed_rhs; break;
        case '*': signed_value = signed_lhs * signed_rhs; break;
        case '/':
            if (signed_rhs == 0)
                return 0;
            if (signed_lhs == -(long long)sign && signed_rhs == -1)
                return 0;
            signed_value = signed_lhs / signed_rhs;
            break;
        case '%':
            if (signed_rhs == 0)
                return 0;
            if (signed_lhs == -(long long)sign && signed_rhs == -1)
                return 0;
            signed_value = signed_lhs % signed_rhs;
            break;
        default:
            return 0;
        }
        bits = (unsigned long long)signed_value;
    }
convert_result:
    bits &= mask;
    if (is_unsigned)
        *result = (long)(unsigned long)bits;
    else if ((bits & sign) != 0)
        *result = (long)((long long)bits - (long long)modulus);
    else
        *result = (long)bits;
    return 1;
}
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
static int mir_machine_call_has_no_arguments(
    const struct MirInsn *call)
{
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_ARG &&
            mir.insns[instruction].secondary_offset ==
                call->secondary_offset)
            return 0;
    return 1;
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
static int mir_aggregate_five_call_arguments(
    const struct MirInsn *call, int arguments[5])
{
    int count = 0;
    int instruction;
    int item;

    for (item = 0; item < 5; ++item)
        arguments[item] = -1;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *arg = &mir.insns[instruction];
        int index;

        if (arg->opcode != MIR_ARG ||
            arg->secondary_offset != call->secondary_offset)
            continue;
        index = (int)arg->immediate;
        if (index < 0 || index >= 5 || arguments[index] >= 0)
            return 0;
        arguments[index] = arg->src1;
        ++count;
    }
    return count == 5;
}
static void mir_machine_emit_global_address_hl(
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

enum MirAggregateCheckValueKind {
    MIR_AGGREGATE_CHECK_CONSTANT = 1,
    MIR_AGGREGATE_CHECK_LOCAL = 2,
    MIR_AGGREGATE_CHECK_GLOBAL = 3
};

struct MirAggregateCheckValue {
    int kind;
    int width;
    int is_unsigned;
    int local_offset;
    struct Sym *root;
    int root_offset;
    unsigned long value;
};

struct MirAggregateCheck {
    struct Sym *function;
    int string_id;
    int width;
    unsigned long expected;
    struct MirAggregateCheckValue actual;
};

struct MirAggregateInitStore {
    int offset;
    int width;
    unsigned long value;
};

struct MirAggregateMultidimChecks {
    int kind;
    struct Sym *print_function;
    struct Sym *check_word_function;
    struct Sym *check_wide_function;
    int heading_string_id;
    int summary_string_id;
    char heading_call_name[64];
    char summary_call_name[64];
    struct {
        struct Sym *board_root;
        struct Sym *cells_root;
        struct Sym *board_fill_function;
        struct Sym *board_weight_function;
        struct Sym *tile_function;
        struct Sym *cells_fill_function;
        struct Sym *cells_checksum_function;
        int strings[6];
        int rows;
        int columns;
        int board_row_stride;
        int board_column_stride;
        int board_weight_offset;
        int cells_stride;
        int cells_tag_offset;
        int cells_index;
        int w_ptr_offset;
        int w_struct_offset;
        int tile_offset;
        int row_offset;
        int column_offset;
        unsigned long weight_expected;
        unsigned long tile_weight_expected;
        unsigned long tile_char_expected;
        unsigned long checksum_expected;
        unsigned long tag_expected;
    } multidim;
    struct {
        struct MirAggregateInitStore stores[43];
        struct MirAggregateCheck checks[17];
        int check_count;
        int strings[5];
        int lg_offset;
        int lu_offset;
        int lb_offset;
        int lop_offset;
        int sum_offset;
        int row_offset;
        int column_offset;
        int index_offset;
        int row_count;
        int column_count;
        int row_stride;
        int column_stride;
        int first_member_offset;
        int second_member_offset;
        unsigned long first_addend;
        unsigned long second_addend;
        unsigned long sum_expected;
    } size2;
};

#define MIR_TOUCH_LOCAL_CHECK_COUNT 27
#define MIR_TOUCH_LOCAL_MEMORY_MAX 512

enum MirTouchLocalValueKind {
    MIR_TOUCH_LOCAL_UNKNOWN = 0,
    MIR_TOUCH_LOCAL_INTEGER = 1,
    MIR_TOUCH_LOCAL_ADDRESS = 2
};

struct MirTouchLocalValue {
    int kind;
    long value;
    int origin_address;
    int origin_width;
};

struct MirTouchLocalMemory {
    int address;
    int width;
    struct MirTouchLocalValue value;
};

struct MirTouchLocalStore {
    unsigned long value;
    int width;
    int compact_offset;
};

struct MirTouchLocalCheck {
    struct Sym *function;
    unsigned long expected;
    int string_id;
    int width;
    int is_unsigned;
    int store_index;
};

struct MirTouchLocalsPlan {
    struct MirTouchLocalStore stores[MIR_TOUCH_LOCAL_CHECK_COUNT];
    struct MirTouchLocalCheck checks[MIR_TOUCH_LOCAL_CHECK_COUNT];
    int frame_bytes;
};

struct MirPackedRecordRunner {
    struct Sym *guards[2];
    struct Sym *records;
    struct Sym *memset_function;
    struct Sym *print_function;
    struct Sym *dump_function;
    int strings[6];
    char print_call_name[64];
    int guard_count;
    int record_count;
    int record_stride;
    int member_offsets[6];
};

#define MIR_MULTIDIM_ARRAY_CHECK_COUNT 24

struct MirMultidimArrayRunner {
    struct Sym *byte_matrix;
    struct Sym *word_matrix;
    struct Sym *cube;
    struct Sym *grid;
    struct Sym *failures;
    struct Sym *check_function;
    struct Sym *row_function;
    struct Sym *column_function;
    struct Sym *print_function;
    int check_strings[MIR_MULTIDIM_ARRAY_CHECK_COUNT];
    int failure_string;
    int success_string;
    char failure_call_name[64];
    char success_call_name[64];
    int byte_array_offset;
    int byte_row_offset;
    int byte_column_offset;
    int byte_row_stride;
    int byte_column_stride;
    int byte_rows;
    int byte_columns;
    int word_array_offset;
    int word_row_offset;
    int word_column_offset;
    int word_row_stride;
    int word_column_stride;
    int word_rows;
    int word_columns;
    int cube_array_offset;
    int cube_a_offset;
    int cube_b_offset;
    int cube_d_offset;
    int cube_plane_stride;
    int cube_row_stride;
    int cube_column_stride;
    int cube_planes;
    int cube_rows;
    int cube_columns;
    int grid_cells_offset;
    int grid_cell_stride;
    int grid_array_offset;
    int grid_row_stride;
    int grid_column_stride;
};

#define MIR_ARRAY_MAIN_PRINT_CALLS 10
#define MIR_ARRAY_MAIN_STRINGS 8
#define MIR_ARRAY_MAIN_FRAME_BYTES 4

enum MirArrayMainSymbol {
    MIR_ARRAY_MAIN_U8,
    MIR_ARRAY_MAIN_U16,
    MIR_ARRAY_MAIN_U32,
    MIR_ARRAY_MAIN_I8,
    MIR_ARRAY_MAIN_I16,
    MIR_ARRAY_MAIN_I32,
    MIR_ARRAY_MAIN_CHARS,
    MIR_ARRAY_MAIN_BOARD,
    MIR_ARRAY_MAIN_WORDS,
    MIR_ARRAY_MAIN_SYMBOLS
};

enum MirArrayMainString {
    MIR_ARRAY_MAIN_SIZE_FAILURE,
    MIR_ARRAY_MAIN_UNSIGNED_FORMAT,
    MIR_ARRAY_MAIN_SIGNED_FORMAT,
    MIR_ARRAY_MAIN_CHARS_FORMAT,
    MIR_ARRAY_MAIN_BOARD_FORMAT,
    MIR_ARRAY_MAIN_NEWLINE,
    MIR_ARRAY_MAIN_WORD_FORMAT,
    MIR_ARRAY_MAIN_SUCCESS,
};

struct MirArrayMainPlan {
    struct Sym *symbols[MIR_ARRAY_MAIN_SYMBOLS];
    struct Sym *print_function;
    struct Sym *exit_function;
    struct Sym *many_function;
    int strings[MIR_ARRAY_MAIN_STRINGS];
    char print_names[MIR_ARRAY_MAIN_PRINT_CALLS][64];
    char exit_name[64];
    int count;
    int replacement_bias;
    int board_columns;
};

struct MirArrayMainConstant {
    short instruction;
    short type;
    long value;
};

struct MirArrayMainBinary {
    short instruction;
    short operation;
    short type;
    short operand_type;
    short left;
    short right;
};

struct MirArrayMainIndex {
    short instruction;
    short base;
    short subscript;
    short type;
    short width;
    short stride;
};

struct MirArrayMainEdge {
    short instruction;
    short target;
};

struct MirArrayMainPhi {
    short instruction;
    short first;
    short second;
    short first_predecessor;
    short second_predecessor;
};

static const unsigned char mir_array_main_opcodes[] = {
    MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_CONST, MIR_CONST, MIR_BINARY,
    MIR_CONST, MIR_CONST, MIR_BINARY, MIR_BINARY, MIR_BRANCH_FALSE,
    MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_CONST,
    MIR_BINARY, MIR_CONST, MIR_CONST, MIR_BINARY, MIR_BINARY,
    MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
    MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL,
    MIR_PHI, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
    MIR_LABEL, MIR_CONST, MIR_CONST, MIR_BINARY, MIR_CONST, MIR_CONST,
    MIR_BINARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
    MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL,
    MIR_JUMP, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_LABEL,
    MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_CONST, MIR_BINARY,
    MIR_CONST, MIR_CONST, MIR_BINARY, MIR_BINARY, MIR_BRANCH_FALSE,
    MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
    MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI,
    MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
    MIR_CONST, MIR_CONST, MIR_BINARY, MIR_CONST, MIR_CONST, MIR_BINARY,
    MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
    MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP,
    MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG,
    MIR_CALL, MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP, MIR_LABEL,
    MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL,
    MIR_NOP, MIR_LOAD, MIR_PHI, MIR_NOP, MIR_CONST, MIR_CONST,
    MIR_BINARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_NOP, MIR_ARG, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS,
    MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS,
    MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS,
    MIR_LOAD_INDIRECT, MIR_NOP, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_NOP, MIR_ARG, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS,
    MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS,
    MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS,
    MIR_LOAD_INDIRECT, MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP, MIR_LABEL,
    MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL,
    MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL,
    MIR_NOP, MIR_LOAD, MIR_NOP, MIR_PHI, MIR_NOP, MIR_CONST,
    MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS,
    MIR_NOP, MIR_CONST, MIR_BINARY, MIR_UNARY, MIR_STORE_INDIRECT,
    MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST,
    MIR_BINARY, MIR_NOP, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_NOP,
    MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_UNARY,
    MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS,
    MIR_NOP, MIR_UNARY, MIR_NOP, MIR_CONST, MIR_BINARY,
    MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS,
    MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT, MIR_ADDRESS,
    MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST, MIR_BINARY,
    MIR_UNARY, MIR_STORE_INDIRECT, MIR_NOP, MIR_LABEL, MIR_NOP,
    MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP,
    MIR_NOP, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_NOP,
    MIR_LOAD, MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP, MIR_CONST,
    MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP,
    MIR_ARG, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
    MIR_ARG, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
    MIR_ARG, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
    MIR_NOP, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP,
    MIR_ARG, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
    MIR_ARG, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
    MIR_ARG, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
    MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP, MIR_LABEL, MIR_NOP,
    MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL,
    MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_CONST,
    MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS,
    MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG,
    MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
    MIR_ARG, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
    MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_CONST,
    MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS,
    MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG,
    MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
    MIR_ARG, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
    MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL, MIR_NOP, MIR_NOP, MIR_NOP,
    MIR_NOP, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_NOP,
    MIR_LOAD, MIR_NOP, MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP, MIR_NOP,
    MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST,
    MIR_STORE, MIR_LABEL, MIR_NOP, MIR_LOAD, MIR_NOP, MIR_NOP,
    MIR_NOP, MIR_NOP, MIR_NOP, MIR_LOAD, MIR_CONST, MIR_BINARY,
    MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS,
    MIR_NOP, MIR_CONST, MIR_BINARY, MIR_LOAD, MIR_BINARY,
    MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL, MIR_LABEL,
    MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL,
    MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP, MIR_LABEL,
    MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL,
    MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL,
    MIR_NOP, MIR_LOAD, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
    MIR_PHI, MIR_NOP, MIR_CONST, MIR_CONST, MIR_BINARY, MIR_BINARY,
    MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG,
    MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
    MIR_ARG, MIR_CALL, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
    MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_CALL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN
};

static const struct MirArrayMainConstant mir_array_main_constants[] = {
    {3, TYPE_INT, 8}, {4, TYPE_INT, 1}, {6, TYPE_INT, 8},
    {7, TYPE_INT, 1}, {12, 0, 1}, {15, TYPE_INT, 8},
    {16, TYPE_INT, 1}, {18, TYPE_INT, 16}, {19, TYPE_INT, 2},
    {24, 0, 1}, {27, 0, 0}, {36, 0, 1}, {39, TYPE_INT, 8},
    {40, TYPE_INT, 1}, {42, TYPE_INT, 16}, {43, TYPE_INT, 2},
    {48, 0, 1}, {51, 0, 0}, {60, 0, 1}, {63, TYPE_INT, 8},
    {64, TYPE_INT, 1}, {66, TYPE_INT, 32}, {67, TYPE_INT, 4},
    {72, 0, 1}, {75, 0, 0}, {84, 0, 1}, {87, TYPE_INT, 8},
    {88, TYPE_INT, 1}, {90, TYPE_INT, 32}, {91, TYPE_INT, 4},
    {96, 0, 1}, {99, 0, 0}, {110, TYPE_INT, 1},
    {118, TYPE_INT, 0}, {125, TYPE_INT, 8}, {126, TYPE_INT, 1},
    {175, 0, 1}, {183, TYPE_INT, 0}, {191, TYPE_INT, 8},
    {198, TYPE_INT, 20}, {206, TYPE_INT, 20}, {214, TYPE_INT, 20},
    {224, TYPE_LONG, 20}, {231, TYPE_INT, 20}, {238, TYPE_INT, 20},
    {245, 0, 1}, {253, TYPE_INT, 0}, {262, TYPE_INT, 8},
    {310, 0, 1}, {318, TYPE_INT, 0}, {323, TYPE_INT, 1},
    {328, TYPE_INT, 2}, {333, TYPE_INT, 3}, {338, TYPE_INT, 4},
    {343, TYPE_INT, 5}, {348, TYPE_INT, 6}, {353, TYPE_INT, 7},
    {363, TYPE_INT, 0}, {374, TYPE_INT, 8}, {378, TYPE_INT, 0},
    {389, TYPE_INT, 8}, {396, TYPE_INT, 8}, {406, 0, 1},
    {417, 0, 1}, {425, TYPE_INT, 0}, {437, TYPE_INT, 16},
    {438, TYPE_INT, 2}, {454, 0, 1}, {463, TYPE_INT, 0}
};

static const struct MirArrayMainBinary mir_array_main_binaries[] = {
    {5, '/', TYPE_INT, TYPE_INT, 3, 4},
    {8, '/', TYPE_INT, TYPE_INT, 6, 7},
    {9, TOK_NE, TYPE_INT, TYPE_INT, 5, 8},
    {17, '/', TYPE_INT, TYPE_INT, 15, 16},
    {20, '/', TYPE_INT, TYPE_INT, 18, 19},
    {21, TOK_NE, TYPE_INT, TYPE_INT, 17, 20},
    {41, '/', TYPE_INT, TYPE_INT, 39, 40},
    {44, '/', TYPE_INT, TYPE_INT, 42, 43},
    {45, TOK_NE, TYPE_INT, TYPE_INT, 41, 44},
    {65, '/', TYPE_INT, TYPE_INT, 63, 64},
    {68, '/', TYPE_INT, TYPE_INT, 66, 67},
    {69, TOK_NE, TYPE_INT, TYPE_INT, 65, 68},
    {89, '/', TYPE_INT, TYPE_INT, 87, 88},
    {92, '/', TYPE_INT, TYPE_INT, 90, 91},
    {93, TOK_NE, TYPE_INT, TYPE_INT, 89, 92},
    {127, '/', TYPE_INT, TYPE_INT, 125, 126},
    {128, '<', TYPE_INT, TYPE_INT, 123, 127},
    {176, '+', 0, 0, 123, 175},
    {192, '<', TYPE_INT, TYPE_INT, 189, 191},
    {199, '+', TYPE_INT, TYPE_INT, 189, 198},
    {207, '+', TYPE_INT, TYPE_INT, 189, 206},
    {215, '+', TYPE_INT, TYPE_INT, 189, 214},
    {225, '-', TYPE_LONG, TYPE_LONG, 222, 224},
    {232, '-', TYPE_INT, TYPE_INT, 189, 231},
    {239, '-', TYPE_INT, TYPE_INT, 189, 238},
    {246, '+', 0, 0, 189, 245},
    {263, '<', TYPE_INT, TYPE_INT, 260, 262},
    {311, '+', 0, 0, 260, 310},
    {375, '<', TYPE_INT, TYPE_INT, 371, 374},
    {390, '<', TYPE_INT, TYPE_INT, 388, 389},
    {397, '*', TYPE_INT, TYPE_INT, 371, 396},
    {399, '+', TYPE_INT, TYPE_INT, 397, 398},
    {407, '+', 0, 0, 405, 406},
    {418, '+', 0, 0, 371, 417},
    {439, '/', TYPE_INT, TYPE_INT, 437, 438},
    {440, '<', TYPE_INT, TYPE_INT, 435, 439},
    {455, '+', 0, 0, 435, 454}
};

static const struct MirArrayMainIndex mir_array_main_indices[] = {
    {136, 134, 123, 49, 1, 1}, {141, 139, 123, 50, 2, 2},
    {146, 144, 123, 52, 4, 4}, {157, 155, 123, 17, 1, 1},
    {162, 160, 123, 18, 2, 2}, {167, 165, 123, 20, 4, 4},
    {196, 194, 189, 52, 4, 4}, {204, 202, 189, 50, 2, 2},
    {212, 210, 189, 49, 1, 1}, {220, 218, 189, 20, 4, 4},
    {229, 227, 189, 18, 2, 2}, {236, 234, 189, 17, 1, 1},
    {271, 269, 260, 49, 1, 1}, {276, 274, 260, 50, 2, 2},
    {281, 279, 260, 52, 4, 4}, {292, 290, 260, 17, 1, 1},
    {297, 295, 260, 18, 2, 2}, {302, 300, 260, 20, 4, 4},
    {319, 317, 318, 17, 1, 1}, {324, 322, 323, 17, 1, 1},
    {329, 327, 328, 17, 1, 1}, {334, 332, 333, 17, 1, 1},
    {339, 337, 338, 17, 1, 1}, {344, 342, 343, 17, 1, 1},
    {349, 347, 348, 17, 1, 1}, {354, 352, 353, 17, 1, 1},
    {400, 394, 399, 17, 1, 1}, {448, 446, 435, 81, 2, 2}
};

static const struct MirArrayMainEdge mir_array_main_edges[] = {
    {10, 18}, {13, 22}, {22, 20}, {25, 21}, {31, 22},
    {34, 13}, {37, 17}, {46, 15}, {49, 16}, {55, 17},
    {58, 8}, {61, 12}, {70, 10}, {73, 11}, {79, 12},
    {82, 3}, {85, 7}, {94, 5}, {97, 6}, {103, 7},
    {106, 1}, {129, 33}, {178, 32}, {193, 37}, {248, 36},
    {264, 41}, {313, 40}, {376, 45}, {391, 48}, {409, 47},
    {420, 44}, {441, 52}, {457, 51}
};

static const struct MirArrayMainPhi mir_array_main_phis[] = {
    {29, 24, 27, 23, 20}, {33, 12, 29, 19, 24},
    {53, 48, 51, 25, 15}, {57, 36, 53, 14, 26},
    {77, 72, 75, 27, 10}, {81, 60, 77, 9, 28},
    {101, 96, 99, 29, 5}, {105, 84, 101, 4, 30},
    {123, 118, 176, 1, 34}, {189, 183, 246, 33, 38},
    {260, 253, 311, 37, 42}, {371, 363, 418, 41, 46},
    {435, 425, 455, 45, 53}
};

static int mir_aggregate_direct_function(
    int instruction, struct Sym **function_out)
{
    const struct MirInsn *call;

    if (instruction < 0 || instruction >= mir.count ||
        mir.insns[instruction].opcode != MIR_CALL)
        return 0;
    call = &mir.insns[instruction];
    *function_out = find_global(call->name);
    if (*function_out == NULL ||
        (*function_out)->is_funcptr || (*function_out)->is_noreturn ||
        ((call->memory_flags & MIR_CALL_FLAG_VARIADIC) == 0 &&
         call->base_name[0] != 0 &&
         strcmp(call->base_name,
                asm_name_for(sym_asm_name(*function_out)))))
        return 0;
    return 1;
}

static int mir_array_main_opcode_sequence(void)
{
    int instruction;

    if (mir.count !=
        (int)(sizeof(mir_array_main_opcodes) /
              sizeof(mir_array_main_opcodes[0])))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            mir_array_main_opcodes[instruction])
            return 0;
    return 1;
}

static int mir_array_main_operations(void)
{
    size_t item;

    for (item = 0;
         item < sizeof(mir_array_main_constants) /
                sizeof(mir_array_main_constants[0]);
         ++item) {
        const struct MirArrayMainConstant *expected =
            &mir_array_main_constants[item];
        const struct MirInsn *insn =
            &mir.insns[expected->instruction];

        if (insn->opcode != MIR_CONST ||
            insn->type != expected->type ||
            insn->immediate != expected->value)
            return 0;
    }
    for (item = 0;
         item < sizeof(mir_array_main_binaries) /
                sizeof(mir_array_main_binaries[0]);
         ++item) {
        const struct MirArrayMainBinary *expected =
            &mir_array_main_binaries[item];
        const struct MirInsn *insn =
            &mir.insns[expected->instruction];

        if (insn->opcode != MIR_BINARY ||
            insn->immediate != expected->operation ||
            insn->type != expected->type ||
            insn->secondary_offset != expected->operand_type ||
            insn->src1 != mir.insns[expected->left].dst ||
            insn->src2 != mir.insns[expected->right].dst)
            return 0;
    }
    if (mir.insns[200].immediate != 0 ||
        mir.insns[200].type != (TYPE_LONG | TYPE_UNSIGNED) ||
        mir.insns[200].src1 != mir.insns[199].dst ||
        mir.insns[216].immediate != 0 ||
        mir.insns[216].type != (TYPE_CHAR | TYPE_UNSIGNED) ||
        mir.insns[216].src1 != mir.insns[215].dst ||
        mir.insns[222].immediate != 0 ||
        mir.insns[222].type != TYPE_LONG ||
        mir.insns[222].src1 != mir.insns[189].dst ||
        mir.insns[240].immediate != 0 ||
        mir.insns[240].type != TYPE_CHAR ||
        mir.insns[240].src1 != mir.insns[239].dst)
        return 0;
    return 1;
}

static int mir_array_main_graph(void)
{
    size_t item;

    for (item = 0;
         item < sizeof(mir_array_main_edges) /
                sizeof(mir_array_main_edges[0]);
         ++item) {
        const struct MirArrayMainEdge *edge =
            &mir_array_main_edges[item];
        const struct MirInsn *insn = &mir.insns[edge->instruction];

        if ((insn->opcode != MIR_BRANCH_FALSE &&
             insn->opcode != MIR_JUMP) ||
            insn->label != edge->target)
            return 0;
    }
    for (item = 0;
         item < sizeof(mir_array_main_phis) /
                sizeof(mir_array_main_phis[0]);
         ++item) {
        const struct MirArrayMainPhi *phi =
            &mir_array_main_phis[item];
        const struct MirInsn *insn = &mir.insns[phi->instruction];

        if (insn->opcode != MIR_PHI ||
            insn->src1 != mir.insns[phi->first].dst ||
            insn->src2 != mir.insns[phi->second].dst ||
            insn->phi_pred1 != phi->first_predecessor ||
            insn->phi_pred2 != phi->second_predecessor)
            return 0;
    }
    return 1;
}

static int mir_array_main_memory(void)
{
    static const short loads[][4] = {
        {137, 136, TYPE_CHAR | TYPE_UNSIGNED, 1},
        {142, 141, TYPE_INT | TYPE_UNSIGNED, 2},
        {147, 146, TYPE_LONG | TYPE_UNSIGNED, 4},
        {158, 157, TYPE_CHAR, 1}, {163, 162, TYPE_INT, 2},
        {168, 167, TYPE_LONG, 4},
        {272, 271, TYPE_CHAR | TYPE_UNSIGNED, 1},
        {277, 276, TYPE_INT | TYPE_UNSIGNED, 2},
        {282, 281, TYPE_LONG | TYPE_UNSIGNED, 4},
        {293, 292, TYPE_CHAR, 1}, {298, 297, TYPE_INT, 2},
        {303, 302, TYPE_LONG, 4}, {320, 319, TYPE_CHAR, 1},
        {325, 324, TYPE_CHAR, 1}, {330, 329, TYPE_CHAR, 1},
        {335, 334, TYPE_CHAR, 1}, {340, 339, TYPE_CHAR, 1},
        {345, 344, TYPE_CHAR, 1}, {350, 349, TYPE_CHAR, 1},
        {355, 354, TYPE_CHAR, 1}, {401, 400, TYPE_CHAR, 1},
        {449, 448, TYPE_CHAR | TYPE_PTR, 2}
    };
    static const short stores[][3] = {
        {201, 196, 200}, {209, 204, 207}, {217, 212, 216},
        {226, 220, 225}, {233, 229, 232}, {241, 236, 240}
    };
    static const short local_stores[][2] = {
        {119, 177}, {184, 247}, {254, 312},
        {364, 419}, {379, 408}, {426, 456}
    };
    int offsets[6];
    size_t item;
    size_t other;

    for (item = 0;
         item < sizeof(mir_array_main_indices) /
                sizeof(mir_array_main_indices[0]);
         ++item) {
        const struct MirArrayMainIndex *expected =
            &mir_array_main_indices[item];
        const struct MirInsn *insn =
            &mir.insns[expected->instruction];

        if (insn->opcode != MIR_INDEX_ADDRESS ||
            insn->src1 != mir.insns[expected->base].dst ||
            insn->src2 != mir.insns[expected->subscript].dst ||
            insn->type != expected->type ||
            insn->memory_size != expected->width ||
            insn->immediate != expected->stride)
            return 0;
    }
    for (item = 0; item < sizeof(loads) / sizeof(loads[0]); ++item) {
        const struct MirInsn *load = &mir.insns[loads[item][0]];

        if (load->opcode != MIR_LOAD_INDIRECT ||
            load->src1 != mir.insns[loads[item][1]].dst ||
            load->type != loads[item][2] ||
            load->memory_size != loads[item][3] ||
            load->memory_flags != 0 || load->bit_width != 0)
            return 0;
    }
    for (item = 0; item < sizeof(stores) / sizeof(stores[0]); ++item) {
        const struct MirInsn *store = &mir.insns[stores[item][0]];

        if (store->opcode != MIR_STORE_INDIRECT ||
            store->src1 != mir.insns[stores[item][1]].dst ||
            store->src2 != mir.insns[stores[item][2]].dst ||
            store->memory_size !=
                (item == 0 || item == 3 ? 4 :
                 item == 1 || item == 4 ? 2 : 1) ||
            store->memory_flags != 0 || store->bit_width != 0)
            return 0;
    }
    for (item = 0;
         item < sizeof(local_stores) / sizeof(local_stores[0]);
         ++item) {
        int type;
        int storage;
        int second_offset;

        if (!mir_scalar_memory_location(
                &mir.insns[local_stores[item][0]], &type,
                &storage, &offsets[item]) ||
            storage != SC_LOCAL || type != TYPE_INT ||
            mir.insns[local_stores[item][0]].memory_size != 2 ||
            !mir_scalar_memory_location(
                &mir.insns[local_stores[item][1]], &type,
                &storage, &second_offset) ||
            storage != SC_LOCAL || type != TYPE_INT ||
            mir.insns[local_stores[item][1]].memory_size != 2 ||
            second_offset != offsets[item])
            return 0;
    }
    for (item = 0; item < 6; ++item)
        for (other = item + 1; other < 6; ++other)
            if (offsets[item] == offsets[other])
                return 0;
    if (mir.insns[388].object != mir.insns[379].object ||
        mir.insns[398].object != mir.insns[379].object ||
        mir.insns[405].object != mir.insns[379].object ||
        mir.insns[122].object != mir.insns[2].object ||
        mir.insns[187].object != mir.insns[2].object ||
        mir.insns[257].object != mir.insns[2].object ||
        mir.insns[367].object != mir.insns[2].object ||
        mir.insns[382].object != mir.insns[2].object ||
        mir.insns[429].object != mir.insns[2].object)
        return 0;
    return 1;
}

static int mir_array_main_global(
    int instruction, int type, struct Sym **symbol_out)
{
    const struct MirInsn *address = &mir.insns[instruction];
    struct Sym *symbol;

    if (address->opcode != MIR_ADDRESS || address->type != type ||
        address->name[0] == 0 ||
        (symbol = find_global(address->name)) == NULL ||
        !symbol->is_defined || symbol->is_volatile ||
        symbol->is_funcptr)
        return 0;
    *symbol_out = symbol;
    return 1;
}

static int mir_array_main_globals(struct MirArrayMainPlan *plan)
{
    static const short first_addresses[MIR_ARRAY_MAIN_SYMBOLS] = {
        134, 139, 144, 155, 160, 165, 317, 394, 446
    };
    static const short types[MIR_ARRAY_MAIN_SYMBOLS] = {
        49, 50, 52, 17, 18, 20, 17, 17, 81
    };
    static const short repeats[][2] = {
        {210, MIR_ARRAY_MAIN_U8}, {269, MIR_ARRAY_MAIN_U8},
        {202, MIR_ARRAY_MAIN_U16}, {274, MIR_ARRAY_MAIN_U16},
        {194, MIR_ARRAY_MAIN_U32}, {279, MIR_ARRAY_MAIN_U32},
        {234, MIR_ARRAY_MAIN_I8}, {290, MIR_ARRAY_MAIN_I8},
        {227, MIR_ARRAY_MAIN_I16}, {295, MIR_ARRAY_MAIN_I16},
        {218, MIR_ARRAY_MAIN_I32}, {300, MIR_ARRAY_MAIN_I32},
        {322, MIR_ARRAY_MAIN_CHARS}, {327, MIR_ARRAY_MAIN_CHARS},
        {332, MIR_ARRAY_MAIN_CHARS}, {337, MIR_ARRAY_MAIN_CHARS},
        {342, MIR_ARRAY_MAIN_CHARS}, {347, MIR_ARRAY_MAIN_CHARS},
        {352, MIR_ARRAY_MAIN_CHARS}
    };
    struct Sym *symbol;
    size_t item;
    size_t other;

    for (item = 0; item < MIR_ARRAY_MAIN_SYMBOLS; ++item)
        if (!mir_array_main_global(
                first_addresses[item], types[item],
                &plan->symbols[item]))
            return 0;
    for (item = 0; item < sizeof(repeats) / sizeof(repeats[0]); ++item)
        if (!mir_array_main_global(
                repeats[item][0], types[repeats[item][1]], &symbol) ||
            symbol != plan->symbols[repeats[item][1]])
            return 0;
    for (item = 0; item < MIR_ARRAY_MAIN_SYMBOLS; ++item)
        for (other = item + 1;
             other < MIR_ARRAY_MAIN_SYMBOLS; ++other)
            if (plan->symbols[item] == plan->symbols[other])
                return 0;
    return 1;
}

static int mir_array_main_function(
    int instruction, int variadic, int noreturn,
    struct Sym **function_out, char *call_name, size_t call_name_size)
{
    const struct MirInsn *call = &mir.insns[instruction];
    struct Sym *function;
    const char *name;

    if (call->opcode != MIR_CALL || call->name[0] == 0 ||
        (function = find_global(call->name)) == NULL ||
        function->is_funcptr ||
        !!function->is_noreturn != noreturn ||
        !!(call->memory_flags & MIR_CALL_FLAG_VARIADIC) != variadic)
        return 0;
    name = call->base_name[0] != 0
        ? call->base_name
        : asm_name_for(sym_asm_name(function));
    if (name[0] == 0 || strlen(name) >= call_name_size)
        return 0;
    *function_out = function;
    snprintf(call_name, call_name_size, "%s", name);
    return 1;
}

static int mir_array_main_calls(struct MirArrayMainPlan *plan)
{
    static const short print_calls[MIR_ARRAY_MAIN_PRINT_CALLS] = {
        109, 150, 171, 285, 306, 357, 403, 413, 451, 462
    };
    static const short string_instructions[MIR_ARRAY_MAIN_STRINGS] = {
        107, 130, 151, 315, 392, 411, 442, 460
    };
    static const short args[][3] = {
        {108, 0, 107}, {111, 1, 110}, {131, 2, 130},
        {133, 2, 123}, {138, 2, 137}, {143, 2, 142},
        {149, 2, 147}, {152, 3, 151}, {154, 3, 123},
        {159, 3, 158}, {164, 3, 163}, {170, 3, 168},
        {266, 4, 265}, {268, 4, 260}, {273, 4, 272},
        {278, 4, 277}, {284, 4, 282}, {287, 5, 286},
        {289, 5, 260}, {294, 5, 293}, {299, 5, 298},
        {305, 5, 303}, {316, 6, 315}, {321, 6, 320},
        {326, 6, 325}, {331, 6, 330}, {336, 6, 335},
        {341, 6, 340}, {346, 6, 345}, {351, 6, 350},
        {356, 6, 355}, {393, 7, 392}, {402, 7, 401},
        {412, 8, 411}, {443, 9, 442}, {445, 9, 435},
        {450, 9, 449}, {461, 11, 460}
    };
    struct Sym *function;
    char ignored_name[64];
    size_t item;
    size_t other;

    for (item = 0; item < MIR_ARRAY_MAIN_PRINT_CALLS; ++item) {
        if (!mir_array_main_function(
                print_calls[item], 1, 0, &function,
                plan->print_names[item],
                sizeof(plan->print_names[item])))
            return 0;
        if (item == 0)
            plan->print_function = function;
        else if (function != plan->print_function)
            return 0;
    }
    if (!mir_array_main_function(
            112, 0, 1, &plan->exit_function,
            plan->exit_name, sizeof(plan->exit_name)) ||
        !mir_array_main_function(
            459, 0, 0, &plan->many_function,
            ignored_name, sizeof(ignored_name)) ||
        plan->exit_function == plan->print_function ||
        plan->many_function == plan->print_function ||
        plan->many_function == plan->exit_function)
        return 0;
    snprintf(plan->exit_name, sizeof(plan->exit_name), "%s",
             mir.insns[112].base_name[0] != 0
                 ? mir.insns[112].base_name
                 : asm_name_for(sym_asm_name(plan->exit_function)));
    for (item = 0; item < MIR_ARRAY_MAIN_STRINGS; ++item) {
        const struct MirInsn *string =
            &mir.insns[string_instructions[item]];

        if (string->opcode != MIR_STRING_ADDRESS ||
            string->type != (TYPE_CHAR | TYPE_PTR))
            return 0;
        plan->strings[item] = (int)string->immediate;
    }
    if (mir.insns[265].immediate !=
            plan->strings[MIR_ARRAY_MAIN_UNSIGNED_FORMAT] ||
        mir.insns[286].immediate !=
            plan->strings[MIR_ARRAY_MAIN_SIGNED_FORMAT])
        return 0;
    for (item = 0; item < MIR_ARRAY_MAIN_STRINGS; ++item)
        for (other = item + 1;
             other < MIR_ARRAY_MAIN_STRINGS; ++other)
            if (plan->strings[item] == plan->strings[other])
                return 0;
    for (item = 0; item < sizeof(args) / sizeof(args[0]); ++item) {
        const struct MirInsn *arg = &mir.insns[args[item][0]];

        if (arg->opcode != MIR_ARG ||
            arg->secondary_offset != args[item][1] ||
            arg->src1 != mir.insns[args[item][2]].dst)
            return 0;
    }
    if (mir.insns[109].secondary_offset != 0 ||
        mir.insns[112].secondary_offset != 1 ||
        mir.insns[150].secondary_offset != 2 ||
        mir.insns[171].secondary_offset != 3 ||
        mir.insns[285].secondary_offset != 4 ||
        mir.insns[306].secondary_offset != 5 ||
        mir.insns[357].secondary_offset != 6 ||
        mir.insns[403].secondary_offset != 7 ||
        mir.insns[413].secondary_offset != 8 ||
        mir.insns[451].secondary_offset != 9 ||
        mir.insns[459].secondary_offset != 10 ||
        mir.insns[462].secondary_offset != 11)
        return 0;
    return 1;
}

static int mir_match_array_main(struct MirArrayMainPlan *plan)
{
    int argc_offset;
    int argv_offset;

    memset(plan, 0, sizeof(*plan));
    if (!mir_array_main_opcode_sequence() ||
        mir_cfg_block_count() != 48 ||
        mir.local_bytes != 12 || mir.has_vla ||
        (mir.return_type & 15) != TYPE_INT)
        return mir_machine_reject("array-main", "shape");
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &argc_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &argv_offset) ||
        argc_offset != 2 || argv_offset != 4)
        return mir_machine_reject("array-main", "parameters");
    if (!mir_array_main_operations())
        return mir_machine_reject("array-main", "operations");
    if (!mir_array_main_graph())
        return mir_machine_reject("array-main", "graph");
    if (!mir_array_main_memory())
        return mir_machine_reject("array-main", "memory");
    if (!mir_array_main_globals(plan))
        return mir_machine_reject("array-main", "globals");
    if (!mir_array_main_calls(plan))
        return mir_machine_reject("array-main", "calls");
    if (mir.insns[464].src1 != mir.insns[463].dst)
        return mir_machine_reject("array-main", "return");
    plan->count = 8;
    plan->replacement_bias = 20;
    plan->board_columns = 8;
    return 1;
}

static int mir_aggregate_fixed_address(
    int value, int before, struct MirAggregateCheckValue *result, int depth)
{
    const struct MirInsn *definition;
    int definition_index;

    if (depth > 16 ||
        (definition = mir_definition(value)) == NULL)
        return 0;
    definition_index = (int)(definition - mir.insns);
    if (definition_index >= before)
        return 0;
    if (definition->opcode == MIR_ADDRESS) {
        int type;
        int storage;
        int offset;

        if (!mir_scalar_memory_location(
                definition, &type, &storage, &offset) ||
            (storage != SC_LOCAL && storage != SC_GLOBAL))
            return 0;
        memset(result, 0, sizeof(*result));
        if (storage == SC_LOCAL) {
            result->kind = MIR_AGGREGATE_CHECK_LOCAL;
            result->local_offset = offset;
        } else {
            result->kind = MIR_AGGREGATE_CHECK_GLOBAL;
            result->root = find_global(definition->name);
            result->root_offset = offset;
            if (result->root == NULL || !result->root->is_defined ||
                result->root->is_volatile)
                return 0;
        }
        return 1;
    }
    if (definition->opcode == MIR_MEMBER_ADDRESS) {
        if (!mir_aggregate_fixed_address(
                definition->src1, definition_index,
                result, depth + 1))
            return 0;
        if (result->kind == MIR_AGGREGATE_CHECK_LOCAL)
            result->local_offset += (int)definition->immediate;
        else
            result->root_offset += (int)definition->immediate;
        return 1;
    }
    if (definition->opcode == MIR_INDEX_ADDRESS) {
        long index;
        long adjustment;

        if (definition->immediate <= 0 ||
            !mir_machine_constant_value(
                definition->src2, &index, 0) ||
            index < 0 ||
            !mir_machine_fold_integer_binary(
                '*', index, definition->immediate,
                TYPE_INT, &adjustment) ||
            adjustment < 0 || adjustment > 32767 ||
            !mir_aggregate_fixed_address(
                definition->src1, definition_index,
                result, depth + 1))
            return 0;
        if (result->kind == MIR_AGGREGATE_CHECK_LOCAL)
            result->local_offset += (int)adjustment;
        else
            result->root_offset += (int)adjustment;
        return 1;
    }
    return 0;
}

static int mir_aggregate_check_value(
    int value, int before, struct MirAggregateCheckValue *result)
{
    const struct MirInsn *definition;
    long constant;

    definition = mir_definition(value);
    while (definition != NULL &&
           definition->opcode == MIR_UNARY &&
           definition->immediate == 0)
        definition = mir_definition(definition->src1);
    if (definition == NULL)
        return 0;
    if (mir_machine_constant_value(
            definition->dst, &constant, 0)) {
        memset(result, 0, sizeof(*result));
        result->kind = MIR_AGGREGATE_CHECK_CONSTANT;
        result->width = type_size(definition->type);
        result->is_unsigned =
            (definition->type & TYPE_UNSIGNED) != 0;
        result->value = (unsigned long)constant;
        return result->width == 1 || result->width == 2 ||
            result->width == 4;
    }
    if (definition->opcode != MIR_LOAD_INDIRECT ||
        (definition->memory_flags & (1 | 8)) != 0 ||
        definition->bit_width != 0 ||
        (definition->memory_size != 1 &&
         definition->memory_size != 2 &&
         definition->memory_size != 4) ||
        !mir_aggregate_fixed_address(
            definition->src1, before, result, 0))
        return 0;
    result->width = definition->memory_size;
    result->is_unsigned =
        (definition->type & TYPE_UNSIGNED) != 0;
    return 1;
}

static int mir_aggregate_match_check(
    int instruction, struct MirAggregateCheck *check,
    struct Sym **word_function, struct Sym **wide_function)
{
    const struct MirInsn *call;
    const struct MirInsn *string;
    const struct MirInsn *actual_definition;
    struct Sym *function;
    int arguments[3];
    long expected;
    int width;

    if (instruction < 0 || instruction >= mir.count)
        return 0;
    call = &mir.insns[instruction];
    if (!mir_aggregate_direct_function(instruction, &function) ||
        !mir_machine_three_call_arguments(call, arguments) ||
        (string = mir_definition(arguments[0])) == NULL ||
        string->opcode != MIR_STRING_ADDRESS ||
        (actual_definition = mir_definition(arguments[1])) == NULL ||
        !mir_machine_constant_value(arguments[2], &expected, 0))
        return 0;
    width = type_size(actual_definition->type);
    if (width != 2 && width != 4)
        return 0;
    if (width == 2) {
        if (*word_function == NULL)
            *word_function = function;
        else if (*word_function != function)
            return 0;
    } else {
        if (*wide_function == NULL)
            *wide_function = function;
        else if (*wide_function != function)
            return 0;
    }
    memset(check, 0, sizeof(*check));
    check->function = function;
    check->string_id = (int)string->immediate;
    check->width = width;
    check->expected = (unsigned long)expected;
    return mir_aggregate_check_value(
        arguments[1], instruction, &check->actual);
}

static int mir_aggregate_local_location(
    const struct MirInsn *insn, int width, int *offset_out)
{
    int type;
    int storage;
    int offset;

    if (insn == NULL || !mir_scalar_memory_location(
            insn, &type, &storage, &offset) ||
        storage != SC_LOCAL || insn->memory_size != width ||
    (insn->memory_flags & (1 | 8)) != 0)
        return 0;
    *offset_out = offset;
    return 1;
}

static int mir_touch_local_convert_integer(
    long value, int type, long *result)
{
    int width = type_size(type);
    unsigned long bits;

    if (type_ptr_depth(type) > 0) {
        *result = value & 0xffffL;
        return type_size(type) == 2;
    }
    if (type_is_float(type) || (width != 1 && width != 2 && width != 4))
        return 0;
    bits = (unsigned long)value;
    if (width == 1)
        bits &= 0xffUL;
    else if (width == 2)
        bits &= 0xffffUL;
    else
        bits &= 0xffffffffUL;
    if ((type & TYPE_UNSIGNED) != 0) {
        *result = (long)bits;
        return 1;
    }
    if (width == 1 && (bits & 0x80UL) != 0)
        *result = (long)bits - 0x100L;
    else if (width == 2 && (bits & 0x8000UL) != 0)
        *result = (long)bits - 0x10000L;
    else if (width == 4 && (bits & 0x80000000UL) != 0)
        *result = (long)((long long)bits - 0x100000000LL);
    else
        *result = (long)bits;
    return 1;
}

static int mir_touch_local_ranges_overlap(
    int left, int left_width, int right, int right_width)
{
    return left < right + right_width && right < left + left_width;
}

static void mir_touch_local_memory_clear(
    struct MirTouchLocalMemory *memory, int *memory_count,
    int address, int width)
{
    int item = 0;

    while (item < *memory_count) {
        if (!mir_touch_local_ranges_overlap(
                memory[item].address, memory[item].width,
                address, width)) {
            ++item;
            continue;
        }
        memory[item] = memory[--*memory_count];
    }
}

static int mir_touch_local_memory_set(
    struct MirTouchLocalMemory *memory, int *memory_count,
    int address, int width, const struct MirTouchLocalValue *value)
{
    mir_touch_local_memory_clear(memory, memory_count, address, width);
    if (*memory_count >= MIR_TOUCH_LOCAL_MEMORY_MAX)
        return 0;
    memory[*memory_count].address = address;
    memory[*memory_count].width = width;
    memory[*memory_count].value = *value;
    ++*memory_count;
    return 1;
}

static int mir_touch_local_memory_get(
    const struct MirTouchLocalMemory *memory, int memory_count,
    int address, int width, struct MirTouchLocalValue *value)
{
    int item;

    for (item = 0; item < memory_count; ++item)
        if (memory[item].address == address &&
            memory[item].width == width) {
            *value = memory[item].value;
            value->origin_address = address;
            value->origin_width = width;
            return 1;
        }
    memset(value, 0, sizeof(*value));
    return 0;
}

static int mir_touch_local_memory_copy(
    struct MirTouchLocalMemory *memory, int *memory_count,
    int destination, int source, int width)
{
    struct MirTouchLocalMemory copied[MIR_TOUCH_LOCAL_MEMORY_MAX];
    int copied_count = 0;
    int item;

    for (item = 0; item < *memory_count; ++item) {
        int offset = memory[item].address - source;

        if (offset < 0 ||
            offset + memory[item].width > width)
            continue;
        copied[copied_count] = memory[item];
        copied[copied_count].address = destination + offset;
        ++copied_count;
    }
    mir_touch_local_memory_clear(
        memory, memory_count, destination, width);
    for (item = 0; item < copied_count; ++item)
        if (!mir_touch_local_memory_set(
                memory, memory_count,
                copied[item].address, copied[item].width,
                &copied[item].value))
            return 0;
    return 1;
}

static int mir_touch_local_value_for_address(
    const struct MirInsn *insn, struct MirTouchLocalValue *value)
{
    int type;
    int storage;
    int offset;

    if (!mir_scalar_memory_location(
            insn, &type, &storage, &offset) ||
        storage != SC_LOCAL ||
        mir_declared_is_vla_object(insn->name))
        return 0;
    memset(value, 0, sizeof(*value));
    value->kind = MIR_TOUCH_LOCAL_ADDRESS;
    value->value = offset;
    return 1;
}

static int mir_touch_local_step(
    int instruction, struct MirTouchLocalValue *values,
    struct MirTouchLocalMemory *memory, int *memory_count)
{
    const struct MirInsn *insn = &mir.insns[instruction];
    struct MirTouchLocalValue result;

    memset(&result, 0, sizeof(result));
    switch (insn->opcode) {
    case MIR_CONST:
        if (!mir_touch_local_convert_integer(
                insn->immediate, insn->type, &result.value))
            return 0;
        result.kind = MIR_TOUCH_LOCAL_INTEGER;
        break;
    case MIR_ADDRESS:
        if (!mir_touch_local_value_for_address(insn, &result))
            return 0;
        break;
    case MIR_MEMBER_ADDRESS:
        if (insn->src1 < 0 || insn->src1 >= mir.next_value ||
            values[insn->src1].kind != MIR_TOUCH_LOCAL_ADDRESS)
            break;
        result = values[insn->src1];
        result.value += insn->immediate;
        break;
    case MIR_INDEX_ADDRESS:
        if (insn->src1 < 0 || insn->src1 >= mir.next_value ||
            insn->src2 < 0 || insn->src2 >= mir.next_value ||
            values[insn->src1].kind != MIR_TOUCH_LOCAL_ADDRESS ||
            values[insn->src2].kind != MIR_TOUCH_LOCAL_INTEGER ||
            insn->immediate <= 0)
            break;
        result = values[insn->src1];
        result.value += values[insn->src2].value * insn->immediate;
        break;
    case MIR_BINARY:
        if (insn->src1 < 0 || insn->src1 >= mir.next_value ||
            insn->src2 < 0 || insn->src2 >= mir.next_value)
            return 0;
        if (values[insn->src1].kind == MIR_TOUCH_LOCAL_INTEGER &&
            values[insn->src2].kind == MIR_TOUCH_LOCAL_INTEGER) {
            if (!mir_machine_fold_integer_binary(
                    (int)insn->immediate,
                    values[insn->src1].value,
                    values[insn->src2].value,
                    insn->type, &result.value))
                break;
            result.kind = MIR_TOUCH_LOCAL_INTEGER;
        } else if (insn->immediate == '+' &&
                   values[insn->src1].kind ==
                       MIR_TOUCH_LOCAL_ADDRESS &&
                   values[insn->src2].kind ==
                       MIR_TOUCH_LOCAL_INTEGER) {
            result = values[insn->src1];
            result.value += values[insn->src2].value;
        } else if (insn->immediate == '+' &&
                   values[insn->src2].kind ==
                       MIR_TOUCH_LOCAL_ADDRESS &&
                   values[insn->src1].kind ==
                       MIR_TOUCH_LOCAL_INTEGER) {
            result = values[insn->src2];
            result.value += values[insn->src1].value;
        } else if (insn->immediate == '-' &&
                   values[insn->src1].kind ==
                       MIR_TOUCH_LOCAL_ADDRESS &&
                   values[insn->src2].kind ==
                       MIR_TOUCH_LOCAL_INTEGER) {
            result = values[insn->src1];
            result.value -= values[insn->src2].value;
        }
        break;
    case MIR_UNARY:
        if (insn->immediate != 0 ||
            insn->src1 < 0 || insn->src1 >= mir.next_value)
            break;
        result = values[insn->src1];
        if (result.kind == MIR_TOUCH_LOCAL_INTEGER &&
            !mir_touch_local_convert_integer(
                result.value, insn->type, &result.value))
            memset(&result, 0, sizeof(result));
        else if (result.kind == MIR_TOUCH_LOCAL_ADDRESS &&
                 (type_ptr_depth(insn->type) == 0 ||
                  type_size(insn->type) != 2))
            memset(&result, 0, sizeof(result));
        break;
    case MIR_LOAD:
    {
        int type;
        int storage;
        int offset;
        int width;

        if (!mir_scalar_memory_location(
                insn, &type, &storage, &offset) ||
            storage != SC_LOCAL)
            return 0;
        width = insn->memory_size != 0
            ? insn->memory_size : type_size(type);
        if (!mir_touch_local_value_for_address(insn, &result) ||
            !mir_touch_local_memory_get(
                memory, *memory_count, (int)result.value,
                width, &result))
            memset(&result, 0, sizeof(result));
        break;
    }
    case MIR_LOAD_INDIRECT:
        if (insn->src1 < 0 || insn->src1 >= mir.next_value ||
            values[insn->src1].kind != MIR_TOUCH_LOCAL_ADDRESS ||
            !mir_touch_local_memory_get(
                memory, *memory_count,
                (int)values[insn->src1].value,
                insn->memory_size, &result))
            memset(&result, 0, sizeof(result));
        break;
    case MIR_STORE:
        if (insn->src1 < 0 || insn->src1 >= mir.next_value ||
            !mir_touch_local_value_for_address(insn, &result))
            return 0;
        if (values[insn->src1].kind == MIR_TOUCH_LOCAL_UNKNOWN)
            mir_touch_local_memory_clear(
                memory, memory_count, (int)result.value,
                insn->memory_size);
        else if (!mir_touch_local_memory_set(
                     memory, memory_count, (int)result.value,
                     insn->memory_size, &values[insn->src1]))
            return 0;
        break;
    case MIR_STORE_INDIRECT:
        if (insn->src1 < 0 || insn->src1 >= mir.next_value ||
            insn->src2 < 0 || insn->src2 >= mir.next_value)
            return 0;
        if (values[insn->src1].kind != MIR_TOUCH_LOCAL_ADDRESS)
            break;
        if (values[insn->src2].kind == MIR_TOUCH_LOCAL_UNKNOWN)
            mir_touch_local_memory_clear(
                memory, memory_count,
                (int)values[insn->src1].value,
                insn->memory_size);
        else if (!mir_touch_local_memory_set(
                     memory, memory_count,
                     (int)values[insn->src1].value,
                     insn->memory_size, &values[insn->src2]))
            return 0;
        break;
    case MIR_COPY_AGGREGATE:
        if (insn->src1 < 0 || insn->src1 >= mir.next_value ||
            insn->src2 < 0 || insn->src2 >= mir.next_value ||
            values[insn->src1].kind != MIR_TOUCH_LOCAL_ADDRESS ||
            values[insn->src2].kind != MIR_TOUCH_LOCAL_ADDRESS ||
            !mir_touch_local_memory_copy(
                memory, memory_count,
                (int)values[insn->src1].value,
                (int)values[insn->src2].value,
                insn->memory_size))
            return 0;
        break;
    default:
        break;
    }
    if (insn->dst >= 0) {
        if (insn->dst >= mir.next_value)
            return 0;
        values[insn->dst] = result;
    }
    return 1;
}

static unsigned long mir_touch_local_bits(long value, int width)
{
    if (width == 1)
        return (unsigned long)value & 0xffUL;
    if (width == 2)
        return (unsigned long)value & 0xffffUL;
    return (unsigned long)value & 0xffffffffUL;
}

static int mir_match_touch_locals(
    struct MirTouchLocalsPlan *plan)
{
    static const int expected_counts[MIR_RETURN + 1] = {
        [MIR_LABEL] = 22,
        [MIR_NOP] = 113,
        [MIR_CONST] = 359,
        [MIR_LOAD] = 48,
        [MIR_STORE] = 20,
        [MIR_ADDRESS] = 139,
        [MIR_INDEX_ADDRESS] = 243,
        [MIR_MEMBER_ADDRESS] = 121,
        [MIR_LOAD_INDIRECT] = 42,
        [MIR_STORE_INDIRECT] = 96,
        [MIR_COPY_AGGREGATE] = 2,
        [MIR_BINARY] = 79,
        [MIR_UNARY] = 2,
        [MIR_STRING_ADDRESS] = 27,
        [MIR_ARG] = 81,
        [MIR_CALL] = 27,
        [MIR_PHI] = 4,
        [MIR_BRANCH_FALSE] = 7,
        [MIR_JUMP] = 7
    };
    static const int label_indices[22] = {
        0, 4, 32, 65, 71, 73, 79, 83, 107, 113, 117,
        127, 157, 163, 164, 170, 238, 248, 420, 426, 614, 620
    };
    static const int branch_indices[7] =
        {9, 38, 89, 123, 133, 244, 254};
    static const int branch_targets[7] =
        {79, 71, 113, 170, 163, 620, 426};
    static const int jump_indices[7] =
        {70, 78, 112, 162, 169, 425, 619};
    static const int jump_targets[7] =
        {32, 4, 83, 127, 117, 248, 238};
    static const int phi_indices[4] = {5, 84, 118, 239};
    static const int phi_predecessors[4][2] = {
        {0, 73}, {79, 107}, {113, 164}, {170, 614}
    };
    static const int final_store_indices[MIR_TOUCH_LOCAL_CHECK_COUNT] = {
        800, 811, 821, 829, 842, 854, 867, 881, 893,
        906, 920, 928, 938, 945, 957, 968, 976, 989,
        1002, 1014, 1028, 1043, 1055, 1067, 1080, 1094, 1111
    };
    static const int call_indices[MIR_TOUCH_LOCAL_CHECK_COUNT] = {
        1123, 1135, 1148, 1159, 1175, 1190, 1200, 1213, 1226,
        1236, 1246, 1258, 1270, 1282, 1295, 1308, 1319, 1335,
        1350, 1360, 1371, 1384, 1397, 1407, 1417, 1427, 1438
    };
    struct MirTouchLocalValue *values;
    struct MirTouchLocalMemory memory[MIR_TOUCH_LOCAL_MEMORY_MAX];
    struct Sym *check_functions[3] = {NULL, NULL, NULL};
    int original_addresses[MIR_TOUCH_LOCAL_CHECK_COUNT];
    int opcode_counts[MIR_RETURN + 1];
    int memory_count = 0;
    int instruction;
    int item;
    int result = 0;
    const char *reject_reason = "abstract";

    memset(plan, 0, sizeof(*plan));
    memset(opcode_counts, 0, sizeof(opcode_counts));
    if (mir.count != 1439 || mir.next_value != 1198 ||
        mir_cfg_block_count() != 22 || mir.local_bytes != 962 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return mir_machine_reject("touch-locals", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        int type = 0;
        int storage = 0;
        int offset = 0;

        if (insn->opcode < 0 || insn->opcode > MIR_RETURN)
            return mir_machine_reject("touch-locals", "opcode-range");
        ++opcode_counts[insn->opcode];
        switch (insn->opcode) {
        case MIR_LABEL:
        case MIR_NOP:
        case MIR_CONST:
        case MIR_INDEX_ADDRESS:
        case MIR_MEMBER_ADDRESS:
        case MIR_BINARY:
        case MIR_UNARY:
        case MIR_STRING_ADDRESS:
        case MIR_ARG:
        case MIR_PHI:
        case MIR_BRANCH_FALSE:
        case MIR_JUMP:
            break;
        case MIR_ADDRESS:
            if (!mir_scalar_memory_location(
                    insn, &type, &storage, &offset) ||
                storage != SC_LOCAL ||
                mir_declared_is_vla_object(insn->name))
                return mir_machine_reject("touch-locals", "address");
            break;
        case MIR_LOAD:
            if (!mir_scalar_memory_location(
                    insn, &type, &storage, &offset) ||
                storage != SC_LOCAL ||
                (insn->memory_flags & (1 | 8)) != 0 ||
                insn->bit_width != 0 ||
                (insn->memory_size != 0 &&
                 insn->memory_size != 1 &&
                 insn->memory_size != 2 &&
                 insn->memory_size != 4) ||
                (insn->memory_size == 0 &&
                 type_size(type) != 1 &&
                 type_size(type) != 2 &&
                 type_size(type) != 4)) {
                if (getenv("DCC_MIR_MACHINE_REPORT") != NULL)
                    fprintf(stderr,
                            "; MIR machine function=%s template=touch-locals"
                            " instruction=%d load flags=%d bits=%d"
                            " size=%d storage=%d\n",
                            mir.name, instruction, insn->memory_flags,
                            insn->bit_width, insn->memory_size, storage);
                return mir_machine_reject("touch-locals", "load");
            }
            break;
        case MIR_STORE:
            if ((insn->memory_flags & (1 | 8)) != 0 ||
                insn->bit_width != 0 ||
                (insn->memory_size != 1 &&
                 insn->memory_size != 2 &&
                 insn->memory_size != 4) ||
                !mir_machine_unobservable_local_store(insn))
                return mir_machine_reject("touch-locals", "store");
            break;
        case MIR_LOAD_INDIRECT:
        case MIR_STORE_INDIRECT:
            if ((insn->memory_flags & (1 | 8)) != 0 ||
                insn->bit_width != 0 ||
                (insn->memory_size != 1 &&
                 insn->memory_size != 2 &&
                 insn->memory_size != 4))
                return mir_machine_reject(
                    "touch-locals", "indirect-memory");
            break;
        case MIR_COPY_AGGREGATE:
            if (insn->memory_flags != 0 ||
                insn->memory_size != 155 ||
                (instruction != 629 && instruction != 638))
                return mir_machine_reject("touch-locals", "aggregate-copy");
            break;
        case MIR_CALL:
            if (instruction < call_indices[0] ||
                insn->memory_flags != 0)
                return mir_machine_reject("touch-locals", "call-phase");
            break;
        default:
            return mir_machine_reject("touch-locals", "opcode");
        }
    }
    for (instruction = 0; instruction <= MIR_RETURN; ++instruction)
        if (opcode_counts[instruction] != expected_counts[instruction])
            return mir_machine_reject("touch-locals", "census");
    for (item = 0; item < 22; ++item)
        if (mir.insns[label_indices[item]].opcode != MIR_LABEL)
            return mir_machine_reject("touch-locals", "labels");
    for (item = 0; item < 7; ++item) {
        if (mir.insns[branch_indices[item]].opcode != MIR_BRANCH_FALSE ||
            mir.insns[branch_indices[item]].label !=
                mir.insns[branch_targets[item]].label ||
            mir.insns[jump_indices[item]].opcode != MIR_JUMP ||
            mir.insns[jump_indices[item]].label !=
                mir.insns[jump_targets[item]].label)
            return mir_machine_reject("touch-locals", "branches");
    }
    for (item = 0; item < 4; ++item) {
        const struct MirInsn *phi = &mir.insns[phi_indices[item]];

        if (phi->opcode != MIR_PHI ||
            phi->phi_pred1 !=
                mir.insns[phi_predecessors[item][0]].label ||
            phi->phi_pred2 !=
                mir.insns[phi_predecessors[item][1]].label)
            return mir_machine_reject("touch-locals", "phis");
    }
    values = (struct MirTouchLocalValue *)calloc(
        (size_t)mir.next_value, sizeof(*values));
    if (values == NULL)
        return mir_machine_reject("touch-locals", "allocation");
    for (instruction = 170; instruction < mir.count; ++instruction) {
        if (!mir_touch_local_step(
                instruction, values, memory, &memory_count)) {
            reject_reason = "abstract-step";
            goto cleanup;
        }
        if (instruction >= final_store_indices[0] &&
            instruction <= final_store_indices[
                MIR_TOUCH_LOCAL_CHECK_COUNT - 1] &&
            mir.insns[instruction].opcode == MIR_STORE_INDIRECT) {
            const struct MirInsn *store = &mir.insns[instruction];
            const struct MirInsn *source;
            int store_index = -1;
            int prior;

            for (item = 0; item < MIR_TOUCH_LOCAL_CHECK_COUNT; ++item)
                if (final_store_indices[item] == instruction) {
                    store_index = item;
                    break;
                }
            if (store_index < 0 ||
                store->src1 < 0 || store->src1 >= mir.next_value ||
                store->src2 < 0 || store->src2 >= mir.next_value ||
                values[store->src1].kind != MIR_TOUCH_LOCAL_ADDRESS ||
                values[store->src2].kind != MIR_TOUCH_LOCAL_INTEGER ||
                (source = mir_definition(store->src2)) == NULL ||
                type_ptr_depth(source->type) != 0 ||
                type_is_float(source->type) ||
                type_size(source->type) != store->memory_size) {
                reject_reason = "final-store";
                goto cleanup;
            }
            original_addresses[store_index] =
                (int)values[store->src1].value;
            for (prior = 0; prior < store_index; ++prior)
                if (original_addresses[prior] ==
                    original_addresses[store_index]) {
                    reject_reason = "store-alias";
                    goto cleanup;
                }
            plan->stores[store_index].width = store->memory_size;
            plan->stores[store_index].value =
                mir_touch_local_bits(
                    values[store->src2].value, store->memory_size);
        }
        if (mir.insns[instruction].opcode == MIR_CALL) {
            const struct MirInsn *call = &mir.insns[instruction];
            const struct MirInsn *string;
            const struct MirInsn *actual;
            const struct MirInsn *expected_definition;
            struct Sym *function;
            int arguments[3];
            int check_index = -1;
            int function_index;
            long expected;

            for (item = 0; item < MIR_TOUCH_LOCAL_CHECK_COUNT; ++item)
                if (call_indices[item] == instruction) {
                    check_index = item;
                    break;
                }
            if (check_index < 0 ||
                !mir_aggregate_direct_function(
                    instruction, &function) ||
                !mir_machine_three_call_arguments(call, arguments) ||
                (string = mir_definition(arguments[0])) == NULL ||
                string->opcode != MIR_STRING_ADDRESS ||
                arguments[1] < 0 || arguments[1] >= mir.next_value ||
                values[arguments[1]].kind != MIR_TOUCH_LOCAL_INTEGER ||
                values[arguments[1]].origin_width == 0 ||
                values[arguments[1]].origin_address !=
                    original_addresses[check_index] ||
                (actual = mir_definition(arguments[1])) == NULL ||
                (expected_definition =
                    mir_definition(arguments[2])) == NULL ||
                type_ptr_depth(actual->type) != 0 ||
                type_is_float(actual->type) ||
                type_ptr_depth(expected_definition->type) != 0 ||
                type_is_float(expected_definition->type) ||
                !mir_machine_constant_value(
                    arguments[2], &expected, 0) ||
                values[arguments[1]].origin_width !=
                    plan->stores[check_index].width ||
                type_size(expected_definition->type) !=
                    (plan->stores[check_index].width == 4 ? 4 : 2) ||
                mir_touch_local_bits(
                    expected,
                    plan->stores[check_index].width) !=
                    plan->stores[check_index].value ||
                type_ptr_depth(call->type) != 0 ||
                (call->type & 15) != TYPE_VOID) {
                reject_reason = "check";
                goto cleanup;
            }
            function_index =
                plan->stores[check_index].width == 1 ? 0 :
                plan->stores[check_index].width == 2 ? 1 : 2;
            if (check_functions[function_index] == NULL)
                check_functions[function_index] = function;
            else if (check_functions[function_index] != function) {
                reject_reason = "check-function";
                goto cleanup;
            }
            plan->checks[check_index].function = function;
            plan->checks[check_index].expected =
                plan->stores[check_index].value;
            plan->checks[check_index].string_id =
                (int)string->immediate;
            plan->checks[check_index].width =
                plan->stores[check_index].width;
            plan->checks[check_index].is_unsigned =
                (actual->type & TYPE_UNSIGNED) != 0;
            plan->checks[check_index].store_index = check_index;
        }
    }
    if (check_functions[0] == NULL ||
        check_functions[1] == NULL ||
        check_functions[2] == NULL ||
        check_functions[0] == check_functions[1] ||
        check_functions[0] == check_functions[2] ||
        check_functions[1] == check_functions[2])
    {
        reject_reason = "check-function-set";
        goto cleanup;
    }
    for (item = 0; item < MIR_TOUCH_LOCAL_CHECK_COUNT; ++item) {
        plan->frame_bytes += plan->stores[item].width;
        plan->stores[item].compact_offset = -plan->frame_bytes;
    }
    result = plan->frame_bytes > 0 && plan->frame_bytes <= 120;
cleanup:
    free(values);
    if (!result)
        mir_machine_reject("touch-locals", reject_reason);
    return result;
}

static int mir_packed_scalar_type(
    int type, int base, int is_unsigned, int pointer_depth)
{
    return type_ptr_depth(type) == pointer_depth &&
           (type & 15) == base &&
           ((type & TYPE_UNSIGNED) != 0) == is_unsigned;
}

static int mir_packed_constant(
    int instruction, long expected, int base, int is_unsigned)
{
    const struct MirInsn *insn;

    if (instruction < 0 || instruction >= mir.count)
        return 0;
    insn = &mir.insns[instruction];
    return insn->opcode == MIR_CONST &&
           insn->immediate == expected &&
           mir_packed_scalar_type(insn->type, base, is_unsigned, 0);
}

static int mir_packed_binary(
    int instruction, int left, int right, int operation,
    int base, int is_unsigned)
{
    const struct MirInsn *insn;

    if (instruction < 0 || instruction >= mir.count)
        return 0;
    insn = &mir.insns[instruction];
    return insn->opcode == MIR_BINARY &&
           insn->src1 == mir.insns[left].dst &&
           insn->src2 == mir.insns[right].dst &&
           insn->immediate == operation &&
           mir_packed_scalar_type(insn->type, base, is_unsigned, 0);
}

static int mir_packed_unary(
    int instruction, int source, int operation,
    int base, int is_unsigned)
{
    const struct MirInsn *insn;

    if (instruction < 0 || instruction >= mir.count)
        return 0;
    insn = &mir.insns[instruction];
    return insn->opcode == MIR_UNARY &&
           insn->src1 == mir.insns[source].dst &&
           insn->immediate == operation &&
           mir_packed_scalar_type(insn->type, base, is_unsigned, 0);
}

static int mir_packed_call_arguments(
    const struct MirInsn *call, int count, int *arguments)
{
    int found = 0;
    int instruction;

    memset(arguments, 0xff, (size_t)count * sizeof(*arguments));
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *arg = &mir.insns[instruction];
        int index;

        if (arg->opcode != MIR_ARG ||
            arg->secondary_offset != call->secondary_offset)
            continue;
        index = (int)arg->immediate;
        if (index < 0 || index >= count || arguments[index] >= 0)
            return 0;
        arguments[index] = arg->src1;
        ++found;
    }
    return found == count;
}

static struct Sym *mir_packed_global_array(
    int instruction, int count, int stride)
{
    const struct MirInsn *address;
    struct Sym *symbol;

    if (instruction < 0 || instruction >= mir.count)
        return NULL;
    address = &mir.insns[instruction];
    if (address->opcode != MIR_ADDRESS ||
        type_ptr_depth(address->type) != 1)
        return NULL;
    symbol = find_global(address->name);
    if (symbol == NULL || !symbol->is_defined || symbol->is_volatile ||
        symbol->storage != SC_GLOBAL || !symbol->is_array ||
        symbol->is_vla || symbol->has_init || symbol->init_count != 0 ||
        symbol->dim_count != 1 || symbol->dims[0] != count ||
        symbol->array_len != count || symbol->elem_size != stride ||
        symbol->size != count * stride)
        return NULL;
    return symbol;
}

static int mir_packed_same_global(
    int instruction, struct Sym *symbol)
{
    const struct MirInsn *address;

    if (instruction < 0 || instruction >= mir.count)
        return 0;
    address = &mir.insns[instruction];
    return address->opcode == MIR_ADDRESS &&
           find_global(address->name) == symbol;
}

static int mir_packed_member(
    int instruction, int base_instruction, int offset,
    int scalar_base, int is_unsigned, int width)
{
    const struct MirInsn *member;

    if (instruction < 0 || instruction >= mir.count)
        return 0;
    member = &mir.insns[instruction];
    return member->opcode == MIR_MEMBER_ADDRESS &&
           member->src1 == mir.insns[base_instruction].dst &&
           member->immediate == offset &&
           member->memory_size == width &&
           member->memory_flags == 0 &&
           mir_packed_scalar_type(
               member->type, scalar_base, is_unsigned, 1);
}

static int mir_packed_load(
    int instruction, int address_instruction,
    int scalar_base, int is_unsigned, int width)
{
    const struct MirInsn *load;

    if (instruction < 0 || instruction >= mir.count)
        return 0;
    load = &mir.insns[instruction];
    return load->opcode == MIR_LOAD_INDIRECT &&
           load->src1 == mir.insns[address_instruction].dst &&
           load->memory_size == width &&
           load->memory_flags == 0 &&
           load->bit_width == 0 &&
           mir_packed_scalar_type(
               load->type, scalar_base, is_unsigned, 0);
}

static int mir_packed_store(
    int instruction, int address_instruction, int value_instruction,
    int width)
{
    const struct MirInsn *store;

    if (instruction < 0 || instruction >= mir.count)
        return 0;
    store = &mir.insns[instruction];
    return store->opcode == MIR_STORE_INDIRECT &&
           store->src1 == mir.insns[address_instruction].dst &&
           store->src2 == mir.insns[value_instruction].dst &&
           store->memory_size == width &&
           store->memory_flags == 0 &&
           store->bit_width == 0;
}

static int mir_packed_branch(int instruction, int value, int target)
{
    return mir.insns[instruction].opcode == MIR_BRANCH_FALSE &&
           mir.insns[instruction].src1 == mir.insns[value].dst &&
           mir.insns[instruction].label == mir.insns[target].label;
}

static int mir_packed_jump(int instruction, int target)
{
    return mir.insns[instruction].opcode == MIR_JUMP &&
           mir.insns[instruction].label == mir.insns[target].label;
}

static int mir_packed_phi(
    int instruction, int first, int second,
    int first_predecessor, int second_predecessor)
{
    const struct MirInsn *phi = &mir.insns[instruction];

    return phi->opcode == MIR_PHI &&
           phi->src1 == mir.insns[first].dst &&
           phi->src2 == mir.insns[second].dst &&
           phi->phi_pred1 == mir.insns[first_predecessor].label &&
           phi->phi_pred2 == mir.insns[second_predecessor].label &&
           mir_packed_scalar_type(phi->type, TYPE_INT, 1, 0);
}

static int mir_packed_direct_function(
    int instruction, struct Sym **function_out)
{
    const struct MirInsn *call;
    struct Sym *function;

    if (instruction < 0 || instruction >= mir.count ||
        mir.insns[instruction].opcode != MIR_CALL)
        return 0;
    call = &mir.insns[instruction];
    function = find_global(call->name);
    if (function == NULL || function->storage != SC_FUNC ||
        function->is_funcptr || function->is_noreturn)
        return 0;
    *function_out = function;
    return 1;
}

static const char *mir_packed_call_name(
    const struct MirInsn *call, struct Sym *function)
{
    return call->base_name[0] != 0
        ? call->base_name
        : asm_name_for(sym_asm_name(function));
}

static struct Sym *mir_multidim_array_root(int instruction)
{
    const struct MirInsn *address;
    struct Sym *root;
    int type;
    int storage;
    int offset;

    if (instruction < 0 || instruction >= mir.count)
        return NULL;
    address = &mir.insns[instruction];
    if (address->opcode != MIR_ADDRESS ||
        !mir_scalar_memory_location(
            address, &type, &storage, &offset) ||
        storage != SC_GLOBAL || offset != 0)
        return NULL;
    root = find_global(address->name);
    if (root == NULL || !root->is_defined || root->is_volatile ||
        root->is_array || root->is_vla || root->has_init)
        return NULL;
    return root;
}

static int mir_multidim_fixed_address(
    int value, struct Sym **root_out, long *offset_out, int depth)
{
    const struct MirInsn *definition;

    if (depth > 16 ||
        (definition = mir_definition(value)) == NULL)
        return 0;
    if (definition->opcode == MIR_ADDRESS) {
        int instruction = (int)(definition - mir.insns);
        struct Sym *root = mir_multidim_array_root(instruction);

        if (root == NULL)
            return 0;
        *root_out = root;
        *offset_out = 0;
        return 1;
    }
    if (definition->opcode == MIR_MEMBER_ADDRESS) {
        long offset;

        if (!mir_multidim_fixed_address(
                definition->src1, root_out, &offset, depth + 1))
            return 0;
        *offset_out = offset + definition->immediate;
        return 1;
    }
    if (definition->opcode == MIR_INDEX_ADDRESS) {
        long index;
        long offset;

        if (!mir_multidim_fixed_address(
                definition->src1, root_out, &offset, depth + 1) ||
            !mir_machine_constant_value(
                definition->src2, &index, 0))
            return 0;
        *offset_out = offset + index * definition->immediate;
        return *offset_out >= 0 && *offset_out <= 32767;
    }
    return 0;
}

static int mir_multidim_fixed_memory(
    int instruction, int opcode, struct Sym *root,
    int offset, int width)
{
    const struct MirInsn *memory;
    struct Sym *actual_root;
    long actual_offset;
    int address_value;

    if (instruction < 0 || instruction >= mir.count)
        return 0;
    memory = &mir.insns[instruction];
    if (memory->opcode != opcode ||
        memory->memory_size != width ||
        memory->memory_flags != 0 ||
        memory->bit_width != 0)
        return 0;
    address_value = memory->src1;
    return mir_multidim_fixed_address(
               address_value, &actual_root, &actual_offset, 0) &&
           actual_root == root && actual_offset == offset;
}

static int mir_multidim_member_group(
    const int *instructions, int count, int offset, int size)
{
    int item;

    for (item = 0; item < count; ++item) {
        const struct MirInsn *member =
            &mir.insns[instructions[item]];

        if (member->opcode != MIR_MEMBER_ADDRESS ||
            member->src1 != mir.insns[instructions[item] - 1].dst ||
            member->immediate != offset ||
            member->memory_size != size)
            return 0;
    }
    return 1;
}

static int mir_multidim_index_group(
    const int *instructions, int count, int stride, int size)
{
    int item;

    for (item = 0; item < count; ++item) {
        int instruction = instructions[item];
        const struct MirInsn *index = &mir.insns[instruction];

        if (index->opcode != MIR_INDEX_ADDRESS ||
            index->immediate != stride ||
            index->memory_size != size)
            return 0;
    }
    return 1;
}

static int mir_multidim_phi(
    int instruction, int first, int second,
    int first_predecessor, int second_predecessor)
{
    const struct MirInsn *phi = &mir.insns[instruction];

    return phi->opcode == MIR_PHI &&
           phi->src1 == mir.insns[first].dst &&
           phi->src2 == mir.insns[second].dst &&
           phi->phi_pred1 == mir.insns[first_predecessor].label &&
           phi->phi_pred2 == mir.insns[second_predecessor].label &&
           mir_packed_scalar_type(phi->type, TYPE_INT, 0, 0);
}

static int mir_match_multidim_array_runner(
    struct MirMultidimArrayRunner *plan)
{
    static const int expected_opcodes[680] = {
        MIR_LABEL, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_PHI, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_LOAD, MIR_BINARY, MIR_UNARY, MIR_STORE_INDIRECT, MIR_LABEL,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_INDEX_ADDRESS, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_CONST, MIR_STORE_INDIRECT, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_INDEX_ADDRESS,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CALL, MIR_INDEX_ADDRESS, MIR_CALL, MIR_INDEX_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_INDEX_ADDRESS, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_INDEX_ADDRESS, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_INDEX_ADDRESS, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_PHI,
        MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD, MIR_INDEX_ADDRESS, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_BINARY, MIR_LOAD,
        MIR_BINARY, MIR_STORE_INDIRECT, MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_LABEL, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_INDEX_ADDRESS, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_INDEX_ADDRESS, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_CONST, MIR_STORE_INDIRECT, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_LOAD, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_CONST,
        MIR_RETURN, MIR_NOP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_RETURN
    };
    static const int byte_root_addresses[] = {
        1, 10, 19, 28, 39, 53, 67, 81, 112, 141, 155, 167,
        171, 175, 177, 181, 190, 204, 206, 210, 220, 224, 228, 239
    };
    static const int word_root_addresses[] = {
        251, 259, 267, 275, 285, 298, 311, 324, 335,
        339, 343, 345, 349, 357, 370, 372, 376
    };
    static const int cube_root_addresses[] = {
        416, 457, 472, 487, 502, 515, 519, 523, 529, 531, 535, 539
    };
    static const int grid_root_addresses[] =
        {548, 560, 572, 584, 598, 615, 632, 649};
    static const int byte_array_members[] = {
        2, 11, 20, 29, 40, 54, 68, 82, 113, 142, 156, 176,
        191, 205, 229, 240
    };
    static const int byte_row_members[] = {168, 178, 207, 221};
    static const int byte_column_members[] = {172, 182, 211, 225};
    static const int word_array_members[] =
        {252, 260, 268, 276, 286, 299, 312, 325, 344, 358, 371};
    static const int word_row_members[] = {336, 346, 373};
    static const int word_column_members[] = {340, 350, 377};
    static const int cube_array_members[] = {417, 458, 473, 488, 503, 530};
    static const int cube_a_members[] = {516, 532};
    static const int cube_b_members[] = {520, 536};
    static const int cube_d_members[] = {524, 540};
    static const int grid_cells_members[] = {549, 561, 573, 585, 599, 616, 633, 650};
    static const int grid_array_members[] = {552, 564, 576, 588, 602, 619, 636, 653};
    static const int byte_row_indices[] =
        {4, 13, 22, 31, 42, 56, 70, 84, 115, 144, 158, 180, 193, 209, 231, 242};
    static const int byte_column_indices[] =
        {6, 15, 24, 33, 44, 58, 72, 86, 117, 146, 160, 184, 195, 213, 233, 244};
    static const int word_row_indices[] =
        {254, 262, 270, 278, 288, 301, 314, 327, 348, 360, 375};
    static const int word_column_indices[] =
        {256, 264, 272, 280, 290, 303, 316, 329, 352, 362, 379};
    static const int cube_plane_indices[] = {419, 460, 475, 490, 505, 534};
    static const int cube_row_indices[] = {421, 462, 477, 492, 507, 538};
    static const int cube_column_indices[] = {423, 464, 479, 494, 509, 542};
    static const int grid_cell_indices[] = {551, 563, 575, 587, 601, 618, 635, 652};
    static const int grid_row_indices[] = {554, 566, 578, 590, 604, 621, 638, 655};
    static const int grid_column_indices[] = {556, 568, 580, 592, 606, 623, 640, 657};
    static const int check_calls[MIR_MULTIDIM_ARRAY_CHECK_COUNT] = {
        50, 64, 78, 92, 152, 166, 201, 219, 250, 295, 308, 321,
        334, 367, 384, 469, 484, 499, 514, 547, 612, 629, 646, 663
    };
    static const int check_strings[MIR_MULTIDIM_ARRAY_CHECK_COUNT] = {
        37, 51, 65, 79, 139, 153, 188, 202, 237, 283, 296, 309,
        322, 355, 368, 455, 470, 485, 500, 527, 596, 613, 630, 647
    };
    static const int check_actuals[MIR_MULTIDIM_ARRAY_CHECK_COUNT] = {
        46, 60, 74, 88, 148, 162, 197, 215, 246, 291, 304, 317,
        330, 363, 380, 465, 480, 495, 510, 543, 608, 625, 642, 659
    };
    static const int check_loads[MIR_MULTIDIM_ARRAY_CHECK_COUNT] = {
        45, 59, 73, 87, 147, 161, 196, 214, 245, 291, 304, 317,
        330, 363, 380, 465, 480, 495, 510, 543, 607, 624, 641, 658
    };
    static const int check_expected[MIR_MULTIDIM_ARRAY_CHECK_COUNT] = {
        48, 62, 76, 90, 150, 164, 199, 217, 248, 293, 306, 319,
        332, 365, 382, 467, 482, 497, 512, 545, 610, 627, 644, 661
    };
    static const int check_roots[MIR_MULTIDIM_ARRAY_CHECK_COUNT] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0,
        1, 1, 1, 1, 1, 1,
        2, 2, 2, 2, 2,
        3, 3, 3, 3
    };
    static const int check_offsets[MIR_MULTIDIM_ARRAY_CHECK_COUNT] = {
        0, 4, 11, 6, 11, 5, 9, -1, 7,
        0, 8, 22, 12, 20, -1,
        0, 46, 24, 18, -1,
        0, 8, 13, 9
    };
    static const int check_values[MIR_MULTIDIM_ARRAY_CHECK_COUNT] = {
        1, 15, 42, 99, 11, 5, 77, 77, 55,
        1000, 2000, 3003, 1202, 4242, 4242,
        0, 123, 100, 21, 123, 10, 21, 32, 23
    };
    struct Sym *roots[4];
    struct Sym *function;
    int arguments[3];
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 680 || mir.next_value != 538 ||
        mir_cfg_block_count() != 17 || mir.local_bytes != 10 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        !mir_packed_scalar_type(mir.return_type, TYPE_INT, 0, 0))
        return mir_machine_reject("multidim-array-runner", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "multidim-array-runner", "opcodes");

    roots[0] = mir_multidim_array_root(1);
    roots[1] = mir_multidim_array_root(251);
    roots[2] = mir_multidim_array_root(416);
    roots[3] = mir_multidim_array_root(548);
    if (roots[0] == NULL || roots[1] == NULL ||
        roots[2] == NULL || roots[3] == NULL ||
        roots[0] == roots[1] || roots[0] == roots[2] ||
        roots[0] == roots[3] || roots[1] == roots[2] ||
        roots[1] == roots[3] || roots[2] == roots[3])
        return mir_machine_reject(
            "multidim-array-runner", "roots");
#define MIR_CHECK_ROOT_GROUP(group, root) \
    do { \
        for (item = 0; item < \
                (int)(sizeof(group) / sizeof((group)[0])); ++item) \
            if (mir_multidim_array_root((group)[item]) != (root)) \
                return mir_machine_reject( \
                    "multidim-array-runner", "root-alias"); \
    } while (0)
    MIR_CHECK_ROOT_GROUP(byte_root_addresses, roots[0]);
    MIR_CHECK_ROOT_GROUP(word_root_addresses, roots[1]);
    MIR_CHECK_ROOT_GROUP(cube_root_addresses, roots[2]);
    MIR_CHECK_ROOT_GROUP(grid_root_addresses, roots[3]);
#undef MIR_CHECK_ROOT_GROUP

    plan->byte_array_offset = (int)mir.insns[2].immediate;
    plan->byte_row_offset = (int)mir.insns[168].immediate;
    plan->byte_column_offset = (int)mir.insns[172].immediate;
    plan->word_array_offset = (int)mir.insns[252].immediate;
    plan->word_row_offset = (int)mir.insns[336].immediate;
    plan->word_column_offset = (int)mir.insns[340].immediate;
    plan->cube_array_offset = (int)mir.insns[417].immediate;
    plan->cube_a_offset = (int)mir.insns[516].immediate;
    plan->cube_b_offset = (int)mir.insns[520].immediate;
    plan->cube_d_offset = (int)mir.insns[524].immediate;
    plan->grid_cells_offset = (int)mir.insns[549].immediate;
    plan->grid_array_offset = (int)mir.insns[552].immediate;
    plan->byte_row_stride = (int)mir.insns[4].immediate;
    plan->byte_column_stride = (int)mir.insns[6].immediate;
    plan->word_row_stride = (int)mir.insns[254].immediate;
    plan->word_column_stride = (int)mir.insns[256].immediate;
    plan->cube_plane_stride = (int)mir.insns[419].immediate;
    plan->cube_row_stride = (int)mir.insns[421].immediate;
    plan->cube_column_stride = (int)mir.insns[423].immediate;
    plan->grid_cell_stride = (int)mir.insns[551].immediate;
    plan->grid_row_stride = (int)mir.insns[554].immediate;
    plan->grid_column_stride = (int)mir.insns[556].immediate;
    plan->byte_rows = 3;
    plan->byte_columns = 4;
    plan->word_rows = 3;
    plan->word_columns = 4;
    plan->cube_planes = 2;
    plan->cube_rows = 3;
    plan->cube_columns = 4;
    if (plan->byte_array_offset != 0 ||
        plan->byte_row_offset != 12 ||
        plan->byte_column_offset != 14 ||
        plan->byte_row_stride != 4 ||
        plan->byte_column_stride != 1 ||
        roots[0]->size != 16 ||
        plan->word_array_offset != 0 ||
        plan->word_row_offset != 24 ||
        plan->word_column_offset != 26 ||
        plan->word_row_stride != 8 ||
        plan->word_column_stride != 2 ||
        roots[1]->size != 28 ||
        plan->cube_array_offset != 0 ||
        plan->cube_a_offset != 48 ||
        plan->cube_b_offset != 50 ||
        plan->cube_d_offset != 52 ||
        plan->cube_plane_stride != 24 ||
        plan->cube_row_stride != 8 ||
        plan->cube_column_stride != 2 ||
        roots[2]->size != 54 ||
        plan->grid_cells_offset != 0 ||
        plan->grid_cell_stride != 6 ||
        plan->grid_array_offset != 0 ||
        plan->grid_row_stride != 2 ||
        plan->grid_column_stride != 1 ||
        roots[3]->size != 18)
        return mir_machine_reject(
            "multidim-array-runner", "layout");
    if (!mir_multidim_member_group(
            byte_array_members,
            sizeof(byte_array_members) / sizeof(byte_array_members[0]),
            plan->byte_array_offset, 12) ||
        !mir_multidim_member_group(
            byte_row_members,
            sizeof(byte_row_members) / sizeof(byte_row_members[0]),
            plan->byte_row_offset, 2) ||
        !mir_multidim_member_group(
            byte_column_members,
            sizeof(byte_column_members) / sizeof(byte_column_members[0]),
            plan->byte_column_offset, 2) ||
        !mir_multidim_member_group(
            word_array_members,
            sizeof(word_array_members) / sizeof(word_array_members[0]),
            plan->word_array_offset, 24) ||
        !mir_multidim_member_group(
            word_row_members,
            sizeof(word_row_members) / sizeof(word_row_members[0]),
            plan->word_row_offset, 2) ||
        !mir_multidim_member_group(
            word_column_members,
            sizeof(word_column_members) / sizeof(word_column_members[0]),
            plan->word_column_offset, 2) ||
        !mir_multidim_member_group(
            cube_array_members,
            sizeof(cube_array_members) / sizeof(cube_array_members[0]),
            plan->cube_array_offset, 48) ||
        !mir_multidim_member_group(
            cube_a_members,
            sizeof(cube_a_members) / sizeof(cube_a_members[0]),
            plan->cube_a_offset, 2) ||
        !mir_multidim_member_group(
            cube_b_members,
            sizeof(cube_b_members) / sizeof(cube_b_members[0]),
            plan->cube_b_offset, 2) ||
        !mir_multidim_member_group(
            cube_d_members,
            sizeof(cube_d_members) / sizeof(cube_d_members[0]),
            plan->cube_d_offset, 2) ||
        !mir_multidim_member_group(
            grid_cells_members,
            sizeof(grid_cells_members) / sizeof(grid_cells_members[0]),
            plan->grid_cells_offset, 18) ||
        !mir_multidim_member_group(
            grid_array_members,
            sizeof(grid_array_members) / sizeof(grid_array_members[0]),
            plan->grid_array_offset, 4))
        return mir_machine_reject(
            "multidim-array-runner", "members");
    if (!mir_multidim_index_group(
            byte_row_indices,
            sizeof(byte_row_indices) / sizeof(byte_row_indices[0]),
            plan->byte_row_stride, 1) ||
        !mir_multidim_index_group(
            byte_column_indices,
            sizeof(byte_column_indices) / sizeof(byte_column_indices[0]),
            plan->byte_column_stride, 1) ||
        !mir_multidim_index_group(
            word_row_indices,
            sizeof(word_row_indices) / sizeof(word_row_indices[0]),
            plan->word_row_stride, 2) ||
        !mir_multidim_index_group(
            word_column_indices,
            sizeof(word_column_indices) / sizeof(word_column_indices[0]),
            plan->word_column_stride, 2) ||
        !mir_multidim_index_group(
            cube_plane_indices,
            sizeof(cube_plane_indices) / sizeof(cube_plane_indices[0]),
            plan->cube_plane_stride, 2) ||
        !mir_multidim_index_group(
            cube_row_indices,
            sizeof(cube_row_indices) / sizeof(cube_row_indices[0]),
            plan->cube_row_stride, 2) ||
        !mir_multidim_index_group(
            cube_column_indices,
            sizeof(cube_column_indices) / sizeof(cube_column_indices[0]),
            plan->cube_column_stride, 2) ||
        !mir_multidim_index_group(
            grid_cell_indices,
            sizeof(grid_cell_indices) / sizeof(grid_cell_indices[0]),
            plan->grid_cell_stride, 6) ||
        !mir_multidim_index_group(
            grid_row_indices,
            sizeof(grid_row_indices) / sizeof(grid_row_indices[0]),
            plan->grid_row_stride, 1) ||
        !mir_multidim_index_group(
            grid_column_indices,
            sizeof(grid_column_indices) / sizeof(grid_column_indices[0]),
            plan->grid_column_stride, 1))
        return mir_machine_reject(
            "multidim-array-runner", "strides");

    for (item = 0; item < MIR_MULTIDIM_ARRAY_CHECK_COUNT; ++item) {
        const struct MirInsn *call = &mir.insns[check_calls[item]];
        const struct MirInsn *string = &mir.insns[check_strings[item]];
        struct Sym *root = roots[check_roots[item]];
        long expected;

        if (!mir_packed_direct_function(
                check_calls[item], &function) ||
            call->memory_flags != 0 || call->src1 >= 0 ||
            !mir_packed_call_arguments(call, 3, arguments) ||
            arguments[0] != string->dst ||
            arguments[1] != mir.insns[check_actuals[item]].dst ||
            arguments[2] != mir.insns[check_expected[item]].dst ||
            string->opcode != MIR_STRING_ADDRESS ||
            string->immediate < 0 ||
            !mir_packed_scalar_type(
                string->type, TYPE_CHAR, 0, 1) ||
            !mir_machine_constant_value(
                mir.insns[check_expected[item]].dst,
                &expected, 0) ||
            expected != check_values[item] ||
            (item != 0 && function != plan->check_function))
            return mir_machine_reject(
                "multidim-array-runner", "check-calls");
        if (item == 0)
            plan->check_function = function;
        plan->check_strings[item] = (int)string->immediate;
        if (item > 0) {
            int prior;

            for (prior = 0; prior < item; ++prior)
                if (plan->check_strings[prior] ==
                    plan->check_strings[item])
                    return mir_machine_reject(
                        "multidim-array-runner", "check-strings");
        }
        if (item < 9 || item >= 20) {
            if (!mir_packed_unary(
                    check_actuals[item], check_loads[item],
                    0, TYPE_INT, 0) ||
                !mir_packed_load(
                    check_loads[item],
                    check_loads[item] - 1,
                    TYPE_CHAR, 1, 1))
                return mir_machine_reject(
                    "multidim-array-runner", "byte-checks");
        } else if (!mir_packed_load(
                       check_loads[item],
                       check_loads[item] - 1,
                       TYPE_INT, 0, 2)) {
            return mir_machine_reject(
                "multidim-array-runner", "word-checks");
        }
        if (check_offsets[item] >= 0 &&
            !mir_multidim_fixed_memory(
                check_loads[item], MIR_LOAD_INDIRECT,
                root, check_offsets[item],
                (item < 9 || item >= 20) ? 1 : 2))
            return mir_machine_reject(
                "multidim-array-runner", "check-addresses");
    }
    if (!plan->check_function->has_proto ||
        plan->check_function->proto_variadic ||
        plan->check_function->proto_nargs != 3 ||
        (plan->check_function->type & 15) != TYPE_VOID ||
        !mir_packed_scalar_type(
            plan->check_function->proto_types[0],
            TYPE_CHAR, 0, 1) ||
        !mir_packed_scalar_type(
            plan->check_function->proto_types[1],
            TYPE_INT, 0, 0) ||
        !mir_packed_scalar_type(
            plan->check_function->proto_types[2],
            TYPE_INT, 0, 0))
        return mir_machine_reject(
            "multidim-array-runner", "check-prototype");

    if (!mir_multidim_phi(97, 93, 135, 0, 132) ||
        !mir_packed_binary(100, 97, 99, '<', TYPE_INT, 0) ||
        !mir_packed_branch(101, 100, 138) ||
        !mir_packed_binary(110, 108, 109, '<', TYPE_INT, 0) ||
        !mir_packed_branch(111, 110, 131) ||
        !mir_packed_binary(120, 97, 119, '*', TYPE_INT, 0) ||
        !mir_packed_binary(122, 120, 121, '+', TYPE_INT, 0) ||
        !mir_packed_unary(123, 122, 0, TYPE_CHAR, 1) ||
        !mir_packed_binary(128, 126, 127, '+', TYPE_INT, 0) ||
        !mir_packed_jump(130, 105) ||
        !mir_packed_binary(135, 97, 134, '+', TYPE_INT, 0) ||
        !mir_packed_jump(137, 96) ||
        !mir_machine_constant_equals(mir.insns[93].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[99].dst, plan->byte_rows) ||
        !mir_machine_constant_equals(mir.insns[102].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[109].dst, plan->byte_columns) ||
        !mir_machine_constant_equals(
            mir.insns[119].dst, plan->byte_columns) ||
        !mir_machine_constant_equals(mir.insns[127].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[134].dst, 1) ||
        !mir_packed_store(124, 117, 123, 1))
        return mir_machine_reject(
            "multidim-array-runner", "byte-loop");

    if (!mir_multidim_phi(389, 385, 451, 138, 448) ||
        !mir_packed_binary(393, 389, 392, '<', TYPE_INT, 0) ||
        !mir_packed_branch(394, 393, 454) ||
        !mir_packed_binary(403, 401, 402, '<', TYPE_INT, 0) ||
        !mir_packed_branch(404, 403, 447) ||
        !mir_packed_binary(414, 412, 413, '<', TYPE_INT, 0) ||
        !mir_packed_branch(415, 414, 440) ||
        !mir_packed_binary(426, 389, 425, '*', TYPE_INT, 0) ||
        !mir_packed_binary(429, 427, 428, '*', TYPE_INT, 0) ||
        !mir_packed_binary(430, 426, 429, '+', TYPE_INT, 0) ||
        !mir_packed_binary(432, 430, 431, '+', TYPE_INT, 0) ||
        !mir_packed_store(433, 423, 432, 2) ||
        !mir_packed_binary(437, 435, 436, '+', TYPE_INT, 0) ||
        !mir_packed_jump(439, 408) ||
        !mir_packed_binary(444, 442, 443, '+', TYPE_INT, 0) ||
        !mir_packed_jump(446, 398) ||
        !mir_packed_binary(451, 389, 450, '+', TYPE_INT, 0) ||
        !mir_packed_jump(453, 388) ||
        !mir_machine_constant_equals(mir.insns[385].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[392].dst, plan->cube_planes) ||
        !mir_machine_constant_equals(mir.insns[395].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[402].dst, plan->cube_rows) ||
        !mir_machine_constant_equals(mir.insns[405].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[413].dst, plan->cube_columns) ||
        !mir_machine_constant_equals(mir.insns[425].dst, 100) ||
        !mir_machine_constant_equals(mir.insns[428].dst, 10) ||
        !mir_machine_constant_equals(mir.insns[436].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[443].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[450].dst, 1))
        return mir_machine_reject(
            "multidim-array-runner", "cube-loop");

    if (!mir_multidim_fixed_memory(9, MIR_STORE_INDIRECT, roots[0], 0, 1) ||
        !mir_multidim_fixed_memory(18, MIR_STORE_INDIRECT, roots[0], 4, 1) ||
        !mir_multidim_fixed_memory(27, MIR_STORE_INDIRECT, roots[0], 11, 1) ||
        !mir_multidim_fixed_memory(36, MIR_STORE_INDIRECT, roots[0], 6, 1) ||
        !mir_multidim_fixed_memory(258, MIR_STORE_INDIRECT, roots[1], 0, 2) ||
        !mir_multidim_fixed_memory(266, MIR_STORE_INDIRECT, roots[1], 8, 2) ||
        !mir_multidim_fixed_memory(274, MIR_STORE_INDIRECT, roots[1], 22, 2) ||
        !mir_multidim_fixed_memory(282, MIR_STORE_INDIRECT, roots[1], 12, 2) ||
        !mir_multidim_fixed_memory(559, MIR_STORE_INDIRECT, roots[3], 0, 1) ||
        !mir_multidim_fixed_memory(571, MIR_STORE_INDIRECT, roots[3], 8, 1) ||
        !mir_multidim_fixed_memory(583, MIR_STORE_INDIRECT, roots[3], 13, 1) ||
        !mir_multidim_fixed_memory(595, MIR_STORE_INDIRECT, roots[3], 9, 1) ||
        !mir_machine_constant_equals(mir.insns[9].src2, 1) ||
        !mir_machine_constant_equals(mir.insns[18].src2, 15) ||
        !mir_machine_constant_equals(mir.insns[27].src2, 42) ||
        !mir_machine_constant_equals(mir.insns[36].src2, 99) ||
        !mir_machine_constant_equals(mir.insns[258].src2, 1000) ||
        !mir_machine_constant_equals(mir.insns[266].src2, 2000) ||
        !mir_machine_constant_equals(mir.insns[274].src2, 3003) ||
        !mir_machine_constant_equals(mir.insns[282].src2, 1202) ||
        !mir_machine_constant_equals(mir.insns[559].src2, 10) ||
        !mir_machine_constant_equals(mir.insns[571].src2, 21) ||
        !mir_machine_constant_equals(mir.insns[583].src2, 32) ||
        !mir_machine_constant_equals(mir.insns[595].src2, 23))
        return mir_machine_reject(
            "multidim-array-runner", "initializers");

    if (!mir_packed_store(170, 168, 169, 2) ||
        !mir_packed_store(174, 172, 173, 2) ||
        !mir_machine_constant_equals(mir.insns[170].src2, 2) ||
        !mir_machine_constant_equals(mir.insns[174].src2, 1) ||
        !mir_packed_load(179, 178, TYPE_INT, 0, 2) ||
        mir.insns[180].src1 != mir.insns[176].dst ||
        !mir_packed_store(187, 184, 186, 1) ||
        !mir_machine_constant_equals(mir.insns[187].src2, 77) ||
        mir.insns[180].src2 != mir.insns[179].dst ||
        !mir_packed_load(183, 182, TYPE_INT, 0, 2) ||
        mir.insns[184].src1 != mir.insns[180].dst ||
        mir.insns[184].src2 != mir.insns[183].dst ||
        !mir_packed_load(208, 207, TYPE_INT, 0, 2) ||
        mir.insns[209].src1 != mir.insns[205].dst ||
        mir.insns[209].src2 != mir.insns[208].dst ||
        !mir_packed_load(212, 211, TYPE_INT, 0, 2) ||
        mir.insns[213].src1 != mir.insns[209].dst ||
        mir.insns[213].src2 != mir.insns[212].dst ||
        !mir_packed_store(223, 221, 222, 2) ||
        !mir_packed_store(227, 225, 226, 2) ||
        !mir_machine_constant_equals(mir.insns[223].src2, 1) ||
        !mir_machine_constant_equals(mir.insns[227].src2, 3) ||
        !mir_packed_direct_function(230, &plan->row_function) ||
        !mir_packed_direct_function(232, &plan->column_function) ||
        plan->row_function == plan->column_function ||
        !mir_machine_call_has_no_arguments(&mir.insns[230]) ||
        !mir_machine_call_has_no_arguments(&mir.insns[232]) ||
        mir.insns[231].src1 != mir.insns[229].dst ||
        mir.insns[231].src2 != mir.insns[230].dst ||
        mir.insns[233].src1 != mir.insns[231].dst ||
        mir.insns[233].src2 != mir.insns[232].dst ||
        !mir_packed_store(236, 233, 235, 1) ||
        !mir_machine_constant_equals(mir.insns[236].src2, 55) ||
        !plan->row_function->has_proto ||
        plan->row_function->proto_variadic ||
        plan->row_function->proto_nargs != 0 ||
        !mir_packed_scalar_type(
            plan->row_function->type, TYPE_INT, 0, 0) ||
        !plan->column_function->has_proto ||
        plan->column_function->proto_variadic ||
        plan->column_function->proto_nargs != 0 ||
        !mir_packed_scalar_type(
            plan->column_function->type, TYPE_INT, 0, 0))
        return mir_machine_reject(
            "multidim-array-runner", "byte-aliases");

    if (!mir_packed_store(338, 336, 337, 2) ||
        !mir_packed_store(342, 340, 341, 2) ||
        !mir_machine_constant_equals(mir.insns[338].src2, 2) ||
        !mir_machine_constant_equals(mir.insns[342].src2, 2) ||
        !mir_packed_load(347, 346, TYPE_INT, 0, 2) ||
        mir.insns[348].src1 != mir.insns[344].dst ||
        mir.insns[348].src2 != mir.insns[347].dst ||
        !mir_packed_load(351, 350, TYPE_INT, 0, 2) ||
        mir.insns[352].src1 != mir.insns[348].dst ||
        mir.insns[352].src2 != mir.insns[351].dst ||
        !mir_packed_store(354, 352, 353, 2) ||
        !mir_machine_constant_equals(mir.insns[354].src2, 4242) ||
        !mir_packed_load(374, 373, TYPE_INT, 0, 2) ||
        mir.insns[375].src1 != mir.insns[371].dst ||
        mir.insns[375].src2 != mir.insns[374].dst ||
        !mir_packed_load(378, 377, TYPE_INT, 0, 2) ||
        mir.insns[379].src1 != mir.insns[375].dst ||
        mir.insns[379].src2 != mir.insns[378].dst ||
        mir.insns[419].src1 != mir.insns[417].dst ||
        mir.insns[419].src2 != mir.insns[389].dst ||
        mir.insns[421].src1 != mir.insns[419].dst ||
        mir.insns[421].src2 != mir.insns[420].dst ||
        mir.insns[423].src1 != mir.insns[421].dst ||
        mir.insns[423].src2 != mir.insns[422].dst ||
        !mir_packed_store(518, 516, 517, 2) ||
        !mir_packed_store(522, 520, 521, 2) ||
        !mir_packed_store(526, 524, 525, 2) ||
        !mir_machine_constant_equals(mir.insns[518].src2, 1) ||
        !mir_machine_constant_equals(mir.insns[522].src2, 2) ||
        !mir_machine_constant_equals(mir.insns[526].src2, 3) ||
        !mir_packed_load(533, 532, TYPE_INT, 0, 2) ||
        mir.insns[534].src1 != mir.insns[530].dst ||
        mir.insns[534].src2 != mir.insns[533].dst ||
        !mir_packed_load(537, 536, TYPE_INT, 0, 2) ||
        mir.insns[538].src1 != mir.insns[534].dst ||
        mir.insns[538].src2 != mir.insns[537].dst ||
        !mir_packed_load(541, 540, TYPE_INT, 0, 2) ||
        mir.insns[542].src1 != mir.insns[538].dst ||
        mir.insns[542].src2 != mir.insns[541].dst)
        return mir_machine_reject(
            "multidim-array-runner", "word-aliases");

    if (mir.insns[664].opcode != MIR_LOAD ||
        mir.insns[668].opcode != MIR_LOAD ||
        !mir_machine_same_location(&mir.insns[664], &mir.insns[668]) ||
        !mir_scalar_memory_location(
            &mir.insns[664], &instruction, &item, &arguments[0]) ||
        item != SC_GLOBAL || instruction != TYPE_INT ||
        arguments[0] != 0 ||
        (plan->failures = find_global(mir.insns[664].name)) == NULL ||
        !plan->failures->is_defined || plan->failures->is_volatile ||
        plan->failures == roots[0] ||
        plan->failures == roots[1] ||
        plan->failures == roots[2] ||
        plan->failures == roots[3] ||
        !mir_packed_branch(665, 664, 674) ||
        !mir_packed_direct_function(670, &plan->print_function) ||
        !mir_packed_direct_function(677, &function) ||
        function != plan->print_function ||
        mir.insns[670].memory_flags != MIR_CALL_FLAG_VARIADIC ||
        mir.insns[677].memory_flags != MIR_CALL_FLAG_VARIADIC ||
        !mir_packed_call_arguments(&mir.insns[670], 2, arguments) ||
        arguments[0] != mir.insns[666].dst ||
        arguments[1] != mir.insns[668].dst ||
        !mir_packed_call_arguments(&mir.insns[677], 1, arguments) ||
        arguments[0] != mir.insns[675].dst ||
        !mir_packed_scalar_type(
            mir.insns[666].type, TYPE_CHAR, 0, 1) ||
        !mir_packed_scalar_type(
            mir.insns[675].type, TYPE_CHAR, 0, 1) ||
        !mir_machine_constant_equals(mir.insns[672].src1, 1) ||
        !mir_machine_constant_equals(mir.insns[679].src1, 0) ||
        !plan->print_function->has_proto ||
        !plan->print_function->proto_variadic ||
        plan->print_function->proto_nargs != 1 ||
        !mir_packed_scalar_type(
            plan->print_function->type, TYPE_INT, 0, 0) ||
        !mir_packed_scalar_type(
            plan->print_function->proto_types[0],
            TYPE_CHAR, 0, 1))
        return mir_machine_reject(
            "multidim-array-runner", "returns");
    plan->failure_string = (int)mir.insns[666].immediate;
    plan->success_string = (int)mir.insns[675].immediate;
    if (plan->failure_string < 0 || plan->success_string < 0 ||
        plan->failure_string == plan->success_string)
        return mir_machine_reject(
            "multidim-array-runner", "summary-strings");
    snprintf(plan->failure_call_name,
             sizeof(plan->failure_call_name), "%s",
             mir_packed_call_name(
                 &mir.insns[670], plan->print_function));
    snprintf(plan->success_call_name,
             sizeof(plan->success_call_name), "%s",
             mir_packed_call_name(
                 &mir.insns[677], plan->print_function));
    plan->byte_matrix = roots[0];
    plan->word_matrix = roots[1];
    plan->cube = roots[2];
    plan->grid = roots[3];
    return 1;
}

static int mir_match_packed_record_runner(
    struct MirPackedRecordRunner *plan)
{
    static const int expected_opcodes[319] = {
        MIR_LABEL, MIR_ADDRESS, MIR_NOP, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CONST, MIR_NOP, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_NOP,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_PHI, MIR_NOP, MIR_CONST, MIR_CONST, MIR_BINARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_STORE, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_NOP,
        MIR_UNARY, MIR_STORE_INDIRECT, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_NOP,
        MIR_NOP, MIR_CONST, MIR_NOP, MIR_BINARY, MIR_STORE_INDIRECT,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_NOP, MIR_UNARY, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_UNARY, MIR_STORE_INDIRECT,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_NOP, MIR_NOP, MIR_UNARY,
        MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE_INDIRECT, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_ADDRESS,
        MIR_NOP, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CONST, MIR_NOP,
        MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_NOP, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP,
        MIR_PHI, MIR_NOP, MIR_CONST, MIR_CONST, MIR_BINARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_STORE, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_NOP, MIR_NOP, MIR_ARG, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_ARG, MIR_NOP, MIR_UNARY,
        MIR_ARG, MIR_CALL, MIR_LABEL, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_NOP, MIR_NOP, MIR_CONST, MIR_NOP,
        MIR_BINARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_NOP, MIR_NOP, MIR_ARG, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_ARG, MIR_NOP, MIR_UNARY,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_ARG, MIR_CALL, MIR_LABEL,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_UNARY,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_ARG,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_ARG,
        MIR_NOP, MIR_UNARY, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_ARG,
        MIR_CALL, MIR_LABEL, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP,
        MIR_ARG, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY,
        MIR_ARG, MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_ARG, MIR_CALL,
        MIR_LABEL, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP,
        MIR_NOP, MIR_UNARY, MIR_CONST, MIR_BINARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP,
        MIR_ARG, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY,
        MIR_ARG, MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_UNARY, MIR_UNARY,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_ARG,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_ARG,
        MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_ARG, MIR_CALL, MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL,
        MIR_ADDRESS, MIR_NOP, MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG,
        MIR_CONST, MIR_NOP, MIR_ARG, MIR_CALL
    };
    static const int labels[13] = {
        0, 30, 89, 95, 125, 160, 188,
        216, 241, 270, 300, 302, 308
    };
    static const int first_member_addresses[6] =
        {44, 49, 57, 65, 72, 80};
    static const int first_member_bases[6] =
        {43, 48, 56, 64, 71, 79};
    static const int first_stores[6] =
        {47, 55, 63, 70, 78, 87};
    static const int first_values[6] =
        {46, 54, 62, 69, 77, 86};
    static const int second_member_addresses[12] = {
        140, 152, 162, 177, 190, 205,
        218, 232, 243, 258, 272, 288
    };
    static const int second_member_bases[12] = {
        139, 151, 161, 176, 189, 204,
        217, 231, 242, 257, 271, 287
    };
    static const int second_loads[12] = {
        141, 153, 163, 178, 191, 206,
        219, 233, 244, 259, 273, 289
    };
    static const int second_members[12] =
        {0, 0, 1, 1, 2, 2, 3, 3, 4, 1, 5, 2};
    static const int member_kinds[6] = {
        TYPE_CHAR, TYPE_INT, TYPE_LONG,
        TYPE_CHAR, TYPE_INT, TYPE_LONG
    };
    static const int member_unsigned[6] = {1, 1, 1, 0, 0, 0};
    static const int member_widths[6] = {1, 2, 4, 1, 2, 4};
    static const int print_calls[6] = {159, 187, 215, 240, 269, 299};
    static const int print_strings[6] = {146, 171, 199, 226, 252, 282};
    static const int print_actuals[6] = {154, 179, 206, 234, 260, 289};
    static const int print_expected[6] = {157, 185, 213, 238, 267, 297};
    static const int print_actual_unsigned[6] = {1, 1, 1, 0, 0, 1};
    struct Sym *memset_function = NULL;
    struct Sym *print_function = NULL;
    struct Sym *dump_function = NULL;
    int arguments[4];
    int destination;
    int fill;
    int count;
    int instruction;
    int item;
    int first_zero;
    int second_zero;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 319 || mir.next_value != 235 ||
        mir_cfg_block_count() != 13 || mir.local_bytes != 8 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        (mir.return_type & 15) != TYPE_VOID ||
        type_ptr_depth(mir.return_type) != 0)
        return mir_machine_reject("packed-record-runner", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
                expected_opcodes[instruction] &&
            !(((instruction == 27 || instruction == 122) &&
               mir.insns[instruction].opcode == MIR_NOP) ||
              ((instruction == 28 || instruction == 123) &&
               mir.insns[instruction].opcode == MIR_CONST)))
            return mir_machine_reject(
                "packed-record-runner", "opcodes");
    for (item = 0; item < 13; ++item)
        if (mir.insns[labels[item]].opcode != MIR_LABEL)
            return mir_machine_reject(
                "packed-record-runner", "labels");

    if (!mir_packed_constant(4, 0, TYPE_INT, 0) ||
        !mir_packed_constant(6, 140, TYPE_INT, 0) ||
        !mir_packed_constant(13, 0, TYPE_INT, 0) ||
        !mir_packed_constant(15, 140, TYPE_INT, 0) ||
        !(mir_packed_constant(27, 0, TYPE_INT, 0) ||
          mir_packed_constant(28, 0, TYPE_INT, 1)) ||
        !mir_packed_constant(33, 280, TYPE_INT, 0) ||
        !mir_packed_constant(34, 14, TYPE_INT, 0) ||
        !mir_packed_constant(52, 2, TYPE_INT, 0) ||
        !mir_packed_constant(61, 4, TYPE_LONG, 1) ||
        !mir_packed_constant(76, 2, TYPE_INT, 0) ||
        !mir_packed_constant(85, 4, TYPE_LONG, 0) ||
        mir.insns[91].immediate != 1 ||
        !mir_packed_constant(99, 0, TYPE_INT, 0) ||
        !mir_packed_constant(101, 140, TYPE_INT, 0) ||
        !mir_packed_constant(108, 0, TYPE_INT, 0) ||
        !mir_packed_constant(110, 140, TYPE_INT, 0) ||
        !(mir_packed_constant(122, 0, TYPE_INT, 0) ||
          mir_packed_constant(123, 0, TYPE_INT, 1)) ||
        !mir_packed_constant(129, 280, TYPE_INT, 0) ||
        !mir_packed_constant(130, 14, TYPE_INT, 0) ||
        !mir_packed_constant(166, 2, TYPE_INT, 0) ||
        !mir_packed_constant(184, 2, TYPE_LONG, 1) ||
        !mir_packed_constant(195, 4, TYPE_LONG, 1) ||
        !mir_packed_constant(212, 4, TYPE_LONG, 1) ||
        !mir_packed_constant(248, 2, TYPE_INT, 0) ||
        !mir_packed_constant(266, 2, TYPE_LONG, 0) ||
        !mir_packed_constant(278, 4, TYPE_LONG, 0) ||
        !mir_packed_constant(296, 4, TYPE_LONG, 0) ||
        mir.insns[304].immediate != 1 ||
        !mir_packed_constant(312, 280, TYPE_INT, 0) ||
        !mir_packed_constant(315, 4, TYPE_INT, 0))
        return mir_machine_reject(
            "packed-record-runner", "constants");

    plan->guard_count = 10;
    plan->record_count = 20;
    plan->record_stride = 14;
    plan->guards[0] =
        mir_packed_global_array(1, plan->guard_count, plan->record_stride);
    plan->guards[1] =
        mir_packed_global_array(10, plan->guard_count, plan->record_stride);
    plan->records =
        mir_packed_global_array(39, plan->record_count, plan->record_stride);
    if (plan->guards[0] == NULL || plan->guards[1] == NULL ||
        plan->records == NULL ||
        plan->guards[0] == plan->guards[1] ||
        plan->guards[0] == plan->records ||
        plan->guards[1] == plan->records ||
        !mir_packed_same_global(96, plan->guards[0]) ||
        !mir_packed_same_global(105, plan->guards[1]) ||
        !mir_packed_same_global(135, plan->records) ||
        !mir_packed_same_global(309, plan->records))
        return mir_machine_reject(
            "packed-record-runner", "arrays");

    for (item = 0; item < 4; ++item) {
        int call_index = item == 0 ? 9 : item == 1 ? 18 :
                         item == 2 ? 104 : 113;
        int root_index = item == 0 ? 1 : item == 1 ? 10 :
                         item == 2 ? 96 : 105;
        int zero_index = item == 0 ? 4 : item == 1 ? 13 :
                         item == 2 ? 99 : 108;
        int size_index = item == 0 ? 6 : item == 1 ? 15 :
                         item == 2 ? 101 : 110;
        struct Sym *function;

        if (!mir_packed_direct_function(call_index, &function) ||
            !mir_call_is_memset_fastcall(
                call_index, &destination, &fill, &count) ||
            destination != mir.insns[root_index].dst ||
            fill != mir.insns[zero_index].dst ||
            count != mir.insns[size_index].dst ||
            (item != 0 && function != memset_function))
            return mir_machine_reject(
                "packed-record-runner", "memset-calls");
        if (item == 0)
            memset_function = function;
    }
    plan->memset_function = memset_function;
    first_zero = mir.insns[27].opcode == MIR_CONST ? 27 : 28;
    second_zero = mir.insns[122].opcode == MIR_CONST ? 122 : 123;

    if (!mir_machine_same_location(
            &mir.insns[29], &mir.insns[31]) ||
        !mir_machine_same_location(
            &mir.insns[29], &mir.insns[93]) ||
        !mir_machine_same_location(
            &mir.insns[124], &mir.insns[127]) ||
        !mir_machine_same_location(
            &mir.insns[124], &mir.insns[306]) ||
        mir_machine_same_location(
            &mir.insns[29], &mir.insns[124]) ||
        mir.insns[29].src1 != mir.insns[first_zero].dst ||
        mir.insns[93].src1 != mir.insns[92].dst ||
        mir.insns[124].src1 != mir.insns[second_zero].dst ||
        mir.insns[306].src1 != mir.insns[305].dst ||
        !mir_packed_phi(31, first_zero, 92, 0, 89) ||
        !mir_packed_phi(127, second_zero, 305, 95, 302) ||
        !mir_packed_binary(35, 33, 34, '/', TYPE_INT, 0) ||
        !mir_packed_binary(36, 31, 35, '<', TYPE_INT, 0) ||
        !mir_packed_branch(37, 36, 95) ||
        mir.insns[92].src1 != mir.insns[31].dst ||
        mir.insns[92].src2 != mir.insns[91].dst ||
        mir.insns[92].immediate != '+' ||
        !mir_packed_jump(94, 30) ||
        !mir_packed_binary(131, 129, 130, '/', TYPE_INT, 0) ||
        !mir_packed_binary(132, 127, 131, '<', TYPE_INT, 0) ||
        !mir_packed_branch(133, 132, 308) ||
        mir.insns[305].src1 != mir.insns[127].dst ||
        mir.insns[305].src2 != mir.insns[304].dst ||
        mir.insns[305].immediate != '+' ||
        !mir_packed_jump(307, 125))
        return mir_machine_reject(
            "packed-record-runner", "loops");

    if (mir.insns[41].src1 != mir.insns[39].dst ||
        mir.insns[41].src2 != mir.insns[31].dst ||
        mir.insns[41].immediate != plan->record_stride ||
        mir.insns[41].memory_size != plan->record_stride ||
        mir.insns[42].src1 != mir.insns[41].dst ||
        mir.insns[42].memory_size != 2 ||
        mir.insns[42].memory_flags != 0 ||
        mir.insns[137].src1 != mir.insns[135].dst ||
        mir.insns[137].src2 != mir.insns[127].dst ||
        mir.insns[137].immediate != plan->record_stride ||
        mir.insns[137].memory_size != plan->record_stride ||
        mir.insns[138].src1 != mir.insns[137].dst ||
        mir.insns[138].memory_size != 2 ||
        mir.insns[138].memory_flags != 0)
        return mir_machine_reject(
            "packed-record-runner", "record-indexing");
    for (item = 0; item < 6; ++item) {
        const struct MirInsn *member =
            &mir.insns[first_member_addresses[item]];

        plan->member_offsets[item] = (int)member->immediate;
        if (!mir_machine_same_location(
                &mir.insns[42],
                &mir.insns[first_member_bases[item]]) ||
            !mir_packed_member(
                first_member_addresses[item],
                first_member_bases[item],
                plan->member_offsets[item],
                member_kinds[item], member_unsigned[item],
                member_widths[item]) ||
            !mir_packed_store(
                first_stores[item],
                first_member_addresses[item],
                first_values[item], member_widths[item]))
            return mir_machine_reject(
                "packed-record-runner", "initializers");
        if (item > 0 &&
            plan->member_offsets[item] !=
                plan->member_offsets[item - 1] +
                member_widths[item - 1])
            return mir_machine_reject(
                "packed-record-runner", "packed-layout");
    }
    if (plan->member_offsets[0] != 0 ||
        plan->member_offsets[5] + member_widths[5] !=
            plan->record_stride ||
        !mir_packed_unary(46, 31, 0, TYPE_CHAR, 1) ||
        !mir_packed_binary(54, 31, 52, '*', TYPE_INT, 1) ||
        !mir_packed_unary(59, 31, 0, TYPE_LONG, 1) ||
        !mir_packed_binary(62, 59, 61, '*', TYPE_LONG, 1) ||
        !mir_packed_unary(67, 31, 0, TYPE_CHAR, 0) ||
        !mir_packed_unary(68, 67, '-', TYPE_INT, 0) ||
        !mir_packed_unary(69, 68, 0, TYPE_CHAR, 0) ||
        !mir_packed_unary(75, 31, '-', TYPE_INT, 0) ||
        !mir_packed_binary(77, 75, 76, '*', TYPE_INT, 0) ||
        !mir_packed_unary(82, 31, 0, TYPE_LONG, 0) ||
        !mir_packed_unary(83, 82, '-', TYPE_LONG, 0) ||
        !mir_packed_binary(86, 83, 85, '*', TYPE_LONG, 0))
        return mir_machine_reject(
            "packed-record-runner", "initializer-values");

    for (item = 0; item < 12; ++item) {
        int member = second_members[item];

        if (!mir_machine_same_location(
                &mir.insns[138],
                &mir.insns[second_member_bases[item]]) ||
            !mir_packed_member(
                second_member_addresses[item],
                second_member_bases[item],
                plan->member_offsets[member],
                member_kinds[member], member_unsigned[member],
                member_widths[member]) ||
            !mir_packed_load(
                second_loads[item],
                second_member_addresses[item],
                member_kinds[member], member_unsigned[member],
                member_widths[member]))
            return mir_machine_reject(
                "packed-record-runner", "check-loads");
    }
    if (!mir_packed_unary(143, 141, 0, TYPE_INT, 0) ||
        !mir_packed_binary(144, 143, 127, TOK_NE, TYPE_INT, 0) ||
        !mir_packed_branch(145, 144, 160) ||
        !mir_packed_binary(168, 127, 166, '*', TYPE_INT, 1) ||
        !mir_packed_binary(169, 163, 168, TOK_NE, TYPE_INT, 0) ||
        !mir_packed_branch(170, 169, 188) ||
        !mir_packed_unary(193, 127, 0, TYPE_LONG, 1) ||
        !mir_packed_binary(196, 193, 195, '*', TYPE_LONG, 1) ||
        !mir_packed_binary(197, 191, 196, TOK_NE, TYPE_INT, 0) ||
        !mir_packed_branch(198, 197, 216) ||
        !mir_packed_unary(221, 127, 0, TYPE_CHAR, 0) ||
        !mir_packed_unary(222, 221, '-', TYPE_INT, 0) ||
        !mir_packed_unary(223, 219, 0, TYPE_INT, 0) ||
        !mir_packed_binary(224, 223, 222, TOK_NE, TYPE_INT, 0) ||
        !mir_packed_branch(225, 224, 241) ||
        !mir_packed_unary(247, 127, '-', TYPE_INT, 0) ||
        !mir_packed_binary(249, 247, 248, '*', TYPE_INT, 0) ||
        !mir_packed_binary(250, 244, 249, TOK_NE, TYPE_INT, 0) ||
        !mir_packed_branch(251, 250, 270) ||
        !mir_packed_unary(275, 127, 0, TYPE_LONG, 0) ||
        !mir_packed_unary(276, 275, '-', TYPE_LONG, 0) ||
        !mir_packed_binary(279, 276, 278, '*', TYPE_LONG, 0) ||
        !mir_packed_binary(280, 273, 279, TOK_NE, TYPE_INT, 0) ||
        !mir_packed_branch(281, 280, 300))
        return mir_machine_reject(
            "packed-record-runner", "checks");

    for (item = 0; item < 6; ++item) {
        const struct MirInsn *call = &mir.insns[print_calls[item]];
        const char *call_name;
        struct Sym *function;

        if (!mir_packed_direct_function(
                print_calls[item], &function) ||
            call->memory_flags != MIR_CALL_FLAG_VARIADIC ||
            call->src1 >= 0 ||
            !mir_packed_call_arguments(call, 4, arguments) ||
            arguments[0] != mir.insns[print_strings[item]].dst ||
            arguments[1] != mir.insns[127].dst ||
            arguments[2] != mir.insns[print_actuals[item]].dst ||
            arguments[3] != mir.insns[print_expected[item]].dst ||
            mir.insns[print_strings[item]].immediate < 0 ||
            !mir_packed_scalar_type(
                mir.insns[print_strings[item]].type,
                TYPE_CHAR, 0, 1) ||
            !mir_packed_scalar_type(
                mir.insns[127].type,
                TYPE_INT, 1, 0) ||
            !mir_packed_scalar_type(
                mir.insns[print_actuals[item]].type,
                TYPE_LONG, print_actual_unsigned[item], 0) ||
            !mir_packed_scalar_type(
                mir.insns[print_expected[item]].type,
                TYPE_LONG, item < 3, 0))
            return mir_machine_reject(
                "packed-record-runner", "print-calls");
        call_name = mir_packed_call_name(call, function);
        if (strlen(call_name) >= sizeof(plan->print_call_name) ||
            (item != 0 &&
             (function != print_function ||
              strcmp(call_name, plan->print_call_name))))
            return mir_machine_reject(
                "packed-record-runner", "print-identity");
        if (item == 0) {
            print_function = function;
            strcpy(plan->print_call_name, call_name);
        }
        plan->strings[item] =
            (int)mir.insns[print_strings[item]].immediate;
    }
    if (!print_function->has_proto ||
        !print_function->proto_variadic ||
        print_function->proto_nargs != 1 ||
        !mir_packed_scalar_type(
            print_function->type, TYPE_INT, 0, 0) ||
        !mir_packed_scalar_type(
            print_function->proto_types[0], TYPE_CHAR, 0, 1))
        return mir_machine_reject(
            "packed-record-runner", "print-prototype");
    plan->print_function = print_function;

    if (!mir_packed_direct_function(318, &dump_function) ||
        dump_function == memset_function ||
        dump_function == print_function ||
        mir.insns[318].memory_flags != 0 ||
        !mir_packed_call_arguments(
            &mir.insns[318], 3, arguments) ||
        arguments[0] != mir.insns[309].dst ||
        arguments[1] != mir.insns[312].dst ||
        arguments[2] != mir.insns[315].dst ||
        !dump_function->has_proto ||
        dump_function->proto_variadic ||
        dump_function->proto_nargs != 3 ||
        (dump_function->type & 15) != TYPE_VOID ||
        type_ptr_depth(dump_function->type) != 0 ||
        !mir_packed_scalar_type(
            dump_function->proto_types[0], TYPE_CHAR, 1, 1) ||
        !mir_packed_scalar_type(
            dump_function->proto_types[1], TYPE_INT, 1, 0) ||
        !mir_packed_scalar_type(
            dump_function->proto_types[2], TYPE_INT, 1, 0))
        return mir_machine_reject(
            "packed-record-runner", "dump-call");
    plan->dump_function = dump_function;
    return 1;
}

static int mir_match_multidim_aggregate_checks(
    struct MirAggregateMultidimChecks *plan)
{
    static const int call_indices[14] = {
        3, 7, 13, 22, 81, 89, 100, 117, 118, 121, 125, 136, 141, 143
    };
    static const int string_indices[8] = {
        1, 16, 75, 92, 101, 119, 126, 137
    };
    int opcode_counts[MIR_RETURN + 1];
    struct Sym *functions[14];
    struct Sym *board_roots[4];
    struct Sym *cells_root;
    long root_offsets[4];
    long cells_offset;
    int instruction;
    int index;
    long constant;
    int arguments[3];

    memset(plan, 0, sizeof(*plan));
    memset(opcode_counts, 0, sizeof(opcode_counts));
    if (mir.count != 144 || mir_cfg_block_count() != 7 ||
        mir.local_bytes != 13 || mir.has_vla ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode < 0 || insn->opcode > MIR_RETURN)
            return 0;
        ++opcode_counts[insn->opcode];
    }
    if (opcode_counts[MIR_LABEL] != 7 ||
        opcode_counts[MIR_STRING_ADDRESS] != 8 ||
        opcode_counts[MIR_ARG] != 28 ||
        opcode_counts[MIR_CALL] != 14 ||
        opcode_counts[MIR_ADDRESS] != 5 ||
        opcode_counts[MIR_NOP] != 23 ||
        opcode_counts[MIR_MEMBER_ADDRESS] != 6 ||
        opcode_counts[MIR_CONST] != 17 ||
        opcode_counts[MIR_STORE] != 8 ||
        opcode_counts[MIR_LOAD] != 10 ||
        opcode_counts[MIR_BINARY] != 5 ||
        opcode_counts[MIR_BRANCH_FALSE] != 2 ||
        opcode_counts[MIR_INDEX_ADDRESS] != 3 ||
        opcode_counts[MIR_LOAD_INDIRECT] != 4 ||
        opcode_counts[MIR_UNARY] != 2 ||
        opcode_counts[MIR_JUMP] != 2)
        return 0;
    for (index = 0; index < 14; ++index)
        if (!mir_aggregate_direct_function(
                call_indices[index], &functions[index]))
            return 0;
    if (functions[3] != functions[4] ||
        functions[3] != functions[10] ||
        functions[6] != functions[7] ||
        functions[6] != functions[11] ||
        functions[9] != functions[12])
        return 0;
    for (index = 0; index < 8; ++index) {
        const struct MirInsn *string =
            &mir.insns[string_indices[index]];

        if (string->opcode != MIR_STRING_ADDRESS)
            return 0;
        if (index == 0)
            plan->heading_string_id = (int)string->immediate;
        else if (index == 7)
            plan->summary_string_id = (int)string->immediate;
        else
            plan->multidim.strings[index - 1] =
                (int)string->immediate;
    }
    for (index = 0; index < 4; ++index) {
        int address_index =
            index == 0 ? 4 : index == 1 ? 8 :
            index == 2 ? 48 : 82;

        if (!mir_machine_global_address_offset(
                mir.insns[address_index].dst,
                &board_roots[index], &root_offsets[index], 0) ||
            board_roots[index]->is_volatile)
            return 0;
    }
    if (board_roots[0] != board_roots[1] ||
        board_roots[0] != board_roots[2] ||
        board_roots[0] != board_roots[3] ||
        root_offsets[0] != root_offsets[1] ||
        root_offsets[0] != root_offsets[2] ||
        root_offsets[0] != root_offsets[3] ||
        root_offsets[0] != 0 ||
        !mir_machine_global_address_offset(
            mir.insns[128].dst, &cells_root, &cells_offset, 0) ||
        cells_offset != 0 ||
        cells_root == board_roots[0] || cells_root->is_volatile)
        return 0;
    if (!mir_aggregate_local_location(
            &mir.insns[15], 4, &plan->multidim.w_ptr_offset) ||
        !mir_aggregate_local_location(
            &mir.insns[25], 4, &plan->multidim.w_struct_offset) ||
        !mir_aggregate_local_location(
            &mir.insns[59], 4, &index) ||
        index != plan->multidim.w_struct_offset ||
        !mir_aggregate_local_location(
            &mir.insns[29], 1, &plan->multidim.row_offset) ||
        !mir_aggregate_local_location(
            &mir.insns[71], 1, &index) ||
        index != plan->multidim.row_offset ||
        !mir_aggregate_local_location(
            &mir.insns[39], 2, &plan->multidim.column_offset) ||
        !mir_aggregate_local_location(
            &mir.insns[64], 2, &index) ||
        index != plan->multidim.column_offset ||
        !mir_aggregate_local_location(
            &mir.insns[91], 2, &plan->multidim.tile_offset))
        return 0;
    if (mir.insns[51].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[53].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[54].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[130].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[131].opcode != MIR_MEMBER_ADDRESS ||
        !mir_machine_constant_value(
            mir.insns[11].dst, &constant, 0) ||
        constant <= 0 || constant > 127)
        return 0;
    plan->multidim.rows = (int)constant;
    if (!mir_machine_constant_value(
            mir.insns[44].dst, &constant, 0) ||
        constant <= 0 || constant > 127)
        return 0;
    plan->multidim.columns = (int)constant;
    plan->multidim.board_row_stride =
        (int)mir.insns[51].immediate;
    plan->multidim.board_column_stride =
        (int)mir.insns[53].immediate;
    plan->multidim.board_weight_offset =
        (int)mir.insns[54].immediate;
    plan->multidim.cells_stride =
        (int)mir.insns[130].immediate;
    plan->multidim.cells_tag_offset =
        (int)mir.insns[131].immediate;
    if (plan->multidim.board_row_stride <= 0 ||
        plan->multidim.board_column_stride <= 0 ||
        plan->multidim.board_row_stride !=
            plan->multidim.board_column_stride *
            plan->multidim.columns ||
        plan->multidim.cells_stride <= 0)
        return 0;
    if (!mir_machine_three_call_arguments(
            &mir.insns[22], arguments) ||
        !mir_machine_constant_value(arguments[2], &constant, 0))
        return 0;
    plan->multidim.weight_expected = (unsigned long)constant;
    if (!mir_machine_three_call_arguments(
            &mir.insns[100], arguments) ||
        !mir_machine_constant_value(arguments[2], &constant, 0))
        return 0;
    plan->multidim.tile_weight_expected = (unsigned long)constant;
    if (!mir_machine_three_call_arguments(
            &mir.insns[117], arguments) ||
        !mir_machine_constant_value(arguments[2], &constant, 0))
        return 0;
    plan->multidim.tile_char_expected = (unsigned long)constant;
    if (!mir_machine_three_call_arguments(
            &mir.insns[125], arguments) ||
        !mir_machine_constant_value(arguments[2], &constant, 0))
        return 0;
    plan->multidim.checksum_expected = (unsigned long)constant;
    if (!mir_machine_three_call_arguments(
            &mir.insns[136], arguments) ||
        !mir_machine_constant_value(arguments[2], &constant, 0) ||
        !mir_machine_constant_value(
            mir.insns[129].dst, &cells_offset, 0))
        return 0;
    plan->multidim.tag_expected = (unsigned long)constant;
    plan->multidim.cells_index = (int)cells_offset;
    if (!mir_machine_single_call_argument(
            &mir.insns[3], &index) ||
        index != mir.insns[1].dst ||
        !mir_machine_single_call_argument(
            &mir.insns[7], &index) ||
        index != mir.insns[4].dst ||
        !mir_machine_two_call_arguments(
            &mir.insns[13], arguments) ||
        arguments[0] != mir.insns[9].dst ||
        arguments[1] != mir.insns[11].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[22], arguments) ||
        arguments[0] != mir.insns[16].dst ||
        arguments[1] != mir.insns[13].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[81], arguments) ||
        arguments[0] != mir.insns[75].dst ||
        arguments[1] != mir.insns[77].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[89], arguments) ||
        arguments[0] != mir.insns[82].dst ||
        arguments[1] != mir.insns[85].dst ||
        arguments[2] != mir.insns[87].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[100], arguments) ||
        arguments[0] != mir.insns[92].dst ||
        arguments[1] != mir.insns[96].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[117], arguments) ||
        arguments[0] != mir.insns[101].dst ||
        arguments[1] != mir.insns[106].dst ||
        !mir_machine_call_has_no_arguments(&mir.insns[118]) ||
        !mir_machine_call_has_no_arguments(&mir.insns[121]) ||
        !mir_machine_three_call_arguments(
            &mir.insns[125], arguments) ||
        arguments[0] != mir.insns[119].dst ||
        arguments[1] != mir.insns[121].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[136], arguments) ||
        arguments[0] != mir.insns[126].dst ||
        arguments[1] != mir.insns[132].dst ||
        !mir_machine_call_has_no_arguments(&mir.insns[141]) ||
        !mir_machine_three_call_arguments(
            &mir.insns[143], arguments) ||
        arguments[0] != mir.insns[137].dst ||
        arguments[1] != mir.insns[13].dst ||
        arguments[2] != mir.insns[141].dst ||
        mir.insns[15].src1 != mir.insns[13].dst ||
        !mir_machine_constant_equals(mir.insns[25].src1, 0) ||
        !mir_machine_constant_equals(mir.insns[29].src1, 0) ||
        !mir_machine_constant_equals(mir.insns[39].src1, 0) ||
        mir.insns[59].src1 != mir.insns[57].dst ||
        mir.insns[64].src1 != mir.insns[63].dst ||
        mir.insns[71].src1 != mir.insns[70].dst ||
        mir.insns[91].src1 != mir.insns[89].dst)
        return 0;
    plan->kind = 1;
    plan->print_function = functions[0];
    if (functions[13] != plan->print_function)
        return 0;
    plan->check_wide_function = functions[3];
    plan->check_word_function = functions[6];
    plan->multidim.board_root = board_roots[0];
    plan->multidim.cells_root = cells_root;
    plan->multidim.board_fill_function = functions[1];
    plan->multidim.board_weight_function = functions[2];
    plan->multidim.tile_function = functions[5];
    plan->multidim.cells_fill_function = functions[8];
    plan->multidim.cells_checksum_function = functions[9];
    snprintf(plan->heading_call_name,
             sizeof(plan->heading_call_name), "%s",
             mir.insns[3].base_name[0] != 0
                 ? mir.insns[3].base_name
                 : asm_name_for(sym_asm_name(functions[0])));
    snprintf(plan->summary_call_name,
             sizeof(plan->summary_call_name), "%s",
             mir.insns[143].base_name[0] != 0
                 ? mir.insns[143].base_name
                 : asm_name_for(sym_asm_name(functions[13])));
    return plan->check_word_function != plan->check_wide_function &&
        plan->multidim.board_fill_function !=
            plan->multidim.board_weight_function &&
        plan->multidim.cells_fill_function !=
            plan->multidim.cells_checksum_function;
}

static int mir_match_size2_aggregate_checks(
    struct MirAggregateMultidimChecks *plan)
{
    static const int check_indices[17] = {
        224, 235, 245, 256, 266, 277, 289, 299, 310,
        320, 331, 341, 352, 363, 373, 384, 395
    };
    int opcode_counts[MIR_RETURN + 1];
    struct Sym *function;
    const struct MirInsn *string;
    int instruction;
    int store;
    int check;
    int offset;
    int type;
    int storage;
    long constant;
    long expected;
    int arguments[3];

    memset(plan, 0, sizeof(*plan));
    memset(opcode_counts, 0, sizeof(opcode_counts));
    if (mir.count != 401 || mir_cfg_block_count() != 7 ||
        mir.local_bytes != 86 || mir.has_vla ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode < 0 || insn->opcode > MIR_RETURN)
            return 0;
        ++opcode_counts[insn->opcode];
    }
    if (opcode_counts[MIR_LABEL] != 7 ||
        opcode_counts[MIR_CONST] != 101 ||
        opcode_counts[MIR_STORE] != 50 ||
        opcode_counts[MIR_NOP] != 37 ||
        opcode_counts[MIR_STRING_ADDRESS] != 24 ||
        opcode_counts[MIR_ARG] != 69 ||
        opcode_counts[MIR_CALL] != 24 ||
        opcode_counts[MIR_BINARY] != 18 ||
        opcode_counts[MIR_PHI] != 1 ||
        opcode_counts[MIR_BRANCH_FALSE] != 2 ||
        opcode_counts[MIR_LOAD] != 9 ||
        opcode_counts[MIR_ADDRESS] != 13 ||
        opcode_counts[MIR_INDEX_ADDRESS] != 16 ||
        opcode_counts[MIR_MEMBER_ADDRESS] != 14 ||
        opcode_counts[MIR_LOAD_INDIRECT] != 13 ||
        opcode_counts[MIR_UNARY] != 1 ||
        opcode_counts[MIR_JUMP] != 2)
        return 0;
    for (store = 0; store < 43; ++store) {
        const struct MirInsn *value = &mir.insns[store * 2 + 1];
        const struct MirInsn *write = &mir.insns[store * 2 + 2];

        if (value->opcode != MIR_CONST ||
            write->opcode != MIR_STORE ||
            write->src1 != value->dst ||
            write->memory_flags != 0 ||
            !mir_scalar_memory_location(
                write, &type, &storage, &offset) ||
            storage != SC_LOCAL ||
            (write->memory_size != 1 &&
             write->memory_size != 2 &&
             write->memory_size != 4))
            return 0;
        plan->size2.stores[store].offset = offset;
        plan->size2.stores[store].width = write->memory_size;
        plan->size2.stores[store].value =
            (unsigned long)value->immediate;
    }
    if (!mir_aggregate_direct_function(
            92, &plan->print_function) ||
        !mir_aggregate_direct_function(400, &function) ||
        function != plan->print_function ||
        !mir_aggregate_direct_function(
            102, &plan->check_word_function) ||
        !mir_aggregate_direct_function(112, &function) ||
        function != plan->check_word_function ||
        !mir_aggregate_direct_function(164, &function) ||
        function != plan->check_word_function ||
        !mir_aggregate_direct_function(
            180, &plan->check_wide_function) ||
        !mir_aggregate_direct_function(214, &function) ||
        function != plan->check_wide_function ||
        plan->check_word_function == plan->check_wide_function)
        return 0;
    string = mir_definition(mir.insns[91].src1);
    if (string == NULL || string->opcode != MIR_STRING_ADDRESS)
        return 0;
    plan->heading_string_id = (int)string->immediate;
    if (!mir_machine_three_call_arguments(
            &mir.insns[102], arguments) ||
        !mir_machine_constant_value(arguments[1], &constant, 0) ||
    !mir_machine_constant_value(arguments[2], &expected, 0) ||
    constant != expected || constant <= 0 || constant > 127)
        return 0;
    plan->size2.row_count = (int)constant;
    string = mir_definition(arguments[0]);
    if (string == NULL || string->opcode != MIR_STRING_ADDRESS)
        return 0;
    plan->size2.strings[0] = (int)string->immediate;
    if (!mir_machine_three_call_arguments(
            &mir.insns[112], arguments) ||
        !mir_machine_constant_value(arguments[1], &constant, 0) ||
    !mir_machine_constant_value(arguments[2], &expected, 0) ||
    constant != expected || constant <= 0 || constant > 127)
        return 0;
    plan->size2.column_count = (int)constant /
        plan->size2.row_count;
    if (plan->size2.column_count <= 0 ||
        plan->size2.row_count *
            plan->size2.column_count != constant)
        return 0;
    string = mir_definition(arguments[0]);
    if (string == NULL || string->opcode != MIR_STRING_ADDRESS)
        return 0;
    plan->size2.strings[1] = (int)string->immediate;
    if (!mir_aggregate_local_location(
            &mir.insns[89], 4, &plan->size2.sum_offset) ||
        !mir_aggregate_local_location(
            &mir.insns[121], 2, &plan->size2.row_offset) ||
        !mir_aggregate_local_location(
            &mir.insns[133], 2, &plan->size2.column_offset) ||
        !mir_aggregate_local_location(
            &mir.insns[149], 2, &plan->size2.index_offset) ||
        !mir_scalar_memory_location(
            &mir.insns[152], &type, &storage,
            &plan->size2.lg_offset) ||
        storage != SC_LOCAL ||
        !mir_scalar_memory_location(
            &mir.insns[302], &type, &storage,
            &plan->size2.lu_offset) ||
        storage != SC_LOCAL ||
        !mir_scalar_memory_location(
            &mir.insns[323], &type, &storage,
            &plan->size2.lb_offset) ||
        storage != SC_LOCAL ||
        !mir_scalar_memory_location(
            &mir.insns[376], &type, &storage,
            &plan->size2.lop_offset) ||
        storage != SC_LOCAL)
        return 0;
    if (!mir_machine_single_call_argument(
            &mir.insns[92], &offset) ||
        offset != mir.insns[90].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[164], arguments) ||
        arguments[0] != mir.insns[150].dst ||
        arguments[1] != mir.insns[158].dst ||
        arguments[2] != mir.insns[162].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[180], arguments) ||
        arguments[0] != mir.insns[165].dst ||
        arguments[1] != mir.insns[173].dst ||
        arguments[2] != mir.insns[178].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[214], arguments) ||
        arguments[0] != mir.insns[208].dst ||
        arguments[1] != mir.insns[210].dst ||
        !mir_machine_two_call_arguments(
            &mir.insns[400], arguments) ||
        arguments[0] != mir.insns[396].dst ||
        arguments[1] != mir.insns[398].dst ||
        mir.insns[89].src1 != mir.insns[88].dst ||
        mir.insns[121].src1 != mir.insns[119].dst ||
        mir.insns[133].src1 != mir.insns[131].dst ||
        mir.insns[149].src1 != mir.insns[148].dst ||
        mir.insns[191].src1 != mir.insns[189].dst ||
        mir.insns[197].src1 != mir.insns[196].dst ||
        mir.insns[205].src1 != mir.insns[204].dst)
        return 0;
    if (mir.insns[154].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[156].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[157].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[172].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[154].immediate <= 0 ||
        mir.insns[156].immediate <= 0 ||
        mir.insns[154].immediate !=
            mir.insns[169].immediate ||
        mir.insns[156].immediate !=
            mir.insns[171].immediate ||
        mir.insns[157].immediate < 0 ||
        mir.insns[172].immediate < 0)
        return 0;
    plan->size2.row_stride = (int)mir.insns[154].immediate;
    plan->size2.column_stride = (int)mir.insns[156].immediate;
    plan->size2.first_member_offset =
        (int)mir.insns[157].immediate;
    plan->size2.second_member_offset =
        (int)mir.insns[172].immediate;
    if (plan->size2.row_stride !=
            plan->size2.column_stride *
            plan->size2.column_count ||
        !mir_machine_constant_value(
            mir.insns[161].dst, &constant, 0))
        return 0;
    plan->size2.first_addend = (unsigned long)constant;
    if (!mir_machine_constant_value(
            mir.insns[176].dst, &constant, 0))
        return 0;
    plan->size2.second_addend = (unsigned long)constant;
    if (!mir_machine_three_call_arguments(
            &mir.insns[164], arguments) ||
        (string = mir_definition(arguments[0])) == NULL ||
        string->opcode != MIR_STRING_ADDRESS)
        return 0;
    plan->size2.strings[2] = (int)string->immediate;
    if (!mir_machine_three_call_arguments(
            &mir.insns[180], arguments) ||
        (string = mir_definition(arguments[0])) == NULL ||
        string->opcode != MIR_STRING_ADDRESS)
        return 0;
    plan->size2.strings[3] = (int)string->immediate;
    if (!mir_machine_three_call_arguments(
            &mir.insns[214], arguments) ||
        !mir_machine_constant_value(arguments[2], &constant, 0))
        return 0;
    string = mir_definition(arguments[0]);
    if (string == NULL || string->opcode != MIR_STRING_ADDRESS)
        return 0;
    plan->size2.strings[4] = (int)string->immediate;
    plan->size2.sum_expected = (unsigned long)constant;
    for (check = 0; check < 17; ++check) {
        if (!mir_aggregate_match_check(
                check_indices[check],
                &plan->size2.checks[check],
                &plan->check_word_function,
                &plan->check_wide_function))
            return 0;
    }
    string = mir_definition(mir.insns[397].src1);
    if (string == NULL || string->opcode != MIR_STRING_ADDRESS ||
        mir.insns[398].opcode != MIR_LOAD ||
        (mir.insns[398].memory_flags & (1 | 8)) != 0 ||
        type_size(mir.insns[398].type) != 4 ||
        !mir_scalar_memory_location(
            &mir.insns[398], &type, &storage, &offset) ||
        storage != SC_LOCAL ||
        offset != plan->size2.sum_offset)
        return 0;
    plan->summary_string_id = (int)string->immediate;
    snprintf(plan->heading_call_name,
             sizeof(plan->heading_call_name), "%s",
             mir.insns[92].base_name[0] != 0
                 ? mir.insns[92].base_name
                 : asm_name_for(sym_asm_name(plan->print_function)));
    snprintf(plan->summary_call_name,
             sizeof(plan->summary_call_name), "%s",
             mir.insns[400].base_name[0] != 0
                 ? mir.insns[400].base_name
                 : asm_name_for(sym_asm_name(plan->print_function)));
    plan->size2.check_count = 17;
    plan->kind = 2;
    return 1;
}

static int mir_match_aggregate_multidim_checks(
    struct MirAggregateMultidimChecks *plan)
{
    if (mir_match_multidim_aggregate_checks(plan) ||
        mir_match_size2_aggregate_checks(plan))
        return 1;
    return 0;
}

static void mir_aggregate_cleanup(MirStream *out, int words)
{
    while (words-- > 0)
        mir_stream_puts("\tpop bc\n", out);
}

static void mir_aggregate_emit_format_call(
    MirStream *out, struct Sym *function, const char *call_name)
{
    if (!strcmp(call_name,
                asm_name_for(sym_asm_name(function))))
        mir_machine_emit_symbol_call(out, function);
    else
        mir_stream_printf(out, "\tcall %s\n", call_name);
}

static void mir_array_main_emit_counter(
    MirStream *out, int offset, int value)
{
    mir_stream_printf(out,
            "\tld (ix%+d),%d\n"
            "\tld (ix%+d),0\n",
            offset, value & 0xff, offset + 1);
}

static void mir_array_main_emit_counter_load(
    MirStream *out, int offset)
{
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n"
            "\tld h,(ix%+d)\n",
            offset, offset + 1);
}

static void mir_array_main_emit_counter_increment(
    MirStream *out, int offset, int carry_label)
{
    mir_stream_printf(out,
            "\tinc (ix%+d)\n"
            "\tjp nz,L%d\n"
            "\tinc (ix%+d)\n"
            "L%d:\n",
            offset, carry_label, offset + 1, carry_label);
}

static void mir_array_main_emit_index_address(
    MirStream *out, struct Sym *symbol, int counter_offset, int stride)
{
    mir_array_main_emit_counter_load(out, counter_offset);
    if (stride >= 2)
        mir_stream_puts("\tadd hl,hl\n", out);
    if (stride >= 4)
        mir_stream_puts("\tadd hl,hl\n", out);
    mir_stream_puts("\tex de,hl\n", out);
    mir_machine_emit_global_address_hl(out, symbol, 0);
    mir_stream_puts("\tadd hl,de\n", out);
}

static void mir_array_main_emit_byte_load(
    MirStream *out, struct Sym *symbol, int counter_offset,
    int fixed_offset, int is_unsigned)
{
    if (counter_offset != 0)
        mir_array_main_emit_index_address(
            out, symbol, counter_offset, 1);
    else
        mir_machine_emit_global_address_hl(
            out, symbol, fixed_offset);
    mir_stream_puts("\tld l,(hl)\n", out);
    if (is_unsigned)
        mir_stream_puts("\tld h,0\n", out);
    else
        mir_stream_puts("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n", out);
}

static void mir_array_main_emit_word_load(
    MirStream *out, struct Sym *symbol, int counter_offset)
{
    mir_array_main_emit_index_address(
        out, symbol, counter_offset, 2);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
          out);
}

static void mir_array_main_emit_wide_load(
    MirStream *out, struct Sym *symbol, int counter_offset)
{
    mir_array_main_emit_index_address(
        out, symbol, counter_offset, 4);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tinc hl\n\tld a,(hl)\n\tinc hl\n"
          "\tld h,(hl)\n\tld l,a\n\tex de,hl\n", out);
}

static void mir_array_main_emit_print_values(
    MirStream *out, const struct MirArrayMainPlan *plan,
    int counter_offset, int is_unsigned, int print)
{
    int base = is_unsigned ? MIR_ARRAY_MAIN_U8 : MIR_ARRAY_MAIN_I8;

    mir_array_main_emit_wide_load(
        out, plan->symbols[base + 2], counter_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_array_main_emit_word_load(
        out, plan->symbols[base + 1], counter_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_array_main_emit_byte_load(
        out, plan->symbols[base], counter_offset, 0, is_unsigned);
    mir_stream_puts("\tpush hl\n", out);
    mir_array_main_emit_counter_load(out, counter_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[
                is_unsigned
                    ? MIR_ARRAY_MAIN_UNSIGNED_FORMAT
                    : MIR_ARRAY_MAIN_SIGNED_FORMAT]);
    mir_emit_runtime_call(out, plan->print_names[print]);
    mir_aggregate_cleanup(out, 6);
}

static void mir_array_main_emit_store_value(
    MirStream *out, struct Sym *symbol, int counter_offset,
    int stride, int bias, int subtract, int width)
{
    mir_array_main_emit_counter_load(out, counter_offset);
    mir_stream_printf(out, "\tld de,%d\n", subtract ? -bias : bias);
    mir_stream_puts("\tadd hl,de\n\tpush hl\n", out);
    mir_array_main_emit_index_address(
        out, symbol, counter_offset, stride);
    mir_stream_puts("\tpop de\n\tld (hl),e\n", out);
    if (width >= 2)
        mir_stream_puts("\tinc hl\n\tld (hl),d\n", out);
    if (width == 4) {
        mir_stream_puts("\tld a,d\n\trlca\n\tsbc a,a\n\tinc hl\n"
              "\tld (hl),a\n\tinc hl\n\tld (hl),a\n", out);
    }
}

static void mir_array_main_emit_size_check(
    MirStream *out, const struct MirArrayMainPlan *plan,
    int failed, int accepted)
{
    int check;

    for (check = 0; check < 5; ++check) {
        mir_stream_printf(out,
                "\tld hl,%d\n\tld de,%d\n"
                "\tor a\n\tsbc hl,de\n\tjp nz,L%d\n",
                plan->count, plan->count, failed);
    }
    mir_stream_printf(out, "\tjp L%d\nL%d:\n\tld hl,S%d\n\tpush hl\n",
            accepted, failed,
            plan->strings[MIR_ARRAY_MAIN_SIZE_FAILURE]);
    mir_emit_runtime_call(out, plan->print_names[0]);
    mir_aggregate_cleanup(out, 1);
    mir_stream_puts("\tld hl,1\n\tpush hl\n", out);
    mir_emit_runtime_call(out, plan->exit_name);
    mir_aggregate_cleanup(out, 1);
    mir_stream_printf(out, "L%d:\n", accepted);
}

static void mir_array_main_emit_character_report(
    MirStream *out, const struct MirArrayMainPlan *plan)
{
    int character;

    for (character = plan->count - 1; character >= 0; --character) {
        mir_array_main_emit_byte_load(
            out, plan->symbols[MIR_ARRAY_MAIN_CHARS],
            0, character, 0);
        mir_stream_puts("\tpush hl\n", out);
    }
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[MIR_ARRAY_MAIN_CHARS_FORMAT]);
    mir_emit_runtime_call(out, plan->print_names[5]);
    mir_aggregate_cleanup(out, plan->count + 1);
}

static void mir_array_main_emit_board(
    MirStream *out, const struct MirArrayMainPlan *plan)
{
    int row_loop = new_label();
    int column_loop = new_label();
    int next_row = new_label();
    int board_done = new_label();
    int column_carry = new_label();
    int row_carry = new_label();

    mir_array_main_emit_counter(out, -2, 0);
    mir_stream_printf(out, "L%d:\n", row_loop);
    mir_array_main_emit_counter_load(out, -2);
    mir_stream_printf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                 "\tjp nc,L%d\n",
            plan->count, board_done);
    mir_array_main_emit_counter(out, -4, 0);
    mir_stream_printf(out, "L%d:\n", column_loop);
    mir_array_main_emit_counter_load(out, -4);
    mir_stream_printf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                 "\tjp nc,L%d\n",
            plan->board_columns, next_row);
    mir_array_main_emit_counter_load(out, -2);
    mir_stream_puts("\tadd hl,hl\n\tadd hl,hl\n\tadd hl,hl\n\tpush hl\n",
          out);
    mir_array_main_emit_counter_load(out, -4);
    mir_stream_puts("\tex de,hl\n\tpop hl\n\tadd hl,de\n\tex de,hl\n",
          out);
    mir_machine_emit_global_address_hl(
        out, plan->symbols[MIR_ARRAY_MAIN_BOARD], 0);
    mir_stream_puts("\tadd hl,de\n\tld l,(hl)\n"
          "\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n"
          "\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[MIR_ARRAY_MAIN_BOARD_FORMAT]);
    mir_emit_runtime_call(out, plan->print_names[6]);
    mir_aggregate_cleanup(out, 2);
    mir_array_main_emit_counter_increment(
        out, -4, column_carry);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n\tld hl,S%d\n\tpush hl\n",
            column_loop, next_row,
            plan->strings[MIR_ARRAY_MAIN_NEWLINE]);
    mir_emit_runtime_call(out, plan->print_names[7]);
    mir_aggregate_cleanup(out, 1);
    mir_array_main_emit_counter_increment(out, -2, row_carry);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", row_loop, board_done);
}

static void mir_array_main_emit_words(
    MirStream *out, const struct MirArrayMainPlan *plan)
{
    int loop = new_label();
    int done = new_label();
    int carry = new_label();

    mir_array_main_emit_counter(out, -2, 0);
    mir_stream_printf(out, "L%d:\n", loop);
    mir_array_main_emit_counter_load(out, -2);
    mir_stream_printf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                 "\tjp nc,L%d\n",
            plan->count, done);
    mir_array_main_emit_index_address(
        out, plan->symbols[MIR_ARRAY_MAIN_WORDS], -2, 2);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tpush de\n", out);
    mir_array_main_emit_counter_load(out, -2);
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[MIR_ARRAY_MAIN_WORD_FORMAT]);
    mir_emit_runtime_call(out, plan->print_names[8]);
    mir_aggregate_cleanup(out, 3);
    mir_array_main_emit_counter_increment(out, -2, carry);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", loop, done);
}

static void mir_emit_array_main(
    MirStream *out, const struct MirArrayMainPlan *plan)
{
    int size_failed = new_label();
    int size_ok = new_label();
    int first_loop = new_label();
    int first_done = new_label();
    int first_carry = new_label();
    int write_loop = new_label();
    int write_done = new_label();
    int write_carry = new_label();
    int second_loop = new_label();
    int second_done = new_label();
    int second_carry = new_label();

    mir_stream_printf(out,
            "%s\n"
            "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n",
            MIR_EXACT_KERNEL_MARKER, MIR_ARRAY_MAIN_FRAME_BYTES);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_array_main_emit_size_check(
        out, plan, size_failed, size_ok);

    mir_array_main_emit_counter(out, -2, 0);
    mir_stream_printf(out, "L%d:\n", first_loop);
    mir_array_main_emit_counter_load(out, -2);
    mir_stream_printf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                 "\tjp nc,L%d\n",
            plan->count, first_done);
    mir_array_main_emit_print_values(out, plan, -2, 1, 1);
    mir_array_main_emit_print_values(out, plan, -2, 0, 2);
    mir_array_main_emit_counter_increment(
        out, -2, first_carry);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", first_loop, first_done);

    mir_array_main_emit_counter(out, -2, 0);
    mir_stream_printf(out, "L%d:\n", write_loop);
    mir_array_main_emit_counter_load(out, -2);
    mir_stream_printf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                 "\tjp nc,L%d\n",
            plan->count, write_done);
    mir_array_main_emit_store_value(
        out, plan->symbols[MIR_ARRAY_MAIN_U32], -2, 4,
        plan->replacement_bias, 0, 4);
    mir_array_main_emit_store_value(
        out, plan->symbols[MIR_ARRAY_MAIN_U16], -2, 2,
        plan->replacement_bias, 0, 2);
    mir_array_main_emit_store_value(
        out, plan->symbols[MIR_ARRAY_MAIN_U8], -2, 1,
        plan->replacement_bias, 0, 1);
    mir_array_main_emit_store_value(
        out, plan->symbols[MIR_ARRAY_MAIN_I32], -2, 4,
        plan->replacement_bias, 1, 4);
    mir_array_main_emit_store_value(
        out, plan->symbols[MIR_ARRAY_MAIN_I16], -2, 2,
        plan->replacement_bias, 1, 2);
    mir_array_main_emit_store_value(
        out, plan->symbols[MIR_ARRAY_MAIN_I8], -2, 1,
        plan->replacement_bias, 1, 1);
    mir_array_main_emit_counter_increment(
        out, -2, write_carry);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", write_loop, write_done);

    mir_array_main_emit_counter(out, -2, 0);
    mir_stream_printf(out, "L%d:\n", second_loop);
    mir_array_main_emit_counter_load(out, -2);
    mir_stream_printf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                 "\tjp nc,L%d\n",
            plan->count, second_done);
    mir_array_main_emit_print_values(out, plan, -2, 1, 3);
    mir_array_main_emit_print_values(out, plan, -2, 0, 4);
    mir_array_main_emit_counter_increment(
        out, -2, second_carry);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", second_loop, second_done);

    mir_array_main_emit_character_report(out, plan);
    mir_array_main_emit_board(out, plan);
    mir_array_main_emit_words(out, plan);
    mir_machine_emit_symbol_call(out, plan->many_function);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[MIR_ARRAY_MAIN_SUCCESS]);
    mir_emit_runtime_call(out, plan->print_names[9]);
    mir_aggregate_cleanup(out, 1);
    mir_stream_puts("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static void mir_aggregate_emit_ix_store(
    MirStream *out, int offset, int width, unsigned long value)
{
    if (width == 1) {
        mir_stream_printf(out, "\tld (ix%+d),%lu\n",
                offset, value & 0xffUL);
    } else if (width == 2) {
        mir_stream_printf(out,
                "\tld (ix%+d),%lu\n"
                "\tld (ix%+d),%lu\n",
                offset, value & 0xffUL,
                offset + 1, (value >> 8) & 0xffUL);
    } else {
        mir_machine_emit_float_bits(out, value);
        mir_machine_emit_ix_wide_store(out, offset);
    }
}

static void mir_aggregate_emit_value(
    MirStream *out, const struct MirAggregateCheckValue *value)
{
    if (value->kind == MIR_AGGREGATE_CHECK_CONSTANT) {
        if (value->width == 4)
            mir_machine_emit_float_bits(out, value->value);
        else
            mir_stream_printf(out, "\tld hl,%lu\n",
                    value->value & 0xffffUL);
        return;
    }
    if (value->kind == MIR_AGGREGATE_CHECK_LOCAL) {
        if (value->width == 4) {
            mir_machine_emit_ix_wide_load(
                out, value->local_offset);
        } else if (value->width == 2) {
            mir_stream_printf(out,
                    "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
                    value->local_offset,
                    value->local_offset + 1);
        } else {
            mir_stream_printf(out, "\tld l,(ix%+d)\n",
                    value->local_offset);
            if (value->is_unsigned)
                mir_stream_puts("\tld h,0\n", out);
            else
                mir_stream_puts("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n",
                      out);
        }
        return;
    }
    mir_machine_emit_global_address_hl(
        out, value->root, value->root_offset);
    if (value->width == 4) {
        mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tinc hl\n\tld a,(hl)\n\tinc hl\n"
              "\tld h,(hl)\n\tld l,a\n\tex de,hl\n", out);
    } else if (value->width == 2) {
        mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tex de,hl\n", out);
    } else {
        mir_stream_puts("\tld l,(hl)\n", out);
        if (value->is_unsigned)
            mir_stream_puts("\tld h,0\n", out);
        else
            mir_stream_puts("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n",
                  out);
    }
}

static void mir_aggregate_emit_check(
    MirStream *out, const struct MirAggregateCheck *check)
{
    if (check->width == 4) {
        mir_machine_emit_float_bits(out, check->expected);
        mir_stream_puts("\tpush de\n\tpush hl\n", out);
        mir_aggregate_emit_value(out, &check->actual);
        mir_stream_puts("\tpush de\n\tpush hl\n", out);
    } else {
        mir_stream_printf(out, "\tld hl,%lu\n\tpush hl\n",
                check->expected & 0xffffUL);
        mir_aggregate_emit_value(out, &check->actual);
        mir_stream_puts("\tpush hl\n", out);
    }
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            check->string_id);
    mir_machine_emit_symbol_call(out, check->function);
    mir_aggregate_cleanup(out, check->width == 4 ? 5 : 3);
}

static void mir_aggregate_emit_wide_check(
    MirStream *out, struct Sym *function, int string_id,
    unsigned long expected, int local_offset)
{
    mir_machine_emit_float_bits(out, expected);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, local_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_machine_emit_symbol_call(out, function);
    mir_aggregate_cleanup(out, 5);
}

static void mir_aggregate_emit_word_check(
    MirStream *out, struct Sym *function, int string_id,
    unsigned long expected)
{
    mir_stream_printf(out, "\tld hl,%lu\n\tpush hl\n",
            expected & 0xffffUL);
    mir_stream_puts("\tpush de\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_machine_emit_symbol_call(out, function);
    mir_aggregate_cleanup(out, 3);
}

static void mir_touch_local_emit_check(
    MirStream *out, const struct MirTouchLocalsPlan *plan,
    const struct MirTouchLocalCheck *check)
{
    const struct MirTouchLocalStore *store =
        &plan->stores[check->store_index];

    if (check->width == 4) {
        mir_machine_emit_float_bits(out, check->expected);
        mir_stream_puts("\tpush de\n\tpush hl\n", out);
        mir_machine_emit_ix_wide_load(out, store->compact_offset);
        mir_stream_puts("\tpush de\n\tpush hl\n", out);
    } else {
        mir_stream_printf(out, "\tld hl,%lu\n\tpush hl\n",
                check->expected & 0xffffUL);
        if (check->width == 1) {
            mir_stream_printf(out, "\tld a,(ix%+d)\n\tld e,a\n",
                    store->compact_offset);
            if (check->is_unsigned)
                mir_stream_puts("\tld d,0\n", out);
            else
                mir_stream_puts("\trlca\n\tsbc a,a\n\tld d,a\n", out);
        } else {
            mir_stream_printf(out,
                    "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
                    store->compact_offset,
                    store->compact_offset + 1);
        }
        mir_stream_puts("\tpush de\n", out);
    }
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            check->string_id);
    mir_machine_emit_symbol_call(out, check->function);
    mir_aggregate_cleanup(out, check->width == 4 ? 5 : 3);
}

static void mir_emit_touch_locals(
    MirStream *out, const struct MirTouchLocalsPlan *plan)
{
    int item;

    mir_stream_printf(out,
            "%s\n"
            "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n",
            MIR_EXACT_KERNEL_MARKER, plan->frame_bytes);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (item = 0; item < MIR_TOUCH_LOCAL_CHECK_COUNT; ++item)
        mir_aggregate_emit_ix_store(
            out, plan->stores[item].compact_offset,
            plan->stores[item].width,
            plan->stores[item].value);
    for (item = 0; item < MIR_TOUCH_LOCAL_CHECK_COUNT; ++item)
        mir_touch_local_emit_check(
            out, plan, &plan->checks[item]);
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static void mir_aggregate_scale_hl(MirStream *out, int factor)
{
    int add;

    if (factor <= 1)
        return;
    if (factor == 2) {
        mir_stream_puts("\tadd hl,hl\n", out);
        return;
    }
    mir_stream_puts("\tld d,h\n\tld e,l\n", out);
    if (factor == 3) {
        mir_stream_puts("\tadd hl,hl\n\tadd hl,de\n", out);
        return;
    }
    if (factor == 4) {
        mir_stream_puts("\tadd hl,hl\n\tadd hl,hl\n", out);
        return;
    }
    if (factor == 6) {
        mir_stream_puts("\tadd hl,hl\n\tadd hl,de\n\tadd hl,hl\n", out);
        return;
    }
    if (factor == 12) {
        mir_stream_puts("\tadd hl,hl\n\tadd hl,de\n"
              "\tadd hl,hl\n\tadd hl,hl\n", out);
        return;
    }
    for (add = 1; add < factor; ++add)
        mir_stream_puts("\tadd hl,de\n", out);
}

static void mir_packed_emit_record_address(
    MirStream *out, int offset)
{
    int step;

    mir_stream_puts("\tld l,(ix-4)\n\tld h,(ix-3)\n", out);
    for (step = 0; step < offset; ++step)
        mir_stream_puts("\tinc hl\n", out);
}

static void mir_packed_emit_index_value(
    MirStream *out, int scale, int negate)
{
    int factor;

    mir_stream_puts("\tld l,(ix-2)\n\tld h,(ix-1)\n", out);
    for (factor = 1; factor < scale; factor *= 2)
        mir_stream_puts("\tadd hl,hl\n", out);
    if (negate)
        mir_stream_puts("\txor a\n\tsub l\n\tld l,a\n"
              "\tld a,0\n\tsbc a,h\n\tld h,a\n", out);
}

static void mir_packed_emit_sign_high(MirStream *out)
{
    mir_stream_puts("\tld a,h\n\trlca\n\tsbc a,a\n"
          "\tld e,a\n\tld d,a\n", out);
}

static void mir_packed_emit_push_index_wide(
    MirStream *out, int scale, int negate)
{
    mir_packed_emit_index_value(out, scale, negate);
    if (negate)
        mir_packed_emit_sign_high(out);
    else
        mir_stream_puts("\tld de,0\n", out);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
}

static void mir_packed_emit_push_member_wide(
    MirStream *out, int offset, int width, int is_signed)
{
    mir_packed_emit_record_address(out, offset);
    if (width == 1) {
        mir_stream_puts("\tld l,(hl)\n", out);
        if (is_signed)
            mir_stream_puts("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n", out);
        else
            mir_stream_puts("\tld h,0\n", out);
        if (is_signed)
            mir_packed_emit_sign_high(out);
        else
            mir_stream_puts("\tld de,0\n", out);
    } else if (width == 2) {
        mir_stream_puts("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
              "\tld l,c\n\tld h,b\n", out);
        if (is_signed)
            mir_packed_emit_sign_high(out);
        else
            mir_stream_puts("\tld de,0\n", out);
    } else {
        mir_stream_puts("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
              "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tld l,c\n\tld h,b\n", out);
    }
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
}

static void mir_packed_emit_print_failure(
    MirStream *out, const struct MirPackedRecordRunner *plan,
    int check)
{
    static const int actual_members[6] = {0, 1, 2, 3, 1, 2};
    static const int expected_scales[6] = {1, 2, 4, 1, 2, 4};
    int member = actual_members[check];

    mir_packed_emit_push_index_wide(
        out, expected_scales[check], check >= 3);
    mir_packed_emit_push_member_wide(
        out, plan->member_offsets[member],
        member == 0 || member == 3 ? 1 :
        member == 1 || member == 4 ? 2 : 4,
        member == 3);
    mir_stream_puts("\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[check]);
    mir_emit_runtime_call(out, plan->print_call_name);
    mir_aggregate_cleanup(out, 6);
}

static void mir_packed_emit_byte_check(
    MirStream *out, const struct MirPackedRecordRunner *plan,
    int check, int member, int scale, int negate, int done)
{
    mir_packed_emit_record_address(out, plan->member_offsets[member]);
    mir_stream_puts("\tld b,(hl)\n\tld a,(ix-2)\n", out);
    if (scale >= 2)
        mir_stream_puts("\tadd a,a\n", out);
    if (scale >= 4)
        mir_stream_puts("\tadd a,a\n", out);
    if (negate)
        mir_stream_puts("\tld c,a\n\txor a\n\tsub c\n", out);
    mir_stream_printf(out, "\tcp b\n\tjp z,L%d\n", done);
    mir_packed_emit_print_failure(out, plan, check);
    mir_stream_printf(out, "L%d:\n", done);
}

static void mir_packed_emit_word_check(
    MirStream *out, const struct MirPackedRecordRunner *plan,
    int check, int member, int scale, int negate, int done)
{
    mir_packed_emit_record_address(out, plan->member_offsets[member]);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n", out);
    mir_packed_emit_index_value(out, scale, negate);
    mir_stream_puts("\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", done);
    mir_packed_emit_print_failure(out, plan, check);
    mir_stream_printf(out, "L%d:\n", done);
}

static void mir_packed_emit_wide_check(
    MirStream *out, const struct MirPackedRecordRunner *plan,
    int check, int member, int scale, int negate, int done)
{
    int mismatch = new_label();

    mir_packed_emit_index_value(out, scale, negate);
    if (negate)
        mir_packed_emit_sign_high(out);
    else
        mir_stream_puts("\tld de,0\n", out);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_packed_emit_record_address(out, plan->member_offsets[member]);
    mir_stream_puts("\tpop bc\n\tpop de\n"
          "\tld a,(hl)\n\tcp c\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", mismatch);
    mir_stream_puts("\tinc hl\n\tld a,(hl)\n\tcp b\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", mismatch);
    mir_stream_puts("\tinc hl\n\tld a,(hl)\n\tcp e\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", mismatch);
    mir_stream_puts("\tinc hl\n\tld a,(hl)\n\tcp d\n", out);
    mir_stream_printf(out, "\tjp z,L%d\nL%d:\n", done, mismatch);
    mir_packed_emit_print_failure(out, plan, check);
    mir_stream_printf(out, "L%d:\n", done);
}

static void mir_packed_emit_memset(
    MirStream *out, struct Sym *root, int bytes)
{
    mir_machine_emit_global_address_hl(out, root, 0);
    mir_stream_printf(out, "\tld e,0\n\tld bc,%d\n", bytes);
    mir_emit_runtime_call(out, "__msf");
}

static void mir_packed_emit_record_pointer(
    MirStream *out, const struct MirPackedRecordRunner *plan)
{
    mir_stream_puts("\tld l,(ix-2)\n\tld h,(ix-1)\n", out);
    mir_aggregate_scale_hl(out, plan->record_stride);
    mir_machine_emit_global_address_de(out, plan->records, 0);
    mir_stream_puts("\tadd hl,de\n\tld (ix-4),l\n\tld (ix-3),h\n", out);
}

static void mir_packed_emit_initializers(
    MirStream *out, const struct MirPackedRecordRunner *plan)
{
    mir_packed_emit_record_address(out, plan->member_offsets[0]);
    mir_stream_puts("\tld a,(ix-2)\n\tld (hl),a\n", out);

    mir_packed_emit_index_value(out, 2, 0);
    mir_stream_puts("\tld c,l\n\tld b,h\n", out);
    mir_packed_emit_record_address(out, plan->member_offsets[1]);
    mir_stream_puts("\tld (hl),c\n\tinc hl\n\tld (hl),b\n", out);

    mir_packed_emit_index_value(out, 4, 0);
    mir_stream_puts("\tld c,l\n\tld b,h\n", out);
    mir_packed_emit_record_address(out, plan->member_offsets[2]);
    mir_stream_puts("\tld (hl),c\n\tinc hl\n\tld (hl),b\n"
          "\tinc hl\n\tld (hl),0\n\tinc hl\n\tld (hl),0\n", out);

    mir_packed_emit_record_address(out, plan->member_offsets[3]);
    mir_stream_puts("\tld a,(ix-2)\n\tld c,a\n\txor a\n\tsub c\n"
          "\tld (hl),a\n", out);

    mir_packed_emit_index_value(out, 2, 1);
    mir_stream_puts("\tld c,l\n\tld b,h\n", out);
    mir_packed_emit_record_address(out, plan->member_offsets[4]);
    mir_stream_puts("\tld (hl),c\n\tinc hl\n\tld (hl),b\n", out);

    mir_packed_emit_index_value(out, 4, 1);
    mir_packed_emit_sign_high(out);
    mir_stream_puts("\tld c,l\n\tld b,h\n", out);
    mir_packed_emit_record_address(out, plan->member_offsets[5]);
    mir_stream_puts("\tld (hl),c\n\tinc hl\n\tld (hl),b\n"
          "\tinc hl\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n", out);
}

static void mir_emit_packed_record_runner(
    MirStream *out, const struct MirPackedRecordRunner *plan)
{
    int initialize_loop = new_label();
    int initialize_done = new_label();
    int check_loop = new_label();
    int check_done = new_label();
    int check_labels[8];
    int item;

    for (item = 0; item < 8; ++item)
        check_labels[item] = new_label();
    mir_stream_printf(out,
            "%s\n"
            "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tld hl,-4\n\tadd hl,sp\n\tld sp,hl\n",
            MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    mir_packed_emit_memset(
        out, plan->guards[0],
        plan->guard_count * plan->record_stride);
    mir_packed_emit_memset(
        out, plan->guards[1],
        plan->guard_count * plan->record_stride);
    mir_stream_puts("\tld hl,0\n\tld (ix-2),l\n\tld (ix-1),h\n", out);
    mir_stream_printf(out,
            "L%d:\n\tld a,(ix-1)\n\tor a\n\tjp nz,L%d\n"
            "\tld a,(ix-2)\n\tcp %d\n\tjp nc,L%d\n",
            initialize_loop, initialize_done,
            plan->record_count, initialize_done);
    mir_packed_emit_record_pointer(out, plan);
    mir_packed_emit_initializers(out, plan);
    mir_stream_printf(out,
            "\tinc (ix-2)\n\tjp nz,L%d\n\tinc (ix-1)\n"
            "\tjp L%d\nL%d:\n",
            check_labels[0], initialize_loop,
            check_labels[0]);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n",
            initialize_loop, initialize_done);

    mir_packed_emit_memset(
        out, plan->guards[0],
        plan->guard_count * plan->record_stride);
    mir_packed_emit_memset(
        out, plan->guards[1],
        plan->guard_count * plan->record_stride);
    mir_stream_puts("\tld hl,0\n\tld (ix-2),l\n\tld (ix-1),h\n", out);
    mir_stream_printf(out,
            "L%d:\n\tld a,(ix-1)\n\tor a\n\tjp nz,L%d\n"
            "\tld a,(ix-2)\n\tcp %d\n\tjp nc,L%d\n",
            check_loop, check_done,
            plan->record_count, check_done);
    mir_packed_emit_record_pointer(out, plan);

    mir_packed_emit_byte_check(
        out, plan, 0, 0, 1, 0, check_labels[1]);
    mir_packed_emit_word_check(
        out, plan, 1, 1, 2, 0, check_labels[2]);
    mir_packed_emit_wide_check(
        out, plan, 2, 2, 4, 0, check_labels[3]);
    mir_packed_emit_byte_check(
        out, plan, 3, 3, 1, 1, check_labels[4]);
    mir_packed_emit_word_check(
        out, plan, 4, 4, 2, 1, check_labels[5]);
    mir_packed_emit_wide_check(
        out, plan, 5, 5, 4, 1, check_labels[6]);

    mir_stream_printf(out,
            "\tinc (ix-2)\n\tjp nz,L%d\n\tinc (ix-1)\n"
            "\tjp L%d\nL%d:\n",
            check_labels[7], check_loop, check_labels[7]);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", check_loop, check_done);

    mir_stream_puts("\tld hl,4\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n",
            plan->record_count * plan->record_stride);
    mir_machine_emit_global_address_hl(out, plan->records, 0);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->dump_function);
    mir_aggregate_cleanup(out, 3);
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static void mir_multidim_emit_byte_store(
    MirStream *out, struct Sym *root, int offset, int value)
{
    mir_machine_emit_global_address_hl(out, root, offset);
    mir_stream_printf(out, "\tld (hl),%d\n", value & 0xff);
}

static void mir_multidim_emit_word_store(
    MirStream *out, struct Sym *root, int offset, int value)
{
    mir_machine_emit_global_address_hl(out, root, offset);
    mir_stream_printf(out,
            "\tld (hl),%d\n\tinc hl\n\tld (hl),%d\n",
            value & 0xff, (value >> 8) & 0xff);
}

static void mir_multidim_emit_check_at(
    MirStream *out, const struct MirMultidimArrayRunner *plan,
    int check, struct Sym *root, int offset, int width, int expected)
{
    mir_machine_emit_global_address_hl(out, root, offset);
    mir_stream_puts("\tld e,(hl)\n", out);
    if (width == 1)
        mir_stream_puts("\tld d,0\n", out);
    else
        mir_stream_puts("\tinc hl\n\tld d,(hl)\n", out);
    mir_aggregate_emit_word_check(
        out, plan->check_function,
        plan->check_strings[check], (unsigned long)expected);
}

static void mir_multidim_emit_dynamic_2d_address(
    MirStream *out, struct Sym *root, int array_offset,
    int row_offset, int column_offset,
    int row_stride, int column_stride)
{
    mir_machine_emit_global_address_hl(out, root, row_offset);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n", out);
    mir_aggregate_scale_hl(out, row_stride);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_global_address_hl(out, root, column_offset);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n", out);
    mir_aggregate_scale_hl(out, column_stride);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_global_address_hl(out, root, array_offset);
    mir_stream_puts("\tpop de\n\tadd hl,de\n"
          "\tpop de\n\tadd hl,de\n", out);
}

static void mir_multidim_emit_call_2d_address(
    MirStream *out, const struct MirMultidimArrayRunner *plan)
{
    mir_machine_emit_symbol_call(out, plan->row_function);
    mir_aggregate_scale_hl(out, plan->byte_row_stride);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->column_function);
    mir_aggregate_scale_hl(out, plan->byte_column_stride);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_global_address_hl(
        out, plan->byte_matrix, plan->byte_array_offset);
    mir_stream_puts("\tpop de\n\tadd hl,de\n"
          "\tpop de\n\tadd hl,de\n", out);
}

static void mir_multidim_emit_dynamic_3d_address(
    MirStream *out, const struct MirMultidimArrayRunner *plan)
{
    mir_machine_emit_global_address_hl(
        out, plan->cube, plan->cube_a_offset);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n", out);
    mir_aggregate_scale_hl(out, plan->cube_plane_stride);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_global_address_hl(
        out, plan->cube, plan->cube_b_offset);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n", out);
    mir_aggregate_scale_hl(out, plan->cube_row_stride);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_global_address_hl(
        out, plan->cube, plan->cube_d_offset);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n", out);
    mir_aggregate_scale_hl(out, plan->cube_column_stride);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_global_address_hl(
        out, plan->cube, plan->cube_array_offset);
    mir_stream_puts("\tpop de\n\tadd hl,de\n"
          "\tpop de\n\tadd hl,de\n"
          "\tpop de\n\tadd hl,de\n", out);
}

static void mir_emit_multidim_array_runner(
    MirStream *out, const struct MirMultidimArrayRunner *plan)
{
    int plane;
    int row;
    int column;
    int value;
    int success = new_label();

    mir_stream_printf(out, "%s\n", MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    mir_multidim_emit_byte_store(
        out, plan->byte_matrix,
        plan->byte_array_offset, 1);
    mir_multidim_emit_byte_store(
        out, plan->byte_matrix,
        plan->byte_array_offset + plan->byte_row_stride, 15);
    mir_multidim_emit_byte_store(
        out, plan->byte_matrix,
        plan->byte_array_offset +
            2 * plan->byte_row_stride +
            3 * plan->byte_column_stride, 42);
    mir_multidim_emit_byte_store(
        out, plan->byte_matrix,
        plan->byte_array_offset +
            plan->byte_row_stride +
            2 * plan->byte_column_stride, 99);
    mir_multidim_emit_check_at(
        out, plan, 0, plan->byte_matrix,
        plan->byte_array_offset, 1, 1);
    mir_multidim_emit_check_at(
        out, plan, 1, plan->byte_matrix,
        plan->byte_array_offset + plan->byte_row_stride, 1, 15);
    mir_multidim_emit_check_at(
        out, plan, 2, plan->byte_matrix,
        plan->byte_array_offset +
            2 * plan->byte_row_stride +
            3 * plan->byte_column_stride, 1, 42);
    mir_multidim_emit_check_at(
        out, plan, 3, plan->byte_matrix,
        plan->byte_array_offset +
            plan->byte_row_stride +
            2 * plan->byte_column_stride, 1, 99);

    mir_machine_emit_global_address_hl(
        out, plan->byte_matrix, plan->byte_array_offset);
    for (row = 0; row < plan->byte_rows; ++row)
        for (column = 0; column < plan->byte_columns; ++column) {
            value = row * plan->byte_columns + column;
            mir_stream_printf(out, "\tld (hl),%d\n", value);
            if (row != plan->byte_rows - 1 ||
                column != plan->byte_columns - 1)
                mir_stream_puts("\tinc hl\n", out);
        }
    mir_multidim_emit_check_at(
        out, plan, 4, plan->byte_matrix,
        plan->byte_array_offset +
            2 * plan->byte_row_stride +
            3 * plan->byte_column_stride, 1, 11);
    mir_multidim_emit_check_at(
        out, plan, 5, plan->byte_matrix,
        plan->byte_array_offset +
            plan->byte_row_stride +
            plan->byte_column_stride, 1, 5);

    mir_multidim_emit_word_store(
        out, plan->byte_matrix, plan->byte_row_offset, 2);
    mir_multidim_emit_word_store(
        out, plan->byte_matrix, plan->byte_column_offset, 1);
    mir_multidim_emit_dynamic_2d_address(
        out, plan->byte_matrix, plan->byte_array_offset,
        plan->byte_row_offset, plan->byte_column_offset,
        plan->byte_row_stride, plan->byte_column_stride);
    mir_stream_puts("\tld (hl),77\n", out);
    mir_multidim_emit_check_at(
        out, plan, 6, plan->byte_matrix,
        plan->byte_array_offset +
            2 * plan->byte_row_stride +
            plan->byte_column_stride, 1, 77);
    mir_multidim_emit_dynamic_2d_address(
        out, plan->byte_matrix, plan->byte_array_offset,
        plan->byte_row_offset, plan->byte_column_offset,
        plan->byte_row_stride, plan->byte_column_stride);
    mir_stream_puts("\tld e,(hl)\n\tld d,0\n", out);
    mir_aggregate_emit_word_check(
        out, plan->check_function,
        plan->check_strings[7], 77);

    mir_multidim_emit_word_store(
        out, plan->byte_matrix, plan->byte_row_offset, 1);
    mir_multidim_emit_word_store(
        out, plan->byte_matrix, plan->byte_column_offset, 3);
    mir_multidim_emit_call_2d_address(out, plan);
    mir_stream_puts("\tld (hl),55\n", out);
    mir_multidim_emit_check_at(
        out, plan, 8, plan->byte_matrix,
        plan->byte_array_offset +
            plan->byte_row_stride +
            3 * plan->byte_column_stride, 1, 55);

    mir_multidim_emit_word_store(
        out, plan->word_matrix,
        plan->word_array_offset, 1000);
    mir_multidim_emit_word_store(
        out, plan->word_matrix,
        plan->word_array_offset + plan->word_row_stride, 2000);
    mir_multidim_emit_word_store(
        out, plan->word_matrix,
        plan->word_array_offset +
            2 * plan->word_row_stride +
            3 * plan->word_column_stride, 3003);
    mir_multidim_emit_word_store(
        out, plan->word_matrix,
        plan->word_array_offset +
            plan->word_row_stride +
            2 * plan->word_column_stride, 1202);
    mir_multidim_emit_check_at(
        out, plan, 9, plan->word_matrix,
        plan->word_array_offset, 2, 1000);
    mir_multidim_emit_check_at(
        out, plan, 10, plan->word_matrix,
        plan->word_array_offset + plan->word_row_stride, 2, 2000);
    mir_multidim_emit_check_at(
        out, plan, 11, plan->word_matrix,
        plan->word_array_offset +
            2 * plan->word_row_stride +
            3 * plan->word_column_stride, 2, 3003);
    mir_multidim_emit_check_at(
        out, plan, 12, plan->word_matrix,
        plan->word_array_offset +
            plan->word_row_stride +
            2 * plan->word_column_stride, 2, 1202);

    mir_multidim_emit_word_store(
        out, plan->word_matrix, plan->word_row_offset, 2);
    mir_multidim_emit_word_store(
        out, plan->word_matrix, plan->word_column_offset, 2);
    mir_multidim_emit_dynamic_2d_address(
        out, plan->word_matrix, plan->word_array_offset,
        plan->word_row_offset, plan->word_column_offset,
        plan->word_row_stride, plan->word_column_stride);
    mir_stream_puts("\tld (hl),146\n\tinc hl\n\tld (hl),16\n", out);
    mir_multidim_emit_check_at(
        out, plan, 13, plan->word_matrix,
        plan->word_array_offset +
            2 * plan->word_row_stride +
            2 * plan->word_column_stride, 2, 4242);
    mir_multidim_emit_dynamic_2d_address(
        out, plan->word_matrix, plan->word_array_offset,
        plan->word_row_offset, plan->word_column_offset,
        plan->word_row_stride, plan->word_column_stride);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n", out);
    mir_aggregate_emit_word_check(
        out, plan->check_function,
        plan->check_strings[14], 4242);

    mir_machine_emit_global_address_hl(
        out, plan->cube, plan->cube_array_offset);
    for (plane = 0; plane < plan->cube_planes; ++plane)
        for (row = 0; row < plan->cube_rows; ++row)
            for (column = 0; column < plan->cube_columns; ++column) {
                value = plane * 100 + row * 10 + column;
                mir_stream_printf(out,
                        "\tld (hl),%d\n\tinc hl\n"
                        "\tld (hl),%d\n",
                        value & 0xff, (value >> 8) & 0xff);
                if (plane != plan->cube_planes - 1 ||
                    row != plan->cube_rows - 1 ||
                    column != plan->cube_columns - 1)
                    mir_stream_puts("\tinc hl\n", out);
            }
    mir_multidim_emit_check_at(
        out, plan, 15, plan->cube,
        plan->cube_array_offset, 2, 0);
    mir_multidim_emit_check_at(
        out, plan, 16, plan->cube,
        plan->cube_array_offset +
            plan->cube_plane_stride +
            2 * plan->cube_row_stride +
            3 * plan->cube_column_stride, 2, 123);
    mir_multidim_emit_check_at(
        out, plan, 17, plan->cube,
        plan->cube_array_offset +
            plan->cube_plane_stride, 2, 100);
    mir_multidim_emit_check_at(
        out, plan, 18, plan->cube,
        plan->cube_array_offset +
            2 * plan->cube_row_stride +
            plan->cube_column_stride, 2, 21);
    mir_multidim_emit_word_store(
        out, plan->cube, plan->cube_a_offset, 1);
    mir_multidim_emit_word_store(
        out, plan->cube, plan->cube_b_offset, 2);
    mir_multidim_emit_word_store(
        out, plan->cube, plan->cube_d_offset, 3);
    mir_multidim_emit_dynamic_3d_address(out, plan);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n", out);
    mir_aggregate_emit_word_check(
        out, plan->check_function,
        plan->check_strings[19], 123);

    mir_multidim_emit_byte_store(
        out, plan->grid,
        plan->grid_cells_offset + plan->grid_array_offset, 10);
    mir_multidim_emit_byte_store(
        out, plan->grid,
        plan->grid_cells_offset + plan->grid_cell_stride +
            plan->grid_array_offset + plan->grid_row_stride, 21);
    mir_multidim_emit_byte_store(
        out, plan->grid,
        plan->grid_cells_offset + 2 * plan->grid_cell_stride +
            plan->grid_array_offset + plan->grid_column_stride, 32);
    mir_multidim_emit_byte_store(
        out, plan->grid,
        plan->grid_cells_offset + plan->grid_cell_stride +
            plan->grid_array_offset + plan->grid_row_stride +
            plan->grid_column_stride, 23);
    mir_multidim_emit_check_at(
        out, plan, 20, plan->grid,
        plan->grid_cells_offset + plan->grid_array_offset, 1, 10);
    mir_multidim_emit_check_at(
        out, plan, 21, plan->grid,
        plan->grid_cells_offset + plan->grid_cell_stride +
            plan->grid_array_offset + plan->grid_row_stride, 1, 21);
    mir_multidim_emit_check_at(
        out, plan, 22, plan->grid,
        plan->grid_cells_offset + 2 * plan->grid_cell_stride +
            plan->grid_array_offset + plan->grid_column_stride, 1, 32);
    mir_multidim_emit_check_at(
        out, plan, 23, plan->grid,
        plan->grid_cells_offset + plan->grid_cell_stride +
            plan->grid_array_offset + plan->grid_row_stride +
            plan->grid_column_stride, 1, 23);

    mir_machine_emit_global_word(out, plan->failures, 0);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", success);
    mir_machine_emit_global_word(out, plan->failures, 0);
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->failure_string);
    mir_aggregate_emit_format_call(
        out, plan->print_function, plan->failure_call_name);
    mir_aggregate_cleanup(out, 2);
    mir_stream_puts("\tld hl,1\n\tret\n", out);
    mir_stream_printf(out, "L%d:\n\tld hl,S%d\n\tpush hl\n",
            success, plan->success_string);
    mir_aggregate_emit_format_call(
        out, plan->print_function, plan->success_call_name);
    mir_stream_puts("\tpop bc\n\tld hl,0\n\tret\n", out);
}

static void mir_emit_multidim_aggregate_checks(
    MirStream *out, const struct MirAggregateMultidimChecks *plan)
{
    const int loop_row = label_id++;
    const int loop_column = label_id++;
    const int column_incremented = label_id++;
    const int next_row = label_id++;
    const int done = label_id++;
    int cells_offset =
        plan->multidim.cells_index *
            plan->multidim.cells_stride +
        plan->multidim.cells_tag_offset;

    mir_stream_printf(out,
            "%s\n"
            "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n",
            MIR_EXACT_KERNEL_MARKER, mir.local_bytes);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->heading_string_id);
    mir_aggregate_emit_format_call(
        out, plan->print_function, plan->heading_call_name);
    mir_stream_puts("\tpop bc\n", out);

    mir_machine_emit_global_address_hl(
        out, plan->multidim.board_root, 0);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(
        out, plan->multidim.board_fill_function);
    mir_stream_puts("\tpop bc\n", out);

    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n",
            plan->multidim.rows);
    mir_machine_emit_global_address_hl(
        out, plan->multidim.board_root, 0);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(
        out, plan->multidim.board_weight_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(
        out, plan->multidim.w_ptr_offset);
    mir_aggregate_emit_wide_check(
        out, plan->check_wide_function,
        plan->multidim.strings[0],
        plan->multidim.weight_expected,
        plan->multidim.w_ptr_offset);

    mir_aggregate_emit_ix_store(
        out, plan->multidim.w_struct_offset, 4, 0);
    mir_stream_printf(out, "\tld (ix%+d),0\nL%d:\n"
            "\tld (ix%+d),0\n\tld (ix%+d),0\nL%d:\n",
            plan->multidim.row_offset, loop_row,
            plan->multidim.column_offset,
            plan->multidim.column_offset + 1,
            loop_column);
    mir_machine_emit_global_address_hl(
        out, plan->multidim.board_root, 0);
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out, "\tld l,(ix%+d)\n\tld h,0\n",
            plan->multidim.row_offset);
    mir_aggregate_scale_hl(
        out, plan->multidim.board_row_stride);
    mir_stream_puts("\tex de,hl\n\tpop hl\n\tadd hl,de\n\tpush hl\n",
          out);
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            plan->multidim.column_offset,
            plan->multidim.column_offset + 1);
    mir_aggregate_scale_hl(
        out, plan->multidim.board_column_stride);
    mir_stream_puts("\tex de,hl\n\tpop hl\n\tadd hl,de\n", out);
    mir_machine_emit_hl_offset(
        out, plan->multidim.board_weight_offset, 0);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n\tld a,h\n\trlca\n\tsbc a,a\n"
          "\tld d,a\n\tld e,a\n\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(
        out, plan->multidim.w_struct_offset);
    mir_stream_puts("\tpop bc\n\tadd hl,bc\n\tex de,hl\n"
          "\tpop bc\n\tadc hl,bc\n\tex de,hl\n", out);
    mir_machine_emit_ix_wide_store(
        out, plan->multidim.w_struct_offset);
    mir_stream_printf(out,
            "\tinc (ix%+d)\n\tjp nz,L%d\n"
            "\tinc (ix%+d)\nL%d:\n"
            "\tld a,(ix%+d)\n\tor a\n\tjp nz,L%d\n"
            "\tld a,(ix%+d)\n\tcp %d\n\tjp c,L%d\n"
            "L%d:\n\tinc (ix%+d)\n"
            "\tld a,(ix%+d)\n\tcp %d\n\tjp c,L%d\n"
            "\tjp L%d\n",
            plan->multidim.column_offset, column_incremented,
            plan->multidim.column_offset + 1, column_incremented,
            plan->multidim.column_offset + 1, next_row,
            plan->multidim.column_offset,
            plan->multidim.columns, loop_column,
            next_row, plan->multidim.row_offset,
            plan->multidim.row_offset,
            plan->multidim.rows, loop_row, done);
    mir_stream_printf(out, "L%d:\n", done);
    mir_aggregate_emit_wide_check(
        out, plan->check_wide_function,
        plan->multidim.strings[1],
        plan->multidim.weight_expected,
        plan->multidim.w_struct_offset);

    mir_stream_printf(out,
            "\tld hl,3\n\tpush hl\n"
            "\tld hl,2\n\tpush hl\n");
    mir_machine_emit_global_address_hl(
        out, plan->multidim.board_root, 0);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(
        out, plan->multidim.tile_function);
    mir_aggregate_cleanup(out, 3);
    mir_stream_printf(out,
            "\tld (ix%+d),l\n\tld (ix%+d),h\n",
            plan->multidim.tile_offset,
            plan->multidim.tile_offset + 1);
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            plan->multidim.tile_offset,
            plan->multidim.tile_offset + 1);
    mir_machine_emit_hl_offset(
        out, plan->multidim.board_weight_offset, 0);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n", out);
    mir_aggregate_emit_word_check(
        out, plan->check_word_function,
        plan->multidim.strings[2],
        plan->multidim.tile_weight_expected);
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld l,(hl)\n\tld a,l\n\trlca\n"
            "\tsbc a,a\n\tld h,a\n\tex de,hl\n",
            plan->multidim.tile_offset,
            plan->multidim.tile_offset + 1);
    mir_aggregate_emit_word_check(
        out, plan->check_word_function,
        plan->multidim.strings[3],
        plan->multidim.tile_char_expected);

    mir_machine_emit_symbol_call(
        out, plan->multidim.cells_fill_function);
    mir_machine_emit_float_bits(
        out, plan->multidim.checksum_expected);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(
        out, plan->multidim.cells_checksum_function);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->multidim.strings[4]);
    mir_machine_emit_symbol_call(
        out, plan->check_wide_function);
    mir_aggregate_cleanup(out, 5);

    mir_machine_emit_global_address_hl(
        out, plan->multidim.cells_root, cells_offset);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n", out);
    mir_aggregate_emit_word_check(
        out, plan->check_word_function,
        plan->multidim.strings[5],
        plan->multidim.tag_expected);

    mir_machine_emit_symbol_call(
        out, plan->multidim.cells_checksum_function);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(
        out, plan->multidim.w_ptr_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->summary_string_id);
    mir_aggregate_emit_format_call(
        out, plan->print_function, plan->summary_call_name);
    mir_aggregate_cleanup(out, 5);
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static void mir_size2_emit_element_address(
    MirStream *out, const struct MirAggregateMultidimChecks *plan)
{
    mir_stream_puts("\tpush ix\n\tpop hl\n", out);
    mir_machine_emit_hl_offset(
        out, plan->size2.lg_offset, 0);
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            plan->size2.row_offset,
            plan->size2.row_offset + 1);
    mir_aggregate_scale_hl(out, plan->size2.row_stride);
    mir_stream_puts("\tex de,hl\n\tpop hl\n\tadd hl,de\n\tpush hl\n",
          out);
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            plan->size2.column_offset,
            plan->size2.column_offset + 1);
    mir_aggregate_scale_hl(out, plan->size2.column_stride);
    mir_stream_puts("\tex de,hl\n\tpop hl\n\tadd hl,de\n", out);
}

static void mir_emit_size2_aggregate_checks(
    MirStream *out, const struct MirAggregateMultidimChecks *plan)
{
    const int row_loop = label_id++;
    const int column_loop = label_id++;
    const int column_incremented = label_id++;
    const int next_row = label_id++;
    const int row_incremented = label_id++;
    const int after_loop = label_id++;
    int store;
    int check;

    mir_stream_printf(out,
            "%s\n"
            "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n",
            MIR_EXACT_KERNEL_MARKER, mir.local_bytes);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (store = 0; store < 43; ++store)
        mir_aggregate_emit_ix_store(
            out, plan->size2.stores[store].offset,
            plan->size2.stores[store].width,
            plan->size2.stores[store].value);
    mir_aggregate_emit_ix_store(
        out, plan->size2.sum_offset, 4, 0);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->heading_string_id);
    mir_aggregate_emit_format_call(
        out, plan->print_function, plan->heading_call_name);
    mir_stream_puts("\tpop bc\n", out);

    mir_stream_printf(out,
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->size2.row_count, plan->size2.row_count,
            plan->size2.strings[0]);
    mir_machine_emit_symbol_call(
        out, plan->check_word_function);
    mir_aggregate_cleanup(out, 3);
    mir_stream_printf(out,
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->size2.row_count * plan->size2.column_count,
            plan->size2.row_count * plan->size2.column_count,
            plan->size2.strings[1]);
    mir_machine_emit_symbol_call(
        out, plan->check_word_function);
    mir_aggregate_cleanup(out, 3);

    mir_aggregate_emit_ix_store(
        out, plan->size2.row_offset, 2, 0);
    mir_stream_printf(out, "L%d:\n", row_loop);
    mir_aggregate_emit_ix_store(
        out, plan->size2.column_offset, 2, 0);
    mir_stream_printf(out, "L%d:\n", column_loop);
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            plan->size2.row_offset,
            plan->size2.row_offset + 1);
    mir_aggregate_scale_hl(out, plan->size2.column_count);
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tex de,hl\n\tpop hl\n\tadd hl,de\n"
            "\tld (ix%+d),l\n\tld (ix%+d),h\n",
            plan->size2.column_offset,
            plan->size2.column_offset + 1,
            plan->size2.index_offset,
            plan->size2.index_offset + 1);

    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld de,%lu\n\tadd hl,de\n\tpush hl\n",
            plan->size2.index_offset,
            plan->size2.index_offset + 1,
            plan->size2.first_addend & 0xffffUL);
    mir_size2_emit_element_address(out, plan);
    mir_machine_emit_hl_offset(
        out, plan->size2.first_member_offset, 0);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->size2.strings[2]);
    mir_machine_emit_symbol_call(
        out, plan->check_word_function);
    mir_aggregate_cleanup(out, 3);

    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld de,%lu\n\tadd hl,de\n"
            "\tld a,h\n\trlca\n\tsbc a,a\n"
            "\tld d,a\n\tld e,a\n"
            "\tpush de\n\tpush hl\n",
            plan->size2.index_offset,
            plan->size2.index_offset + 1,
            plan->size2.second_addend & 0xffffUL);
    mir_size2_emit_element_address(out, plan);
    mir_machine_emit_hl_offset(
        out, plan->size2.second_member_offset, 0);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tinc hl\n\tld a,(hl)\n\tinc hl\n"
          "\tld h,(hl)\n\tld l,a\n\tex de,hl\n"
          "\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->size2.strings[3]);
    mir_machine_emit_symbol_call(
        out, plan->check_wide_function);
    mir_aggregate_cleanup(out, 5);

    mir_size2_emit_element_address(out, plan);
    mir_machine_emit_hl_offset(
        out, plan->size2.second_member_offset, 0);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tinc hl\n\tld a,(hl)\n\tinc hl\n"
          "\tld h,(hl)\n\tld l,a\n\tex de,hl\n"
          "\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, plan->size2.sum_offset);
    mir_stream_puts("\tpop bc\n\tadd hl,bc\n\tex de,hl\n"
          "\tpop bc\n\tadc hl,bc\n\tex de,hl\n", out);
    mir_machine_emit_ix_wide_store(out, plan->size2.sum_offset);

    mir_stream_printf(out,
            "\tinc (ix%+d)\n\tjp nz,L%d\n"
            "\tinc (ix%+d)\nL%d:\n"
            "\tld a,(ix%+d)\n\tor a\n\tjp nz,L%d\n"
            "\tld a,(ix%+d)\n\tcp %d\n\tjp c,L%d\n",
            plan->size2.column_offset, column_incremented,
            plan->size2.column_offset + 1, column_incremented,
            plan->size2.column_offset + 1, next_row,
            plan->size2.column_offset,
            plan->size2.column_count, column_loop);
    mir_stream_printf(out,
            "L%d:\n\tinc (ix%+d)\n\tjp nz,L%d\n"
            "\tinc (ix%+d)\nL%d:\n"
            "\tld a,(ix%+d)\n\tor a\n\tjp nz,L%d\n"
            "\tld a,(ix%+d)\n\tcp %d\n\tjp c,L%d\n",
            next_row, plan->size2.row_offset, row_incremented,
            plan->size2.row_offset + 1, row_incremented,
            plan->size2.row_offset + 1, after_loop,
            plan->size2.row_offset,
            plan->size2.row_count, row_loop);

    mir_stream_printf(out, "L%d:\n", after_loop);
    mir_aggregate_emit_wide_check(
        out, plan->check_wide_function, plan->size2.strings[4],
        plan->size2.sum_expected, plan->size2.sum_offset);
    for (check = 0; check < plan->size2.check_count; ++check)
        mir_aggregate_emit_check(out, &plan->size2.checks[check]);
    mir_machine_emit_ix_wide_load(out, plan->size2.sum_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->summary_string_id);
    mir_aggregate_emit_format_call(
        out, plan->print_function, plan->summary_call_name);
    mir_aggregate_cleanup(out, 3);
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static void mir_emit_aggregate_multidim_checks(
    MirStream *out, const struct MirAggregateMultidimChecks *plan)
{
    if (plan->kind == 1)
        mir_emit_multidim_aggregate_checks(out, plan);
    else
        mir_emit_size2_aggregate_checks(out, plan);
}

#define MIR_PTR_CONDITION_FAIL_CALLS 49
#define MIR_PTR_CONDITION_CHECK_CALLS 16

struct MirPtrConditionPlan {
    struct Sym *print_function;
    struct Sym *init_function;
    struct Sym *fail_function;
    struct Sym *check_function;
    struct Sym *pick_wrapper_function;
    struct Sym *pick_node_function;
    struct Sym *pick_leaf_function;
    struct Sym *pick_int_function;
    struct Sym *pick_long_function;
    struct Sym *globals[7];
    int start_string;
    int fail_strings[MIR_PTR_CONDITION_FAIL_CALLS];
    int check_strings[MIR_PTR_CONDITION_CHECK_CALLS];
    int failed_string;
    int pass_string;
    char print_names[3][64];
};

static const char mir_ptr_condition_opcodes[] =
    "LTAFRCIACAFRCIACAFRCIACAFRCIACAFRCIRCIWRCIRCIWRCIRCINWRCIRCINWRCIRCIWRCIRCIWRCIR"
    "CINWRCIRCINWCNSLPNCBQRNICNBWRNICNBUWRNICNUBWRNICNBWRNICNBUWRNICNUBWNLNCBSJLRCINS"
    "RCIMCINSRCIMCIMCINSCNSRCIDDMCIMCIMCIDCBQLNCBSJLTAFLRCIDDMDCIMCIMDCBQLOCBSJLTAFLR"
    "CIDDMCIMDMCIDCBQLOCBSJLTAFLRCIDDMCIMDCCBBDCBQLOCBSJLTAFLRACAFMCCBBDCIDCBQLOCBSJL"
    "TAFLRACAFACAFMCIMCIDCBQLOCBSJLTAFLRCIMCIACAFMDCBQLOCBSJLTAFLRCIMCIMCICIDCBQLOCBS"
    "JLTAFLRCCBBDCBQLOCBSJLTAFLRCCBBCCBBDCBQLOCBSJLTAFLRCIDDMCIMCIMCIDCUBQLOCBSJLTAFL"
    "RCIDDMDCIMCIMDCUBQLOCBSJLTAFLRCIDDMCIMDMCIDCUBQLOCBSJLTAFLRCIDDMCIMDCBDCUBQLOCBS"
    "JLTAFLRACAFMCCBBDCIDCBQLOCBSJLTAFLRACAFACAFMCIMCIDCUBQLOCBSJLTAFLRCIMCIACAFMDCUB"
    "QLOCBSJLTAFLRCIMCIMCICIDCUBQLOCBSJLTAFLRCBDCUBQLOCBSJLTAFLRCBCBDCUBQLOCBSJLTAFLR"
    "CIDDMCIMCIMCIDCBQLOCBSJLTAFLRCIDDMDCIMCIMDCBQLOCBSJLTAFLRCIDDMCIMDMCIDCBQLOCBSJL"
    "TAFLRCIDDMCIMDCCBBDCBQLOCBSJLTAFLRACAFMCCBBDCIDCBQLOCBSJLTAFLRACAFACAFMCIMCIDCBQ"
    "LOCBSJLTAFLRCIMCIACAFMDCBQLOCBSJLTAFLRCIMCIMCICIDCBQLOCBSJLTAFLRCCBBDCBQLOCBSJLT"
    "AFLRCCBBCCBBDCBQLOCBSJLTAFLRDMCIMCIMCIDCBQRDMCIMCIMCIDCBQLNJLJLNNLOCBSJLTAFLRDMC"
    "IMCIDCUBQLNJLRDMCIMCIDCUBQLNJLJLNLJLNNLOCBSJLTAFLRDMCIDCBUQLOCBSJLTAFLRCCBBDCBQR"
    "CBDCUBQLNJLJLNNRCCBBDCBQLNJLJLNNLOCBSJLTAFLRCCBBCCBBDCBQRCBCBDCUBQLNJLJLNNRCCBBC"
    "CBBDCBQLNJLJLNNLOCBSJLTAFLRCIDDCIMDCIMCIMCIDCBQLOCBSJLTAFLRCIDDCIMDCIMCIMCIDCUBQ"
    "LOCBSJLTAFLRCIDDCIMDCIMCIMCIDCBQLOCBSJLTAFLRCIDDMCIMCIMCIDCBQLTAFJLOCBSLRACAFACA"
    "FMCIMCIDCUBQLTAFJLOCBSLRCIMCIACAFMDCBQLTAFJLOCBSLTAOACAFCNSCNSCNSLNNNNOCBQROCBIA"
    "OCBAFDCBQLNJLJLNNROCBIAOCBAFDCBQLNJLJLNNOOBNSOCBSOCBSNCBQTAFNJNLNLJLTAOACAFTAOAC"
    "AFCNSCNSCNSLNNNNOCBQROCBIDMOCBIMOIMOIDCUBQLNJLJLNNROCBIDMOCBIMOIMOIDCBQLNJLJLNNO"
    "ROCBIDMOCBIMOIMOIDUBNSOCBSOCBSNCBQTAFNJNLNLJLTAOACAFTAOACAFCNSCNSLNNNNOCBQLNJLRC"
    "IDDMCIMCIMCIDCBQLNJLJLNLJLNNOCBSOCBSNCBQTAFNJNLNLJLTAOACAFCNSCNSCNSLNNNNOOBNSOCB"
    "SOCBSNCBQTAFNJNLNLOCBQROCBIDDMOCBIMDMOCBIDCBQLNJLJLNNJLTAOACAFTAOACAFCNSCNSCNSLN"
    "NNNOROCBIDDMOCBIMDOCBBDUBNSOCBSOCBSNCBQTAFNJNLNLOCBQROCBIDDMOCBIMDOCBCBBDCBQLNJL"
    "JLNNJLTAOACAFTAOACAFCNSCNSLPNPNNCBQRNCBBDCBQLNJLJLNNRNCBBDCBQLNJLJLNNNNBNSNLNCBS"
    "JLTANACAFTANACAFCNSCNSLPNPNNCBQRCIDDMCIMNIMDCBQLNJLJLNNNRCIDDMCIMNIMNIDBNSNLNCBS"
    "JLTANACAFTANACAFCNSCNSLPNPNNCBQRCIDDMCIMNIMNIDCBQLNJLJLNNNRCIDDMCIMNIMNIDUBNSNLN"
    "CBSJLTANACAFTANACAFRCIDDMCIMCIMCIDCBQRCIDDMCIMCIMCIDCBLJLRCIDDMCIMCIMCIDCUBLLPQL"
    "OCBSNJLTAFNLRCIDDMCIMCIMCIDCUBQRCIDDMCIMCIMCIDCBLJLRCIDDMCIMCIMCIDCBLLPQLOCBSNJL"
    "TAFNLRACAFMCIMDCIDCBQRACAFMCIMDCIDCUBQLNJLJLNNLNJLRACAFMCIMDCIDCBQRACAFMCIMDMDCB"
    "QLNJLJLNNLNJLJLNLJLNNLOCBSNJLTAFNLOQTAOAFCXNLTAFCX";

static const short mir_ptr_promoted_opcode_indices[] = {
    1018, 1021, 1023, 1024, 1050, 1067, 1070, 1072, 1076, 1077,
    1128, 1131, 1133, 1134, 1145, 1148, 1150, 1151, 1187, 1190,
    1192, 1193, 1208, 1211, 1213, 1214, 1450, 1453, 1455, 1456,
    1473, 1476, 1478, 1479, 1563, 1566, 1568, 1569, 1592, 1595,
    1597, 1598, 1675, 1697, 1700, 1702, 1706, 1707, 1806, 1809,
    1811, 1812, 1917, 1920, 1922, 1923, 1965, 1968, 1970, 1971,
    1982, 1985, 1987, 1988, 2048, 2051, 2053, 2054, 2130, 2133,
    2135, 2136, 2359, 2362, 2364, 2365, 2367, 2402, 2405, 2407,
    2408, 2410, 2413, 2415, 2419, 2420
};

static const char mir_ptr_promoted_opcodes[] =
    "CCPQCCCPPQCCPQCCPQCCPQCCPQCCPQCCPQCCPQCCPQCCCPPQCCPQCCPQ"
    "CCPQCCPQCCPQCCPQCCPQCCCPQCCPPQ";

static const char mir_ptr_binary_operations[] =
    "LAAAAAAAEAEAEAMAEAMAEAEAEAEAMAEAMAMAEAEAEAEAAEAMAEAEAEAE"
    "AAEAAAEAEAEAEAMAEAMAEAEAEAEAMAEAMAMAEAEEAEEANAMAEAEMAEAM"
    "AMAEAAEMAMAEAEAEAEANALANALDDRDDRAAAGLDDNDDGDDAAAGLNAAGAAA"
    "GLDDDGDDPAAAAGLDDPMAGLMARMALAALLAALGAAEEEAEEEAEEEEA";

static const char mir_ptr_binary_types[] =
    "AAAFAAFAAAAAAAADAAAGAAAAAAAAADAAADADAAAAAAAAEAAAHAAAAAAAAEAAEEAABABABAACBAAIBABA"
    "BABAACBAACACBAABAAAABAADAEAACBAADADAEEAACACBAAAAABAAAAABAAAAAAABAAAAAAAAAABAAAAA"
    "AAAAAAAAAAAAAAAAAAEAAAAAAAAACBAADAACBAAAAAAABAAABAAAABABAABA";

static const char mir_ptr_unary_operations[] =
    "CCCCCCCCCCCCCCCNCCCCCCCCCCC";

static char mir_ptr_condition_opcode_char(int opcode)
{
    switch (opcode) {
    case MIR_LABEL: return 'L';
    case MIR_CONST: return 'C';
    case MIR_BINARY: return 'B';
    case MIR_INDEX_ADDRESS: return 'I';
    case MIR_NOP: return 'N';
    case MIR_LOAD_INDIRECT: return 'D';
    case MIR_ARG: return 'A';
    case MIR_MEMBER_ADDRESS: return 'M';
    case MIR_JUMP: return 'J';
    case MIR_ADDRESS: return 'R';
    case MIR_LOAD: return 'O';
    case MIR_CALL: return 'F';
    case MIR_STORE: return 'S';
    case MIR_BRANCH_FALSE: return 'Q';
    case MIR_STRING_ADDRESS: return 'T';
    case MIR_UNARY: return 'U';
    case MIR_STORE_INDIRECT: return 'W';
    case MIR_PHI: return 'P';
    case MIR_RETURN: return 'X';
    default: return 0;
    }
}

static char mir_ptr_binary_operation_char(int operation)
{
    switch (operation) {
    case '<': return 'L';
    case '+': return 'A';
    case '*': return 'M';
    case '&': return 'D';
    case '%': return 'P';
    case '>': return 'G';
    case TOK_EQ: return 'E';
    case TOK_NE: return 'N';
    case TOK_GE: return 'R';
    default: return 0;
    }
}

static char mir_ptr_binary_type_char(
    int type, int operand_type)
{
    if (type == TYPE_INT && operand_type == TYPE_INT)
        return 'A';
    if (type == TYPE_INT && operand_type == TYPE_LONG)
        return 'B';
    if (type == (TYPE_LONG | TYPE_PTR) &&
        operand_type == TYPE_INT)
        return 'C';
    if (type == (TYPE_INT | TYPE_PTR) &&
        operand_type == TYPE_INT)
        return 'D';
    if (type == (TYPE_CHAR | TYPE_PTR) &&
        operand_type == TYPE_INT)
        return 'E';
    if (type == TYPE_LONG && operand_type == TYPE_LONG)
        return 'F';
    if (type == (TYPE_INT | TYPE_PTR | TYPE_PTR2) &&
        operand_type == TYPE_INT)
        return 'G';
    if (type == (TYPE_CHAR | TYPE_PTR | TYPE_PTR2) &&
        operand_type == TYPE_INT)
        return 'H';
    if (type == (TYPE_LONG | TYPE_PTR | TYPE_PTR2) &&
        operand_type == TYPE_INT)
        return 'I';
    return 0;
}

enum MirPtrConditionSlot {
    MIR_PTR_I = -2,
    MIR_PTR_COUNT = -4,
    MIR_PTR_SUM = -6,
    MIR_PTR_GUARD = -8,
    MIR_PTR_WP = -10,
    MIR_PTR_NP = -12,
    MIR_PTR_LP = -14,
    MIR_PTR_LWP = -18,
    MIR_PTR_LWPP = -22,
    MIR_PTR_LI = -38,
    MIR_PTR_LC = -46,
    MIR_PTR_LL = -78,
    MIR_PTR_LW = -762
};

enum MirPtrConditionGlobal {
    MIR_PTR_GW,
    MIR_PTR_GWP,
    MIR_PTR_GWPP,
    MIR_PTR_GI,
    MIR_PTR_GC,
    MIR_PTR_GL,
    MIR_PTR_FAILS
};

static void mir_ptr_ix_address(MirStream *out, int offset)
{
    mir_stream_puts("\tpush ix\n\tpop hl\n", out);
    mir_machine_emit_hl_offset(out, offset, 0);
}

static void mir_ptr_global_address(
    MirStream *out, struct Sym *symbol, int offset)
{
    mir_stream_printf(out, "\tld hl,%s\n",
            asm_name_for(sym_asm_name(symbol)));
    mir_machine_emit_hl_offset(out, offset, 0);
}

static void mir_ptr_load_pointer(MirStream *out)
{
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n", out);
}

static void mir_ptr_load_word(MirStream *out)
{
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n", out);
}

static void mir_ptr_local_wrapper(
    MirStream *out, int index)
{
    mir_ptr_ix_address(out, MIR_PTR_LW + index * 342);
}

static void mir_ptr_local_pointer_array(
    MirStream *out, int offset, int index, int dereferences)
{
    mir_ptr_ix_address(out, offset + index * 2);
    while (dereferences-- > 0)
        mir_ptr_load_pointer(out);
}

static void mir_ptr_global_pointer_array(
    MirStream *out, const struct MirPtrConditionPlan *plan,
    int global, int index, int dereferences)
{
    mir_ptr_global_address(
        out, plan->globals[global], index * 2);
    while (dereferences-- > 0)
        mir_ptr_load_pointer(out);
}

static void mir_ptr_wrapper_node_leaf(
    MirStream *out, int node, int leaf)
{
    mir_machine_emit_hl_offset(
        out, node * 155 + leaf * 35, 0);
}

static void mir_ptr_push_call2(
    MirStream *out, struct Sym *function, int second)
{
    mir_stream_puts("\tex de,hl\n", out);
    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n", second);
    mir_stream_puts("\tex de,hl\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
}

static void mir_ptr_fail(
    MirStream *out, const struct MirPtrConditionPlan *plan, int string)
{
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", string);
    mir_machine_emit_symbol_call(out, plan->fail_function);
    mir_stream_puts("\tpop bc\n", out);
}

static void mir_ptr_increment_count(MirStream *out)
{
    int done = new_label();

    mir_stream_printf(out,
            "\tinc (ix%+d)\n\tjp nz,L%d\n"
            "\tinc (ix%+d)\nL%d:\n",
            MIR_PTR_COUNT, done, MIR_PTR_COUNT + 1, done);
}

static void mir_ptr_word_equal_check(
    MirStream *out, const struct MirPtrConditionPlan *plan,
    int expected, int string)
{
    int failed = new_label();
    int done = new_label();

    mir_ptr_load_word(out);
    mir_stream_printf(out,
            "\tld de,%d\n\tor a\n\tsbc hl,de\n"
            "\tjp nz,L%d\n",
            expected, failed);
    mir_ptr_increment_count(out);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", done, failed);
    mir_ptr_fail(out, plan, string);
    mir_stream_printf(out, "L%d:\n", done);
}

static void mir_ptr_char_equal_check(
    MirStream *out, const struct MirPtrConditionPlan *plan,
    int expected, int string)
{
    int failed = new_label();
    int done = new_label();

    mir_stream_puts("\tld a,(hl)\n", out);
    mir_stream_printf(out, "\tcp %d\n\tjp nz,L%d\n",
            expected & 255, failed);
    mir_ptr_increment_count(out);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", done, failed);
    mir_ptr_fail(out, plan, string);
    mir_stream_printf(out, "L%d:\n", done);
}

static void mir_ptr_long_equal_check(
    MirStream *out, const struct MirPtrConditionPlan *plan,
    unsigned long expected, int string)
{
    int failed = new_label();
    int done = new_label();
    int byte;

    for (byte = 0; byte < 4; ++byte) {
        mir_stream_puts("\tld a,(hl)\n", out);
        mir_stream_printf(out, "\tcp %lu\n\tjp nz,L%d\n",
                (expected >> (byte * 8)) & 255UL, failed);
        if (byte != 3)
            mir_stream_puts("\tinc hl\n", out);
    }
    mir_ptr_increment_count(out);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", done, failed);
    mir_ptr_fail(out, plan, string);
    mir_stream_printf(out, "L%d:\n", done);
}

static void mir_ptr_check_int(
    MirStream *out, const struct MirPtrConditionPlan *plan,
    int string, int slot, int expected)
{
    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n", expected);
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n",
            slot, slot + 1);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", string);
    mir_machine_emit_symbol_call(out, plan->check_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n", out);
}

static void mir_ptr_store_word_slot(
    MirStream *out, int slot, int value)
{
    mir_stream_printf(out,
            "\tld (ix%+d),%d\n\tld (ix%+d),%d\n",
            slot, value & 255, slot + 1, (value >> 8) & 255);
}

static void mir_ptr_call_picker(
    MirStream *out, struct Sym *function, int index)
{
    mir_ptr_push_call2(out, function, index);
}

static void mir_ptr_emit_initial_checks(
    MirStream *out, const struct MirPtrConditionPlan *plan)
{
    int string = 0;

    mir_ptr_local_pointer_array(out, MIR_PTR_LWPP, 0, 2);
    mir_ptr_wrapper_node_leaf(out, 1, 2);
    mir_machine_emit_hl_offset(out, 2 + 3 * 2, 0);
    mir_ptr_word_equal_check(
        out, plan, 3143, plan->fail_strings[string++]);

    mir_ptr_local_pointer_array(out, MIR_PTR_LWPP, 1, 2);
    mir_machine_emit_hl_offset(out, 310, 0);
    mir_ptr_load_pointer(out);
    mir_ptr_wrapper_node_leaf(out, 1, 0);
    mir_ptr_word_equal_check(
        out, plan, 4101, plan->fail_strings[string++]);

    mir_ptr_global_pointer_array(out, plan, MIR_PTR_GWPP, 0, 2);
    mir_machine_emit_hl_offset(out, 105, 0);
    mir_ptr_load_pointer(out);
    mir_machine_emit_hl_offset(out, 2 + 2 * 2, 0);
    mir_ptr_word_equal_check(
        out, plan, 1032, plan->fail_strings[string++]);

    mir_ptr_global_pointer_array(out, plan, MIR_PTR_GWPP, 1, 2);
    mir_machine_emit_hl_offset(out, 155 + 107, 0);
    mir_ptr_load_pointer(out);
    mir_machine_emit_hl_offset(out, 2 * 2, 0);
    mir_ptr_word_equal_check(
        out, plan, 2312, plan->fail_strings[string++]);

    mir_ptr_ix_address(out, MIR_PTR_LWP);
    mir_ptr_call_picker(out, plan->pick_wrapper_function, 0);
    mir_machine_emit_hl_offset(out, 318 + 3 * 2, 0);
    mir_ptr_load_pointer(out);
    mir_ptr_word_equal_check(
        out, plan, 3123, plan->fail_strings[string++]);

    mir_ptr_global_address(out, plan->globals[MIR_PTR_GWP], 0);
    mir_ptr_call_picker(out, plan->pick_wrapper_function, 1);
    mir_ptr_call_picker(out, plan->pick_node_function, 0);
    mir_machine_emit_hl_offset(out, 2 * 35 + 2 + 2, 0);
    mir_ptr_word_equal_check(
        out, plan, 2041, plan->fail_strings[string++]);

    mir_ptr_local_wrapper(out, 1);
    mir_machine_emit_hl_offset(out, 155, 0);
    mir_ptr_call_picker(out, plan->pick_leaf_function, 2);
    mir_ptr_word_equal_check(
        out, plan, 4121, plan->fail_strings[string++]);

    mir_ptr_local_wrapper(out, 0);
    mir_machine_emit_hl_offset(out, 113 + 6 + 4, 0);
    mir_ptr_word_equal_check(
        out, plan, 3212, plan->fail_strings[string++]);

    mir_ptr_global_address(out, plan->globals[MIR_PTR_GI], 5 * 2);
    mir_ptr_word_equal_check(
        out, plan, 5005, plan->fail_strings[string++]);

    mir_ptr_ix_address(out, MIR_PTR_LI + 5 * 2);
    mir_ptr_word_equal_check(
        out, plan, 7005, plan->fail_strings[string++]);

    mir_ptr_local_pointer_array(out, MIR_PTR_LWPP, 0, 2);
    mir_ptr_wrapper_node_leaf(out, 1, 2);
    mir_machine_emit_hl_offset(out, 11 + 3, 0);
    mir_ptr_char_equal_check(
        out, plan, 119, plan->fail_strings[string++]);

    mir_ptr_local_pointer_array(out, MIR_PTR_LWPP, 1, 2);
    mir_machine_emit_hl_offset(out, 310, 0);
    mir_ptr_load_pointer(out);
    mir_ptr_wrapper_node_leaf(out, 1, 0);
    mir_machine_emit_hl_offset(out, 10, 0);
    mir_ptr_char_equal_check(
        out, plan, 54, plan->fail_strings[string++]);

    mir_ptr_global_pointer_array(out, plan, MIR_PTR_GWPP, 0, 2);
    mir_machine_emit_hl_offset(out, 105, 0);
    mir_ptr_load_pointer(out);
    mir_machine_emit_hl_offset(out, 11 + 2, 0);
    mir_ptr_char_equal_check(
        out, plan, 13, plan->fail_strings[string++]);

    mir_ptr_global_pointer_array(out, plan, MIR_PTR_GWPP, 1, 2);
    mir_machine_emit_hl_offset(out, 155 + 109, 0);
    mir_ptr_load_pointer(out);
    mir_machine_emit_hl_offset(out, 2, 0);
    mir_ptr_char_equal_check(
        out, plan, 39, plan->fail_strings[string++]);

    mir_ptr_ix_address(out, MIR_PTR_LWP);
    mir_ptr_call_picker(out, plan->pick_wrapper_function, 0);
    mir_machine_emit_hl_offset(out, 326 + 3 * 2, 0);
    mir_ptr_load_pointer(out);
    mir_ptr_char_equal_check(
        out, plan, 109, plan->fail_strings[string++]);

    mir_ptr_global_address(out, plan->globals[MIR_PTR_GWP], 0);
    mir_ptr_call_picker(out, plan->pick_wrapper_function, 1);
    mir_ptr_call_picker(out, plan->pick_node_function, 0);
    mir_machine_emit_hl_offset(out, 2 * 35 + 11 + 1, 0);
    mir_ptr_char_equal_check(
        out, plan, 121, plan->fail_strings[string++]);

    mir_ptr_local_wrapper(out, 1);
    mir_machine_emit_hl_offset(out, 155, 0);
    mir_ptr_call_picker(out, plan->pick_leaf_function, 2);
    mir_machine_emit_hl_offset(out, 10, 0);
    mir_ptr_char_equal_check(
        out, plan, 60, plan->fail_strings[string++]);

    mir_ptr_local_wrapper(out, 0);
    mir_machine_emit_hl_offset(out, 125 + 3 + 2, 0);
    mir_ptr_char_equal_check(
        out, plan, 123, plan->fail_strings[string++]);

    mir_ptr_global_address(out, plan->globals[MIR_PTR_GC], 5);
    mir_ptr_char_equal_check(
        out, plan, 75, plan->fail_strings[string++]);

    mir_ptr_ix_address(out, MIR_PTR_LC + 5);
    mir_ptr_char_equal_check(
        out, plan, 85, plan->fail_strings[string++]);

    mir_ptr_local_pointer_array(out, MIR_PTR_LWPP, 0, 2);
    mir_ptr_wrapper_node_leaf(out, 1, 2);
    mir_machine_emit_hl_offset(out, 19 + 3 * 4, 0);
    mir_ptr_long_equal_check(
        out, plan, 3000163UL, plan->fail_strings[string++]);

    mir_ptr_local_pointer_array(out, MIR_PTR_LWPP, 1, 2);
    mir_machine_emit_hl_offset(out, 310, 0);
    mir_ptr_load_pointer(out);
    mir_ptr_wrapper_node_leaf(out, 1, 0);
    mir_machine_emit_hl_offset(out, 15, 0);
    mir_ptr_long_equal_check(
        out, plan, 4000103UL, plan->fail_strings[string++]);

    mir_ptr_global_pointer_array(out, plan, MIR_PTR_GWPP, 0, 2);
    mir_machine_emit_hl_offset(out, 105, 0);
    mir_ptr_load_pointer(out);
    mir_machine_emit_hl_offset(out, 19 + 2 * 4, 0);
    mir_ptr_long_equal_check(
        out, plan, 1000052UL, plan->fail_strings[string++]);

    mir_ptr_global_pointer_array(out, plan, MIR_PTR_GWPP, 1, 2);
    mir_machine_emit_hl_offset(out, 155 + 111, 0);
    mir_ptr_load_pointer(out);
    mir_machine_emit_hl_offset(out, 2 * 4, 0);
    mir_ptr_long_equal_check(
        out, plan, 2000412UL, plan->fail_strings[string++]);

    mir_ptr_ix_address(out, MIR_PTR_LWP);
    mir_ptr_call_picker(out, plan->pick_wrapper_function, 0);
    mir_machine_emit_hl_offset(out, 334 + 3 * 2, 0);
    mir_ptr_load_pointer(out);
    mir_ptr_long_equal_check(
        out, plan, 3000143UL, plan->fail_strings[string++]);

    mir_ptr_global_address(out, plan->globals[MIR_PTR_GWP], 0);
    mir_ptr_call_picker(out, plan->pick_wrapper_function, 1);
    mir_ptr_call_picker(out, plan->pick_node_function, 0);
    mir_machine_emit_hl_offset(out, 2 * 35 + 19 + 4, 0);
    mir_ptr_long_equal_check(
        out, plan, 2000061UL, plan->fail_strings[string++]);

    mir_ptr_local_wrapper(out, 1);
    mir_machine_emit_hl_offset(out, 155, 0);
    mir_ptr_call_picker(out, plan->pick_leaf_function, 2);
    mir_machine_emit_hl_offset(out, 15, 0);
    mir_ptr_long_equal_check(
        out, plan, 4000123UL, plan->fail_strings[string++]);

    mir_ptr_local_wrapper(out, 0);
    mir_machine_emit_hl_offset(out, 131 + 3 * 4 + 2 * 4, 0);
    mir_ptr_long_equal_check(
        out, plan, 3000312UL, plan->fail_strings[string++]);

    mir_ptr_global_address(out, plan->globals[MIR_PTR_GL], 5 * 4);
    mir_ptr_long_equal_check(
        out, plan, 600005UL, plan->fail_strings[string++]);

    mir_ptr_ix_address(out, MIR_PTR_LL + 5 * 4);
    mir_ptr_long_equal_check(
        out, plan, 800005UL, plan->fail_strings[string++]);
}

static void mir_ptr_word_eq_jump(
    MirStream *out, int expected, int label, int jump_if_equal)
{
    mir_ptr_load_word(out);
    mir_stream_printf(out,
            "\tld de,%d\n\tor a\n\tsbc hl,de\n"
            "\tjp %s,L%d\n",
            expected, jump_if_equal ? "z" : "nz", label);
}

static void mir_ptr_char_eq_jump(
    MirStream *out, int expected, int label, int jump_if_equal)
{
    mir_stream_printf(out,
            "\tld a,(hl)\n\tcp %d\n\tjp %s,L%d\n",
            expected & 255, jump_if_equal ? "z" : "nz", label);
}

static void mir_ptr_long_eq_jump(
    MirStream *out, unsigned long expected, int label,
    int jump_if_equal)
{
    int mismatch = jump_if_equal ? new_label() : label;
    int byte;

    for (byte = 0; byte < 4; ++byte) {
        mir_stream_printf(out,
                "\tld a,(hl)\n\tcp %lu\n\tjp nz,L%d\n",
                (expected >> (byte * 8)) & 255UL, mismatch);
        if (byte != 3)
            mir_stream_puts("\tinc hl\n", out);
    }
    if (jump_if_equal) {
        mir_stream_printf(out, "\tjp L%d\nL%d:\n", label, mismatch);
    }
}

static void mir_ptr_finish_boolean_check(
    MirStream *out, const struct MirPtrConditionPlan *plan,
    int failed, int done, int string)
{
    mir_ptr_increment_count(out);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", done, failed);
    mir_ptr_fail(out, plan, string);
    mir_stream_printf(out, "L%d:\n", done);
}

static void mir_ptr_emit_logical_checks(
    MirStream *out, const struct MirPtrConditionPlan *plan)
{
    int failed;
    int done;
    int next;

    failed = new_label();
    done = new_label();
    mir_ptr_ix_address(out, MIR_PTR_WP);
    mir_ptr_load_pointer(out);
    mir_ptr_wrapper_node_leaf(out, 1, 1);
    mir_machine_emit_hl_offset(out, 2 + 3 * 2, 0);
    mir_ptr_word_eq_jump(out, 3133, failed, 0);
    mir_ptr_ix_address(out, MIR_PTR_WP);
    mir_ptr_load_pointer(out);
    mir_ptr_wrapper_node_leaf(out, 1, 1);
    mir_machine_emit_hl_offset(out, 19 + 3 * 4, 0);
    mir_ptr_long_eq_jump(out, 3000153UL, failed, 0);
    mir_ptr_finish_boolean_check(
        out, plan, failed, done, plan->fail_strings[30]);

    failed = new_label();
    done = new_label();
    next = new_label();
    mir_ptr_ix_address(out, MIR_PTR_NP);
    mir_ptr_load_pointer(out);
    mir_machine_emit_hl_offset(out, 2 * 35 + 11 + 2, 0);
    mir_ptr_char_eq_jump(out, 98, next, 1);
    mir_ptr_ix_address(out, MIR_PTR_NP);
    mir_ptr_load_pointer(out);
    mir_machine_emit_hl_offset(out, 2 * 35 + 11 + 2, 0);
    mir_ptr_char_eq_jump(out, 0, failed, 0);
    mir_stream_printf(out, "L%d:\n", next);
    mir_ptr_finish_boolean_check(
        out, plan, failed, done, plan->fail_strings[31]);

    mir_ptr_ix_address(out, MIR_PTR_LP);
    mir_ptr_load_pointer(out);
    mir_machine_emit_hl_offset(out, 19 + 4, 0);
    mir_ptr_long_equal_check(
        out, plan, 3000041UL, plan->fail_strings[32]);

    failed = new_label();
    done = new_label();
    mir_ptr_global_address(out, plan->globals[MIR_PTR_GI], 5 * 2);
    mir_ptr_word_eq_jump(out, 5005, failed, 0);
    mir_ptr_global_address(out, plan->globals[MIR_PTR_GC], 5);
    mir_ptr_char_eq_jump(out, 75, failed, 0);
    mir_ptr_global_address(out, plan->globals[MIR_PTR_GL], 5 * 4);
    mir_ptr_long_eq_jump(out, 600005UL, failed, 0);
    mir_ptr_finish_boolean_check(
        out, plan, failed, done, plan->fail_strings[33]);

    failed = new_label();
    done = new_label();
    mir_ptr_ix_address(out, MIR_PTR_LI + 5 * 2);
    mir_ptr_word_eq_jump(out, 7005, failed, 0);
    mir_ptr_ix_address(out, MIR_PTR_LC + 5);
    mir_ptr_char_eq_jump(out, 85, failed, 0);
    mir_ptr_ix_address(out, MIR_PTR_LL + 5 * 4);
    mir_ptr_long_eq_jump(out, 800005UL, failed, 0);
    mir_ptr_finish_boolean_check(
        out, plan, failed, done, plan->fail_strings[34]);

    mir_ptr_global_pointer_array(out, plan, MIR_PTR_GWPP, 0, 2);
    mir_machine_emit_hl_offset(out, 310, 0);
    mir_ptr_load_pointer(out);
    mir_machine_emit_hl_offset(out, 2 * 35 + 2, 0);
    mir_ptr_word_equal_check(
        out, plan, 1040, plan->fail_strings[35]);

    mir_ptr_global_pointer_array(out, plan, MIR_PTR_GWPP, 0, 2);
    mir_machine_emit_hl_offset(out, 310, 0);
    mir_ptr_load_pointer(out);
    mir_machine_emit_hl_offset(out, 2 * 35 + 11, 0);
    mir_ptr_char_equal_check(
        out, plan, 16, plan->fail_strings[36]);

    mir_ptr_global_pointer_array(out, plan, MIR_PTR_GWPP, 0, 2);
    mir_machine_emit_hl_offset(out, 310, 0);
    mir_ptr_load_pointer(out);
    mir_machine_emit_hl_offset(out, 2 * 35 + 19, 0);
    mir_ptr_long_equal_check(
        out, plan, 1000060UL, plan->fail_strings[37]);

    mir_ptr_local_pointer_array(out, MIR_PTR_LWPP, 0, 2);
    mir_ptr_wrapper_node_leaf(out, 1, 2);
    mir_machine_emit_hl_offset(out, 2 + 3 * 2, 0);
    mir_ptr_word_equal_check(
        out, plan, 3143, plan->fail_strings[38]);

    failed = new_label();
    done = new_label();
    mir_ptr_global_address(out, plan->globals[MIR_PTR_GWP], 0);
    mir_ptr_call_picker(out, plan->pick_wrapper_function, 1);
    mir_ptr_call_picker(out, plan->pick_node_function, 0);
    mir_machine_emit_hl_offset(out, 2 * 35 + 11 + 1, 0);
    mir_stream_puts("\tbit 7,(hl)\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", failed);
    mir_ptr_finish_boolean_check(
        out, plan, failed, done, plan->fail_strings[39]);

    mir_ptr_local_wrapper(out, 1);
    mir_machine_emit_hl_offset(out, 155, 0);
    mir_ptr_call_picker(out, plan->pick_leaf_function, 2);
    mir_machine_emit_hl_offset(out, 15, 0);
    mir_ptr_long_equal_check(
        out, plan, 4000123UL, plan->fail_strings[40]);
}

static void mir_ptr_store_pointer(MirStream *out)
{
    mir_stream_puts("\tpop de\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n", out);
}

static void mir_ptr_emit_init_call(
    MirStream *out, const struct MirPtrConditionPlan *plan,
    int base, int local, int index)
{
    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n", base);
    if (local)
        mir_ptr_local_wrapper(out, index);
    else
        mir_ptr_global_address(
            out, plan->globals[MIR_PTR_GW], index * 342);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->init_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
}

static void mir_ptr_emit_array_initialization(
    MirStream *out, const struct MirPtrConditionPlan *plan)
{
    int loop = new_label();
    int done = new_label();
    int incremented = new_label();

    mir_ptr_store_word_slot(out, MIR_PTR_I, 0);
    mir_stream_printf(out, "L%d:\n", loop);
    mir_stream_puts("\tld a,(ix-2)\n\tcp 8\n", out);
    mir_stream_printf(out, "\tjp nc,L%d\n", done);

    mir_stream_puts("\tld l,(ix-2)\n\tld h,0\n"
          "\tld de,5000\n\tadd hl,de\n\tex de,hl\n"
          "\tld l,(ix-2)\n\tld h,0\n\tadd hl,hl\n", out);
    mir_stream_printf(out, "\tld bc,%s\n\tadd hl,bc\n",
            asm_name_for(sym_asm_name(plan->globals[MIR_PTR_GI])));
    mir_stream_puts("\tld (hl),e\n\tinc hl\n\tld (hl),d\n", out);

    mir_stream_puts("\tld a,(ix-2)\n\tadd a,70\n\tld e,a\n"
          "\tld l,(ix-2)\n\tld h,0\n", out);
    mir_stream_printf(out, "\tld bc,%s\n\tadd hl,bc\n",
            asm_name_for(sym_asm_name(plan->globals[MIR_PTR_GC])));
    mir_stream_puts("\tld (hl),e\n", out);

    mir_stream_puts("\tld l,(ix-2)\n\tld h,0\n\tld e,l\n\tld d,h\n"
          "\tadd hl,hl\n\tadd hl,hl\n", out);
    mir_stream_printf(out, "\tld bc,%s\n\tadd hl,bc\n",
            asm_name_for(sym_asm_name(plan->globals[MIR_PTR_GL])));
    mir_stream_puts("\tld a,e\n\tadd a,192\n\tld (hl),a\n\tinc hl\n"
          "\tld a,d\n\tadc a,39\n\tld (hl),a\n\tinc hl\n"
          "\tld (hl),9\n\tinc hl\n\tld (hl),0\n", out);

    mir_stream_puts("\tld l,(ix-2)\n\tld h,0\n"
          "\tld de,7000\n\tadd hl,de\n\tex de,hl\n"
          "\tld l,(ix-2)\n\tld h,0\n\tadd hl,hl\n"
          "\tld bc,-38\n\tadd hl,bc\n\tpush ix\n\tpop bc\n"
          "\tadd hl,bc\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n", out);

    mir_stream_puts("\tld a,(ix-2)\n\tadd a,80\n\tld e,a\n"
          "\tld l,(ix-2)\n\tld h,0\n\tld bc,-46\n"
          "\tadd hl,bc\n\tpush ix\n\tpop bc\n\tadd hl,bc\n"
          "\tld (hl),e\n", out);

    mir_stream_puts("\tld l,(ix-2)\n\tld h,0\n\tld e,l\n\tld d,h\n"
          "\tadd hl,hl\n\tadd hl,hl\n\tld bc,-78\n"
          "\tadd hl,bc\n\tpush ix\n\tpop bc\n\tadd hl,bc\n"
          "\tld a,e\n\tadd a,0\n\tld (hl),a\n\tinc hl\n"
          "\tld a,d\n\tadc a,53\n\tld (hl),a\n\tinc hl\n"
          "\tld (hl),12\n\tinc hl\n\tld (hl),0\n", out);

    mir_stream_printf(out,
            "\tinc (ix%+d)\n\tjp nz,L%d\n"
            "\tinc (ix%+d)\nL%d:\n\tjp L%d\nL%d:\n",
            MIR_PTR_I, incremented, MIR_PTR_I + 1,
            incremented, loop, done);
}

static void mir_ptr_emit_setup(
    MirStream *out, const struct MirPtrConditionPlan *plan)
{
    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-762\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", plan->start_string);
    mir_emit_runtime_call(out, plan->print_names[0]);
    mir_stream_puts("\tpop bc\n", out);

    mir_ptr_emit_init_call(out, plan, 1000, 0, 0);
    mir_ptr_emit_init_call(out, plan, 2000, 0, 1);
    mir_ptr_emit_init_call(out, plan, 3000, 1, 0);
    mir_ptr_emit_init_call(out, plan, 4000, 1, 1);

    mir_ptr_global_address(out, plan->globals[MIR_PTR_GW], 0);
    mir_stream_puts("\tpush hl\n", out);
    mir_ptr_global_address(out, plan->globals[MIR_PTR_GWP], 0);
    mir_ptr_store_pointer(out);
    mir_ptr_global_address(out, plan->globals[MIR_PTR_GW], 342);
    mir_stream_puts("\tpush hl\n", out);
    mir_ptr_global_address(out, plan->globals[MIR_PTR_GWP], 2);
    mir_ptr_store_pointer(out);
    mir_ptr_global_address(out, plan->globals[MIR_PTR_GWP], 0);
    mir_stream_puts("\tpush hl\n", out);
    mir_ptr_global_address(out, plan->globals[MIR_PTR_GWPP], 0);
    mir_ptr_store_pointer(out);
    mir_ptr_global_address(out, plan->globals[MIR_PTR_GWP], 2);
    mir_stream_puts("\tpush hl\n", out);
    mir_ptr_global_address(out, plan->globals[MIR_PTR_GWPP], 2);
    mir_ptr_store_pointer(out);

    mir_ptr_local_wrapper(out, 0);
    mir_stream_puts("\tpush hl\n", out);
    mir_ptr_ix_address(out, MIR_PTR_LWP);
    mir_ptr_store_pointer(out);
    mir_ptr_local_wrapper(out, 1);
    mir_stream_puts("\tpush hl\n", out);
    mir_ptr_ix_address(out, MIR_PTR_LWP + 2);
    mir_ptr_store_pointer(out);
    mir_ptr_ix_address(out, MIR_PTR_LWP);
    mir_stream_puts("\tpush hl\n", out);
    mir_ptr_ix_address(out, MIR_PTR_LWPP);
    mir_ptr_store_pointer(out);
    mir_ptr_ix_address(out, MIR_PTR_LWP + 2);
    mir_stream_puts("\tpush hl\n", out);
    mir_ptr_ix_address(out, MIR_PTR_LWPP + 2);
    mir_ptr_store_pointer(out);

    mir_ptr_emit_array_initialization(out, plan);

    mir_ptr_local_wrapper(out, 0);
    mir_stream_puts("\tld (ix-10),l\n\tld (ix-9),h\n", out);
    mir_ptr_local_wrapper(out, 0);
    mir_stream_puts("\tld (ix-12),l\n\tld (ix-11),h\n", out);
    mir_ptr_local_wrapper(out, 0);
    mir_stream_puts("\tld (ix-14),l\n\tld (ix-13),h\n", out);
    mir_ptr_store_word_slot(out, MIR_PTR_COUNT, 0);
}

static void mir_ptr_add_i_stride(MirStream *out, int stride)
{
    int loop = new_label();
    int done = new_label();

    mir_stream_puts("\tld a,(ix-2)\n\tor a\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n\tld b,a\n\tld de,%d\nL%d:\n"
                 "\tadd hl,de\n\tdjnz L%d\nL%d:\n",
            done, stride, loop, loop, done);
}

static void mir_ptr_local_wrapper_i1(MirStream *out)
{
    int even = new_label();

    mir_ptr_local_wrapper(out, 0);
    mir_stream_puts("\tld a,(ix-2)\n\tand 1\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", even);
    mir_machine_emit_hl_offset(out, 342, 0);
    mir_stream_printf(out, "L%d:\n", even);
}

static void mir_ptr_local_lwp_i1(MirStream *out)
{
    int even = new_label();

    mir_ptr_ix_address(out, MIR_PTR_LWP);
    mir_stream_puts("\tld a,(ix-2)\n\tand 1\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n\tinc hl\n\tinc hl\nL%d:\n",
            even, even);
    mir_ptr_load_pointer(out);
}

static void mir_ptr_global_gwpp_i1(
    MirStream *out, const struct MirPtrConditionPlan *plan)
{
    int even = new_label();

    mir_ptr_global_address(out, plan->globals[MIR_PTR_GWPP], 0);
    mir_stream_puts("\tld a,(ix-2)\n\tand 1\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n\tinc hl\n\tinc hl\nL%d:\n",
            even, even);
    mir_ptr_load_pointer(out);
    mir_ptr_load_pointer(out);
}

static void mir_ptr_increment_slot(MirStream *out, int slot)
{
    int done = new_label();

    mir_stream_printf(out,
            "\tinc (ix%+d)\n\tjp nz,L%d\n"
            "\tinc (ix%+d)\nL%d:\n",
            slot, done, slot + 1, done);
}

static void mir_ptr_add_slot(MirStream *out, int target, int source)
{
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n"
            "\tadd hl,de\n\tld (ix%+d),l\n\tld (ix%+d),h\n",
            target, target + 1, source, source + 1,
            target, target + 1);
}

static void mir_ptr_guard(
    MirStream *out, const struct MirPtrConditionPlan *plan,
    int fail_string, int break_label)
{
    int okay = new_label();

    mir_ptr_increment_slot(out, MIR_PTR_GUARD);
    mir_stream_puts("\tld a,(ix-7)\n\tor a\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n\tld a,(ix-8)\n\tcp 11\n"
                 "\tjp c,L%d\n",
            break_label, okay);
    mir_ptr_fail(out, plan, fail_string);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", break_label, okay);
}

static void mir_ptr_emit_while_loops(
    MirStream *out, const struct MirPtrConditionPlan *plan)
{
    int loop;
    int done;
    int next;

    mir_ptr_store_word_slot(out, MIR_PTR_I, 0);
    mir_ptr_store_word_slot(out, MIR_PTR_SUM, 0);
    mir_ptr_store_word_slot(out, MIR_PTR_GUARD, 0);
    loop = new_label();
    done = new_label();
    next = new_label();
    mir_stream_printf(out, "L%d:\n\tld a,(ix-1)\n\tor a\n"
                 "\tjp nz,L%d\n\tld a,(ix-2)\n\tcp 4\n"
                 "\tjp nc,L%d\n",
            loop, done, done);
    mir_ptr_local_wrapper_i1(out);
    mir_stream_puts("\tld a,(ix-2)\n\tand 3\n\tld e,a\n\tld d,0\n"
          "\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->pick_int_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_ptr_load_word(out);
    mir_stream_puts("\tld a,h\n\txor 128\n\tcp 139\n", out);
    mir_stream_printf(out, "\tjp c,L%d\n\tjp nz,L%d\n"
                 "\tld a,l\n\tcp 184\n\tjp c,L%d\nL%d:\n",
            done, next, done, next);
    mir_ptr_local_wrapper_i1(out);
    mir_stream_puts("\tld a,(ix-2)\n\tand 3\n\tld e,a\n\tld d,0\n"
          "\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->pick_long_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_stream_puts("\tinc hl\n\tinc hl\n\tinc hl\n\tld a,(hl)\n"
          "\tbit 7,a\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n\tcp 0\n\tjp c,L%d\n"
                 "\tjp nz,L%d\n\tdec hl\n\tld a,(hl)\n\tcp 45\n"
                 "\tjp c,L%d\n\tjp nz,L%d\n\tdec hl\n"
                 "\tld a,(hl)\n\tcp 198\n\tjp c,L%d\n"
                 "\tjp nz,L%d\n\tdec hl\n\tld a,(hl)\n\tcp 192\n"
                 "\tjp c,L%d\n",
            done, done, next, done, next, done, next, done);
    mir_stream_printf(out, "L%d:\n", next);
    mir_ptr_add_slot(out, MIR_PTR_SUM, MIR_PTR_I);
    mir_ptr_increment_slot(out, MIR_PTR_I);
    mir_ptr_guard(
        out, plan, plan->fail_strings[41], done);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", loop, done);
    mir_ptr_check_int(
        out, plan, plan->check_strings[1],
        MIR_PTR_I, 4);
    mir_ptr_check_int(
        out, plan, plan->check_strings[2],
        MIR_PTR_SUM, 6);

    mir_ptr_store_word_slot(out, MIR_PTR_I, 0);
    mir_ptr_store_word_slot(out, MIR_PTR_SUM, 0);
    mir_ptr_store_word_slot(out, MIR_PTR_GUARD, 0);
    loop = new_label();
    done = new_label();
    next = new_label();
    mir_stream_printf(out, "L%d:\n\tld a,(ix-1)\n\tor a\n"
                 "\tjp nz,L%d\n\tld a,(ix-2)\n\tcp 3\n"
                 "\tjp nc,L%d\n",
            loop, done, done);
    mir_ptr_local_lwp_i1(out);
    mir_stream_puts("\tld a,(ix-2)\n\tand 1\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", next);
    mir_machine_emit_hl_offset(out, 155, 0);
    mir_stream_printf(out, "L%d:\n", next);
    mir_ptr_add_i_stride(out, 35);
    mir_machine_emit_hl_offset(out, 11, 0);
    mir_ptr_add_i_stride(out, 1);
    mir_stream_puts("\tld a,(hl)\n\tor a\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", done);
    mir_ptr_local_lwp_i1(out);
    mir_stream_puts("\tld a,(ix-2)\n\tand 1\n", out);
    next = new_label();
    mir_stream_printf(out, "\tjp z,L%d\n", next);
    mir_machine_emit_hl_offset(out, 155, 0);
    mir_stream_printf(out, "L%d:\n", next);
    mir_ptr_add_i_stride(out, 35);
    mir_machine_emit_hl_offset(out, 19, 0);
    mir_ptr_add_i_stride(out, 4);
    mir_stream_puts("\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tinc hl\n"
          "\tor (hl)\n\tinc hl\n\tor (hl)\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n\tbit 7,(hl)\n\tjp nz,L%d\n",
            done, done);
    mir_ptr_local_lwp_i1(out);
    mir_stream_puts("\tld a,(ix-2)\n\tand 1\n", out);
    next = new_label();
    mir_stream_printf(out, "\tjp z,L%d\n", next);
    mir_machine_emit_hl_offset(out, 155, 0);
    mir_stream_printf(out, "L%d:\n", next);
    mir_ptr_add_i_stride(out, 35);
    mir_machine_emit_hl_offset(out, 11, 0);
    mir_ptr_add_i_stride(out, 1);
    mir_stream_puts("\tld a,(hl)\n\tld l,a\n\trla\n\tsbc a,a\n\tld h,a\n"
          "\tld e,(ix-6)\n\tld d,(ix-5)\n\tadd hl,de\n"
          "\tld (ix-6),l\n\tld (ix-5),h\n", out);
    mir_ptr_increment_slot(out, MIR_PTR_I);
    mir_ptr_guard(
        out, plan, plan->fail_strings[42], done);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", loop, done);
    mir_ptr_check_int(
        out, plan, plan->check_strings[3],
        MIR_PTR_I, 3);
    mir_ptr_check_int(
        out, plan, plan->check_strings[4],
        MIR_PTR_SUM, 272);

    mir_ptr_store_word_slot(out, MIR_PTR_I, 0);
    mir_ptr_store_word_slot(out, MIR_PTR_GUARD, 0);
    loop = new_label();
    done = new_label();
    next = new_label();
    mir_stream_printf(out, "L%d:\n\tld a,(ix-1)\n\tor a\n"
                 "\tjp nz,L%d\n\tld a,(ix-2)\n\tcp 2\n"
                 "\tjp c,L%d\n",
            loop, done, next);
    mir_ptr_local_pointer_array(out, MIR_PTR_LWPP, 0, 2);
    mir_ptr_wrapper_node_leaf(out, 1, 2);
    mir_machine_emit_hl_offset(out, 2 + 3 * 2, 0);
    mir_ptr_word_eq_jump(out, 3143, done, 1);
    mir_stream_printf(out, "L%d:\n", next);
    mir_ptr_increment_slot(out, MIR_PTR_I);
    mir_ptr_guard(
        out, plan, plan->fail_strings[43], done);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", loop, done);
    mir_ptr_check_int(
        out, plan, plan->check_strings[5],
        MIR_PTR_I, 2);
}

static void mir_ptr_emit_do_loops(
    MirStream *out, const struct MirPtrConditionPlan *plan)
{
    int loop;
    int done;
    int next;

    mir_ptr_store_word_slot(out, MIR_PTR_I, 0);
    mir_ptr_store_word_slot(out, MIR_PTR_SUM, 0);
    mir_ptr_store_word_slot(out, MIR_PTR_GUARD, 0);
    loop = new_label();
    done = new_label();
    next = new_label();
    mir_stream_printf(out, "L%d:\n", loop);
    mir_ptr_add_slot(out, MIR_PTR_SUM, MIR_PTR_I);
    mir_ptr_increment_slot(out, MIR_PTR_I);
    mir_ptr_guard(
        out, plan, plan->fail_strings[44], done);
    mir_stream_puts("\tld a,(ix-1)\n\tor a\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n\tld a,(ix-2)\n\tcp 4\n"
                 "\tjp nc,L%d\n",
            done, done);
    mir_ptr_global_gwpp_i1(out, plan);
    mir_stream_puts("\tld a,(ix-2)\n\tand 1\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", next);
    mir_machine_emit_hl_offset(out, 155, 0);
    mir_stream_printf(out, "L%d:\n", next);
    mir_machine_emit_hl_offset(out, 105, 0);
    mir_ptr_load_pointer(out);
    mir_stream_puts("\tld a,(ix-2)\n\tand 3\n\tadd a,a\n\tld e,a\n"
          "\tld d,0\n\tadd hl,de\n", out);
    mir_ptr_load_word(out);
    mir_stream_puts("\tbit 7,h\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n\tld a,h\n\tor l\n"
                 "\tjp z,L%d\n\tjp L%d\nL%d:\n",
            done, done, loop, done);
    mir_ptr_check_int(
        out, plan, plan->check_strings[6],
        MIR_PTR_I, 4);
    mir_ptr_check_int(
        out, plan, plan->check_strings[7],
        MIR_PTR_SUM, 6);

    mir_ptr_store_word_slot(out, MIR_PTR_I, 0);
    mir_ptr_store_word_slot(out, MIR_PTR_SUM, 0);
    mir_ptr_store_word_slot(out, MIR_PTR_GUARD, 0);
    loop = new_label();
    done = new_label();
    next = new_label();
    mir_stream_printf(out, "L%d:\n", loop);
    mir_ptr_local_pointer_array(out, MIR_PTR_LWPP, 0, 0);
    mir_stream_puts("\tld a,(ix-2)\n\tand 1\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n\tinc hl\n\tinc hl\nL%d:\n",
            next, next);
    mir_ptr_load_pointer(out);
    mir_ptr_load_pointer(out);
    mir_stream_puts("\tld a,(ix-2)\n\tand 1\n", out);
    next = new_label();
    mir_stream_printf(out, "\tjp z,L%d\n", next);
    mir_machine_emit_hl_offset(out, 155, 0);
    mir_stream_printf(out, "L%d:\n", next);
    mir_machine_emit_hl_offset(out, 109, 0);
    mir_ptr_load_pointer(out);
    mir_stream_puts("\tld a,(ix-2)\n\tld e,a\n\tld d,0\n\tadd hl,de\n"
          "\tld a,(hl)\n\tld l,a\n\trla\n\tsbc a,a\n\tld h,a\n"
          "\tld e,(ix-6)\n\tld d,(ix-5)\n\tadd hl,de\n"
          "\tld (ix-6),l\n\tld (ix-5),h\n", out);
    mir_ptr_increment_slot(out, MIR_PTR_I);
    mir_ptr_guard(
        out, plan, plan->fail_strings[45], done);
    mir_stream_puts("\tld a,(ix-1)\n\tor a\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n\tld a,(ix-2)\n\tcp 3\n"
                 "\tjp nc,L%d\n",
            done, done);
    mir_ptr_local_pointer_array(out, MIR_PTR_LWPP, 0, 0);
    mir_stream_puts("\tld a,(ix-2)\n\tand 1\n", out);
    next = new_label();
    mir_stream_printf(out, "\tjp z,L%d\n\tinc hl\n\tinc hl\nL%d:\n",
            next, next);
    mir_ptr_load_pointer(out);
    mir_ptr_load_pointer(out);
    mir_stream_puts("\tld a,(ix-2)\n\tand 1\n", out);
    next = new_label();
    mir_stream_printf(out, "\tjp z,L%d\n", next);
    mir_machine_emit_hl_offset(out, 155, 0);
    mir_stream_printf(out, "L%d:\n", next);
    mir_machine_emit_hl_offset(out, 111, 0);
    mir_ptr_load_pointer(out);
    mir_stream_puts("\tld a,(ix-2)\n\tadd a,a\n\tadd a,a\n"
          "\tld e,a\n\tld d,0\n\tadd hl,de\n"
          "\tinc hl\n\tinc hl\n\tinc hl\n\tld a,(hl)\n"
          "\tbit 7,a\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n\tld b,a\n\tdec hl\n"
                 "\tld a,(hl)\n\tor b\n\tld b,a\n\tdec hl\n"
                 "\tld a,(hl)\n\tor b\n\tld b,a\n\tdec hl\n"
                 "\tld a,(hl)\n\tor b\n\tjp z,L%d\n"
                 "\tjp L%d\nL%d:\n",
            done, done, loop, done);
    mir_ptr_check_int(
        out, plan, plan->check_strings[8],
        MIR_PTR_I, 3);
    mir_ptr_check_int(
        out, plan, plan->check_strings[9],
        MIR_PTR_SUM, 362);
}

static void mir_ptr_emit_for_loops(
    MirStream *out, const struct MirPtrConditionPlan *plan)
{
    int loop;
    int done;
    int next;

    mir_ptr_store_word_slot(out, MIR_PTR_SUM, 0);
    mir_ptr_store_word_slot(out, MIR_PTR_I, 0);
    loop = new_label();
    done = new_label();
    next = new_label();
    mir_stream_printf(out, "L%d:\n\tld a,(ix-1)\n\tor a\n"
                 "\tjp nz,L%d\n\tld a,(ix-2)\n\tcp 4\n"
                 "\tjp nc,L%d\n",
            loop, done, done);
    mir_stream_puts("\tld l,(ix-2)\n\tld h,0\n\tadd hl,hl\n", out);
    mir_stream_printf(out, "\tld de,%s\n\tadd hl,de\n",
            asm_name_for(sym_asm_name(plan->globals[MIR_PTR_GI])));
    mir_ptr_load_word(out);
    mir_stream_puts("\tld a,h\n\txor 128\n\tcp 147\n", out);
    mir_stream_printf(out, "\tjp c,L%d\n\tjp nz,L%d\n"
                 "\tld a,l\n\tcp 136\n\tjp c,L%d\nL%d:\n",
            done, next, done, next);
    mir_stream_puts("\tld l,(ix-2)\n\tld h,0\n\tadd hl,hl\n\tadd hl,hl\n", out);
    mir_stream_printf(out, "\tld de,%s\n\tadd hl,de\n",
            asm_name_for(sym_asm_name(plan->globals[MIR_PTR_GL])));
    mir_stream_puts("\tinc hl\n\tinc hl\n\tinc hl\n\tld a,(hl)\n"
          "\tbit 7,a\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n\tcp 0\n\tjp c,L%d\n"
                 "\tjp nz,L%d\n\tdec hl\n\tld a,(hl)\n\tcp 9\n"
                 "\tjp c,L%d\n\tjp nz,L%d\n\tdec hl\n"
                 "\tld a,(hl)\n\tcp 39\n\tjp c,L%d\n"
                 "\tjp nz,L%d\n\tdec hl\n\tld a,(hl)\n\tcp 202\n"
                 "\tjp nc,L%d\nL%d:\n",
            next, next, done, next, done, next, done, done, next);
    mir_ptr_add_slot(out, MIR_PTR_SUM, MIR_PTR_I);
    mir_ptr_increment_slot(out, MIR_PTR_I);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", loop, done);
    mir_ptr_check_int(
        out, plan, plan->check_strings[10],
        MIR_PTR_I, 4);
    mir_ptr_check_int(
        out, plan, plan->check_strings[11],
        MIR_PTR_SUM, 6);

    mir_ptr_store_word_slot(out, MIR_PTR_SUM, 0);
    mir_ptr_store_word_slot(out, MIR_PTR_I, 0);
    loop = new_label();
    done = new_label();
    next = new_label();
    mir_stream_printf(out, "L%d:\n\tld a,(ix-1)\n\tor a\n"
                 "\tjp nz,L%d\n\tld a,(ix-2)\n\tcp 3\n"
                 "\tjp nc,L%d\n",
            loop, done, done);
    mir_ptr_local_pointer_array(out, MIR_PTR_LWPP, 0, 2);
    mir_ptr_add_i_stride(out, 35);
    mir_ptr_load_word(out);
    mir_stream_puts("\tld a,h\n\txor 128\n\tcp 140\n", out);
    mir_stream_printf(out, "\tjp c,L%d\n\tjp nz,L%d\n"
                 "\tld a,l\n\tcp 28\n\tjp nc,L%d\nL%d:\n",
            next, done, done, next);
    mir_ptr_local_pointer_array(out, MIR_PTR_LWPP, 0, 2);
    mir_ptr_add_i_stride(out, 35);
    mir_machine_emit_hl_offset(out, 2, 0);
    mir_stream_puts("\tld a,(ix-2)\n\tadd a,a\n\tld e,a\n"
          "\tld d,0\n\tadd hl,de\n", out);
    mir_ptr_load_word(out);
    mir_stream_puts("\tld e,(ix-6)\n\tld d,(ix-5)\n\tadd hl,de\n"
          "\tld (ix-6),l\n\tld (ix-5),h\n", out);
    mir_ptr_increment_slot(out, MIR_PTR_I);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", loop, done);
    mir_ptr_check_int(
        out, plan, plan->check_strings[12],
        MIR_PTR_I, 3);
    mir_ptr_check_int(
        out, plan, plan->check_strings[13],
        MIR_PTR_SUM, 9093);

    mir_ptr_store_word_slot(out, MIR_PTR_SUM, 0);
    mir_ptr_store_word_slot(out, MIR_PTR_I, 0);
    loop = new_label();
    done = new_label();
    mir_stream_printf(out, "L%d:\n\tld a,(ix-1)\n\tor a\n"
                 "\tjp nz,L%d\n\tld a,(ix-2)\n\tcp 3\n"
                 "\tjp nc,L%d\n",
            loop, done, done);
    mir_ptr_local_pointer_array(out, MIR_PTR_LWPP, 1, 2);
    mir_machine_emit_hl_offset(out, 155, 0);
    mir_ptr_add_i_stride(out, 35);
    mir_machine_emit_hl_offset(out, 19, 0);
    mir_stream_puts("\tld a,(ix-2)\n\tadd a,a\n\tadd a,a\n"
          "\tld e,a\n\tld d,0\n\tadd hl,de\n"
          "\tinc hl\n\tinc hl\n\tinc hl\n\tld a,(hl)\n"
          "\tbit 7,a\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n\tld b,a\n\tdec hl\n"
                 "\tld a,(hl)\n\tor b\n\tld b,a\n\tdec hl\n"
                 "\tld a,(hl)\n\tor b\n\tld b,a\n\tdec hl\n"
                 "\tld a,(hl)\n\tor b\n\tjp z,L%d\n",
            done, done);
    mir_ptr_local_pointer_array(out, MIR_PTR_LWPP, 1, 2);
    mir_machine_emit_hl_offset(out, 155, 0);
    mir_ptr_add_i_stride(out, 35);
    mir_machine_emit_hl_offset(out, 11, 0);
    mir_ptr_add_i_stride(out, 1);
    mir_stream_puts("\tld a,(hl)\n\tld l,a\n\trla\n\tsbc a,a\n\tld h,a\n"
          "\tld e,(ix-6)\n\tld d,(ix-5)\n\tadd hl,de\n"
          "\tld (ix-6),l\n\tld (ix-5),h\n", out);
    mir_ptr_increment_slot(out, MIR_PTR_I);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", loop, done);
    mir_ptr_check_int(
        out, plan, plan->check_strings[14],
        MIR_PTR_I, 3);
    mir_ptr_check_int(
        out, plan, plan->check_strings[15],
        MIR_PTR_SUM, 264);
}

static void mir_ptr_emit_end_checks(
    MirStream *out, const struct MirPtrConditionPlan *plan)
{
    int alternate;
    int failed;
    int success;
    int done;

    alternate = new_label();
    failed = new_label();
    success = new_label();
    done = new_label();
    mir_ptr_local_pointer_array(out, MIR_PTR_LWPP, 0, 2);
    mir_ptr_wrapper_node_leaf(out, 1, 2);
    mir_machine_emit_hl_offset(out, 2 + 3 * 2, 0);
    mir_ptr_word_eq_jump(out, 3143, alternate, 0);
    mir_ptr_local_pointer_array(out, MIR_PTR_LWPP, 0, 2);
    mir_ptr_wrapper_node_leaf(out, 1, 2);
    mir_machine_emit_hl_offset(out, 19 + 3 * 4, 0);
    mir_ptr_long_eq_jump(out, 3000163UL, failed, 0);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", success, alternate);
    mir_ptr_local_pointer_array(out, MIR_PTR_LWPP, 0, 2);
    mir_ptr_wrapper_node_leaf(out, 1, 2);
    mir_machine_emit_hl_offset(out, 11 + 3, 0);
    mir_ptr_char_eq_jump(out, 0, failed, 0);
    mir_stream_printf(out, "L%d:\n", success);
    mir_ptr_increment_count(out);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", done, failed);
    mir_ptr_fail(out, plan, plan->fail_strings[46]);
    mir_stream_printf(out, "L%d:\n", done);

    alternate = new_label();
    failed = new_label();
    success = new_label();
    done = new_label();
    mir_ptr_local_pointer_array(out, MIR_PTR_LWPP, 1, 2);
    mir_ptr_wrapper_node_leaf(out, 0, 1);
    mir_machine_emit_hl_offset(out, 11 + 2, 0);
    mir_ptr_char_eq_jump(out, 69, alternate, 0);
    mir_ptr_local_pointer_array(out, MIR_PTR_LWPP, 1, 2);
    mir_ptr_wrapper_node_leaf(out, 0, 1);
    mir_machine_emit_hl_offset(out, 2 + 2 * 2, 0);
    mir_ptr_word_eq_jump(out, 4032, failed, 0);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", success, alternate);
    mir_ptr_local_pointer_array(out, MIR_PTR_LWPP, 1, 2);
    mir_ptr_wrapper_node_leaf(out, 0, 1);
    mir_machine_emit_hl_offset(out, 19 + 2 * 4, 0);
    mir_ptr_long_eq_jump(out, 4000052UL, failed, 0);
    mir_stream_printf(out, "L%d:\n", success);
    mir_ptr_increment_count(out);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", done, failed);
    mir_ptr_fail(out, plan, plan->fail_strings[47]);
    mir_stream_printf(out, "L%d:\n", done);

    alternate = new_label();
    failed = new_label();
    success = new_label();
    done = new_label();
    mir_ptr_global_address(out, plan->globals[MIR_PTR_GWP], 0);
    mir_ptr_call_picker(out, plan->pick_wrapper_function, 1);
    mir_machine_emit_hl_offset(out, 155 + 111, 0);
    mir_ptr_load_pointer(out);
    mir_machine_emit_hl_offset(out, 2 * 4, 0);
    mir_ptr_long_eq_jump(out, 2000412UL, alternate, 0);
    mir_ptr_global_address(out, plan->globals[MIR_PTR_GWP], 0);
    mir_ptr_call_picker(out, plan->pick_wrapper_function, 1);
    mir_machine_emit_hl_offset(out, 155 + 109, 0);
    mir_ptr_load_pointer(out);
    mir_machine_emit_hl_offset(out, 2, 0);
    mir_ptr_char_eq_jump(out, 39, alternate, 0);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", success, alternate);
    mir_ptr_global_address(out, plan->globals[MIR_PTR_GWP], 0);
    mir_ptr_call_picker(out, plan->pick_wrapper_function, 1);
    mir_machine_emit_hl_offset(out, 155 + 107, 0);
    mir_ptr_load_pointer(out);
    mir_machine_emit_hl_offset(out, 2 * 2, 0);
    mir_ptr_word_eq_jump(out, 2312, failed, 0);
    mir_ptr_global_address(out, plan->globals[MIR_PTR_GWP], 0);
    mir_ptr_call_picker(out, plan->pick_wrapper_function, 1);
    mir_machine_emit_hl_offset(out, 155 + 105, 0);
    mir_ptr_load_pointer(out);
    mir_machine_emit_hl_offset(out, 15, 0);
    mir_ptr_long_eq_jump(out, 2000113UL, failed, 0);
    mir_stream_printf(out, "L%d:\n", success);
    mir_ptr_increment_count(out);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", done, failed);
    mir_ptr_fail(out, plan, plan->fail_strings[48]);
    mir_stream_printf(out, "L%d:\n", done);
}

static void mir_emit_ptr_condition_main(
    MirStream *out, const struct MirPtrConditionPlan *plan)
{
    int success = new_label();

    mir_ptr_emit_setup(out, plan);
    mir_ptr_emit_initial_checks(out, plan);
    mir_ptr_emit_logical_checks(out, plan);
    mir_ptr_check_int(
        out, plan, plan->check_strings[0],
        MIR_PTR_COUNT, 41);
    mir_ptr_emit_while_loops(out, plan);
    mir_ptr_emit_do_loops(out, plan);
    mir_ptr_emit_for_loops(out, plan);
    mir_ptr_emit_end_checks(out, plan);

    mir_machine_emit_global_word(
        out, plan->globals[MIR_PTR_FAILS], 0);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n\tpush hl\n"
                 "\tld hl,S%d\n\tpush hl\n",
            success, plan->failed_string);
    mir_emit_runtime_call(out, plan->print_names[1]);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld hl,1\n"
          "\tld sp,ix\n\tpop ix\n\tret\n", out);
    mir_stream_printf(out, "L%d:\n\tld hl,S%d\n\tpush hl\n",
            success, plan->pass_string);
    mir_emit_runtime_call(out, plan->print_names[2]);
    mir_stream_puts("\tpop bc\n\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n",
          out);
}

static int mir_ptr_condition_string(int instruction, int *string_out)
{
    const struct MirInsn *insn;

    if (instruction < 0 || instruction >= mir.count)
        return 0;
    insn = &mir.insns[instruction];
    if (insn->opcode != MIR_STRING_ADDRESS ||
        insn->immediate < 0)
        return 0;
    *string_out = (int)insn->immediate;
    return 1;
}

static int mir_ptr_condition_call(
    int instruction, struct Sym **function_out)
{
    return mir_aggregate_direct_function(instruction, function_out);
}

static int mir_match_ptr_condition_main(
    struct MirPtrConditionPlan *plan)
{
    static const short fail_calls[MIR_PTR_CONDITION_FAIL_CALLS] = {
        209, 237, 265, 294, 322, 352, 378, 404, 424, 448,
        478, 507, 536, 564, 592, 623, 650, 677, 696, 717,
        746, 774, 802, 831, 859, 889, 915, 941, 961, 985,
        1034, 1087, 1108, 1161, 1224, 1256, 1289, 1321,
        1344, 1375, 1401, 1499, 1636, 1722, 1771, 1881,
        2249, 2322, 2431
    };
    static const short check_calls[MIR_PTR_CONDITION_CHECK_CALLS] = {
        1415, 1514, 1521, 1651, 1658, 1737, 1821, 1828,
        1932, 1939, 2008, 2015, 2088, 2095, 2171, 2178
    };
    static const short pick_wrapper_calls[] = {
        300, 328, 570, 598, 837, 865, 1356, 2329, 2345,
        2374, 2390
    };
    static const short pick_node_calls[] = {332, 602, 869, 1360};
    static const short pick_leaf_calls[] = {363, 634, 900, 1392};
    static const short init_calls[] = {10, 17, 24, 31};
    struct MirPtrConditionConstant {
        short instruction;
        long value;
    };
    static const struct MirPtrConditionConstant expected_constants[] = {
        {8, 1000}, {15, 2000}, {22, 3000}, {29, 4000},
        {98, 8}, {104, 5000}, {111, 70}, {119, 600000},
        {127, 7000}, {134, 80}, {142, 800000},
        {197, 3143}, {225, 4101}, {253, 1032},
        {282, 2312}, {310, 3123}, {340, 2041},
        {366, 4121}, {392, 3212}, {412, 5005},
        {1413, 41}, {1431, 4}, {1446, 3000},
        {1469, 3000000}, {1512, 4}, {1519, 6},
        {1537, 3}, {1631, 10}, {1649, 3}, {1656, 272},
        {1717, 10}, {1735, 2}, {1766, 10},
        {1819, 4}, {1826, 6}, {1876, 10},
        {1930, 3}, {1937, 362}, {1952, 4},
        {1961, 5000}, {1978, 600010}, {2006, 4},
        {2013, 6}, {2028, 3}, {2044, 3100},
        {2086, 3}, {2093, 9093}, {2108, 3},
        {2126, 0}, {2169, 3}, {2176, 264},
        {2441, 1}, {2448, 0}
    };
    struct Sym *function;
    int arguments[3];
    int type;
    int storage;
    int offset;
    long global_offset;
    int instruction;
    int promoted_variant = 0;
    size_t promoted_cursor = 0;
    size_t binary_cursor = 0;
    size_t unary_cursor = 0;
    size_t item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 2450 || mir_cfg_block_count() != 246 ||
        mir.local_bytes != 762 || mir.has_vla ||
        (mir.return_type & 15) != TYPE_INT ||
        strlen(mir_ptr_condition_opcodes) != (size_t)mir.count)
        return mir_machine_reject(
            "pointer-condition-main", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        char actual = mir_ptr_condition_opcode_char(
            mir.insns[instruction].opcode);

        if (actual == mir_ptr_condition_opcodes[instruction])
            continue;
        if (promoted_cursor >=
                sizeof(mir_ptr_promoted_opcode_indices) /
                    sizeof(mir_ptr_promoted_opcode_indices[0]) ||
            instruction !=
                mir_ptr_promoted_opcode_indices[promoted_cursor] ||
            actual != mir_ptr_promoted_opcodes[promoted_cursor])
            return mir_machine_reject(
                "pointer-condition-main", "opcodes");
        promoted_variant = 1;
        ++promoted_cursor;
    }
    if (promoted_variant &&
        promoted_cursor !=
            sizeof(mir_ptr_promoted_opcode_indices) /
                sizeof(mir_ptr_promoted_opcode_indices[0]))
        return mir_machine_reject(
            "pointer-condition-main", "promoted-opcodes");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode == MIR_BINARY) {
            if (binary_cursor >= strlen(mir_ptr_binary_operations) ||
                mir_ptr_binary_operation_char(
                    (int)insn->immediate) !=
                    mir_ptr_binary_operations[binary_cursor] ||
                mir_ptr_binary_type_char(
                    insn->type, insn->secondary_offset) !=
                    mir_ptr_binary_types[binary_cursor])
                return mir_machine_reject(
                    "pointer-condition-main", "operations");
            ++binary_cursor;
        } else if (insn->opcode == MIR_UNARY) {
            char operation =
                insn->immediate == 0 ? 'C' :
                insn->immediate == '!' ? 'N' : 0;

            if (unary_cursor >=
                    strlen(mir_ptr_unary_operations) ||
                operation !=
                    mir_ptr_unary_operations[unary_cursor])
                return mir_machine_reject(
                    "pointer-condition-main", "unary");
            ++unary_cursor;
        }
        if ((insn->opcode == MIR_LOAD_INDIRECT ||
             insn->opcode == MIR_STORE_INDIRECT) &&
            (insn->bit_width != 0 ||
             (insn->memory_size != 1 &&
              insn->memory_size != 2 &&
              insn->memory_size != 4)))
            return mir_machine_reject(
                "pointer-condition-main", "memory-effects");
        if (insn->opcode == MIR_INDEX_ADDRESS &&
            (insn->immediate <= 0 ||
             (insn->memory_size != 1 &&
              insn->memory_size != 2 &&
              insn->memory_size != 4 &&
              insn->memory_size != 35 &&
              insn->memory_size != 155 &&
              insn->memory_size != 342)))
            return mir_machine_reject(
                "pointer-condition-main", "index-layout");
    }
    if (binary_cursor != strlen(mir_ptr_binary_operations) ||
        unary_cursor != strlen(mir_ptr_unary_operations))
        return mir_machine_reject(
            "pointer-condition-main", "operation-count");
    for (item = 0;
         item < sizeof(expected_constants) /
                sizeof(expected_constants[0]);
         ++item)
        if (!mir_machine_constant_equals(
                 mir.insns[expected_constants[item].instruction].dst,
                 expected_constants[item].value))
            return mir_machine_reject(
                "pointer-condition-main", "constants");

    if (!mir_ptr_condition_call(3, &plan->print_function) ||
        !mir_ptr_condition_call(2440, &function) ||
        function != plan->print_function ||
        !mir_ptr_condition_call(2447, &function) ||
        function != plan->print_function ||
        !mir_ptr_condition_string(1, &plan->start_string) ||
        !mir_ptr_condition_string(2436, &plan->failed_string) ||
        !mir_ptr_condition_string(2445, &plan->pass_string))
        return mir_machine_reject(
            "pointer-condition-main", "print");
    for (item = 0; item < 3; ++item) {
        int call = item == 0 ? 3 : item == 1 ? 2440 : 2447;
        const char *call_name =
            mir.insns[call].base_name[0] != 0
                ? mir.insns[call].base_name
                : asm_name_for(sym_asm_name(plan->print_function));

        if (strlen(call_name) >= sizeof(plan->print_names[item]))
            return 0;
        strcpy(plan->print_names[item], call_name);
    }
    for (item = 0;
         item < sizeof(init_calls) / sizeof(init_calls[0]);
         ++item) {
        if (!mir_ptr_condition_call(init_calls[item], &function))
            return mir_machine_reject(
                "pointer-condition-main", "init-call");
        if (item == 0)
            plan->init_function = function;
        else if (function != plan->init_function)
            return mir_machine_reject(
                "pointer-condition-main", "init-identity");
    }
    for (item = 0; item < MIR_PTR_CONDITION_FAIL_CALLS; ++item) {
        int argument;
        const struct MirInsn *definition;

        if (!mir_ptr_condition_call(fail_calls[item], &function) ||
            !mir_machine_single_call_argument(
                &mir.insns[fail_calls[item]], &argument))
            return mir_machine_reject(
                "pointer-condition-main", "fail-call");
        if (item == 0)
            plan->fail_function = function;
        else if (function != plan->fail_function)
            return mir_machine_reject(
                "pointer-condition-main", "fail-identity");
        definition = mir_definition(argument);
        if (definition == NULL ||
            definition->opcode != MIR_STRING_ADDRESS ||
            definition->immediate < 0)
            return mir_machine_reject(
                "pointer-condition-main", "fail-string");
        plan->fail_strings[item] = (int)definition->immediate;
    }
    for (item = 0; item < MIR_PTR_CONDITION_CHECK_CALLS; ++item) {
        const struct MirInsn *definition;

        if (!mir_ptr_condition_call(check_calls[item], &function) ||
            !mir_machine_three_call_arguments(
                &mir.insns[check_calls[item]], arguments))
            return mir_machine_reject(
                "pointer-condition-main", "check-call");
        if (item == 0)
            plan->check_function = function;
        else if (function != plan->check_function)
            return mir_machine_reject(
                "pointer-condition-main", "check-identity");
        definition = mir_definition(arguments[0]);
        if (definition == NULL ||
            definition->opcode != MIR_STRING_ADDRESS ||
            definition->immediate < 0 ||
            mir_definition(arguments[2]) == NULL ||
            mir_definition(arguments[2])->opcode != MIR_CONST)
            return mir_machine_reject(
                "pointer-condition-main", "check-arguments");
        plan->check_strings[item] = (int)definition->immediate;
    }
#define MIR_PTR_CAPTURE_CALLS(calls, member) \
    do { \
        for (item = 0; item < sizeof(calls) / sizeof((calls)[0]); ++item) { \
            if (!mir_ptr_condition_call((calls)[item], &function)) \
                return mir_machine_reject( \
                    "pointer-condition-main", "picker-call"); \
            if (item == 0) \
                plan->member = function; \
            else if (function != plan->member) \
                return mir_machine_reject( \
                    "pointer-condition-main", "picker-identity"); \
        } \
    } while (0)
    MIR_PTR_CAPTURE_CALLS(
        pick_wrapper_calls, pick_wrapper_function);
    MIR_PTR_CAPTURE_CALLS(pick_node_calls, pick_node_function);
    MIR_PTR_CAPTURE_CALLS(pick_leaf_calls, pick_leaf_function);
#undef MIR_PTR_CAPTURE_CALLS
    if (!mir_ptr_condition_call(1444, &plan->pick_int_function) ||
        !mir_ptr_condition_call(1467, &plan->pick_long_function))
        return mir_machine_reject(
            "pointer-condition-main", "loop-pickers");

    if (!mir_machine_global_address_offset(
            mir.insns[4].dst, &plan->globals[0], &global_offset, 0) ||
        global_offset != 0 ||
        !mir_machine_global_address_offset(
            mir.insns[32].dst, &plan->globals[1], &global_offset, 0) ||
        global_offset != 0 ||
        !mir_machine_global_address_offset(
            mir.insns[46].dst, &plan->globals[2], &global_offset, 0) ||
        global_offset != 0 ||
        !mir_machine_global_address_offset(
            mir.insns[101].dst, &plan->globals[3], &global_offset, 0) ||
        global_offset != 0 ||
        !mir_machine_global_address_offset(
            mir.insns[108].dst, &plan->globals[4], &global_offset, 0) ||
        global_offset != 0 ||
        !mir_machine_global_address_offset(
            mir.insns[116].dst, &plan->globals[5], &global_offset, 0) ||
        global_offset != 0 ||
        !mir_scalar_memory_location(
            &mir.insns[2434], &type, &storage, &offset) ||
        storage != SC_GLOBAL || type != TYPE_INT ||
        (plan->globals[6] = find_global(
             mir.insns[2434].name)) == NULL)
        return mir_machine_reject(
            "pointer-condition-main", "globals");
    for (item = 0; item < 7; ++item) {
        size_t other;

        if (plan->globals[item]->is_volatile)
            return mir_machine_reject(
                "pointer-condition-main", "volatile-global");
        for (other = item + 1; other < 7; ++other)
            if (plan->globals[item] == plan->globals[other])
                return mir_machine_reject(
                    "pointer-condition-main", "global-alias");
    }
    if (plan->print_function == plan->init_function ||
        plan->print_function == plan->fail_function ||
        plan->print_function == plan->check_function ||
        plan->fail_function == plan->check_function ||
        plan->pick_wrapper_function == plan->pick_node_function ||
        plan->pick_wrapper_function == plan->pick_leaf_function ||
        plan->pick_int_function == plan->pick_long_function)
        return mir_machine_reject(
            "pointer-condition-main", "function-alias");
    if (mir.insns[18].opcode != MIR_ADDRESS ||
        mir.insns[62].opcode != MIR_ADDRESS ||
        mir.insns[76].opcode != MIR_ADDRESS ||
        mir.insns[155].opcode != MIR_ADDRESS ||
        mir.insns[159].opcode != MIR_STORE ||
        mir.insns[159].src1 != mir.insns[157].dst ||
        mir.insns[167].opcode != MIR_STORE ||
        mir.insns[167].src1 != mir.insns[165].dst ||
        mir.insns[178].opcode != MIR_STORE ||
        mir.insns[178].src1 != mir.insns[176].dst ||
        !strcmp(mir.insns[18].name, mir.insns[62].name) ||
        !strcmp(mir.insns[18].name, mir.insns[76].name) ||
        !strcmp(mir.insns[62].name, mir.insns[76].name) ||
        mir.insns[94].object < 0 ||
        mir.insns[181].object < 0 ||
        mir.insns[1421].object < 0 ||
        mir.insns[1424].object < 0 ||
        mir.insns[94].object == mir.insns[181].object ||
        mir.insns[94].object == mir.insns[1421].object ||
        mir.insns[94].object == mir.insns[1424].object ||
        mir.insns[181].object == mir.insns[1421].object ||
        mir.insns[181].object == mir.insns[1424].object ||
        mir.insns[1421].object == mir.insns[1424].object)
        return mir_machine_reject(
            "pointer-condition-main", "local-aliases");
    if (mir.insns[38].opcode != MIR_STORE_INDIRECT ||
        mir.insns[38].src1 != mir.insns[34].dst ||
        mir.insns[38].src2 != mir.insns[37].dst ||
        mir.insns[45].opcode != MIR_STORE_INDIRECT ||
        mir.insns[45].src1 != mir.insns[41].dst ||
        mir.insns[45].src2 != mir.insns[44].dst ||
        mir.insns[53].opcode != MIR_STORE_INDIRECT ||
        mir.insns[53].src1 != mir.insns[48].dst ||
        mir.insns[53].src2 != mir.insns[51].dst ||
        mir.insns[61].opcode != MIR_STORE_INDIRECT ||
        mir.insns[61].src1 != mir.insns[56].dst ||
        mir.insns[61].src2 != mir.insns[59].dst ||
        mir.insns[68].opcode != MIR_STORE_INDIRECT ||
        mir.insns[68].src1 != mir.insns[64].dst ||
        mir.insns[68].src2 != mir.insns[67].dst ||
        mir.insns[75].opcode != MIR_STORE_INDIRECT ||
        mir.insns[75].src1 != mir.insns[71].dst ||
        mir.insns[75].src2 != mir.insns[74].dst ||
        mir.insns[83].opcode != MIR_STORE_INDIRECT ||
        mir.insns[83].src1 != mir.insns[78].dst ||
        mir.insns[83].src2 != mir.insns[81].dst ||
        mir.insns[91].opcode != MIR_STORE_INDIRECT ||
        mir.insns[91].src1 != mir.insns[86].dst ||
        mir.insns[91].src2 != mir.insns[89].dst)
        return mir_machine_reject(
            "pointer-condition-main", "alias-initialization");
    if (mir.insns[6].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[6].immediate != 342 ||
        mir.insns[163].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[163].immediate != 0 ||
        mir.insns[165].immediate != 155 ||
        mir.insns[174].immediate != 0 ||
        mir.insns[176].immediate != 35 ||
        mir.insns[193].immediate != 2 ||
        mir.insns[216].immediate != 310 ||
        mir.insns[247].immediate != 105 ||
        mir.insns[275].immediate != 107 ||
        mir.insns[386].immediate != 113 ||
        mir.insns[388].immediate != 6 ||
        mir.insns[390].immediate != 2 ||
        mir.insns[2442].opcode != MIR_RETURN ||
        mir.insns[2442].src1 != mir.insns[2441].dst ||
        mir.insns[2449].opcode != MIR_RETURN ||
        mir.insns[2449].src1 != mir.insns[2448].dst)
        return mir_machine_reject(
            "pointer-condition-main", "aggregate-layout");
    return 1;
}

#define MIR_ADDITIVE_CHECKS 6
#define MIR_ADDITIVE_FIXED_STORES 4

struct MirAdditiveSubscriptPlan {
    struct Sym *target;
    struct Sym *source;
    struct Sym *check_function;
    int strings[MIR_ADDITIVE_CHECKS];
    int check_offsets[MIR_ADDITIVE_CHECKS];
    unsigned long expected[MIR_ADDITIVE_CHECKS];
    int fixed_offsets[MIR_ADDITIVE_FIXED_STORES];
    int fixed_values[MIR_ADDITIVE_FIXED_STORES];
    int fill_count;
    int fill_bias;
    int source_count;
    int source_offset_mask;
};

static int mir_additive_opcode_sequence(void)
{
    static const unsigned char expected[] = {
        MIR_LABEL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_PHI,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_UNARY, MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_ADDRESS, MIR_NOP,
        MIR_NOP, MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_NOP, MIR_NOP, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT,
        MIR_ADDRESS, MIR_NOP, MIR_NOP, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_NOP,
        MIR_NOP, MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS,
        MIR_NOP, MIR_NOP, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_NOP, MIR_NOP,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_ADDRESS, MIR_NOP, MIR_NOP, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_NOP,
        MIR_NOP, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_UNARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_LABEL, MIR_PHI, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_NOP, MIR_BINARY, MIR_UNARY,
        MIR_STORE, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_NOP,
        MIR_BINARY, MIR_UNARY, MIR_STORE, MIR_ADDRESS, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_INDEX_ADDRESS, MIR_LOAD, MIR_UNARY,
        MIR_STORE_INDIRECT, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_ADDRESS, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_ADDRESS, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL
    };
    int instruction;

    if (mir.count != (int)(sizeof(expected) / sizeof(expected[0])))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        if ((instruction == 143 || instruction == 144) &&
            ((mir.insns[143].opcode == MIR_LOAD &&
              mir.insns[144].opcode == MIR_CONST) ||
             (mir.insns[143].opcode == MIR_CONST &&
              mir.insns[144].opcode == MIR_LOAD)))
            continue;
        if (mir.insns[instruction].opcode != expected[instruction])
            return 0;
    }
    return 1;
}

static int mir_additive_int_constant(int instruction, long *value)
{
    const struct MirInsn *insn = &mir.insns[instruction];

    if (insn->opcode != MIR_CONST ||
        !mir_packed_scalar_type(insn->type, TYPE_INT, 0, 0))
        return 0;
    *value = insn->immediate;
    return 1;
}

static int mir_additive_commutative_binary(
    int instruction, int left, int right, int operation, int type)
{
    const struct MirInsn *insn = &mir.insns[instruction];
    int left_value = mir.insns[left].dst;
    int right_value = mir.insns[right].dst;

    return insn->opcode == MIR_BINARY &&
           insn->immediate == operation &&
           insn->type == type &&
           insn->secondary_offset == type &&
           ((insn->src1 == left_value && insn->src2 == right_value) ||
            (insn->src1 == right_value && insn->src2 == left_value));
}

static int mir_additive_ordered_binary(
    int instruction, int left, int right, int operation, int type)
{
    const struct MirInsn *insn = &mir.insns[instruction];

    return insn->opcode == MIR_BINARY &&
           insn->src1 == mir.insns[left].dst &&
           insn->src2 == mir.insns[right].dst &&
           insn->immediate == operation &&
           insn->type == type &&
           insn->secondary_offset == type;
}

static int mir_additive_global_root(
    int instruction, struct Sym **root_out)
{
    long offset;

    if (mir.insns[instruction].opcode != MIR_ADDRESS ||
        !mir_machine_global_address_offset(
            mir.insns[instruction].dst, root_out, &offset, 0) ||
        offset != 0 || (*root_out)->is_volatile ||
        (*root_out)->is_funcptr)
        return 0;
    return 1;
}

static int mir_additive_local_word(
    int instruction, int *offset_out)
{
    int type;
    int storage;

    return mir_scalar_memory_location(
               &mir.insns[instruction], &type, &storage, offset_out) &&
           storage == SC_LOCAL &&
           (mir.insns[instruction].memory_size == 2 ||
            (mir.insns[instruction].opcode == MIR_LOAD &&
             mir.insns[instruction].memory_size == 0)) &&
           mir_packed_scalar_type(type, TYPE_INT, 0, 0) &&
           (mir.insns[instruction].memory_flags & (1 | 8)) == 0;
}

static int mir_additive_check(
    struct MirAdditiveSubscriptPlan *plan, int slot, int string_instruction,
    int address_instruction, int index_constant_instruction,
    int index_instruction, int load_instruction, int convert_instruction,
    int call_instruction)
{
    static const int argument_instruction[MIR_ADDITIVE_CHECKS][3] = {
        {58, 66, 68}, {71, 79, 81}, {84, 92, 94},
        {97, 105, 107}, {159, 169, 171}, {174, 184, 186}
    };
    struct Sym *root;
    struct Sym *function;
    int arguments[3];
    long offset;
    long expected;

    if (!mir_additive_global_root(address_instruction, &root) ||
        root != plan->target ||
        !mir_additive_int_constant(index_constant_instruction, &offset) ||
        offset < 0 || offset > 32767 ||
        mir.insns[index_instruction].src1 !=
            mir.insns[address_instruction].dst ||
        mir.insns[index_instruction].src2 !=
            mir.insns[index_constant_instruction].dst ||
        mir.insns[index_instruction].memory_size != 1 ||
        mir.insns[index_instruction].immediate != 1 ||
        !mir_packed_scalar_type(
            mir.insns[index_instruction].type, TYPE_CHAR, 1, 1) ||
        mir.insns[load_instruction].src1 !=
            mir.insns[index_instruction].dst ||
        mir.insns[load_instruction].memory_size != 1 ||
        !mir_packed_scalar_type(
            mir.insns[load_instruction].type, TYPE_CHAR, 1, 0) ||
        mir.insns[convert_instruction].src1 !=
            mir.insns[load_instruction].dst ||
        mir.insns[convert_instruction].immediate != 0 ||
        !mir_packed_scalar_type(
            mir.insns[convert_instruction].type, TYPE_LONG, 0, 0) ||
        !mir_aggregate_direct_function(call_instruction, &function) ||
        !mir_machine_three_call_arguments(
            &mir.insns[call_instruction], arguments) ||
        arguments[0] !=
            mir.insns[argument_instruction[slot][0]].src1 ||
        arguments[1] !=
            mir.insns[argument_instruction[slot][1]].src1 ||
        arguments[2] !=
            mir.insns[argument_instruction[slot][2]].src1 ||
        arguments[0] != mir.insns[string_instruction].dst ||
        arguments[1] != mir.insns[convert_instruction].dst ||
        !mir_machine_constant_value(arguments[2], &expected, 0) ||
        !mir_packed_scalar_type(
            mir_definition(arguments[2])->type, TYPE_LONG, 0, 0))
        return 0;
    if (slot == 0)
        plan->check_function = function;
    else if (function != plan->check_function)
        return 0;
    if (mir.insns[string_instruction].opcode != MIR_STRING_ADDRESS ||
        !mir_packed_scalar_type(
            mir.insns[string_instruction].type, TYPE_CHAR, 0, 1))
        return 0;
    plan->strings[slot] =
        (int)mir.insns[string_instruction].immediate;
    plan->check_offsets[slot] = (int)offset;
    plan->expected[slot] = (unsigned long)expected;
    return 1;
}

static int mir_match_additive_subscript_runner(
    struct MirAdditiveSubscriptPlan *plan)
{
    static const int fixed_address[MIR_ADDITIVE_FIXED_STORES] = {
        25, 33, 41, 49
    };
    static const int fixed_index_constant[MIR_ADDITIVE_FIXED_STORES] = {
        28, 36, 44, 52
    };
    static const int fixed_index[MIR_ADDITIVE_FIXED_STORES] = {
        29, 37, 45, 53
    };
    static const int fixed_value[MIR_ADDITIVE_FIXED_STORES] = {
        31, 39, 47, 55
    };
    static const int fixed_store[MIR_ADDITIVE_FIXED_STORES] = {
        32, 40, 48, 56
    };
    static const int check_layout[MIR_ADDITIVE_CHECKS][6] = {
        {57, 59, 62, 63, 64, 65},
        {70, 72, 75, 76, 77, 78},
        {83, 85, 88, 89, 90, 91},
        {96, 98, 101, 102, 103, 104},
        {158, 160, 165, 166, 167, 168},
        {173, 175, 180, 181, 182, 183}
    };
    struct Sym *root;
    int i_offset;
    int offset_offset;
    int value_offset;
    int offset_load_instruction;
    int offset_one_instruction;
    int item;
    long constant;

    memset(plan, 0, sizeof(*plan));
    if (!mir_additive_opcode_sequence() ||
        mir_cfg_block_count() != 7 || mir.local_bytes != 6 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    offset_load_instruction =
        mir.insns[143].opcode == MIR_LOAD ? 143 : 144;
    offset_one_instruction =
        mir.insns[143].opcode == MIR_CONST ? 143 : 144;
    if (!mir_additive_local_word(3, &i_offset) ||
        !mir_additive_local_word(22, &item) || item != i_offset ||
        !mir_additive_local_word(111, &item) || item != i_offset ||
        !mir_additive_local_word(155, &item) || item != i_offset ||
        !mir_additive_local_word(130, &offset_offset) ||
        !mir_additive_local_word(offset_load_instruction, &item) ||
        item != offset_offset ||
        !mir_additive_local_word(141, &value_offset) ||
        !mir_additive_local_word(147, &item) || item != value_offset ||
        i_offset == offset_offset || i_offset == value_offset ||
        offset_offset == value_offset)
        return mir_machine_reject(
            "additive-subscript-runner", "local-layout");
    if (!mir_machine_constant_equals(mir.insns[3].src1, 0) ||
        mir.insns[5].object < 0 ||
        mir.insns[5].object != mir.insns[3].object ||
        mir.insns[5].object != mir.insns[22].object ||
        mir.insns[5].src1 != mir.insns[1].dst ||
        mir.insns[5].src2 != mir.insns[21].dst ||
        mir.insns[5].phi_pred1 != mir.insns[0].label ||
        mir.insns[5].phi_pred2 != mir.insns[18].label ||
        !mir_additive_int_constant(7, &constant) ||
        constant <= 0 || constant > 256)
        return mir_machine_reject(
            "additive-subscript-runner", "fill-loop");
    plan->fill_count = (int)constant;
    if (!mir_additive_ordered_binary(8, 5, 7, '<', TYPE_INT) ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].label != mir.insns[24].label ||
        !mir_additive_global_root(10, &plan->target) ||
        mir.insns[12].src1 != mir.insns[10].dst ||
        mir.insns[12].src2 != mir.insns[5].dst ||
        mir.insns[12].memory_size != 1 ||
        mir.insns[12].immediate != 1 ||
        !mir_packed_scalar_type(
            mir.insns[12].type, TYPE_CHAR, 1, 1) ||
        !mir_additive_int_constant(14, &constant) ||
        constant != 1 ||
        !mir_additive_commutative_binary(15, 5, 14, '+', TYPE_INT) ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[16].immediate != 0 ||
        !mir_packed_scalar_type(
            mir.insns[16].type, TYPE_CHAR, 1, 0) ||
        mir.insns[17].src1 != mir.insns[12].dst ||
        mir.insns[17].src2 != mir.insns[16].dst ||
        mir.insns[17].memory_size != 1 ||
        !mir_additive_int_constant(20, &constant) ||
        constant != 1 ||
        !mir_additive_commutative_binary(21, 5, 20, '+', TYPE_INT) ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        mir.insns[23].label != mir.insns[4].label)
        return mir_machine_reject(
            "additive-subscript-runner", "fill-operations");
    plan->fill_bias = 1;
    for (item = 0; item < MIR_ADDITIVE_FIXED_STORES; ++item) {
        long offset;
        long value;

        if (!mir_additive_global_root(fixed_address[item], &root) ||
            root != plan->target ||
            !mir_additive_int_constant(
                fixed_index_constant[item], &offset) ||
            offset < 0 || offset > 32767 ||
            mir.insns[fixed_index[item]].src1 !=
                mir.insns[fixed_address[item]].dst ||
            mir.insns[fixed_index[item]].src2 !=
                mir.insns[fixed_index_constant[item]].dst ||
            mir.insns[fixed_index[item]].memory_size != 1 ||
            mir.insns[fixed_index[item]].immediate != 1 ||
            !mir_packed_scalar_type(
                mir.insns[fixed_index[item]].type,
                TYPE_CHAR, 1, 1) ||
            mir.insns[fixed_value[item]].immediate < 0 ||
            mir.insns[fixed_value[item]].immediate > 255 ||
            !mir_packed_scalar_type(
                mir.insns[fixed_value[item]].type,
                TYPE_CHAR, 1, 0) ||
            mir.insns[fixed_store[item]].src1 !=
                mir.insns[fixed_index[item]].dst ||
            mir.insns[fixed_store[item]].src2 !=
                mir.insns[fixed_value[item]].dst ||
            mir.insns[fixed_store[item]].memory_size != 1) {
            return mir_machine_reject(
                "additive-subscript-runner", "fixed-stores");
        }
        value = mir.insns[fixed_value[item]].immediate;
        plan->fixed_offsets[item] = (int)offset;
        plan->fixed_values[item] = (int)value;
    }
    for (item = 0; item < 4; ++item)
        if (!mir_additive_check(
                plan, item, check_layout[item][0],
                check_layout[item][1], check_layout[item][2],
                check_layout[item][3], check_layout[item][4],
                check_layout[item][5], 69 + 13 * item))
            return mir_machine_reject(
                "additive-subscript-runner", "fixed-checks");
    if (!plan->check_function->has_proto ||
        plan->check_function->proto_variadic ||
        plan->check_function->proto_nargs != 3 ||
        (plan->check_function->type & 15) != TYPE_VOID ||
        !mir_packed_scalar_type(
            plan->check_function->proto_types[0], TYPE_CHAR, 0, 1) ||
        !mir_packed_scalar_type(
            plan->check_function->proto_types[1], TYPE_LONG, 0, 0) ||
        !mir_packed_scalar_type(
            plan->check_function->proto_types[2], TYPE_LONG, 0, 0))
        return mir_machine_reject(
            "additive-subscript-runner", "check-prototype");
    if (!mir_machine_constant_equals(mir.insns[111].src1, 0) ||
        mir.insns[113].object != mir.insns[5].object ||
        mir.insns[113].src1 != mir.insns[109].dst ||
        mir.insns[113].src2 != mir.insns[154].dst ||
        mir.insns[113].phi_pred1 != mir.insns[24].label ||
        mir.insns[113].phi_pred2 != mir.insns[151].label ||
        !mir_additive_int_constant(115, &constant) ||
        constant <= 0 || constant > 256)
        return mir_machine_reject(
            "additive-subscript-runner", "source-loop");
    plan->source_count = (int)constant;
    if (!mir_additive_ordered_binary(116, 113, 115, '<', TYPE_INT) ||
        mir.insns[117].src1 != mir.insns[116].dst ||
        mir.insns[117].label != mir.insns[157].label ||
        !mir_additive_global_root(120, &plan->source) ||
        plan->source == plan->target ||
        mir.insns[122].src1 != mir.insns[120].dst ||
        mir.insns[122].src2 != mir.insns[113].dst ||
        mir.insns[122].memory_size != 4 ||
        mir.insns[122].immediate != 4 ||
        !mir_packed_scalar_type(
            mir.insns[122].type, TYPE_INT, 1, 1) ||
        !mir_machine_constant_equals(mir.insns[123].dst, 0) ||
        mir.insns[124].src1 != mir.insns[122].dst ||
        mir.insns[124].src2 != mir.insns[123].dst ||
        mir.insns[124].memory_size != 2 ||
        mir.insns[124].immediate != 2 ||
        !mir_packed_scalar_type(
            mir.insns[124].type, TYPE_INT, 1, 1) ||
        mir.insns[125].src1 != mir.insns[124].dst ||
        mir.insns[125].memory_size != 2 ||
        !mir_packed_scalar_type(
            mir.insns[125].type, TYPE_INT, 1, 0) ||
        !mir_additive_int_constant(126, &constant) ||
        constant < 0 || constant > 254 ||
        mir.insns[128].src1 != mir.insns[125].dst ||
        mir.insns[128].src2 != mir.insns[126].dst ||
        mir.insns[128].immediate != '&' ||
        mir.insns[128].secondary_offset != mir.insns[128].type ||
        !mir_packed_scalar_type(
            mir.insns[128].type, TYPE_INT, 1, 0) ||
        mir.insns[129].src1 != mir.insns[128].dst ||
        mir.insns[129].immediate != 0 ||
        !mir_packed_scalar_type(
            mir.insns[129].type, TYPE_INT, 0, 0) ||
        mir.insns[130].src1 != mir.insns[129].dst)
        return mir_machine_reject(
            "additive-subscript-runner", "source-offset");
    plan->source_offset_mask = (int)constant;
    if (!mir_additive_global_root(131, &root) ||
        root != plan->source ||
        mir.insns[133].src1 != mir.insns[131].dst ||
        mir.insns[133].src2 != mir.insns[113].dst ||
        mir.insns[133].memory_size != 4 ||
        mir.insns[133].immediate != 4 ||
        !mir_packed_scalar_type(
            mir.insns[133].type, TYPE_INT, 1, 1) ||
        !mir_machine_constant_equals(mir.insns[134].dst, 1) ||
        mir.insns[135].src1 != mir.insns[133].dst ||
        mir.insns[135].src2 != mir.insns[134].dst ||
        mir.insns[135].memory_size != 2 ||
        mir.insns[135].immediate != 2 ||
        !mir_packed_scalar_type(
            mir.insns[135].type, TYPE_INT, 1, 1) ||
        mir.insns[136].src1 != mir.insns[135].dst ||
        mir.insns[136].memory_size != 2 ||
        !mir_packed_scalar_type(
            mir.insns[136].type, TYPE_INT, 1, 0) ||
        !mir_machine_constant_equals(mir.insns[137].dst, 255) ||
        mir.insns[139].src1 != mir.insns[136].dst ||
        mir.insns[139].src2 != mir.insns[137].dst ||
        mir.insns[139].immediate != '&' ||
        mir.insns[139].secondary_offset != mir.insns[139].type ||
        !mir_packed_scalar_type(
            mir.insns[139].type, TYPE_INT, 1, 0) ||
        mir.insns[140].src1 != mir.insns[139].dst ||
        mir.insns[140].immediate != 0 ||
        !mir_packed_scalar_type(
            mir.insns[140].type, TYPE_INT, 0, 0) ||
        mir.insns[141].src1 != mir.insns[140].dst)
        return mir_machine_reject(
            "additive-subscript-runner", "source-value");
    if (!mir_additive_global_root(142, &root) ||
        root != plan->target ||
        !mir_machine_constant_equals(
            mir.insns[offset_one_instruction].dst, 1) ||
        !mir_additive_commutative_binary(
            145, offset_load_instruction, offset_one_instruction,
            '+', TYPE_INT) ||
        mir.insns[146].src1 != mir.insns[142].dst ||
        mir.insns[146].src2 != mir.insns[145].dst ||
        mir.insns[146].memory_size != 1 ||
        mir.insns[146].immediate != 1 ||
        !mir_packed_scalar_type(
            mir.insns[146].type, TYPE_CHAR, 1, 1) ||
        mir.insns[148].src1 != mir.insns[147].dst ||
        mir.insns[148].immediate != 0 ||
        !mir_packed_scalar_type(
            mir.insns[148].type, TYPE_CHAR, 1, 0) ||
        mir.insns[149].src1 != mir.insns[146].dst ||
        mir.insns[149].src2 != mir.insns[148].dst ||
        mir.insns[149].memory_size != 1 ||
        !mir_machine_constant_equals(mir.insns[153].dst, 1) ||
        !mir_additive_commutative_binary(
            154, 113, 153, '+', TYPE_INT) ||
        mir.insns[155].src1 != mir.insns[154].dst ||
        mir.insns[156].label != mir.insns[112].label)
        return mir_machine_reject(
            "additive-subscript-runner", "target-store");
    for (item = 4; item < MIR_ADDITIVE_CHECKS; ++item)
        if (!mir_additive_check(
                plan, item, check_layout[item][0],
                check_layout[item][1], check_layout[item][2],
                check_layout[item][3], check_layout[item][4],
                check_layout[item][5], item == 4 ? 172 : 187))
            return mir_machine_reject(
                "additive-subscript-runner", "source-checks");
    for (item = 0; item < MIR_ADDITIVE_CHECKS; ++item) {
        int other;

        for (other = item + 1; other < MIR_ADDITIVE_CHECKS; ++other)
            if (plan->strings[item] == plan->strings[other])
                return mir_machine_reject(
                    "additive-subscript-runner", "check-strings");
    }
    return 1;
}

static void mir_additive_emit_absolute_byte_store(
    MirStream *out, struct Sym *symbol, int offset, int value)
{
    mir_stream_printf(out, "\tld a,%d\n\tld (%s%+d),a\n",
            value & 255, asm_name_for(sym_asm_name(symbol)), offset);
}

static void mir_additive_emit_check(
    MirStream *out, const struct MirAdditiveSubscriptPlan *plan, int check)
{
    mir_emit_final_call_constant(out, plan->expected[check], 4);
    mir_stream_printf(out,
            "\tld a,(%s%+d)\n\tld l,a\n\tld h,0\n"
            "\tld de,0\n\tpush de\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            asm_name_for(sym_asm_name(plan->target)),
            plan->check_offsets[check], plan->strings[check]);
    mir_machine_emit_symbol_call(out, plan->check_function);
    mir_emit_final_call_cleanup(out, 5);
}

static void mir_emit_additive_subscript_runner(
    MirStream *out, const struct MirAdditiveSubscriptPlan *plan)
{
    int fill_loop = new_label();
    int source_loop = new_label();
    int item;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_address_hl(out, plan->target, 0);
    mir_stream_printf(out, "\tld b,%d\n\tld a,%d\nL%d:\n"
                 "\tld (hl),a\n\tinc hl\n\tinc a\n\tdjnz L%d\n",
            plan->fill_count & 255, plan->fill_bias & 255,
            fill_loop, fill_loop);
    for (item = 0; item < MIR_ADDITIVE_FIXED_STORES; ++item)
        mir_additive_emit_absolute_byte_store(
            out, plan->target, plan->fixed_offsets[item],
            plan->fixed_values[item]);
    for (item = 0; item < 4; ++item)
        mir_additive_emit_check(out, plan, item);
    mir_machine_emit_global_address_hl(out, plan->source, 0);
    mir_stream_printf(out,
            "\tld b,%d\nL%d:\n"
            "\tld a,(hl)\n\tand %d\n\tinc a\n\tld c,a\n"
            "\tinc hl\n\tinc hl\n\tld a,(hl)\n\tpush hl\n",
            plan->source_count & 255, source_loop,
            plan->source_offset_mask);
    mir_machine_emit_global_address_hl(out, plan->target, 0);
    mir_stream_puts("\tld e,c\n\tld d,0\n\tadd hl,de\n\tld (hl),a\n"
          "\tpop hl\n\tinc hl\n\tinc hl\n", out);
    mir_stream_printf(out, "\tdjnz L%d\n", source_loop);
    for (item = 4; item < MIR_ADDITIVE_CHECKS; ++item)
        mir_additive_emit_check(out, plan, item);
    mir_stream_puts("\tret\n", out);
}

static int mir_vla_smooth_signed_type(int type, int width)
{
    return type_ptr_depth(type) == 0 &&
           !type_is_float(type) &&
           (type & 15) == (width == 4 ? TYPE_LONG : TYPE_INT) &&
           (type & TYPE_UNSIGNED) == 0 &&
           type_size(type) == width;
}

static int mir_vla_smooth_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
           !type_is_float(type) &&
           (type & 15) == TYPE_INT &&
           (type & TYPE_UNSIGNED) == 0 &&
           type_size(type) == 2;
}

static int mir_vla_smooth_parameter(
    int instruction, int expected_offset, int pointer,
    int *stack_offset)
{
    const struct MirInsn *parameter = &mir.insns[instruction];
    int memory_type;
    int memory_storage;
    int memory_offset;

    if (parameter->opcode != MIR_PARAM ||
        parameter->object < 0 ||
        !mir_scalar_memory_location(
            parameter, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM ||
        (pointer
             ? (!mir_vla_smooth_pointer_type(parameter->type) ||
                !mir_vla_smooth_pointer_type(memory_type) ||
                mir_machine_pointee_is_volatile(parameter))
             : (!mir_vla_smooth_signed_type(parameter->type, 2) ||
                !mir_vla_smooth_signed_type(memory_type, 2))) ||
        !mir_machine_parameter_value_offset(parameter->dst, stack_offset) ||
        *stack_offset != expected_offset)
        return 0;
    return 1;
}

static int mir_vla_smooth_local(
    int instruction, int width, int object, int *offset_out)
{
    const struct MirInsn *insn = &mir.insns[instruction];
    int memory_type;
    int memory_storage;
    int memory_offset;

    if ((insn->opcode != MIR_LOAD && insn->opcode != MIR_STORE) ||
        insn->object != object ||
        !mir_scalar_memory_location(
            insn, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_LOCAL ||
        !mir_vla_smooth_signed_type(memory_type, width) ||
        !mir_vla_smooth_signed_type(insn->type, width) ||
        !mir_machine_named_nonvolatile(insn))
        return 0;
    if (offset_out != NULL)
        *offset_out = memory_offset;
    return 1;
}

static int mir_vla_smooth_same_local(
    int instruction, int anchor, int width)
{
    return mir_vla_smooth_local(
               instruction, width, mir.insns[anchor].object, NULL) &&
           mir_machine_same_location(
               &mir.insns[instruction], &mir.insns[anchor]);
}

static int mir_vla_smooth_distinct_objects(const int *objects, int count)
{
    int left;
    int right;

    for (left = 0; left < count; ++left) {
        if (objects[left] < 0)
            return 0;
        for (right = left + 1; right < count; ++right)
            if (objects[left] == objects[right])
                return 0;
    }
    return 1;
}

static int mir_vla_smooth_index(
    int instruction, int base, int subscript)
{
    const struct MirInsn *index = &mir.insns[instruction];

    return index->opcode == MIR_INDEX_ADDRESS &&
           index->src1 == mir.insns[base].dst &&
           index->src2 == mir.insns[subscript].dst &&
           index->immediate == 2 &&
           index->memory_size == 2 &&
           index->memory_flags == 0 &&
           mir_vla_smooth_pointer_type(index->type);
}

static int mir_match_vla_smooth(
    struct MirVlaSmoothPlan *plan)
{
    static const int expected_opcodes[141] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_NOP,
        MIR_CONST, MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_PHI, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_CONST, MIR_STORE, MIR_NOP,
        MIR_CONST, MIR_STORE, MIR_NOP, MIR_NOP, MIR_NOP, MIR_BINARY,
        MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_LOAD, MIR_NOP,
        MIR_NOP, MIR_BINARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_NOP, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD, MIR_LOAD, MIR_BINARY,
        MIR_UNARY, MIR_STORE_INDIRECT, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL,
        MIR_LOAD, MIR_RETURN
    };
    static const int label_indices[12] = {
        0, 26, 53, 78, 81, 83, 99, 101, 107, 130, 132, 138
    };
    static const int constant_values[][2] = {
        {6, 0}, {9, 2}, {24, 0}, {43, 0}, {46, 0}, {71, 0},
        {79, 1}, {82, 0}, {95, 1}, {103, 1}, {127, 1}, {134, 1}
    };
    static const int parameter_object_uses[][2] = {
        {27, 1}, {38, 1}, {54, 1}, {75, 1},
        {28, 2}, {55, 2},
        {29, 3}, {56, 3}, {87, 3}, {120, 3},
        {30, 4}, {57, 4}, {108, 4}, {116, 4}
    };
    static const int local_object_uses[][2] = {
        {31, 7}, {58, 7},
        {20, 11}, {32, 11}, {50, 11}, {59, 11}, {66, 11},
        {13, 25}, {19, 25}, {33, 25}, {37, 25}, {49, 25}, {60, 25},
        {65, 25}, {109, 25}, {117, 25}, {121, 25}, {133, 25}, {136, 25},
        {16, 44}, {34, 44}, {61, 44},
        {18, 47}, {35, 47}, {62, 47},
        {22, 52}, {36, 52}, {63, 52}
    };
    static const int local_widths[6] = {4, 2, 2, 4, 2, 2};
    int objects[10];
    int offsets[6];
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 141 || mir_cfg_block_count() != 12 ||
        mir.has_vla || mir.local_bytes != 16 ||
        mir.aggregate_temp_bytes != 0 ||
        !mir_vla_smooth_signed_type(mir.return_type, 4))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject("vla-smooth", "opcodes");
    for (item = 0; item < 12; ++item) {
        int other;

        for (other = item + 1; other < 12; ++other)
            if (mir.insns[label_indices[item]].label ==
                mir.insns[label_indices[other]].label)
                return mir_machine_reject("vla-smooth", "labels");
    }
    if (!mir_vla_smooth_parameter(
            1, 2, 0, &plan->n_stack_offset) ||
        !mir_vla_smooth_parameter(
            2, 4, 0, &plan->w_stack_offset) ||
        !mir_vla_smooth_parameter(
            3, 6, 1, &plan->src_stack_offset) ||
        !mir_vla_smooth_parameter(
            4, 8, 1, &plan->dst_stack_offset))
        return mir_machine_reject("vla-smooth", "parameter-abi");
    objects[0] = mir.insns[1].object;
    objects[1] = mir.insns[2].object;
    objects[2] = mir.insns[3].object;
    objects[3] = mir.insns[4].object;
    objects[4] = mir.insns[7].object;
    objects[5] = mir.insns[11].object;
    objects[6] = mir.insns[25].object;
    objects[7] = mir.insns[44].object;
    objects[8] = mir.insns[47].object;
    objects[9] = mir.insns[52].object;
    if (!mir_vla_smooth_distinct_objects(objects, 10))
        return mir_machine_reject("vla-smooth", "objects");
    for (item = 0;
         item < (int)(sizeof(parameter_object_uses) /
                      sizeof(parameter_object_uses[0]));
         ++item)
        if (mir.insns[parameter_object_uses[item][0]].object !=
            mir.insns[parameter_object_uses[item][1]].object)
            return mir_machine_reject(
                "vla-smooth", "parameter-objects");
    if (!mir_vla_smooth_local(7, 4, objects[4], &offsets[0]) ||
        !mir_vla_smooth_local(11, 2, objects[5], &offsets[1]) ||
        !mir_vla_smooth_local(25, 2, objects[6], &offsets[2]) ||
        !mir_vla_smooth_local(44, 4, objects[7], &offsets[3]) ||
        !mir_vla_smooth_local(47, 2, objects[8], &offsets[4]) ||
        !mir_vla_smooth_local(52, 2, objects[9], &offsets[5]))
        return mir_machine_reject("vla-smooth", "local-types");
    for (item = 0; item < 6; ++item) {
        int other;

        for (other = item + 1; other < 6; ++other)
            if (offsets[item] <
                    offsets[other] + local_widths[other] &&
                offsets[other] <
                    offsets[item] + local_widths[item])
                return mir_machine_reject(
                    "vla-smooth", "local-layout");
    }
    for (item = 0;
         item < (int)(sizeof(local_object_uses) /
                      sizeof(local_object_uses[0]));
         ++item)
        if (mir.insns[local_object_uses[item][0]].object !=
            mir.insns[local_object_uses[item][1]].object)
            return mir_machine_reject("vla-smooth", "local-objects");
    if (!mir_vla_smooth_same_local(126, 7, 4) ||
        !mir_vla_smooth_same_local(129, 7, 4) ||
        !mir_vla_smooth_same_local(139, 7, 4) ||
        !mir_vla_smooth_same_local(86, 44, 4) ||
        !mir_vla_smooth_same_local(93, 44, 4) ||
        !mir_vla_smooth_same_local(111, 44, 4) ||
        !mir_vla_smooth_same_local(94, 47, 2) ||
        !mir_vla_smooth_same_local(97, 47, 2) ||
        !mir_vla_smooth_same_local(112, 47, 2) ||
        !mir_vla_smooth_same_local(64, 52, 2) ||
        !mir_vla_smooth_same_local(70, 52, 2) ||
        !mir_vla_smooth_same_local(74, 52, 2) ||
        !mir_vla_smooth_same_local(88, 52, 2) ||
        !mir_vla_smooth_same_local(102, 52, 2) ||
        !mir_vla_smooth_same_local(105, 52, 2))
        return mir_machine_reject("vla-smooth", "local-aliases");
    if (!mir_machine_unobservable_local_store(&mir.insns[7]) ||
        !mir_machine_unobservable_local_store(&mir.insns[11]) ||
        !mir_machine_unobservable_local_store(&mir.insns[25]) ||
        !mir_machine_unobservable_local_store(&mir.insns[44]) ||
        !mir_machine_unobservable_local_store(&mir.insns[47]) ||
        !mir_machine_unobservable_local_store(&mir.insns[52]))
        return mir_machine_reject("vla-smooth", "local-address");
    for (item = 0;
         item < (int)(sizeof(constant_values) /
                      sizeof(constant_values[0]));
         ++item)
        if (!mir_machine_constant_equals(
                mir.insns[constant_values[item][0]].dst,
                constant_values[item][1]))
            return mir_machine_reject("vla-smooth", "constants");
    if (!mir_vla_smooth_signed_type(mir.insns[6].type, 4) ||
        !mir_vla_smooth_signed_type(mir.insns[9].type, 2) ||
        !mir_vla_smooth_signed_type(mir.insns[43].type, 4) ||
        !mir_vla_smooth_signed_type(mir.insns[46].type, 2) ||
        !mir_vla_smooth_signed_type(mir.insns[71].type, 2) ||
        !mir_vla_smooth_signed_type(mir.insns[127].type, 4))
        return mir_machine_reject("vla-smooth", "constant-types");
    if (mir.insns[7].src1 != mir.insns[6].dst ||
        mir.insns[10].immediate != '/' ||
        mir.insns[10].src1 != mir.insns[2].dst ||
        mir.insns[10].src2 != mir.insns[9].dst ||
        !mir_vla_smooth_signed_type(mir.insns[10].type, 2) ||
        !mir_vla_smooth_signed_type(
            mir.insns[10].secondary_offset, 2) ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[25].src1 != mir.insns[24].dst)
        return mir_machine_reject("vla-smooth", "initialization");
    if (mir.insns[33].src1 != mir.insns[24].dst ||
        mir.insns[33].src2 != mir.insns[135].dst ||
        mir.insns[33].phi_pred1 != mir.insns[0].label ||
        mir.insns[33].phi_pred2 != mir.insns[132].label ||
        mir.insns[33].object != objects[6] ||
        !mir_vla_smooth_signed_type(mir.insns[33].type, 2) ||
        mir.insns[39].immediate != '<' ||
        mir.insns[39].src1 != mir.insns[33].dst ||
        mir.insns[39].src2 != mir.insns[1].dst ||
        !mir_vla_smooth_signed_type(mir.insns[39].type, 2) ||
        mir.insns[40].src1 != mir.insns[39].dst ||
        mir.insns[40].label != mir.insns[138].label)
        return mir_machine_reject("vla-smooth", "outer-loop");
    if (mir.insns[44].src1 != mir.insns[43].dst ||
        mir.insns[47].src1 != mir.insns[46].dst ||
        mir.insns[51].immediate != '-' ||
        mir.insns[51].src1 != mir.insns[33].dst ||
        mir.insns[51].src2 != mir.insns[10].dst ||
        !mir_vla_smooth_signed_type(mir.insns[51].type, 2) ||
        mir.insns[52].src1 != mir.insns[51].dst ||
        mir.insns[67].immediate != '+' ||
        mir.insns[67].src1 != mir.insns[33].dst ||
        mir.insns[67].src2 != mir.insns[10].dst ||
        !mir_vla_smooth_signed_type(mir.insns[67].type, 2) ||
        mir.insns[68].immediate != TOK_LE ||
        mir.insns[68].src1 != mir.insns[64].dst ||
        mir.insns[68].src2 != mir.insns[67].dst ||
        !mir_vla_smooth_signed_type(mir.insns[68].type, 2) ||
        mir.insns[69].src1 != mir.insns[68].dst ||
        mir.insns[69].label != mir.insns[107].label)
        return mir_machine_reject("vla-smooth", "inner-bounds");
    if (mir.insns[72].immediate != TOK_GE ||
        mir.insns[72].src1 != mir.insns[70].dst ||
        mir.insns[72].src2 != mir.insns[71].dst ||
        !mir_vla_smooth_signed_type(mir.insns[72].type, 2) ||
        mir.insns[73].src1 != mir.insns[72].dst ||
        mir.insns[73].label != mir.insns[81].label ||
        mir.insns[76].immediate != '<' ||
        mir.insns[76].src1 != mir.insns[74].dst ||
        mir.insns[76].src2 != mir.insns[1].dst ||
        !mir_vla_smooth_signed_type(mir.insns[76].type, 2) ||
        mir.insns[77].src1 != mir.insns[76].dst ||
        mir.insns[77].label != mir.insns[81].label ||
        mir.insns[80].label != mir.insns[83].label ||
        mir.insns[84].src1 != mir.insns[79].dst ||
        mir.insns[84].src2 != mir.insns[82].dst ||
        mir.insns[84].phi_pred1 != mir.insns[78].label ||
        mir.insns[84].phi_pred2 != mir.insns[81].label ||
        mir.insns[85].src1 != mir.insns[84].dst ||
        mir.insns[85].label != mir.insns[99].label)
        return mir_machine_reject("vla-smooth", "valid-index");
    if (!mir_vla_smooth_index(89, 3, 88) ||
        mir.insns[90].src1 != mir.insns[89].dst ||
        mir.insns[90].memory_size != 2 ||
        mir.insns[90].memory_flags != 0 ||
        mir.insns[90].bit_width != 0 ||
        !mir_vla_smooth_signed_type(mir.insns[90].type, 2) ||
        mir.insns[91].immediate != '+' ||
        mir.insns[91].src1 != mir.insns[86].dst ||
        mir.insns[91].src2 != mir.insns[90].dst ||
        !mir_vla_smooth_signed_type(mir.insns[91].type, 4) ||
        !mir_vla_smooth_signed_type(
            mir.insns[91].secondary_offset, 4) ||
        mir.insns[93].src1 != mir.insns[91].dst ||
        mir.insns[96].immediate != '+' ||
        mir.insns[96].src1 != mir.insns[94].dst ||
        mir.insns[96].src2 != mir.insns[95].dst ||
        mir.insns[97].src1 != mir.insns[96].dst)
        return mir_machine_reject("vla-smooth", "accumulation");
    if (mir.insns[104].immediate != '+' ||
        mir.insns[104].src1 != mir.insns[102].dst ||
        mir.insns[104].src2 != mir.insns[103].dst ||
        mir.insns[105].src1 != mir.insns[104].dst ||
        mir.insns[106].label != mir.insns[53].label)
        return mir_machine_reject("vla-smooth", "inner-increment");
    if (!mir_vla_smooth_index(110, 4, 33) ||
        mir.insns[113].immediate != '/' ||
        mir.insns[113].src1 != mir.insns[111].dst ||
        mir.insns[113].src2 != mir.insns[112].dst ||
        !mir_vla_smooth_signed_type(mir.insns[113].type, 4) ||
        !mir_vla_smooth_signed_type(
            mir.insns[113].secondary_offset, 4) ||
        mir.insns[114].immediate != 0 ||
        mir.insns[114].src1 != mir.insns[113].dst ||
        !mir_vla_smooth_signed_type(mir.insns[114].type, 2) ||
        mir.insns[115].src1 != mir.insns[110].dst ||
        mir.insns[115].src2 != mir.insns[114].dst ||
        mir.insns[115].memory_size != 2 ||
        mir.insns[115].memory_flags != 0 ||
        mir.insns[115].bit_width != 0)
        return mir_machine_reject("vla-smooth", "average-store");
    if (!mir_vla_smooth_index(118, 4, 33) ||
        mir.insns[119].src1 != mir.insns[118].dst ||
        mir.insns[119].memory_size != 2 ||
        mir.insns[119].memory_flags != 0 ||
        mir.insns[119].bit_width != 0 ||
        !mir_vla_smooth_signed_type(mir.insns[119].type, 2) ||
        !mir_vla_smooth_index(122, 3, 33) ||
        mir.insns[123].src1 != mir.insns[122].dst ||
        mir.insns[123].memory_size != 2 ||
        mir.insns[123].memory_flags != 0 ||
        mir.insns[123].bit_width != 0 ||
        !mir_vla_smooth_signed_type(mir.insns[123].type, 2) ||
        mir.insns[124].immediate != TOK_NE ||
        mir.insns[124].src1 != mir.insns[119].dst ||
        mir.insns[124].src2 != mir.insns[123].dst ||
        !mir_vla_smooth_signed_type(mir.insns[124].type, 2) ||
        mir.insns[125].src1 != mir.insns[124].dst ||
        mir.insns[125].label != mir.insns[130].label ||
        mir.insns[128].immediate != '+' ||
        mir.insns[128].src1 != mir.insns[126].dst ||
        mir.insns[128].src2 != mir.insns[127].dst ||
        !mir_vla_smooth_signed_type(mir.insns[128].type, 4) ||
        mir.insns[129].src1 != mir.insns[128].dst)
        return mir_machine_reject("vla-smooth", "alias-compare");
    if (mir.insns[135].immediate != '+' ||
        mir.insns[135].src1 != mir.insns[33].dst ||
        mir.insns[135].src2 != mir.insns[134].dst ||
        mir.insns[136].src1 != mir.insns[135].dst ||
        mir.insns[137].label != mir.insns[26].label ||
        mir.insns[140].src1 != mir.insns[139].dst)
        return mir_machine_reject("vla-smooth", "return-graph");
    return 1;
}

static void mir_vla_smooth_emit_index_address(
    MirStream *out, int pointer_offset, int index_offset)
{
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tadd hl,hl\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n"
            "\tadd hl,de\n",
            index_offset, index_offset + 1,
            pointer_offset + 2, pointer_offset + 3);
}

static void mir_vla_smooth_emit_bc_index_address(
    MirStream *out, int pointer_offset)
{
    mir_stream_printf(out,
            "\tld h,b\n\tld l,c\n\tadd hl,hl\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n"
            "\tadd hl,de\n",
            pointer_offset + 2, pointer_offset + 3);
}

static void mir_emit_vla_smooth(
    MirStream *out, const struct MirVlaSmoothPlan *plan)
{
    int changed_ready = new_label();
    int changed_done = new_label();
    int half_ready = new_label();
    int inner = new_label();
    int inner_done = new_label();
    int inner_increment = new_label();
    int outer = new_label();
    int outer_done = new_label();
    int valid_index = new_label();

    mir_stream_printf(out, "%s\n", MIR_EXACT_KERNEL_MARKER);
    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-14\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\tld hl,0\n"
          "\tld (ix-14),l\n\tld (ix-13),h\n"
          "\tld (ix-12),l\n\tld (ix-11),h\n", out);
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tbit 7,h\n\tjp z,L%d\n\tinc hl\n"
            "L%d:\n\tsra h\n\trr l\n"
            "\tld (ix-2),l\n\tld (ix-1),h\n"
            "\tld hl,0\n\tld (ix-4),l\n\tld (ix-3),h\n"
            "L%d:\n",
            plan->w_stack_offset + 2, plan->w_stack_offset + 3,
            half_ready, half_ready, outer);
    mir_stream_printf(out,
            "\tld l,(ix-4)\n\tld h,(ix-3)\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n"
            "\tld a,h\n\txor 80h\n\tld h,a\n"
            "\tld a,d\n\txor 80h\n\tld d,a\n"
            "\tor a\n\tsbc hl,de\n\tjp nc,L%d\n",
            plan->n_stack_offset + 2, plan->n_stack_offset + 3,
            outer_done);
    mir_stream_puts("\tld hl,0\n"
          "\tld (ix-8),l\n\tld (ix-7),h\n"
          "\tld (ix-6),l\n\tld (ix-5),h\n"
          "\tld (ix-10),l\n\tld (ix-9),h\n"
          "\tld l,(ix-4)\n\tld h,(ix-3)\n"
          "\tld e,(ix-2)\n\tld d,(ix-1)\n"
          "\tor a\n\tsbc hl,de\n"
          "\tld b,h\n\tld c,l\n", out);
    mir_stream_printf(out, "L%d:\n", inner);
    mir_stream_puts("\tld l,(ix-4)\n\tld h,(ix-3)\n"
          "\tld e,(ix-2)\n\tld d,(ix-1)\n"
          "\tadd hl,de\n\tex de,hl\n"
          "\tld h,b\n\tld l,c\n"
          "\tld a,h\n\txor 80h\n\tld h,a\n"
          "\tld a,d\n\txor 80h\n\tld d,a\n"
          "\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp c,L%d\n\tjp z,L%d\n\tjp L%d\n",
            valid_index, valid_index, inner_done);
    mir_stream_printf(out, "L%d:\n", valid_index);
    mir_stream_puts("\tbit 7,b\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", inner_increment);
    mir_stream_printf(out,
            "\tld h,b\n\tld l,c\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n"
            "\tld a,h\n\txor 80h\n\tld h,a\n"
            "\tld a,d\n\txor 80h\n\tld d,a\n"
            "\tor a\n\tsbc hl,de\n\tjp nc,L%d\n",
            plan->n_stack_offset + 2, plan->n_stack_offset + 3,
            inner_increment);
    mir_vla_smooth_emit_bc_index_address(
        out, plan->src_stack_offset);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tpush bc\n\tld c,e\n\tld b,d\n"
          "\tld a,b\n\trlca\n\tsbc a,a\n\tld e,a\n\tld d,a\n"
          "\tld l,(ix-8)\n\tld h,(ix-7)\n\tadd hl,bc\n"
          "\tld (ix-8),l\n\tld (ix-7),h\n"
          "\tld l,(ix-6)\n\tld h,(ix-5)\n\tadc hl,de\n"
          "\tld (ix-6),l\n\tld (ix-5),h\n"
          "\tld l,(ix-10)\n\tld h,(ix-9)\n\tinc hl\n"
          "\tld (ix-10),l\n\tld (ix-9),h\n\tpop bc\n", out);
    mir_stream_printf(out, "L%d:\n", inner_increment);
    mir_stream_puts("\tinc bc\n", out);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", inner, inner_done);
    mir_stream_puts("\tld l,(ix-6)\n\tld h,(ix-5)\n\tpush hl\n"
          "\tld l,(ix-8)\n\tld h,(ix-7)\n\tpush hl\n"
          "\tld l,(ix-10)\n\tld h,(ix-9)\n"
          "\tld a,h\n\trlca\n\tsbc a,a\n\tld e,a\n\tld d,a\n",
          out);
    mir_emit_runtime_call(out, "__lds");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld b,h\n\tld c,l\n", out);
    mir_vla_smooth_emit_index_address(
        out, plan->dst_stack_offset, -4);
    mir_stream_puts("\tld (hl),c\n\tinc hl\n\tld (hl),b\n", out);
    mir_vla_smooth_emit_index_address(
        out, plan->dst_stack_offset, -4);
    mir_stream_puts("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n", out);
    mir_vla_smooth_emit_index_address(
        out, plan->src_stack_offset, -4);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld a,c\n\tcp e\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", changed_ready);
    mir_stream_puts("\tld a,b\n\tcp d\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", changed_done);
    mir_stream_printf(out, "L%d:\n", changed_ready);
    mir_stream_puts("\tld l,(ix-14)\n\tld h,(ix-13)\n\tinc hl\n"
          "\tld (ix-14),l\n\tld (ix-13),h\n"
          "\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", changed_done);
    mir_stream_puts("\tld l,(ix-12)\n\tld h,(ix-11)\n\tinc hl\n"
          "\tld (ix-12),l\n\tld (ix-11),h\n", out);
    mir_stream_printf(out, "L%d:\n", changed_done);
    mir_stream_puts("\tld l,(ix-4)\n\tld h,(ix-3)\n\tinc hl\n"
          "\tld (ix-4),l\n\tld (ix-3),h\n", out);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", outer, outer_done);
    mir_stream_puts("\tld l,(ix-14)\n\tld h,(ix-13)\n"
          "\tld e,(ix-12)\n\tld d,(ix-11)\n"
          "\tld sp,ix\n\tpop ix\n\tret\n", out);
}

struct MirHeapPopPlan {
    int parameter_stack_offset;
};

static int mir_heap_pop_word_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           (type & 15) == TYPE_INT &&
           (type & TYPE_UNSIGNED) == 0 &&
           !type_is_float(type) &&
           type_size(type) == 2;
}

static int mir_heap_pop_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
           (type & 15) == TYPE_INT &&
           type_size(type) == 2;
}

static int mir_heap_pop_member(
    int instruction, int parameter_value,
    int offset, int extent)
{
    const struct MirInsn *member = &mir.insns[instruction];

    return member->opcode == MIR_MEMBER_ADDRESS &&
           member->src1 == parameter_value &&
           member->immediate == offset &&
           member->memory_size == extent &&
           member->memory_flags == (extent == 128 ? 2 : 0) &&
           member->bit_width == 0 &&
           mir_heap_pop_pointer_type(member->type);
}

static int mir_heap_pop_index(
    int instruction, int base_instruction,
    int subscript_instruction)
{
    const struct MirInsn *index = &mir.insns[instruction];

    return index->opcode == MIR_INDEX_ADDRESS &&
           index->src1 == mir.insns[base_instruction].dst &&
           index->src2 == mir.insns[subscript_instruction].dst &&
           index->immediate == 2 &&
           index->memory_size == 2 &&
           index->memory_flags == 0 &&
           index->bit_width == 0 &&
           mir_heap_pop_pointer_type(index->type);
}

static int mir_heap_pop_load(
    int instruction, int address_instruction)
{
    const struct MirInsn *load = &mir.insns[instruction];

    return load->opcode == MIR_LOAD_INDIRECT &&
           load->src1 == mir.insns[address_instruction].dst &&
           load->memory_size == 2 &&
           load->memory_flags == 0 &&
           load->bit_width == 0 &&
           mir_heap_pop_word_type(load->type);
}

static int mir_heap_pop_store_indirect(
    int instruction, int address_instruction,
    int value_instruction)
{
    const struct MirInsn *store = &mir.insns[instruction];

    return store->opcode == MIR_STORE_INDIRECT &&
           store->src1 == mir.insns[address_instruction].dst &&
           store->src2 == mir.insns[value_instruction].dst &&
           store->memory_size == 2 &&
           store->memory_flags == 0 &&
           store->bit_width == 0;
}

static int mir_heap_pop_local_store(
    int instruction, int value_instruction, int object)
{
    const struct MirInsn *store = &mir.insns[instruction];

    return store->opcode == MIR_STORE &&
           store->src1 == mir.insns[value_instruction].dst &&
           store->object == object &&
           store->memory_size == 2 &&
           store->memory_flags == 0 &&
           store->bit_width == 0;
}

static int mir_heap_pop_local_load(int instruction, int object)
{
    const struct MirInsn *load = &mir.insns[instruction];

    return load->opcode == MIR_LOAD &&
           load->object == object &&
           load->memory_size == 0 &&
           load->memory_flags == 0 &&
           load->bit_width == 0 &&
           mir_heap_pop_word_type(load->type);
}

static int mir_heap_pop_binary(
    int instruction, int left_instruction,
    int right_instruction, int operation)
{
    const struct MirInsn *binary = &mir.insns[instruction];

    return binary->opcode == MIR_BINARY &&
           binary->src1 == mir.insns[left_instruction].dst &&
           binary->src2 == mir.insns[right_instruction].dst &&
           binary->immediate == operation &&
           mir_heap_pop_word_type(binary->type) &&
           mir_heap_pop_word_type(binary->secondary_offset);
}

static int mir_heap_pop_distinct_objects(
    const int *objects, int count)
{
    int left;
    int right;

    for (left = 0; left < count; ++left) {
        if (objects[left] < 0)
            return 0;
        for (right = left + 1; right < count; ++right)
            if (objects[left] == objects[right])
                return 0;
    }
    return 1;
}

static int mir_match_heap_pop(
    struct MirHeapPopPlan *plan)
{
    static const int expected_opcodes[177] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST, MIR_NOP, MIR_BINARY, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_NOP, MIR_CONST, MIR_NOP, MIR_BINARY,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_NOP, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP,
        MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_LOAD,
        MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_JUMP, MIR_LABEL,
        MIR_NOP, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_STORE, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_STORE_INDIRECT, MIR_NOP, MIR_LOAD, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_RETURN
    };
    static const int data_members[] = {
        3, 18, 22, 84, 89, 114, 119, 146, 152, 156, 162
    };
    static const int size_members[] = {10, 12, 24, 79, 109};
    int objects[6];
    int instruction;
    int member;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 177 || mir_cfg_block_count() != 13 ||
        mir.has_vla || mir.local_bytes != 12 ||
        mir.aggregate_temp_bytes != 0 ||
        !mir_heap_pop_word_type(mir.return_type))
        return 0;
    for (instruction = 0; instruction < 177; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->parameter_stack_offset) ||
        plan->parameter_stack_offset != 2 ||
        type_ptr_depth(mir.insns[1].type) != 1 ||
        type_size(mir.insns[1].type) != 2 ||
        mir_machine_pointee_is_volatile(&mir.insns[1]))
        return 0;
    for (member = 0;
         member < (int)(sizeof(data_members) / sizeof(data_members[0]));
         ++member)
        if (!mir_heap_pop_member(
                data_members[member], mir.insns[1].dst, 0, 128))
            return 0;
    for (member = 0;
         member < (int)(sizeof(size_members) / sizeof(size_members[0]));
         ++member)
        if (!mir_heap_pop_member(
                size_members[member], mir.insns[1].dst, 128, 2))
            return 0;

    if (!mir_machine_constant_equals(mir.insns[4].dst, 0) ||
        !mir_heap_pop_index(5, 3, 4) ||
        !mir_heap_pop_load(6, 5) ||
        !mir_heap_pop_member(10, mir.insns[1].dst, 128, 2) ||
        !mir_heap_pop_member(12, mir.insns[1].dst, 128, 2) ||
        !mir_heap_pop_load(13, 12) ||
        !mir_machine_constant_equals(mir.insns[14].dst, 1) ||
        !mir_heap_pop_binary(15, 13, 14, '-') ||
        !mir_heap_pop_store_indirect(16, 10, 15) ||
        !mir_machine_constant_equals(mir.insns[19].dst, 0) ||
        !mir_heap_pop_index(20, 18, 19) ||
        !mir_heap_pop_load(25, 24) ||
        !mir_heap_pop_index(26, 22, 25) ||
        !mir_heap_pop_load(27, 26) ||
        !mir_heap_pop_store_indirect(28, 20, 27) ||
        !mir_machine_constant_equals(mir.insns[29].dst, 0))
        return 0;

    objects[0] = mir.insns[8].object;
    objects[1] = mir.insns[31].object;
    objects[2] = mir.insns[66].object;
    objects[3] = mir.insns[73].object;
    objects[4] = mir.insns[76].object;
    objects[5] = mir.insns[150].object;
    if (!mir_heap_pop_distinct_objects(objects, 6) ||
        !mir_heap_pop_local_store(8, 6, objects[0]) ||
        !mir_heap_pop_local_store(31, 29, objects[1]) ||
        !mir_machine_constant_equals(mir.insns[61].dst, 2) ||
        !mir_heap_pop_binary(63, 61, 55, '*') ||
        !mir_machine_constant_equals(mir.insns[64].dst, 1) ||
        !mir_heap_pop_binary(65, 63, 64, '+') ||
        !mir_heap_pop_local_store(66, 65, objects[2]) ||
        !mir_machine_constant_equals(mir.insns[68].dst, 2) ||
        !mir_heap_pop_binary(70, 68, 55, '*') ||
        !mir_machine_constant_equals(mir.insns[71].dst, 2) ||
        !mir_heap_pop_binary(72, 70, 71, '+') ||
        !mir_heap_pop_local_store(73, 72, objects[3]) ||
        !mir_heap_pop_local_store(76, 55, objects[4]))
        return 0;

    if (mir.insns[55].object != objects[1] ||
        mir.insns[55].src1 != mir.insns[29].dst ||
        mir.insns[55].src2 != mir.insns[168].dst ||
        mir.insns[55].phi_pred1 != mir.insns[0].label ||
        mir.insns[55].phi_pred2 != mir.insns[172].label ||
        !mir_heap_pop_word_type(mir.insns[55].type) ||
        !mir_heap_pop_load(80, 79) ||
        !mir_heap_pop_binary(81, 65, 80, '<') ||
        mir.insns[82].src1 != mir.insns[81].dst ||
        mir.insns[82].label != mir.insns[98].label ||
        !mir_heap_pop_index(86, 84, 65) ||
        !mir_heap_pop_load(87, 86) ||
        !mir_heap_pop_index(91, 89, 55) ||
        !mir_heap_pop_load(92, 91) ||
        !mir_heap_pop_binary(93, 87, 92, '<') ||
        mir.insns[94].src1 != mir.insns[93].dst ||
        mir.insns[94].label != mir.insns[98].label)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[96].dst, 1) ||
        mir.insns[97].label != mir.insns[100].label ||
        !mir_machine_constant_equals(mir.insns[99].dst, 0) ||
        mir.insns[101].src1 != mir.insns[96].dst ||
        mir.insns[101].src2 != mir.insns[99].dst ||
        mir.insns[101].phi_pred1 != mir.insns[95].label ||
        mir.insns[101].phi_pred2 != mir.insns[98].label ||
        mir.insns[102].src1 != mir.insns[101].dst ||
        mir.insns[102].label != mir.insns[106].label ||
        !mir_heap_pop_local_store(105, 65, objects[4]))
        return 0;

    if (!mir_heap_pop_load(110, 109) ||
        !mir_heap_pop_binary(111, 72, 110, '<') ||
        mir.insns[112].src1 != mir.insns[111].dst ||
        mir.insns[112].label != mir.insns[128].label ||
        !mir_heap_pop_index(116, 114, 72) ||
        !mir_heap_pop_load(117, 116) ||
        !mir_heap_pop_local_load(120, objects[4]) ||
        !mir_heap_pop_index(121, 119, 120) ||
        !mir_heap_pop_load(122, 121) ||
        !mir_heap_pop_binary(123, 117, 122, '<') ||
        mir.insns[124].src1 != mir.insns[123].dst ||
        mir.insns[124].label != mir.insns[128].label)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[126].dst, 1) ||
        mir.insns[127].label != mir.insns[130].label ||
        !mir_machine_constant_equals(mir.insns[129].dst, 0) ||
        mir.insns[131].src1 != mir.insns[126].dst ||
        mir.insns[131].src2 != mir.insns[129].dst ||
        mir.insns[131].phi_pred1 != mir.insns[125].label ||
        mir.insns[131].phi_pred2 != mir.insns[128].label ||
        mir.insns[132].src1 != mir.insns[131].dst ||
        mir.insns[132].label != mir.insns[136].label ||
        !mir_heap_pop_local_store(135, 72, objects[4]))
        return 0;

    if (!mir_heap_pop_local_load(137, objects[4]) ||
        !mir_heap_pop_binary(139, 137, 55, TOK_EQ) ||
        mir.insns[140].src1 != mir.insns[139].dst ||
        mir.insns[140].label != mir.insns[143].label ||
        mir.insns[142].label != mir.insns[174].label ||
        !mir_heap_pop_index(148, 146, 55) ||
        !mir_heap_pop_load(149, 148) ||
        !mir_heap_pop_local_store(150, 149, objects[5]) ||
        !mir_heap_pop_index(154, 152, 55) ||
        !mir_heap_pop_local_load(157, objects[4]) ||
        !mir_heap_pop_index(158, 156, 157) ||
        !mir_heap_pop_load(159, 158) ||
        !mir_heap_pop_store_indirect(160, 154, 159) ||
        !mir_heap_pop_local_load(163, objects[4]) ||
        !mir_heap_pop_index(164, 162, 163) ||
        !mir_heap_pop_store_indirect(166, 164, 149) ||
        !mir_heap_pop_local_load(168, objects[4]) ||
        !mir_heap_pop_local_store(170, 168, objects[1]) ||
        mir.insns[173].label != mir.insns[52].label ||
        mir.insns[176].src1 != mir.insns[6].dst)
        return 0;
    return 1;
}

static void mir_heap_pop_emit_base(MirStream *out)
{
    mir_stream_puts("\tld l,c\n\tld h,b\n", out);
}

static void mir_heap_pop_emit_size_address(MirStream *out)
{
    mir_heap_pop_emit_base(out);
    mir_stream_puts("\tld de,128\n\tadd hl,de\n", out);
}

static void mir_heap_pop_emit_frame_word(
    MirStream *out, int frame_offset)
{
    mir_stream_printf(out,
            "\tld l,(ix%d)\n\tld h,(ix%d)\n",
            frame_offset, frame_offset + 1);
}

static void mir_heap_pop_emit_index_address(
    MirStream *out, int frame_offset)
{
    mir_heap_pop_emit_frame_word(out, frame_offset);
    mir_stream_puts("\tadd hl,hl\n\tadd hl,bc\n", out);
}

static void mir_heap_pop_emit_signed_ge_jump(
    MirStream *out, int label)
{
    mir_stream_puts("\tld a,h\n\txor 80h\n\tld h,a\n"
          "\tld a,d\n\txor 80h\n\tld d,a\n"
          "\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp nc,L%d\n", label);
}

static void mir_heap_pop_emit_child(
    MirStream *out, int addend, int done_label)
{
    mir_heap_pop_emit_frame_word(out, -4);
    mir_stream_puts("\tadd hl,hl\n", out);
    if (addend >= 1)
        mir_stream_puts("\tinc hl\n", out);
    if (addend >= 2)
        mir_stream_puts("\tinc hl\n", out);
    mir_stream_puts("\tpush hl\n", out);
    mir_heap_pop_emit_size_address(out);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tpop hl\n", out);
    mir_heap_pop_emit_signed_ge_jump(out, done_label);

    mir_heap_pop_emit_frame_word(out, -4);
    mir_stream_puts("\tadd hl,hl\n", out);
    if (addend >= 1)
        mir_stream_puts("\tinc hl\n", out);
    if (addend >= 2)
        mir_stream_puts("\tinc hl\n", out);
    mir_stream_puts("\tpush hl\n\tadd hl,hl\n\tadd hl,bc\n"
          "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n", out);
    mir_heap_pop_emit_index_address(out, -6);
    mir_stream_puts("\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n"
          "\tld a,d\n\txor 80h\n\tld d,a\n"
          "\tld a,h\n\txor 80h\n\tld h,a\n"
          "\tex de,hl\n\tor a\n\tsbc hl,de\n"
          "\tpop hl\n", out);
    mir_stream_printf(out, "\tjp nc,L%d\n", done_label);
    mir_stream_puts("\tld (ix-6),l\n\tld (ix-5),h\n", out);
}

static void mir_emit_heap_pop(
    MirStream *out, const struct MirHeapPopPlan *plan)
{
    int loop = new_label();
    int left_done = new_label();
    int right_done = new_label();
    int done = new_label();

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-6\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld c,(ix+%d)\n\tld b,(ix+%d)\n",
            plan->parameter_stack_offset + 2,
            plan->parameter_stack_offset + 3);

    mir_heap_pop_emit_base(out);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld (ix-2),e\n\tld (ix-1),d\n", out);

    mir_heap_pop_emit_size_address(out);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tdec de\n\tld (hl),d\n\tdec hl\n\tld (hl),e\n",
          out);
    mir_heap_pop_emit_size_address(out);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n"
          "\tadd hl,hl\n\tadd hl,bc\n"
          "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n",
          out);
    mir_heap_pop_emit_base(out);
    mir_stream_puts("\tld (hl),e\n\tinc hl\n\tld (hl),d\n"
          "\txor a\n\tld (ix-4),a\n\tld (ix-3),a\n", out);

    mir_stream_printf(out, "L%d:\n", loop);
    mir_stream_puts("\tld l,(ix-4)\n\tld h,(ix-3)\n"
          "\tld (ix-6),l\n\tld (ix-5),h\n", out);
    mir_heap_pop_emit_child(out, 1, left_done);
    mir_stream_printf(out, "L%d:\n", left_done);
    mir_heap_pop_emit_child(out, 2, right_done);
    mir_stream_printf(out, "L%d:\n", right_done);

    mir_stream_puts("\tld l,(ix-6)\n\tld h,(ix-5)\n"
          "\tld e,(ix-4)\n\tld d,(ix-3)\n"
          "\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", done);

    mir_heap_pop_emit_index_address(out, -4);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n",
          out);
    mir_heap_pop_emit_index_address(out, -6);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n",
          out);
    mir_heap_pop_emit_index_address(out, -4);
    mir_stream_puts("\tpop de\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n",
          out);
    mir_heap_pop_emit_index_address(out, -6);
    mir_stream_puts("\tpop de\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n"
          "\tld l,(ix-6)\n\tld h,(ix-5)\n"
          "\tld (ix-4),l\n\tld (ix-3),h\n", out);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", loop, done);
    mir_stream_puts("\tld l,(ix-2)\n\tld h,(ix-1)\n"
          "\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_vla_fill_call_signed_word_type(int type)
{
    return type_ptr_depth(type) == 0 &&
        !type_is_float(type) &&
        (type & 15) == TYPE_INT &&
        (type & TYPE_UNSIGNED) == 0 &&
        type_size(type) == 2;
}

static int mir_vla_fill_call_word_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
        !type_is_float(type) &&
        (type & 15) == TYPE_INT &&
        (type & TYPE_UNSIGNED) == 0 &&
        type_size(type) == 2;
}

static int mir_match_vla_fill_call_schedule(
    struct MirVlaFillCallSchedule *plan)
{
    static const int expected_opcodes[57] = {
        MIR_LABEL, MIR_PARAM, MIR_VLA_SAVE, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_VLA_ALLOC, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP,
        MIR_PHI, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_NOP, MIR_LOAD,
        MIR_BINARY, MIR_STORE_INDIRECT, MIR_LABEL, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_RETURN
    };
    const struct MirInsn *rows = &mir.insns[1];
    const struct MirInsn *allocation = &mir.insns[6];
    const struct MirInsn *outer_store = &mir.insns[9];
    const struct MirInsn *outer_phi = &mir.insns[12];
    const struct MirInsn *inner_store = &mir.insns[19];
    const struct MirInsn *call = &mir.insns[55];
    int arguments[2];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 57 || mir_cfg_block_count() != 7 ||
        !mir.has_vla || mir.local_bytes != 10 ||
        !mir_vla_fill_call_signed_word_type(mir.return_type))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "vla-fill-call-schedule", "opcodes");
        if ((insn->opcode == MIR_LOAD ||
             insn->opcode == MIR_STORE ||
             insn->opcode == MIR_PARAM) &&
            !mir_machine_named_nonvolatile(insn))
            return mir_machine_reject(
                "vla-fill-call-schedule", "volatile-named-memory");
        if ((insn->opcode == MIR_LOAD_INDIRECT ||
             insn->opcode == MIR_STORE_INDIRECT) &&
            ((insn->memory_flags & (1 | 8)) != 0 ||
             insn->bit_width != 0))
            return mir_machine_reject(
                "vla-fill-call-schedule", "volatile-indirect-memory");
    }

    if (!mir_machine_parameter_value_offset(
            rows->dst, &plan->rows_stack_offset) ||
        !mir_vla_fill_call_signed_word_type(rows->type) ||
        rows->object < 0 ||
        mir.insns[3].object != rows->object ||
        !mir_machine_constant_equals(mir.insns[4].dst, 6) ||
        mir.insns[5].src1 != rows->dst ||
        mir.insns[5].src2 != mir.insns[4].dst ||
        mir.insns[5].immediate != '*' ||
        !mir_vla_fill_call_signed_word_type(mir.insns[5].type) ||
        allocation->src1 != mir.insns[5].dst ||
        allocation->memory_size != 6 ||
        allocation->name[0] == '\0' ||
        !mir_declared_is_vla_object(allocation->name))
        return mir_machine_reject(
            "vla-fill-call-schedule", "allocation");

    if (!mir_machine_constant_equals(mir.insns[7].dst, 0) ||
        !mir_machine_unobservable_local_store(outer_store) ||
        outer_store->src1 != mir.insns[7].dst ||
        outer_store->object < 0 ||
        outer_phi->object != outer_store->object ||
        !mir_machine_same_location(outer_phi, outer_store) ||
        outer_phi->src1 != mir.insns[7].dst ||
        outer_phi->src2 != mir.insns[47].dst ||
        outer_phi->phi_pred1 != mir.insns[0].label ||
        outer_phi->phi_pred2 != mir.insns[44].label ||
        mir.insns[15].src1 != outer_phi->dst ||
        mir.insns[15].src2 != rows->dst ||
        mir.insns[15].immediate != '<' ||
        !mir_vla_fill_call_signed_word_type(mir.insns[15].type) ||
        !mir_vla_fill_call_signed_word_type(
            mir.insns[15].secondary_offset) ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[16].label != mir.insns[50].label ||
        !mir_machine_constant_equals(mir.insns[46].dst, 1) ||
        mir.insns[47].src1 != outer_phi->dst ||
        mir.insns[47].src2 != mir.insns[46].dst ||
        mir.insns[47].immediate != '+' ||
        !mir_vla_fill_call_signed_word_type(mir.insns[47].type) ||
        !mir_vla_fill_call_signed_word_type(
            mir.insns[47].secondary_offset) ||
        !mir_machine_same_location(&mir.insns[48], outer_store) ||
        mir.insns[48].src1 != mir.insns[47].dst ||
        mir.insns[49].label != mir.insns[10].label)
        return mir_machine_reject(
            "vla-fill-call-schedule", "outer-loop");

    if (!mir_machine_constant_equals(mir.insns[17].dst, 0) ||
        !mir_machine_unobservable_local_store(inner_store) ||
        inner_store->src1 != mir.insns[17].dst ||
        inner_store->object < 0 ||
        inner_store->object == outer_store->object ||
        !mir_machine_same_location(&mir.insns[24], inner_store) ||
        !mir_machine_constant_equals(mir.insns[25].dst, 3) ||
        mir.insns[26].src1 != mir.insns[24].dst ||
        mir.insns[26].src2 != mir.insns[25].dst ||
        mir.insns[26].immediate != '<' ||
        !mir_vla_fill_call_signed_word_type(mir.insns[26].type) ||
        !mir_vla_fill_call_signed_word_type(
            mir.insns[26].secondary_offset) ||
        mir.insns[27].src1 != mir.insns[26].dst ||
        mir.insns[27].label != mir.insns[43].label ||
        !mir_machine_same_location(&mir.insns[38], inner_store) ||
        !mir_machine_constant_equals(mir.insns[39].dst, 1) ||
        mir.insns[40].src1 != mir.insns[38].dst ||
        mir.insns[40].src2 != mir.insns[39].dst ||
        mir.insns[40].immediate != '+' ||
        !mir_vla_fill_call_signed_word_type(mir.insns[40].type) ||
        !mir_vla_fill_call_signed_word_type(
            mir.insns[40].secondary_offset) ||
        !mir_machine_same_location(&mir.insns[41], inner_store) ||
        mir.insns[41].src1 != mir.insns[40].dst ||
        mir.insns[42].label != mir.insns[20].label)
        return mir_machine_reject(
            "vla-fill-call-schedule", "inner-loop");

    if (mir.insns[28].name[0] == '\0' ||
        strcmp(mir.insns[28].name, allocation->name) ||
        mir.insns[30].src1 != mir.insns[28].dst ||
        mir.insns[30].src2 != outer_phi->dst ||
        mir.insns[30].immediate != 6 ||
        mir.insns[30].memory_size != 2 ||
        !mir_machine_same_location(&mir.insns[31], inner_store) ||
        mir.insns[32].src1 != mir.insns[30].dst ||
        mir.insns[32].src2 != mir.insns[31].dst ||
        mir.insns[32].immediate != 2 ||
        mir.insns[32].memory_size != 2 ||
        !mir_machine_same_location(&mir.insns[34], inner_store) ||
        mir.insns[35].src1 != outer_phi->dst ||
        mir.insns[35].src2 != mir.insns[34].dst ||
        mir.insns[35].immediate != '+' ||
        !mir_vla_fill_call_signed_word_type(mir.insns[35].type) ||
        !mir_vla_fill_call_signed_word_type(
            mir.insns[35].secondary_offset) ||
        mir.insns[36].src1 != mir.insns[32].dst ||
        mir.insns[36].src2 != mir.insns[35].dst ||
        mir.insns[36].memory_size != 2)
        return mir_machine_reject(
            "vla-fill-call-schedule", "fill");

    plan->callee = find_global(call->name);
    if (plan->callee == NULL ||
        plan->callee->storage != SC_FUNC ||
        !plan->callee->is_defined ||
        plan->callee->is_funcptr ||
        !plan->callee->has_proto ||
        plan->callee->proto_nargs != 2 ||
        plan->callee->proto_variadic ||
        !mir_vla_fill_call_signed_word_type(plan->callee->type) ||
        !mir_vla_fill_call_signed_word_type(
            plan->callee->proto_types[0]) ||
        !mir_vla_fill_call_word_pointer_type(
            plan->callee->proto_types[1]) ||
        !mir_vla_fill_call_signed_word_type(call->type) ||
        !mir_machine_two_call_arguments(call, arguments) ||
        arguments[0] != rows->dst ||
        arguments[1] != mir.insns[53].dst ||
        mir.insns[53].name[0] == '\0' ||
        strcmp(mir.insns[53].name, allocation->name) ||
        mir.insns[56].src1 != call->dst)
        return mir_machine_reject(
            "vla-fill-call-schedule", "call");
    return 1;
}

static void mir_emit_vla_fill_call_schedule(
    MirStream *out, const struct MirVlaFillCallSchedule *plan)
{
    int fill = new_label();
    int filled = new_label();
    int rows_offset = plan->rows_stack_offset + 2;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tdec sp\n\tdec sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            rows_offset, rows_offset + 1);
    mir_machine_emit_vla_allocate_rows(out, 6);
    mir_stream_puts("\tld hl,0\n\tadd hl,sp\n"
          "\tld (ix-2),l\n\tld (ix-1),h\n", out);
    mir_stream_printf(out,
            "\tld c,(ix%+d)\n\tld b,(ix%+d)\n"
            "\tbit 7,b\n\tjp nz,L%d\n"
            "\tld a,b\n\tor c\n\tjp z,L%d\n"
            "\tld l,(ix-2)\n\tld h,(ix-1)\n"
            "\tld de,0\n"
            "L%d:\n"
            "\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tinc hl\n"
            "\tinc de\n"
            "\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tinc hl\n"
            "\tinc de\n"
            "\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tinc hl\n"
            "\tdec de\n"
            "\tdec bc\n\tld a,b\n\tor c\n\tjp nz,L%d\n"
            "L%d:\n"
            "\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n",
            rows_offset, rows_offset + 1,
            filled, filled, fill, fill, filled,
            rows_offset, rows_offset + 1);
    mir_machine_emit_symbol_call(out, plan->callee);
    mir_stream_puts("\tpop bc\n\tpop bc\n"
          "\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_matrix_signed_word_type(int type)
{
    return type_ptr_depth(type) == 0 &&
        !type_is_float(type) &&
        (type & 15) == TYPE_INT &&
        (type & TYPE_UNSIGNED) == 0 &&
        type_size(type) == 2;
}

static int mir_matrix_word_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
        !type_is_float(type) &&
        (type & 15) == TYPE_INT &&
        (type & TYPE_UNSIGNED) == 0 &&
        type_size(type) == 2;
}

static int mir_matrix_aggregate_parameter(
    const struct MirInsn *parameter, int *stack_offset_out)
{
    int memory_type;
    int memory_storage;
    int memory_offset;

    if (parameter->opcode != MIR_PARAM ||
        !mir_machine_named_nonvolatile(parameter) ||
        !mir_scalar_memory_location(
            parameter, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM ||
        type_size(memory_type) != 8 ||
        memory_offset < 2)
        return 0;
    *stack_offset_out = memory_offset - 2;
    return 1;
}

static int mir_matrix_address_location(
    const struct MirInsn *address, int storage, int *offset_out)
{
    int memory_type;
    int memory_storage;
    int memory_offset;

    if (address->opcode != MIR_ADDRESS ||
        !mir_machine_named_nonvolatile(address) ||
        (address->memory_flags & (1 | 8)) != 0 ||
        !mir_scalar_memory_location(
            address, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != storage ||
        type_size(memory_type) != 8)
        return 0;
    *offset_out = memory_offset;
    return 1;
}

static int mir_matrix_member_address(
    int instruction, int base)
{
    const struct MirInsn *member = &mir.insns[instruction];

    return member->opcode == MIR_MEMBER_ADDRESS &&
        member->src1 == mir.insns[base].dst &&
        member->immediate == 0 &&
        member->memory_size == 8 &&
        (member->memory_flags & (1 | 8)) == 0 &&
        mir_matrix_word_pointer_type(member->type);
}

static int mir_matrix_index_address(
    int instruction, int base, int index, int stride)
{
    const struct MirInsn *address = &mir.insns[instruction];

    return address->opcode == MIR_INDEX_ADDRESS &&
        address->src1 == mir.insns[base].dst &&
        address->src2 == mir.insns[index].dst &&
        address->immediate == stride &&
        address->memory_size == 2 &&
        (address->memory_flags & (1 | 8)) == 0 &&
        mir_matrix_word_pointer_type(address->type);
}

static int mir_matrix_word_load(int instruction, int address)
{
    const struct MirInsn *load = &mir.insns[instruction];

    return load->opcode == MIR_LOAD_INDIRECT &&
        load->src1 == mir.insns[address].dst &&
        load->memory_size == 2 &&
        load->bit_width == 0 &&
        (load->memory_flags & (1 | 8)) == 0 &&
        mir_matrix_signed_word_type(load->type);
}

static int mir_matrix_word_store(
    int instruction, int address, int value)
{
    const struct MirInsn *store = &mir.insns[instruction];

    return store->opcode == MIR_STORE_INDIRECT &&
        store->src1 == mir.insns[address].dst &&
        store->src2 == mir.insns[value].dst &&
        store->memory_size == 2 &&
        store->bit_width == 0 &&
        (store->memory_flags & (1 | 8)) == 0;
}

static int mir_match_matrix_multiply_schedule(
    struct MirMatrixMultiplySchedule *plan)
{
    static const int expected_opcodes[89] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_CONST, MIR_STORE, MIR_CONST,
        MIR_STORE, MIR_CONST, MIR_STORE, MIR_CONST, MIR_STORE, MIR_NOP,
        MIR_CONST, MIR_STORE, MIR_LABEL, MIR_PHI, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP,
        MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD, MIR_INDEX_ADDRESS,
        MIR_LOAD, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY,
        MIR_BINARY, MIR_STORE_INDIRECT, MIR_LABEL, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_LABEL, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL,
        MIR_ADDRESS, MIR_RETURN
    };
    const struct MirInsn *left = &mir.insns[1];
    const struct MirInsn *right = &mir.insns[2];
    const struct MirInsn *outer_store = &mir.insns[13];
    const struct MirInsn *outer_phi = &mir.insns[15];
    const struct MirInsn *middle_store = &mir.insns[23];
    const struct MirInsn *inner_store = &mir.insns[33];
    int result_offsets[5];
    int left_offset;
    int right_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 89 || mir_cfg_block_count() != 10 ||
        mir.has_vla || mir.local_bytes != 15 ||
        mir.aggregate_temp_bytes != 0 ||
        type_size(mir.return_type) != 8)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "matrix-multiply-schedule", "opcodes");
        if ((insn->opcode == MIR_LOAD ||
             insn->opcode == MIR_STORE ||
             insn->opcode == MIR_PARAM ||
             insn->opcode == MIR_ADDRESS) &&
            !mir_machine_named_nonvolatile(insn))
            return mir_machine_reject(
                "matrix-multiply-schedule", "volatile-named-memory");
        if ((insn->opcode == MIR_LOAD_INDIRECT ||
             insn->opcode == MIR_STORE_INDIRECT) &&
            ((insn->memory_flags & (1 | 8)) != 0 ||
             insn->bit_width != 0 ||
             insn->memory_size != 2))
            return mir_machine_reject(
                "matrix-multiply-schedule", "volatile-indirect-memory");
    }

    if (!mir_matrix_aggregate_parameter(
            left, &plan->left_stack_offset) ||
        !mir_matrix_aggregate_parameter(
            right, &plan->right_stack_offset) ||
        plan->left_stack_offset != 4 ||
        plan->right_stack_offset != plan->left_stack_offset + 8)
        return mir_machine_reject(
            "matrix-multiply-schedule", "parameters");

    if (!mir_matrix_address_location(
            &mir.insns[42], SC_LOCAL, &result_offsets[0]) ||
        !mir_matrix_address_location(
            &mir.insns[87], SC_LOCAL, &result_offsets[4]) ||
        result_offsets[4] != result_offsets[0] ||
        !mir_machine_same_location(&mir.insns[42], &mir.insns[87]))
        return mir_machine_reject(
            "matrix-multiply-schedule", "result-location");
    for (instruction = 0; instruction < 4; ++instruction) {
        const struct MirInsn *constant = &mir.insns[3 + instruction * 2];
        const struct MirInsn *store = &mir.insns[4 + instruction * 2];
        int memory_type;
        int memory_storage;

        if (!mir_machine_constant_equals(constant->dst, 0) ||
            store->opcode != MIR_STORE ||
            store->src1 != constant->dst ||
            store->memory_size != 2 ||
            !mir_scalar_memory_location(
                store, &memory_type, &memory_storage,
                &result_offsets[instruction]) ||
            memory_storage != SC_LOCAL ||
            type_size(memory_type) != 8)
            return mir_machine_reject(
                "matrix-multiply-schedule", "result-initializers");
        if (instruction > 0 &&
            result_offsets[instruction] !=
                result_offsets[0] + instruction * 2)
            return mir_machine_reject(
                "matrix-multiply-schedule", "result-layout");
    }

    if (!mir_machine_constant_equals(mir.insns[12].dst, 0) ||
        !mir_machine_unobservable_local_store(outer_store) ||
        outer_store->src1 != mir.insns[12].dst ||
        outer_store->object < 0 ||
        outer_phi->object != outer_store->object ||
        !mir_machine_same_location(outer_phi, outer_store) ||
        outer_phi->src1 != mir.insns[12].dst ||
        outer_phi->src2 != mir.insns[83].dst ||
        outer_phi->phi_pred1 != mir.insns[0].label ||
        outer_phi->phi_pred2 != mir.insns[80].label ||
        !mir_machine_constant_equals(mir.insns[17].dst, 2) ||
        mir.insns[18].src1 != outer_phi->dst ||
        mir.insns[19].src1 != mir.insns[18].dst ||
        mir.insns[19].src2 != mir.insns[17].dst ||
        mir.insns[19].immediate != '<' ||
        mir.insns[20].src1 != mir.insns[19].dst ||
        mir.insns[20].label != mir.insns[86].label)
        return mir_machine_reject(
            "matrix-multiply-schedule", "outer-loop");

    if (!mir_machine_constant_equals(mir.insns[21].dst, 0) ||
        !mir_machine_unobservable_local_store(middle_store) ||
        middle_store->src1 != mir.insns[21].dst ||
        middle_store->object < 0 ||
        !mir_machine_constant_equals(mir.insns[28].dst, 2) ||
        mir.insns[29].src1 != mir.insns[27].dst ||
        mir.insns[29].src2 != mir.insns[28].dst ||
        mir.insns[29].immediate != '<' ||
        mir.insns[30].src1 != mir.insns[29].dst ||
        mir.insns[30].label != mir.insns[79].label ||
        !mir_machine_constant_equals(mir.insns[31].dst, 0) ||
        !mir_machine_unobservable_local_store(inner_store) ||
        inner_store->src1 != mir.insns[31].dst ||
        inner_store->object < 0 ||
        inner_store->object == middle_store->object ||
        !mir_machine_constant_equals(mir.insns[39].dst, 2) ||
        mir.insns[40].src1 != mir.insns[38].dst ||
        mir.insns[40].src2 != mir.insns[39].dst ||
        mir.insns[40].immediate != '<' ||
        mir.insns[41].src1 != mir.insns[40].dst ||
        mir.insns[41].label != mir.insns[72].label)
        return mir_machine_reject(
            "matrix-multiply-schedule", "inner-loops");

    if (!mir_matrix_member_address(43, 42) ||
        !mir_matrix_index_address(45, 43, 15, 4) ||
        mir.insns[46].object != middle_store->object ||
        !mir_matrix_index_address(47, 45, 46, 2) ||
        !mir_matrix_word_load(48, 47) ||
        !mir_matrix_address_location(
            &mir.insns[49], SC_PARAM, &left_offset) ||
        left_offset - 2 != plan->left_stack_offset ||
        !mir_matrix_member_address(50, 49) ||
        !mir_matrix_index_address(52, 50, 15, 4) ||
        mir.insns[53].object != inner_store->object ||
        !mir_matrix_index_address(54, 52, 53, 2) ||
        !mir_matrix_word_load(55, 54) ||
        !mir_matrix_address_location(
            &mir.insns[56], SC_PARAM, &right_offset) ||
        right_offset - 2 != plan->right_stack_offset ||
        !mir_matrix_member_address(57, 56) ||
        mir.insns[58].object != inner_store->object ||
        !mir_matrix_index_address(59, 57, 58, 4) ||
        mir.insns[60].object != middle_store->object ||
        !mir_matrix_index_address(61, 59, 60, 2) ||
        !mir_matrix_word_load(62, 61) ||
        mir.insns[63].src1 != mir.insns[55].dst ||
        mir.insns[63].src2 != mir.insns[62].dst ||
        mir.insns[63].immediate != '*' ||
        !mir_matrix_signed_word_type(mir.insns[63].type) ||
        mir.insns[64].src1 != mir.insns[48].dst ||
        mir.insns[64].src2 != mir.insns[63].dst ||
        mir.insns[64].immediate != '+' ||
        !mir_matrix_signed_word_type(mir.insns[64].type) ||
        !mir_matrix_word_store(65, 47, 64))
        return mir_machine_reject(
            "matrix-multiply-schedule", "matrix-update");

    if (!mir_machine_constant_equals(mir.insns[68].dst, 1) ||
        mir.insns[69].src1 != mir.insns[67].dst ||
        mir.insns[69].src2 != mir.insns[68].dst ||
        mir.insns[69].immediate != '+' ||
        !mir_machine_same_location(&mir.insns[70], inner_store) ||
        mir.insns[70].src1 != mir.insns[69].dst ||
        mir.insns[71].label != mir.insns[34].label ||
        !mir_machine_constant_equals(mir.insns[75].dst, 1) ||
        mir.insns[76].src1 != mir.insns[74].dst ||
        mir.insns[76].src2 != mir.insns[75].dst ||
        mir.insns[76].immediate != '+' ||
        !mir_machine_same_location(&mir.insns[77], middle_store) ||
        mir.insns[77].src1 != mir.insns[76].dst ||
        mir.insns[78].label != mir.insns[24].label ||
        !mir_machine_constant_equals(mir.insns[82].dst, 1) ||
        mir.insns[83].src1 != outer_phi->dst ||
        mir.insns[83].src2 != mir.insns[82].dst ||
        mir.insns[83].immediate != '+' ||
        !mir_machine_same_location(&mir.insns[84], outer_store) ||
        mir.insns[84].src1 != mir.insns[83].dst ||
        mir.insns[85].label != mir.insns[14].label ||
        mir.insns[88].src1 != mir.insns[87].dst)
        return mir_machine_reject(
            "matrix-multiply-schedule", "updates-return");
    return 1;
}

static void mir_emit_matrix_product(
    MirStream *out, int left_first, int right_first,
    int left_second, int right_second, int result_offset)
{
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
            left_first, left_first + 1,
            right_first, right_first + 1);
    mir_emit_runtime_call(out, "__mulu");
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
            left_second, left_second + 1,
            right_second, right_second + 1);
    mir_emit_runtime_call(out, "__mulu");
    mir_stream_printf(out,
            "\tpop de\n\tadd hl,de\n"
            "\tld (iy+%d),l\n\tld (iy+%d),h\n",
            result_offset, result_offset + 1);
}

static void mir_emit_matrix_multiply_schedule(
    MirStream *out, const struct MirMatrixMultiplySchedule *plan)
{
    int left = plan->left_stack_offset + 4;
    int right = plan->right_stack_offset + 4;

    mir_stream_puts(";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\tld l,(ix+6)\n\tld h,(ix+7)\n\tpush hl\n\tpop iy\n",
          out);
    mir_emit_matrix_product(
        out, left, right, left + 2, right + 4, 0);
    mir_emit_matrix_product(
        out, left, right + 2, left + 2, right + 6, 2);
    mir_emit_matrix_product(
        out, left + 4, right, left + 6, right + 4, 4);
    mir_emit_matrix_product(
        out, left + 4, right + 2, left + 6, right + 6, 6);
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tpop iy\n"
          ";@dcc.reg free=iy\n\tret\n", out);
}

static int mir_match_matrix_bitops_schedule(
    struct MirMatrixBitopsSchedule *plan)
{
    static const int expected_opcodes[89] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_PHI, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY,
        MIR_STORE_INDIRECT, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY,
        MIR_STORE_INDIRECT, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT, MIR_NOP, MIR_LABEL,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL
    };
    const struct MirInsn *matrix = &mir.insns[1];
    const struct MirInsn *outer_store = &mir.insns[4];
    const struct MirInsn *outer_phi = &mir.insns[7];
    const struct MirInsn *inner_store = &mir.insns[15];
    static const int member_indices[5] = { 25, 35, 45, 55, 65 };
    static const int row_indices[5] = { 27, 37, 47, 57, 67 };
    static const int column_loads[5] = { 28, 38, 48, 58, 68 };
    static const int column_indices[5] = { 29, 39, 49, 59, 69 };
    static const int value_loads[5] = { 30, 40, 50, 60, 70 };
    static const int constants[5] = { 31, 41, 51, 61, 71 };
    static const int binaries[5] = { 32, 42, 52, 62, 72 };
    static const int stores[5] = { 33, 43, 53, 63, 73 };
    static const int operations[5] = { '*', '-', '|', '&', '^' };
    static const long expected_constants[5] = { 3, 1, 256, 511, 2 };
    int instruction;
    int operation;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 89 || mir_cfg_block_count() != 7 ||
        mir.has_vla || mir.local_bytes != 3 ||
        mir.aggregate_temp_bytes != 0 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "matrix-bitops-schedule", "opcodes");
        if ((insn->opcode == MIR_LOAD ||
             insn->opcode == MIR_STORE ||
             insn->opcode == MIR_PARAM) &&
            !mir_machine_named_nonvolatile(insn))
            return mir_machine_reject(
                "matrix-bitops-schedule", "volatile-named-memory");
        if ((insn->opcode == MIR_LOAD_INDIRECT ||
             insn->opcode == MIR_STORE_INDIRECT) &&
            ((insn->memory_flags & (1 | 8)) != 0 ||
             insn->bit_width != 0 ||
             insn->memory_size != 2))
            return mir_machine_reject(
                "matrix-bitops-schedule", "volatile-indirect-memory");
    }

    if (!mir_machine_parameter_value_offset(
            matrix->dst, &plan->matrix_stack_offset) ||
        !mir_machine_named_nonvolatile(matrix) ||
        mir_machine_pointee_is_volatile(matrix) ||
        matrix->object < 0 ||
        type_ptr_depth(matrix->type) != 1 ||
        type_size(matrix->type) != 2)
        return mir_machine_reject(
            "matrix-bitops-schedule", "parameter");

    if (!mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_machine_unobservable_local_store(outer_store) ||
        outer_store->src1 != mir.insns[3].dst ||
        outer_store->object < 0 ||
        outer_phi->object != outer_store->object ||
        !mir_machine_same_location(outer_phi, outer_store) ||
        outer_phi->src1 != mir.insns[3].dst ||
        outer_phi->src2 != mir.insns[85].dst ||
        outer_phi->phi_pred1 != mir.insns[0].label ||
        outer_phi->phi_pred2 != mir.insns[82].label ||
        !mir_machine_constant_equals(mir.insns[9].dst, 2) ||
        mir.insns[10].src1 != outer_phi->dst ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[11].src2 != mir.insns[9].dst ||
        mir.insns[11].immediate != '<' ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[12].label != mir.insns[88].label)
        return mir_machine_reject(
            "matrix-bitops-schedule", "outer-loop");

    if (!mir_machine_constant_equals(mir.insns[13].dst, 0) ||
        !mir_machine_unobservable_local_store(inner_store) ||
        inner_store->src1 != mir.insns[13].dst ||
        inner_store->object < 0 ||
        inner_store->object == outer_store->object ||
        !mir_machine_constant_equals(mir.insns[21].dst, 2) ||
        mir.insns[22].src1 != mir.insns[20].dst ||
        mir.insns[22].src2 != mir.insns[21].dst ||
        mir.insns[22].immediate != '<' ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[23].label != mir.insns[81].label)
        return mir_machine_reject(
            "matrix-bitops-schedule", "inner-loop");

    for (operation = 0; operation < 5; ++operation) {
        if (!mir_matrix_member_address(
                member_indices[operation], 1) ||
            !mir_matrix_index_address(
                row_indices[operation],
                member_indices[operation], 7, 4) ||
            mir.insns[column_loads[operation]].object !=
                inner_store->object ||
            !mir_matrix_index_address(
                column_indices[operation],
                row_indices[operation],
                column_loads[operation], 2) ||
            !mir_matrix_word_load(
                value_loads[operation],
                column_indices[operation]) ||
            !mir_machine_constant_equals(
                mir.insns[constants[operation]].dst,
                expected_constants[operation]) ||
            mir.insns[binaries[operation]].src1 !=
                mir.insns[value_loads[operation]].dst ||
            mir.insns[binaries[operation]].src2 !=
                mir.insns[constants[operation]].dst ||
            mir.insns[binaries[operation]].immediate !=
                operations[operation] ||
            !mir_matrix_signed_word_type(
                mir.insns[binaries[operation]].type) ||
            !mir_matrix_word_store(
                stores[operation],
                column_indices[operation],
                binaries[operation]))
            return mir_machine_reject(
                "matrix-bitops-schedule", "updates");
    }

    if (!mir_machine_constant_equals(mir.insns[77].dst, 1) ||
        mir.insns[78].src1 != mir.insns[76].dst ||
        mir.insns[78].src2 != mir.insns[77].dst ||
        mir.insns[78].immediate != '+' ||
        !mir_machine_same_location(&mir.insns[79], inner_store) ||
        mir.insns[79].src1 != mir.insns[78].dst ||
        mir.insns[80].label != mir.insns[16].label ||
        !mir_machine_constant_equals(mir.insns[84].dst, 1) ||
        mir.insns[85].src1 != outer_phi->dst ||
        mir.insns[85].src2 != mir.insns[84].dst ||
        mir.insns[85].immediate != '+' ||
        !mir_machine_same_location(&mir.insns[86], outer_store) ||
        mir.insns[86].src1 != mir.insns[85].dst ||
        mir.insns[87].label != mir.insns[5].label)
        return mir_machine_reject(
            "matrix-bitops-schedule", "updates");
    return 1;
}

static void mir_emit_matrix_bitops_schedule(
    MirStream *out, const struct MirMatrixBitopsSchedule *plan)
{
    int loop = new_label();

    mir_stream_puts(";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tpush de\n\tpop iy\n\tld b,4\n"
            "L%d:\n"
            "\tld l,(iy+0)\n\tld h,(iy+1)\n"
            "\tld e,l\n\tld d,h\n"
            "\tadd hl,hl\n\tadd hl,de\n\tdec hl\n"
            "\tset 0,h\n\tld a,h\n\tand 1\n\tld h,a\n"
            "\tld a,l\n\txor 2\n\tld l,a\n"
            "\tld (iy+0),l\n\tld (iy+1),h\n"
            "\tinc iy\n\tinc iy\n\tdjnz L%d\n"
            "\tpop iy\n;@dcc.reg free=iy\n\tret\n",
            plan->matrix_stack_offset + 2, loop, loop);
}

static int mir_board_print_signed_word_type(int type)
{
    return type_ptr_depth(type) == 0 &&
        !type_is_float(type) &&
        (type & 15) == TYPE_INT &&
        (type & TYPE_UNSIGNED) == 0 &&
        type_size(type) == 2;
}

static int mir_board_print_bool_type(int type)
{
    return type_ptr_depth(type) == 0 &&
        !type_is_float(type) &&
        (type & 15) == TYPE_BOOL &&
        type_size(type) == 1;
}

static int mir_board_print_word_location(
    const struct MirInsn *insn, int opcode, int storage,
    int *object_out)
{
    int memory_type;
    int memory_storage;
    int memory_offset;

    if (insn == NULL || insn->opcode != opcode ||
        insn->bit_width != 0 ||
        (insn->memory_flags & (1 | 8)) != 0 ||
        !mir_scalar_memory_location(
            insn, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != storage ||
        !mir_board_print_signed_word_type(memory_type) ||
        !mir_machine_named_nonvolatile(insn))
        return 0;
    if (object_out != NULL)
        *object_out = insn->object;
    return 1;
}

static int mir_board_print_same_word_location(
    const struct MirInsn *insn, int opcode, int storage,
    int object, const struct MirInsn *location)
{
    int actual_object;

    return mir_board_print_word_location(
               insn, opcode, storage, &actual_object) &&
        actual_object == object &&
        mir_machine_same_location(insn, location);
}

static int mir_board_print_call_arguments(
    const struct MirInsn *call, int expected_count, int *arguments)
{
    int count = 0;
    int instruction;
    int argument;

    for (argument = 0; argument < expected_count; ++argument)
        arguments[argument] = -1;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *arg = &mir.insns[instruction];

        if (arg->opcode != MIR_ARG ||
            arg->secondary_offset != call->secondary_offset)
            continue;
        argument = (int)arg->immediate;
        if (argument < 0 || argument >= expected_count ||
            arguments[argument] >= 0)
            return 0;
        arguments[argument] = arg->src1;
        ++count;
    }
    return count == expected_count;
}

static int mir_board_print_call_name(
    const struct MirInsn *call, struct Sym *function,
    char *name_out, size_t name_size)
{
    const char *name = call->base_name[0] != 0
        ? call->base_name
        : asm_name_for(sym_asm_name(function));

    if (name[0] == 0 || strlen(name) >= name_size)
        return 0;
    snprintf(name_out, name_size, "%s", name);
    return 1;
}

static int mir_match_board_matrix_print_schedule(
    struct MirBoardMatrixPrintSchedule *plan)
{
    static const int expected_opcodes[59] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_PHI,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_CONST, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_LOAD, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_ARG, MIR_CALL, MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL
    };
    const struct MirInsn *size = &mir.insns[1];
    const struct MirInsn *row_store = &mir.insns[8];
    const struct MirInsn *column_store = &mir.insns[19];
    const struct MirInsn *value_call = &mir.insns[37];
    const struct MirInsn *newline_call = &mir.insns[47];
    const struct MirInsn *final_newline_call = &mir.insns[58];
    struct Sym *board;
    struct Sym *newline_function;
    struct Sym *final_newline_function;
    long board_offset;
    int value_arguments[2];
    int newline_arguments[1];
    int final_newline_arguments[1];
    int row_object;
    int column_object;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 59 || mir_cfg_block_count() != 7 ||
        mir.has_vla || mir.local_bytes != 4 ||
        mir.aggregate_temp_bytes != 0 ||
        !mir_has_cfg_backedge() ||
        type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_VOID ||
        type_size(mir.return_type) != 0)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "board-matrix-print-schedule", "opcodes");
    if (mir.insns[16].label != mir.insns[55].label ||
        mir.insns[27].label != mir.insns[44].label ||
        mir.insns[43].label != mir.insns[20].label ||
        mir.insns[54].label != mir.insns[9].label)
        return mir_machine_reject(
            "board-matrix-print-schedule", "control-flow");

    if (!mir_board_print_signed_word_type(size->type) ||
        !mir_machine_parameter_value_offset(
            size->dst, &plan->size_stack_offset) ||
        plan->size_stack_offset < 2 ||
        plan->size_stack_offset + 1 > 127 ||
        !mir_board_print_word_location(
            row_store, MIR_STORE, SC_LOCAL, &row_object) ||
        !mir_board_print_word_location(
            column_store, MIR_STORE, SC_LOCAL, &column_object) ||
        row_object < 0 || column_object < 0 ||
        row_object == column_object)
        return mir_machine_reject(
            "board-matrix-print-schedule", "locations");

    if (!mir_machine_constant_equals(mir.insns[7].dst, 0) ||
        row_store->src1 != mir.insns[7].dst ||
        mir.insns[11].object != row_object ||
        mir.insns[11].src1 != mir.insns[7].dst ||
        mir.insns[11].src2 != mir.insns[52].dst ||
        mir.insns[11].phi_pred1 != mir.insns[0].label ||
        mir.insns[11].phi_pred2 != mir.insns[49].label ||
        mir.insns[15].src1 != mir.insns[11].dst ||
        mir.insns[15].src2 != size->dst ||
        mir.insns[15].immediate != '<' ||
        !mir_board_print_signed_word_type(mir.insns[15].type) ||
        !mir_board_print_signed_word_type(
            mir.insns[15].secondary_offset) ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        !mir_machine_constant_equals(mir.insns[18].dst, 0) ||
        column_store->src1 != mir.insns[18].dst ||
        !mir_board_print_same_word_location(
            &mir.insns[24], MIR_LOAD, SC_LOCAL,
            column_object, column_store) ||
        mir.insns[26].src1 != mir.insns[24].dst ||
        mir.insns[26].src2 != size->dst ||
        mir.insns[26].immediate != '<' ||
        !mir_board_print_signed_word_type(mir.insns[26].type) ||
        !mir_board_print_signed_word_type(
            mir.insns[26].secondary_offset) ||
        mir.insns[27].src1 != mir.insns[26].dst ||
        !mir_machine_constant_equals(mir.insns[40].dst, 1) ||
        mir.insns[41].src1 != mir.insns[39].dst ||
        mir.insns[41].src2 != mir.insns[40].dst ||
        mir.insns[41].immediate != '+' ||
        !mir_board_print_same_word_location(
            &mir.insns[42], MIR_STORE, SC_LOCAL,
            column_object, column_store) ||
        mir.insns[42].src1 != mir.insns[41].dst ||
        !mir_machine_constant_equals(mir.insns[51].dst, 1) ||
        mir.insns[52].src1 != mir.insns[11].dst ||
        mir.insns[52].src2 != mir.insns[51].dst ||
        mir.insns[52].immediate != '+' ||
        !mir_board_print_same_word_location(
            &mir.insns[53], MIR_STORE, SC_LOCAL,
            row_object, row_store) ||
        mir.insns[53].src1 != mir.insns[52].dst)
        return mir_machine_reject(
            "board-matrix-print-schedule", "loops");

    plan->row_stride = 8;
    if (!mir_machine_global_address_offset(
            mir.insns[30].dst, &board, &board_offset, 0) ||
        board_offset != 0 ||
        (board->storage != SC_GLOBAL &&
         board->storage != SC_EXTERN) ||
        !board->is_array || board->is_vla ||
        board->is_volatile || board->pointee_is_volatile ||
        board->array_len != plan->row_stride ||
        board->elem_size != plan->row_stride ||
        mir.insns[32].src1 != mir.insns[30].dst ||
        mir.insns[32].src2 != mir.insns[11].dst ||
        mir.insns[32].immediate != plan->row_stride ||
        mir.insns[32].memory_size != plan->row_stride ||
        mir.insns[34].src1 != mir.insns[32].dst ||
        mir.insns[34].src2 != mir.insns[33].dst ||
        mir.insns[34].immediate != 1 ||
        mir.insns[34].memory_size != 1 ||
        !mir_board_print_same_word_location(
            &mir.insns[33], MIR_LOAD, SC_LOCAL,
            column_object, column_store) ||
        mir.insns[35].src1 != mir.insns[34].dst ||
        mir.insns[35].memory_size != 1 ||
        mir.insns[35].bit_width != 0 ||
        (mir.insns[35].memory_flags & (1 | 8)) != 0 ||
        !mir_board_print_bool_type(mir.insns[35].type))
        return mir_machine_reject(
            "board-matrix-print-schedule", "board");
    plan->board = board;

    plan->print_function = find_global(value_call->name);
    newline_function = find_global(newline_call->name);
    final_newline_function = find_global(final_newline_call->name);
    if (plan->print_function == NULL ||
        plan->print_function->storage != SC_FUNC ||
        plan->print_function->is_funcptr ||
        !plan->print_function->has_proto ||
        !plan->print_function->proto_variadic ||
        newline_function != plan->print_function ||
        final_newline_function != plan->print_function ||
        (value_call->memory_flags & MIR_CALL_FLAG_VARIADIC) == 0 ||
        (newline_call->memory_flags & MIR_CALL_FLAG_VARIADIC) == 0 ||
        (final_newline_call->memory_flags &
         MIR_CALL_FLAG_VARIADIC) == 0 ||
        !mir_board_print_call_arguments(
            value_call, 2, value_arguments) ||
        value_arguments[0] != mir.insns[28].dst ||
        value_arguments[1] != mir.insns[35].dst ||
        !mir_board_print_call_arguments(
            newline_call, 1, newline_arguments) ||
        newline_arguments[0] != mir.insns[45].dst ||
        !mir_board_print_call_arguments(
            final_newline_call, 1, final_newline_arguments) ||
        final_newline_arguments[0] != mir.insns[56].dst ||
        mir.insns[28].type != (TYPE_CHAR | TYPE_PTR) ||
        mir.insns[45].type != (TYPE_CHAR | TYPE_PTR) ||
        mir.insns[56].type != (TYPE_CHAR | TYPE_PTR) ||
        mir.insns[45].immediate != mir.insns[56].immediate ||
        !mir_board_print_call_name(
            value_call, plan->print_function,
            plan->value_call_name,
            sizeof(plan->value_call_name)) ||
        !mir_board_print_call_name(
            newline_call, plan->print_function,
            plan->newline_call_name,
            sizeof(plan->newline_call_name)))
        return mir_machine_reject(
            "board-matrix-print-schedule", "print-calls");
    plan->value_string_id = (int)mir.insns[28].immediate;
    plan->newline_string_id = (int)mir.insns[45].immediate;
    return 1;
}

static void mir_emit_board_matrix_print_schedule(
    MirStream *out, const struct MirBoardMatrixPrintSchedule *plan)
{
    const char *board_name =
        asm_name_for(sym_asm_name(plan->board));
    int outer = new_label();
    int inner = new_label();
    int inner_body = new_label();
    int row_done = new_label();
    int done = new_label();
    int size_offset = plan->size_stack_offset + 4;

    if ((plan->board->storage == SC_EXTERN ||
         plan->board->needs_extrn) &&
        mir_extrn_should_emit(plan->board))
        mir_stream_printf(out, "\textrn %s\n", board_name);
    mir_stream_puts(";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tdec sp\n\tdec sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n"
            "\tbit 7,d\n\tjp nz, L%d\n"
            "\tld a,d\n\tor e\n\tjp z, L%d\n"
            "\tld (ix-2),0\n\tld (ix-1),0\n"
            "\tld iy,%s\n"
            "L%d:\n"
            "\tld l,(ix-2)\n\tld h,(ix-1)\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n"
            "\tor a\n\tsbc hl,de\n\tjp z, L%d\n"
            "\tld bc,0\n"
            "L%d:\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n"
            "\tld a,b\n\tcp d\n\tjp nz, L%d\n"
            "\tld a,c\n\tcp e\n\tjp z, L%d\n"
            "L%d:\n"
            "\tpush bc\n\tld l,(iy+0)\n\tld h,0\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            size_offset, size_offset + 1,
            done,
            done,
            board_name,
            outer,
            size_offset, size_offset + 1,
            done,
            inner,
            size_offset, size_offset + 1,
            inner_body, row_done,
            inner_body,
            plan->value_string_id);
    mir_aggregate_emit_format_call(
        out, plan->print_function, plan->value_call_name);
    mir_stream_printf(out,
            "\tpop de\n\tpop de\n\tpop bc\n"
            "\tinc iy\n\tinc bc\n\tjp L%d\n"
            "L%d:\n\tld hl,S%d\n\tpush hl\n",
            inner, row_done, plan->newline_string_id);
    mir_aggregate_emit_format_call(
        out, plan->print_function, plan->newline_call_name);
    mir_stream_printf(out,
            "\tpop bc\n"
            "\tld hl,%d\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n"
            "\tor a\n\tsbc hl,de\n"
            "\tpush iy\n\tpop de\n\tadd hl,de\n"
            "\tpush hl\n\tpop iy\n"
            "\tinc (ix-2)\n\tjp nz, L%d\n"
            "\tinc (ix-1)\n\tjp L%d\n"
            "L%d:\n\tld hl,S%d\n\tpush hl\n",
            plan->row_stride,
            size_offset, size_offset + 1,
            outer, outer,
            done, plan->newline_string_id);
    mir_aggregate_emit_format_call(
        out, plan->print_function, plan->newline_call_name);
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tpop iy\n"
          ";@dcc.reg free=iy\n\tret\n", out);
}

static int mir_byte_sum_signed_word_type(int type)
{
    return type_ptr_depth(type) == 0 &&
        !type_is_float(type) &&
        (type & 15) == TYPE_INT &&
        (type & TYPE_UNSIGNED) == 0 &&
        type_size(type) == 2;
}

static int mir_byte_sum_signed_byte_type(int type)
{
    return type_ptr_depth(type) == 0 &&
        !type_is_float(type) &&
        (type & 15) == TYPE_CHAR &&
        (type & TYPE_UNSIGNED) == 0 &&
        type_size(type) == 1;
}

static int mir_byte_sum_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
        !type_is_float(type) &&
        type_size(type) == 2;
}

static int mir_match_direct_byte_sum_loop_schedule(
    struct MirByteSumLoopSchedule *plan)
{
    static const int expected_opcodes[45] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_PHI, MIR_NOP, MIR_NOP, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_RETURN
    };
    const struct MirInsn *pointer = &mir.insns[1];
    const struct MirInsn *count = &mir.insns[2];
    const struct MirInsn *total_store = &mir.insns[5];
    const struct MirInsn *index_store = &mir.insns[8];
    const struct MirInsn *index_phi = &mir.insns[13];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 45 || mir_cfg_block_count() != 5 ||
        mir.has_vla ||
        !mir_byte_sum_signed_word_type(mir.return_type))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
                expected_opcodes[instruction])
            return mir_machine_reject(
                "byte-sum-loop-schedule", "direct-opcodes");
    if (!mir_machine_parameter_value_offset(
            pointer->dst, &plan->pointer_stack_offset) ||
        !mir_machine_parameter_value_offset(
            count->dst, &plan->count_stack_offset) ||
        plan->count_stack_offset != plan->pointer_stack_offset + 2 ||
        !mir_byte_sum_pointer_type(pointer->type) ||
        !mir_byte_sum_signed_word_type(count->type) ||
        !mir_machine_named_nonvolatile(pointer) ||
        !mir_machine_named_nonvolatile(count) ||
        mir_machine_pointee_is_volatile(pointer) ||
        pointer->object < 0 || count->object < 0 ||
        pointer->object == count->object ||
        mir.insns[10].object != pointer->object ||
        mir.insns[11].object != count->object ||
        mir.insns[15].object != count->object)
        return mir_machine_reject(
            "byte-sum-loop-schedule", "direct-parameters");
    if (!mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[6].dst, 0) ||
        !mir_machine_unobservable_local_store(total_store) ||
        !mir_machine_unobservable_local_store(index_store) ||
        total_store->src1 != mir.insns[3].dst ||
        index_store->src1 != mir.insns[6].dst ||
        total_store->object < 0 || index_store->object < 0 ||
        total_store->object == index_store->object ||
        index_phi->object != index_store->object ||
        !mir_machine_same_location(index_phi, index_store) ||
        index_phi->src1 != mir.insns[6].dst ||
        index_phi->src2 != mir.insns[39].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[36].label ||
        !mir_byte_sum_signed_word_type(index_phi->type))
        return mir_machine_reject(
            "byte-sum-loop-schedule", "direct-state");
    if (mir.insns[16].src1 != index_phi->dst ||
        mir.insns[16].src2 != count->dst ||
        mir.insns[16].immediate != '<' ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        mir.insns[17].label != mir.insns[42].label ||
        mir.insns[20].src1 != pointer->dst ||
        mir.insns[20].src2 != index_phi->dst ||
        mir.insns[20].immediate != 1 ||
        mir.insns[20].memory_size != 1 ||
        (mir.insns[20].memory_flags & (1 | 8)) != 0 ||
        mir.insns[21].src1 != mir.insns[20].dst ||
        mir.insns[21].memory_size != 1 ||
        !mir_byte_sum_signed_byte_type(mir.insns[21].type) ||
        (mir.insns[21].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_constant_equals(mir.insns[22].dst, 0) ||
        mir.insns[23].src1 != mir.insns[21].dst ||
        !mir_byte_sum_signed_word_type(mir.insns[23].type) ||
        mir.insns[24].src1 != mir.insns[23].dst ||
        mir.insns[24].src2 != mir.insns[22].dst ||
        mir.insns[24].immediate != TOK_NE ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        mir.insns[25].label != mir.insns[35].label)
        return mir_machine_reject(
            "byte-sum-loop-schedule", "direct-test");
    if (!mir_machine_same_location(&mir.insns[26], total_store) ||
        mir.insns[29].src1 != pointer->dst ||
        mir.insns[29].src2 != index_phi->dst ||
        mir.insns[29].immediate != 1 ||
        mir.insns[29].memory_size != 1 ||
        mir.insns[30].src1 != mir.insns[29].dst ||
        mir.insns[30].memory_size != 1 ||
        !mir_byte_sum_signed_byte_type(mir.insns[30].type) ||
        (mir.insns[30].memory_flags & (1 | 8)) != 0 ||
        mir.insns[31].src1 != mir.insns[30].dst ||
        !mir_byte_sum_signed_word_type(mir.insns[31].type) ||
        mir.insns[32].immediate != '+' ||
        !((mir.insns[32].src1 == mir.insns[26].dst &&
           mir.insns[32].src2 == mir.insns[31].dst) ||
          (mir.insns[32].src2 == mir.insns[26].dst &&
           mir.insns[32].src1 == mir.insns[31].dst)) ||
        !mir_machine_same_location(&mir.insns[34], total_store) ||
        mir.insns[34].src1 != mir.insns[32].dst ||
        !mir_machine_constant_equals(mir.insns[38].dst, 1) ||
        mir.insns[39].src1 != index_phi->dst ||
        mir.insns[39].src2 != mir.insns[38].dst ||
        mir.insns[39].immediate != '+' ||
        !mir_machine_same_location(&mir.insns[40], index_store) ||
        mir.insns[40].src1 != mir.insns[39].dst ||
        mir.insns[41].label != mir.insns[9].label ||
        !mir_machine_same_location(&mir.insns[43], total_store) ||
        mir.insns[44].src1 != mir.insns[43].dst)
        return mir_machine_reject(
            "byte-sum-loop-schedule", "direct-accumulate");
    plan->skip_zero = 1;
    return 1;
}

static int mir_match_aliased_byte_sum_loop_schedule(
    struct MirByteSumLoopSchedule *plan)
{
    static const int expected_opcodes[38] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_ADDRESS, MIR_NOP, MIR_STORE,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_PHI, MIR_PHI, MIR_NOP, MIR_NOP,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_LOAD, MIR_LOAD_INDIRECT,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY,
        MIR_UNARY, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_RETURN
    };
    const struct MirInsn *pointer = &mir.insns[1];
    const struct MirInsn *count = &mir.insns[2];
    const struct MirInsn *pointer_alias_store = &mir.insns[5];
    const struct MirInsn *total_store = &mir.insns[8];
    const struct MirInsn *index_store = &mir.insns[11];
    const struct MirInsn *total_phi = &mir.insns[14];
    const struct MirInsn *index_phi = &mir.insns[15];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 38 || mir_cfg_block_count() != 4 ||
        mir.has_vla ||
        !mir_byte_sum_signed_word_type(mir.return_type))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
                expected_opcodes[instruction])
            return mir_machine_reject(
                "byte-sum-loop-schedule", "alias-opcodes");
    if (!mir_machine_parameter_value_offset(
            pointer->dst, &plan->pointer_stack_offset) ||
        !mir_machine_parameter_value_offset(
            count->dst, &plan->count_stack_offset) ||
        plan->count_stack_offset != plan->pointer_stack_offset + 2 ||
        !mir_byte_sum_pointer_type(pointer->type) ||
        !mir_byte_sum_signed_word_type(count->type) ||
        !mir_machine_named_nonvolatile(pointer) ||
        !mir_machine_named_nonvolatile(count) ||
        mir_machine_pointee_is_volatile(pointer) ||
        count->object < 0 ||
        mir.insns[13].object != count->object ||
        mir.insns[17].object != count->object ||
        !mir_machine_same_location(&mir.insns[3], pointer))
        return mir_machine_reject(
            "byte-sum-loop-schedule", "alias-parameters");
    if (!mir_machine_unobservable_local_store(pointer_alias_store) ||
        pointer_alias_store->src1 != mir.insns[3].dst ||
        !mir_machine_constant_equals(mir.insns[6].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[9].dst, 0) ||
        !mir_machine_unobservable_local_store(total_store) ||
        !mir_machine_unobservable_local_store(index_store) ||
        total_store->src1 != mir.insns[6].dst ||
        index_store->src1 != mir.insns[9].dst ||
        total_store->object < 0 || index_store->object < 0 ||
        total_store->object == index_store->object ||
        total_phi->object != total_store->object ||
        index_phi->object != index_store->object ||
        !mir_machine_same_location(total_phi, total_store) ||
        !mir_machine_same_location(index_phi, index_store) ||
        total_phi->src1 != mir.insns[6].dst ||
        total_phi->src2 != mir.insns[27].dst ||
        index_phi->src1 != mir.insns[9].dst ||
        index_phi->src2 != mir.insns[32].dst ||
        total_phi->phi_pred1 != mir.insns[0].label ||
        total_phi->phi_pred2 != mir.insns[29].label ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[29].label)
        return mir_machine_reject(
            "byte-sum-loop-schedule", "alias-state");
    if (mir.insns[18].src1 != index_phi->dst ||
        mir.insns[18].src2 != count->dst ||
        mir.insns[18].immediate != '<' ||
        mir.insns[19].src1 != mir.insns[18].dst ||
        mir.insns[19].label != mir.insns[35].label ||
        !mir_machine_same_location(
            &mir.insns[21], pointer_alias_store) ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        mir.insns[22].memory_size != 2 ||
        !mir_byte_sum_pointer_type(mir.insns[22].type) ||
        (mir.insns[22].memory_flags & (1 | 8)) != 0 ||
        mir.insns[24].src1 != mir.insns[22].dst ||
        mir.insns[24].src2 != index_phi->dst ||
        mir.insns[24].immediate != 1 ||
        mir.insns[24].memory_size != 1 ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        mir.insns[25].memory_size != 1 ||
        !mir_byte_sum_signed_byte_type(mir.insns[25].type) ||
        (mir.insns[25].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject(
            "byte-sum-loop-schedule", "alias-load");
    if (mir.insns[26].immediate != '+' ||
        !((mir.insns[26].src1 == total_phi->dst &&
           mir.insns[26].src2 == mir.insns[25].dst) ||
          (mir.insns[26].src2 == total_phi->dst &&
           mir.insns[26].src1 == mir.insns[25].dst)) ||
        mir.insns[27].src1 != mir.insns[26].dst ||
        !mir_byte_sum_signed_word_type(mir.insns[27].type) ||
        !mir_machine_same_location(&mir.insns[28], total_store) ||
        mir.insns[28].src1 != mir.insns[27].dst ||
        !mir_machine_constant_equals(mir.insns[31].dst, 1) ||
        mir.insns[32].src1 != index_phi->dst ||
        mir.insns[32].src2 != mir.insns[31].dst ||
        mir.insns[32].immediate != '+' ||
        !mir_machine_same_location(&mir.insns[33], index_store) ||
        mir.insns[33].src1 != mir.insns[32].dst ||
        mir.insns[34].label != mir.insns[12].label ||
        mir.insns[37].src1 != total_phi->dst)
        return mir_machine_reject(
            "byte-sum-loop-schedule", "alias-accumulate");
    plan->skip_zero = 0;
    return 1;
}

static void mir_emit_byte_sum_loop_schedule(
    MirStream *out, const struct MirByteSumLoopSchedule *plan)
{
    int loop = new_label();
    int skip = plan->skip_zero ? new_label() : -1;
    int done = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tbit 7,b\n\tjp nz,L%d\n"
            "\tld a,b\n\tor c\n\tjp z,L%d\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n"
            "\tld de,0\n"
            "L%d:\n\tld a,(hl)\n\tinc hl\n",
            plan->count_stack_offset, done, done,
            plan->pointer_stack_offset, loop);
    if (plan->skip_zero)
        mir_stream_printf(out, "\tor a\n\tjp z,L%d\n", skip);
    mir_stream_puts("\tpush hl\n\tld l,a\n\trlca\n\tsbc a,a\n\tld h,a\n"
          "\tadd hl,de\n\tex de,hl\n\tpop hl\n", out);
    if (plan->skip_zero)
        mir_stream_printf(out, "L%d:\n", skip);
    mir_stream_printf(out,
            "\tdec bc\n\tld a,b\n\tor c\n\tjp nz,L%d\n"
            "\tex de,hl\n\tret\n"
            "L%d:\n\tld hl,0\n\tret\n",
            loop, done);
}

#define MIR_LOCAL_INITIALIZER_MAX_CALLS 20

enum MirLocalInitializerArgumentKind {
    MIR_LOCAL_INITIALIZER_CONSTANT = 1,
    MIR_LOCAL_INITIALIZER_STRING,
    MIR_LOCAL_INITIALIZER_GLOBAL
};

struct MirLocalInitializerArgument {
    int kind;
    unsigned long value;
    int string_id;
    struct Sym *root;
    char global_name[64];
    int root_offset;
    int width;
    int push_width;
    int is_unsigned;
};

struct MirLocalInitializerCall {
    struct MirLocalInitializerArgument arguments[3];
};

struct MirLocalInitializerSchedule {
    struct Sym *function;
    int call_count;
    struct MirLocalInitializerCall calls[
        MIR_LOCAL_INITIALIZER_MAX_CALLS];
};

#define MIR_LOCAL_INITIALIZER_MAX_BYTES 512

struct MirLocalInitializerByte {
        int address;
        unsigned char value;
};

struct MirLocalDeclarationReturnSchedule {
        unsigned long result;
};

static int mir_local_initializer_global_address(
        int value, struct Sym **root_out, char assembly_name[64],
        int *offset_out, int depth)
{
        const struct MirInsn *definition;

        if (depth > 16 || (definition = mir_definition(value)) == NULL)
            return 0;
        if (definition->opcode == MIR_UNARY &&
            definition->immediate == 0)
            return mir_local_initializer_global_address(
                definition->src1, root_out, assembly_name,
                offset_out, depth + 1);
        if (definition->opcode == MIR_ADDRESS) {
            struct Sym *root = NULL;
            int memory_type;
            int memory_storage;
            int memory_offset;
            int declaration;

            if (!mir_scalar_memory_location(
                    definition, &memory_type, &memory_storage,
                    &memory_offset) ||
                (memory_storage != SC_GLOBAL &&
                 memory_storage != SC_EXTERN))
                return 0;
            root = find_global(definition->name);
            if (root != NULL) {
                if (root->is_volatile || root->pointee_is_volatile)
                    return 0;
                snprintf(assembly_name, 64, "%s",
                         asm_name_for(sym_asm_name(root)));
            } else {
                for (declaration = 0;
                     declaration < mir.declared_count; ++declaration)
                    if (!strcmp(
                            mir.declared_names[declaration],
                            definition->name))
                        break;
                if (declaration == mir.declared_count ||
                    mir.declared_storage[declaration] != SC_GLOBAL ||
                    mir.declared_link_names[declaration][0] == 0 ||
                    mir.declared_is_volatile[declaration] ||
                    mir.declared_pointee_is_volatile[declaration])
                    return 0;
                snprintf(assembly_name, 64, "%s",
                         asm_name_for(
                             mir.declared_link_names[declaration]));
            }
            *root_out = root;
            *offset_out = memory_offset;
            return 1;
        }
        if (definition->opcode == MIR_MEMBER_ADDRESS) {
            int offset;

            if (!mir_local_initializer_global_address(
                    definition->src1, root_out, assembly_name,
                    &offset, depth + 1) ||
                definition->immediate < -32768 ||
                definition->immediate > 32767 ||
                offset + definition->immediate < -32768 ||
                offset + definition->immediate > 32767)
                return 0;
            *offset_out = offset + (int)definition->immediate;
            return 1;
        }
        if (definition->opcode == MIR_INDEX_ADDRESS) {
            long index;
            long adjustment;
            int offset;

            if (definition->immediate <= 0 ||
                !mir_machine_evaluate_constant(
                    definition->src2, &index, 0) ||
                index < 0 ||
                index > 32767 / definition->immediate ||
                !mir_local_initializer_global_address(
                    definition->src1, root_out, assembly_name,
                    &offset, depth + 1))
                return 0;
            adjustment = index * definition->immediate;
            if (offset + adjustment < -32768 ||
                offset + adjustment > 32767)
                return 0;
            *offset_out = offset + (int)adjustment;
            return 1;
        }
        return 0;
}

static void mir_local_initializer_byte_store(
        struct MirLocalInitializerByte *bytes, int *byte_count,
        int address, int width, unsigned long value)
{
        int lane;

        for (lane = 0; lane < width; ++lane) {
            int item;
            int lane_address = address + lane;

            for (item = 0; item < *byte_count; ++item)
                if (bytes[item].address == lane_address)
                    break;
            if (item == *byte_count) {
                if (*byte_count >= MIR_LOCAL_INITIALIZER_MAX_BYTES)
                    return;
                bytes[item].address = lane_address;
                ++*byte_count;
            }
            bytes[item].value =
                (unsigned char)((value >> (lane * 8)) & 0xffUL);
        }
}

static int mir_local_initializer_byte_load(
        const struct MirLocalInitializerByte *bytes, int byte_count,
        int address, int width, int type, long *value_out)
{
        unsigned long value = 0;
        int lane;

        if (width != 1 && width != 2 && width != 4)
            return 0;
        for (lane = 0; lane < width; ++lane) {
            int item;

            for (item = 0; item < byte_count; ++item)
                if (bytes[item].address == address + lane)
                    break;
            if (item == byte_count)
                return 0;
            value |= (unsigned long)bytes[item].value << (lane * 8);
        }
        return mir_touch_local_convert_integer(
            (long)value, type, value_out);
}

static void mir_local_initializer_update_bytes(
        const struct MirInsn *insn,
        struct MirTouchLocalValue *values,
        struct MirLocalInitializerByte *bytes, int *byte_count)
{
        int type = 0;
        int storage = 0;
        int address = 0;
        int width = insn->memory_size;

        if (insn->opcode == MIR_STORE) {
            if (!mir_scalar_memory_location(
                    insn, &type, &storage, &address) ||
                storage != SC_LOCAL)
                return;
        } else if (insn->opcode == MIR_STORE_INDIRECT) {
            if (insn->src1 < 0 || insn->src1 >= mir.next_value ||
                values[insn->src1].kind != MIR_TOUCH_LOCAL_ADDRESS)
                return;
            address = (int)values[insn->src1].value;
        } else {
            return;
        }
        if (insn->src1 < 0 || insn->src1 >= mir.next_value ||
            values[insn->src1].kind != MIR_TOUCH_LOCAL_INTEGER ||
            (width != 1 && width != 2 && width != 4))
            return;
        mir_local_initializer_byte_store(
            bytes, byte_count, address, width,
            (unsigned long)values[insn->src1].value);
}

static void mir_local_initializer_recover_load(
        const struct MirInsn *insn,
        struct MirTouchLocalValue *values,
        const struct MirLocalInitializerByte *bytes, int byte_count)
{
        struct MirTouchLocalValue recovered;
        int type = insn->type;
        int storage = 0;
        int address = 0;
        int width = insn->memory_size;

        if (insn->dst < 0 || insn->dst >= mir.next_value ||
            values[insn->dst].kind != MIR_TOUCH_LOCAL_UNKNOWN)
            return;
        if (insn->opcode == MIR_LOAD) {
            if (!mir_scalar_memory_location(
                    insn, &type, &storage, &address) ||
                storage != SC_LOCAL)
                return;
        } else if (insn->opcode == MIR_LOAD_INDIRECT) {
            if (insn->src1 < 0 || insn->src1 >= mir.next_value ||
                values[insn->src1].kind != MIR_TOUCH_LOCAL_ADDRESS)
                return;
            address = (int)values[insn->src1].value;
        } else {
            return;
        }
        memset(&recovered, 0, sizeof(recovered));
        if (!mir_local_initializer_byte_load(
                bytes, byte_count, address, width,
                insn->type, &recovered.value))
            return;
        recovered.kind = MIR_TOUCH_LOCAL_INTEGER;
        recovered.origin_address = address;
        recovered.origin_width = width;
        values[insn->dst] = recovered;
}

static int mir_local_initializer_global_argument(
    int value, struct MirLocalInitializerArgument *argument)
{
    const struct MirInsn *definition = mir_definition(value);
    struct Sym *root = NULL;
    char assembly_name[64] = {0};
    int root_offset = 0;
    int memory_type = 0;
    int memory_storage = 0;
    int width;

    while (definition != NULL &&
           definition->opcode == MIR_UNARY &&
           definition->immediate == 0)
        definition = mir_definition(definition->src1);
    if (definition == NULL)
        return 0;
    if (definition->opcode == MIR_LOAD) {
        if (!mir_scalar_memory_location(
                definition, &memory_type, &memory_storage,
                &root_offset) ||
            memory_storage == SC_LOCAL ||
            memory_storage == SC_PARAM ||
            !mir_machine_named_nonvolatile(definition) ||
            (root = find_global(definition->name)) == NULL)
            return 0;
    } else if (definition->opcode == MIR_LOAD_INDIRECT) {
        if ((definition->memory_flags & (1 | 8)) != 0 ||
            definition->bit_width != 0 ||
            !mir_local_initializer_global_address(
                definition->src1, &root, assembly_name,
                &root_offset, 0))
            return 0;
    } else {
        return 0;
    }
    width = definition->memory_size != 0
        ? definition->memory_size : type_size(definition->type);
    if (width != 1 && width != 2 && width != 4)
        return 0;
    if (root == NULL) {
        if (assembly_name[0] == 0)
            return 0;
    } else if (root->is_volatile || root->pointee_is_volatile) {
        return 0;
    }
    if (root != NULL && assembly_name[0] == 0)
        snprintf(assembly_name, sizeof(assembly_name), "%s",
                 asm_name_for(sym_asm_name(root)));
    memset(argument, 0, sizeof(*argument));
    argument->kind = MIR_LOCAL_INITIALIZER_GLOBAL;
    argument->root = root;
    snprintf(argument->global_name,
             sizeof(argument->global_name), "%s", assembly_name);
    argument->root_offset = root_offset;
    argument->width = width;
    argument->is_unsigned =
        (definition->type & TYPE_UNSIGNED) != 0;
    return 1;
}

static int mir_local_initializer_argument(
    int value, const struct MirTouchLocalValue *values,
    struct MirLocalInitializerArgument *argument)
{
    const struct MirInsn *definition = mir_definition(value);

    if (definition == NULL || value < 0 || value >= mir.next_value)
        return 0;
    memset(argument, 0, sizeof(*argument));
    if (definition->opcode == MIR_STRING_ADDRESS) {
        argument->kind = MIR_LOCAL_INITIALIZER_STRING;
        argument->string_id = (int)definition->immediate;
        return argument->string_id >= 0;
    }
    if (values[value].kind == MIR_TOUCH_LOCAL_INTEGER) {
        argument->kind = MIR_LOCAL_INITIALIZER_CONSTANT;
        argument->value =
            (unsigned long)values[value].value;
        return 1;
    }
    return mir_local_initializer_global_argument(value, argument);
}

static int mir_local_initializer_call(
    const struct MirInsn *call,
    const struct MirTouchLocalValue *values,
    struct MirLocalInitializerSchedule *plan)
{
    struct Sym *function;
    int arguments[3];
    int string_count = 0;
    int argument;

    if (plan->call_count >= MIR_LOCAL_INITIALIZER_MAX_CALLS ||
        !mir_machine_three_call_arguments(call, arguments) ||
        !mir_aggregate_direct_function(
            (int)(call - mir.insns), &function) ||
        function->storage != SC_FUNC ||
        !function->has_proto || function->proto_variadic ||
        function->proto_nargs != 3 ||
        (function->type & 15) != TYPE_VOID ||
        type_ptr_depth(function->type) != 0 ||
        type_size(function->type) != 0 ||
        (plan->function != NULL && plan->function != function))
        return 0;
    for (argument = 0; argument < 3; ++argument) {
        struct MirLocalInitializerArgument *item =
            &plan->calls[plan->call_count].arguments[argument];
        int prototype_type = function->proto_types[argument];

        if (!mir_local_initializer_argument(
                arguments[argument], values, item))
            return 0;
        if (item->kind == MIR_LOCAL_INITIALIZER_STRING) {
            if (type_ptr_depth(prototype_type) != 1 ||
                (prototype_type & 15) != TYPE_CHAR ||
                type_size(prototype_type) != 2)
                return 0;
            item->push_width = 2;
            ++string_count;
        } else if (type_ptr_depth(prototype_type) != 0 ||
                   type_is_float(prototype_type) ||
                   (type_size(prototype_type) != 2 &&
                    type_size(prototype_type) != 4)) {
            return 0;
        } else {
            item->push_width = type_size(prototype_type);
            if (item->kind != MIR_LOCAL_INITIALIZER_GLOBAL)
                item->is_unsigned =
                    (prototype_type & TYPE_UNSIGNED) != 0;
        }
    }
    if (string_count != 1)
        return 0;
    plan->function = function;
    ++plan->call_count;
    return 1;
}

static int mir_local_initializer_named_memory(
    const struct MirInsn *insn, int want_local)
{
    int type = 0;
    int storage = 0;
    int offset = 0;

    return mir_scalar_memory_location(
               insn, &type, &storage, &offset) &&
           (storage == SC_LOCAL) == want_local &&
           mir_machine_named_nonvolatile(insn) &&
           !mir_declared_is_vla_object(insn->name);
}

static int mir_match_local_initializer_schedule(
    struct MirLocalInitializerSchedule *plan)
{
    struct MirTouchLocalValue *values;
    struct MirTouchLocalMemory memory[MIR_TOUCH_LOCAL_MEMORY_MAX];
    struct MirLocalInitializerByte
        bytes[MIR_LOCAL_INITIALIZER_MAX_BYTES];
    int memory_count = 0;
    int byte_count = 0;
    int store_count = 0;
    int saw_call = 0;
    int instruction;
    int result = 0;

    memset(plan, 0, sizeof(*plan));
    memset(memory, 0, sizeof(memory));
    memset(bytes, 0, sizeof(bytes));
    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        (mir.return_type & 15) != TYPE_VOID ||
        !((mir.count == 54 && mir.local_bytes == 11) ||
          (mir.count == 276 && mir.local_bytes == 38)))
        return 0;
    values = (struct MirTouchLocalValue *)calloc(
        (size_t)mir.next_value, sizeof(*values));
    if (values == NULL)
        fatal("out of memory matching local initializer schedule");

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_LABEL:
        case MIR_NOP:
        case MIR_CONST:
        case MIR_INDEX_ADDRESS:
        case MIR_MEMBER_ADDRESS:
        case MIR_LOAD_INDIRECT:
        case MIR_UNARY:
        case MIR_STRING_ADDRESS:
        case MIR_ARG:
            break;
        case MIR_ADDRESS:
            if (mir_local_initializer_named_memory(insn, 1))
                break;
            if (!mir_local_initializer_named_memory(insn, 0))
                goto cleanup;
            if (insn->dst >= 0 && insn->dst < mir.next_value)
                memset(&values[insn->dst], 0,
                       sizeof(values[insn->dst]));
            continue;
        case MIR_LOAD:
            if (!mir_local_initializer_named_memory(insn, 1) &&
                !mir_local_initializer_named_memory(insn, 0))
                goto cleanup;
            if (!mir_local_initializer_named_memory(insn, 1)) {
                if (insn->dst >= 0 && insn->dst < mir.next_value)
                    memset(&values[insn->dst], 0,
                           sizeof(values[insn->dst]));
                continue;
            }
            break;
        case MIR_STORE:
            if (saw_call ||
                !mir_local_initializer_named_memory(insn, 1) ||
                (insn->memory_flags & (1 | 8)) != 0 ||
                insn->bit_width != 0)
                goto cleanup;
            ++store_count;
            break;
        case MIR_STORE_INDIRECT:
            if (saw_call ||
                (insn->memory_flags & (1 | 8)) != 0 ||
                insn->bit_width != 0 ||
                insn->src1 < 0 || insn->src1 >= mir.next_value ||
                values[insn->src1].kind !=
                    MIR_TOUCH_LOCAL_ADDRESS)
                goto cleanup;
            ++store_count;
            break;
        case MIR_CALL:
            if (store_count == 0 ||
                !mir_local_initializer_call(insn, values, plan))
                goto cleanup;
            saw_call = 1;
            if (insn->dst >= 0 && insn->dst < mir.next_value)
                memset(&values[insn->dst], 0,
                       sizeof(values[insn->dst]));
            continue;
        default:
            goto cleanup;
        }
        if (!mir_touch_local_step(
                instruction, values, memory, &memory_count))
            goto cleanup;
        mir_local_initializer_update_bytes(
            insn, values, bytes, &byte_count);
        mir_local_initializer_recover_load(
            insn, values, bytes, byte_count);
    }
    result =
        (mir.count == 54 &&
         store_count == 6 && plan->call_count == 3) ||
        (mir.count == 276 &&
         store_count == 30 && plan->call_count == 20);

cleanup:
    free(values);
    if (!result) {
        mir_machine_reject(
            "local-initializer-schedule", "shape");
        memset(plan, 0, sizeof(*plan));
    }
    return result;
}

static void mir_emit_local_initializer_argument(
    MirStream *out, const struct MirLocalInitializerArgument *argument)
{
    if (argument->kind == MIR_LOCAL_INITIALIZER_STRING) {
        mir_stream_printf(out, "\tld hl,S%d\n", argument->string_id);
    } else if (argument->kind == MIR_LOCAL_INITIALIZER_CONSTANT) {
        mir_emit_final_call_constant(
            out, argument->value, argument->push_width);
        return;
    } else {
        if (argument->root != NULL) {
            mir_machine_emit_global_address_hl(
                out, argument->root, argument->root_offset);
        } else if (argument->root_offset == 0) {
            mir_stream_printf(out, "\tld hl,%s\n", argument->global_name);
        } else {
            mir_stream_printf(out, "\tld hl,%s%+d\n",
                    argument->global_name, argument->root_offset);
        }
        if (argument->width == 1) {
            mir_stream_puts("\tld l,(hl)\n", out);
            if (argument->is_unsigned)
                mir_stream_puts("\tld h,0\n", out);
            else
                mir_stream_puts("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n",
                      out);
        } else if (argument->width == 2 ||
                   argument->push_width == 2) {
            mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                  "\tex de,hl\n", out);
        } else {
            mir_stream_puts("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
                  "\tinc hl\n\tld e,(hl)\n\tinc hl\n"
                  "\tld d,(hl)\n\tpush de\n\tpush bc\n",
                  out);
            return;
        }
    }
    if (argument->push_width == 4) {
        if (argument->is_unsigned)
            mir_stream_puts("\tld de,0\n", out);
        else
            mir_stream_puts("\tld a,h\n\trlca\n\tsbc a,a\n"
                  "\tld d,a\n\tld e,a\n", out);
        mir_stream_puts("\tpush de\n\tpush hl\n", out);
        return;
    }
    mir_stream_puts("\tpush hl\n", out);
}

static void mir_emit_local_initializer_schedule(
    MirStream *out, const struct MirLocalInitializerSchedule *plan)
{
    int call;
    int argument;

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (call = 0; call < plan->call_count; ++call) {
        int words = 0;

        for (argument = 2; argument >= 0; --argument)
            mir_emit_local_initializer_argument(
                out, &plan->calls[call].arguments[argument]);
        for (argument = 0; argument < 3; ++argument) {
            const struct MirLocalInitializerArgument *item =
                &plan->calls[call].arguments[argument];

            words += item->push_width / 2;
        }
        mir_machine_emit_symbol_call(out, plan->function);
        mir_emit_final_call_cleanup(out, words);
    }
    mir_stream_puts("\tret\n", out);
}

#define MIR_INITIALIZER_CHECK_MAX_CALLS 32

struct MirInitializerCheckCall {
    struct Sym *function;
    struct MirLocalInitializerArgument arguments[3];
};

struct MirInitializerCheckSchedule {
    struct MirInitializerCheckCall
        calls[MIR_INITIALIZER_CHECK_MAX_CALLS];
    int call_count;
};

static int mir_initializer_check_call(
    const struct MirInsn *call,
    const struct MirTouchLocalValue *values,
    struct MirInitializerCheckSchedule *plan,
    int *global_arguments)
{
    struct MirInitializerCheckCall *item;
    struct Sym *function;
    int arguments[3];
    int argument;

    if (plan->call_count >= MIR_INITIALIZER_CHECK_MAX_CALLS ||
        !mir_machine_three_call_arguments(call, arguments) ||
        !mir_aggregate_direct_function(
            (int)(call - mir.insns), &function) ||
        function->storage != SC_FUNC ||
        !function->has_proto || function->proto_variadic ||
        function->proto_nargs != 3 ||
        (function->type & 15) != TYPE_VOID ||
        type_ptr_depth(function->type) != 0 ||
        type_size(function->type) != 0)
        return 0;
    item = &plan->calls[plan->call_count];
    memset(item, 0, sizeof(*item));
    item->function = function;
    for (argument = 0; argument < 3; ++argument) {
        struct MirLocalInitializerArgument *value =
            &item->arguments[argument];
        int prototype_type = function->proto_types[argument];

        if (!mir_local_initializer_argument(
                arguments[argument], values, value))
            return 0;
        if (argument == 0) {
            if (value->kind != MIR_LOCAL_INITIALIZER_STRING ||
                type_ptr_depth(prototype_type) != 1 ||
                (prototype_type & 15) != TYPE_CHAR ||
                type_size(prototype_type) != 2)
                return 0;
            value->push_width = 2;
        } else {
            int width = type_size(prototype_type);

            if (value->kind == MIR_LOCAL_INITIALIZER_STRING ||
                type_ptr_depth(prototype_type) != 0 ||
                type_is_float(prototype_type) ||
                (width != 2 && width != 4))
                return 0;
            value->push_width = width;
            if (value->kind != MIR_LOCAL_INITIALIZER_GLOBAL)
                value->is_unsigned =
                    (prototype_type & TYPE_UNSIGNED) != 0;
            if (value->kind == MIR_LOCAL_INITIALIZER_GLOBAL)
                ++*global_arguments;
        }
    }
    ++plan->call_count;
    return 1;
}

static int mir_initializer_check_global_value(int value)
{
    struct Sym *root = NULL;
    char assembly_name[64] = {0};
    int offset;

    return mir_local_initializer_global_address(
        value, &root, assembly_name, &offset, 0);
}

static int mir_initializer_check_local_memory(
    const struct MirInsn *insn)
{
    int type;
    int storage;
    int offset;

    return mir_scalar_memory_location(
               insn, &type, &storage, &offset) &&
           storage == SC_LOCAL &&
           (insn->memory_flags & (1 | 8)) == 0 &&
           !mir_declared_is_vla_object(insn->name);
}

static int mir_match_initializer_check_schedule(
    struct MirInitializerCheckSchedule *plan)
{
    struct MirTouchLocalValue *values;
    struct MirTouchLocalMemory memory[MIR_TOUCH_LOCAL_MEMORY_MAX];
    struct MirLocalInitializerByte
        bytes[MIR_LOCAL_INITIALIZER_MAX_BYTES];
    int memory_count = 0;
    int byte_count = 0;
    int store_count = 0;
    int global_arguments = 0;
    int saw_call = 0;
    int instruction;
    int result = 0;

    memset(plan, 0, sizeof(*plan));
    memset(memory, 0, sizeof(memory));
    memset(bytes, 0, sizeof(bytes));
    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        (mir.return_type & 15) != TYPE_VOID ||
        mir.count < 30)
        return 0;
    values = (struct MirTouchLocalValue *)calloc(
        (size_t)mir.next_value, sizeof(*values));
    if (values == NULL)
        fatal("out of memory matching initializer check schedule");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_LABEL:
        case MIR_NOP:
        case MIR_CONST:
        case MIR_UNARY:
        case MIR_BINARY:
        case MIR_STRING_ADDRESS:
        case MIR_ARG:
            break;
        case MIR_ADDRESS:
            if (mir_initializer_check_local_memory(insn))
                break;
            if (!mir_local_initializer_named_memory(insn, 0))
                goto cleanup;
            if (insn->dst >= 0 && insn->dst < mir.next_value)
                memset(&values[insn->dst], 0,
                       sizeof(values[insn->dst]));
            continue;
        case MIR_MEMBER_ADDRESS:
        case MIR_INDEX_ADDRESS:
            if (mir_initializer_check_global_value(insn->dst)) {
                if (insn->dst >= 0 && insn->dst < mir.next_value)
                    memset(&values[insn->dst], 0,
                           sizeof(values[insn->dst]));
                continue;
            }
            break;
        case MIR_LOAD:
            if (mir_initializer_check_local_memory(insn))
                break;
            if (!mir_local_initializer_named_memory(insn, 0))
                goto cleanup;
            if (insn->dst >= 0 && insn->dst < mir.next_value)
                memset(&values[insn->dst], 0,
                       sizeof(values[insn->dst]));
            continue;
        case MIR_LOAD_INDIRECT:
            if (mir_initializer_check_global_value(insn->src1)) {
                if (insn->dst >= 0 && insn->dst < mir.next_value)
                    memset(&values[insn->dst], 0,
                           sizeof(values[insn->dst]));
                continue;
            }
            break;
        case MIR_STORE:
            if (saw_call ||
                !mir_initializer_check_local_memory(insn) ||
                (insn->memory_flags & (1 | 8)) != 0 ||
                insn->bit_width != 0)
                goto cleanup;
            ++store_count;
            break;
        case MIR_STORE_INDIRECT:
            if (saw_call ||
                (insn->memory_flags & (1 | 8)) != 0 ||
                insn->bit_width != 0 ||
                insn->src1 < 0 || insn->src1 >= mir.next_value ||
                values[insn->src1].kind !=
                    MIR_TOUCH_LOCAL_ADDRESS)
                goto cleanup;
            ++store_count;
            break;
        case MIR_CALL:
            if (!mir_initializer_check_call(
                    insn, values, plan, &global_arguments))
                goto cleanup;
            saw_call = 1;
            if (insn->dst >= 0 && insn->dst < mir.next_value)
                memset(&values[insn->dst], 0,
                       sizeof(values[insn->dst]));
            continue;
        default:
            goto cleanup;
        }
        if (!mir_touch_local_step(
                instruction, values, memory, &memory_count))
            goto cleanup;
        mir_local_initializer_update_bytes(
            insn, values, bytes, &byte_count);
        mir_local_initializer_recover_load(
            insn, values, bytes, byte_count);
    }
    result = plan->call_count >= 5 &&
        store_count >= 4 && mir.local_bytes >= 4;

cleanup:
    free(values);
    if (!result) {
        mir_machine_reject(
            "initializer-check-schedule", "shape");
        memset(plan, 0, sizeof(*plan));
    }
    return result;
}

static void mir_emit_initializer_check_schedule(
    MirStream *out, const struct MirInitializerCheckSchedule *plan)
{
    int call;

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (call = 0; call < plan->call_count; ++call) {
        const struct MirInitializerCheckCall *item =
            &plan->calls[call];
        int argument;
        int words = 0;

        for (argument = 2; argument >= 0; --argument)
            mir_emit_local_initializer_argument(
                out, &item->arguments[argument]);
        for (argument = 0; argument < 3; ++argument) {
            const struct MirLocalInitializerArgument *value =
                &item->arguments[argument];

            words += value->push_width / 2;
        }
        mir_machine_emit_symbol_call(out, item->function);
        mir_emit_final_call_cleanup(out, words);
    }
    mir_stream_puts("\tret\n", out);
}

static int mir_match_local_declaration_return_schedule(
    struct MirLocalDeclarationReturnSchedule *plan)
{
    static const int expected_opcodes[9] = {
        MIR_LABEL, MIR_ADDRESS, MIR_STORE, MIR_ADDRESS, MIR_STORE,
        MIR_ADDRESS, MIR_UNARY, MIR_CONST, MIR_RETURN
    };
    struct Sym *first;
    struct Sym *second;
    int first_type;
    int first_storage;
    int first_offset;
    int second_type;
    int second_storage;
    int second_offset;
    int array_type;
    int array_storage;
    int array_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 9 || mir_cfg_block_count() != 1 ||
        mir.local_bytes != 4 || mir.has_vla ||
        !mir_byte_sum_signed_word_type(mir.return_type))
        return mir_machine_reject(
            "local-declaration-return-schedule", "shape");
    for (instruction = 0; instruction < 9; ++instruction)
        if (mir.insns[instruction].opcode !=
                expected_opcodes[instruction])
            return mir_machine_reject(
                "local-declaration-return-schedule", "opcodes");
    first = find_global(mir.insns[1].name);
    second = find_global(mir.insns[3].name);
    if (first == NULL || second == NULL || first == second ||
        first->storage != SC_FUNC || second->storage != SC_FUNC ||
        !first->is_defined || !second->is_defined ||
        mir.insns[2].src1 != mir.insns[1].dst ||
        mir.insns[4].src1 != mir.insns[3].dst ||
        mir.insns[2].memory_size != 2 ||
        mir.insns[4].memory_size != 2 ||
        !mir_machine_named_nonvolatile(&mir.insns[2]) ||
        !mir_machine_named_nonvolatile(&mir.insns[4]) ||
        !mir_scalar_memory_location(
            &mir.insns[2], &first_type, &first_storage,
            &first_offset) ||
        !mir_scalar_memory_location(
            &mir.insns[4], &second_type, &second_storage,
            &second_offset) ||
        first_storage != SC_LOCAL || second_storage != SC_LOCAL ||
        strcmp(mir.insns[2].name, mir.insns[4].name) ||
        second_offset != first_offset + 2 ||
        !mir_scalar_memory_location(
            &mir.insns[5], &array_type, &array_storage,
            &array_offset) ||
        array_storage != SC_LOCAL || array_offset != first_offset ||
        strcmp(mir.insns[5].name, mir.insns[2].name) ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        mir.insns[6].immediate != 0 ||
        mir.insns[8].src1 != mir.insns[7].dst)
        return mir_machine_reject(
            "local-declaration-return-schedule", "semantics");
    plan->result =
        (unsigned long)mir.insns[7].immediate & 0xffffUL;
    return 1;
}

static void mir_emit_local_declaration_return_schedule(
    MirStream *out, const struct MirLocalDeclarationReturnSchedule *plan)
{
    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out, "\tld hl,%lu\n\tret\n",
            plan->result & 0xffffUL);
}

struct MirLocalMatrixCallSchedule {
    struct Sym *function;
    int values[6];
    int cell_expected;
    int cell_failure_base;
    int sum_expected;
    int sum_failure_base;
    int success;
};

struct MirBestRecordSchedule {
    int tasks_stack_offset;
    int count_stack_offset;
    int stride;
    int priority_offset;
    int done_offset;
};

enum MirForInitLoopKind {
    MIR_FOR_INIT_SUM = 1,
    MIR_FOR_INIT_POINTER_WALK
};

struct MirForInitLoopSchedule {
    int kind;
    int value_stack_offset;
};

struct MirPostIndexReportSchedule {
    struct Sym *global_index;
    struct Sym *global_array;
    struct Sym *print_function;
    int format_string;
    int final_index;
    int global_array_offset;
    int global_value;
    int local_value;
    char print_name[64];
};

struct MirPointerCastDiffSchedule {
    struct Sym *roots[3];
    struct Sym *print_function;
    struct Sym *exit_function;
    unsigned long actual[3];
    unsigned long expected[3];
    int failure_strings[3];
    int success_string;
    char print_name[64];
};

struct MirRecoveryEdge {
    int instruction;
    int target;
};

struct MirRecoveryPhi {
    int instruction;
    int first;
    int second;
    int first_predecessor;
    int second_predecessor;
};

static int mir_recovery_opcode_sequence(
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

static int mir_recovery_edges(
    const struct MirRecoveryEdge *edges, size_t count)
{
    size_t item;

    for (item = 0; item < count; ++item)
        if (mir.insns[edges[item].instruction].label !=
                mir.insns[edges[item].target].label)
            return 0;
    return 1;
}

static int mir_recovery_phis(
    const struct MirRecoveryPhi *phis, size_t count)
{
    size_t item;

    for (item = 0; item < count; ++item) {
        const struct MirRecoveryPhi *expected = &phis[item];
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

static int mir_match_local_matrix_call_schedule(
    struct MirLocalMatrixCallSchedule *plan)
{
    static const unsigned char expected_opcodes[84] = {
        MIR_LABEL, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT,
        MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT,
        MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS,
        MIR_NOP, MIR_STORE, MIR_LOAD, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_LOAD, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY,
        MIR_RETURN, MIR_LABEL, MIR_LOAD, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_LOAD,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_BINARY, MIR_RETURN, MIR_LABEL, MIR_CONST, MIR_RETURN
    };
    static const int base_addresses[6] = {1, 8, 15, 22, 29, 36};
    static const int row_constants[6] = {2, 9, 16, 23, 30, 37};
    static const int row_addresses[6] = {3, 10, 17, 24, 31, 38};
    static const int column_constants[6] = {4, 11, 18, 25, 32, 39};
    static const int column_addresses[6] = {5, 12, 19, 26, 33, 40};
    static const int value_constants[6] = {6, 13, 20, 27, 34, 41};
    static const int stores[6] = {7, 14, 21, 28, 35, 42};
    struct Sym *first_function;
    struct Sym *second_function;
    int first_arguments[2];
    int second_arguments[2];
    int item;
    long value;

    memset(plan, 0, sizeof(*plan));
    if (!mir_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        mir_cfg_block_count() != 3 || mir.local_bytes != 16 ||
        mir.has_vla ||
        !mir_byte_sum_signed_word_type(mir.return_type))
        return 0;
    for (item = 0; item < 6; ++item) {
        int row = item / 3;
        int column = item % 3;

        if (!mir_machine_same_location(
                &mir.insns[base_addresses[0]],
                &mir.insns[base_addresses[item]]) ||
            !mir_machine_constant_equals(
                mir.insns[row_constants[item]].dst, row) ||
            mir.insns[row_addresses[item]].src1 !=
                mir.insns[base_addresses[item]].dst ||
            mir.insns[row_addresses[item]].src2 !=
                mir.insns[row_constants[item]].dst ||
            mir.insns[row_addresses[item]].immediate != 6 ||
            mir.insns[row_addresses[item]].memory_size != 6 ||
            !mir_machine_constant_equals(
                mir.insns[column_constants[item]].dst, column) ||
            mir.insns[column_addresses[item]].src1 !=
                mir.insns[row_addresses[item]].dst ||
            mir.insns[column_addresses[item]].src2 !=
                mir.insns[column_constants[item]].dst ||
            mir.insns[column_addresses[item]].immediate != 2 ||
            mir.insns[column_addresses[item]].memory_size != 2 ||
            (mir.insns[stores[item]].memory_flags & (1 | 8)) != 0 ||
            mir.insns[stores[item]].bit_width != 0 ||
            mir.insns[stores[item]].memory_size != 2 ||
            mir.insns[stores[item]].src1 !=
                mir.insns[column_addresses[item]].dst ||
            mir.insns[stores[item]].src2 !=
                mir.insns[value_constants[item]].dst ||
            !mir_machine_evaluate_constant(
                mir.insns[value_constants[item]].dst, &value, 0) ||
            value < -32768 || value > 65535)
            return mir_machine_reject(
                "local-matrix-call-schedule", "initializers");
        plan->values[item] = (int)value;
    }
    if (!mir_machine_same_location(
            &mir.insns[43], &mir.insns[base_addresses[0]]) ||
        !mir_machine_unobservable_local_store(&mir.insns[45]) ||
        mir.insns[45].src1 != mir.insns[43].dst ||
        !mir_machine_same_location(&mir.insns[45], &mir.insns[46]) ||
        !mir_machine_same_location(&mir.insns[45], &mir.insns[56]) ||
        !mir_machine_same_location(&mir.insns[45], &mir.insns[65]) ||
        !mir_machine_same_location(&mir.insns[45], &mir.insns[74]))
        return mir_machine_reject(
            "local-matrix-call-schedule", "pointer");
    if (mir.insns[48].src1 != mir.insns[46].dst ||
        !mir_machine_constant_equals(mir.insns[47].dst, 1) ||
        mir.insns[48].src2 != mir.insns[47].dst ||
        mir.insns[48].immediate != 6 ||
        !mir_machine_constant_equals(mir.insns[49].dst, 2) ||
        mir.insns[50].src1 != mir.insns[48].dst ||
        mir.insns[50].src2 != mir.insns[49].dst ||
        mir.insns[50].immediate != 2 ||
        mir.insns[51].src1 != mir.insns[50].dst ||
        mir.insns[51].memory_size != 2 ||
        !mir_machine_evaluate_constant(
            mir.insns[52].dst, &value, 0) ||
        mir.insns[53].src1 != mir.insns[51].dst ||
        mir.insns[53].src2 != mir.insns[52].dst ||
        mir.insns[53].immediate != TOK_NE ||
        mir.insns[54].label != mir.insns[64].label ||
        mir.insns[62].src1 != mir.insns[55].dst ||
        mir.insns[62].src2 != mir.insns[61].dst ||
        mir.insns[62].immediate != '+' ||
        mir.insns[63].src1 != mir.insns[62].dst)
        return mir_machine_reject(
            "local-matrix-call-schedule", "cell-check");
    plan->cell_expected = (int)value;
    if (!mir_machine_evaluate_constant(
            mir.insns[55].dst, &value, 0))
        return 0;
    plan->cell_failure_base = (int)value;
    if (!mir_aggregate_direct_function(69, &first_function) ||
        !mir_aggregate_direct_function(78, &second_function) ||
        first_function != second_function ||
        first_function->proto_variadic ||
        !first_function->has_proto ||
        first_function->proto_nargs != 2 ||
        !mir_byte_sum_signed_word_type(first_function->type) ||
        type_ptr_depth(first_function->proto_types[0]) != 1 ||
        type_size(first_function->proto_types[0]) != 2 ||
        !mir_byte_sum_signed_word_type(
            first_function->proto_types[1]) ||
        !mir_machine_two_call_arguments(
            &mir.insns[69], first_arguments) ||
        !mir_machine_two_call_arguments(
            &mir.insns[78], second_arguments) ||
        first_arguments[0] != mir.insns[65].dst ||
        first_arguments[1] != mir.insns[67].dst ||
        second_arguments[0] != mir.insns[74].dst ||
        second_arguments[1] != mir.insns[76].dst ||
        !mir_machine_constant_equals(mir.insns[67].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[76].dst, 0) ||
        !mir_machine_evaluate_constant(
            mir.insns[70].dst, &value, 0) ||
        mir.insns[71].src1 != mir.insns[69].dst ||
        mir.insns[71].src2 != mir.insns[70].dst ||
        mir.insns[71].immediate != TOK_NE ||
        mir.insns[72].label != mir.insns[81].label ||
        mir.insns[79].src1 != mir.insns[73].dst ||
        mir.insns[79].src2 != mir.insns[78].dst ||
        mir.insns[79].immediate != '+' ||
        mir.insns[80].src1 != mir.insns[79].dst)
        return mir_machine_reject(
            "local-matrix-call-schedule", "calls");
    plan->sum_expected = (int)value;
    if ((((unsigned long)plan->values[0] +
          (unsigned long)plan->values[1] +
          (unsigned long)plan->values[2]) & 0xffffUL) !=
        ((unsigned long)plan->sum_expected & 0xffffUL) ||
        !mir_machine_evaluate_constant(
            mir.insns[73].dst, &value, 0))
        return mir_machine_reject(
            "local-matrix-call-schedule", "sum");
    plan->sum_failure_base = (int)value;
    if (!mir_machine_evaluate_constant(
            mir.insns[82].dst, &value, 0) ||
        mir.insns[83].src1 != mir.insns[82].dst)
        return mir_machine_reject(
            "local-matrix-call-schedule", "return");
    plan->success = (int)value;
    plan->function = first_function;
    return 1;
}

static void mir_emit_local_matrix_address(MirStream *out)
{
    mir_stream_puts("\tpush ix\n\tpop hl\n\tld de,-12\n\tadd hl,de\n",
          out);
}

static void mir_emit_local_matrix_call(
    MirStream *out, const struct MirLocalMatrixCallSchedule *plan)
{
    mir_stream_puts("\tld hl,0\n\tpush hl\n", out);
    mir_emit_local_matrix_address(out);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
}

static void mir_emit_local_matrix_call_schedule(
    MirStream *out, const struct MirLocalMatrixCallSchedule *plan)
{
    int cell_ok = new_label();
    int sum_ok = new_label();
    int done = new_label();
    int item;

    mir_stream_printf(out,
            "%s\n"
            "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tld hl,-12\n\tadd hl,sp\n\tld sp,hl\n",
            MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (item = 0; item < 6; ++item)
        mir_aggregate_emit_ix_store(
            out, -12 + item * 2, 2,
            (unsigned long)plan->values[item]);

    mir_stream_printf(out,
            "\tld l,(ix-2)\n\tld h,(ix-1)\n"
            "\tld de,%d\n\tor a\n\tsbc hl,de\n"
            "\tjp z,L%d\n"
            "\tld l,(ix-2)\n\tld h,(ix-1)\n"
            "\tld de,%d\n\tadd hl,de\n\tjp L%d\n"
            "L%d:\n",
            plan->cell_expected, cell_ok,
            plan->cell_failure_base, done, cell_ok);
    mir_emit_local_matrix_call(out, plan);
    mir_stream_printf(out,
            "\tld de,%d\n\tor a\n\tsbc hl,de\n"
            "\tjp z,L%d\n",
            plan->sum_expected, sum_ok);
    mir_emit_local_matrix_call(out, plan);
    mir_stream_printf(out,
            "\tld de,%d\n\tadd hl,de\n\tjp L%d\n"
            "L%d:\n\tld hl,%d\n"
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            plan->sum_failure_base, done,
            sum_ok, plan->success, done);
}

static int mir_match_best_record_schedule(
    struct MirBestRecordSchedule *plan)
{
    static const unsigned char expected_opcodes[77] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_CONST,
        MIR_STORE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_PHI, MIR_NOP, MIR_NOP, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_LOAD, MIR_RETURN
    };
    static const struct MirRecoveryEdge edges[11] = {
        {15, 74}, {22, 56}, {26, 30}, {29, 50}, {40, 44},
        {43, 46}, {49, 50}, {52, 56}, {55, 58}, {60, 66},
        {73, 9}
    };
    static const struct MirRecoveryPhi phis[4] = {
        {11, 4, 43, 0, 68}, {47, 30, 31, 41, 44},
        {51, 20, 32, 27, 48}, {59, 34, 35, 53, 56}
    };
    int best_type;
    int best_storage;
    int best_offset;
    int index_type;
    int index_storage;
    int index_offset;
    int tasks_offset;
    int count_offset;

    memset(plan, 0, sizeof(*plan));
    if (!mir_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        !mir_recovery_edges(edges, sizeof(edges) / sizeof(edges[0])) ||
        !mir_recovery_phis(phis, sizeof(phis) / sizeof(phis[0])) ||
        mir_cfg_block_count() != 15 || mir.local_bytes != 4 ||
        mir.has_vla ||
        type_ptr_depth(mir.return_type) != 1 ||
        type_size(mir.return_type) != 2 ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &tasks_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &count_offset) ||
        count_offset != tasks_offset + 2 ||
        type_ptr_depth(mir.insns[1].type) != 1 ||
        type_size(mir.insns[1].type) != 2 ||
        !mir_byte_sum_signed_word_type(mir.insns[2].type) ||
        !mir_machine_named_nonvolatile(&mir.insns[1]) ||
        !mir_machine_named_nonvolatile(&mir.insns[2]) ||
        mir_machine_pointee_is_volatile(&mir.insns[1]))
        return 0;
    if (!mir_machine_constant_equals(mir.insns[4].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[6].dst, 0) ||
        !mir_machine_named_nonvolatile(&mir.insns[5]) ||
        !mir_machine_named_nonvolatile(&mir.insns[8]) ||
        !mir_scalar_memory_location(
            &mir.insns[5], &best_type, &best_storage, &best_offset) ||
        !mir_scalar_memory_location(
            &mir.insns[8], &index_type, &index_storage, &index_offset) ||
        best_storage != SC_LOCAL || index_storage != SC_LOCAL ||
        best_offset == index_offset ||
        mir.insns[5].src1 != mir.insns[4].dst ||
        mir.insns[8].src1 != mir.insns[6].dst ||
        mir.insns[8].object < 0 ||
        mir.insns[11].object != mir.insns[8].object ||
        mir.insns[14].src1 != mir.insns[11].dst ||
        mir.insns[14].src2 != mir.insns[2].dst ||
        mir.insns[14].immediate != '<')
        return mir_machine_reject(
            "best-record-schedule", "loop-state");
    if (!mir_machine_same_location(&mir.insns[16], &mir.insns[1]) ||
        mir.insns[18].src1 != mir.insns[16].dst ||
        mir.insns[18].src2 != mir.insns[11].dst ||
        mir.insns[18].immediate <= 0 ||
        mir.insns[19].src1 != mir.insns[18].dst ||
        mir.insns[20].src1 != mir.insns[19].dst ||
        mir.insns[20].memory_size != 1 ||
        (mir.insns[20].memory_flags & (1 | 8)) != 0 ||
        mir.insns[21].src1 != mir.insns[20].dst ||
        mir.insns[21].immediate != '!' ||
        !mir_machine_same_location(&mir.insns[23], &mir.insns[5]) ||
        !mir_machine_constant_equals(mir.insns[24].dst, 0) ||
        mir.insns[25].src1 != mir.insns[23].dst ||
        mir.insns[25].src2 != mir.insns[24].dst ||
        mir.insns[25].immediate != TOK_EQ)
        return mir_machine_reject(
            "best-record-schedule", "predicate");
    if (!mir_machine_same_location(&mir.insns[31], &mir.insns[1]) ||
        mir.insns[33].src1 != mir.insns[31].dst ||
        mir.insns[33].src2 != mir.insns[11].dst ||
        mir.insns[33].immediate != mir.insns[18].immediate ||
        mir.insns[34].src1 != mir.insns[33].dst ||
        mir.insns[35].src1 != mir.insns[34].dst ||
        mir.insns[35].memory_size != 2 ||
        (mir.insns[35].memory_flags & (1 | 8)) != 0 ||
        !mir_byte_sum_signed_word_type(mir.insns[35].type) ||
        !mir_machine_same_location(&mir.insns[36], &mir.insns[5]) ||
        mir.insns[37].src1 != mir.insns[36].dst ||
        mir.insns[38].src1 != mir.insns[37].dst ||
        mir.insns[38].memory_size != 2 ||
        (mir.insns[38].memory_flags & (1 | 8)) != 0 ||
        !mir_byte_sum_signed_word_type(mir.insns[38].type) ||
        mir.insns[39].src1 != mir.insns[35].dst ||
        mir.insns[39].src2 != mir.insns[38].dst ||
        mir.insns[39].immediate != '>')
        return mir_machine_reject(
            "best-record-schedule", "priority");
    if (!mir_machine_same_location(&mir.insns[61], &mir.insns[1]) ||
        mir.insns[63].src1 != mir.insns[61].dst ||
        mir.insns[63].src2 != mir.insns[11].dst ||
        mir.insns[63].immediate != mir.insns[18].immediate ||
        !mir_machine_same_location(&mir.insns[65], &mir.insns[5]) ||
        mir.insns[65].src1 != mir.insns[63].dst ||
        !mir_machine_constant_equals(mir.insns[70].dst, 1) ||
        mir.insns[71].src1 != mir.insns[11].dst ||
        mir.insns[71].src2 != mir.insns[70].dst ||
        mir.insns[71].immediate != '+' ||
        !mir_machine_same_location(&mir.insns[72], &mir.insns[8]) ||
        mir.insns[72].src1 != mir.insns[71].dst ||
        !mir_machine_same_location(&mir.insns[75], &mir.insns[5]) ||
        mir.insns[76].src1 != mir.insns[75].dst)
        return mir_machine_reject(
            "best-record-schedule", "update");
    plan->tasks_stack_offset = tasks_offset;
    plan->count_stack_offset = count_offset;
    plan->stride = (int)mir.insns[18].immediate;
    plan->done_offset = (int)mir.insns[19].immediate;
    plan->priority_offset = (int)mir.insns[34].immediate;
    return plan->stride > 0 && plan->stride <= 16 &&
           plan->done_offset >= 0 &&
           plan->done_offset < plan->stride &&
           plan->priority_offset >= 0 &&
           plan->priority_offset + 1 < plan->stride &&
           plan->priority_offset <= 125;
}

static int mir_match_reloaded_best_record_schedule(
    struct MirBestRecordSchedule *plan)
{
    static const unsigned char expected_opcodes[79] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_PHI, MIR_NOP, MIR_NOP, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_LOAD, MIR_NOP, MIR_INDEX_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_PHI, MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_LOAD, MIR_RETURN
    };
    static const struct MirRecoveryEdge edges[11] = {
        {17, 76}, {24, 58}, {28, 32}, {31, 52}, {42, 46},
        {45, 48}, {51, 52}, {54, 58}, {57, 60}, {62, 68},
        {75, 11}
    };
    static const struct MirRecoveryPhi phis[4] = {
        {13, 43, 42, 0, 70}, {49, 29, 30, 43, 46},
        {53, 19, 31, 29, 50}, {61, 33, 34, 55, 58}
    };
    int best_type;
    int best_storage;
    int best_offset;
    int index_type;
    int index_storage;
    int index_offset;
    int tasks_offset;
    int count_offset;

    memset(plan, 0, sizeof(*plan));
    if (!mir_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        !mir_recovery_edges(edges, sizeof(edges) / sizeof(edges[0])) ||
        !mir_recovery_phis(phis, sizeof(phis) / sizeof(phis[0])) ||
        mir_cfg_block_count() != 15 || mir.local_bytes != 4 ||
        mir.has_vla ||
        type_ptr_depth(mir.return_type) != 1 ||
        type_size(mir.return_type) != 2 ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &tasks_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &count_offset) ||
        count_offset != tasks_offset + 2 ||
        type_ptr_depth(mir.insns[1].type) != 1 ||
        type_size(mir.insns[1].type) != 2 ||
        !mir_byte_sum_signed_word_type(mir.insns[2].type) ||
        !mir_machine_named_nonvolatile(&mir.insns[1]) ||
        !mir_machine_named_nonvolatile(&mir.insns[2]))
        return mir_machine_reject(
            "best-record-schedule", "reloaded-shape");
    if (!mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[9].dst, 0) ||
        !mir_machine_named_nonvolatile(&mir.insns[5]) ||
        !mir_machine_named_nonvolatile(&mir.insns[10]) ||
        !mir_scalar_memory_location(
            &mir.insns[5], &best_type, &best_storage, &best_offset) ||
        !mir_scalar_memory_location(
            &mir.insns[10], &index_type, &index_storage, &index_offset) ||
        best_storage != SC_LOCAL || index_storage != SC_LOCAL ||
        best_offset == index_offset ||
        mir.insns[5].src1 != mir.insns[3].dst ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].object < 0 ||
        mir.insns[13].object != mir.insns[10].object ||
        mir.insns[16].src1 != mir.insns[13].dst ||
        mir.insns[16].src2 != mir.insns[2].dst ||
        mir.insns[16].immediate != '<')
        return mir_machine_reject(
            "best-record-schedule", "reloaded-loop-state");
    if (strcmp(mir.insns[18].name, mir.insns[1].name) ||
        mir.insns[18].type != mir.insns[1].type ||
        !mir_machine_named_nonvolatile(&mir.insns[18]) ||
        !mir_machine_same_location(&mir.insns[33], &mir.insns[18]) ||
        !mir_machine_same_location(&mir.insns[63], &mir.insns[18]) ||
        mir_machine_pointee_is_volatile(&mir.insns[1]) ||
        mir.insns[20].src1 != mir.insns[18].dst ||
        mir.insns[20].src2 != mir.insns[13].dst ||
        mir.insns[20].immediate <= 0 ||
        mir.insns[21].src1 != mir.insns[20].dst ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        mir.insns[22].memory_size != 1 ||
        (mir.insns[22].memory_flags & (1 | 8)) != 0 ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[23].immediate != '!' ||
        !mir_machine_same_location(&mir.insns[25], &mir.insns[5]) ||
        !mir_machine_constant_equals(mir.insns[26].dst, 0) ||
        mir.insns[27].src1 != mir.insns[25].dst ||
        mir.insns[27].src2 != mir.insns[26].dst ||
        mir.insns[27].immediate != TOK_EQ)
        return mir_machine_reject(
            "best-record-schedule", "reloaded-predicate");
    if (mir.insns[35].src1 != mir.insns[33].dst ||
        mir.insns[35].src2 != mir.insns[13].dst ||
        mir.insns[35].immediate != mir.insns[20].immediate ||
        mir.insns[36].src1 != mir.insns[35].dst ||
        mir.insns[37].src1 != mir.insns[36].dst ||
        mir.insns[37].memory_size != 2 ||
        (mir.insns[37].memory_flags & (1 | 8)) != 0 ||
        !mir_byte_sum_signed_word_type(mir.insns[37].type) ||
        !mir_machine_same_location(&mir.insns[38], &mir.insns[5]) ||
        mir.insns[39].src1 != mir.insns[38].dst ||
        mir.insns[40].src1 != mir.insns[39].dst ||
        mir.insns[40].memory_size != 2 ||
        (mir.insns[40].memory_flags & (1 | 8)) != 0 ||
        !mir_byte_sum_signed_word_type(mir.insns[40].type) ||
        mir.insns[41].src1 != mir.insns[37].dst ||
        mir.insns[41].src2 != mir.insns[40].dst ||
        mir.insns[41].immediate != '>')
        return mir_machine_reject(
            "best-record-schedule", "reloaded-priority");
    if (mir.insns[65].src1 != mir.insns[63].dst ||
        mir.insns[65].src2 != mir.insns[13].dst ||
        mir.insns[65].immediate != mir.insns[20].immediate ||
        !mir_machine_same_location(&mir.insns[67], &mir.insns[5]) ||
        mir.insns[67].src1 != mir.insns[65].dst ||
        !mir_machine_constant_equals(mir.insns[72].dst, 1) ||
        mir.insns[73].src1 != mir.insns[13].dst ||
        mir.insns[73].src2 != mir.insns[72].dst ||
        mir.insns[73].immediate != '+' ||
        !mir_machine_same_location(&mir.insns[74], &mir.insns[10]) ||
        mir.insns[74].src1 != mir.insns[73].dst ||
        !mir_machine_same_location(&mir.insns[77], &mir.insns[5]) ||
        mir.insns[78].src1 != mir.insns[77].dst)
        return mir_machine_reject(
            "best-record-schedule", "reloaded-update");
    plan->tasks_stack_offset = tasks_offset;
    plan->count_stack_offset = count_offset;
    plan->stride = (int)mir.insns[20].immediate;
    plan->done_offset = (int)mir.insns[21].immediate;
    plan->priority_offset = (int)mir.insns[36].immediate;
    return plan->stride > 0 && plan->stride <= 16 &&
           plan->done_offset >= 0 &&
           plan->done_offset < plan->stride &&
           plan->priority_offset >= 0 &&
           plan->priority_offset + 1 < plan->stride &&
           plan->priority_offset <= 125;
}

static void mir_emit_best_record_schedule(
    MirStream *out, const struct MirBestRecordSchedule *plan)
{
    int loop = new_label();
    int choose = new_label();
    int skip = new_label();
    int empty = new_label();
    int done = new_label();
    int step;

    mir_stream_printf(out,
            "%s\n"
            ";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
            "\tpush iy\n",
            MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tbit 7,b\n\tjp nz,L%d\n"
            "\tld a,b\n\tor c\n\tjp z,L%d\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld iy,0\n"
            "L%d:\n"
            "\tld h,d\n\tld l,e\n",
            plan->count_stack_offset + 2, empty, empty,
            plan->tasks_stack_offset + 2, loop);
    for (step = 0; step < plan->done_offset; ++step)
        mir_stream_puts("\tinc hl\n", out);
    mir_stream_printf(out,
            "\tld a,(hl)\n\tor a\n\tjp nz,L%d\n"
            "\tpush iy\n\tpop hl\n"
            "\tld a,h\n\tor l\n\tjp z,L%d\n"
            "\tpush de\n"
            "\tld h,d\n\tld l,e\n",
            skip, choose);
    for (step = 0; step < plan->priority_offset; ++step)
        mir_stream_puts("\tinc hl\n", out);
    mir_stream_printf(out,
            "\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n"
            "\tld e,(iy%+d)\n\tld d,(iy%+d)\n"
            "\tld a,h\n\txor 80h\n\tld h,a\n"
            "\tld a,d\n\txor 80h\n\tld d,a\n"
            "\tor a\n\tsbc hl,de\n\tpop de\n"
            "\tjp z,L%d\n\tjp c,L%d\n"
            "L%d:\n\tpush de\n\tpop iy\n"
            "L%d:\n",
            plan->priority_offset,
            plan->priority_offset + 1,
            skip, skip, choose, skip);
    for (step = 0; step < plan->stride; ++step)
        mir_stream_puts("\tinc de\n", out);
    mir_stream_printf(out,
            "\tdec bc\n\tld a,b\n\tor c\n\tjp nz,L%d\n"
            "\tpush iy\n\tpop hl\n\tjp L%d\n"
            "L%d:\n\tld hl,0\n"
            "L%d:\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            loop, done, empty, done);
}

static int mir_match_for_init_sum_schedule(
    struct MirForInitLoopSchedule *plan)
{
    static const unsigned char expected_opcodes[32] = {
        MIR_LABEL, MIR_PARAM, MIR_CONST, MIR_STORE,
        MIR_CONST, MIR_STORE, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_LABEL, MIR_NOP,
        MIR_PHI, MIR_PHI, MIR_NOP, MIR_NOP,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP,
        MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_RETURN
    };
    static const struct MirRecoveryEdge edges[2] =
        {{17, 29}, {28, 10}};
    static const struct MirRecoveryPhi phis[2] = {
        {12, 1, 14, 0, 23}, {13, 5, 18, 0, 23}
    };
    int stack_offset;

    memset(plan, 0, sizeof(*plan));
    if (!mir_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        !mir_recovery_edges(edges, 2) ||
        !mir_recovery_phis(phis, 2) ||
        mir_cfg_block_count() != 4 || mir.local_bytes != 4 ||
        mir.has_vla ||
        !mir_byte_sum_signed_word_type(mir.return_type) ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &stack_offset) ||
        !mir_byte_sum_signed_word_type(mir.insns[1].type) ||
        !mir_machine_named_nonvolatile(&mir.insns[1]) ||
        !mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[4].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[7].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[25].dst, 1))
        return 0;
    if (!mir_machine_unobservable_local_store(&mir.insns[3]) ||
        !mir_machine_unobservable_local_store(&mir.insns[5]) ||
        mir.insns[3].object < 0 || mir.insns[5].object < 0 ||
        mir.insns[3].object == mir.insns[5].object ||
        mir.insns[3].src1 != mir.insns[2].dst ||
        mir.insns[5].src1 != mir.insns[4].dst ||
        mir.insns[8].src1 != mir.insns[4].dst ||
        mir.insns[8].src2 != mir.insns[7].dst ||
        mir.insns[8].immediate != '+' ||
        !mir_machine_same_location(&mir.insns[9], &mir.insns[5]) ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[12].object != mir.insns[3].object ||
        mir.insns[13].object != mir.insns[5].object ||
        mir.insns[16].src1 != mir.insns[13].dst ||
        mir.insns[16].src2 != mir.insns[1].dst ||
        mir.insns[16].immediate != TOK_LE ||
        mir.insns[20].src1 != mir.insns[12].dst ||
        mir.insns[20].src2 != mir.insns[13].dst ||
        mir.insns[20].immediate != '+' ||
        !mir_machine_same_location(&mir.insns[22], &mir.insns[3]) ||
        mir.insns[22].src1 != mir.insns[20].dst ||
        mir.insns[26].src1 != mir.insns[13].dst ||
        mir.insns[26].src2 != mir.insns[25].dst ||
        mir.insns[26].immediate != '+' ||
        !mir_machine_same_location(&mir.insns[27], &mir.insns[5]) ||
        mir.insns[27].src1 != mir.insns[26].dst ||
        mir.insns[31].src1 != mir.insns[12].dst)
        return mir_machine_reject(
            "for-init-sum-schedule", "semantics");
    plan->kind = MIR_FOR_INIT_SUM;
    plan->value_stack_offset = stack_offset;
    return 1;
}

static int mir_match_for_init_pointer_walk_schedule(
    struct MirForInitLoopSchedule *plan)
{
    static const unsigned char expected_opcodes[34] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_LOAD,
        MIR_NOP, MIR_BINARY, MIR_STORE, MIR_CONST,
        MIR_STORE, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_LABEL, MIR_LOAD, MIR_NOP,
        MIR_PHI, MIR_LOAD, MIR_LOAD, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_LABEL, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL,
        MIR_NOP, MIR_RETURN
    };
    static const struct MirRecoveryEdge edges[2] =
        {{20, 31}, {30, 13}};
    static const struct MirRecoveryPhi phi =
        {16, 5, 17, 0, 25};
    int pointer_type;
    int pointer_storage;
    int pointer_local_offset;
    int steps_type;
    int steps_storage;
    int steps_local_offset;
    int pointer_offset;
    int length_offset;

    memset(plan, 0, sizeof(*plan));
    if (!mir_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        !mir_recovery_edges(edges, 2) ||
        !mir_recovery_phis(&phi, 1) ||
        mir_cfg_block_count() != 4 || mir.local_bytes != 4 ||
        mir.has_vla ||
        !mir_byte_sum_signed_word_type(mir.return_type) ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &pointer_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &length_offset) ||
        length_offset != pointer_offset + 2 ||
        !mir_byte_sum_pointer_type(mir.insns[1].type) ||
        !mir_byte_sum_signed_word_type(mir.insns[2].type) ||
        !mir_machine_named_nonvolatile(&mir.insns[1]) ||
        !mir_machine_named_nonvolatile(&mir.insns[2]) ||
        strcmp(mir.insns[3].name, mir.insns[1].name) ||
        mir.insns[3].type != mir.insns[1].type ||
        !mir_machine_named_nonvolatile(&mir.insns[3]) ||
        mir.insns[5].src1 != mir.insns[3].dst ||
        mir.insns[5].src2 != mir.insns[2].dst ||
        mir.insns[5].immediate != '+' ||
        !mir_machine_named_nonvolatile(&mir.insns[6]) ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        !mir_machine_constant_equals(mir.insns[7].dst, 0) ||
        !mir_machine_named_nonvolatile(&mir.insns[8]) ||
        mir.insns[8].src1 != mir.insns[7].dst ||
        !mir_scalar_memory_location(
            &mir.insns[6], &pointer_type, &pointer_storage,
            &pointer_local_offset) ||
        !mir_scalar_memory_location(
            &mir.insns[8], &steps_type, &steps_storage,
            &steps_local_offset) ||
        pointer_storage != SC_LOCAL || steps_storage != SC_LOCAL ||
        pointer_local_offset == steps_local_offset ||
        mir.insns[8].object < 0)
        return mir_machine_reject(
            "for-init-pointer-walk-schedule", "shape");
    if (!mir_machine_same_location(&mir.insns[9], &mir.insns[6]) ||
        !mir_machine_constant_equals(mir.insns[10].dst, 1) ||
        mir.insns[11].src1 != mir.insns[9].dst ||
        mir.insns[11].src2 != mir.insns[10].dst ||
        mir.insns[11].immediate != '-' ||
        !mir_machine_same_location(&mir.insns[12], &mir.insns[6]) ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        strcmp(mir.insns[14].name, mir.insns[1].name) ||
        mir.insns[14].type != mir.insns[1].type ||
        !mir_machine_named_nonvolatile(&mir.insns[14]) ||
        mir.insns[16].object != mir.insns[8].object ||
        !mir_machine_same_location(&mir.insns[17], &mir.insns[6]) ||
        strcmp(mir.insns[18].name, mir.insns[1].name) ||
        mir.insns[18].type != mir.insns[1].type ||
        !mir_machine_named_nonvolatile(&mir.insns[18]) ||
        mir.insns[19].src1 != mir.insns[17].dst ||
        mir.insns[19].src2 != mir.insns[18].dst ||
        mir.insns[19].immediate != TOK_NE ||
        !mir_machine_constant_equals(mir.insns[22].dst, 1) ||
        mir.insns[23].src1 != mir.insns[16].dst ||
        mir.insns[23].src2 != mir.insns[22].dst ||
        mir.insns[23].immediate != '+' ||
        !mir_machine_same_location(&mir.insns[24], &mir.insns[8]) ||
        mir.insns[24].src1 != mir.insns[23].dst ||
        !mir_machine_same_location(&mir.insns[26], &mir.insns[6]) ||
        !mir_machine_constant_equals(mir.insns[27].dst, 1) ||
        mir.insns[28].src1 != mir.insns[26].dst ||
        mir.insns[28].src2 != mir.insns[27].dst ||
        mir.insns[28].immediate != '-' ||
        !mir_machine_same_location(&mir.insns[29], &mir.insns[6]) ||
        mir.insns[29].src1 != mir.insns[28].dst ||
        mir.insns[33].src1 != mir.insns[16].dst)
        return mir_machine_reject(
            "for-init-pointer-walk-schedule", "semantics");
    plan->kind = MIR_FOR_INIT_POINTER_WALK;
    plan->value_stack_offset = length_offset;
    return 1;
}

static void mir_emit_for_init_loop_schedule(
    MirStream *out, const struct MirForInitLoopSchedule *plan)
{
    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    if (plan->kind == MIR_FOR_INIT_POINTER_WALK) {
        mir_stream_printf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n"
                "\tdec hl\n\tret\n",
                plan->value_stack_offset);
    } else {
        int loop = new_label();
        int done = new_label();

        mir_stream_printf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                "\tbit 7,d\n\tjp nz,L%d\n"
                "\tld a,d\n\tor e\n\tjp z,L%d\n"
                "\tld hl,0\n"
                "L%d:\n\tadd hl,de\n\tdec de\n"
                "\tld a,d\n\tor e\n\tjp nz,L%d\n\tret\n"
                "L%d:\n\tld hl,0\n\tret\n",
                plan->value_stack_offset, done, done,
                loop, loop, done);
    }
}

static int mir_match_post_index_report_schedule(
    struct MirPostIndexReportSchedule *plan)
{
    static const unsigned char expected_opcodes[43] = {
        MIR_LABEL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_ADDRESS, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_INDEX_ADDRESS, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_CONST, MIR_NOP, MIR_STORE, MIR_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_INDEX_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_NOP, MIR_ARG,
        MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN
    };
    struct Sym *global_index;
    struct Sym *global_array;
    struct Sym *print_function;
    int arguments[5];
    int local_type;
    int local_storage;
    int local_offset;
    long initial;
    long increment;
    long global_value;
    long local_value;

    memset(plan, 0, sizeof(*plan));
    if (!mir_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        mir_cfg_block_count() != 1 || mir.local_bytes != 8 ||
        mir.has_vla ||
        !mir_byte_sum_signed_word_type(mir.return_type))
        return 0;
    if (!mir_machine_evaluate_constant(
            mir.insns[1].dst, &initial, 0) ||
        initial != 1 ||
        !mir_machine_evaluate_constant(
            mir.insns[6].dst, &increment, 0) ||
        increment != 1 ||
        mir.insns[7].src1 != mir.insns[5].dst ||
        mir.insns[7].src2 != mir.insns[6].dst ||
        mir.insns[7].immediate != '+' ||
        mir.insns[3].src1 != mir.insns[1].dst ||
        mir.insns[8].src1 != mir.insns[7].dst ||
        !mir_machine_same_location(
            &mir.insns[3], &mir.insns[5]) ||
        !mir_machine_same_location(
            &mir.insns[3], &mir.insns[8]) ||
        !mir_machine_same_location(
            &mir.insns[3], &mir.insns[26]) ||
        !mir_machine_named_nonvolatile(&mir.insns[3]))
        return mir_machine_reject(
            "post-index-report-schedule", "global-index");
    global_index = find_global(mir.insns[3].name);
    global_array = find_global(mir.insns[4].name);
    if (global_index == NULL || global_array == NULL ||
        global_index == global_array ||
        global_index->storage != SC_GLOBAL ||
        global_index->is_array || global_index->is_volatile ||
        !mir_byte_sum_signed_word_type(global_index->type) ||
        (global_array->storage != SC_GLOBAL &&
         global_array->storage != SC_EXTERN) ||
        !global_array->is_array || global_array->is_volatile ||
        global_array->pointee_is_volatile ||
        global_array->elem_size != 2 ||
        !mir_machine_named_nonvolatile(&mir.insns[4]) ||
        mir.insns[9].src1 != mir.insns[4].dst ||
        mir.insns[9].src2 != mir.insns[5].dst ||
        mir.insns[9].immediate != 2 ||
        mir.insns[9].memory_size != 2 ||
        !mir_machine_evaluate_constant(
            mir.insns[10].dst, &global_value, 0) ||
        mir.insns[11].src1 != mir.insns[9].dst ||
        mir.insns[11].src2 != mir.insns[10].dst ||
        mir.insns[11].memory_size != 2 ||
        (mir.insns[11].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject(
            "post-index-report-schedule", "global-array");
    if (!mir_machine_evaluate_constant(
            mir.insns[12].dst, &initial, 0) ||
        initial != 1 ||
        !mir_scalar_memory_location(
            &mir.insns[14], &local_type,
            &local_storage, &local_offset) ||
        local_storage != SC_LOCAL ||
        !mir_byte_sum_signed_word_type(local_type) ||
        mir.insns[14].src1 != mir.insns[12].dst ||
        !mir_machine_same_location(
            &mir.insns[14], &mir.insns[19]) ||
        mir.insns[18].src1 != mir.insns[12].dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        !mir_machine_constant_equals(mir.insns[17].dst, 1) ||
        mir.insns[18].immediate != '+' ||
        mir.insns[19].src1 != mir.insns[18].dst ||
        mir.insns[20].src1 != mir.insns[15].dst ||
        mir.insns[20].src2 != mir.insns[12].dst ||
        mir.insns[20].immediate != 1 ||
        mir.insns[20].memory_size != 1 ||
        !mir_machine_evaluate_constant(
            mir.insns[22].dst, &local_value, 0) ||
        mir.insns[23].src1 != mir.insns[20].dst ||
        mir.insns[23].src2 != mir.insns[22].dst ||
        mir.insns[23].memory_size != 1 ||
        (mir.insns[23].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_named_nonvolatile(&mir.insns[15]) ||
        strcmp(mir.insns[15].name, mir.insns[35].name) ||
        mir.insns[38].src1 != mir.insns[37].dst ||
        mir.insns[38].memory_size != 1)
        return mir_machine_reject(
            "post-index-report-schedule", "local-index");
    print_function = NULL;
    if (!mir_aggregate_direct_function(40, &print_function) ||
        print_function == NULL || !print_function->has_proto ||
        !print_function->proto_variadic ||
        print_function->proto_nargs != 1 ||
        mir.insns[40].base_name[0] == 0 ||
        !mir_aggregate_five_call_arguments(
            &mir.insns[40], arguments) ||
        arguments[0] != mir.insns[24].dst ||
        arguments[1] != mir.insns[26].dst ||
        arguments[2] != mir.insns[31].dst ||
        arguments[3] != mir.insns[18].dst ||
        arguments[4] != mir.insns[38].dst ||
        find_global(mir.insns[28].name) != global_array ||
        !mir_machine_constant_equals(mir.insns[29].dst, 1) ||
        mir.insns[30].src1 != mir.insns[28].dst ||
        mir.insns[30].src2 != mir.insns[29].dst ||
        mir.insns[30].immediate != 2 ||
        mir.insns[31].src1 != mir.insns[30].dst ||
        mir.insns[31].memory_size != 2 ||
        !mir_machine_constant_equals(mir.insns[36].dst, 1) ||
        mir.insns[37].src1 != mir.insns[35].dst ||
        mir.insns[37].src2 != mir.insns[36].dst ||
        mir.insns[37].immediate != 1 ||
        !mir_machine_constant_equals(mir.insns[41].dst, 0) ||
        mir.insns[42].src1 != mir.insns[41].dst)
        return mir_machine_reject(
            "post-index-report-schedule", "report");
    plan->global_index = global_index;
    plan->global_array = global_array;
    plan->print_function = print_function;
    plan->format_string = (int)mir.insns[24].immediate;
    plan->final_index = 2;
    plan->global_array_offset = 2;
    plan->global_value = (int)global_value;
    plan->local_value = (int)local_value & 0xff;
    snprintf(plan->print_name, sizeof(plan->print_name), "%s",
             mir.insns[40].base_name);
    return 1;
}

static void mir_emit_post_index_report_schedule(
    MirStream *out, const struct MirPostIndexReportSchedule *plan)
{
    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out, "\tld hl,%d\n", plan->final_index);
    mir_machine_emit_global_word_store(
        out, plan->global_index, 0);
    mir_stream_printf(out, "\tld hl,%d\n", plan->global_value);
    mir_machine_emit_global_word_store(
        out, plan->global_array, plan->global_array_offset);
    mir_stream_printf(out,
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,%d\n\tpush hl\n",
            plan->local_value, plan->final_index);
    mir_machine_emit_global_word(
        out, plan->global_array, plan->global_array_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_global_word(out, plan->global_index, 0);
    mir_stream_printf(out,
            "\tpush hl\n\tld hl,S%d\n\tpush hl\n",
            plan->format_string);
    mir_emit_runtime_call(out, plan->print_name);
    mir_emit_final_call_cleanup(out, 5);
    mir_stream_puts("\tld hl,0\n\tret\n", out);
}

static int mir_pointer_cast_root(
    int address_instruction, struct Sym **root_out)
{
    const struct MirInsn *address =
        &mir.insns[address_instruction];
    struct Sym *root;

    if (address->opcode != MIR_ADDRESS ||
        !mir_machine_named_nonvolatile(address) ||
        type_ptr_depth(address->type) != 1 ||
        type_size(address->type) != 2 ||
        (root = find_global(address->name)) == NULL ||
        (root->storage != SC_GLOBAL &&
         root->storage != SC_EXTERN) ||
        !root->is_array || root->is_volatile ||
        root->pointee_is_volatile)
        return 0;
    *root_out = root;
    return 1;
}

static int mir_match_pointer_cast_diff_schedule(
    struct MirPointerCastDiffSchedule *plan)
{
    static const unsigned char expected_opcodes[100] = {
        MIR_LABEL, MIR_ADDRESS, MIR_NOP, MIR_STORE, MIR_ADDRESS, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_CONST, MIR_CONST, MIR_BINARY, MIR_BINARY,
        MIR_STORE, MIR_LOAD, MIR_NOP, MIR_LOAD, MIR_NOP, MIR_BINARY,
        MIR_STORE, MIR_ADDRESS, MIR_NOP, MIR_STORE, MIR_ADDRESS, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_CONST, MIR_CONST, MIR_BINARY, MIR_BINARY,
        MIR_STORE, MIR_LOAD, MIR_NOP, MIR_LOAD, MIR_NOP, MIR_BINARY,
        MIR_STORE, MIR_ADDRESS, MIR_NOP, MIR_STORE, MIR_ADDRESS, MIR_NOP,
        MIR_CONST, MIR_CONST, MIR_BINARY, MIR_BINARY, MIR_STORE, MIR_LOAD,
        MIR_NOP, MIR_LOAD, MIR_NOP, MIR_BINARY, MIR_STORE, MIR_CONST,
        MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_LABEL, MIR_CONST, MIR_NOP, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP, MIR_LABEL,
        MIR_CONST, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP, MIR_LABEL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN
    };
    static const int base_addresses[3] = {1, 19, 37};
    static const int base_stores[3] = {3, 21, 39};
    static const int end_addresses[3] = {4, 22, 40};
    static const int count_constants[3] = {8, 26, 42};
    static const int size_constants[3] = {9, 27, 43};
    static const int multiplies[3] = {10, 28, 44};
    static const int additions[3] = {11, 29, 45};
    static const int end_stores[3] = {12, 30, 46};
    static const int end_loads[3] = {13, 31, 47};
    static const int base_loads[3] = {15, 33, 49};
    static const int differences[3] = {17, 35, 51};
    static const int difference_stores[3] = {18, 36, 52};
    static const int expected_constants[3] = {53, 67, 81};
    static const int actual_uses[3] = {54, 68, 82};
    static const int comparisons[3] = {55, 69, 83};
    static const int branches[3] = {56, 70, 84};
    static const int next_labels[3] = {66, 80, 94};
    static const int string_instructions[3] = {57, 71, 85};
    static const int print_calls[3] = {61, 75, 89};
    static const int exit_constants[3] = {62, 76, 90};
    static const int exit_calls[3] = {64, 78, 92};
    struct Sym *print_function = NULL;
    struct Sym *exit_function = NULL;
    int local_type;
    int local_storage;
    int local_offset;
    int arguments[2];
    long count;
    long size;
    long expected;
    int group;

    memset(plan, 0, sizeof(*plan));
    if (!mir_recovery_opcode_sequence(
            expected_opcodes, sizeof(expected_opcodes)) ||
        mir_cfg_block_count() != 4 || mir.local_bytes != 18 ||
        mir.has_vla ||
        !mir_byte_sum_signed_word_type(mir.return_type))
        return 0;
    for (group = 0; group < 3; ++group) {
        if (!mir_pointer_cast_root(
                base_addresses[group], &plan->roots[group]) ||
            !mir_pointer_cast_root(
                end_addresses[group], &plan->roots[group]) ||
            find_global(mir.insns[base_addresses[group]].name) !=
                find_global(mir.insns[end_addresses[group]].name) ||
            !mir_machine_evaluate_constant(
                mir.insns[count_constants[group]].dst, &count, 0) ||
            !mir_machine_evaluate_constant(
                mir.insns[size_constants[group]].dst, &size, 0) ||
            count < 0 || count > 65535 || size <= 0 || size > 16 ||
            mir.insns[multiplies[group]].src1 !=
                mir.insns[count_constants[group]].dst ||
            mir.insns[multiplies[group]].src2 !=
                mir.insns[size_constants[group]].dst ||
            mir.insns[multiplies[group]].immediate != '*' ||
            mir.insns[additions[group]].src1 !=
                mir.insns[end_addresses[group]].dst ||
            mir.insns[additions[group]].src2 !=
                mir.insns[multiplies[group]].dst ||
            mir.insns[additions[group]].immediate != '+' ||
            mir.insns[base_stores[group]].src1 !=
                mir.insns[base_addresses[group]].dst ||
            mir.insns[end_stores[group]].src1 !=
                mir.insns[additions[group]].dst ||
            !mir_machine_same_location(
                &mir.insns[base_stores[group]],
                &mir.insns[base_loads[group]]) ||
            !mir_machine_same_location(
                &mir.insns[end_stores[group]],
                &mir.insns[end_loads[group]]) ||
            mir.insns[differences[group]].src1 !=
                mir.insns[end_loads[group]].dst ||
            mir.insns[differences[group]].src2 !=
                mir.insns[base_loads[group]].dst ||
            mir.insns[differences[group]].immediate != '-' ||
            mir.insns[difference_stores[group]].src1 !=
                mir.insns[differences[group]].dst ||
            !mir_scalar_memory_location(
                &mir.insns[difference_stores[group]],
                &local_type, &local_storage, &local_offset) ||
            local_storage != SC_LOCAL ||
            !mir_byte_sum_signed_word_type(local_type) ||
            !mir_machine_same_location(
                &mir.insns[difference_stores[group]],
                &mir.insns[actual_uses[group]]) ||
            !mir_machine_evaluate_constant(
                mir.insns[expected_constants[group]].dst,
                &expected, 0) ||
            mir.insns[comparisons[group]].src1 !=
                mir.insns[expected_constants[group]].dst ||
            mir.insns[comparisons[group]].src2 !=
                mir.insns[differences[group]].dst ||
            mir.insns[comparisons[group]].immediate != TOK_NE ||
            mir.insns[branches[group]].src1 !=
                mir.insns[comparisons[group]].dst ||
            mir.insns[branches[group]].label !=
                mir.insns[next_labels[group]].label)
            return mir_machine_reject(
                "pointer-cast-diff-schedule", "calculation");
        plan->actual[group] =
            ((unsigned long)count * (unsigned long)size) & 0xffffUL;
        plan->expected[group] =
            (unsigned long)expected & 0xffffUL;
        plan->failure_strings[group] =
            (int)mir.insns[string_instructions[group]].immediate;
        if (!mir_aggregate_direct_function(
                print_calls[group], &print_function) ||
            print_function == NULL || !print_function->proto_variadic ||
            mir.insns[print_calls[group]].base_name[0] == 0 ||
            !mir_machine_two_call_arguments(
                &mir.insns[print_calls[group]], arguments) ||
            arguments[0] !=
                mir.insns[string_instructions[group]].dst ||
            arguments[1] != mir.insns[differences[group]].dst ||
            !mir_machine_constant_equals(
                mir.insns[exit_constants[group]].dst, 1) ||
            mir.insns[exit_calls[group]].src1 >= 0 ||
            (exit_function =
                find_global(mir.insns[exit_calls[group]].name)) == NULL ||
            exit_function->storage != SC_FUNC ||
            exit_function->is_funcptr ||
            !exit_function->has_proto ||
            exit_function->proto_variadic ||
            exit_function->proto_nargs != 1 ||
            (exit_function->type & 15) != TYPE_VOID ||
            (mir.insns[exit_calls[group]].base_name[0] != 0 &&
             strcmp(mir.insns[exit_calls[group]].base_name,
                    asm_name_for(sym_asm_name(exit_function)))) ||
            !mir_machine_single_call_argument(
                &mir.insns[exit_calls[group]], &arguments[0]) ||
            arguments[0] != mir.insns[exit_constants[group]].dst)
            return mir_machine_reject(
                "pointer-cast-diff-schedule", "failure-calls");
        if (group == 0) {
            plan->print_function = print_function;
            plan->exit_function = exit_function;
            snprintf(plan->print_name, sizeof(plan->print_name), "%s",
                     mir.insns[print_calls[group]].base_name);
        } else if (plan->print_function != print_function ||
                   plan->exit_function != exit_function ||
                   strcmp(plan->print_name,
                          mir.insns[print_calls[group]].base_name)) {
            return mir_machine_reject(
                "pointer-cast-diff-schedule", "mixed-calls");
        }
    }
    if (!mir_aggregate_direct_function(97, &print_function) ||
        print_function != plan->print_function ||
        strcmp(plan->print_name, mir.insns[97].base_name) ||
        !mir_machine_single_call_argument(
            &mir.insns[97], &arguments[0]) ||
        arguments[0] != mir.insns[95].dst ||
        !mir_machine_constant_equals(mir.insns[98].dst, 0) ||
        mir.insns[99].src1 != mir.insns[98].dst)
        return mir_machine_reject(
            "pointer-cast-diff-schedule", "success");
    plan->success_string = (int)mir.insns[95].immediate;
    return 1;
}

static void mir_emit_pointer_cast_diff_schedule(
    MirStream *out, const struct MirPointerCastDiffSchedule *plan)
{
    int next[3];
    int group;

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (group = 0; group < 3; ++group) {
        next[group] = new_label();
        mir_stream_printf(out,
                "\tld hl,%lu\n\tld de,%lu\n"
                "\tor a\n\tsbc hl,de\n\tjp z,L%d\n"
                "\tld hl,%lu\n\tpush hl\n"
                "\tld hl,S%d\n\tpush hl\n",
                plan->actual[group], plan->expected[group],
                next[group], plan->actual[group],
                plan->failure_strings[group]);
        mir_emit_runtime_call(out, plan->print_name);
        mir_emit_final_call_cleanup(out, 2);
        mir_stream_puts("\tld hl,1\n\tpush hl\n", out);
        mir_machine_emit_symbol_call(out, plan->exit_function);
        mir_emit_final_call_cleanup(out, 1);
        mir_stream_printf(out, "L%d:\n", next[group]);
    }
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->success_string);
    mir_emit_runtime_call(out, plan->print_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_stream_puts("\tld hl,0\n\tret\n", out);
}

#define MIR_AGGREGATE_WORD_SUM_MAX_TERMS 8

struct MirAggregateWordSumTerm {
    int stack_offset;
    int width;
    int is_unsigned;
};

struct MirAggregateWordSumSchedule {
    struct MirAggregateWordSumTerm
        terms[MIR_AGGREGATE_WORD_SUM_MAX_TERMS];
    int term_count;
};

struct MirGlobalAppendScalarSchedule {
    struct Sym *array;
    struct Sym *count;
    int array_offset;
    int count_offset;
    int parameter_offsets[2];
    int parameter_count;
    int operation;
    int stride;
};

struct MirUnionAliasRunnerSchedule {
    struct Sym *print_function;
    int failure_string;
    int success_string;
    char failure_name[64];
    char success_name[64];
};

static int mir_aggregate_opcode_sequence(
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

static int mir_aggregate_has_volatile_memory(void)
{
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if ((insn->opcode == MIR_ADDRESS ||
             insn->opcode == MIR_LOAD ||
             insn->opcode == MIR_STORE) &&
            ((insn->memory_flags & (1 | 8)) != 0 ||
             !mir_machine_named_nonvolatile(insn)))
            return 1;
        if ((insn->opcode == MIR_MEMBER_ADDRESS ||
             insn->opcode == MIR_INDEX_ADDRESS ||
             insn->opcode == MIR_LOAD_INDIRECT ||
             insn->opcode == MIR_STORE_INDIRECT ||
             insn->opcode == MIR_COPY_AGGREGATE) &&
            (insn->memory_flags & (1 | 8)) != 0)
            return 1;
    }
    return 0;
}

static int mir_aggregate_word_sum_address(
    int value, int *stack_offset, int depth)
{
    const struct MirInsn *definition;

    if (depth > 16 || (definition = mir_definition(value)) == NULL)
        return 0;
    if (definition->opcode == MIR_ADDRESS) {
        int memory_type;
        int memory_storage;
        int memory_offset;

        if (!mir_machine_named_nonvolatile(definition) ||
            !mir_scalar_memory_location(
                definition, &memory_type,
                &memory_storage, &memory_offset) ||
            memory_storage != SC_PARAM ||
            memory_offset < 2)
            return 0;
        *stack_offset = memory_offset - 2;
        return 1;
    }
    if (definition->opcode == MIR_MEMBER_ADDRESS) {
        int offset;

        if ((definition->memory_flags & (1 | 8)) != 0 ||
            definition->bit_width != 0 ||
            definition->immediate < -32768 ||
            definition->immediate > 32767 ||
            !mir_aggregate_word_sum_address(
                definition->src1, &offset, depth + 1) ||
            offset + definition->immediate < 0 ||
            offset + definition->immediate > 32767)
            return 0;
        *stack_offset = offset + (int)definition->immediate;
        return 1;
    }
    if (definition->opcode == MIR_INDEX_ADDRESS) {
        long index;
        long adjustment;
        int offset;

        if ((definition->memory_flags & (1 | 8)) != 0 ||
            definition->immediate <= 0 ||
            !mir_machine_evaluate_constant(
                definition->src2, &index, 0) ||
            index < 0 ||
            index > 32767 / definition->immediate ||
            !mir_aggregate_word_sum_address(
                definition->src1, &offset, depth + 1))
            return 0;
        adjustment = index * definition->immediate;
        if (offset + adjustment < 0 ||
            offset + adjustment > 32767)
            return 0;
        *stack_offset = offset + (int)adjustment;
        return 1;
    }
    return 0;
}

static int mir_aggregate_word_sum_leaf(
    int value, struct MirAggregateWordSumSchedule *plan)
{
    const struct MirInsn *definition;
    struct MirAggregateWordSumTerm *term;
    int stack_offset;

    if (plan->term_count >= MIR_AGGREGATE_WORD_SUM_MAX_TERMS)
        return 0;
    definition = mir_definition(value);
    while (definition != NULL &&
           definition->opcode == MIR_UNARY) {
        if (definition->immediate != 0 ||
            type_ptr_depth(definition->type) != 0 ||
            type_is_float(definition->type) ||
            type_size(definition->type) != 2)
            return 0;
        definition = mir_definition(definition->src1);
    }
    if (definition == NULL)
        return 0;
    term = &plan->terms[plan->term_count];
    memset(term, 0, sizeof(*term));
    if (definition->opcode == MIR_PARAM) {
        if (type_ptr_depth(definition->type) != 0 ||
            type_is_float(definition->type) ||
            type_size(definition->type) != 2 ||
            !mir_machine_parameter_value_offset(
                definition->dst, &stack_offset))
            return 0;
        term->stack_offset = stack_offset;
        term->width = 2;
        term->is_unsigned =
            (definition->type & TYPE_UNSIGNED) != 0;
    } else if (definition->opcode == MIR_LOAD_INDIRECT) {
        if ((definition->memory_size != 1 &&
             definition->memory_size != 2) ||
            definition->bit_width != 0 ||
            (definition->memory_flags & (1 | 8)) != 0 ||
            type_ptr_depth(definition->type) != 0 ||
            type_is_float(definition->type) ||
            !mir_aggregate_word_sum_address(
                definition->src1, &stack_offset, 0))
            return 0;
        term->stack_offset = stack_offset;
        term->width = definition->memory_size;
        term->is_unsigned =
            (definition->type & TYPE_UNSIGNED) != 0;
    } else {
        return 0;
    }
    ++plan->term_count;
    return 1;
}

static int mir_aggregate_word_sum_tree(
    int value, struct MirAggregateWordSumSchedule *plan, int depth)
{
    const struct MirInsn *definition;

    if (depth > MIR_AGGREGATE_WORD_SUM_MAX_TERMS ||
        (definition = mir_definition(value)) == NULL)
        return 0;
    if (definition->opcode == MIR_BINARY &&
        definition->immediate == '+' &&
        type_ptr_depth(definition->type) == 0 &&
        !type_is_float(definition->type) &&
        type_size(definition->type) == 2)
        return mir_aggregate_word_sum_tree(
                   definition->src1, plan, depth + 1) &&
               mir_aggregate_word_sum_tree(
                   definition->src2, plan, depth + 1);
    return mir_aggregate_word_sum_leaf(value, plan);
}

static int mir_match_aggregate_word_sum_schedule(
    struct MirAggregateWordSumSchedule *plan)
{
    const struct MirInsn *return_insn = NULL;
    int binary_count = 0;
    int return_count = 0;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        type_ptr_depth(mir.return_type) != 0 ||
        type_is_float(mir.return_type) ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_LABEL:
        case MIR_NOP:
        case MIR_PARAM:
        case MIR_ADDRESS:
        case MIR_CONST:
            break;
        case MIR_MEMBER_ADDRESS:
        case MIR_INDEX_ADDRESS:
        case MIR_LOAD_INDIRECT:
            if ((insn->memory_flags & (1 | 8)) != 0)
                return 0;
            break;
        case MIR_UNARY:
            if (insn->immediate != 0)
                return 0;
            break;
        case MIR_BINARY:
            if (insn->immediate != '+' ||
                type_size(insn->type) != 2 ||
                type_is_float(insn->type))
                return 0;
            ++binary_count;
            break;
        case MIR_RETURN:
            ++return_count;
            return_insn = insn;
            break;
        default:
            return 0;
        }
    }
    if (return_count != 1 || return_insn == NULL ||
        !mir_aggregate_word_sum_tree(
            return_insn->src1, plan, 0) ||
        plan->term_count < 3 ||
        binary_count != plan->term_count - 1)
        return 0;
    return 1;
}

static void mir_emit_aggregate_word_sum_schedule(
    MirStream *out, const struct MirAggregateWordSumSchedule *plan)
{
    int term;

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\tld bc,0\n", out);
    for (term = 0; term < plan->term_count; ++term) {
        const struct MirAggregateWordSumTerm *item =
            &plan->terms[term];

        mir_stream_printf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld e,(hl)\n",
                item->stack_offset);
        if (item->width == 2) {
            mir_stream_puts("\tinc hl\n\tld d,(hl)\n", out);
        } else if (item->is_unsigned) {
            mir_stream_puts("\tld d,0\n", out);
        } else {
            mir_stream_puts("\tld a,e\n\trlca\n\tsbc a,a\n\tld d,a\n",
                  out);
        }
        mir_stream_puts("\tld l,c\n\tld h,b\n\tadd hl,de\n"
              "\tld c,l\n\tld b,h\n", out);
    }
    mir_stream_puts("\tld l,c\n\tld h,b\n\tret\n", out);
}

static int mir_match_global_append_scalar_schedule(
    struct MirGlobalAppendScalarSchedule *plan)
{
    static const unsigned char direct_opcodes[10] = {
        MIR_LABEL, MIR_PARAM, MIR_ADDRESS, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_INDEX_ADDRESS,
        MIR_NOP, MIR_STORE_INDIRECT
    };
    static const unsigned char binary_opcodes[13] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_ADDRESS,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_NOP, MIR_BINARY,
        MIR_STORE_INDIRECT
    };
    const struct MirInsn *address;
    const struct MirInsn *load;
    const struct MirInsn *increment;
    const struct MirInsn *store;
    const struct MirInsn *index;
    const struct MirInsn *value;
    const struct MirInsn *destination;
    struct Sym *array;
    struct Sym *count;
    long array_offset;
    int memory_type;
    int memory_storage;
    int count_offset;
    int parameter_count;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        (mir.return_type & 15) != TYPE_VOID ||
        mir.local_bytes != 0 || mir.aggregate_temp_bytes != 0)
        return 0;
    if (mir_aggregate_opcode_sequence(
            direct_opcodes, sizeof(direct_opcodes)))
        parameter_count = 1;
    else if (mir_aggregate_opcode_sequence(
                 binary_opcodes, sizeof(binary_opcodes)))
        parameter_count = 2;
    else
        return 0;
    address = &mir.insns[parameter_count + 1];
    load = &mir.insns[parameter_count + 2];
    increment = &mir.insns[parameter_count + 4];
    store = &mir.insns[parameter_count + 5];
    index = &mir.insns[parameter_count + 6];
    value = parameter_count == 1
        ? &mir.insns[8] : &mir.insns[11];
    destination = &mir.insns[mir.count - 1];
    if (!mir_machine_global_address_offset(
            address->dst, &array, &array_offset, 0) ||
        array == NULL || array->is_volatile ||
        array->pointee_is_volatile || !array->is_array ||
        array->elem_size != 2 ||
        array_offset < -32768 || array_offset > 32767 ||
        !mir_scalar_memory_location(
            load, &memory_type,
            &memory_storage, &count_offset) ||
        (memory_storage != SC_GLOBAL &&
         memory_storage != SC_EXTERN) ||
        !mir_machine_named_nonvolatile(load) ||
        (count = find_global(load->name)) == NULL ||
        count->is_volatile ||
        !mir_machine_same_location(load, store) ||
        !mir_machine_constant_equals(
            mir.insns[parameter_count + 3].dst, 1) ||
        increment->src1 != load->dst ||
        increment->src2 !=
            mir.insns[parameter_count + 3].dst ||
        increment->immediate != '+' ||
        store->src1 != increment->dst ||
        index->src1 != address->dst ||
        index->src2 != load->dst ||
        index->immediate != 2 ||
        index->memory_size != 2 ||
        destination->src1 != index->dst ||
        destination->src2 !=
            (parameter_count == 1
                 ? mir.insns[1].dst : value->dst) ||
        destination->memory_size != 2 ||
        (destination->memory_flags & (1 | 8)) != 0)
        return mir_machine_reject(
            "global-append-scalar-schedule", "shape");
    if (parameter_count == 1) {
        if (value->opcode != MIR_NOP)
            return 0;
    } else if (value->opcode != MIR_BINARY ||
               value->src1 != mir.insns[1].dst ||
               value->src2 != mir.insns[2].dst ||
               (value->immediate != '+' &&
                value->immediate != '-' &&
                value->immediate != '&' &&
                value->immediate != '|' &&
                value->immediate != '^')) {
        return 0;
    }
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->parameter_offsets[0]) ||
        !mir_byte_sum_signed_word_type(mir.insns[1].type))
        return 0;
    if (parameter_count == 2 &&
        (!mir_machine_parameter_value_offset(
             mir.insns[2].dst, &plan->parameter_offsets[1]) ||
         plan->parameter_offsets[1] !=
             plan->parameter_offsets[0] + 2 ||
         !mir_byte_sum_signed_word_type(mir.insns[2].type)))
        return 0;
    plan->array = array;
    plan->count = count;
    plan->array_offset = (int)array_offset;
    plan->count_offset = count_offset;
    plan->parameter_count = parameter_count;
    plan->operation = parameter_count == 1
        ? 0 : (int)value->immediate;
    plan->stride = 2;
    return 1;
}

static void mir_aggregate_emit_stack_word(
    MirStream *out, int stack_offset)
{
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tex de,hl\n",
            stack_offset);
}

static void mir_emit_global_append_scalar_schedule(
    MirStream *out, const struct MirGlobalAppendScalarSchedule *plan)
{
    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_word(
        out, plan->count, plan->count_offset);
    mir_stream_puts("\tld c,l\n\tld b,h\n\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->count, plan->count_offset);
    mir_stream_puts("\tld l,c\n\tld h,b\n\tadd hl,hl\n", out);
    mir_machine_emit_global_address_de(
        out, plan->array, plan->array_offset);
    mir_stream_puts("\tadd hl,de\n\tld c,l\n\tld b,h\n", out);
    if (plan->parameter_count == 1) {
        mir_aggregate_emit_stack_word(
            out, plan->parameter_offsets[0]);
    } else {
        mir_stream_puts("\tpush bc\n", out);
        mir_aggregate_emit_stack_word(
            out, plan->parameter_offsets[0] + 2);
        mir_stream_puts("\tpush hl\n", out);
        mir_aggregate_emit_stack_word(
            out, plan->parameter_offsets[1] + 4);
        mir_stream_puts("\tex de,hl\n\tpop hl\n", out);
        if (plan->operation == '+')
            mir_stream_puts("\tadd hl,de\n", out);
        else if (plan->operation == '-') {
            mir_stream_puts("\tor a\n\tsbc hl,de\n", out);
        } else if (plan->operation == '&') {
            mir_stream_puts("\tld a,l\n\tand e\n\tld l,a\n"
                  "\tld a,h\n\tand d\n\tld h,a\n", out);
        } else if (plan->operation == '|') {
            mir_stream_puts("\tld a,l\n\tor e\n\tld l,a\n"
                  "\tld a,h\n\tor d\n\tld h,a\n", out);
        } else {
            mir_stream_puts("\tld a,l\n\txor e\n\tld l,a\n"
                  "\tld a,h\n\txor d\n\tld h,a\n", out);
        }
        mir_stream_puts("\tpop bc\n", out);
    }
    mir_stream_puts("\tld a,l\n\tld (bc),a\n\tinc bc\n"
          "\tld a,h\n\tld (bc),a\n\tret\n", out);
}

static int mir_match_union_alias_runner_schedule(
    struct MirUnionAliasRunnerSchedule *plan)
{
    int failure_argument;
    int success_argument;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 115 || mir.next_value != 82 ||
        mir_cfg_block_count() != 8 || mir.local_bytes != 8 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        mir_aggregate_has_volatile_memory() ||
        mir_has_cfg_backedge() ||
        !mir_byte_sum_signed_word_type(mir.return_type) ||
        !mir_machine_constant_equals(mir.insns[1].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[6].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[13].dst, 52) ||
        !mir_machine_constant_equals(mir.insns[19].dst, 18) ||
        !mir_machine_constant_equals(mir.insns[21].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[22].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[33].dst, 52) ||
        !mir_machine_constant_equals(mir.insns[45].dst, 18) ||
        !mir_machine_constant_equals(mir.insns[56].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[63].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[65].dst, 4) ||
        !mir_machine_constant_equals(mir.insns[66].dst, 4) ||
        !mir_machine_constant_equals(mir.insns[78].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[88].dst, 1234) ||
        !mir_machine_constant_equals(mir.insns[93].dst, 1234) ||
        !mir_machine_constant_equals(mir.insns[106].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[113].dst, 0))
        return 0;
    if (mir.insns[11].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[11].immediate != 0 ||
        mir.insns[17].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[17].immediate != 1 ||
        mir.insns[31].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[31].immediate != 0 ||
        mir.insns[43].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[43].immediate != 1 ||
        mir.insns[54].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[54].immediate != 0 ||
        mir.insns[59].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[59].immediate != 0 ||
        mir.insns[74].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[74].immediate != 0 ||
        mir.insns[87].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[87].immediate != 0 ||
        mir.insns[91].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[91].immediate != 0)
        return mir_machine_reject(
            "union-alias-runner-schedule", "layout");
    plan->print_function =
        find_global(mir.insns[105].name);
    if (plan->print_function == NULL ||
        find_global(mir.insns[112].name) != plan->print_function ||
        !mir_machine_single_call_argument(
            &mir.insns[105], &failure_argument) ||
        !mir_machine_single_call_argument(
            &mir.insns[112], &success_argument) ||
        mir_definition(failure_argument) == NULL ||
        mir_definition(failure_argument)->opcode !=
            MIR_STRING_ADDRESS ||
        mir_definition(success_argument) == NULL ||
        mir_definition(success_argument)->opcode !=
            MIR_STRING_ADDRESS)
        return mir_machine_reject(
            "union-alias-runner-schedule", "prints");
    plan->failure_string =
        (int)mir_definition(failure_argument)->immediate;
    plan->success_string =
        (int)mir_definition(success_argument)->immediate;
    snprintf(plan->failure_name, sizeof(plan->failure_name), "%s",
             mir.insns[105].base_name);
    snprintf(plan->success_name, sizeof(plan->success_name), "%s",
             mir.insns[112].base_name);
    return plan->failure_name[0] != 0 &&
           plan->success_name[0] != 0;
}

static void mir_emit_union_alias_runner_schedule(
    MirStream *out, const struct MirUnionAliasRunnerSchedule *plan)
{
    int failed = new_label();

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-6\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\tld (ix-2),52\n\tld (ix-1),18\n"
          "\tld a,(ix-2)\n\tcp 52\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", failed);
    mir_stream_puts("\tld a,(ix-1)\n\tcp 18\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", failed);
    mir_stream_puts("\txor a\n\tld (ix-6),a\n\tld (ix-5),a\n"
          "\tld (ix-4),a\n\tld (ix-3),a\n"
          "\tinc a\n\tld (ix-6),a\n\tcp (ix-6)\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", failed);
    mir_stream_puts("\tld hl,1234\n\tld (ix-6),l\n\tld (ix-5),h\n"
          "\tld e,(ix-6)\n\tld d,(ix-5)\n"
          "\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out,
            "\tjp nz,L%d\n\tld hl,S%d\n\tpush hl\n",
            failed, plan->success_string);
    mir_emit_runtime_call(out, plan->success_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_stream_puts("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
    mir_stream_printf(out, "L%d:\n\tld hl,S%d\n\tpush hl\n",
            failed, plan->failure_string);
    mir_emit_runtime_call(out, plan->failure_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_stream_puts("\tld hl,1\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

struct MirUnnamedBitfieldReportSchedule {
    struct Sym *print_function;
    int strings[6];
    int argument_counts[6];
    int values[6][3];
    char print_name[64];
};

static int mir_unnamed_bitfield_value(
    int address_instruction, int member_instruction,
    int load_instruction, int store_instruction,
    int *value_out)
{
    const struct MirInsn *address =
        &mir.insns[address_instruction];
    const struct MirInsn *member =
        &mir.insns[member_instruction];
    const struct MirInsn *load =
        &mir.insns[load_instruction];
    const struct MirInsn *store =
        &mir.insns[store_instruction];
    const struct MirInsn *constant =
        mir_definition(store->src1);
    unsigned long bits;
    unsigned long field_mask;

    if (address->opcode != MIR_ADDRESS ||
        member->opcode != MIR_MEMBER_ADDRESS ||
        member->src1 != address->dst ||
        load->opcode != MIR_LOAD_INDIRECT ||
        load->src1 != member->dst ||
        store->opcode != MIR_STORE ||
        constant == NULL || constant->opcode != MIR_CONST ||
        strcmp(address->name, store->name) ||
        member->memory_size != 2 || load->memory_size != 2 ||
        member->memory_flags != 0 || load->memory_flags != 0)
        return 0;
    bits = (unsigned long)constant->immediate & 0xffffUL;
    if (load->bit_width == 0) {
        *value_out = (int)bits;
        return 1;
    }
    if (load->bit_width < 1 || load->bit_width > 15 ||
        load->bit_shift < 0 ||
        load->bit_shift + load->bit_width > 16 ||
        member->bit_shift != load->bit_shift ||
        member->bit_width != load->bit_width ||
        member->bit_mask != load->bit_mask)
        return 0;
    field_mask = (1UL << load->bit_width) - 1UL;
    bits = (bits >> load->bit_shift) & field_mask;
    if ((load->type & TYPE_UNSIGNED) == 0 &&
        (bits & (1UL << (load->bit_width - 1))) != 0)
        bits |= ~field_mask;
    *value_out = (int)(bits & 0xffffUL);
    return 1;
}

static int mir_match_unnamed_bitfield_report_schedule(
    struct MirUnnamedBitfieldReportSchedule *plan)
{
    const int expected_opcodes[99] = {
        MIR_LABEL, MIR_CONST, MIR_STORE, MIR_CONST, MIR_STORE, MIR_CONST,
        MIR_STORE, MIR_CONST, MIR_STORE, MIR_CONST, MIR_STORE, MIR_CONST,
        MIR_STORE, MIR_CONST, MIR_STORE, MIR_CONST, MIR_STORE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_RETURN
    };
    const int call_instructions[6] = {30, 44, 54, 68, 82, 96};
    const int string_instructions[6] = {17, 31, 45, 55, 69, 83};
    const int argument_counts[6] = {4, 4, 3, 4, 4, 4};
    const int argument_instructions[6][4] = {
        {17, 21, 25, 27}, {31, 35, 39, 41},
        {45, 49, 51, -1}, {55, 59, 63, 65},
        {69, 73, 77, 79}, {83, 87, 91, 93}
    };
    const int field_specs[11][4] = {
        {19, 20, 21, 2}, {23, 24, 25, 2},
        {33, 34, 35, 4}, {37, 38, 39, 4},
        {47, 48, 49, 6},
        {57, 58, 59, 8}, {61, 62, 63, 10},
        {71, 72, 73, 12}, {75, 76, 77, 14},
        {85, 86, 87, 16}, {89, 90, 91, 16}
    };
    const int field_call_slots[11][2] = {
        {0, 0}, {0, 1}, {1, 0}, {1, 1}, {2, 0},
        {3, 0}, {3, 1}, {4, 0}, {4, 1}, {5, 0}, {5, 1}
    };
    int arguments[4];
    int instruction;
    int field;
    int call;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 99 || mir.next_value != 66 ||
        mir_cfg_block_count() != 1 || mir.local_bytes != 16 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        (mir.return_type & 15) != TYPE_INT)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "unnamed-bitfield-report", "opcodes");
    for (field = 0; field < 11; ++field) {
        int call_slot = field_call_slots[field][0];
        int value_slot = field_call_slots[field][1];

        if (!mir_unnamed_bitfield_value(
                field_specs[field][0], field_specs[field][1],
                field_specs[field][2], field_specs[field][3],
                &plan->values[call_slot][value_slot]))
            return mir_machine_reject(
                "unnamed-bitfield-report", "field");
    }
    for (call = 0; call < 6; ++call) {
        struct Sym *function = NULL;
        char call_name[64];
        long size_value;
        int argument;

        if (!mir_array_main_function(
                call_instructions[call], 1, 0,
                &function, call_name, sizeof(call_name)) ||
            !mir_packed_call_arguments(
                &mir.insns[call_instructions[call]],
                argument_counts[call], arguments))
            return mir_machine_reject(
                "unnamed-bitfield-report", "call");
        for (argument = 0;
             argument < argument_counts[call]; ++argument)
            if (arguments[argument] !=
                mir.insns[
                    argument_instructions[call][argument]].dst)
                return mir_machine_reject(
                    "unnamed-bitfield-report", "argument");
        if (call == 0) {
            plan->print_function = function;
            dcc_copy_str(
                plan->print_name, sizeof(plan->print_name),
                call_name);
        } else if (function != plan->print_function ||
                   strcmp(call_name, plan->print_name)) {
            return mir_machine_reject(
                "unnamed-bitfield-report", "print-alias");
        }
        plan->strings[call] =
            (int)mir.insns[string_instructions[call]].immediate;
        plan->argument_counts[call] = argument_counts[call];
        if (!mir_machine_evaluate_constant(
                arguments[argument_counts[call] - 1],
                &size_value, 0) ||
            size_value < 0 || size_value > 65535)
            return mir_machine_reject(
                "unnamed-bitfield-report", "size");
        plan->values[call][argument_counts[call] - 2] =
            (int)size_value;
    }
    if (plan->values[0][2] != 2 ||
        plan->values[1][2] != 2 ||
        plan->values[2][1] != 2 ||
        plan->values[3][2] != 4 ||
        plan->values[4][2] != 4 ||
        plan->values[5][2] != 2 ||
        !mir_machine_constant_equals(mir.insns[97].dst, 0) ||
        mir.insns[98].src1 != mir.insns[97].dst)
        return mir_machine_reject(
            "unnamed-bitfield-report", "result");
    return 1;
}

static void mir_emit_unnamed_bitfield_report_schedule(
    MirStream *out, const struct MirUnnamedBitfieldReportSchedule *plan)
{
    int call;

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (call = 0; call < 6; ++call) {
        int value_count = plan->argument_counts[call] - 1;
        int value;

        for (value = value_count - 1; value >= 0; --value)
            mir_emit_final_call_constant(
                out, (unsigned int)plan->values[call][value], 2);
        mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
                plan->strings[call]);
        mir_emit_runtime_call(out, plan->print_name);
        mir_emit_final_call_cleanup(
            out, plan->argument_counts[call]);
    }
    mir_stream_puts("\tld hl,0\n\tret\n", out);
}

struct MirAnonymousInitializerReportSchedule {
    struct Sym *print_function;
    struct Sym *check_function;
    struct Sym *string_check_function;
    struct Sym *failures;
    int print_strings[6];
    int print_counts[6];
    int print_values[6][3];
    int message_string;
    int check_names[14];
    int check_values[14];
    int string_check_name;
    int success_string;
    char print_name[64];
};

static int mir_anonymous_initializer_constant(
    int instruction, int *value_out)
{
    long value;

    if (instruction < 0 || instruction >= mir.count ||
        mir.insns[instruction].opcode != MIR_CONST ||
        !mir_machine_evaluate_constant(
            mir.insns[instruction].dst, &value, 0))
        return 0;
#if LONG_MAX > 0xffffffffL
    /* On LP64 hosts long can exceed 32 bits; reject values too wide to fit
     * as either a signed or unsigned 32-bit constant. On ILP32/LLP64 hosts
     * long is already exactly 32 bits, so every value is in range and this
     * check would be a tautology - skip it there instead of emitting a
     * comparison the compiler (correctly) flags as always-false. */
    if (value < -2147483647L - 1L || value > 0xffffffffL)
        return 0;
#endif
    *value_out = (int)(unsigned long)value;
    return 1;
}

static int mir_match_anonymous_initializer_report_schedule(
    struct MirAnonymousInitializerReportSchedule *plan)
{
    const int expected_opcodes[278] = {
        MIR_LABEL, MIR_CONST, MIR_STORE, MIR_CONST, MIR_STORE, MIR_CONST,
        MIR_STORE, MIR_CONST, MIR_STORE, MIR_CONST, MIR_STORE, MIR_CONST,
        MIR_STORE, MIR_CONST, MIR_STORE, MIR_CONST, MIR_STORE, MIR_CONST,
        MIR_STORE, MIR_STRING_ADDRESS, MIR_STORE, MIR_CONST, MIR_STORE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG,
        MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG,
        MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_ARG, MIR_NOP, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_ARG,
        MIR_NOP, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_UNARY, MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_ARG, MIR_NOP, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_ARG,
        MIR_NOP, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_UNARY, MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_ARG, MIR_NOP, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_ARG,
        MIR_NOP, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_UNARY, MIR_ARG, MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_ARG,
        MIR_NOP, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_UNARY, MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_ARG, MIR_NOP, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_ARG,
        MIR_NOP, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_ARG, MIR_NOP, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_RETURN
    };
    const int initial_constants[10] = {
        1, 3, 5, 7, 9, 11, 13, 15, 17, 21
    };
    const int initial_stores[10] = {
        2, 4, 6, 8, 10, 12, 14, 16, 18, 22
    };
    const int print_calls[6] = {37, 52, 67, 74, 89, 100};
    const int print_counts[6] = {4, 4, 4, 2, 4, 3};
    const int print_argument_instructions[6][4] = {
        {23, 27, 31, 35}, {38, 42, 46, 50},
        {53, 57, 61, 65}, {68, 72, -1, -1},
        {75, 79, 83, 87}, {90, 94, 98, -1}
    };
    const int check_calls[14] = {
        111, 122, 133, 144, 155, 166, 177,
        188, 201, 212, 223, 234, 245, 265
    };
    const int check_name_instructions[14] = {
        101, 112, 123, 134, 145, 156, 167,
        178, 189, 202, 213, 224, 235, 255
    };
    const int check_actual_instructions[14] = {
        106, 117, 128, 139, 150, 161, 172,
        183, 194, 207, 218, 229, 240, 260
    };
    const int check_expected_instructions[14] = {
        109, 120, 131, 142, 153, 164, 175,
        186, 199, 210, 221, 232, 243, 263
    };
    int initial[10];
    int print_arguments[4];
    int check_arguments[3];
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 278 || mir.next_value != 196 ||
        mir_cfg_block_count() != 2 || mir.local_bytes != 17 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        (mir.return_type & 15) != TYPE_INT)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "anonymous-initializer-report", "opcodes");
    for (item = 0; item < 10; ++item)
        if (!mir_anonymous_initializer_constant(
                initial_constants[item], &initial[item]) ||
            mir.insns[initial_stores[item]].src1 !=
                mir.insns[initial_constants[item]].dst)
            return mir_machine_reject(
                "anonymous-initializer-report", "initializers");
    if (strcmp(mir.insns[6].name, mir.insns[8].name) ||
        strcmp(mir.insns[10].name, mir.insns[12].name) ||
        strcmp(mir.insns[14].name, mir.insns[16].name) ||
        strcmp(mir.insns[14].name, mir.insns[18].name) ||
        strcmp(mir.insns[20].name, mir.insns[22].name) ||
        mir.insns[19].opcode != MIR_STRING_ADDRESS ||
        mir.insns[20].src1 != mir.insns[19].dst)
        return mir_machine_reject(
            "anonymous-initializer-report", "storage");

    plan->message_string = (int)mir.insns[19].immediate;
    plan->print_values[0][0] = initial[0] & 15;
    plan->print_values[0][1] = (initial[0] >> 4) & 15;
    plan->print_values[0][2] = (initial[0] >> 8) & 255;
    plan->print_values[1][0] = initial[1] & 31;
    plan->print_values[1][1] = (initial[1] >> 5) & 15;
    plan->print_values[1][2] = (initial[1] >> 9) & 127;
    plan->print_values[2][0] = initial[2] & 255;
    plan->print_values[2][1] = initial[3] & 255;
    plan->print_values[2][2] =
        (initial[2] & 255) | ((initial[3] & 255) << 8);
    plan->print_values[3][0] = initial[4] & 255;
    plan->print_values[4][0] = initial[6] & 255;
    plan->print_values[4][1] = initial[7] & 0xffff;
    plan->print_values[4][2] = initial[8] & 0xffff;
    plan->print_values[5][0] = initial[9] & 0xffff;
    for (item = 0; item < 6; ++item) {
        struct Sym *function = NULL;
        char call_name[64];
        int argument;

        if (!mir_array_main_function(
                print_calls[item], 1, 0,
                &function, call_name, sizeof(call_name)) ||
            !mir_packed_call_arguments(
                &mir.insns[print_calls[item]],
                print_counts[item], print_arguments))
            return mir_machine_reject(
                "anonymous-initializer-report", "print-call");
        for (argument = 0;
             argument < print_counts[item]; ++argument)
            if (print_arguments[argument] !=
                mir.insns[
                    print_argument_instructions[item][argument]].dst)
                return mir_machine_reject(
                    "anonymous-initializer-report", "print-argument");
        if (item == 0) {
            plan->print_function = function;
            dcc_copy_str(
                plan->print_name, sizeof(plan->print_name),
                call_name);
        } else if (function != plan->print_function ||
                   strcmp(call_name, plan->print_name)) {
            return mir_machine_reject(
                "anonymous-initializer-report", "print-alias");
        }
        plan->print_strings[item] =
            (int)mir.insns[
                print_argument_instructions[item][0]].immediate;
        plan->print_counts[item] = print_counts[item];
    }
    for (item = 0; item < 14; ++item) {
        struct Sym *function = NULL;
        char call_name[64];
        long expected;

        if (!mir_array_main_function(
                check_calls[item], 0, 0,
                &function, call_name, sizeof(call_name)) ||
            !mir_packed_call_arguments(
                &mir.insns[check_calls[item]], 3,
                check_arguments) ||
            check_arguments[0] !=
                mir.insns[check_name_instructions[item]].dst ||
            check_arguments[1] !=
                mir.insns[check_actual_instructions[item]].dst ||
            check_arguments[2] !=
                mir.insns[check_expected_instructions[item]].dst ||
            !mir_machine_evaluate_constant(
                check_arguments[2], &expected, 0))
            return mir_machine_reject(
                "anonymous-initializer-report", "check-call");
        if (item == 0)
            plan->check_function = function;
        else if (function != plan->check_function)
            return mir_machine_reject(
                "anonymous-initializer-report", "check-alias");
        plan->check_names[item] =
            (int)mir.insns[
                check_name_instructions[item]].immediate;
        plan->check_values[item] =
            (int)(unsigned long)expected;
    }
    {
        const int expected_values[14] = {
            plan->print_values[0][0],
            plan->print_values[0][1],
            plan->print_values[0][2],
            plan->print_values[1][0],
            plan->print_values[1][1],
            plan->print_values[1][2],
            plan->print_values[2][0],
            plan->print_values[2][1],
            plan->print_values[2][2],
            plan->print_values[3][0],
            plan->print_values[4][0],
            plan->print_values[4][1],
            plan->print_values[4][2],
            plan->print_values[5][0]
        };

        for (item = 0; item < 14; ++item)
            if ((unsigned long)plan->check_values[item] !=
                (unsigned long)expected_values[item])
                return mir_machine_reject(
                    "anonymous-initializer-report",
                    "check-value");
    }
    {
        struct Sym *function = NULL;
        char call_name[64];

        if (!mir_array_main_function(
                254, 0, 0, &function,
                call_name, sizeof(call_name)) ||
            !mir_packed_call_arguments(
                &mir.insns[254], 3, check_arguments) ||
            check_arguments[0] != mir.insns[246].dst ||
            check_arguments[1] != mir.insns[250].dst ||
            check_arguments[2] != mir.insns[252].dst ||
            mir.insns[252].immediate != plan->message_string)
            return mir_machine_reject(
                "anonymous-initializer-report", "string-check");
        plan->string_check_function = function;
        plan->string_check_name =
            (int)mir.insns[246].immediate;
    }
    plan->failures = find_global(mir.insns[266].name);
    if (plan->failures == NULL ||
        plan->failures->storage != SC_GLOBAL ||
        plan->failures->is_volatile ||
        !mir_machine_same_location(
            &mir.insns[266], &mir.insns[274]) ||
        mir.insns[268].immediate != TOK_EQ ||
        mir.insns[269].src1 != mir.insns[268].dst ||
        mir.insns[276].immediate != TOK_NE ||
        mir.insns[277].src1 != mir.insns[276].dst)
        return mir_machine_reject(
            "anonymous-initializer-report", "final");
    {
        struct Sym *function = NULL;
        char call_name[64];
        int argument;

        if (!mir_array_main_function(
                272, 1, 0, &function,
                call_name, sizeof(call_name)) ||
            function != plan->print_function ||
            strcmp(call_name, plan->print_name) ||
            !mir_packed_call_arguments(
                &mir.insns[272], 1, &argument) ||
            argument != mir.insns[270].dst)
            return mir_machine_reject(
                "anonymous-initializer-report", "success");
        plan->success_string = (int)mir.insns[270].immediate;
    }
    return 1;
}

static void mir_emit_anonymous_initializer_report_schedule(
    MirStream *out, const struct MirAnonymousInitializerReportSchedule *plan)
{
    int skip_success = new_label();
    int return_done = new_label();
    int item;

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (item = 0; item < 6; ++item) {
        if (item == 5) {
            mir_emit_final_call_constant(
                out, (unsigned int)plan->print_values[item][0], 2);
            mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
                    plan->message_string);
        } else {
            int argument;
            int value_count = plan->print_counts[item] - 1;

            for (argument = value_count - 1;
                 argument >= 0; --argument)
                mir_emit_final_call_constant(
                    out,
                    (unsigned int)plan->print_values[item][argument],
                    2);
        }
        mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
                plan->print_strings[item]);
        mir_emit_runtime_call(out, plan->print_name);
        mir_emit_final_call_cleanup(
            out, plan->print_counts[item]);
    }
    for (item = 0; item < 14; ++item) {
        mir_emit_final_call_constant(
            out, (unsigned long)plan->check_values[item], 4);
        mir_emit_final_call_constant(
            out, (unsigned long)plan->check_values[item], 4);
        mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
                plan->check_names[item]);
        mir_machine_emit_symbol_call(out, plan->check_function);
        mir_emit_final_call_cleanup(out, 5);
    }
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->message_string);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->message_string);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->string_check_name);
    mir_machine_emit_symbol_call(
        out, plan->string_check_function);
    mir_emit_final_call_cleanup(out, 3);

    mir_machine_emit_global_word(out, plan->failures, 0);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjr nz,L%d\n\tld hl,S%d\n\tpush hl\n",
            skip_success, plan->success_string);
    mir_emit_runtime_call(out, plan->print_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_stream_printf(out, "L%d:\n", skip_success);
    mir_machine_emit_global_word(out, plan->failures, 0);
    mir_stream_puts("\tld a,h\n\tor l\n\tld hl,0\n", out);
    mir_stream_printf(out, "\tjr z,L%d\n\tinc l\nL%d:\n",
            return_done, return_done);
    mir_stream_puts("\tret\n", out);
}

int mir_try_emit_aggregate_checks(MirStream *out)
{
    struct MirAggregateWordSumSchedule aggregate_word_sum;
    struct MirGlobalAppendScalarSchedule global_append;
    struct MirUnionAliasRunnerSchedule union_alias;
    struct MirBestRecordSchedule best_record;
    struct MirForInitLoopSchedule for_init_loop;
    struct MirLocalDeclarationReturnSchedule declaration_return;
    struct MirLocalInitializerSchedule local_initializer;
    struct MirInitializerCheckSchedule initializer_checks;
    struct MirLocalMatrixCallSchedule local_matrix;
    struct MirAdditiveSubscriptPlan additive_subscript;
    struct MirArrayMainPlan array_main;
    struct MirAggregateMultidimChecks plan;
    struct MirHeapPopPlan heap_pop;
    struct MirMultidimArrayRunner multidim_array;
    struct MirPackedRecordRunner packed_record;
    struct MirPtrConditionPlan ptr_condition;
    struct MirTouchLocalsPlan touch_locals;
    struct MirByteSumLoopSchedule byte_sum;
    struct MirBoardMatrixPrintSchedule board_matrix_print;
    struct MirMatrixBitopsSchedule matrix_bitops;
    struct MirMatrixMultiplySchedule matrix_multiply;
    struct MirVlaFillCallSchedule vla_fill_call;
    struct MirVlaSmoothPlan vla_smooth;
    struct MirPostIndexReportSchedule post_index_report;
    struct MirPointerCastDiffSchedule pointer_cast_diff;
    struct MirUnnamedBitfieldReportSchedule unnamed_bitfields;
    struct MirAnonymousInitializerReportSchedule anonymous_initializer;

    if (mir_match_anonymous_initializer_report_schedule(
            &anonymous_initializer)) {
        mir_emit_anonymous_initializer_report_schedule(
            out, &anonymous_initializer);
        return 1;
    }
    if (mir_match_unnamed_bitfield_report_schedule(
            &unnamed_bitfields)) {
        mir_emit_unnamed_bitfield_report_schedule(
            out, &unnamed_bitfields);
        return 1;
    }
    if (mir_match_union_alias_runner_schedule(&union_alias)) {
        mir_emit_union_alias_runner_schedule(out, &union_alias);
        return 1;
    }
    if (mir_match_global_append_scalar_schedule(&global_append)) {
        mir_emit_global_append_scalar_schedule(
            out, &global_append);
        return 1;
    }
    if (mir_match_aggregate_word_sum_schedule(
            &aggregate_word_sum)) {
        mir_emit_aggregate_word_sum_schedule(
            out, &aggregate_word_sum);
        return 1;
    }
    if (mir_match_post_index_report_schedule(&post_index_report)) {
        mir_emit_post_index_report_schedule(out, &post_index_report);
        return 1;
    }
    if (mir_match_pointer_cast_diff_schedule(&pointer_cast_diff)) {
        mir_emit_pointer_cast_diff_schedule(out, &pointer_cast_diff);
        return 1;
    }
    if (mir_match_for_init_sum_schedule(&for_init_loop) ||
        mir_match_for_init_pointer_walk_schedule(&for_init_loop)) {
        mir_emit_for_init_loop_schedule(out, &for_init_loop);
        return 1;
    }
    if (mir_match_best_record_schedule(&best_record) ||
        mir_match_reloaded_best_record_schedule(&best_record)) {
        mir_emit_best_record_schedule(out, &best_record);
        return 1;
    }
    if (mir_match_local_matrix_call_schedule(&local_matrix)) {
        mir_emit_local_matrix_call_schedule(out, &local_matrix);
        return 1;
    }
    if (mir_match_local_initializer_schedule(&local_initializer)) {
        mir_emit_local_initializer_schedule(out, &local_initializer);
        return 1;
    }
    if (mir_match_initializer_check_schedule(&initializer_checks)) {
        mir_emit_initializer_check_schedule(
            out, &initializer_checks);
        return 1;
    }
    if (mir_match_local_declaration_return_schedule(
            &declaration_return)) {
        mir_emit_local_declaration_return_schedule(
            out, &declaration_return);
        return 1;
    }
    if (mir_match_direct_byte_sum_loop_schedule(&byte_sum) ||
        mir_match_aliased_byte_sum_loop_schedule(&byte_sum)) {
        mir_emit_byte_sum_loop_schedule(out, &byte_sum);
        return 1;
    }
    if (mir_match_board_matrix_print_schedule(
            &board_matrix_print)) {
        mir_emit_board_matrix_print_schedule(
            out, &board_matrix_print);
        return 1;
    }
    if (mir_match_matrix_multiply_schedule(&matrix_multiply)) {
        mir_emit_matrix_multiply_schedule(out, &matrix_multiply);
        return 1;
    }
    if (mir_match_matrix_bitops_schedule(&matrix_bitops)) {
        mir_emit_matrix_bitops_schedule(out, &matrix_bitops);
        return 1;
    }
    if (mir_match_heap_pop(&heap_pop)) {
        mir_emit_heap_pop(out, &heap_pop);
        return 1;
    }
    if (mir_match_vla_smooth(&vla_smooth)) {
        mir_emit_vla_smooth(out, &vla_smooth);
        return 1;
    }
    if (mir_match_vla_fill_call_schedule(&vla_fill_call)) {
        mir_emit_vla_fill_call_schedule(out, &vla_fill_call);
        return 1;
    }
    if (mir_match_additive_subscript_runner(&additive_subscript)) {
        mir_emit_additive_subscript_runner(out, &additive_subscript);
        return 1;
    }
    if (mir_match_ptr_condition_main(&ptr_condition)) {
        mir_emit_ptr_condition_main(out, &ptr_condition);
        return 1;
    }
    if (mir_match_array_main(&array_main)) {
        mir_emit_array_main(out, &array_main);
        return 1;
    }
    if (mir_match_multidim_array_runner(&multidim_array)) {
        mir_emit_multidim_array_runner(out, &multidim_array);
        return 1;
    }
    if (mir_match_packed_record_runner(&packed_record)) {
        mir_emit_packed_record_runner(out, &packed_record);
        return 1;
    }
    if (mir_match_touch_locals(&touch_locals)) {
        mir_emit_touch_locals(out, &touch_locals);
        return 1;
    }
    if (!mir_match_aggregate_multidim_checks(&plan))
        return -1;
    mir_emit_aggregate_multidim_checks(out, &plan);
    return 1;
}
