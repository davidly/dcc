/* dcc_mir_machine_aggregate_checks.c - strict aggregate check schedules. */

#include "dcc_mir_machine_internal.h"

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
static void mir_machine_emit_global_address_hl(
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
#define MIR_TOUCH_LOCAL_MEMORY_MAX 96

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

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 319 || mir.next_value != 235 ||
        mir_cfg_block_count() != 13 || mir.local_bytes != 8 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        (mir.return_type & 15) != TYPE_VOID ||
        type_ptr_depth(mir.return_type) != 0)
        return mir_machine_reject("packed-record-runner", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
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
        !mir_packed_constant(27, 0, TYPE_INT, 0) ||
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
        !mir_packed_constant(122, 0, TYPE_INT, 0) ||
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
        mir.insns[29].src1 != mir.insns[27].dst ||
        mir.insns[93].src1 != mir.insns[92].dst ||
        mir.insns[124].src1 != mir.insns[122].dst ||
        mir.insns[306].src1 != mir.insns[305].dst ||
        !mir_packed_phi(31, 27, 92, 0, 89) ||
        !mir_packed_phi(127, 122, 305, 95, 302) ||
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
        mir.insns[138].memory_flags != 0 ||
        !mir_machine_same_location(
            &mir.insns[42], &mir.insns[138]))
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

static void mir_aggregate_cleanup(FILE *out, int words)
{
    while (words-- > 0)
        fputs("\tpop bc\n", out);
}

static void mir_aggregate_emit_format_call(
    FILE *out, struct Sym *function, const char *call_name)
{
    if (!strcmp(call_name,
                asm_name_for(sym_asm_name(function))))
        mir_machine_emit_symbol_call(out, function);
    else
        fprintf(out, "\tcall %s\n", call_name);
}

static void mir_array_main_emit_counter(
    FILE *out, int offset, int value)
{
    fprintf(out,
            "\tld (ix%+d),%d\n"
            "\tld (ix%+d),0\n",
            offset, value & 0xff, offset + 1);
}

static void mir_array_main_emit_counter_load(
    FILE *out, int offset)
{
    fprintf(out,
            "\tld l,(ix%+d)\n"
            "\tld h,(ix%+d)\n",
            offset, offset + 1);
}

static void mir_array_main_emit_counter_increment(
    FILE *out, int offset, int carry_label)
{
    fprintf(out,
            "\tinc (ix%+d)\n"
            "\tjp nz,L%d\n"
            "\tinc (ix%+d)\n"
            "L%d:\n",
            offset, carry_label, offset + 1, carry_label);
}

static void mir_array_main_emit_index_address(
    FILE *out, struct Sym *symbol, int counter_offset, int stride)
{
    mir_array_main_emit_counter_load(out, counter_offset);
    if (stride >= 2)
        fputs("\tadd hl,hl\n", out);
    if (stride >= 4)
        fputs("\tadd hl,hl\n", out);
    fputs("\tex de,hl\n", out);
    mir_machine_emit_global_address_hl(out, symbol, 0);
    fputs("\tadd hl,de\n", out);
}

static void mir_array_main_emit_byte_load(
    FILE *out, struct Sym *symbol, int counter_offset,
    int fixed_offset, int is_unsigned)
{
    if (counter_offset != 0)
        mir_array_main_emit_index_address(
            out, symbol, counter_offset, 1);
    else
        mir_machine_emit_global_address_hl(
            out, symbol, fixed_offset);
    fputs("\tld l,(hl)\n", out);
    if (is_unsigned)
        fputs("\tld h,0\n", out);
    else
        fputs("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n", out);
}

static void mir_array_main_emit_word_load(
    FILE *out, struct Sym *symbol, int counter_offset)
{
    mir_array_main_emit_index_address(
        out, symbol, counter_offset, 2);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
          out);
}

static void mir_array_main_emit_wide_load(
    FILE *out, struct Sym *symbol, int counter_offset)
{
    mir_array_main_emit_index_address(
        out, symbol, counter_offset, 4);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tinc hl\n\tld a,(hl)\n\tinc hl\n"
          "\tld h,(hl)\n\tld l,a\n\tex de,hl\n", out);
}

static void mir_array_main_emit_print_values(
    FILE *out, const struct MirArrayMainPlan *plan,
    int counter_offset, int is_unsigned, int print)
{
    int base = is_unsigned ? MIR_ARRAY_MAIN_U8 : MIR_ARRAY_MAIN_I8;

    mir_array_main_emit_wide_load(
        out, plan->symbols[base + 2], counter_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_array_main_emit_word_load(
        out, plan->symbols[base + 1], counter_offset);
    fputs("\tpush hl\n", out);
    mir_array_main_emit_byte_load(
        out, plan->symbols[base], counter_offset, 0, is_unsigned);
    fputs("\tpush hl\n", out);
    mir_array_main_emit_counter_load(out, counter_offset);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[
                is_unsigned
                    ? MIR_ARRAY_MAIN_UNSIGNED_FORMAT
                    : MIR_ARRAY_MAIN_SIGNED_FORMAT]);
    mir_emit_runtime_call(out, plan->print_names[print]);
    mir_aggregate_cleanup(out, 6);
}

static void mir_array_main_emit_store_value(
    FILE *out, struct Sym *symbol, int counter_offset,
    int stride, int bias, int subtract, int width)
{
    mir_array_main_emit_counter_load(out, counter_offset);
    fprintf(out, "\tld de,%d\n", subtract ? -bias : bias);
    fputs("\tadd hl,de\n\tpush hl\n", out);
    mir_array_main_emit_index_address(
        out, symbol, counter_offset, stride);
    fputs("\tpop de\n\tld (hl),e\n", out);
    if (width >= 2)
        fputs("\tinc hl\n\tld (hl),d\n", out);
    if (width == 4) {
        fputs("\tld a,d\n\trlca\n\tsbc a,a\n\tinc hl\n"
              "\tld (hl),a\n\tinc hl\n\tld (hl),a\n", out);
    }
}

static void mir_array_main_emit_size_check(
    FILE *out, const struct MirArrayMainPlan *plan,
    int failed, int accepted)
{
    int check;

    for (check = 0; check < 5; ++check) {
        fprintf(out,
                "\tld hl,%d\n\tld de,%d\n"
                "\tor a\n\tsbc hl,de\n\tjp nz,L%d\n",
                plan->count, plan->count, failed);
    }
    fprintf(out, "\tjp L%d\nL%d:\n\tld hl,S%d\n\tpush hl\n",
            accepted, failed,
            plan->strings[MIR_ARRAY_MAIN_SIZE_FAILURE]);
    mir_emit_runtime_call(out, plan->print_names[0]);
    mir_aggregate_cleanup(out, 1);
    fputs("\tld hl,1\n\tpush hl\n", out);
    mir_emit_runtime_call(out, plan->exit_name);
    mir_aggregate_cleanup(out, 1);
    fprintf(out, "L%d:\n", accepted);
}

static void mir_array_main_emit_character_report(
    FILE *out, const struct MirArrayMainPlan *plan)
{
    int character;

    for (character = plan->count - 1; character >= 0; --character) {
        mir_array_main_emit_byte_load(
            out, plan->symbols[MIR_ARRAY_MAIN_CHARS],
            0, character, 0);
        fputs("\tpush hl\n", out);
    }
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[MIR_ARRAY_MAIN_CHARS_FORMAT]);
    mir_emit_runtime_call(out, plan->print_names[5]);
    mir_aggregate_cleanup(out, plan->count + 1);
}

static void mir_array_main_emit_board(
    FILE *out, const struct MirArrayMainPlan *plan)
{
    int row_loop = new_label();
    int column_loop = new_label();
    int next_row = new_label();
    int board_done = new_label();
    int column_carry = new_label();
    int row_carry = new_label();

    mir_array_main_emit_counter(out, -2, 0);
    fprintf(out, "L%d:\n", row_loop);
    mir_array_main_emit_counter_load(out, -2);
    fprintf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                 "\tjp nc,L%d\n",
            plan->count, board_done);
    mir_array_main_emit_counter(out, -4, 0);
    fprintf(out, "L%d:\n", column_loop);
    mir_array_main_emit_counter_load(out, -4);
    fprintf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                 "\tjp nc,L%d\n",
            plan->board_columns, next_row);
    mir_array_main_emit_counter_load(out, -2);
    fputs("\tadd hl,hl\n\tadd hl,hl\n\tadd hl,hl\n\tpush hl\n",
          out);
    mir_array_main_emit_counter_load(out, -4);
    fputs("\tex de,hl\n\tpop hl\n\tadd hl,de\n\tex de,hl\n",
          out);
    mir_machine_emit_global_address_hl(
        out, plan->symbols[MIR_ARRAY_MAIN_BOARD], 0);
    fputs("\tadd hl,de\n\tld l,(hl)\n"
          "\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n"
          "\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[MIR_ARRAY_MAIN_BOARD_FORMAT]);
    mir_emit_runtime_call(out, plan->print_names[6]);
    mir_aggregate_cleanup(out, 2);
    mir_array_main_emit_counter_increment(
        out, -4, column_carry);
    fprintf(out, "\tjp L%d\nL%d:\n\tld hl,S%d\n\tpush hl\n",
            column_loop, next_row,
            plan->strings[MIR_ARRAY_MAIN_NEWLINE]);
    mir_emit_runtime_call(out, plan->print_names[7]);
    mir_aggregate_cleanup(out, 1);
    mir_array_main_emit_counter_increment(out, -2, row_carry);
    fprintf(out, "\tjp L%d\nL%d:\n", row_loop, board_done);
}

static void mir_array_main_emit_words(
    FILE *out, const struct MirArrayMainPlan *plan)
{
    int loop = new_label();
    int done = new_label();
    int carry = new_label();

    mir_array_main_emit_counter(out, -2, 0);
    fprintf(out, "L%d:\n", loop);
    mir_array_main_emit_counter_load(out, -2);
    fprintf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                 "\tjp nc,L%d\n",
            plan->count, done);
    mir_array_main_emit_index_address(
        out, plan->symbols[MIR_ARRAY_MAIN_WORDS], -2, 2);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tpush de\n", out);
    mir_array_main_emit_counter_load(out, -2);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[MIR_ARRAY_MAIN_WORD_FORMAT]);
    mir_emit_runtime_call(out, plan->print_names[8]);
    mir_aggregate_cleanup(out, 3);
    mir_array_main_emit_counter_increment(out, -2, carry);
    fprintf(out, "\tjp L%d\nL%d:\n", loop, done);
}

static void mir_emit_array_main(
    FILE *out, const struct MirArrayMainPlan *plan)
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

    fprintf(out,
            "%s\n"
            "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n",
            MIR_EXACT_KERNEL_MARKER, MIR_ARRAY_MAIN_FRAME_BYTES);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_array_main_emit_size_check(
        out, plan, size_failed, size_ok);

    mir_array_main_emit_counter(out, -2, 0);
    fprintf(out, "L%d:\n", first_loop);
    mir_array_main_emit_counter_load(out, -2);
    fprintf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                 "\tjp nc,L%d\n",
            plan->count, first_done);
    mir_array_main_emit_print_values(out, plan, -2, 1, 1);
    mir_array_main_emit_print_values(out, plan, -2, 0, 2);
    mir_array_main_emit_counter_increment(
        out, -2, first_carry);
    fprintf(out, "\tjp L%d\nL%d:\n", first_loop, first_done);

    mir_array_main_emit_counter(out, -2, 0);
    fprintf(out, "L%d:\n", write_loop);
    mir_array_main_emit_counter_load(out, -2);
    fprintf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n"
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
    fprintf(out, "\tjp L%d\nL%d:\n", write_loop, write_done);

    mir_array_main_emit_counter(out, -2, 0);
    fprintf(out, "L%d:\n", second_loop);
    mir_array_main_emit_counter_load(out, -2);
    fprintf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                 "\tjp nc,L%d\n",
            plan->count, second_done);
    mir_array_main_emit_print_values(out, plan, -2, 1, 3);
    mir_array_main_emit_print_values(out, plan, -2, 0, 4);
    mir_array_main_emit_counter_increment(
        out, -2, second_carry);
    fprintf(out, "\tjp L%d\nL%d:\n", second_loop, second_done);

    mir_array_main_emit_character_report(out, plan);
    mir_array_main_emit_board(out, plan);
    mir_array_main_emit_words(out, plan);
    mir_machine_emit_symbol_call(out, plan->many_function);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[MIR_ARRAY_MAIN_SUCCESS]);
    mir_emit_runtime_call(out, plan->print_names[9]);
    mir_aggregate_cleanup(out, 1);
    fputs("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static void mir_aggregate_emit_ix_store(
    FILE *out, int offset, int width, unsigned long value)
{
    if (width == 1) {
        fprintf(out, "\tld (ix%+d),%lu\n",
                offset, value & 0xffUL);
    } else if (width == 2) {
        fprintf(out,
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
    FILE *out, const struct MirAggregateCheckValue *value)
{
    if (value->kind == MIR_AGGREGATE_CHECK_CONSTANT) {
        if (value->width == 4)
            mir_machine_emit_float_bits(out, value->value);
        else
            fprintf(out, "\tld hl,%lu\n",
                    value->value & 0xffffUL);
        return;
    }
    if (value->kind == MIR_AGGREGATE_CHECK_LOCAL) {
        if (value->width == 4) {
            mir_machine_emit_ix_wide_load(
                out, value->local_offset);
        } else if (value->width == 2) {
            fprintf(out,
                    "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
                    value->local_offset,
                    value->local_offset + 1);
        } else {
            fprintf(out, "\tld l,(ix%+d)\n",
                    value->local_offset);
            if (value->is_unsigned)
                fputs("\tld h,0\n", out);
            else
                fputs("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n",
                      out);
        }
        return;
    }
    mir_machine_emit_global_address_hl(
        out, value->root, value->root_offset);
    if (value->width == 4) {
        fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tinc hl\n\tld a,(hl)\n\tinc hl\n"
              "\tld h,(hl)\n\tld l,a\n\tex de,hl\n", out);
    } else if (value->width == 2) {
        fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tex de,hl\n", out);
    } else {
        fputs("\tld l,(hl)\n", out);
        if (value->is_unsigned)
            fputs("\tld h,0\n", out);
        else
            fputs("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n",
                  out);
    }
}

static void mir_aggregate_emit_check(
    FILE *out, const struct MirAggregateCheck *check)
{
    if (check->width == 4) {
        mir_machine_emit_float_bits(out, check->expected);
        fputs("\tpush de\n\tpush hl\n", out);
        mir_aggregate_emit_value(out, &check->actual);
        fputs("\tpush de\n\tpush hl\n", out);
    } else {
        fprintf(out, "\tld hl,%lu\n\tpush hl\n",
                check->expected & 0xffffUL);
        mir_aggregate_emit_value(out, &check->actual);
        fputs("\tpush hl\n", out);
    }
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            check->string_id);
    mir_machine_emit_symbol_call(out, check->function);
    mir_aggregate_cleanup(out, check->width == 4 ? 5 : 3);
}

static void mir_aggregate_emit_wide_check(
    FILE *out, struct Sym *function, int string_id,
    unsigned long expected, int local_offset)
{
    mir_machine_emit_float_bits(out, expected);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, local_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_machine_emit_symbol_call(out, function);
    mir_aggregate_cleanup(out, 5);
}

static void mir_aggregate_emit_word_check(
    FILE *out, struct Sym *function, int string_id,
    unsigned long expected)
{
    fprintf(out, "\tld hl,%lu\n\tpush hl\n",
            expected & 0xffffUL);
    fputs("\tpush de\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_machine_emit_symbol_call(out, function);
    mir_aggregate_cleanup(out, 3);
}

static void mir_touch_local_emit_check(
    FILE *out, const struct MirTouchLocalsPlan *plan,
    const struct MirTouchLocalCheck *check)
{
    const struct MirTouchLocalStore *store =
        &plan->stores[check->store_index];

    if (check->width == 4) {
        mir_machine_emit_float_bits(out, check->expected);
        fputs("\tpush de\n\tpush hl\n", out);
        mir_machine_emit_ix_wide_load(out, store->compact_offset);
        fputs("\tpush de\n\tpush hl\n", out);
    } else {
        fprintf(out, "\tld hl,%lu\n\tpush hl\n",
                check->expected & 0xffffUL);
        if (check->width == 1) {
            fprintf(out, "\tld a,(ix%+d)\n\tld e,a\n",
                    store->compact_offset);
            if (check->is_unsigned)
                fputs("\tld d,0\n", out);
            else
                fputs("\trlca\n\tsbc a,a\n\tld d,a\n", out);
        } else {
            fprintf(out,
                    "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
                    store->compact_offset,
                    store->compact_offset + 1);
        }
        fputs("\tpush de\n", out);
    }
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            check->string_id);
    mir_machine_emit_symbol_call(out, check->function);
    mir_aggregate_cleanup(out, check->width == 4 ? 5 : 3);
}

static void mir_emit_touch_locals(
    FILE *out, const struct MirTouchLocalsPlan *plan)
{
    int item;

    fprintf(out,
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
    fputs("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static void mir_aggregate_scale_hl(FILE *out, int factor)
{
    int add;

    if (factor <= 1)
        return;
    if (factor == 2) {
        fputs("\tadd hl,hl\n", out);
        return;
    }
    fputs("\tld d,h\n\tld e,l\n", out);
    if (factor == 3) {
        fputs("\tadd hl,hl\n\tadd hl,de\n", out);
        return;
    }
    if (factor == 4) {
        fputs("\tadd hl,hl\n\tadd hl,hl\n", out);
        return;
    }
    if (factor == 6) {
        fputs("\tadd hl,hl\n\tadd hl,de\n\tadd hl,hl\n", out);
        return;
    }
    if (factor == 12) {
        fputs("\tadd hl,hl\n\tadd hl,de\n"
              "\tadd hl,hl\n\tadd hl,hl\n", out);
        return;
    }
    for (add = 1; add < factor; ++add)
        fputs("\tadd hl,de\n", out);
}

static void mir_packed_emit_record_address(
    FILE *out, int offset)
{
    int step;

    fputs("\tld l,(ix-4)\n\tld h,(ix-3)\n", out);
    for (step = 0; step < offset; ++step)
        fputs("\tinc hl\n", out);
}

static void mir_packed_emit_index_value(
    FILE *out, int scale, int negate)
{
    int factor;

    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n", out);
    for (factor = 1; factor < scale; factor *= 2)
        fputs("\tadd hl,hl\n", out);
    if (negate)
        fputs("\txor a\n\tsub l\n\tld l,a\n"
              "\tld a,0\n\tsbc a,h\n\tld h,a\n", out);
}

static void mir_packed_emit_sign_high(FILE *out)
{
    fputs("\tld a,h\n\trlca\n\tsbc a,a\n"
          "\tld e,a\n\tld d,a\n", out);
}

static void mir_packed_emit_push_index_wide(
    FILE *out, int scale, int negate)
{
    mir_packed_emit_index_value(out, scale, negate);
    if (negate)
        mir_packed_emit_sign_high(out);
    else
        fputs("\tld de,0\n", out);
    fputs("\tpush de\n\tpush hl\n", out);
}

static void mir_packed_emit_push_member_wide(
    FILE *out, int offset, int width, int is_signed)
{
    mir_packed_emit_record_address(out, offset);
    if (width == 1) {
        fputs("\tld l,(hl)\n", out);
        if (is_signed)
            fputs("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n", out);
        else
            fputs("\tld h,0\n", out);
        if (is_signed)
            mir_packed_emit_sign_high(out);
        else
            fputs("\tld de,0\n", out);
    } else if (width == 2) {
        fputs("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
              "\tld l,c\n\tld h,b\n", out);
        if (is_signed)
            mir_packed_emit_sign_high(out);
        else
            fputs("\tld de,0\n", out);
    } else {
        fputs("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
              "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tld l,c\n\tld h,b\n", out);
    }
    fputs("\tpush de\n\tpush hl\n", out);
}

static void mir_packed_emit_print_failure(
    FILE *out, const struct MirPackedRecordRunner *plan,
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
    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[check]);
    mir_emit_runtime_call(out, plan->print_call_name);
    mir_aggregate_cleanup(out, 6);
}

static void mir_packed_emit_byte_check(
    FILE *out, const struct MirPackedRecordRunner *plan,
    int check, int member, int scale, int negate, int done)
{
    mir_packed_emit_record_address(out, plan->member_offsets[member]);
    fputs("\tld b,(hl)\n\tld a,(ix-2)\n", out);
    if (scale >= 2)
        fputs("\tadd a,a\n", out);
    if (scale >= 4)
        fputs("\tadd a,a\n", out);
    if (negate)
        fputs("\tld c,a\n\txor a\n\tsub c\n", out);
    fprintf(out, "\tcp b\n\tjp z,L%d\n", done);
    mir_packed_emit_print_failure(out, plan, check);
    fprintf(out, "L%d:\n", done);
}

static void mir_packed_emit_word_check(
    FILE *out, const struct MirPackedRecordRunner *plan,
    int check, int member, int scale, int negate, int done)
{
    mir_packed_emit_record_address(out, plan->member_offsets[member]);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n", out);
    mir_packed_emit_index_value(out, scale, negate);
    fputs("\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp z,L%d\n", done);
    mir_packed_emit_print_failure(out, plan, check);
    fprintf(out, "L%d:\n", done);
}

static void mir_packed_emit_wide_check(
    FILE *out, const struct MirPackedRecordRunner *plan,
    int check, int member, int scale, int negate, int done)
{
    int mismatch = new_label();

    mir_packed_emit_index_value(out, scale, negate);
    if (negate)
        mir_packed_emit_sign_high(out);
    else
        fputs("\tld de,0\n", out);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_packed_emit_record_address(out, plan->member_offsets[member]);
    fputs("\tpop bc\n\tpop de\n"
          "\tld a,(hl)\n\tcp c\n", out);
    fprintf(out, "\tjp nz,L%d\n", mismatch);
    fputs("\tinc hl\n\tld a,(hl)\n\tcp b\n", out);
    fprintf(out, "\tjp nz,L%d\n", mismatch);
    fputs("\tinc hl\n\tld a,(hl)\n\tcp e\n", out);
    fprintf(out, "\tjp nz,L%d\n", mismatch);
    fputs("\tinc hl\n\tld a,(hl)\n\tcp d\n", out);
    fprintf(out, "\tjp z,L%d\nL%d:\n", done, mismatch);
    mir_packed_emit_print_failure(out, plan, check);
    fprintf(out, "L%d:\n", done);
}

static void mir_packed_emit_memset(
    FILE *out, struct Sym *root, int bytes)
{
    mir_machine_emit_global_address_hl(out, root, 0);
    fprintf(out, "\tld e,0\n\tld bc,%d\n", bytes);
    mir_emit_runtime_call(out, "__msf");
}

static void mir_packed_emit_record_pointer(
    FILE *out, const struct MirPackedRecordRunner *plan)
{
    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n", out);
    mir_aggregate_scale_hl(out, plan->record_stride);
    mir_machine_emit_global_address_de(out, plan->records, 0);
    fputs("\tadd hl,de\n\tld (ix-4),l\n\tld (ix-3),h\n", out);
}

static void mir_packed_emit_initializers(
    FILE *out, const struct MirPackedRecordRunner *plan)
{
    mir_packed_emit_record_address(out, plan->member_offsets[0]);
    fputs("\tld a,(ix-2)\n\tld (hl),a\n", out);

    mir_packed_emit_index_value(out, 2, 0);
    fputs("\tld c,l\n\tld b,h\n", out);
    mir_packed_emit_record_address(out, plan->member_offsets[1]);
    fputs("\tld (hl),c\n\tinc hl\n\tld (hl),b\n", out);

    mir_packed_emit_index_value(out, 4, 0);
    fputs("\tld c,l\n\tld b,h\n", out);
    mir_packed_emit_record_address(out, plan->member_offsets[2]);
    fputs("\tld (hl),c\n\tinc hl\n\tld (hl),b\n"
          "\tinc hl\n\tld (hl),0\n\tinc hl\n\tld (hl),0\n", out);

    mir_packed_emit_record_address(out, plan->member_offsets[3]);
    fputs("\tld a,(ix-2)\n\tld c,a\n\txor a\n\tsub c\n"
          "\tld (hl),a\n", out);

    mir_packed_emit_index_value(out, 2, 1);
    fputs("\tld c,l\n\tld b,h\n", out);
    mir_packed_emit_record_address(out, plan->member_offsets[4]);
    fputs("\tld (hl),c\n\tinc hl\n\tld (hl),b\n", out);

    mir_packed_emit_index_value(out, 4, 1);
    mir_packed_emit_sign_high(out);
    fputs("\tld c,l\n\tld b,h\n", out);
    mir_packed_emit_record_address(out, plan->member_offsets[5]);
    fputs("\tld (hl),c\n\tinc hl\n\tld (hl),b\n"
          "\tinc hl\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n", out);
}

static void mir_emit_packed_record_runner(
    FILE *out, const struct MirPackedRecordRunner *plan)
{
    int initialize_loop = new_label();
    int initialize_done = new_label();
    int check_loop = new_label();
    int check_done = new_label();
    int check_labels[8];
    int item;

    for (item = 0; item < 8; ++item)
        check_labels[item] = new_label();
    fprintf(out,
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
    fputs("\tld hl,0\n\tld (ix-2),l\n\tld (ix-1),h\n", out);
    fprintf(out,
            "L%d:\n\tld a,(ix-1)\n\tor a\n\tjp nz,L%d\n"
            "\tld a,(ix-2)\n\tcp %d\n\tjp nc,L%d\n",
            initialize_loop, initialize_done,
            plan->record_count, initialize_done);
    mir_packed_emit_record_pointer(out, plan);
    mir_packed_emit_initializers(out, plan);
    fprintf(out,
            "\tinc (ix-2)\n\tjp nz,L%d\n\tinc (ix-1)\n"
            "\tjp L%d\nL%d:\n",
            check_labels[0], initialize_loop,
            check_labels[0]);
    fprintf(out, "\tjp L%d\nL%d:\n",
            initialize_loop, initialize_done);

    mir_packed_emit_memset(
        out, plan->guards[0],
        plan->guard_count * plan->record_stride);
    mir_packed_emit_memset(
        out, plan->guards[1],
        plan->guard_count * plan->record_stride);
    fputs("\tld hl,0\n\tld (ix-2),l\n\tld (ix-1),h\n", out);
    fprintf(out,
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

    fprintf(out,
            "\tinc (ix-2)\n\tjp nz,L%d\n\tinc (ix-1)\n"
            "\tjp L%d\nL%d:\n",
            check_labels[7], check_loop, check_labels[7]);
    fprintf(out, "\tjp L%d\nL%d:\n", check_loop, check_done);

    fputs("\tld hl,4\n\tpush hl\n", out);
    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->record_count * plan->record_stride);
    mir_machine_emit_global_address_hl(out, plan->records, 0);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->dump_function);
    mir_aggregate_cleanup(out, 3);
    fputs("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static void mir_multidim_emit_byte_store(
    FILE *out, struct Sym *root, int offset, int value)
{
    mir_machine_emit_global_address_hl(out, root, offset);
    fprintf(out, "\tld (hl),%d\n", value & 0xff);
}

static void mir_multidim_emit_word_store(
    FILE *out, struct Sym *root, int offset, int value)
{
    mir_machine_emit_global_address_hl(out, root, offset);
    fprintf(out,
            "\tld (hl),%d\n\tinc hl\n\tld (hl),%d\n",
            value & 0xff, (value >> 8) & 0xff);
}

static void mir_multidim_emit_check_at(
    FILE *out, const struct MirMultidimArrayRunner *plan,
    int check, struct Sym *root, int offset, int width, int expected)
{
    mir_machine_emit_global_address_hl(out, root, offset);
    fputs("\tld e,(hl)\n", out);
    if (width == 1)
        fputs("\tld d,0\n", out);
    else
        fputs("\tinc hl\n\tld d,(hl)\n", out);
    mir_aggregate_emit_word_check(
        out, plan->check_function,
        plan->check_strings[check], (unsigned long)expected);
}

static void mir_multidim_emit_dynamic_2d_address(
    FILE *out, struct Sym *root, int array_offset,
    int row_offset, int column_offset,
    int row_stride, int column_stride)
{
    mir_machine_emit_global_address_hl(out, root, row_offset);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n", out);
    mir_aggregate_scale_hl(out, row_stride);
    fputs("\tpush hl\n", out);
    mir_machine_emit_global_address_hl(out, root, column_offset);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n", out);
    mir_aggregate_scale_hl(out, column_stride);
    fputs("\tpush hl\n", out);
    mir_machine_emit_global_address_hl(out, root, array_offset);
    fputs("\tpop de\n\tadd hl,de\n"
          "\tpop de\n\tadd hl,de\n", out);
}

static void mir_multidim_emit_call_2d_address(
    FILE *out, const struct MirMultidimArrayRunner *plan)
{
    mir_machine_emit_symbol_call(out, plan->row_function);
    mir_aggregate_scale_hl(out, plan->byte_row_stride);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->column_function);
    mir_aggregate_scale_hl(out, plan->byte_column_stride);
    fputs("\tpush hl\n", out);
    mir_machine_emit_global_address_hl(
        out, plan->byte_matrix, plan->byte_array_offset);
    fputs("\tpop de\n\tadd hl,de\n"
          "\tpop de\n\tadd hl,de\n", out);
}

static void mir_multidim_emit_dynamic_3d_address(
    FILE *out, const struct MirMultidimArrayRunner *plan)
{
    mir_machine_emit_global_address_hl(
        out, plan->cube, plan->cube_a_offset);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n", out);
    mir_aggregate_scale_hl(out, plan->cube_plane_stride);
    fputs("\tpush hl\n", out);
    mir_machine_emit_global_address_hl(
        out, plan->cube, plan->cube_b_offset);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n", out);
    mir_aggregate_scale_hl(out, plan->cube_row_stride);
    fputs("\tpush hl\n", out);
    mir_machine_emit_global_address_hl(
        out, plan->cube, plan->cube_d_offset);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n", out);
    mir_aggregate_scale_hl(out, plan->cube_column_stride);
    fputs("\tpush hl\n", out);
    mir_machine_emit_global_address_hl(
        out, plan->cube, plan->cube_array_offset);
    fputs("\tpop de\n\tadd hl,de\n"
          "\tpop de\n\tadd hl,de\n"
          "\tpop de\n\tadd hl,de\n", out);
}

static void mir_emit_multidim_array_runner(
    FILE *out, const struct MirMultidimArrayRunner *plan)
{
    int plane;
    int row;
    int column;
    int value;
    int success = new_label();

    fprintf(out, "%s\n", MIR_EXACT_KERNEL_MARKER);
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
            fprintf(out, "\tld (hl),%d\n", value);
            if (row != plan->byte_rows - 1 ||
                column != plan->byte_columns - 1)
                fputs("\tinc hl\n", out);
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
    fputs("\tld (hl),77\n", out);
    mir_multidim_emit_check_at(
        out, plan, 6, plan->byte_matrix,
        plan->byte_array_offset +
            2 * plan->byte_row_stride +
            plan->byte_column_stride, 1, 77);
    mir_multidim_emit_dynamic_2d_address(
        out, plan->byte_matrix, plan->byte_array_offset,
        plan->byte_row_offset, plan->byte_column_offset,
        plan->byte_row_stride, plan->byte_column_stride);
    fputs("\tld e,(hl)\n\tld d,0\n", out);
    mir_aggregate_emit_word_check(
        out, plan->check_function,
        plan->check_strings[7], 77);

    mir_multidim_emit_word_store(
        out, plan->byte_matrix, plan->byte_row_offset, 1);
    mir_multidim_emit_word_store(
        out, plan->byte_matrix, plan->byte_column_offset, 3);
    mir_multidim_emit_call_2d_address(out, plan);
    fputs("\tld (hl),55\n", out);
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
    fputs("\tld (hl),146\n\tinc hl\n\tld (hl),16\n", out);
    mir_multidim_emit_check_at(
        out, plan, 13, plan->word_matrix,
        plan->word_array_offset +
            2 * plan->word_row_stride +
            2 * plan->word_column_stride, 2, 4242);
    mir_multidim_emit_dynamic_2d_address(
        out, plan->word_matrix, plan->word_array_offset,
        plan->word_row_offset, plan->word_column_offset,
        plan->word_row_stride, plan->word_column_stride);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n", out);
    mir_aggregate_emit_word_check(
        out, plan->check_function,
        plan->check_strings[14], 4242);

    mir_machine_emit_global_address_hl(
        out, plan->cube, plan->cube_array_offset);
    for (plane = 0; plane < plan->cube_planes; ++plane)
        for (row = 0; row < plan->cube_rows; ++row)
            for (column = 0; column < plan->cube_columns; ++column) {
                value = plane * 100 + row * 10 + column;
                fprintf(out,
                        "\tld (hl),%d\n\tinc hl\n"
                        "\tld (hl),%d\n",
                        value & 0xff, (value >> 8) & 0xff);
                if (plane != plan->cube_planes - 1 ||
                    row != plan->cube_rows - 1 ||
                    column != plan->cube_columns - 1)
                    fputs("\tinc hl\n", out);
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
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n", out);
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
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", success);
    mir_machine_emit_global_word(out, plan->failures, 0);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->failure_string);
    mir_aggregate_emit_format_call(
        out, plan->print_function, plan->failure_call_name);
    mir_aggregate_cleanup(out, 2);
    fputs("\tld hl,1\n\tret\n", out);
    fprintf(out, "L%d:\n\tld hl,S%d\n\tpush hl\n",
            success, plan->success_string);
    mir_aggregate_emit_format_call(
        out, plan->print_function, plan->success_call_name);
    fputs("\tpop bc\n\tld hl,0\n\tret\n", out);
}

static void mir_emit_multidim_aggregate_checks(
    FILE *out, const struct MirAggregateMultidimChecks *plan)
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

    fprintf(out,
            "%s\n"
            "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n",
            MIR_EXACT_KERNEL_MARKER, mir.local_bytes);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->heading_string_id);
    mir_aggregate_emit_format_call(
        out, plan->print_function, plan->heading_call_name);
    fputs("\tpop bc\n", out);

    mir_machine_emit_global_address_hl(
        out, plan->multidim.board_root, 0);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(
        out, plan->multidim.board_fill_function);
    fputs("\tpop bc\n", out);

    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->multidim.rows);
    mir_machine_emit_global_address_hl(
        out, plan->multidim.board_root, 0);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(
        out, plan->multidim.board_weight_function);
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(
        out, plan->multidim.w_ptr_offset);
    mir_aggregate_emit_wide_check(
        out, plan->check_wide_function,
        plan->multidim.strings[0],
        plan->multidim.weight_expected,
        plan->multidim.w_ptr_offset);

    mir_aggregate_emit_ix_store(
        out, plan->multidim.w_struct_offset, 4, 0);
    fprintf(out, "\tld (ix%+d),0\nL%d:\n"
            "\tld (ix%+d),0\n\tld (ix%+d),0\nL%d:\n",
            plan->multidim.row_offset, loop_row,
            plan->multidim.column_offset,
            plan->multidim.column_offset + 1,
            loop_column);
    mir_machine_emit_global_address_hl(
        out, plan->multidim.board_root, 0);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld l,(ix%+d)\n\tld h,0\n",
            plan->multidim.row_offset);
    mir_aggregate_scale_hl(
        out, plan->multidim.board_row_stride);
    fputs("\tex de,hl\n\tpop hl\n\tadd hl,de\n\tpush hl\n",
          out);
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            plan->multidim.column_offset,
            plan->multidim.column_offset + 1);
    mir_aggregate_scale_hl(
        out, plan->multidim.board_column_stride);
    fputs("\tex de,hl\n\tpop hl\n\tadd hl,de\n", out);
    mir_machine_emit_hl_offset(
        out, plan->multidim.board_weight_offset, 0);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n\tld a,h\n\trlca\n\tsbc a,a\n"
          "\tld d,a\n\tld e,a\n\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(
        out, plan->multidim.w_struct_offset);
    fputs("\tpop bc\n\tadd hl,bc\n\tex de,hl\n"
          "\tpop bc\n\tadc hl,bc\n\tex de,hl\n", out);
    mir_machine_emit_ix_wide_store(
        out, plan->multidim.w_struct_offset);
    fprintf(out,
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
    fprintf(out, "L%d:\n", done);
    mir_aggregate_emit_wide_check(
        out, plan->check_wide_function,
        plan->multidim.strings[1],
        plan->multidim.weight_expected,
        plan->multidim.w_struct_offset);

    fprintf(out,
            "\tld hl,3\n\tpush hl\n"
            "\tld hl,2\n\tpush hl\n");
    mir_machine_emit_global_address_hl(
        out, plan->multidim.board_root, 0);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(
        out, plan->multidim.tile_function);
    mir_aggregate_cleanup(out, 3);
    fprintf(out,
            "\tld (ix%+d),l\n\tld (ix%+d),h\n",
            plan->multidim.tile_offset,
            plan->multidim.tile_offset + 1);
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            plan->multidim.tile_offset,
            plan->multidim.tile_offset + 1);
    mir_machine_emit_hl_offset(
        out, plan->multidim.board_weight_offset, 0);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n", out);
    mir_aggregate_emit_word_check(
        out, plan->check_word_function,
        plan->multidim.strings[2],
        plan->multidim.tile_weight_expected);
    fprintf(out,
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
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(
        out, plan->multidim.cells_checksum_function);
    fputs("\tpush de\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->multidim.strings[4]);
    mir_machine_emit_symbol_call(
        out, plan->check_wide_function);
    mir_aggregate_cleanup(out, 5);

    mir_machine_emit_global_address_hl(
        out, plan->multidim.cells_root, cells_offset);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n", out);
    mir_aggregate_emit_word_check(
        out, plan->check_word_function,
        plan->multidim.strings[5],
        plan->multidim.tag_expected);

    mir_machine_emit_symbol_call(
        out, plan->multidim.cells_checksum_function);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(
        out, plan->multidim.w_ptr_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->summary_string_id);
    mir_aggregate_emit_format_call(
        out, plan->print_function, plan->summary_call_name);
    mir_aggregate_cleanup(out, 5);
    fputs("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static void mir_size2_emit_element_address(
    FILE *out, const struct MirAggregateMultidimChecks *plan)
{
    fputs("\tpush ix\n\tpop hl\n", out);
    mir_machine_emit_hl_offset(
        out, plan->size2.lg_offset, 0);
    fputs("\tpush hl\n", out);
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            plan->size2.row_offset,
            plan->size2.row_offset + 1);
    mir_aggregate_scale_hl(out, plan->size2.row_stride);
    fputs("\tex de,hl\n\tpop hl\n\tadd hl,de\n\tpush hl\n",
          out);
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            plan->size2.column_offset,
            plan->size2.column_offset + 1);
    mir_aggregate_scale_hl(out, plan->size2.column_stride);
    fputs("\tex de,hl\n\tpop hl\n\tadd hl,de\n", out);
}

static void mir_emit_size2_aggregate_checks(
    FILE *out, const struct MirAggregateMultidimChecks *plan)
{
    const int row_loop = label_id++;
    const int column_loop = label_id++;
    const int column_incremented = label_id++;
    const int next_row = label_id++;
    const int row_incremented = label_id++;
    const int after_loop = label_id++;
    int store;
    int check;

    fprintf(out,
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
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->heading_string_id);
    mir_aggregate_emit_format_call(
        out, plan->print_function, plan->heading_call_name);
    fputs("\tpop bc\n", out);

    fprintf(out,
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->size2.row_count, plan->size2.row_count,
            plan->size2.strings[0]);
    mir_machine_emit_symbol_call(
        out, plan->check_word_function);
    mir_aggregate_cleanup(out, 3);
    fprintf(out,
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
    fprintf(out, "L%d:\n", row_loop);
    mir_aggregate_emit_ix_store(
        out, plan->size2.column_offset, 2, 0);
    fprintf(out, "L%d:\n", column_loop);
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            plan->size2.row_offset,
            plan->size2.row_offset + 1);
    mir_aggregate_scale_hl(out, plan->size2.column_count);
    fputs("\tpush hl\n", out);
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tex de,hl\n\tpop hl\n\tadd hl,de\n"
            "\tld (ix%+d),l\n\tld (ix%+d),h\n",
            plan->size2.column_offset,
            plan->size2.column_offset + 1,
            plan->size2.index_offset,
            plan->size2.index_offset + 1);

    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld de,%lu\n\tadd hl,de\n\tpush hl\n",
            plan->size2.index_offset,
            plan->size2.index_offset + 1,
            plan->size2.first_addend & 0xffffUL);
    mir_size2_emit_element_address(out, plan);
    mir_machine_emit_hl_offset(
        out, plan->size2.first_member_offset, 0);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->size2.strings[2]);
    mir_machine_emit_symbol_call(
        out, plan->check_word_function);
    mir_aggregate_cleanup(out, 3);

    fprintf(out,
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
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tinc hl\n\tld a,(hl)\n\tinc hl\n"
          "\tld h,(hl)\n\tld l,a\n\tex de,hl\n"
          "\tpush de\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->size2.strings[3]);
    mir_machine_emit_symbol_call(
        out, plan->check_wide_function);
    mir_aggregate_cleanup(out, 5);

    mir_size2_emit_element_address(out, plan);
    mir_machine_emit_hl_offset(
        out, plan->size2.second_member_offset, 0);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tinc hl\n\tld a,(hl)\n\tinc hl\n"
          "\tld h,(hl)\n\tld l,a\n\tex de,hl\n"
          "\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, plan->size2.sum_offset);
    fputs("\tpop bc\n\tadd hl,bc\n\tex de,hl\n"
          "\tpop bc\n\tadc hl,bc\n\tex de,hl\n", out);
    mir_machine_emit_ix_wide_store(out, plan->size2.sum_offset);

    fprintf(out,
            "\tinc (ix%+d)\n\tjp nz,L%d\n"
            "\tinc (ix%+d)\nL%d:\n"
            "\tld a,(ix%+d)\n\tor a\n\tjp nz,L%d\n"
            "\tld a,(ix%+d)\n\tcp %d\n\tjp c,L%d\n",
            plan->size2.column_offset, column_incremented,
            plan->size2.column_offset + 1, column_incremented,
            plan->size2.column_offset + 1, next_row,
            plan->size2.column_offset,
            plan->size2.column_count, column_loop);
    fprintf(out,
            "L%d:\n\tinc (ix%+d)\n\tjp nz,L%d\n"
            "\tinc (ix%+d)\nL%d:\n"
            "\tld a,(ix%+d)\n\tor a\n\tjp nz,L%d\n"
            "\tld a,(ix%+d)\n\tcp %d\n\tjp c,L%d\n",
            next_row, plan->size2.row_offset, row_incremented,
            plan->size2.row_offset + 1, row_incremented,
            plan->size2.row_offset + 1, after_loop,
            plan->size2.row_offset,
            plan->size2.row_count, row_loop);

    fprintf(out, "L%d:\n", after_loop);
    mir_aggregate_emit_wide_check(
        out, plan->check_wide_function, plan->size2.strings[4],
        plan->size2.sum_expected, plan->size2.sum_offset);
    for (check = 0; check < plan->size2.check_count; ++check)
        mir_aggregate_emit_check(out, &plan->size2.checks[check]);
    mir_machine_emit_ix_wide_load(out, plan->size2.sum_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->summary_string_id);
    mir_aggregate_emit_format_call(
        out, plan->print_function, plan->summary_call_name);
    mir_aggregate_cleanup(out, 3);
    fputs("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static void mir_emit_aggregate_multidim_checks(
    FILE *out, const struct MirAggregateMultidimChecks *plan)
{
    if (plan->kind == 1)
        mir_emit_multidim_aggregate_checks(out, plan);
    else
        mir_emit_size2_aggregate_checks(out, plan);
}

int mir_try_emit_aggregate_checks(FILE *out)
{
    struct MirArrayMainPlan array_main;
    struct MirAggregateMultidimChecks plan;
    struct MirMultidimArrayRunner multidim_array;
    struct MirPackedRecordRunner packed_record;
    struct MirTouchLocalsPlan touch_locals;

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
